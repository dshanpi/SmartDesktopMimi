#include "setting_app.h"
#include "../../system/app_manager.h"
#include "../../system/device_identity.h"
#include "../../system/backlight_control.h"
#include "../../system/volume_control.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"
#include "../../ui/desktop_clock.h"
#include "../../system/settings.h"
#include "../../system/backend_types.h"
#include "../../middleware/middleware.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_IMG_DECLARE(light_mode_32dp_1F1F1F)
LV_IMG_DECLARE(volume_up_32dp_1F1F1F)
LV_FONT_DECLARE(font_inter_semibold_96)

static bool settings_dirty = false;
static lv_obj_t *preview_clock_label = NULL;
static lv_obj_t *preview_date_label = NULL;
static lv_obj_t *preview_brightness_label = NULL;
static lv_obj_t *preview_volume_label = NULL;
static lv_obj_t *preview_volume_bars[3] = {NULL, NULL, NULL};
static lv_timer_t *preview_clock_timer = NULL;
static lv_obj_t *bind_code_label = NULL;
static lv_obj_t *bind_code_item = NULL;

static void make_transparent(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_surface(lv_obj_t *obj, lv_color_t bg, int32_t radius)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xE0E3EA), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void back_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        printf("Back button clicked\n");
        app_manager_back_home();
    }
}

static void read_system_version(char *out, size_t out_len)
{
    snprintf(out, out_len, "%s", APP_SYSTEM_VERSION);
    FILE *fp = fopen("/etc/aitvbox-version", "r");
    if (!fp) return;

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "AITVBOX_VERSION=", 16) == 0) {
            char *v = line + 16;
            size_t n = strlen(v);
            while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r')) v[--n] = '\0';
            if (n > 0) snprintf(out, out_len, "%s", v);
            break;
        }
    }
    fclose(fp);
}

static void request_fresh_bind_token(void)
{
    cloud_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = CLOUD_CMD_REFRESH_BIND_TOKEN;
    mw_publish(TOPIC_CLOUD_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static void bind_token_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (bind_code_item && !lv_obj_has_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE)) return;
    if (bind_code_label) lv_label_set_text(bind_code_label, "Loading...");
    request_fresh_bind_token();
}

static void setting_cloud_status_cb(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_CLOUD_STATUS ||
        msg->data_len != sizeof(cloud_status_t) || !bind_code_label) {
        return;
    }

    const cloud_status_t *status = (const cloud_status_t *)msg->data;
    if (status->state == CLOUD_STATE_UNAVAILABLE) {
        lv_label_set_text(bind_code_label, "Cloud service unavailable in this build");
        if (bind_code_item) lv_obj_clear_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
    } else if (status->bind_token_refreshing) {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, "Loading...");
    } else if (status->bind_token[0]) {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, status->bind_token);
    } else if (status->bind_token_error != 0) {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, "Failed · Tap to retry");
    } else {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, "Tap to get pairing code");
    }
}

static void update_preview_volume_bars(int32_t value)
{
    for (int i = 0; i < 3; i++) {
        if (!preview_volume_bars[i]) continue;
        int32_t threshold = 18 + i * 28;
        lv_obj_set_style_bg_opa(preview_volume_bars[i],
                                value >= threshold ? LV_OPA_COVER : LV_OPA_30,
                                0);
    }
}

/* Apply hardware + memory on drag; persist only when the finger lifts so we
 * do not rewrite /overlay on every VALUE_CHANGED tick. */
static void brightness_slider_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
    int32_t value = lv_slider_get_value(slider);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text_fmt(value_label, "%d%%", value);
        if (preview_brightness_label) {
            lv_label_set_text_fmt(preview_brightness_label, "Brightness %d%%", value);
        }

        sys_settings_t *settings = sys_settings_get();
        settings->brightness = value;
        settings_dirty = true;
        sys_backlight_set_percent(value);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        sys_settings_t *settings = sys_settings_get();
        settings->brightness = value;
        sys_settings_save();
        settings_dirty = false;
    }
}

static void volume_slider_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
    int32_t value = lv_slider_get_value(slider);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text_fmt(value_label, "%d%%", value);
        if (preview_volume_label) {
            lv_label_set_text_fmt(preview_volume_label, "Volume %d%%", value);
        }
        update_preview_volume_bars(value);

        sys_settings_t *settings = sys_settings_get();
        settings->volume = value;
        settings_dirty = true;
        sys_volume_set_percent(value);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        sys_settings_t *settings = sys_settings_get();
        settings->volume = value;
        sys_settings_save();
        settings_dirty = false;
    }
}

static void setting_init(void);

static void setting_rebuild_async(void *unused)
{
    (void)unused;
    lv_obj_t *old = lv_scr_act();
    setting_init();
    if (old) lv_obj_del_async(old);
}

static void color_event_cb(lv_event_t *e)
{
    uint32_t *preset_id_ptr = (uint32_t *)lv_event_get_user_data(e);
    lv_obj_t *dot = lv_event_get_target(e);
    if (!preset_id_ptr) return;

    /* Manual pick locks the accent (turns off follow-time). */
    printf("Gradient preset clicked: %u (follow-time off)\n",
           (unsigned)*preset_id_ptr);
    ui_theme_set_gradient_preset(*preset_id_ptr);
    ui_clock_apply_theme();

    lv_obj_t *cont = lv_obj_get_parent(dot);
    uint32_t child_cnt = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        lv_obj_t *marker = lv_obj_get_child(child, 0);
        lv_obj_set_style_border_width(child, 0, 0);
        lv_obj_set_style_outline_width(child, 0, 0);
        if (marker) lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_set_style_border_color(dot, UI_BG_CARD, 0);
    lv_obj_set_style_outline_width(dot, 2, 0);
    lv_obj_set_style_outline_color(dot, UI_COLOR_PRIMARY, 0);
    lv_obj_t *marker = lv_obj_get_child(dot, 0);
    if (marker) lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_async_call(setting_rebuild_async, NULL);
}

static void follow_time_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    printf("Theme follow-time: %s\n", on ? "on" : "off");
    ui_theme_set_follow_time(on);
    if (on) {
        ui_clock_apply_theme();
        lv_async_call(setting_rebuild_async, NULL);
    }
}

static void style_dark_header(lv_obj_t *header)
{
    if (!header) return;

    lv_obj_set_style_bg_color(header, lv_color_hex(0x303548), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);

    if (lv_obj_get_child_count(header) < 2) return;
    lv_obj_t *back_btn = lv_obj_get_child(header, 0);
    lv_obj_t *title = lv_obj_get_child(header, 1);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF7F8FC), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444B61), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, UI_RADIUS_LG, 0);

    if (lv_obj_get_child_count(back_btn) > 0) {
        lv_obj_t *back_icon = lv_obj_get_child(back_btn, 0);
        lv_obj_set_style_img_recolor(back_icon, lv_color_hex(0xF7F8FC), 0);
        lv_obj_set_style_img_recolor_opa(back_icon, LV_OPA_COVER, 0);
    }
}

static void preview_clock_update(lv_timer_t *timer)
{
    (void)timer;
    if (!preview_clock_label || !preview_date_label) return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    static const char *weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    int hour12 = tm_now.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;

    lv_label_set_text_fmt(preview_clock_label, "%d:%02d", hour12, tm_now.tm_min);
    lv_label_set_text_fmt(preview_date_label,
                          "%s, %02d/%02d/%04d  %s",
                          weekdays[tm_now.tm_wday],
                          tm_now.tm_mon + 1,
                          tm_now.tm_mday,
                          tm_now.tm_year + 1900,
                          tm_now.tm_hour < 12 ? "AM" : "PM");
}

static lv_obj_t *create_preview_card(lv_obj_t *parent,
                                     int32_t brightness,
                                     int32_t volume)
{
    lv_obj_t *preview = lv_obj_create(parent);
    lv_obj_set_size(preview, 1, lv_pct(100));
    lv_obj_set_flex_grow(preview, 1);
    style_surface(preview, lv_color_hex(0xF1F3F7), 20);
    lv_obj_set_style_pad_all(preview, 16, 0);
    lv_obj_set_style_pad_row(preview, 10, 0);
    lv_obj_set_flex_flow(preview, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(preview,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(preview);
    lv_label_set_text(title, "Desktop Preview");
    lv_obj_set_style_text_font(title, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x303746), 0);
    lv_obj_set_width(title, lv_pct(100));

    lv_obj_t *face = lv_obj_create(preview);
    lv_obj_set_size(face, lv_pct(100), 1);
    lv_obj_set_flex_grow(face, 1);
    lv_obj_set_style_bg_color(face, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_grad(face, ui_theme_get_active_gradient(), 0);
    lv_obj_set_style_border_width(face, 1, 0);
    lv_obj_set_style_border_color(face, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(face, LV_OPA_40, 0);
    lv_obj_set_style_radius(face, 18, 0);
    lv_obj_set_style_shadow_width(face, 14, 0);
    lv_obj_set_style_shadow_color(face, lv_color_hex(0x68718A), 0);
    lv_obj_set_style_shadow_opa(face, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(face, 4, 0);
    lv_obj_set_style_pad_all(face, 0, 0);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *face_caption = lv_label_create(face);
    lv_label_set_text(face_caption, "AI Desktop");
    lv_obj_set_style_text_font(face_caption, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(face_caption, lv_color_hex(0xEDF0FF), 0);
    lv_obj_align(face_caption, LV_ALIGN_TOP_LEFT, 18, 14);

    preview_clock_label = lv_label_create(face);
    lv_label_set_text(preview_clock_label, "00:00");
    lv_obj_set_style_text_font(preview_clock_label, &font_inter_semibold_96, 0);
    lv_obj_set_style_text_color(preview_clock_label, lv_color_hex(0xFFFDF8), 0);
    lv_obj_align(preview_clock_label, LV_ALIGN_CENTER, 0, -28);

    preview_date_label = lv_label_create(face);
    lv_label_set_text(preview_date_label, "");
    lv_obj_set_style_text_font(preview_date_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(preview_date_label, lv_color_hex(0xEDF0FF), 0);
    lv_obj_align(preview_date_label, LV_ALIGN_CENTER, 0, 42);

    lv_obj_t *separator = lv_obj_create(face);
    lv_obj_set_size(separator, lv_pct(90), 1);
    lv_obj_align(separator, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0xD9DEFF), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_40, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    preview_brightness_label = lv_label_create(face);
    lv_label_set_text_fmt(preview_brightness_label, "Brightness %d%%", brightness);
    lv_obj_set_style_text_font(preview_brightness_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(preview_brightness_label, lv_color_hex(0xF7F8FF), 0);
    lv_obj_align(preview_brightness_label, LV_ALIGN_BOTTOM_LEFT, 18, -20);

    preview_volume_label = lv_label_create(face);
    lv_label_set_text_fmt(preview_volume_label, "Volume %d%%", volume);
    lv_obj_set_style_text_font(preview_volume_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(preview_volume_label, lv_color_hex(0xF7F8FF), 0);
    lv_obj_align(preview_volume_label, LV_ALIGN_BOTTOM_MID, 10, -20);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *bar = lv_obj_create(face);
        lv_obj_set_size(bar, 5, 10 + i * 7);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_RIGHT, -44 + i * 12, -18);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        preview_volume_bars[i] = bar;
    }
    update_preview_volume_bars(volume);

    preview_clock_update(NULL);
    return preview;
}

static lv_obj_t *create_vertical_control(lv_obj_t *parent,
                                         const void *icon_src,
                                         const char *title,
                                         int32_t value,
                                         lv_event_cb_t callback)
{
    lv_obj_t *column = lv_obj_create(parent);
    lv_obj_set_size(column, 104, lv_pct(100));
    make_transparent(column);
    lv_obj_set_style_pad_row(column, 6, 0);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon_plate = lv_obj_create(column);
    lv_obj_set_size(icon_plate, 40, 40);
    lv_obj_set_style_bg_color(icon_plate,
                              lv_color_mix(UI_COLOR_PRIMARY, lv_color_hex(0xFFFFFF), 220),
                              0);
    lv_obj_set_style_bg_opa(icon_plate, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(icon_plate, 1, 0);
    lv_obj_set_style_border_color(icon_plate, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_opa(icon_plate, LV_OPA_50, 0);
    lv_obj_set_style_radius(icon_plate, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(icon_plate, 0, 0);
    lv_obj_clear_flag(icon_plate, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *icon = lv_img_create(icon_plate);
    lv_img_set_src(icon, icon_src);
    /* White glyph on the primary-tinted plate; dark icons wash out on deep themes. */
    lv_obj_set_style_img_recolor(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_center(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title_label = lv_label_create(column);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x747B89), 0);

    lv_obj_t *slider = lv_slider_create(column);
    lv_obj_set_width(slider, 10);
    lv_obj_set_flex_grow(slider, 1);
    /* The R818 backlight is not reliably visible below 20%.  Keep the UI
     * range consistent with settings normalization at startup. */
    lv_slider_set_range(slider, 20, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    /* Track: soft gray rail */
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xD7DAE1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    /* Filled portion follows theme primary */
    lv_obj_set_style_bg_color(slider, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    /*
     * Knob: pure white disc + primary ring (not dark gray — reads dirty on light UI).
     * Shadow stays soft neutral, not heavy charcoal.
     */
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 3, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, UI_COLOR_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 10, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, UI_COLOR_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_20, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 9, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 24);

    lv_obj_t *value_label = lv_label_create(column);
    lv_label_set_text_fmt(value_label, "%d%%", value);
    lv_obj_set_style_text_font(value_label, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x303746), 0);

    /* VALUE_CHANGED: live preview; RELEASED/PRESS_LOST: persist to disk. */
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, value_label);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_RELEASED, value_label);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_PRESS_LOST, value_label);
    return column;
}

/* Theme block: reserved bottom of the control rail (flex child, no absolute y). */
static void create_theme_selector(lv_obj_t *rail)
{
    lv_obj_t *section = lv_obj_create(rail);
    lv_obj_set_size(section, lv_pct(100), LV_SIZE_CONTENT);
    make_transparent(section);
    lv_obj_set_style_pad_left(section, 16, 0);
    lv_obj_set_style_pad_right(section, 16, 0);
    lv_obj_set_style_pad_top(section, 10, 0);
    lv_obj_set_style_pad_bottom(section, 14, 0);
    lv_obj_set_style_pad_row(section, 8, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(section,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *divider = lv_obj_create(section);
    lv_obj_set_size(divider, lv_pct(100), 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xE0E3EA), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 跟随时间 */
    lv_obj_t *follow_row = lv_obj_create(section);
    lv_obj_set_size(follow_row, lv_pct(100), 30);
    make_transparent(follow_row);
    lv_obj_set_flex_flow(follow_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(follow_row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *follow_label = lv_label_create(follow_row);
    lv_label_set_text(follow_label, "Follow Time");
    lv_obj_set_style_text_font(follow_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(follow_label, lv_color_hex(0x747B89), 0);

    lv_obj_t *follow_sw = lv_switch_create(follow_row);
    lv_obj_set_size(follow_sw, 44, 24);
    lv_obj_set_style_bg_color(follow_sw, lv_color_hex(0xD0D5DE), 0);
    lv_obj_set_style_bg_color(follow_sw, UI_COLOR_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (ui_theme_get_follow_time()) {
        lv_obj_add_state(follow_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(follow_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(follow_sw, follow_time_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 卡片主题 + 色点（独占一行，避免与文案横向挤叠） */
    lv_obj_t *theme_label = lv_label_create(section);
    lv_label_set_text(theme_label, "Card Theme");
    lv_obj_set_width(theme_label, lv_pct(100));
    lv_obj_set_style_text_font(theme_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(theme_label, lv_color_hex(0x747B89), 0);

    lv_obj_t *colors = lv_obj_create(section);
    lv_obj_set_size(colors, lv_pct(100), 32);
    make_transparent(colors);
    lv_obj_set_flex_flow(colors, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(colors, 6, 0);
    lv_obj_set_flex_align(colors,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    static uint32_t preset_ids[UI_THEME_GRADIENT_PRESET_COUNT] = {
        0U, 1U, 2U, 3U, 4U
    };

    for (uint32_t i = 0; i < UI_THEME_GRADIENT_PRESET_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(colors);
        lv_obj_set_size(dot, 42, 28);
        lv_obj_set_style_radius(dot, 8, 0);
        lv_obj_set_style_bg_color(dot,
                                  ui_theme_get_gradient_color(i, 0), 0);
        lv_obj_set_style_bg_grad(dot, ui_theme_get_gradient(i), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_shadow_width(dot, 3, 0);
        lv_obj_set_style_shadow_color(dot, lv_color_hex(0x697286), 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_20, 0);
        lv_obj_set_style_shadow_offset_y(dot, 1, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_outline_pad(dot, 1, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *marker = lv_obj_create(dot);
        lv_obj_set_size(marker, 6, 6);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(marker, 0, 0);
        lv_obj_center(marker);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(dot, color_event_cb, LV_EVENT_CLICKED,
                            &preset_ids[i]);

        if (i == ui_theme_get_gradient_preset_id()) {
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_outline_width(dot, 2, 0);
            lv_obj_set_style_outline_color(dot,
                                            ui_theme_get_gradient_color(i, 0),
                                            0);
            lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_obj_t *create_control_rail(lv_obj_t *parent,
                                     int32_t brightness,
                                     int32_t volume)
{
    lv_obj_t *rail = lv_obj_create(parent);
    lv_obj_set_size(rail, 286, lv_pct(100));
    style_surface(rail, lv_color_hex(0xF7F8FB), 20);
    lv_obj_set_style_pad_all(rail, 0, 0);
    lv_obj_set_style_pad_top(rail, 14, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rail,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(rail);
    lv_label_set_text(title, "Display & Sound");
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_pad_left(title, 18, 0);
    lv_obj_set_style_pad_right(title, 18, 0);
    lv_obj_set_style_pad_bottom(title, 6, 0);
    lv_obj_set_style_text_font(title, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x303746), 0);

    /* Sliders take remaining height; theme block stays below without overlap. */
    lv_obj_t *controls = lv_obj_create(rail);
    lv_obj_set_size(controls, lv_pct(100), 1);
    lv_obj_set_flex_grow(controls, 1);
    make_transparent(controls);
    lv_obj_set_style_pad_top(controls, 4, 0);
    lv_obj_set_style_pad_bottom(controls, 4, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    create_vertical_control(controls,
                            &light_mode_32dp_1F1F1F,
                            "Brightness",
                            brightness,
                            brightness_slider_cb);
    create_vertical_control(controls,
                            &volume_up_32dp_1F1F1F,
                            "Volume",
                            volume,
                            volume_slider_cb);

    create_theme_selector(rail);
    return rail;
}

static lv_obj_t *create_meta_item(lv_obj_t *parent,
                                  const char *key,
                                  const char *value,
                                  lv_obj_t **value_label_out)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, 1, lv_pct(100));
    lv_obj_set_flex_grow(item, 1);
    make_transparent(item);
    lv_obj_set_style_pad_row(item, 6, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *key_label = lv_label_create(item);
    lv_label_set_text(key_label, key);
    lv_obj_set_style_text_font(key_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(key_label, lv_color_hex(0x7B8290), 0);

    lv_obj_t *value_label = lv_label_create(item);
    lv_label_set_text(value_label, value);
    lv_obj_set_width(value_label, lv_pct(100));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(value_label, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x303746), 0);
    if (value_label_out) *value_label_out = value_label;
    return item;
}

static lv_obj_t *create_device_strip(lv_obj_t *parent,
                                     const char *system_version,
                                     const char *bind_code)
{
    lv_obj_t *strip = lv_obj_create(parent);
    lv_obj_set_size(strip, lv_pct(100), 112);
    style_surface(strip, lv_color_hex(0xF7F8FB), 18);
    lv_obj_set_style_pad_all(strip, 18, 0);
    lv_obj_set_style_pad_column(strip, 18, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *section_title = lv_label_create(strip);
    lv_label_set_text(section_title, "Device Info");
    lv_obj_set_width(section_title, 112);
    lv_obj_set_style_text_font(section_title, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(section_title, lv_color_hex(0x303746), 0);

    lv_obj_t *divider = lv_obj_create(strip);
    lv_obj_set_size(divider, 1, lv_pct(70));
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xE0E3EA), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    create_meta_item(strip, "Device Name", APP_DEVICE_NAME, NULL);
    create_meta_item(strip, "System Version", system_version, NULL);
    bind_code_item = create_meta_item(strip, "Pairing Code", bind_code, &bind_code_label);
    lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bind_code_item, bind_token_click_cb, LV_EVENT_CLICKED, NULL);
    return strip;
}

static void setting_init(void)
{
    printf("Setting App Init\n");
    int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    int32_t screen_h = lv_display_get_vertical_resolution(NULL);
    if (screen_w <= 0) screen_w = 1024;
    if (screen_h <= 0) screen_h = 768;

    if (preview_clock_timer) {
        lv_timer_del(preview_clock_timer);
        preview_clock_timer = NULL;
    }
    preview_clock_label = NULL;
    preview_date_label = NULL;
    preview_brightness_label = NULL;
    preview_volume_label = NULL;
    bind_code_item = NULL;
    for (int i = 0; i < 3; i++) preview_volume_bars[i] = NULL;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xE8ECF1), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0xDDE3EA), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *upper_field = lv_obj_create(scr);
    lv_obj_set_size(upper_field, lv_pct(100), 184);
    lv_obj_align(upper_field, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(upper_field, lv_color_hex(0x303548), 0);
    lv_obj_set_style_bg_opa(upper_field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(upper_field, 0, 0);
    lv_obj_set_style_radius(upper_field, 0, 0);
    lv_obj_set_style_pad_all(upper_field, 0, 0);
    lv_obj_clear_flag(upper_field, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *header = ui_create_app_header(scr,
                                             "Settings",
                                             back_event_handler,
                                             false,
                                             NULL);
    style_dark_header(header);

    int32_t panel_x = 24;
    int32_t panel_y = 100;
    int32_t panel_w = LV_MAX(320, screen_w - 48);
    int32_t panel_h = LV_MAX(420, screen_h - panel_y - 24);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_set_pos(panel, panel_x, panel_y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFCFCFE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_shadow_width(panel, 24, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x788294), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_set_style_pad_row(panel, 16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    sys_settings_t *settings = sys_settings_get();

    lv_obj_t *main_row = lv_obj_create(panel);
    lv_obj_set_size(main_row, lv_pct(100), 1);
    lv_obj_set_flex_grow(main_row, 1);
    make_transparent(main_row);
    lv_obj_set_style_pad_column(main_row, 16, 0);
    lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    create_preview_card(main_row, settings->brightness, settings->volume);
    create_control_rail(main_row, settings->brightness, settings->volume);

    char system_version[64];
    read_system_version(system_version, sizeof(system_version));
    create_device_strip(panel, system_version, "Loading...");

    mw_subscribe(TOPIC_CLOUD_STATUS, setting_cloud_status_cb);
    request_fresh_bind_token();

    preview_clock_timer = lv_timer_create(preview_clock_update, 1000, NULL);
    lv_scr_load(scr);
}

static void setting_close(void)
{
    if (preview_clock_timer) {
        lv_timer_del(preview_clock_timer);
        preview_clock_timer = NULL;
    }
    preview_clock_label = NULL;
    preview_date_label = NULL;
    preview_brightness_label = NULL;
    preview_volume_label = NULL;
    bind_code_label = NULL;
    bind_code_item = NULL;
    for (int i = 0; i < 3; i++) preview_volume_bars[i] = NULL;

    mw_unsubscribe(TOPIC_CLOUD_STATUS, setting_cloud_status_cb);

    if (settings_dirty) {
        sys_settings_save();
        settings_dirty = false;
    }
    printf("Setting App Closed\n");
}

AppDescriptor app_setting = {
    .id = APP_ID_SETTING,
    .name = "setting",
    .init = setting_init,
    .close = setting_close
};
