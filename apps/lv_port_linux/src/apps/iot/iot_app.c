#include "iot_app.h"
#include "iot_client.h"
#include "iot_json.h"
#include "../../system/app_manager.h"
#include "../../ui/mimi_ui.h"
#include "../../ui/ui_fonts.h"
#include <json-c/json.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Network workers own only copied request/response data. The LVGL thread owns
 * all widgets. A generation prevents a late response from reopening a page. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static bool busy, ready, active;
static unsigned generation, job_generation;
static int job_status;
static char job_path[320], job_body[2048], job_result[131072];
static lv_obj_t *body, *message, *content, *refresh, *modal;
static lv_timer_t * timer;
static json_object * devices;
static char selected_id[129], selected_name[256];
static bool selected_online, selected_controls;
/* LVGL-thread-only aggregate. No identifiers or response text survive here. */
static mimi_iot_summary summary;
mimi_iot_summary mimi_iot_summary_get(void)
{
    if(summary.state == MIMI_IOT_SUMMARY_READY && lv_tick_elaps(summary.received_at) >= 60000U)
        summary.state = MIMI_IOT_SUMMARY_STALE;
    mimi_iot_summary result = summary;
    if(result.state != MIMI_IOT_SUMMARY_READY) result.total = result.online = 0;
    return result;
}
static const char * tr(const char * z, const char * e)
{
    return ui_i18n_is_zh() ? z : e;
}
static const char * field(json_object * o, const char * k)
{
    json_object * v = NULL;
    return o && json_object_object_get_ex(o, k, &v) && json_object_get_type(v) == json_type_string
               ? json_object_get_string(v)
               : "";
}
static bool boolean(json_object * o, const char * k)
{
    json_object * v = NULL;
    return o && json_object_object_get_ex(o, k, &v) && json_object_get_type(v) == json_type_boolean &&
           json_object_get_boolean(v);
}
static bool identifier(const char * s)
{
    if(!*s || strlen(s) > 128) return false;
    for(; *s; s++)
        if(!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' ||
             *s == '-'))
            return false;
    return true;
}
static void * worker(void * unused)
{
    (void)unused;
    char path[320], command[2048];
    pthread_mutex_lock(&mutex);
    strcpy(path, job_path);
    strcpy(command, job_body);
    pthread_mutex_unlock(&mutex);
    char * result = calloc(1, sizeof(job_result));
    int rc        = result ? mimi_iot_request(path, *command ? command : NULL, result, sizeof(job_result)) : -1;
    pthread_mutex_lock(&mutex);
    snprintf(job_result, sizeof(job_result), "%s", result ? result : "");
    job_status = rc;
    busy       = false;
    ready      = true;
    pthread_mutex_unlock(&mutex);
    free(result);
    return NULL;
}
static void launch(const char * path, const char * command)
{
    pthread_mutex_lock(&mutex);
    if(busy) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    ready          = false;
    busy           = true;
    job_generation = generation;
    snprintf(job_path, sizeof(job_path), "%s", path);
    snprintf(job_body, sizeof(job_body), "%s", command ? command : "");
    pthread_t thread;
    if(pthread_create(&thread, NULL, worker, NULL) == 0)
        pthread_detach(thread);
    else {
        busy          = false;
        ready         = true;
        job_status    = -1;
        job_result[0] = 0;
    }
    pthread_mutex_unlock(&mutex);
    if(command && *command) summary.state = MIMI_IOT_SUMMARY_STALE;
    lv_obj_set_style_max_height(message, LV_COORD_MAX, 0);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_label_set_text(message, tr("正在连接…", "Connecting…"));
    lv_obj_add_state(refresh, LV_STATE_DISABLED);
}
static bool is_busy(void)
{
    pthread_mutex_lock(&mutex);
    bool result = busy;
    pthread_mutex_unlock(&mutex);
    return result;
}
static void fetch_devices(lv_event_t * event)
{
    (void)event;
    if(modal) return;
    if(is_busy()) {
        lv_label_set_text(message, tr("正在等待上一请求结束…", "Waiting for the previous request to finish…"));
        lv_obj_add_state(refresh, LV_STATE_DISABLED);
        return;
    }
    selected_id[0] = 0;
    lv_obj_clean(content);
    launch("/v1/devices", NULL);
}
static void close_modal(lv_event_t * e)
{
    (void)e;
    if(modal) {
        lv_obj_delete(modal);
        modal = NULL;
    }
}
static lv_obj_t * button(lv_obj_t * parent, const char * text, lv_event_cb_t cb, void * data)
{
    lv_obj_t * b = lv_button_create(parent);
    mimi_ui_button_style(b, true);
    lv_obj_set_size(b, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(b, 56, 0);
    lv_obj_t * l = mimi_ui_text(b, text, ui_font_body_md(), MIMI_TEXT);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_max_height(l, 2 * ui_font_body_md()->line_height, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    return b;
}
struct control
{
    char code[129], type[24];
    lv_obj_t * input;
};
static struct control controls[24];
static unsigned control_count;
static char pending[2048];
static void send_confirmed(lv_event_t * e)
{
    (void)e;
    if(is_busy() || !modal || !identifier(selected_id) || !selected_online || !selected_controls || !pending[0])
        return;
    char path[320];
    snprintf(path, sizeof(path), "/v1/devices/%s/commands", selected_id);
    close_modal(NULL);
    launch(path, pending);
    memset(pending, 0, sizeof(pending));
}
static void confirm(lv_event_t * e)
{
    if(is_busy() || modal) return;
    struct control * c = lv_event_get_user_data(e);
    json_object *cmd = json_object_new_object(), *value = NULL;
    if(!strcmp(c->type, "Boolean"))
        value = json_object_new_boolean(lv_obj_has_state(c->input, LV_STATE_CHECKED));
    else if(!strcmp(c->type, "Integer"))
        value = json_object_new_int(lv_spinbox_get_value(c->input));
    else {
        char option[256];
        lv_dropdown_get_selected_str(c->input, option, sizeof(option));
        value = json_object_new_string(option);
    }
    json_object_object_add(cmd, "code", json_object_new_string(c->code));
    json_object_object_add(cmd, "value", value);
    snprintf(pending, sizeof(pending), "%s", json_object_to_json_string_ext(cmd, JSON_C_TO_STRING_PLAIN));
    json_object_put(cmd);
    modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, lv_pct(88), LV_SIZE_CONTENT);
    lv_obj_center(modal);
    mimi_ui_surface(modal, 18);
    lv_obj_set_style_max_height(modal, lv_display_get_vertical_resolution(NULL) - 48, 0);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(modal, LV_DIR_VER);
    lv_obj_set_style_pad_all(modal, 24, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(modal, 14, 0);
    lv_obj_t * l = mimi_ui_text(modal, tr("确认控制设备", "Confirm device command"), ui_font_h2(), MIMI_TEXT);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    l = mimi_ui_text(modal, selected_name, ui_font_body_lg(), MIMI_TEXT);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    l = mimi_ui_text(modal, pending, ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    button(modal, tr("确认发送", "Send command"), send_confirmed, NULL);
    button(modal, tr("取消", "Cancel"), close_modal, NULL);
}
static void spin(lv_event_t * e)
{
    struct control * c = lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT || lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t * l = lv_obj_get_child(lv_event_get_target(e), 0);
        if(!strcmp(lv_label_get_text(l), "+"))
            lv_spinbox_increment(c->input);
        else
            lv_spinbox_decrement(c->input);
    }
}
static void show_functions(json_object * root)
{
    lv_obj_clean(content);
    control_count       = 0;
    json_object * items = NULL;
    selected_controls   = boolean(root, "control_allowed");
    if(!json_object_object_get_ex(root, "functions", &items) || json_object_get_type(items) != json_type_array) {
        lv_label_set_text(message, tr("设备指令数据不可用", "Device command data is unavailable"));
        return;
    }
    /* Keep controls reachable; the confirmation shows the complete name. */
    lv_obj_set_style_max_height(message, 3 * ui_font_body_lg()->line_height, 0);
    lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
    lv_label_set_text(message, selected_name);
    for(size_t i = 0; i < json_object_array_length(items) && control_count < 24; i++) {
        json_object * f  = json_object_array_get_idx(items, i);
        const char *code = field(f, "code"), *type = field(f, "type");
        if(!identifier(code) || (strcmp(type, "Boolean") && strcmp(type, "Integer") && strcmp(type, "Enum"))) continue;
        struct control * c = &controls[control_count];
        snprintf(c->code, sizeof(c->code), "%s", code);
        snprintf(c->type, sizeof(c->type), "%s", type);
        json_object * schema = json_tokener_parse(field(f, "values"));
        if(!schema) continue;
        lv_obj_t * card = lv_obj_create(content);
        mimi_ui_surface(card, 16);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(card, 16, 0);
        lv_obj_set_style_pad_row(card, 12, 0);
        lv_obj_t * caption = mimi_ui_text(card, code, ui_font_body_lg(), MIMI_TEXT);
        lv_obj_set_width(caption, lv_pct(100));
        lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
        if(!strcmp(type, "Boolean")) {
            c->input = lv_switch_create(card);
        } else if(!strcmp(type, "Integer")) {
            json_object *jmin = NULL, *jmax = NULL, *jstep = NULL;
            int64_t min, max, step;
            if(!json_object_object_get_ex(schema, "min", &jmin) || !json_object_object_get_ex(schema, "max", &jmax) ||
               !json_object_object_get_ex(schema, "step", &jstep) || (min = json_object_get_int64(jmin)) < INT32_MIN ||
               (max = json_object_get_int64(jmax)) > INT32_MAX || (step = json_object_get_int64(jstep)) < 1 ||
               step > INT32_MAX || min > max) {
                json_object_put(schema);
                lv_obj_delete(card);
                continue;
            }
            c->input = lv_spinbox_create(card);
            lv_spinbox_set_range(c->input, (int32_t)min, (int32_t)max);
            lv_spinbox_set_value(c->input, (int32_t)min);
            lv_spinbox_set_step(c->input, (uint32_t)step);
            button(card, "-", spin, c);
            button(card, "+", spin, c);
        } else {
            json_object * range = NULL;
            char options[2048]  = {0};
            bool valid          = json_object_object_get_ex(schema, "range", &range) &&
                         json_object_get_type(range) == json_type_array && json_object_array_length(range) > 0;
            size_t used = 0;
            if(valid)
                for(size_t j = 0; j < json_object_array_length(range); j++) {
                    json_object * o = json_object_array_get_idx(range, j);
                    const char * s  = json_object_get_string(o);
                    size_t n        = s ? strlen(s) : 0;
                    if(json_object_get_type(o) != json_type_string || !n || n >= 256 || strchr(s, '\n') ||
                       used + n + 2 >= sizeof(options)) {
                        valid = false;
                        break;
                    }
                    if(used) options[used++] = '\n';
                    memcpy(options + used, s, n + 1);
                    used += n;
                }
            if(!valid) {
                json_object_put(schema);
                lv_obj_delete(card);
                continue;
            }
            c->input = lv_dropdown_create(card);
            lv_obj_set_width(c->input, lv_pct(100));
            lv_dropdown_set_options(c->input, options);
        }
        json_object_put(schema);
        lv_obj_t * b = button(card, tr("设置此项…", "Set value…"), confirm, c);
        if(!selected_online || !selected_controls) lv_obj_add_state(b, LV_STATE_DISABLED);
        control_count++;
    }
    if(!control_count)
        mimi_ui_text(content, tr("此设备暂时没有可用的控制项", "No supported controls for this device"),
                     ui_font_body_lg(), MIMI_MUTED);
    if(!selected_controls)
        mimi_ui_text(content, tr("当前授权为只读", "This device is read-only for your account"), ui_font_body_md(),
                     MIMI_MUTED);
    button(content, tr("返回设备列表", "Back to devices"), fetch_devices, NULL);
}
static void select_device(lv_event_t * e)
{
    if(is_busy() || modal) return;
    size_t i        = (size_t)(uintptr_t)lv_event_get_user_data(e);
    json_object * d = json_object_array_get_idx(devices, i);
    const char * id = field(d, "id");
    if(!identifier(id)) return;
    snprintf(selected_id, sizeof(selected_id), "%s", id);
    snprintf(selected_name, sizeof(selected_name), "%s", field(d, "name"));
    selected_online = boolean(d, "online");
    char path[320];
    snprintf(path, sizeof(path), "/v1/devices/%s/functions", selected_id);
    launch(path, NULL);
}
static bool summarize_devices(json_object *items)
{
    json_object *ids = json_object_new_object();
    if(!ids) return false;
    unsigned total = (unsigned)json_object_array_length(items), online = 0;
    bool valid = true;
    for(unsigned i=0; i<total; i++) {
        json_object *d=json_object_array_get_idx(items,i), *connected=NULL, *duplicate=NULL;
        if(!d || json_object_get_type(d)!=json_type_object) { valid=false;break; }
        const char *id=field(d,"id");
        if(!identifier(id) || json_object_object_get_ex(ids,id,&duplicate) ||
           !json_object_object_get_ex(d,"online",&connected) || json_object_get_type(connected)!=json_type_boolean) {
            valid=false;break;
        }
        json_object_object_add(ids,id,json_object_new_boolean(true));
        if(json_object_get_boolean(connected)) online++;
    }
    json_object_put(ids);
    if(valid) summary=(mimi_iot_summary){MIMI_IOT_SUMMARY_READY,total,online,lv_tick_get()};
    return valid;
}
static void show_devices(json_object * root)
{
    json_object * items = NULL;
    if(!json_object_object_get_ex(root, "devices", &items) || json_object_get_type(items) != json_type_array ||
       json_object_array_length(items) > 2000 || !summarize_devices(items)) {
        summary.state = MIMI_IOT_SUMMARY_UNAVAILABLE;
        lv_obj_clean(content);
        lv_label_set_text(message, tr("设备列表不可用", "Device list unavailable"));
        return;
    }
    if(devices) json_object_put(devices);
    devices = json_object_get(items);
    lv_obj_clean(content);
    lv_label_set_text(message,
                      tr("设备状态已刷新；点击设备查看控制项", "Status refreshed. Select a device to view controls."));
    size_t total = json_object_array_length(devices);
    if(!total)
        mimi_ui_text(content,
                     tr("没有已授权设备，请先在智能生活中添加设备并授权账号",
                        "No authorized devices. Add devices in Smart Life and authorize the account."),
                     ui_font_body_lg(), MIMI_MUTED);
    for(size_t i = 0; i < total && i < 100; i++) {
        json_object * d = json_object_array_get_idx(devices, i);
        char text[600];
        snprintf(text, sizeof(text), "%s  ·  %s", field(d, "name"),
                 boolean(d, "online") ? tr("在线", "Online") : tr("离线", "Offline"));
        button(content, text, select_device, (void *)(uintptr_t)i);
        json_object * status = NULL;
        if(json_object_object_get_ex(d, "status", &status) && json_object_get_type(status) == json_type_array) {
            for(size_t j = 0; j < json_object_array_length(status) && j < 4; j++) {
                json_object *p = json_object_array_get_idx(status, j), *value = NULL;
                if(!json_object_object_get_ex(p, "value", &value)) continue;
                snprintf(text, sizeof(text), "%s: %.250s", field(p, "code"),
                         json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN));
                lv_obj_t * l = mimi_ui_text(content, text, ui_font_body_md(), MIMI_MUTED);
                lv_obj_set_width(l, lv_pct(100));
                lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            }
        }
    }
    if(total > 100)
        mimi_ui_text(content, tr("仅显示前 100 个设备", "Showing the first 100 devices"), ui_font_body_md(),
                     MIMI_MUTED);
}
static void tick(lv_timer_t * t)
{
    (void)t;
    if(!active) return;
    pthread_mutex_lock(&mutex);
    if(!ready || busy) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    ready         = false;
    unsigned gen  = job_generation;
    int rc        = job_status;
    char * result = strdup(job_result);
    char path[320];
    strcpy(path, job_path);
    pthread_mutex_unlock(&mutex);
    if(gen != generation) {
        free(result);
        fetch_devices(NULL);
        return;
    }
    lv_obj_remove_state(refresh, LV_STATE_DISABLED);
    json_object * root = result ? mimi_iot_json_object(result, strlen(result)) : NULL;
    free(result);
    if(rc || !root || (strstr(path, "/commands") && !mimi_iot_command_accepted(root))) {
        const char * error = field(root, "error");
        summary.state = !strcmp(error,"not_configured") ? MIMI_IOT_SUMMARY_NOT_CONFIGURED
            : !strcmp(error,"authorization_required") ? MIMI_IOT_SUMMARY_UNAUTHORIZED
            : MIMI_IOT_SUMMARY_UNAVAILABLE;
        if(strstr(path, "/commands")) {
            /* An unknown outcome may already have changed the real device. */
            lv_obj_clean(content);
            button(content, tr("刷新设备状态", "Refresh device status"), fetch_devices, NULL);
        }
        lv_label_set_text(message,
                          !strcmp(error, "not_configured")
                              ? tr("尚未连接智家账号，请由管理员配置受限授权",
                                   "Smart Home is not connected. Ask the administrator to configure account access.")
                              : !strcmp(error, "authorization_required")
                                    ? tr("授权已失效或权限不足，请重新授权",
                                         "Authorization expired or permission denied. Reauthorize the account.")
                                    : strstr(path, "/commands")
                                          ? tr("未确认指令受理结果。请先刷新设备状态，避免重复操作。",
                                               "Command acceptance is unconfirmed. Refresh device state before retrying.")
                                          : tr("连接失败或设备数据无效，请刷新后重试",
                                               "Connection failed or invalid device data. Refresh before retrying."));
    } else if(strstr(path, "/commands")) {
        lv_obj_clean(content);
        lv_label_set_text(message, tr("指令已受理，尚未确认设备状态。请刷新核对结果。",
                                      "Command accepted; device state is not confirmed. Refresh to check the result."));
        button(content, tr("刷新设备状态", "Refresh device status"), fetch_devices, NULL);
    } else if(strstr(path, "/functions"))
        show_functions(root);
    else
        show_devices(root);
    if(root) json_object_put(root);
}
static void back(lv_event_t * e)
{
    (void)e;
    app_manager_back_home();
}
static void close_app(void)
{
    active = false;
    generation++;
    if(timer) {
        lv_timer_delete(timer);
        timer = NULL;
    }
    close_modal(NULL);
    if(devices) {
        json_object_put(devices);
        devices = NULL;
    }
    memset(pending, 0, sizeof(pending));
    body = message = content = refresh = NULL;
}
static void init_app(void)
{
    close_app();
    active            = true;
    selected_id[0]    = 0;
    lv_obj_t * screen = lv_obj_create(NULL);
    mimi_ui_header(screen, tr("涂鸦智家", "Tuya Smart Home"), back);
    body = mimi_ui_body(screen);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 16, 0);
    message = mimi_ui_text(body, "", ui_font_body_lg(), MIMI_TEXT);
    lv_obj_set_width(message, lv_pct(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    refresh = button(body, tr("刷新设备", "Refresh devices"), fetch_devices, NULL);
    content = lv_obj_create(body);
    lv_obj_set_size(content, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 12, 0);
    timer = lv_timer_create(tick, 100, NULL);
    lv_scr_load(screen);
    fetch_devices(NULL);
}
AppDescriptor app_iot = {.id = APP_ID_IOT, .name = "Tuya Smart Home", .init = init_app, .close = close_app};
