#include "desktop_widgets.h"
#include "desktop_home_tokens.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
#include "../system/app_manager.h"
#include "../system/cloud_pairing_status.h"
#include "../system/backend_types.h"
#include "../apps/iot/iot_client.h"
LV_IMAGE_DECLARE(cloud);
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static mimi_widget_layout saved, draft;
static mimi_widget_factory factory;
static lv_obj_t *root, *grid, *editor, *library, *message, *edit_entry, *slot_objects[5];
static lv_obj_t *values[MIMI_WIDGET_COUNT], *details[MIMI_WIDGET_COUNT];
static bool editing, valid_sensor, cloud_unavailable;
static int temperature, humidity, ownership = -1, selected = -1, drag_slot = -1;
static lv_point_t drag_start;
static char layout_path[4096];
extern bool wifi_connected;
static const char * tr(const char * zh, const char * en)
{
    return ui_i18n_is_zh() ? zh : en;
}
static void rebuild(void);
static void show_library(int slot);
static const char * title(mimi_widget_kind k)
{
    const char * zh[] = {"添加组件", "Mimi 助手", "HDMI 控制", "环境", "智能设备", "设备连接", "网络状态", "系统状态"};
    const char * en[] = {"Add widget",    "Mimi assistant",    "HDMI control", "Environment",
                         "Smart devices", "Device connection", "Network",      "System"};
    return ui_i18n_is_zh() ? zh[k] : en[k];
}
static lv_obj_t * box(lv_obj_t * p, int x, int y, int w, int h, bool surface)
{
    lv_obj_t * o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 20, 0);
    lv_obj_set_style_bg_color(o, HOME_SURFACE, 0);
    lv_obj_set_style_bg_opa(o, surface ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, HOME_BORDER, 0);
    lv_obj_set_style_border_width(o, surface ? 1 : 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t * label(lv_obj_t * p, const char * text, int x, int y, const lv_font_t * font, lv_color_t color)
{
    lv_obj_t * o = lv_label_create(p);
    lv_label_set_text(o, text);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, color, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static void refresh(void)
{
    lv_obj_t * v = values[MIMI_WIDGET_ENVIRONMENT];
    if(v && lv_obj_is_valid(v)) {
        if(valid_sensor)
            lv_label_set_text_fmt(v, "%d°C    %d%%", temperature, humidity);
        else
            lv_label_set_text(v, "--°C    --%");
        lv_label_set_text(details[MIMI_WIDGET_ENVIRONMENT], valid_sensor
                                                                ? tr("温度          湿度", "Temperature   Humidity")
                                                                : tr("暂未取得环境数据", "No sensor data"));
    }
    v = values[MIMI_WIDGET_CONNECTION];
    if(v && lv_obj_is_valid(v)) {
        lv_label_set_text(v, cloud_unavailable
                                 ? tr("此版本未启用", "Not enabled in this edition")
                                 : ownership == CLOUD_PAIRING_BOUND
                                       ? tr("已绑定", "Linked")
                                       : ownership == CLOUD_PAIRING_UNBOUND ? tr("未绑定", "Not linked")
                                                                            : tr("状态待确认", "Status unavailable"));
        lv_label_set_text(details[MIMI_WIDGET_CONNECTION],
                          cloud_unavailable ? tr("查看设备信息", "Device information") : "100ask.net");
    }
    v = values[MIMI_WIDGET_NETWORK];
    if(v && lv_obj_is_valid(v))
        lv_label_set_text(v, wifi_connected ? tr("已连接", "Connected") : tr("未连接", "Disconnected"));
}
void desktop_widgets_sensor(bool valid, int t, int h)
{
    valid_sensor = valid;
    temperature  = t;
    humidity     = h;
    refresh();
}
void desktop_widgets_network(void)
{
    refresh();
}
void desktop_widgets_cloud(int state)
{
    cloud_unavailable = state == CLOUD_STATE_UNAVAILABLE;
    if(cloud_unavailable) ownership = -1;
    refresh();
}
void desktop_widgets_pairing(const mw_msg_t * msg)
{
    if(!msg || msg->data_len != sizeof(cloud_pairing_status_v1_t)) return;
    cloud_pairing_status_v1_t status;
    memcpy(&status, msg->data, sizeof(status));
    if(!cloud_pairing_status_valid(&status)) return;
    ownership = status.error ? -1 : (int)status.ownership;
    refresh(); /* Never retain the binding code. */
}
static void action(lv_event_t * e)
{
    if(editing) return;
    mimi_widget_kind k = (mimi_widget_kind)(intptr_t)lv_event_get_user_data(e);
    app_manager_open(k == MIMI_WIDGET_IOT
                         ? APP_ID_IOT
                         : k == MIMI_WIDGET_NETWORK
                               ? APP_ID_WIFI
                               : k == MIMI_WIDGET_CONNECTION && !cloud_unavailable ? APP_ID_PAIRING : APP_ID_SETTING);
}
static lv_obj_t * small(lv_obj_t * p, mimi_widget_kind k, int w, int h)
{
    lv_obj_t * c      = box(p, 0, 0, w, h, true);
    lv_color_t accent = k == MIMI_WIDGET_ENVIRONMENT ? HOME_GREEN : k == MIMI_WIDGET_IOT ? lv_color_hex(0xFF8845) : HOME_BLUE;
    lv_obj_set_style_bg_color(c, lv_color_mix(HOME_SURFACE, accent, 247), 0);
    lv_obj_t * badge = box(c, 24, 14, 56, 56, false);
    lv_obj_set_style_radius(badge, 12, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, lv_color_mix(HOME_SURFACE, accent, 220), 0);
    const char * icon = k == MIMI_WIDGET_IOT
                            ? LV_SYMBOL_HOME
                            : k == MIMI_WIDGET_NETWORK
                                  ? LV_SYMBOL_WIFI
                                  : k == MIMI_WIDGET_ENVIRONMENT
                                        ? LV_SYMBOL_TINT
                                        : k == MIMI_WIDGET_CONNECTION ? LV_SYMBOL_REFRESH : LV_SYMBOL_SETTINGS;
    lv_obj_t * s = label(badge, icon, 0, 0, &lv_font_montserrat_24, accent);
    lv_obj_center(s);
    if(k==MIMI_WIDGET_ENVIRONMENT) {
        lv_obj_add_flag(s,LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *stem=box(badge,24,10,10,29,false);lv_obj_set_style_radius(stem,5,0);lv_obj_set_style_border_width(stem,2,0);lv_obj_set_style_border_color(stem,accent,0);
        lv_obj_t *bulb=box(badge,20,31,18,18,false);lv_obj_set_style_radius(bulb,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(bulb,2,0);lv_obj_set_style_border_color(bulb,accent,0);
        lv_obj_t *fill=box(badge,26,15,6,27,false);lv_obj_set_style_bg_color(fill,accent,0);lv_obj_set_style_bg_opa(fill,LV_OPA_COVER,0);
    }

    if(k == MIMI_WIDGET_CONNECTION) {
        lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t * im = lv_image_create(badge);
        lv_image_set_src(im, &cloud);
        lv_obj_set_size(im, 40, 40);
        lv_image_set_inner_align(im, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_set_style_image_recolor(im, accent, 0);
        lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
        lv_obj_center(im);
    }
    label(c, title(k), 96, 30, ui_font_h3(), HOME_TEXT);
    const char * value = k == MIMI_WIDGET_IOT
                             ? (mimi_iot_configuration_present() == 0 ? tr("尚未配置", "Not configured")
                                                                      : tr("状态待确认", "Status unavailable"))
                             : k == MIMI_WIDGET_SYSTEM ? tr("设备设置", "Device settings") : "--";
    values[k] = label(c, value, 22, 84, k == MIMI_WIDGET_ENVIRONMENT ? ui_font_h1() : ui_font_h2(), HOME_TEXT);
    lv_obj_set_width(values[k], w - 44);
    lv_obj_set_style_text_align(values[k], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(values[k], LV_LABEL_LONG_DOT);
    details[k] =
        label(c,
              k == MIMI_WIDGET_IOT
                  ? (mimi_iot_configuration_present() == 0 ? tr("添加设备后显示状态", "Add devices to see their status")
                                                           : tr("打开应用查看状态", "Open app to check status"))
                  : k == MIMI_WIDGET_SYSTEM ? tr("显示、声音与系统信息", "Display, sound and system info") : "",
              22, 118, ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_set_width(details[k], w - 44);
    lv_obj_set_style_text_align(details[k], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(details[k], LV_LABEL_LONG_DOT);
    lv_obj_t * line = box(c, 24, h - 52, w - 48, 1, false);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(line, lv_color_mix(HOME_SURFACE, accent, 212), 0);
    lv_obj_t * foot = label(c,
                            k == MIMI_WIDGET_ENVIRONMENT ? tr("查看环境  ›", "View environment  >")
                                                         : k == MIMI_WIDGET_IOT ? tr("前往配置  ›", "Configure  >")
                                                                                : tr("查看设备  ›", "View device  >"),
                            0, h - 37, ui_font_body_md(), accent);
    lv_obj_set_width(foot, w);
    lv_obj_set_style_text_align(foot, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, action, LV_EVENT_CLICKED, (void *)(intptr_t)k);
    return c;
}
static void redraw_async(void * unused)
{
    (void)unused;
    if(root && lv_obj_is_valid(root)) rebuild();
}
static void remove_slot(lv_event_t * e)
{
    unsigned slot = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if(editing && !mimi_widget_layout_set(&draft, slot, MIMI_WIDGET_EMPTY)) lv_async_call(redraw_async, NULL);
}
static void slot_event(lv_event_t * e)
{
    int slot             = (int)(intptr_t)lv_event_get_user_data(e);
    lv_indev_t * indev   = lv_indev_active();
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_LONG_PRESSED && !editing) {
        desktop_widgets_edit();
        return;
    }
    if(!editing) return;
    if(code == LV_EVENT_PRESSED && indev) {
        drag_slot = slot;
        lv_indev_get_point(indev, &drag_start);
    }
    if(code == LV_EVENT_RELEASED && indev && drag_slot == slot) {
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        drag_slot = -1;
        if(LV_ABS(pt.x - drag_start.x) > 24 || LV_ABS(pt.y - drag_start.y) > 24) {
            for(int i = 0; i < 5; i++) {
                lv_area_t a;
                lv_obj_get_coords(slot_objects[i], &a);
                if(pt.x >= a.x1 && pt.x <= a.x2 && pt.y >= a.y1 && pt.y <= a.y2 &&
                   !mimi_widget_layout_swap(&draft, slot, i)) {
                    lv_async_call(redraw_async, NULL);
                    break;
                }
            }
        } else if(draft.slots[slot] == MIMI_WIDGET_EMPTY)
            show_library(slot);
    }
}
static void close_library(void)
{
    if(library) {
        lv_obj_delete(library);
        library = NULL;
    }
    selected = -1;
}
static void choose(lv_event_t * e)
{
    mimi_widget_kind k = (mimi_widget_kind)(intptr_t)lv_event_get_user_data(e);
    if(selected < 0 || !editing || draft.slots[selected] != MIMI_WIDGET_EMPTY) return;
    if(!mimi_widget_layout_set(&draft, (unsigned)selected, k)) {
        close_library();
        lv_async_call(redraw_async, NULL);
    }
}
static void close_library_event(lv_event_t * e)
{
    (void)e;
    close_library();
}
static void show_library(int slot)
{
    close_library();
    selected = slot;
    library  = box(editor, 0, 0, HOME_SCREEN_W, HOME_SCREEN_H, false);
    lv_obj_add_flag(library, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(library, 0, 0);
    lv_obj_set_style_bg_color(library, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(library, LV_OPA_20, 0);
    lv_obj_add_event_cb(library, close_library_event, LV_EVENT_CLICKED, NULL);
    int w            = HOME_SCREEN_W * 39 / 100;
    lv_obj_t * panel = box(library, HOME_SCREEN_W - w, 0, w, HOME_SCREEN_H - HOME_STATUS_H, true);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    label(panel, tr("添加组件", "Add widget"), 24, 24, ui_font_h2(), HOME_TEXT);
    label(panel, tr("选择一个组件放到空位", "Choose a widget for the empty slot"), 24, 62, ui_font_body_md(),
          HOME_TEXT_MUTED);
    int rh = (HOME_SCREEN_H - HOME_STATUS_H - 156) / 7;
    for(int k = 1; k < MIMI_WIDGET_COUNT; k++) {
        mimi_widget_layout candidate = draft;
        bool allowed                 = !mimi_widget_layout_set(&candidate, slot, (mimi_widget_kind)k);
        lv_obj_t * row               = box(panel, 16, 102 + (k - 1) * rh, w - 32, rh - 7, true);
        label(row, title((mimi_widget_kind)k), 14, 9, ui_font_body_lg(), HOME_TEXT);
        label(row, k <= MIMI_WIDGET_HDMI ? tr("大组件", "Large widget") : tr("小组件", "Small widget"), 14, 34,
              ui_font_body_md(), HOME_TEXT_MUTED);
        lv_obj_t * button = box(row, w - 112, 9, 64, rh - 25, false);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        bool exists = false;
        for(int i = 0; i < 5; i++)
            if(draft.slots[i] == (mimi_widget_kind)k) exists = true;
        lv_obj_t * l = label(button, allowed ? "+" : exists ? tr("已添加", "Added") : tr("不适用", "N/A"), 0, 0,
                             ui_font_body_md(), allowed ? HOME_CORAL : HOME_TEXT_MUTED);
        lv_obj_center(l);
        if(allowed)
            lv_obj_add_event_cb(button, choose, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        else
            lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    label(panel, tr("移除组件不会卸载应用", "Removing widgets does not uninstall apps"), 24,
          HOME_SCREEN_H - HOME_STATUS_H - 40, ui_font_body_md(), HOME_TEXT_MUTED);
}
static void finish(lv_event_t * e)
{
    bool save = (bool)(intptr_t)lv_event_get_user_data(e);
    if(save) {
        int rc = mimi_widget_layout_save(layout_path, &draft);
        if(rc < 0) {
            lv_label_set_text(message, tr("保存失败，草稿已保留", "Save failed; draft retained"));
            return;
        }
        saved = draft;
        if(rc == MIMI_LAYOUT_DURABILITY_WARNING) {
            lv_label_set_text(message,
                              tr("已写入，磁盘同步未确认；请勿断电", "Written; disk sync unconfirmed. Keep power on."));
            return;
        }
    }
    editing = false;
    lv_obj_clear_flag(edit_entry, LV_OBJ_FLAG_HIDDEN);
    close_library();
    grid = NULL;
    lv_obj_delete(editor);
    editor = NULL;
    lv_async_call(redraw_async, NULL);
}
static void edit_event(lv_event_t * e)
{
    (void)e;
    desktop_widgets_edit();
}
void desktop_widgets_edit(void)
{
    if(editing) return;
    draft   = saved;
    editing = true;
    lv_obj_add_flag(edit_entry, LV_OBJ_FLAG_HIDDEN);
    editor = box(root, 0, HOME_STATUS_H, HOME_SCREEN_W, HOME_SCREEN_H - HOME_STATUS_H, false);
    lv_obj_add_flag(editor, LV_OBJ_FLAG_CLICKABLE);
    /* Full overlay captures every touch; Home actions below cannot run in edit mode. */
    lv_obj_t * cancel = box(editor, HOME_CONTENT_X, 4, 90, 32, false);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    label(cancel, tr("取消", "Cancel"), 0, 2, ui_font_body_lg(), HOME_TEXT_MUTED);
    lv_obj_add_event_cb(cancel, finish, LV_EVENT_CLICKED, 0);
    message = label(editor, tr("编辑组件", "Edit widgets"), HOME_SCREEN_W / 2 - 70, 7, ui_font_body_lg(), HOME_TEXT);
    lv_obj_t * save = box(editor, HOME_SCREEN_W - HOME_CONTENT_X - 110, 2, 110, 34, true);
    lv_obj_add_flag(save, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(save, HOME_CORAL, 0);
    lv_obj_t * l = label(save, tr("保存布局", "Save layout"), 0, 0, ui_font_body_md(), HOME_TEXT_INVERSE);
    lv_obj_center(l);
    lv_obj_add_event_cb(save, finish, LV_EVENT_CLICKED, (void *)1);
    lv_async_call(redraw_async, NULL);
}
static void rebuild(void)
{
    factory(NULL, MIMI_WIDGET_EMPTY, 0, 0);
    memset(values, 0, sizeof(values));
    memset(details, 0, sizeof(details));
    if(grid) lv_obj_delete(grid);
    int w       = HOME_CONTENT_W;
    bool narrow = HOME_SCREEN_W < 1000;
    int large = narrow ? w : (w - 10) / 2, smallw = narrow ? w : (w - 22) / 3;
    int big_h   = narrow ? 286 : (HOME_DASHBOARD_H - 12) * 286 / 492,
        small_h = narrow ? 206 : HOME_DASHBOARD_H - big_h - 12;
    grid        = box(editing ? editor : root, HOME_CONTENT_X, HOME_DASHBOARD_Y - (editing ? HOME_STATUS_H : 0), w,
               HOME_DASHBOARD_H, false);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    const mimi_widget_layout * layout = editing ? &draft : &saved;
    for(int i = 0; i < 5; i++) {
        int cw = i < 2 ? large : smallw, ch = i < 2 ? big_h : small_h;
        int x = narrow ? 0 : i < 2 ? i * (large + 10) : (i - 2) * (smallw + 11);
        int y =
            narrow ? (i < 2 ? i * (big_h + 12) : 2 * (big_h + 12) + (i - 2) * (small_h + 12)) : i < 2 ? 0 : big_h + 12;
        mimi_widget_kind k = layout->slots[i];
        lv_obj_t * card;
        if(k == MIMI_WIDGET_EMPTY) {
            card         = box(grid, 0, 0, cw, ch, true);
            lv_obj_t * l = label(card, title(k), 0, 0, ui_font_h3(), HOME_TEXT_MUTED);
            lv_obj_center(l);
        } else
            card = i < 2 ? factory(grid, k, cw, ch) : small(grid, k, cw, ch);
        lv_obj_set_pos(card, x, y);
        slot_objects[i] = card;
        lv_obj_add_event_cb(card, slot_event, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        if(editing) {
            lv_obj_t * shield = box(card, 0, 0, cw, ch, false);
            lv_obj_add_flag(shield, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(shield, slot_event, LV_EVENT_ALL, (void *)(intptr_t)i);
            if(k) {
                lv_obj_t * minus = box(shield, cw - 34, 7, 27, 27, true);
                lv_obj_set_style_bg_color(minus, HOME_CORAL, 0);
                lv_obj_add_flag(minus, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_t * l = label(minus, "−", 0, 0, ui_font_h3(), HOME_TEXT_INVERSE);
                lv_obj_center(l);
                lv_obj_add_event_cb(minus, remove_slot, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            }
        }
    }
    refresh();
}
void desktop_widgets_create(lv_obj_t * parent, mimi_widget_factory f)
{
    /* A theme rebuild cancels the uncommitted draft, never saving implicitly. */
    editing = false;
    editor = library = grid = NULL;
    root                    = parent;
    factory                 = f;
    mimi_widget_layout_default(&saved);
    if(!mimi_widget_layout_path(layout_path, sizeof(layout_path))) mimi_widget_layout_load(layout_path, &saved);
    lv_obj_t * edit = box(root, HOME_SCREEN_W - HOME_CONTENT_X - 130, HOME_STATUS_H + 4, 130, 32, false);
    lv_obj_add_flag(edit, LV_OBJ_FLAG_CLICKABLE);
    edit_entry = edit;
    label(edit, tr("编辑组件", "Edit widgets"), 16, 4, ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_add_event_cb(edit, edit_event, LV_EVENT_CLICKED, NULL);
    rebuild();
}

const char * desktop_widgets_connection_text(void)
{
    return cloud_unavailable ? tr("此版本未启用", "Not enabled in this edition")
                             : ownership == CLOUD_PAIRING_BOUND
                                   ? tr("已绑定 · 100ask.net", "Linked · 100ask.net")
                                   : ownership == CLOUD_PAIRING_UNBOUND ? tr("未绑定", "Not linked")
                                                                        : tr("状态待确认", "Status unavailable");
}
