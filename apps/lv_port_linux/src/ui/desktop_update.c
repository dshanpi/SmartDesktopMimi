#include "desktop_update.h"
#include "mimi_ui.h"
#include "ui_fonts.h"
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <json-c/json.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/* One deadline spans connect, send and every receive, including slow peers. */
static long long request_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int request_wait(int fd, short events, long long deadline)
{
    for (;;) {
        long long now = request_now_ms();
        if (now < 0 || now >= deadline) return -1;
        struct pollfd pending = {.fd = fd, .events = events};
        int rc = poll(&pending, 1, (int)(deadline - now));
        if (rc > 0) return (pending.revents & POLLNVAL) ? -1 : 0;
        if (rc == 0 || errno != EINTR) return -1;
    }
}

/* The local updater sends bounded, close-delimited responses, with an
 * explicit length for status. Chunked coding is not part of this contract. */
static int response_status(const char *out, size_t used)
{
    if (used < 16 || memchr(out, '\0', used)) return -1;
    const char *end = strstr(out, "\r\n\r\n");
    const char *line = strstr(out, "\r\n");
    if (!end || !line || line > end || line - out < 13 ||
        (strncmp(out, "HTTP/1.1 ", 9) && strncmp(out, "HTTP/1.0 ", 9)) ||
        out[9] < '1' || out[9] > '5' || out[10] < '0' || out[10] > '9' ||
        out[11] < '0' || out[11] > '9' || out[12] != ' ') return -1;
    size_t declared = 0;
    int have_length = 0;
    const char *cursor = line + 2;
    while (cursor < end) {
        line = strstr(cursor, "\r\n");
        if (!line || line > end) return -1;
        const char *colon = memchr(cursor, ':', (size_t)(line - cursor));
        if (!colon || colon == cursor) return -1;
        for (const char *p = cursor; p < colon; ++p) {
            unsigned char c = (unsigned char)*p;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c))) return -1;
        }
        for (const char *p = colon + 1; p < line; ++p)
            if (((unsigned char)*p < 32 && *p != '\t') || *p == 127) return -1;
        size_t name_length = (size_t)(colon - cursor);
        if (name_length == 17 && !strncasecmp(cursor, "Transfer-Encoding", 17)) return -1;
        if (name_length == 14 && !strncasecmp(cursor, "Content-Length", 14)) {
            if (have_length) return -1;
            have_length = 1;
            const char *value = colon + 1;
            while (value < line && (*value == ' ' || *value == '\t')) ++value;
            if (value == line || *value < '0' || *value > '9') return -1;
            while (value < line && *value >= '0' && *value <= '9') {
                size_t digit = (size_t)(*value++ - '0');
                if (declared > used / 10 || declared * 10 + digit > used) return -1;
                declared = declared * 10 + digit;
            }
            while (value < line && (*value == ' ' || *value == '\t')) ++value;
            if (value != line) return -1;
        }
        cursor = line + 2;
    }
    if (have_length && declared != used - (size_t)(end + 4 - out)) return -1;
    return out[9] == '2' ? 0 : -2;
}

static int request(const char *op, char *out, size_t cap)
{
    long long started = request_now_ms();
    if (started < 0) return -1;
    long long deadline = started + 3000;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "/run/smartdesktopmimi/update.sock");
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (errno != EINPROGRESS || request_wait(fd, POLLOUT, deadline) < 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
            close(fd); return -1;
        }
    }
    char header[192];
    int size = snprintf(header, sizeof(header), "%s /%s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                        strcmp(op, "status") == 0 ? "GET" : "POST", op);
    size_t sent = 0;
    while (sent < (size_t)size) {
        if (request_wait(fd, POLLOUT, deadline) < 0) { close(fd); return -1; }
        ssize_t n = send(fd, header + sent, (size_t)size - sent, MSG_NOSIGNAL);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) { close(fd); return -1; }
        sent += (size_t)n;
    }
    size_t used = 0;
    while (used + 1 < cap) {
        if (request_wait(fd, POLLIN, deadline) < 0) { close(fd); return -1; }
        ssize_t n = recv(fd, out + used, cap - used - 1, 0);
        if (n == 0) break;
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0) { close(fd); return -1; }
        used += (size_t)n;
    }
    close(fd); out[used] = '\0';
    if (used + 1 >= cap) return -1;
    return response_status(out, used);
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
    if (!object || !json_object_is_type(object, json_type_object) ||
        !json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_string)) return "";
    return json_object_get_string(value);
}
static bool bool_field(json_object *object, const char *name)
{
    json_object *value = NULL;
    return object && json_object_is_type(object, json_type_object) &&
        json_object_object_get_ex(object, name, &value) &&
        json_object_is_type(value, json_type_boolean) && json_object_get_boolean(value);
}
static bool status_field(json_object *object, const char *name, enum json_type type)
{
    json_object *value = NULL;
    if (!json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, type)) return false;
    /* C labels and phase comparisons must not see a truncated JSON string. */
    return type != json_type_string ||
        strlen(json_object_get_string(value)) == (size_t)json_object_get_string_len(value);
}
static json_object *parse_status(const char *snapshot)
{
    struct json_tokener *tok = json_tokener_new();
    if (!tok) return NULL;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT);
    json_object *state = json_tokener_parse_ex(tok, snapshot, (int)strlen(snapshot));
    bool parsed = json_tokener_get_error(tok) == json_tokener_success;
    json_tokener_free(tok);
    json_object *candidate = NULL, *manifest = NULL;
    if (!parsed || !state || !json_object_is_type(state, json_type_object)) goto invalid;
    if (!status_field(state, "busy", json_type_boolean) ||
        !status_field(state, "downloaded", json_type_boolean) ||
        !status_field(state, "phase", json_type_string) ||
        !status_field(state, "current_version", json_type_string) ||
        !status_field(state, "channel", json_type_string) ||
        !status_field(state, "error", json_type_string) ||
        !json_object_object_get_ex(state, "candidate", &candidate)) goto invalid;
    if (candidate && (!json_object_is_type(candidate, json_type_object) ||
        !json_object_object_get_ex(candidate, "manifest", &manifest) ||
        !manifest || !json_object_is_type(manifest, json_type_object) ||
        !status_field(manifest, "version", json_type_string) ||
        !*text_field(manifest, "version"))) goto invalid;
    /* An archive without a candidate cannot identify what would be installed. */
    if (bool_field(state, "downloaded") && !candidate) goto invalid;
    return state;
invalid:
    if (state) json_object_put(state);
    return NULL;
}
static bool can_cancel(json_object *state)
{
    const char *phase = text_field(state, "phase");
    return bool_field(state, "busy") &&
        (strcmp(phase, "check") == 0 || strcmp(phase, "download") == 0);
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
    json_object *state = parse_status(snapshot), *candidate = NULL, *manifest = NULL;
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
        can_cancel(state) && !working};
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
