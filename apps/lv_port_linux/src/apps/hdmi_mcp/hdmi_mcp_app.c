#include "hdmi_mcp_app.h"

#include "../../system/app_manager.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"
#include "../../ui/ui_i18n.h"

#include <errno.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define AGENT_CONTROL_SOCKET "/var/run/aitvbox/control.sock"
#define AGENT_RESPONSE_MAX (96 * 1024)
#define AGENT_VISIBLE_HISTORY 24

static lv_obj_t *screen;
static lv_obj_t *state_label;
static lv_obj_t *task_label;
static lv_obj_t *history_list;
static lv_obj_t *stop_button;
static lv_timer_t *monitor_timer;
static bool previous_running;
static uint32_t history_digest;

static int write_all(int fd, const void *data, size_t length)
{
    const unsigned char *cursor = data;
    while (length > 0) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static json_object *control_request(const char *request)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return NULL;
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             AGENT_CONTROL_SOCKET);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        write_all(fd, request, strlen(request)) != 0 ||
        write_all(fd, "\n", 1) != 0) {
        close(fd);
        return NULL;
    }

    char *response = malloc(AGENT_RESPONSE_MAX + 1);
    if (!response) {
        close(fd);
        return NULL;
    }
    size_t used = 0;
    while (used < AGENT_RESPONSE_MAX) {
        ssize_t count = read(fd, response + used, AGENT_RESPONSE_MAX - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
        if (memchr(response, '\n', used)) break;
    }
    close(fd);
    response[used] = '\0';
    char *newline = memchr(response, '\n', used);
    if (newline) *newline = '\0';
    json_object *result = newline ? json_tokener_parse(response) : NULL;
    free(response);
    return result;
}

static const char *state_text(const char *state)
{
    if (!state) return ui_tr(UI_STR_MCP_OFFLINE);
    if (!strcmp(state, "idle")) return ui_tr(UI_STR_READY);
    if (!strcmp(state, "running")) return ui_tr(UI_STR_MCP_RUNNING);
    if (!strcmp(state, "stopping")) return ui_tr(UI_STR_MCP_STOPPING);
    if (!strcmp(state, "succeeded")) return ui_tr(UI_STR_MCP_SUCCEEDED);
    if (!strcmp(state, "failed")) return ui_tr(UI_STR_MCP_FAILED);
    return ui_tr(UI_STR_MCP_EMPTY);
}

static lv_color_t role_color(const char *role)
{
    if (role && !strcmp(role, "assistant"))
        return lv_color_mix(UI_COLOR_SECONDARY, UI_BG_CARD, 36);
    if (role && !strcmp(role, "tool"))
        return lv_color_mix(UI_COLOR_SUCCESS, UI_BG_CARD, 36);
    if (role && !strcmp(role, "system"))
        return lv_color_mix(UI_COLOR_WARNING, UI_BG_CARD, 36);
    return UI_BG_ELEVATED;
}

static const char *role_text(const char *role, const char *type)
{
    if (role && !strcmp(role, "assistant")) return "AI";
    if (role && !strcmp(role, "tool")) return ui_i18n_is_zh() ? "工具" : "Tool";
    if (role && !strcmp(role, "system")) return ui_i18n_is_zh() ? "系统" : "System";
    if (type && !strcmp(type, "task")) return ui_i18n_is_zh() ? "用户任务" : "User task";
    return ui_i18n_is_zh() ? "用户" : "User";
}

static uint32_t digest_text(const char *text)
{
    uint32_t value = 2166136261u;
    if (!text) return value;
    while (*text) {
        value ^= (unsigned char)*text++;
        value *= 16777619u;
    }
    return value;
}

static void rebuild_history(json_object *history)
{
    if (!history_list || !history ||
        !json_object_is_type(history, json_type_array)) return;
    const char *serialized = json_object_to_json_string_ext(
        history, JSON_C_TO_STRING_PLAIN);
    uint32_t next_digest = digest_text(serialized);
    if (next_digest == history_digest) return;
    history_digest = next_digest;
    lv_obj_clean(history_list);

    size_t count = json_object_array_length(history);
    size_t start = count > AGENT_VISIBLE_HISTORY
                       ? count - AGENT_VISIBLE_HISTORY : 0;
    for (size_t index = start; index < count; index++) {
        json_object *entry = json_object_array_get_idx(history, index);
        json_object *role_object = NULL, *type_object = NULL;
        json_object *message_object = NULL, *step_object = NULL;
        if (!entry ||
            !json_object_object_get_ex(entry, "message", &message_object) ||
            !json_object_is_type(message_object, json_type_string)) continue;
        (void)json_object_object_get_ex(entry, "role", &role_object);
        (void)json_object_object_get_ex(entry, "type", &type_object);
        (void)json_object_object_get_ex(entry, "step", &step_object);
        const char *role = role_object ? json_object_get_string(role_object) : "";
        const char *type = type_object ? json_object_get_string(type_object) : "";
        const char *message = json_object_get_string(message_object);
        int step = step_object ? json_object_get_int(step_object) : 0;

        lv_obj_t *card = lv_obj_create(history_list);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_bg_color(card, role_color(role), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 6, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *meta = lv_label_create(card);
        if (step > 0) {
            lv_label_set_text_fmt(meta, ui_i18n_is_zh() ? "%s · 步骤 %d" : "%s · Step %d",
                                  role_text(role, type), step);
        } else {
            lv_label_set_text(meta, role_text(role, type));
        }
        lv_obj_set_style_text_font(meta, UI_TEXT_BODY_MD, 0);
        lv_obj_set_style_text_color(meta, UI_TEXT_SECONDARY, 0);

        lv_obj_t *body = lv_label_create(card);
        lv_label_set_text(body, message);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, lv_pct(100));
        lv_obj_set_style_text_font(body, UI_TEXT_BODY_MD, 0);
        lv_obj_set_style_text_color(body, UI_TEXT_PRIMARY, 0);
    }
    lv_obj_scroll_to_y(history_list, LV_COORD_MAX, LV_ANIM_ON);
}

static void apply_status(json_object *status)
{
    json_object *state_object = NULL, *history = NULL;
    const char *state = NULL;
    if (!status) return;
    if (json_object_object_get_ex(status, "state", &state_object) &&
        json_object_is_type(state_object, json_type_string)) {
        state = json_object_get_string(state_object);
    }
    bool running = state && (!strcmp(state, "running") ||
                             !strcmp(state, "stopping"));

    if (state_label) {
        lv_label_set_text(state_label, state_text(state));
        lv_obj_set_style_text_color(
            state_label,
            running ? UI_COLOR_SUCCESS : UI_TEXT_SECONDARY, 0);
    }
    if (stop_button) {
        if (running) lv_obj_clear_state(stop_button, LV_STATE_DISABLED);
        else lv_obj_add_state(stop_button, LV_STATE_DISABLED);
    }
    if (json_object_object_get_ex(status, "history", &history) &&
        json_object_is_type(history, json_type_array)) {
        size_t count = json_object_array_length(history);
        for (size_t index = 0; index < count; index++) {
            json_object *entry = json_object_array_get_idx(history, index);
            json_object *type = NULL, *message = NULL;
            if (entry &&
                json_object_object_get_ex(entry, "type", &type) &&
                !strcmp(json_object_get_string(type), "task") &&
                json_object_object_get_ex(entry, "message", &message)) {
                if (task_label)
                    lv_label_set_text(task_label,
                                      json_object_get_string(message));
                break;
            }
        }
        rebuild_history(history);
    }

    if (running && !previous_running) {
        app_manager_open(APP_ID_HDMI_MCP);
    }
    previous_running = running;
}

static void monitor_cb(lv_timer_t *timer)
{
    (void)timer;
    json_object *status = control_request("{\"command\":\"status\"}");
    if (status) {
        apply_status(status);
        json_object_put(status);
    } else if (state_label) {
        lv_label_set_text(state_label, ui_tr(UI_STR_MCP_OFFLINE));
    }
}

void hdmi_mcp_app_monitor_start(void)
{
    if (monitor_timer) return;
    monitor_timer = lv_timer_create(monitor_cb, 500, NULL);
    lv_timer_ready(monitor_timer);
}

static void back_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        app_manager_back_home();
}

static void stop_confirmed(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    json_object *reply = control_request("{\"command\":\"stop\"}");
    if (reply) json_object_put(reply);
    lv_obj_t *box = lv_event_get_user_data(event);
    if (box) lv_obj_delete_async(box);
}

static void stop_cancelled(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *box = lv_event_get_user_data(event);
    if (box) lv_obj_delete_async(box);
}

static void stop_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *box = lv_msgbox_create(lv_layer_top());
    lv_msgbox_add_title(box, ui_i18n_is_zh() ? "停止当前任务？" : "Stop current task?");
    lv_msgbox_add_text(box, ui_i18n_is_zh()
                       ? "停止后会立即释放所有自动控制输入。"
                       : "Stopping immediately releases automated input.");
    lv_obj_t *confirm = lv_msgbox_add_footer_button(box, ui_tr(UI_STR_STOP));
    lv_obj_t *cancel = lv_msgbox_add_footer_button(box, ui_tr(UI_STR_CANCEL));
    lv_obj_add_event_cb(confirm, stop_confirmed, LV_EVENT_CLICKED, box);
    lv_obj_add_event_cb(cancel, stop_cancelled, LV_EVENT_CLICKED, box);
}

static lv_obj_t *create_action(lv_obj_t *parent, const char *text,
                               lv_event_cb_t callback, bool danger)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, lv_pct(100), 52);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_bg_color(
        button, danger ? UI_COLOR_ERROR : UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xA8B2C3),
                              LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

void hdmi_mcp_app_init(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, UI_BG_DESKTOP, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_app_header(screen, ui_tr(UI_STR_APP_HDMI_MCP), back_event, false, NULL);

    lv_obj_t *content = lv_obj_create(screen);
    int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    int32_t screen_h = lv_display_get_vertical_resolution(NULL);
    int32_t content_w = LV_MIN(912, screen_w - 48);
    lv_obj_set_size(content, content_w, screen_h - UI_APP_HEADER_H - 48);
    lv_obj_set_pos(content, (screen_w - content_w) / 2, UI_APP_HEADER_H + 24);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_column(content, 24, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *summary = lv_obj_create(content);
    lv_obj_set_size(summary, 288, lv_pct(100));
    lv_obj_set_style_radius(summary, 22, 0);
    lv_obj_set_style_bg_color(summary, UI_BG_CARD, 0);
    lv_obj_set_style_bg_opa(summary, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(summary, 0, 0);
    lv_obj_set_style_pad_all(summary, 24, 0);
    lv_obj_set_style_pad_row(summary, 14, 0);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *eyebrow = lv_label_create(summary);
    lv_label_set_text(eyebrow, ui_tr(UI_STR_MCP_CURRENT_TASK));
    lv_obj_set_style_text_font(eyebrow, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(eyebrow, UI_TEXT_SECONDARY, 0);

    state_label = lv_label_create(summary);
    lv_label_set_text(state_label, ui_tr(UI_STR_MCP_SYNCING));
    lv_obj_set_style_text_font(state_label, UI_TEXT_H2, 0);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(state_label, lv_pct(100));

    task_label = lv_label_create(summary);
    lv_label_set_text(task_label, ui_tr(UI_STR_MCP_EMPTY));
    lv_label_set_long_mode(task_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(task_label, lv_pct(100));
    lv_obj_set_style_text_font(task_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(task_label, UI_TEXT_PRIMARY, 0);
    lv_obj_set_flex_grow(task_label, 1);

    stop_button = create_action(summary, ui_tr(UI_STR_STOP), stop_event, true);
    lv_obj_add_state(stop_button, LV_STATE_DISABLED);
    create_action(summary, ui_tr(UI_STR_HOME), back_event, false);

    lv_obj_t *timeline = lv_obj_create(content);
    lv_obj_set_flex_grow(timeline, 1);
    lv_obj_set_height(timeline, lv_pct(100));
    lv_obj_set_style_radius(timeline, 22, 0);
    lv_obj_set_style_bg_color(timeline, UI_BG_CARD, 0);
    lv_obj_set_style_bg_opa(timeline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(timeline, 0, 0);
    lv_obj_set_style_pad_all(timeline, 24, 0);
    lv_obj_set_style_pad_row(timeline, 12, 0);
    lv_obj_set_flex_flow(timeline, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(timeline, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(timeline);
    lv_label_set_text(title, ui_tr(UI_STR_MCP_HISTORY));
    lv_obj_set_style_text_font(title, UI_TEXT_H2, 0);
    lv_obj_set_style_text_color(title, UI_TEXT_PRIMARY, 0);

    history_list = lv_obj_create(timeline);
    lv_obj_set_width(history_list, lv_pct(100));
    lv_obj_set_flex_grow(history_list, 1);
    lv_obj_set_style_bg_opa(history_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(history_list, 0, 0);
    lv_obj_set_style_pad_all(history_list, 0, 0);
    lv_obj_set_style_pad_row(history_list, 10, 0);
    lv_obj_set_flex_flow(history_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(history_list, LV_DIR_VER);

    history_digest = 0;
    lv_scr_load(screen);
    monitor_cb(NULL);
}

void hdmi_mcp_app_close(void)
{
    screen = NULL;
    state_label = NULL;
    task_label = NULL;
    history_list = NULL;
    stop_button = NULL;
    history_digest = 0;
}

AppDescriptor app_hdmi_mcp = {
    .id = APP_ID_HDMI_MCP,
    .name = "HDMI MCP",
    .init = hdmi_mcp_app_init,
    .close = hdmi_mcp_app_close,
};
