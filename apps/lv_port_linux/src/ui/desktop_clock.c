#include "desktop_clock.h"
#include "desktop_all_apps.h"
#include "desktop_home_tokens.h"
#include "theme/theme.h"
#include "ui_fonts.h"
#include "../system/app_manager.h"
#include "../apps/app.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef enum {
    HOME_ACTION_AI = 1,
    HOME_ACTION_HDMI,
    HOME_ACTION_APPS,
    HOME_ACTION_DEVICES,
    HOME_ACTION_UPDATES,
    HOME_ACTION_SETTINGS,
} home_action_t;

static lv_obj_t *time_label;
static lv_obj_t *date_label;
static lv_obj_t *greeting_label;
static lv_obj_t *sensor_label;
static lv_obj_t *ai_orb_core;
static lv_obj_t *ai_orb_glow;
static lv_obj_t *ai_action_button;
static lv_obj_t *hdmi_action_button;
static lv_timer_t *clock_timer;

static void make_noninteractive(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void style_clear_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_surface(lv_obj_t *obj, lv_coord_t radius)
{
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, HOME_SURFACE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, HOME_BORDER, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(obj, 12, 0);
    lv_obj_set_style_shadow_offset_y(obj, 5, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_text(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    make_noninteractive(label);
    return label;
}

static void home_action_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    home_action_t action =
        (home_action_t)(intptr_t)lv_event_get_user_data(event);
    switch (action) {
    case HOME_ACTION_AI:
        app_manager_open(APP_ID_AI);
        break;
    case HOME_ACTION_HDMI:
        app_manager_open(APP_ID_HDMI_MCP);
        break;
    case HOME_ACTION_APPS:
        ui_all_apps_show();
        break;
    case HOME_ACTION_DEVICES:
        app_manager_open(APP_ID_WIFI);
        break;
    case HOME_ACTION_UPDATES:
        app_manager_open(APP_ID_OTA);
        break;
    case HOME_ACTION_SETTINGS:
        app_manager_open(APP_ID_SETTING);
        break;
    default:
        break;
    }
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *symbol,
                                      const char *text, lv_color_t color,
                                      home_action_t action)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_size(button, 424, 64);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_grad_color(button,
                                   lv_color_mix(color, HOME_VIOLET, 172), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button,
                              lv_color_mix(color, lv_color_black(), 190),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button,
                                  lv_color_mix(color, lv_color_white(), 170),
                                  0);
    lv_obj_set_style_border_opa(button, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(button, color, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(button, 12, 0);
    lv_obj_set_style_pad_hor(button, 22, 0);
    lv_obj_set_style_pad_ver(button, 0, 0);
    lv_obj_set_style_pad_column(button, 14, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(button, home_action_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)action);

    create_text(button, symbol, &lv_font_montserrat_28, HOME_TEXT);
    lv_obj_t *label = create_text(button, text, ui_font_h3(), HOME_TEXT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return button;
}

static void orb_pulse_exec(void *obj, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)obj, value, 0);
    lv_obj_set_style_opa((lv_obj_t *)obj,
                         (lv_opa_t)(90 + (value - 246) * 5), 0);
}

static void start_orb_animation(void)
{
    if (!ai_orb_glow) return;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ai_orb_glow);
    lv_anim_set_values(&anim, 246, 262);
    lv_anim_set_time(&anim, 1800);
    lv_anim_set_playback_time(&anim, 1800);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, orb_pulse_exec);
    lv_anim_start(&anim);
}

static void create_ai_orb(lv_obj_t *parent)
{
    lv_obj_t *stage = lv_obj_create(parent);
    lv_obj_set_size(stage, 170, 170);
    lv_obj_set_pos(stage, 155, 50);
    style_clear_container(stage);
    lv_obj_set_style_clip_corner(stage, false, 0);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    ai_orb_glow = lv_obj_create(stage);
    lv_obj_set_size(ai_orb_glow, 154, 154);
    lv_obj_center(ai_orb_glow);
    lv_obj_set_style_radius(ai_orb_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ai_orb_glow, HOME_CORAL, 0);
    lv_obj_set_style_bg_grad_color(ai_orb_glow, HOME_VIOLET, 0);
    lv_obj_set_style_bg_grad_dir(ai_orb_glow, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(ai_orb_glow, LV_OPA_70, 0);
    lv_obj_set_style_border_width(ai_orb_glow, 2, 0);
    lv_obj_set_style_border_color(ai_orb_glow, lv_color_hex(0xFFD6CF), 0);
    lv_obj_set_style_border_opa(ai_orb_glow, LV_OPA_50, 0);
    lv_obj_set_style_shadow_color(ai_orb_glow, HOME_VIOLET, 0);
    lv_obj_set_style_shadow_opa(ai_orb_glow, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(ai_orb_glow, 28, 0);
    make_noninteractive(ai_orb_glow);

    ai_orb_core = lv_obj_create(stage);
    lv_obj_set_size(ai_orb_core, 118, 118);
    lv_obj_center(ai_orb_core);
    lv_obj_set_style_radius(ai_orb_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ai_orb_core, lv_color_hex(0x30316D), 0);
    lv_obj_set_style_bg_grad_color(ai_orb_core, HOME_VIOLET, 0);
    lv_obj_set_style_bg_grad_dir(ai_orb_core, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(ai_orb_core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ai_orb_core, 1, 0);
    lv_obj_set_style_border_color(ai_orb_core, lv_color_white(), 0);
    lv_obj_set_style_border_opa(ai_orb_core, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(ai_orb_core, 0, 0);
    make_noninteractive(ai_orb_core);

    lv_obj_t *highlight = lv_obj_create(ai_orb_core);
    lv_obj_set_size(highlight, 66, 44);
    lv_obj_align(highlight, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_set_style_radius(highlight, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(highlight, lv_color_hex(0xFFD3C7), 0);
    lv_obj_set_style_bg_opa(highlight, LV_OPA_50, 0);
    lv_obj_set_style_border_width(highlight, 0, 0);
    lv_obj_set_style_pad_all(highlight, 0, 0);
    lv_obj_set_style_transform_rotation(highlight, 3350, 0);
    make_noninteractive(highlight);

    start_orb_animation();
}

static lv_obj_t *create_ai_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 480, 370);
    style_surface(card, HOME_RADIUS);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x171C2A), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x121724), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, home_action_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)HOME_ACTION_AI);

    lv_obj_t *title = create_text(card, "AI Assistant", ui_font_h3(), HOME_TEXT);
    lv_obj_set_pos(title, 22, 18);

    sensor_label = create_text(card, "Environment  --°  --%",
                               ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_align(sensor_label, LV_ALIGN_TOP_RIGHT, -22, 21);

    create_ai_orb(card);

    greeting_label = create_text(card, "Good morning", ui_font_h1(), HOME_TEXT);
    lv_obj_set_width(greeting_label, 436);
    lv_obj_set_style_text_align(greeting_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(greeting_label, 22, 222);

    lv_obj_t *prompt = create_text(card, "What can I help with?",
                                   ui_font_body_lg(), HOME_TEXT_MUTED);
    lv_obj_set_width(prompt, 436);
    lv_obj_set_style_text_align(prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(prompt, 22, 263);

    ai_action_button = create_action_button(card, LV_SYMBOL_AUDIO,
                                             "Talk to Mimi", HOME_CORAL,
                                             HOME_ACTION_AI);
    lv_obj_align(ai_action_button, LV_ALIGN_BOTTOM_MID, 0, -18);
    return card;
}

static void create_monitor_preview(lv_obj_t *parent)
{
    lv_obj_t *preview = lv_obj_create(parent);
    lv_obj_set_size(preview, 432, 206);
    lv_obj_set_pos(preview, 24, 65);
    lv_obj_set_style_radius(preview, 16, 0);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x0C1830), 0);
    lv_obj_set_style_bg_grad_color(preview, lv_color_hex(0x142D57), 0);
    lv_obj_set_style_bg_grad_dir(preview, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(preview, 1, 0);
    lv_obj_set_style_border_color(preview, lv_color_hex(0x355889), 0);
    lv_obj_set_style_pad_all(preview, 0, 0);
    make_noninteractive(preview);

    /* A native LVGL standby surface avoids opening the capture node twice.
     * The real stream starts only after Open KVM is selected. */
    lv_obj_t *monitor = lv_obj_create(preview);
    lv_obj_set_size(monitor, 100, 66);
    lv_obj_align(monitor, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_radius(monitor, 8, 0);
    lv_obj_set_style_bg_opa(monitor, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(monitor, 3, 0);
    lv_obj_set_style_border_color(monitor, HOME_CYAN, 0);
    lv_obj_set_style_pad_all(monitor, 0, 0);
    make_noninteractive(monitor);

    lv_obj_t *stand = lv_obj_create(preview);
    lv_obj_set_size(stand, 42, 5);
    lv_obj_align(stand, LV_ALIGN_CENTER, 0, 11);
    lv_obj_set_style_radius(stand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(stand, HOME_CYAN, 0);
    lv_obj_set_style_bg_opa(stand, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(stand, 0, 0);
    lv_obj_set_style_pad_all(stand, 0, 0);
    make_noninteractive(stand);

    lv_obj_t *ready = create_text(preview, "Ready to control",
                                  ui_font_h2(), HOME_TEXT);
    lv_obj_align(ready, LV_ALIGN_BOTTOM_MID, 0, -47);
    lv_obj_t *hint = create_text(preview, "Secure HDMI workspace",
                                 ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static lv_obj_t *create_hdmi_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 480, 370);
    style_surface(card, HOME_RADIUS);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, home_action_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)HOME_ACTION_HDMI);

    lv_obj_t *title = create_text(card, "HDMI Live", ui_font_h2(), HOME_TEXT);
    lv_obj_set_pos(title, 24, 20);

    lv_obj_t *live_dot = lv_obj_create(card);
    lv_obj_set_size(live_dot, 10, 10);
    lv_obj_align(live_dot, LV_ALIGN_TOP_RIGHT, -70, 28);
    lv_obj_set_style_radius(live_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(live_dot, HOME_CYAN, 0);
    lv_obj_set_style_bg_opa(live_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(live_dot, HOME_CYAN, 0);
    lv_obj_set_style_shadow_opa(live_dot, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(live_dot, 8, 0);
    lv_obj_set_style_border_width(live_dot, 0, 0);
    lv_obj_set_style_pad_all(live_dot, 0, 0);
    make_noninteractive(live_dot);

    lv_obj_t *live = create_text(card, "LIVE", ui_font_body_md(), HOME_CYAN);
    lv_obj_align(live, LV_ALIGN_TOP_RIGHT, -22, 22);

    create_monitor_preview(card);
    hdmi_action_button = create_action_button(card, LV_SYMBOL_VIDEO,
                                               "Open KVM", HOME_BLUE,
                                               HOME_ACTION_HDMI);
    lv_obj_align(hdmi_action_button, LV_ALIGN_BOTTOM_MID, 0, -18);
    return card;
}

static lv_obj_t *create_quick_tile(lv_obj_t *parent, const char *symbol,
                                   const char *title, const char *detail,
                                   lv_color_t accent, home_action_t action)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 232, 164);
    style_surface(tile, HOME_RADIUS_SM);
    lv_obj_set_style_bg_color(tile, HOME_SURFACE_RAISED, 0);
    lv_obj_set_style_bg_color(tile, HOME_SURFACE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(tile, home_action_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)action);

    lv_obj_t *icon_plate = lv_obj_create(tile);
    lv_obj_set_size(icon_plate, 52, 52);
    lv_obj_set_pos(icon_plate, 18, 18);
    lv_obj_set_style_radius(icon_plate, 15, 0);
    lv_obj_set_style_bg_color(icon_plate, accent, 0);
    lv_obj_set_style_bg_opa(icon_plate, LV_OPA_20, 0);
    lv_obj_set_style_border_width(icon_plate, 1, 0);
    lv_obj_set_style_border_color(icon_plate, accent, 0);
    lv_obj_set_style_border_opa(icon_plate, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(icon_plate, 0, 0);
    make_noninteractive(icon_plate);

    lv_obj_t *icon = create_text(icon_plate, symbol,
                                 &lv_font_montserrat_28, accent);
    lv_obj_center(icon);

    lv_obj_t *name = create_text(tile, title, ui_font_h3(), HOME_TEXT);
    lv_obj_set_pos(name, 18, 88);
    lv_obj_t *meta = create_text(tile, detail, ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_set_pos(meta, 18, 121);

    lv_obj_t *chevron = create_text(tile, LV_SYMBOL_RIGHT,
                                    &lv_font_montserrat_20, HOME_TEXT_MUTED);
    lv_obj_align(chevron, LV_ALIGN_BOTTOM_RIGHT, -18, -22);
    return tile;
}

static uint32_t next_clock_tick_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 1000;
    uint32_t elapsed = (uint32_t)(ts.tv_nsec / 1000000L);
    return elapsed < 1000U ? 1000U - elapsed : 1000U;
}

static void clock_timer_cb(lv_timer_t *timer)
{
    time_t now = time(NULL);
    struct tm local_tm;
    if (localtime_r(&now, &local_tm) == NULL) return;

    if (time_label) {
        lv_label_set_text_fmt(time_label, "%02d:%02d",
                              local_tm.tm_hour, local_tm.tm_min);
    }
    if (date_label) {
        static const char *days[] = {
            "Sunday", "Monday", "Tuesday", "Wednesday",
            "Thursday", "Friday", "Saturday"
        };
        static const char *months[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        int day = local_tm.tm_wday >= 0 && local_tm.tm_wday < 7
                    ? local_tm.tm_wday : 0;
        int month = local_tm.tm_mon >= 0 && local_tm.tm_mon < 12
                    ? local_tm.tm_mon : 0;
        lv_label_set_text_fmt(date_label, "%s  %s %d",
                              days[day], months[month], local_tm.tm_mday);
    }
    if (greeting_label) {
        const char *greeting = local_tm.tm_hour < 5 ? "Hello, night owl"
                              : local_tm.tm_hour < 12 ? "Good morning"
                              : local_tm.tm_hour < 18 ? "Good afternoon"
                              : "Good evening";
        lv_label_set_text(greeting_label, greeting);
    }
    if (timer) lv_timer_set_period(timer, next_clock_tick_ms());
}

void ui_clock_apply_theme(void)
{
    /* The home palette is intentionally stable across app themes. Reapply
     * the two action accents after a runtime theme refresh. */
    if (ai_action_button && lv_obj_is_valid(ai_action_button)) {
        lv_obj_set_style_bg_color(ai_action_button, HOME_CORAL, 0);
    }
    if (hdmi_action_button && lv_obj_is_valid(hdmi_action_button)) {
        lv_obj_set_style_bg_color(hdmi_action_button, HOME_BLUE, 0);
    }
    if (ai_orb_core && lv_obj_is_valid(ai_orb_core)) {
        lv_obj_invalidate(ai_orb_core);
    }
}

void ui_clock_set_sensor(bool valid, int temp_c, int humi_pct)
{
    if (!sensor_label) return;
    if (!valid) {
        lv_label_set_text(sensor_label, "Environment  offline");
        lv_obj_set_style_text_color(sensor_label, HOME_TEXT_MUTED, 0);
        return;
    }
    lv_label_set_text_fmt(sensor_label, "Environment  %d°  %d%%",
                          temp_c, humi_pct);
    bool warning = temp_c < 16 || temp_c > 28 ||
                   humi_pct < 30 || humi_pct > 70;
    lv_obj_set_style_text_color(sensor_label,
                                warning ? HOME_CORAL : HOME_GREEN, 0);
}

void ui_clock_create(lv_obj_t *parent)
{
    lv_obj_t *dashboard = lv_obj_create(parent);
    lv_obj_set_size(dashboard, HOME_CONTENT_W, HOME_DASHBOARD_H);
    style_clear_container(dashboard);
    lv_obj_set_flex_flow(dashboard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dashboard, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(dashboard, 16, 0);
    lv_obj_add_flag(dashboard, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *hero_row = lv_obj_create(dashboard);
    lv_obj_set_size(hero_row, HOME_CONTENT_W, 370);
    style_clear_container(hero_row);
    lv_obj_set_flex_flow(hero_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero_row, 16, 0);
    lv_obj_add_flag(hero_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    create_ai_card(hero_row);
    create_hdmi_card(hero_row);

    lv_obj_t *quick_row = lv_obj_create(dashboard);
    lv_obj_set_size(quick_row, HOME_CONTENT_W, 164);
    style_clear_container(quick_row);
    lv_obj_set_flex_flow(quick_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(quick_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(quick_row, 16, 0);
    create_quick_tile(quick_row, LV_SYMBOL_LIST, "Apps", "Browse library",
                      HOME_BLUE, HOME_ACTION_APPS);
    create_quick_tile(quick_row, LV_SYMBOL_WIFI, "Devices", "Wi-Fi & Bluetooth",
                      HOME_CYAN, HOME_ACTION_DEVICES);
    create_quick_tile(quick_row, LV_SYMBOL_DOWNLOAD, "Updates", "System software",
                      HOME_GREEN, HOME_ACTION_UPDATES);
    create_quick_tile(quick_row, LV_SYMBOL_SETTINGS, "Settings", "Display & sound",
                      HOME_VIOLET, HOME_ACTION_SETTINGS);

    /* Time remains visible without reclaiming the functional power readout in
     * the status bar: a compact overlay sits above the card grid. */
    time_label = create_text(dashboard, "--:--", ui_font_h2(), HOME_TEXT);
    lv_obj_add_flag(time_label, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, -50, -58);
    date_label = create_text(dashboard, "Monday  Sep 21",
                             ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_add_flag(date_label, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 82, -58);

    clock_timer_cb(NULL);
    if (!clock_timer) {
        clock_timer = lv_timer_create(clock_timer_cb, next_clock_tick_ms(), NULL);
    }
}
