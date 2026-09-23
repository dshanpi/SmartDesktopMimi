#include "desktop_status_bar.h"
#include "ui_helpers.h"
#include "theme/theme.h"
#include "desktop_home_tokens.h"
#include "../system/app_manager.h"
#include "../system/settings.h"
#include "../system/backend_types.h"
#include "../middleware/middleware.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMAGE_DECLARE(wireless_network_wifi);
LV_IMAGE_DECLARE(cloud);

/* 顶栏布局尺寸 */
#define HDMI_MODE_ROW_WIDTH 228
#define HDMI_MODE_ROW_HEIGHT 36
#define HDMI_MODE_LABEL_WIDTH 152
#define TOP_STATUS_GROUP_WIDTH 242
#define TOP_POWER_LABEL_WIDTH 300
#define CONTROL_CENTER_WIDTH 976
#define CONTROL_CENTER_HEIGHT 344
#define CONTROL_CENTER_CARD_WIDTH 216
#define CONTROL_CENTER_CARD_HEIGHT 184

/* =======================
 * 全局变量（ui_helpers.c 的回调通过 extern 引用）
 * ======================= */
lv_obj_t *wifi_icon_obj = NULL;
lv_obj_t *hdmi_switch_obj = NULL;
lv_obj_t *cloud_icon_obj = NULL;
lv_obj_t *cloud_status_dot_obj = NULL;
lv_obj_t *cloud_progress_track_obj = NULL; /* OTA download track under cloud icon */
lv_obj_t *cloud_progress_fill_obj = NULL;
bool wifi_connected = false;
char wifi_connected_ssid[33] = {0};
bool wifi_runtime_enabled = true;
bool wifi_runtime_scanning = false;
bool wifi_has_ip = false;
bool wifi_startup_scan_decided = false;
lv_timer_t *wifi_startup_scan_timer = NULL;
lv_obj_t *power_label = NULL;   /* INA219 整机功耗/电流/电压（顶栏中间） */

/* HDMI 显示模式开关状态（顶栏 + HDMI 全屏覆盖层） */
static bool hdmi_ignore_event = false;
static bool hdmi_toggle_pending = false;
static bool hdmi_toggle_target = false;
static lv_timer_t *hdmi_pending_timer = NULL;
static lv_obj_t *hdmi_overlay_switch = NULL;
static lv_obj_t *control_center_overlay = NULL;
static lv_obj_t *control_center_panel = NULL;
static lv_obj_t *control_center_wifi_detail = NULL;
static lv_obj_t *control_center_power_detail = NULL;
static lv_obj_t *control_center_hdmi_switch = NULL;
static bool control_center_visible = false;

/* 前置声明 */
static void on_hdmi_switch_changed(lv_event_t *e);
static void on_hdmi_row_clicked(lv_event_t *e);
static void on_control_center_hdmi_changed(lv_event_t *e);
static void hdmi_switch_sync(bool enabled);
static void hdmi_send_enable(bool on);
static void hdmi_pending_timeout_cb(lv_timer_t *timer);
static void hdmi_clear_pending(void);
static void control_center_show(void);
static void control_center_hide(bool animate);

typedef enum {
    CONTROL_CENTER_ACTION_WIFI = 1,
    CONTROL_CENTER_ACTION_USER_APPS,
    CONTROL_CENTER_ACTION_SETTINGS,
} control_center_action_t;

static void control_center_anim_y(void *obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void control_center_hide_ready(lv_anim_t *anim)
{
    (void)anim;
    if (control_center_overlay && lv_obj_is_valid(control_center_overlay)) {
        lv_obj_add_flag(control_center_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void control_center_hide(bool animate)
{
    if (!control_center_overlay || !control_center_panel || !control_center_visible) return;
    control_center_visible = false;
    lv_anim_delete(control_center_panel, control_center_anim_y);

    if (!animate) {
        lv_obj_set_y(control_center_panel, -CONTROL_CENTER_HEIGHT - 24);
        lv_obj_add_flag(control_center_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, control_center_panel);
    lv_anim_set_values(&anim, lv_obj_get_y(control_center_panel),
                       -CONTROL_CENTER_HEIGHT - 24);
    lv_anim_set_time(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&anim, control_center_anim_y);
    lv_anim_set_ready_cb(&anim, control_center_hide_ready);
    lv_anim_start(&anim);
}

static void control_center_overlay_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED &&
        lv_event_get_target(event) == lv_event_get_current_target(event)) {
        control_center_hide(true);
    }
}

static void control_center_panel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) return;
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        control_center_hide(true);
    }
}

static void control_center_action_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    control_center_action_t action =
        (control_center_action_t)(intptr_t)lv_event_get_user_data(event);

    control_center_hide(false);
    if (action == CONTROL_CENTER_ACTION_WIFI) {
        app_manager_open(APP_ID_WIFI);
    } else if (action == CONTROL_CENTER_ACTION_USER_APPS) {
        app_manager_open(APP_ID_USER_APPS);
    } else if (action == CONTROL_CENTER_ACTION_SETTINGS) {
        app_manager_open(APP_ID_SETTING);
    }
}

static lv_obj_t *control_center_create_card(lv_obj_t *parent,
                                            const char *eyebrow,
                                            const char *title,
                                            const char *detail,
                                            lv_color_t accent,
                                            control_center_action_t action)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CONTROL_CENTER_CARD_WIDTH, CONTROL_CENTER_CARD_HEIGHT);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x3B4258), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x596179), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x48516B), LV_STATE_PRESSED);
    if (action) {
        lv_obj_add_event_cb(card, control_center_action_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)action);
    }

    lv_obj_t *accent_dot = lv_obj_create(card);
    lv_obj_set_size(accent_dot, 12, 12);
    lv_obj_align(accent_dot, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(accent_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(accent_dot, accent, 0);
    lv_obj_set_style_bg_opa(accent_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent_dot, 0, 0);
    lv_obj_set_style_pad_all(accent_dot, 0, 0);
    lv_obj_clear_flag(accent_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *eyebrow_label = lv_label_create(card);
    lv_label_set_text(eyebrow_label, eyebrow);
    lv_obj_align(eyebrow_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(eyebrow_label, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(eyebrow_label, lv_color_hex(0xAEB7CB), 0);
    lv_obj_clear_flag(eyebrow_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, -4);
    lv_obj_set_style_text_font(title_label, ui_font_h2(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(title_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *detail_label = lv_label_create(card);
    lv_label_set_text(detail_label, detail);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(detail_label, CONTROL_CENTER_CARD_WIDTH - 36);
    lv_obj_align(detail_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_font(detail_label, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(detail_label, lv_color_hex(0xCBD2E0), 0);
    lv_obj_clear_flag(detail_label, LV_OBJ_FLAG_CLICKABLE);
    return detail_label;
}

static void control_center_build(void)
{
    if (control_center_overlay && lv_obj_is_valid(control_center_overlay)) return;

    control_center_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(control_center_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(control_center_overlay, 0, 0);
    lv_obj_set_style_bg_color(control_center_overlay, lv_color_hex(0x10131C), 0);
    lv_obj_set_style_bg_opa(control_center_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(control_center_overlay, 0, 0);
    lv_obj_set_style_radius(control_center_overlay, 0, 0);
    lv_obj_set_style_pad_all(control_center_overlay, 0, 0);
    lv_obj_clear_flag(control_center_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(control_center_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(control_center_overlay, control_center_overlay_event,
                        LV_EVENT_CLICKED, NULL);

    control_center_panel = lv_obj_create(control_center_overlay);
    lv_obj_set_size(control_center_panel, CONTROL_CENTER_WIDTH, CONTROL_CENTER_HEIGHT);
    lv_obj_set_pos(control_center_panel, 24, -CONTROL_CENTER_HEIGHT - 24);
    lv_obj_set_style_radius(control_center_panel, 30, 0);
    lv_obj_set_style_bg_color(control_center_panel, lv_color_hex(0x2F3548), 0);
    lv_obj_set_style_bg_grad_color(control_center_panel, lv_color_hex(0x262B3B), 0);
    lv_obj_set_style_bg_grad_dir(control_center_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(control_center_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(control_center_panel, 1, 0);
    lv_obj_set_style_border_color(control_center_panel, lv_color_hex(0x596179), 0);
    lv_obj_set_style_shadow_color(control_center_panel, lv_color_hex(0x111522), 0);
    lv_obj_set_style_shadow_opa(control_center_panel, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(control_center_panel, 28, 0);
    lv_obj_set_style_shadow_offset_y(control_center_panel, 10, 0);
    lv_obj_set_style_pad_hor(control_center_panel, 24, 0);
    lv_obj_set_style_pad_top(control_center_panel, 16, 0);
    lv_obj_set_style_pad_bottom(control_center_panel, 18, 0);
    lv_obj_clear_flag(control_center_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(control_center_panel, control_center_panel_event,
                        LV_EVENT_GESTURE, NULL);

    lv_obj_t *title = lv_label_create(control_center_panel);
    lv_label_set_text(title, "Control Center");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 2);
    lv_obj_set_style_text_font(title, ui_font_h2(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    control_center_power_detail = lv_label_create(control_center_panel);
    lv_label_set_text(control_center_power_detail, "--.-V ---mA --.-W");
    lv_obj_align(control_center_power_detail, LV_ALIGN_TOP_RIGHT, -4, 6);
    lv_obj_set_style_text_font(control_center_power_detail, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(control_center_power_detail, lv_color_hex(0xD8DEEA), 0);

    lv_obj_t *cards = lv_obj_create(control_center_panel);
    lv_obj_set_size(cards, lv_pct(100), CONTROL_CENTER_CARD_HEIGHT);
    lv_obj_align(cards, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_opa(cards, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cards, 0, 0);
    lv_obj_set_style_pad_all(cards, 0, 0);
    lv_obj_set_style_pad_column(cards, 20, 0);
    lv_obj_clear_flag(cards, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cards, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    control_center_wifi_detail =
        control_center_create_card(cards, "NETWORK", "Wi-Fi", "Not connected",
                                   lv_color_hex(0x4CB8FF),
                                   CONTROL_CENTER_ACTION_WIFI);

    lv_obj_t *hdmi_card = lv_obj_create(cards);
    lv_obj_set_size(hdmi_card, CONTROL_CENTER_CARD_WIDTH, CONTROL_CENTER_CARD_HEIGHT);
    lv_obj_set_style_radius(hdmi_card, 24, 0);
    lv_obj_set_style_bg_color(hdmi_card, lv_color_hex(0x3B4258), 0);
    lv_obj_set_style_bg_opa(hdmi_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdmi_card, 1, 0);
    lv_obj_set_style_border_color(hdmi_card, lv_color_hex(0x596179), 0);
    lv_obj_set_style_pad_all(hdmi_card, 18, 0);
    lv_obj_clear_flag(hdmi_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdmi_eyebrow = lv_label_create(hdmi_card);
    lv_label_set_text(hdmi_eyebrow, "VIDEO INPUT");
    lv_obj_align(hdmi_eyebrow, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(hdmi_eyebrow, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(hdmi_eyebrow, lv_color_hex(0xAEB7CB), 0);

    lv_obj_t *hdmi_title = lv_label_create(hdmi_card);
    lv_label_set_text(hdmi_title, "HDMI Display");
    lv_obj_align(hdmi_title, LV_ALIGN_LEFT_MID, 0, -4);
    lv_obj_set_style_text_font(hdmi_title, ui_font_h2(), 0);
    lv_obj_set_style_text_color(hdmi_title, lv_color_hex(0xFFFFFF), 0);

    control_center_hdmi_switch = lv_switch_create(hdmi_card);
    lv_obj_set_size(control_center_hdmi_switch, 60, 32);
    lv_obj_align(control_center_hdmi_switch, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_ext_click_area(control_center_hdmi_switch, 12);
    lv_obj_set_style_bg_color(control_center_hdmi_switch, UI_BORDER_DEFAULT,
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(control_center_hdmi_switch, UI_COLOR_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(control_center_hdmi_switch, lv_color_white(),
                              LV_PART_KNOB);
    lv_obj_add_event_cb(control_center_hdmi_switch,
                        on_control_center_hdmi_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    control_center_create_card(cards, "APPS", "User Apps", "Install and open apps",
                               lv_color_hex(0x42D3B0),
                               CONTROL_CENTER_ACTION_USER_APPS);
    control_center_create_card(cards, "SYSTEM", "Settings", "Display, sound, and theme",
                               lv_color_hex(0xA78BFA),
                               CONTROL_CENTER_ACTION_SETTINGS);

    lv_obj_t *handle = lv_obj_create(control_center_panel);
    lv_obj_set_size(handle, 72, 5);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(0x9AA4B8), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_80, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_set_style_pad_all(handle, 0, 0);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(control_center_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void control_center_show(void)
{
    control_center_build();
    if (!control_center_overlay || !control_center_panel || control_center_visible) return;

    if (control_center_wifi_detail) {
        if (wifi_connected && wifi_connected_ssid[0]) {
            lv_label_set_text_fmt(control_center_wifi_detail, "Connected\n%s",
                                  wifi_connected_ssid);
        } else if (!wifi_runtime_enabled) {
            lv_label_set_text(control_center_wifi_detail, "Wi-Fi is off");
        } else {
            lv_label_set_text(control_center_wifi_detail, "Not connected");
        }
    }
    if (control_center_power_detail && power_label) {
        lv_label_set_text(control_center_power_detail, lv_label_get_text(power_label));
    }
    hdmi_switch_sync(sys_settings_get()->hdmi_enabled);

    control_center_visible = true;
    lv_obj_clear_flag(control_center_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(control_center_overlay);
    lv_anim_delete(control_center_panel, control_center_anim_y);
    lv_obj_set_y(control_center_panel, -CONTROL_CENTER_HEIGHT - 24);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, control_center_panel);
    lv_anim_set_values(&anim, -CONTROL_CENTER_HEIGHT - 24, 12);
    lv_anim_set_time(&anim, 280);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, control_center_anim_y);
    lv_anim_start(&anim);
}

static void control_center_trigger_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED) {
        control_center_show();
    } else if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_active();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
            control_center_show();
        }
    }
}

/* =======================
 * 状态栏
 * ======================= */
void ui_statusbar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, HOME_CONTENT_W, HOME_STATUS_H);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bar, 18, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x101620), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, HOME_BORDER, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(bar, 10, 0);
    lv_obj_set_style_shadow_offset_y(bar, 4, 0);
    lv_obj_set_style_pad_hor(bar, 24, 0);
    lv_obj_set_style_pad_top(bar, 0, 0);
    lv_obj_set_style_pad_bottom(bar, 0, 0);
    lv_obj_set_style_pad_row(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bar,
                           LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER);
    apply_debug_outline(bar, lv_color_hex(0xE63946));

    lv_obj_t *top_row = lv_obj_create(bar);
    lv_obj_set_size(top_row, lv_pct(100), 62);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 0, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(top_row, LV_LAYOUT_NONE);
    apply_debug_outline(top_row, lv_color_hex(0xFF7F11));

    lv_obj_t *left_status_group = lv_obj_create(top_row);
    lv_obj_set_size(left_status_group, TOP_STATUS_GROUP_WIDTH, lv_pct(100));
    lv_obj_align(left_status_group, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(left_status_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_status_group, 0, 0);
    lv_obj_set_style_pad_all(left_status_group, 0, 0);
    lv_obj_set_style_pad_column(left_status_group, 12, 0);
    lv_obj_clear_flag(left_status_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left_status_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_status_group,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    apply_debug_outline(left_status_group, lv_color_hex(0x8EC63F));

    lv_obj_t *brand = lv_label_create(left_status_group);
    lv_label_set_text(brand, "Mimi");
    lv_obj_set_width(brand, 104);
    lv_obj_set_style_text_font(brand, ui_font_h1(), 0);
    lv_obj_set_style_text_color(brand, HOME_TEXT, 0);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_clear_flag(brand, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wifi_badge = lv_obj_create(left_status_group);
    lv_obj_set_size(wifi_badge, 48, 48);
    lv_obj_set_style_radius(wifi_badge, 0, 0);
    lv_obj_set_style_bg_opa(wifi_badge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_badge, 0, 0);
    lv_obj_set_style_outline_width(wifi_badge, 0, 0);
    lv_obj_set_style_shadow_width(wifi_badge, 0, 0);
    lv_obj_set_style_pad_all(wifi_badge, 0, 0);
    lv_obj_clear_flag(wifi_badge, LV_OBJ_FLAG_SCROLLABLE);
    apply_debug_outline(wifi_badge, lv_color_hex(0x2A9D8F));

    wifi_icon_obj = lv_image_create(wifi_badge);
    lv_image_set_src(wifi_icon_obj, &wireless_network_wifi);
    lv_obj_set_size(wifi_icon_obj, 34, 34);
    lv_image_set_inner_align(wifi_icon_obj, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_scale(wifi_icon_obj, LV_SCALE_NONE);
    lv_obj_set_style_image_recolor(wifi_icon_obj, lv_color_hex(0xF7F8FC), 0);
    lv_obj_set_style_image_recolor_opa(wifi_icon_obj, LV_OPA_COVER, 0);
    lv_obj_center(wifi_icon_obj);

    /* 100ask 云徽章：云图 + 右下角状态点 + 底边 OTA 下载进度条。
     * 点颜色：灰=离线 / 绿=已连 / 蓝=下载中 / 橙=待安装（见 ui_helpers）。 */
    lv_obj_t *cloud_badge = lv_obj_create(left_status_group);
    lv_obj_set_size(cloud_badge, 54, 48);
    lv_obj_set_style_radius(cloud_badge, 0, 0);
    lv_obj_set_style_bg_opa(cloud_badge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cloud_badge, 0, 0);
    lv_obj_set_style_outline_width(cloud_badge, 0, 0);
    lv_obj_set_style_shadow_width(cloud_badge, 0, 0);
    lv_obj_set_style_pad_all(cloud_badge, 0, 0);
    lv_obj_set_layout(cloud_badge, LV_LAYOUT_NONE);
    lv_obj_clear_flag(cloud_badge, LV_OBJ_FLAG_SCROLLABLE);

    cloud_icon_obj = lv_image_create(cloud_badge);
    lv_image_set_src(cloud_icon_obj, &cloud);
    lv_obj_set_size(cloud_icon_obj, 40, 40);
    lv_image_set_inner_align(cloud_icon_obj, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_style_image_recolor(cloud_icon_obj, lv_color_hex(0xF7F8FC), 0);
    lv_obj_set_style_image_recolor_opa(cloud_icon_obj, LV_OPA_COVER, 0);
    lv_obj_align(cloud_icon_obj, LV_ALIGN_TOP_MID, 0, 2);

    cloud_status_dot_obj = lv_obj_create(cloud_badge);
    lv_obj_set_size(cloud_status_dot_obj, 10, 10);
    lv_obj_set_style_radius(cloud_status_dot_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cloud_status_dot_obj, lv_color_hex(0xA6B2BA), 0);
    lv_obj_set_style_bg_opa(cloud_status_dot_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cloud_status_dot_obj, 1, 0);
    lv_obj_set_style_border_color(cloud_status_dot_obj, lv_color_hex(0x303548), 0);
    lv_obj_set_style_border_opa(cloud_status_dot_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(cloud_status_dot_obj, 0, 0);
    lv_obj_clear_flag(cloud_status_dot_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cloud_status_dot_obj, LV_ALIGN_TOP_RIGHT, -4, 28);

    /* 2px track under the cloud; fill width driven by OTA progress (0–100). */
    cloud_progress_track_obj = lv_obj_create(cloud_badge);
    lv_obj_set_size(cloud_progress_track_obj, 40, 3);
    lv_obj_align(cloud_progress_track_obj, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_radius(cloud_progress_track_obj, 2, 0);
    lv_obj_set_style_bg_color(cloud_progress_track_obj, lv_color_hex(0x464C61), 0);
    lv_obj_set_style_bg_opa(cloud_progress_track_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cloud_progress_track_obj, 0, 0);
    lv_obj_set_style_pad_all(cloud_progress_track_obj, 0, 0);
    lv_obj_set_layout(cloud_progress_track_obj, LV_LAYOUT_NONE);
    lv_obj_clear_flag(cloud_progress_track_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cloud_progress_track_obj, LV_OBJ_FLAG_HIDDEN);

    cloud_progress_fill_obj = lv_obj_create(cloud_progress_track_obj);
    lv_obj_set_size(cloud_progress_fill_obj, 0, 3);
    lv_obj_align(cloud_progress_fill_obj, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(cloud_progress_fill_obj, 2, 0);
    lv_obj_set_style_bg_color(cloud_progress_fill_obj, lv_color_hex(0x5B9BD5), 0);
    lv_obj_set_style_bg_opa(cloud_progress_fill_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cloud_progress_fill_obj, 0, 0);
    lv_obj_set_style_pad_all(cloud_progress_fill_obj, 0, 0);
    lv_obj_clear_flag(cloud_progress_fill_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 顶栏中间：INA219 整机功耗/电流/电压。相对 top_row 绝对居中，不受左右区域宽度影响。
     * 主题主色纯色（随主题切换），不上云，仅本机显示。 */
    lv_obj_t *mid_col = lv_obj_create(top_row);
    lv_obj_set_size(mid_col, TOP_POWER_LABEL_WIDTH, lv_pct(100));
    lv_obj_align(mid_col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(mid_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mid_col, 0, 0);
    lv_obj_set_style_pad_all(mid_col, 0, 0);
    lv_obj_clear_flag(mid_col, LV_OBJ_FLAG_SCROLLABLE);
    /* Power readout is display-only; don't steal taps from the HDMI switch. */
    lv_obj_clear_flag(mid_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(mid_col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid_col,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    apply_debug_outline(mid_col, lv_color_hex(0x06D6A0));

    power_label = lv_label_create(mid_col);
    lv_label_set_text(power_label, "--.-V ---mA --.-W");
    lv_label_set_long_mode(power_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(power_label, TOP_POWER_LABEL_WIDTH);
    lv_obj_set_style_text_font(power_label, ui_font_h3(), 0);
    lv_obj_set_style_text_color(power_label, lv_color_hex(0xE0E3EC), 0);
    lv_obj_set_style_text_align(power_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(power_label, LV_OPA_90, 0);
    lv_obj_clear_flag(power_label, LV_OBJ_FLAG_CLICKABLE);
    /* The redesigned home overlays time/date in the center. Keep the power
     * object alive for backend updates and expose its detail in Control
     * Center, but do not paint two competing readouts in the status bar. */
    lv_obj_add_flag(power_label, LV_OBJ_FLAG_HIDDEN);

    /* Tap the center readout or swipe down to reveal the control center. */
    lv_obj_add_flag(mid_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(mid_col, 8);
    lv_obj_add_event_cb(mid_col, control_center_trigger_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mid_col, control_center_trigger_event, LV_EVENT_GESTURE, NULL);

    lv_obj_t *pull_handle = lv_obj_create(mid_col);
    lv_obj_set_size(pull_handle, 40, 4);
    lv_obj_align(pull_handle, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_radius(pull_handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pull_handle, lv_color_hex(0xAEB7CB), 0);
    lv_obj_set_style_bg_opa(pull_handle, LV_OPA_70, 0);
    lv_obj_set_style_border_width(pull_handle, 0, 0);
    lv_obj_set_style_pad_all(pull_handle, 0, 0);
    lv_obj_clear_flag(pull_handle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *right_col = lv_obj_create(top_row);
    lv_obj_set_size(right_col, HDMI_MODE_ROW_WIDTH + 12, lv_pct(100));
    lv_obj_align(right_col, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_style_pad_all(right_col, 0, 0);
    lv_obj_set_style_pad_row(right_col, 6, 0);
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    /* Keep right column above mid_col for hit-testing. */
    lv_obj_move_foreground(right_col);
    apply_debug_outline(right_col, lv_color_hex(0x118AB2));

    lv_obj_t *hdmi_row = lv_obj_create(right_col);
    lv_obj_set_size(hdmi_row, HDMI_MODE_ROW_WIDTH + 8, 48);
    lv_obj_set_style_min_width(hdmi_row, HDMI_MODE_ROW_WIDTH, 0);
    lv_obj_set_style_bg_opa(hdmi_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdmi_row, 0, 0);
    lv_obj_set_style_pad_all(hdmi_row, 0, 0);
    lv_obj_set_style_pad_column(hdmi_row, 10, 0);
    lv_obj_clear_flag(hdmi_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hdmi_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(hdmi_row, 8);
    lv_obj_set_flex_flow(hdmi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdmi_row,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    apply_debug_outline(hdmi_row, lv_color_hex(0x3A86FF));

    lv_obj_t *hdmi_label = lv_label_create(hdmi_row);
    lv_label_set_text(hdmi_label, "Display Mode");
    lv_label_set_long_mode(hdmi_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(hdmi_label, HDMI_MODE_LABEL_WIDTH);
    lv_obj_set_style_min_width(hdmi_label, HDMI_MODE_LABEL_WIDTH, 0);
    lv_obj_set_style_text_color(hdmi_label, lv_color_hex(0xF7F8FC), 0);
    /* 用带 CJK 的主题字体：font_interdisplay_regular_36 只含 ASCII（range 32-126），
     * 渲染中文会乱码。顶栏中文文字须用 ui_font_* 系列。 */
    lv_obj_set_style_text_font(hdmi_label, ui_font_h3(), 0);
    lv_obj_set_style_text_opa(hdmi_label, LV_OPA_90, 0);
    lv_obj_clear_flag(hdmi_label, LV_OBJ_FLAG_CLICKABLE);

    hdmi_switch_obj = lv_switch_create(hdmi_row);
    lv_obj_set_size(hdmi_switch_obj, 56, 30);
    lv_obj_set_ext_click_area(hdmi_switch_obj, 16);
    lv_obj_set_style_bg_color(hdmi_switch_obj, UI_BORDER_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdmi_switch_obj, UI_COLOR_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(hdmi_switch_obj, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(hdmi_switch_obj, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_shadow_color(hdmi_switch_obj, UI_COLOR_PRIMARY, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(hdmi_switch_obj, 8, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_opa(hdmi_switch_obj, LV_OPA_20, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_add_event_cb(hdmi_switch_obj, on_hdmi_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* Tap the whole row (label included) to flip the switch. */
    lv_obj_add_event_cb(hdmi_row, on_hdmi_row_clicked, LV_EVENT_CLICKED, NULL);

    /* 初始开关态取自持久化设置（默认关闭）。 */
    hdmi_switch_sync(sys_settings_get()->hdmi_enabled);

    refresh_wifi_icon_visual();
}

/* =======================
 * HDMI 显示模式开关
 * ======================= */

static void hdmi_clear_pending(void)
{
    hdmi_toggle_pending = false;
    if (hdmi_pending_timer) {
        lv_timer_del(hdmi_pending_timer);
        hdmi_pending_timer = NULL;
    }
}

static void hdmi_pending_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    hdmi_pending_timer = NULL;
    if (!hdmi_toggle_pending) return;
    printf("[HDMI] toggle ack timeout, keep UI target=%d\n",
           hdmi_toggle_target ? 1 : 0);
    hdmi_toggle_pending = false;
    /* Keep optimistic UI; user can toggle again freely. */
}

static void hdmi_switch_apply_visual(lv_obj_t *sw, bool enabled)
{
    if (!sw || !lv_obj_is_valid(sw)) return;
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (checked == enabled) return;

    hdmi_ignore_event = true;
    if (enabled)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    hdmi_ignore_event = false;
}

static void hdmi_switch_sync(bool enabled)
{
    hdmi_switch_apply_visual(hdmi_switch_obj, enabled);
    hdmi_switch_apply_visual(hdmi_overlay_switch, enabled);
    hdmi_switch_apply_visual(control_center_hdmi_switch, enabled);
}

static void hdmi_send_enable(bool on)
{
    hdmi_toggle_pending = true;
    hdmi_toggle_target = on;

    /* Optimistic UI + persist; backend ack only confirms / may re-sync. */
    hdmi_switch_sync(on);
    sys_settings_t *s = sys_settings_get();
    if (s->hdmi_enabled != on) {
        s->hdmi_enabled = on;
        sys_settings_save();
    }

    if (hdmi_pending_timer) {
        lv_timer_del(hdmi_pending_timer);
        hdmi_pending_timer = NULL;
    }
    hdmi_pending_timer = lv_timer_create(hdmi_pending_timeout_cb, 2500, NULL);
    lv_timer_set_repeat_count(hdmi_pending_timer, 1);

    hdmi_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = on ? HDMI_CMD_ENABLE : HDMI_CMD_DISABLE;
    mw_publish(TOPIC_HDMI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    printf("[HDMI] request enable=%d\n", on ? 1 : 0);
}

static void on_hdmi_switch_changed(lv_event_t *e)
{
    if (hdmi_ignore_event) return;

    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    /* Always accept the latest user intent; do not lock the switch. */
    hdmi_send_enable(on);
}

static void on_hdmi_row_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!hdmi_switch_obj) return;

    /*
     * Row CLICKED bubbles from children. If the press hit the switch, skip —
     * VALUE_CHANGED already issued the command (avoid double-toggle).
     */
    lv_obj_t *row = lv_event_get_current_target(e);
    lv_obj_t *orig = lv_event_get_target(e);
    for (lv_obj_t *o = orig; o && o != row; o = lv_obj_get_parent(o)) {
        if (o == hdmi_switch_obj) return;
    }
    if (orig == hdmi_switch_obj) return;

    bool on = !lv_obj_has_state(hdmi_switch_obj, LV_STATE_CHECKED);
    hdmi_send_enable(on);
}

static void on_hdmi_overlay_switch_changed(lv_event_t *e)
{
    if (hdmi_ignore_event) return;
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    hdmi_send_enable(on);
}

static void on_control_center_hdmi_changed(lv_event_t *e)
{
    if (hdmi_ignore_event) return;
    lv_obj_t *sw = lv_event_get_target(e);
    hdmi_send_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* Floating control while the transparent HDMI screen covers the desktop. */
static void hdmi_overlay_build(lv_obj_t *screen)
{
    if (!screen) return;

    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, 220, 52);
    lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_set_style_radius(bar, 16, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x303548), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x464C61), 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, "Display Mode");
    lv_obj_set_style_text_font(label, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7F8FC), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

    hdmi_overlay_switch = lv_switch_create(bar);
    lv_obj_set_size(hdmi_overlay_switch, 52, 28);
    lv_obj_set_ext_click_area(hdmi_overlay_switch, 12);
    lv_obj_set_style_bg_color(hdmi_overlay_switch, UI_BORDER_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdmi_overlay_switch, UI_COLOR_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(hdmi_overlay_switch, LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(hdmi_overlay_switch, lv_color_white(), LV_PART_KNOB);
    hdmi_switch_apply_visual(hdmi_overlay_switch, true);
    lv_obj_add_event_cb(hdmi_overlay_switch, on_hdmi_overlay_switch_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/* 由 hdmi_preview_status_callback 调用：同步开关、ack 后持久化、显示/隐藏 HDMI 透明屏。 */
void hdmi_ui_handle_status(const hdmi_preview_status_t *status)
{
    if (!status) return;

    if (hdmi_toggle_pending) {
        if (status->enabled == hdmi_toggle_target) {
            hdmi_clear_pending();
            hdmi_switch_sync(status->enabled);
        }
        /* While waiting, ignore opposite enabled frames so the knob does not bounce. */
    } else {
        hdmi_switch_sync(status->enabled);
    }

    if (status->active) {
        app_manager_show_hdmi_preview_screen();
        /* Attach floating switch once the transparent screen is active. */
        lv_obj_t *scr = lv_scr_act();
        if (scr && !hdmi_overlay_switch) {
            hdmi_overlay_build(scr);
        } else {
            hdmi_switch_apply_visual(hdmi_overlay_switch, status->enabled);
        }
    } else {
        hdmi_overlay_switch = NULL;
        app_manager_hide_hdmi_preview_screen();
    }
}
