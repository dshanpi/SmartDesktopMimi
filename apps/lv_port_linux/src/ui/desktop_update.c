#include "desktop_update.h"
#include "mimi_ui.h"
#include "ui_fonts.h"
#include <errno.h>
#include <json-c/json.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

/* Only the worker touches sockets; only the LVGL thread touches widgets. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool worker_busy;
static char response[65536];
static char action_error[256];
static char operation[16] = "status";
static lv_obj_t *overlay, *status_label, *buttons[4];
static lv_timer_t *timer;
static const char *operations[] = {"check", "download", "install", "cancel"};

static int request(const char *op, char *out, size_t cap)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "/run/smartdesktopmimi/update.sock");
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) { close(fd); return -1; }
    char header[192];
    int size = snprintf(header, sizeof(header), "%s /%s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                        strcmp(op, "status") == 0 ? "GET" : "POST", op);
    size_t sent = 0;
    while (sent < (size_t)size) {
        ssize_t n = send(fd, header + sent, (size_t)size - sent, MSG_NOSIGNAL);
        if (n <= 0) { close(fd); return -1; }
        sent += (size_t)n;
    }
    size_t used = 0;
    while (used + 1 < cap) {
        ssize_t n = recv(fd, out + used, cap - used - 1, 0);
        if (n == 0) break;
        if (n < 0) { close(fd); return -1; }
        used += (size_t)n;
    }
    close(fd); out[used] = '\0';
    if (used + 1 >= cap) return -1;
    return strncmp(out, "HTTP/1.1 2", 10) == 0 ? 0 : -2;
}

static void *worker(void *unused)
{
    (void)unused;
    char op[16], result[65536] = {0}, error[256] = {0};
    pthread_mutex_lock(&lock);
    snprintf(op, sizeof(op), "%s", operation);
    pthread_mutex_unlock(&lock);
    int rc = request(op, result, sizeof(result));
    if (strcmp(op, "status") != 0) {
        char *failure = rc == -2 ? strstr(result, "\r\n\r\n") : NULL;
        if (rc != 0) snprintf(error, sizeof(error), "%s", failure ? failure + 4 : "Update request failed. Please retry.");
        rc = request("status", result, sizeof(result));
    }
    pthread_mutex_lock(&lock);
    char *body = rc == 0 ? strstr(result, "\r\n\r\n") : NULL;
    snprintf(response, sizeof(response), "%s", body ? body + 4 : "");
    if (strcmp(op, "status") != 0) snprintf(action_error, sizeof(action_error), "%s", error);
    worker_busy = false;
    pthread_mutex_unlock(&lock);
    return NULL;
}

static void launch(const char *op)
{
    pthread_mutex_lock(&lock);
    if (!worker_busy) {
        worker_busy = true;
        snprintf(operation, sizeof(operation), "%s", op);
        pthread_t thread;
        if (pthread_create(&thread, NULL, worker, NULL) == 0) pthread_detach(thread);
        else worker_busy = false;
    }
    pthread_mutex_unlock(&lock);
}
static const char *text_field(json_object *object, const char *name)
{
    json_object *value = NULL;
    return object && json_object_object_get_ex(object, name, &value) ? json_object_get_string(value) : "";
}
static bool bool_field(json_object *object, const char *name)
{
    json_object *value = NULL;
    return object && json_object_object_get_ex(object, name, &value) && json_object_get_boolean(value);
}
static void refresh(lv_timer_t *unused)
{
    (void)unused;
    char snapshot[65536], request_error[256];
    pthread_mutex_lock(&lock);
    snprintf(snapshot, sizeof(snapshot), "%s", response);
    snprintf(request_error, sizeof(request_error), "%s", action_error);
    bool working = worker_busy;
    pthread_mutex_unlock(&lock);
    json_object *state = json_tokener_parse(snapshot), *candidate = NULL, *manifest = NULL;
    if (!state) {
        lv_label_set_text(status_label, ui_i18n_is_zh()
            ? "需要支持桌面更新的系统基线。\n更新服务尚未就绪，当前桌面保持运行。"
            : "A compatible system baseline is required.\nThe update service is not ready; your current desktop keeps running.");
    } else {
        json_object_object_get_ex(state, "candidate", &candidate);
        if (candidate) json_object_object_get_ex(candidate, "manifest", &manifest);
        lv_label_set_text_fmt(status_label, "%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s\n%s",
            ui_i18n_is_zh() ? "当前版本" : "Current version", text_field(state, "current_version"),
            ui_i18n_is_zh() ? "通道" : "Channel", text_field(state, "channel"),
            ui_i18n_is_zh() ? "状态" : "Status", text_field(state, "phase"),
            ui_i18n_is_zh() ? "可用版本" : "Available version", text_field(manifest, "version"),
            text_field(state, "error"), request_error);
    }
    bool busy = working || bool_field(state, "busy");
    bool enabled[4] = {state && !busy, candidate && !busy,
        bool_field(state, "downloaded") && !busy,
        bool_field(state, "busy") && strcmp(text_field(state, "phase"), "install") != 0 && !working};
    for (int i = 0; i < 4; i++) {
        if (enabled[i]) lv_obj_remove_state(buttons[i], LV_STATE_DISABLED);
        else lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
    }
    if (state) json_object_put(state);
    launch("status");
}
static void action(lv_event_t *event)
{
    launch((const char *)lv_event_get_user_data(event));
    for (int i = 0; i < 4; i++) lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
}
static void cleanup(lv_event_t *event)
{
    (void)event;
    if (timer) lv_timer_delete(timer);
    timer = NULL; overlay = NULL; status_label = NULL;
}
static void close_dialog(lv_event_t *event)
{
    (void)event;
    if (overlay) lv_obj_delete(overlay);
}
void desktop_update_show(void)
{
    if (overlay) return;
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_add_event_cb(overlay, cleanup, LV_EVENT_DELETE, NULL);
    lv_obj_t *panel = lv_obj_create(overlay);
    mimi_ui_surface(panel, 20);
    lv_obj_set_size(panel, 760, 520); lv_obj_center(panel);
    lv_obj_set_style_pad_all(panel, 28, 0);
    mimi_ui_text(panel, ui_i18n_is_zh() ? "桌面应用更新" : "Desktop application update", ui_font_h2(), MIMI_TEXT);
    lv_obj_t *hint = mimi_ui_text(panel, ui_i18n_is_zh()
        ? "更新桌面、应用页面和资源；系统固件单独更新。"
        : "Update desktop pages and resources. System firmware is updated separately.", ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_pos(hint, 0, 48); lv_obj_set_width(hint, lv_pct(100));
    status_label = mimi_ui_text(panel, ui_tr(UI_STR_LOADING), ui_font_body_md(), MIMI_TEXT);
    lv_obj_set_pos(status_label, 0, 106); lv_obj_set_size(status_label, lv_pct(100), 240);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    const char *zh[] = {"检查更新", "下载", "安装并重启", "取消下载"};
    const char *en[] = {"Check", "Download", "Install & restart", "Cancel download"};
    for (int i = 0; i < 4; i++) {
        buttons[i] = lv_button_create(panel); mimi_ui_button_style(buttons[i], i == 2);
        lv_obj_set_size(buttons[i], 162, 48); lv_obj_set_pos(buttons[i], i * 171, 360);
        lv_obj_t *label = mimi_ui_text(buttons[i], ui_i18n_is_zh() ? zh[i] : en[i], ui_font_body_md(), MIMI_TEXT); lv_obj_center(label);
        lv_obj_add_event_cb(buttons[i], action, LV_EVENT_CLICKED, (void *)operations[i]);
        lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
    }
    lv_obj_t *close = lv_button_create(panel); mimi_ui_button_style(close, false);
    lv_obj_set_size(close, 110, 44); lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *label = mimi_ui_text(close, ui_i18n_is_zh() ? "关闭" : "Close", ui_font_body_md(), MIMI_TEXT); lv_obj_center(label);
    lv_obj_add_event_cb(close, close_dialog, LV_EVENT_CLICKED, NULL);
    timer = lv_timer_create(refresh, 1500, NULL); launch("status");
}
