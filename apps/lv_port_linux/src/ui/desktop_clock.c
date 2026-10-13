#include "desktop_clock.h"
#include "desktop_all_apps.h"
#include "desktop_update.h"
#include "desktop_home_tokens.h"
#include "theme/theme.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
#include "../system/app_manager.h"
#include "../apps/app.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CLOCK_SYNC_RETRY_MS 1000U
#define CLOCK_MIN_VALID_YEAR 2024
#define CLOCK_MAX_VALID_YEAR 2099

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

static void clock_timer_cb(lv_timer_t *timer);

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
    lv_obj_set_style_shadow_opa(obj, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_offset_y(obj, 4, 0);
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
        desktop_update_show();
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
                                      lv_coord_t width, home_action_t action)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_size(button, width, 56);
    lv_obj_set_style_radius(button, 17, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button,
                              lv_color_mix(color, lv_color_black(), 190),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
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

    create_text(button, symbol, &lv_font_montserrat_24, HOME_TEXT_INVERSE);
    lv_obj_t *label = create_text(button, text, ui_font_body_lg(), HOME_TEXT_INVERSE);
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
    /* Re-entering Home must not stack another animation on the persistent
     * desktop object. */
    lv_anim_delete(ai_orb_glow, orb_pulse_exec);
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

static void stop_orb_animation(void)
{
    if (!ai_orb_glow || !lv_obj_is_valid(ai_orb_glow)) return;
    lv_anim_delete(ai_orb_glow, orb_pulse_exec);
    lv_obj_set_style_transform_scale(ai_orb_glow, LV_SCALE_NONE, 0);
    lv_obj_set_style_opa(ai_orb_glow, LV_OPA_50, 0);
}

static void home_screen_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCREEN_UNLOADED) {
        stop_orb_animation();
        if (clock_timer) lv_timer_pause(clock_timer);
        return;
    }
    if (code != LV_EVENT_SCREEN_LOADED) return;

    start_orb_animation();
    if (clock_timer) {
        lv_timer_resume(clock_timer);
        /* Time may have been synchronized while another screen was active. */
        lv_timer_ready(clock_timer);
    } else {
        clock_timer_cb(NULL);
    }
}

static int32_t ai_width, hdmi_width, card_height;
static bool compact_cards;

static void create_ai_orb(lv_obj_t *parent)
{
    lv_obj_t *stage = lv_obj_create(parent);
    lv_obj_set_size(stage, 164, 164);
    lv_obj_align(stage, LV_ALIGN_TOP_RIGHT, -72, 70);
    style_clear_container(stage);
    lv_obj_set_style_clip_corner(stage, false, 0);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    ai_orb_glow = lv_obj_create(stage);
    lv_obj_set_size(ai_orb_glow, 148, 148);
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
    lv_obj_set_size(ai_orb_core, 112, 112);
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
    lv_obj_set_size(card, ai_width, card_height);
    style_surface(card, HOME_RADIUS);
    lv_obj_set_style_bg_color(card,
                              lv_color_mix(HOME_SURFACE, HOME_CORAL, 232), 0);
    lv_obj_set_style_bg_grad_color(card, HOME_SURFACE, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, home_action_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)HOME_ACTION_AI);

    lv_obj_t *title = create_text(card, ui_tr(UI_STR_HOME_AI_ASSISTANT),
                                  ui_font_h2(), HOME_TEXT);
    lv_obj_set_pos(title, 24, 22);

    sensor_label = create_text(card, "Environment  --°  --%",
                               ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_set_pos(sensor_label, 24, compact_cards ? 184 : 338);

    if (!compact_cards) create_ai_orb(card);

    greeting_label = create_text(card, ui_tr(UI_STR_HOME_GREETING_MORNING),
                                 ui_font_h1(), HOME_TEXT);
    lv_obj_set_width(greeting_label, ai_width - 48);
    lv_obj_set_style_text_align(greeting_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(greeting_label, 24, compact_cards ? 80 : 230);

    lv_obj_t *prompt = create_text(card, ui_tr(UI_STR_HOME_HELP_PROMPT),
                                   ui_font_body_lg(), HOME_TEXT_MUTED);
    lv_obj_set_width(prompt, ai_width - 48);
    lv_obj_set_style_text_align(prompt, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(prompt, 24, compact_cards ? 132 : 286);

    ai_action_button = create_action_button(card, LV_SYMBOL_AUDIO,
                                             ui_tr(UI_STR_HOME_TALK_MIMI), HOME_CORAL,
                                             246, HOME_ACTION_AI);
    lv_obj_align(ai_action_button, LV_ALIGN_BOTTOM_LEFT, 32, -20);
    return card;
}

static void create_monitor_preview(lv_obj_t *parent)
{
    lv_obj_t *preview = lv_obj_create(parent);
    int32_t preview_height = compact_cards ? 120 : 232;
    lv_obj_set_size(preview, hdmi_width - 48, preview_height);
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

    if (preview_height < 200) {
        lv_obj_add_flag(monitor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(stand, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *ready = create_text(preview, ui_tr(UI_STR_HOME_READY_CONTROL),
                                  ui_font_h2(), HOME_DARK_TEXT);
    lv_obj_align(ready, LV_ALIGN_BOTTOM_MID, 0, -47);
    lv_obj_t *hint = create_text(preview, ui_tr(UI_STR_HOME_SECURE_HDMI),
                                 ui_font_body_md(), HOME_DARK_TEXT_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static lv_obj_t *create_hdmi_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, hdmi_width, card_height);
    style_surface(card, HOME_RADIUS);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, home_action_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)HOME_ACTION_HDMI);

    lv_obj_t *title = create_text(card, ui_tr(UI_STR_HOME_HDMI_LIVE), ui_font_h2(), HOME_TEXT);
    lv_obj_set_pos(title, 24, 20);

    lv_obj_t *live_dot = lv_obj_create(card);
    lv_obj_set_size(live_dot, 10, 10);
    lv_obj_align(live_dot, LV_ALIGN_TOP_RIGHT, -110, 28);
    lv_obj_set_style_radius(live_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(live_dot, HOME_CYAN, 0);
    lv_obj_set_style_bg_opa(live_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(live_dot, HOME_CYAN, 0);
    lv_obj_set_style_shadow_opa(live_dot, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(live_dot, 8, 0);
    lv_obj_set_style_border_width(live_dot, 0, 0);
    lv_obj_set_style_pad_all(live_dot, 0, 0);
    make_noninteractive(live_dot);

    lv_obj_t *live = create_text(card, ui_tr(UI_STR_READY),
                                 ui_font_body_md(), HOME_CYAN);
    lv_obj_align(live, LV_ALIGN_TOP_RIGHT, -22, 22);

    create_monitor_preview(card);
    hdmi_action_button = create_action_button(card, LV_SYMBOL_VIDEO,
                                               ui_tr(UI_STR_HOME_OPEN_KVM), HOME_BLUE,
                                               hdmi_width - 48, HOME_ACTION_HDMI);
    lv_obj_align(hdmi_action_button, LV_ALIGN_BOTTOM_MID, 0, -20);
    return card;
}


static bool local_time_is_plausible(const struct tm *local_tm)
{
    if (!local_tm) return false;
    int year = local_tm->tm_year + 1900;
    return year >= CLOCK_MIN_VALID_YEAR && year <= CLOCK_MAX_VALID_YEAR;
}

static void label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !lv_obj_is_valid(label) || !text) return;
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static uint32_t next_clock_tick_ms(bool time_valid)
{
    if (!time_valid) return CLOCK_SYNC_RETRY_MS;

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return CLOCK_SYNC_RETRY_MS;
    uint32_t elapsed = (uint32_t)(ts.tv_sec % 60) * 1000U +
                       (uint32_t)(ts.tv_nsec / 1000000L);
    return elapsed < 60000U ? 60000U - elapsed : 1U;
}

static void clock_timer_cb(lv_timer_t *timer)
{
    time_t now = time(NULL);
    struct tm local_tm;
    bool time_valid = localtime_r(&now, &local_tm) != NULL &&
                      local_time_is_plausible(&local_tm);

    if (!time_valid) {
        label_set_text_if_changed(time_label, "--:--");
        label_set_text_if_changed(date_label, ui_tr(UI_STR_TIME_NOT_SET));
        label_set_text_if_changed(greeting_label, ui_tr(UI_STR_WELCOME_MIMI));
        if (timer) lv_timer_set_period(timer, next_clock_tick_ms(false));
        return;
    }

    if (time_label) {
        char value[8];
        snprintf(value, sizeof(value), "%02d:%02d",
                 local_tm.tm_hour, local_tm.tm_min);
        label_set_text_if_changed(time_label, value);
    }
    if (date_label) {
        int day = local_tm.tm_wday >= 0 && local_tm.tm_wday < 7
                    ? local_tm.tm_wday : 0;
        int month = local_tm.tm_mon >= 0 && local_tm.tm_mon < 12
                    ? local_tm.tm_mon + 1 : 1;
        char value[80];
        ui_i18n_format_date(value, sizeof(value), local_tm.tm_year + 1900,
                            month, local_tm.tm_mday, day);
        label_set_text_if_changed(date_label, value);
    }
    if (greeting_label) {
        const char *greeting = local_tm.tm_hour < 5 ? ui_tr(UI_STR_HOME_GREETING_NIGHT)
                              : local_tm.tm_hour < 12 ? ui_tr(UI_STR_HOME_GREETING_MORNING)
                              : local_tm.tm_hour < 18 ? ui_tr(UI_STR_HOME_GREETING_AFTERNOON)
                              : ui_tr(UI_STR_HOME_GREETING_EVENING);
        label_set_text_if_changed(greeting_label, greeting);
    }
    if (timer) lv_timer_set_period(timer, next_clock_tick_ms(true));
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
        lv_label_set_text(sensor_label, ui_tr(UI_STR_ENVIRONMENT_OFFLINE));
        lv_obj_set_style_text_color(sensor_label, HOME_TEXT_MUTED, 0);
        return;
    }
    lv_label_set_text_fmt(sensor_label,
                          ui_i18n_is_zh() ? "环境  %d°  %d%%" : "Environment  %d°  %d%%",
                          temp_c, humi_pct);
    bool warning = temp_c < 16 || temp_c > 28 ||
                   humi_pct < 30 || humi_pct > 70;
    lv_obj_set_style_text_color(sensor_label,
                                warning ? HOME_CORAL : HOME_GREEN, 0);
}

void ui_clock_create(lv_obj_t *parent)
{
    lv_obj_t *home_screen = lv_obj_get_screen(parent);
    if (home_screen) {
        lv_obj_add_event_cb(home_screen, home_screen_event_cb,
                            LV_EVENT_SCREEN_UNLOADED, NULL);
        lv_obj_add_event_cb(home_screen, home_screen_event_cb,
                            LV_EVENT_SCREEN_LOADED, NULL);
    }

    bool stacked = HOME_SCREEN_W < 1000;
    compact_cards = stacked || HOME_DASHBOARD_H < 414;
    card_height = compact_cards ? 300 : HOME_DASHBOARD_H;
    ai_width = stacked ? HOME_CONTENT_W : (HOME_CONTENT_W - HOME_GAP) * 566 / 896;
    hdmi_width = stacked ? HOME_CONTENT_W : HOME_CONTENT_W - HOME_GAP - ai_width;
    lv_obj_t *dashboard = lv_obj_create(parent);
    lv_obj_set_size(dashboard, HOME_CONTENT_W, HOME_DASHBOARD_H);
    style_clear_container(dashboard);
    lv_obj_set_pos(dashboard, HOME_CONTENT_X, HOME_DASHBOARD_Y);
    lv_obj_set_flex_flow(dashboard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dashboard, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(dashboard, 16, 0);
    lv_obj_add_flag(dashboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(dashboard, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(dashboard, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *hero_row = lv_obj_create(dashboard);
    lv_obj_set_size(hero_row, HOME_CONTENT_W, stacked ? card_height * 2 + HOME_GAP : card_height);
    style_clear_container(hero_row);
    lv_obj_set_flex_flow(hero_row, stacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero_row, HOME_GAP, 0);
    lv_obj_set_style_pad_row(hero_row, HOME_GAP, 0);
    lv_obj_add_flag(hero_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    create_ai_card(hero_row);
    create_hdmi_card(hero_row);

    /* Draw the clock above the previously-created status bar. It is a child
     * of the desktop root (not the dashboard), so it cannot be clipped by a
     * negative flex offset as the earlier implementation was. */
    time_label = create_text(parent, "--:--", ui_font_h3(), HOME_TEXT);
    lv_obj_add_flag(time_label, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(time_label, 86);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(time_label, HOME_SCREEN_W / 2 - (stacked ? 183 : 162), stacked ? 72 : 19);
    date_label = create_text(parent, "Monday  Sep 21",
                             ui_font_body_md(), HOME_TEXT_MUTED);
    lv_obj_add_flag(date_label, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(date_label, 280);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(date_label, HOME_SCREEN_W / 2 - (stacked ? 85 : 64), stacked ? 75 : 22);

    clock_timer_cb(NULL);
    if (!clock_timer) {
        time_t now = time(NULL);
        struct tm local_tm;
        bool time_valid = localtime_r(&now, &local_tm) != NULL &&
                          local_time_is_plausible(&local_tm);
        clock_timer = lv_timer_create(clock_timer_cb,
                                      next_clock_tick_ms(time_valid), NULL);
    }
}
