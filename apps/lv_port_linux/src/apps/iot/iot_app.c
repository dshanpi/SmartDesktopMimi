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
static lv_obj_t *body, *message, *content, *refresh, *modal, *overview;
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
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
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
    lv_obj_remove_local_style_prop(l,LV_STYLE_TEXT_COLOR,0);
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
static void info_card(const char *title, const char *description)
{
    lv_obj_t *card=lv_obj_create(content);mimi_ui_surface(card,20);
    lv_obj_set_width(card,lv_pct(100));lv_obj_set_height(card,LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card,24,0);lv_obj_set_style_pad_row(card,12,0);
    lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);
    lv_obj_t *heading=mimi_ui_text(card,title,ui_font_h2(),MIMI_TEAL);
    lv_obj_set_width(heading,lv_pct(100));lv_label_set_long_mode(heading,LV_LABEL_LONG_WRAP);
    lv_obj_t *text=mimi_ui_text(card,description,ui_font_body_md(),MIMI_MUTED);
    lv_obj_set_width(text,lv_pct(100));lv_label_set_long_mode(text,LV_LABEL_LONG_WRAP);
}
struct control
{
    char code[129], type[24];
    int32_t min, max, step;
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
    if(!strcmp(c->type, "Integer")) {
        int64_t v = lv_spinbox_get_value(c->input);
        if(v < c->min || v > c->max || c->step < 1 || (v - c->min) % c->step) {
            pending[0] = 0;
            lv_label_set_text(message, tr("数值不符合设备范围或步长，请用加减按钮调整。",
                "Value does not match the device range or step. Adjust with + or -."));
            return;
        }
    }
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
    char summary[512];
    const char *display_value = !strcmp(c->type, "Boolean")
        ? (json_object_get_boolean(value) ? tr("开启", "On") : tr("关闭", "Off"))
        : json_object_get_string(value);
    snprintf(summary, sizeof(summary), tr("功能：%s\n设置为：%s", "Function: %s\nSet to: %s"),
             c->code, display_value);
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
    l = mimi_ui_text(modal, summary, ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    button(modal, tr("确认发送", "Send command"), send_confirmed, NULL);
    mimi_ui_button_style(button(modal, tr("取消", "Cancel"), close_modal, NULL), false);
}
static void spin(lv_event_t * e)
{
    struct control * c = lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT || lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t * l = lv_obj_get_child(lv_event_get_target(e), 0);
        /* Device steps are arithmetic, not the spinbox's digit-oriented
         * zero-crossing behavior. Widen before adding to avoid overflow. */
        int64_t value = lv_spinbox_get_value(c->input);
        int64_t step = c->step;
        int64_t offset = value - c->min;
        if(!strcmp(lv_label_get_text(l), "+"))
            value = c->min + (offset / step + 1) * step;
        else
            value = c->min + ((offset + step - 1) / step - 1) * step;
        int64_t min = c->min;
        int64_t max = c->max;
        /* Clamp to the last schema-aligned value, not an off-grid maximum. */
        max = min + ((max - min) / step) * step;
        if(value < min) value = min;
        if(value > max) value = max;
        lv_spinbox_set_value(c->input, (int32_t)value);
    }
}
static void show_functions(json_object * root)
{
    lv_obj_clean(content);
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
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
    lv_label_set_text(overview,tr("设备控制", "Device controls"));
    lv_label_set_text(message, selected_name);
    for(size_t i = 0; i < json_object_array_length(items) && control_count < 24; i++) {
        json_object * f  = json_object_array_get_idx(items, i);
        const char *code = field(f, "code"), *type = field(f, "type");
        if(!identifier(code) || (strcmp(type, "Boolean") && strcmp(type, "Integer") && strcmp(type, "Enum"))) continue;
        struct control * c = &controls[control_count];
        snprintf(c->code, sizeof(c->code), "%s", code);
        snprintf(c->type, sizeof(c->type), "%s", type);
        const char *schema_text = field(f, "values");
        json_object * schema = mimi_iot_json_object(schema_text, strlen(schema_text));
        if(!schema) continue;
        lv_obj_t * card = lv_obj_create(content);
        mimi_ui_surface(card, 16);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(card, 16, 0);
        lv_obj_set_style_pad_row(card, 12, 0);
        lv_obj_t * caption = mimi_ui_text(card, *field(f,"name")?field(f,"name"):code, ui_font_body_lg(), MIMI_TEXT);
        lv_obj_set_width(caption, lv_pct(100));
        lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
        if(!strcmp(type, "Boolean")) {
            c->input = lv_switch_create(card);
        } else if(!strcmp(type, "Integer")) {
            json_object *jmin = NULL, *jmax = NULL, *jstep = NULL;
            int64_t min, max, step;
            if(!json_object_object_get_ex(schema, "min", &jmin) || !json_object_object_get_ex(schema, "max", &jmax) ||
               !json_object_object_get_ex(schema, "step", &jstep) ||
               json_object_get_type(jmin) != json_type_int || json_object_get_type(jmax) != json_type_int ||
               json_object_get_type(jstep) != json_type_int || (min = json_object_get_int64(jmin)) < INT32_MIN ||
               (max = json_object_get_int64(jmax)) > INT32_MAX || (step = json_object_get_int64(jstep)) < 1 ||
               step > INT32_MAX || min > max) {
                json_object_put(schema);
                lv_obj_delete(card);
                continue;
            }
            c->min = (int32_t)min; c->max = (int32_t)max; c->step = (int32_t)step;
            lv_obj_t *row=lv_obj_create(card);mimi_ui_clear(row);
            lv_obj_set_size(row,lv_pct(100),56);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(row,12,0);
            lv_obj_t *minus=button(row,"-",spin,c);lv_obj_set_size(minus,56,56);
            c->input = lv_spinbox_create(row);lv_obj_set_size(c->input,150,56);
            lv_spinbox_set_range(c->input, (int32_t)min, (int32_t)max);
            lv_spinbox_set_value(c->input, (int32_t)min);
            lv_spinbox_set_step(c->input, (uint32_t)step);
            lv_obj_t *plus=button(row,"+",spin,c);lv_obj_set_size(plus,56,56);
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
                    bool printable = true;
                    for(size_t k = 0; k < n; k++)
                        if((unsigned char)s[k] < 32 || (unsigned char)s[k] == 127) printable = false;
                    if(json_object_get_type(o) != json_type_string || !n || n >= 256 ||
                       n != (size_t)json_object_get_string_len(o) || !printable ||
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
    if(devices) for(size_t i=0;i<json_object_array_length(devices);i++) {
        json_object *device=json_object_array_get_idx(devices,i), *properties=NULL;
        if(strcmp(field(device,"id"),selected_id) || !json_object_object_get_ex(device,"status",&properties) ||
           json_object_get_type(properties)!=json_type_array) continue;
        for(size_t j=0;j<json_object_array_length(properties) && j<4;j++) {
            json_object *property=json_object_array_get_idx(properties,j), *value=NULL;
            if(!json_object_object_get_ex(property,"value",&value)) continue;
            char text[600];snprintf(text,sizeof(text),"%s: %.250s",field(property,"code"),json_object_to_json_string_ext(value,JSON_C_TO_STRING_PLAIN));
            lv_obj_t *label=mimi_ui_text(content,text,ui_font_body_md(),MIMI_MUTED);
            lv_obj_set_width(label,lv_pct(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
        }
        break;
    }
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
        lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
        lv_label_set_text(message, tr("设备列表不可用", "Device list unavailable"));
        lv_label_set_text(overview,tr("我的智能生活","My Smart Life"));
        info_card(tr("设备列表暂不可用","Device list unavailable"),tr("请刷新重试，或联系管理员检查智家服务。","Refresh to retry, or ask your administrator to check the Smart Home service."));
        return;
    }
    if(devices) json_object_put(devices);
    devices = json_object_get(items);
    lv_obj_clean(content);
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
    lv_label_set_text(message,
                      tr("设备状态已刷新；点击设备查看控制项", "Status refreshed. Select a device to view controls."));
    size_t total = json_object_array_length(devices);
    lv_label_set_text_fmt(overview,ui_i18n_is_zh()?"我的设备  ·  %u 在线 / %u 台":"My devices  ·  %u online / %u total",summary.online,summary.total);
    if(!total) {
        info_card(tr("还没有已授权的设备", "No devices yet"),
            tr("在智能生活 App 中添加设备，并完成账号授权。完成后点击“刷新设备”，它们就会出现在这里。", "Add devices in Smart Life and authorize your account. Then select Refresh devices to see them here."));
        return;
    }
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(content,12,0);
    lv_obj_update_layout(content);
    int card_width=(lv_obj_get_content_width(content)-12)/2;
    for(size_t i = 0; i < total && i < 100; i++) {
        json_object *d=json_object_array_get_idx(devices,i);
        const bool online=boolean(d,"online");
        lv_obj_t *card=lv_button_create(content);mimi_ui_button_style(card,false);
        lv_obj_set_size(card,card_width,124);lv_obj_set_style_radius(card,18,0);
        lv_obj_set_style_pad_all(card,18,0);lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card,12,0);
        lv_obj_add_event_cb(card,select_device,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        char text[600];
        snprintf(text,sizeof(text),"%s  ·  %s",*field(d,"name")?field(d,"name"):tr("未命名设备","Unnamed device"),
            online?tr("在线","Online"):tr("离线","Offline"));
        lv_obj_t *name=mimi_ui_text(card,text,ui_font_body_lg(),MIMI_TEXT);
        lv_obj_set_width(name,lv_pct(100));lv_obj_set_style_max_height(name,2*ui_font_body_lg()->line_height,0);
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        lv_obj_t *hint=mimi_ui_text(card,online?tr("查看设备与控制  →","View device & controls  >"):tr("设备离线 · 查看详情","Offline · View details"),ui_font_body_md(),online?MIMI_TEAL:MIMI_MUTED);
        lv_obj_set_width(hint,lv_pct(100));lv_label_set_long_mode(hint,LV_LABEL_LONG_DOT);
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
            lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
            button(content, tr("刷新设备状态", "Refresh device status"), fetch_devices, NULL);
        }
        if(!strstr(path,"/commands")) {
            lv_obj_clean(content);lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
            lv_label_set_text(overview,tr("我的智能生活", "My Smart Life"));
            if(!strcmp(error,"not_configured"))
                info_card(tr("连接你的智能生活", "Connect Smart Life"),
                    tr("1  在智能生活 App 中添加设备。\n\n2  请管理员完成智家账号授权。\n\n3  回到这里刷新，即可查看已授权设备。", "1  Add devices in the Smart Life app.\n\n2  Ask your administrator to authorize the account.\n\n3  Refresh here to see authorized devices."));
            else if(!strcmp(error,"authorization_required"))
                info_card(tr("需要重新授权", "Reconnect your account"),
                    tr("请管理员更新智家账号授权，然后刷新设备。现有设备配置不会被清除。", "Ask your administrator to renew account access, then refresh devices. Existing device configuration is kept."));
            else info_card(tr("暂时无法连接", "Unable to connect"),
                    tr("请检查网络和智家服务是否可用，再点击刷新设备重试。", "Check your network and Smart Home service, then select Refresh devices to retry."));
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
        lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
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
    body = message = content = refresh = overview = NULL;
}
static void init_app(void)
{
    close_app();
    active            = true;
    selected_id[0]    = 0;
    lv_obj_t * screen = lv_obj_create(NULL);
    mimi_ui_header(screen, tr("涂鸦智家", "Tuya Smart Home"), back);
    body = mimi_ui_body(screen);
    lv_obj_set_size(body,lv_display_get_horizontal_resolution(NULL)-48,
        lv_display_get_vertical_resolution(NULL)-MIMI_HEADER_H-48);
    lv_obj_set_pos(body,24,MIMI_HEADER_H+24);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 16, 0);
    lv_obj_t *toolbar=lv_obj_create(body);mimi_ui_clear(toolbar);
    lv_obj_set_size(toolbar,lv_pct(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(toolbar,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(toolbar,16,0);
    lv_obj_set_flex_align(toolbar,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_START);
    lv_obj_t *intro=lv_obj_create(toolbar);mimi_ui_clear(intro);
    lv_obj_set_size(intro,0,LV_SIZE_CONTENT);lv_obj_set_flex_grow(intro,1);
    lv_obj_set_flex_flow(intro,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(intro,8,0);
    overview=mimi_ui_text(intro,tr("我的智能生活", "My Smart Life"),ui_font_h2(),MIMI_TEXT);
    lv_obj_set_width(overview,lv_pct(100));lv_label_set_long_mode(overview,LV_LABEL_LONG_WRAP);
    message = mimi_ui_text(intro, "", ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_width(message, lv_pct(100));lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    refresh = button(toolbar, tr("刷新设备", "Refresh devices"), fetch_devices, NULL);
    lv_obj_set_size(refresh,180,52);lv_obj_set_style_radius(refresh,14,0);

    content = lv_obj_create(body);mimi_ui_clear(content);
    lv_obj_set_size(content, lv_pct(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_add_flag(content,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(content,LV_DIR_VER);
    timer = lv_timer_create(tick, 100, NULL);
    lv_scr_load(screen);
    fetch_devices(NULL);
}
AppDescriptor app_iot = {.id = APP_ID_IOT, .name = "Tuya Smart Home", .init = init_app, .close = close_app};
