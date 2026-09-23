#include "theme.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "../../system/settings.h"
#include "../desktop_clock.h"

/* =======================
 *  THEME DEFINITIONS
 * ======================= */

/* --- Light Theme (Default) --- */
static const ui_theme_t theme_light = {
    .name = "Light",
    
    /* Backgrounds */
    .bg_default = LV_COLOR_MAKE(0xF0, 0xF1, 0xF4),
    .bg_card = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .bg_inverse = LV_COLOR_MAKE(0x30, 0x35, 0x48),
    .bg_desktop = LV_COLOR_MAKE(0xE8, 0xEC, 0xF1),
    
    /* Text */
    .text_primary = LV_COLOR_MAKE(0x27, 0x2C, 0x37),
    .text_secondary = LV_COLOR_MAKE(0x74, 0x7B, 0x89),
    .text_inverse = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .text_accent = LV_COLOR_MAKE(0x59, 0x6B, 0xC8),
    
    /* Borders */
    .border_default = LV_COLOR_MAKE(0xD8, 0xDD, 0xE6),
    .border_focus = LV_COLOR_MAKE(0x59, 0x6B, 0xC8),
    
    /* Status */
    .color_primary = LV_COLOR_MAKE(0x59, 0x6B, 0xC8),
    .color_secondary = LV_COLOR_MAKE(0x6B, 0x79, 0xD3),
    
    /* Shadows */
    .shadow_default = LV_COLOR_MAKE(0x77, 0x81, 0x93),
    
    /* Desktop Specifics */
    .desktop_card_bg = LV_COLOR_MAKE(0xFA, 0xFA, 0xF7),
    .desktop_dock_bg = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .desktop_text_main = LV_COLOR_MAKE(0x31, 0x37, 0x46),
    .desktop_border = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .dock_app_border = LV_COLOR_MAKE(0xDD, 0xE1, 0xE8),

    /* Solid hardware surface with a restrained highlight gradient. */
    .glass_bg = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .glass_bg_grad = LV_COLOR_MAKE(0xF7, 0xF8, 0xFB),
    .glass_border = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .glass_outline = LV_COLOR_MAKE(0xE0, 0xE3, 0xEA),
    .glass_shadow = LV_COLOR_MAKE(0x7C, 0x85, 0x96),

    /* Desktop accents */
    .desktop_strong_text = LV_COLOR_MAKE(0x27, 0x2C, 0x37),
    .desktop_divider = LV_COLOR_MAKE(0xE0, 0xE3, 0xEA),
};

/* --- Dark Theme (Example) --- */
static const ui_theme_t theme_dark = {
    .name = "Dark",
    
    /* Backgrounds */
    .bg_default = LV_COLOR_MAKE(0x12, 0x12, 0x12), 
    .bg_card = LV_COLOR_MAKE(0x1E, 0x1E, 0x1E),
    .bg_inverse = LV_COLOR_MAKE(0xFA, 0xFA, 0xFA),
    .bg_desktop = LV_COLOR_MAKE(0x0F, 0x0F, 0x13),
    
    /* Text */
    .text_primary = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .text_secondary = LV_COLOR_MAKE(0xAA, 0xAA, 0xAA),
    .text_inverse = LV_COLOR_MAKE(0x12, 0x12, 0x12),
    .text_accent = LV_COLOR_MAKE(0x00, 0xE5, 0xFF), // Brighter Cyan
    
    /* Borders */
    .border_default = LV_COLOR_MAKE(0x33, 0x33, 0x33),
    .border_focus = LV_COLOR_MAKE(0x00, 0xE5, 0xFF),
    
    /* Status */
    .color_primary = LV_COLOR_MAKE(0x00, 0xE5, 0xFF),
    .color_secondary = LV_COLOR_MAKE(0x5C, 0x7C, 0xFA),
    
    /* Shadows */
    .shadow_default = LV_COLOR_MAKE(0x00, 0x00, 0x00),
    
    /* Desktop Specifics */
    .desktop_card_bg = LV_COLOR_MAKE(0x1E, 0x1E, 0x1E),
    .desktop_dock_bg = LV_COLOR_MAKE(0x2C, 0x2C, 0x2C),
    .desktop_text_main = LV_COLOR_MAKE(0xE0, 0xE0, 0xE0),
    .desktop_border = LV_COLOR_MAKE(0x44, 0x44, 0x44),
    .dock_app_border = LV_COLOR_MAKE(0x44, 0x44, 0x44),

    /* Glassmorphism (dark: 深色磨砂玻璃近似) */
    .glass_bg = LV_COLOR_MAKE(0x26, 0x2A, 0x31),
    .glass_bg_grad = LV_COLOR_MAKE(0x19, 0x1E, 0x27),
    .glass_border = LV_COLOR_MAKE(0x54, 0x5D, 0x69),
    .glass_outline = LV_COLOR_MAKE(0x2F, 0x3A, 0x48),
    .glass_shadow = LV_COLOR_MAKE(0x00, 0x00, 0x00),

    /* Desktop accents */
    .desktop_strong_text = LV_COLOR_MAKE(0xE8, 0xE8, 0xE8),
    .desktop_divider = LV_COLOR_MAKE(0x33, 0x33, 0x33),
};

/* =======================
 *  GLOBAL STATE
 * ======================= */
static ui_theme_t active_theme; // Mutable copy of the current theme
const ui_theme_t * current_theme = &active_theme; // Public pointer points to mutable copy

typedef struct {
    uint32_t colors[3];
} ui_gradient_preset_t;

/* Preset ids stay stable for settings migration:
 * 0 下午靛蓝 · 1 上午天蓝 · 2 傍晚暖珊瑚 · 3 夜晚暮紫 · 4 清晨柔玫瑰 */
static const ui_gradient_preset_t gradient_presets[UI_THEME_GRADIENT_PRESET_COUNT] = {
    {{0x5267B8U, 0x7375C8U, 0x907CAFU}}, /* afternoon */
    {{0x3974B8U, 0x5B88C5U, 0x759BC7U}}, /* morning */
    {{0xA95568U, 0xC66F6AU, 0xD18A70U}}, /* evening */
    {{0x62598DU, 0x806B9AU, 0x9A7C97U}}, /* night */
    {{0xC8787AU, 0xD49888U, 0xE0B8A0U}}, /* dawn */
};

/* hour → period → preset:
 * 05–08 清晨→4 · 08–12 上午→1 · 12–17 下午→0 · 17–20 傍晚→2 · 20–05 夜晚→3 */
static const uint32_t hour_to_preset[24] = {
    /* 00-04 */ 3U, 3U, 3U, 3U, 3U,
    /* 05-07 */ 4U, 4U, 4U,
    /* 08-11 */ 1U, 1U, 1U, 1U,
    /* 12-16 */ 0U, 0U, 0U, 0U, 0U,
    /* 17-19 */ 2U, 2U, 2U,
    /* 20-23 */ 3U, 3U, 3U, 3U,
};

static lv_grad_dsc_t gradient_descriptors[UI_THEME_GRADIENT_PRESET_COUNT];
static uint32_t active_gradient_preset_id = 0U;
static bool gradients_inited = false;
static lv_timer_t *time_follow_timer = NULL;
static int last_synced_hour = -1;

/* =======================
 *  SHARED STYLES
 * ======================= */
static lv_style_t style_card;
static lv_style_t style_slider_main;
static lv_style_t style_slider_indicator;
static lv_style_t style_slider_knob;
static lv_style_t style_text_accent;
static lv_style_t style_bg_desktop;
static lv_style_t style_glass_card;
static bool inited = false;

/* =======================
 *  IMPLEMENTATION
 * ======================= */

static uint32_t normalize_gradient_preset_id(uint32_t preset_id)
{
    return preset_id < UI_THEME_GRADIENT_PRESET_COUNT ? preset_id : 0U;
}

static void init_gradient_descriptors(void)
{
    static const uint8_t fractions[3] = {0U, 132U, 255U};

    if (gradients_inited) return;

    for (uint32_t i = 0; i < UI_THEME_GRADIENT_PRESET_COUNT; i++) {
        lv_color_t colors[3] = {
            lv_color_hex(gradient_presets[i].colors[0]),
            lv_color_hex(gradient_presets[i].colors[1]),
            lv_color_hex(gradient_presets[i].colors[2]),
        };
        lv_grad_init_stops(&gradient_descriptors[i], colors, NULL, fractions, 3);
        lv_grad_linear_init(&gradient_descriptors[i],
                            lv_pct(0), lv_pct(0),
                            lv_pct(100), lv_pct(100),
                            LV_GRAD_EXTEND_PAD);
    }

    gradients_inited = true;
}

static void apply_gradient_preset_to_theme(uint32_t preset_id)
{
    active_gradient_preset_id = normalize_gradient_preset_id(preset_id);
    active_theme.color_primary = lv_color_hex(
        gradient_presets[active_gradient_preset_id].colors[0]);
    active_theme.color_secondary = lv_color_hex(
        gradient_presets[active_gradient_preset_id].colors[2]);
    active_theme.text_accent = active_theme.color_primary;
    active_theme.border_focus = active_theme.color_primary;
}

uint32_t ui_theme_get_gradient_preset_id(void)
{
    return active_gradient_preset_id;
}

const lv_grad_dsc_t *ui_theme_get_gradient(uint32_t preset_id)
{
    init_gradient_descriptors();
    return &gradient_descriptors[normalize_gradient_preset_id(preset_id)];
}

const lv_grad_dsc_t *ui_theme_get_active_gradient(void)
{
    return ui_theme_get_gradient(active_gradient_preset_id);
}

lv_color_t ui_theme_get_gradient_color(uint32_t preset_id, uint32_t stop_id)
{
    preset_id = normalize_gradient_preset_id(preset_id);
    if (stop_id > 2U) stop_id = 2U;
    return lv_color_hex(gradient_presets[preset_id].colors[stop_id]);
}

lv_color_t ui_theme_primary_surface_bottom(lv_color_t primary)
{
    return lv_color_mix(primary, lv_color_white(), 220);
}

lv_color_t ui_theme_primary_surface_border(lv_color_t primary)
{
    return lv_color_mix(primary, lv_color_white(), 180);
}

lv_color_t ui_theme_primary_surface_shadow(lv_color_t primary)
{
    return lv_color_mix(primary, lv_color_hex(0x303548), 168);
}

lv_color_t ui_theme_primary_surface_divider(lv_color_t primary)
{
    return lv_color_mix(primary, lv_color_white(), 155);
}

static void apply_gradient_preset_runtime(uint32_t preset_id, bool save_settings)
{
    preset_id = normalize_gradient_preset_id(preset_id);
    apply_gradient_preset_to_theme(preset_id);

    bool is_dark = (strcmp(active_theme.name, "Dark") == 0);
    lv_theme_default_init(NULL,
                          active_theme.color_primary,
                          active_theme.color_secondary,
                          is_dark,
                          LV_FONT_DEFAULT);

    if (save_settings) {
        sys_settings_t *settings = sys_settings_get();
        settings->theme_preset_id = preset_id;
        settings->theme_color = lv_color_to_int(active_theme.color_primary);
        sys_settings_save();
    }

    if (inited) {
        lv_style_set_bg_color(&style_slider_indicator,
                              active_theme.color_primary);
        lv_style_set_bg_color(&style_slider_knob,
                              active_theme.color_primary);
        lv_style_set_text_color(&style_text_accent,
                                active_theme.color_primary);
    }

    lv_obj_t *screen = lv_scr_act();
    if (screen) lv_obj_invalidate(screen);
}

uint32_t ui_theme_preset_for_hour(int hour)
{
    if (hour < 0 || hour > 23) hour = 0;
    return hour_to_preset[hour];
}

uint32_t ui_theme_preset_for_now(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        return 0U;
    }
    return ui_theme_preset_for_hour(tm_now.tm_hour);
}

bool ui_theme_get_follow_time(void)
{
    return sys_settings_get()->theme_follow_time;
}

void ui_theme_set_follow_time(bool enable)
{
    sys_settings_t *settings = sys_settings_get();
    if (settings->theme_follow_time == enable) {
        if (enable) {
            ui_theme_sync_time_of_day(true);
        }
        return;
    }

    settings->theme_follow_time = enable;
    if (enable) {
        /* Force apply the current period and persist. */
        last_synced_hour = -1;
        ui_theme_sync_time_of_day(true);
    } else {
        sys_settings_save();
    }
}

bool ui_theme_sync_time_of_day(bool force)
{
    sys_settings_t *settings = sys_settings_get();
    if (!settings->theme_follow_time) {
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        return false;
    }

    int hour = tm_now.tm_hour;
    uint32_t preset = ui_theme_preset_for_hour(hour);

    if (!force &&
        hour == last_synced_hour &&
        preset == active_gradient_preset_id) {
        return false;
    }

    if (!force && preset == active_gradient_preset_id) {
        last_synced_hour = hour;
        return false;
    }

    printf("[Theme] time-of-day hour=%02d → preset %u\n",
           hour, (unsigned)preset);
    apply_gradient_preset_runtime(preset, true);
    last_synced_hour = hour;
    ui_clock_apply_theme();
    return true;
}

static void time_follow_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_theme_sync_time_of_day(false);
}

void ui_theme_init(void) {
    if (inited) return;

    init_gradient_descriptors();

    /* Initialize active theme with default (Light) */
    memcpy(&active_theme, &theme_light, sizeof(ui_theme_t));

    /* Load persistent color from settings (may be overridden by follow-time). */
    sys_settings_t *settings = sys_settings_get();
    apply_gradient_preset_to_theme(settings->theme_preset_id);

    /* Card Style */
    lv_style_init(&style_card);
    lv_style_set_width(&style_card, UI_SETTING_CARD_WIDTH);
    lv_style_set_height(&style_card, LV_SIZE_CONTENT);
    lv_style_set_pad_all(&style_card, UI_SPACE_LG);
    lv_style_set_radius(&style_card, UI_RADIUS_LG);
    // 使用当前主题的颜色
    lv_style_set_bg_color(&style_card, current_theme->bg_card);
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_shadow_width(&style_card, 10);
    lv_style_set_shadow_color(&style_card, current_theme->shadow_default);
    lv_style_set_shadow_opa(&style_card, LV_OPA_10);
    lv_style_set_border_width(&style_card, 0);

    /* Slider Main (Track) */
    lv_style_init(&style_slider_main);
    lv_style_set_bg_color(&style_slider_main, current_theme->border_default); // Use border color for track
    lv_style_set_radius(&style_slider_main, UI_RADIUS_SM);
    lv_style_set_height(&style_slider_main, UI_SPACE_XS);

    /* Slider Indicator */
    lv_style_init(&style_slider_indicator);
    lv_style_set_bg_color(&style_slider_indicator, current_theme->color_primary);
    lv_style_set_radius(&style_slider_indicator, UI_RADIUS_SM);

    /* Slider Knob */
    lv_style_init(&style_slider_knob);
    lv_style_set_bg_color(&style_slider_knob, current_theme->color_primary);
    lv_style_set_radius(&style_slider_knob, UI_RADIUS_CIRCLE);
    lv_style_set_width(&style_slider_knob, UI_SETTINGS_SLIDER_KNOB_SIZE);
    lv_style_set_height(&style_slider_knob, UI_SETTINGS_SLIDER_KNOB_SIZE);
    lv_style_set_pad_all(&style_slider_knob, 0);

    /* Text Accent */
    lv_style_init(&style_text_accent);
    lv_style_set_text_color(&style_text_accent, current_theme->text_accent);

    /* Desktop Background */
    lv_style_init(&style_bg_desktop);
    
    /* Keep the desktop neutral. The selected theme color is an interaction accent,
     * not a full-screen tint. */
    lv_style_set_bg_color(&style_bg_desktop, current_theme->bg_desktop);
    lv_style_set_bg_grad_color(&style_bg_desktop, lv_color_hex(0xDDE3EA));
    lv_style_set_bg_grad_dir(&style_bg_desktop, LV_GRAD_DIR_VER);
    lv_style_set_bg_grad_opa(&style_bg_desktop, LV_OPA_COVER);
    lv_style_set_bg_opa(&style_bg_desktop, LV_OPA_COVER);

    /* Glass Card Style (静态磨砂玻璃近似；radius/opa/shadow_width 由调用方传入) */
    lv_style_init(&style_glass_card);
    lv_style_set_bg_color(&style_glass_card, current_theme->glass_bg);
    lv_style_set_bg_grad_color(&style_glass_card, current_theme->glass_bg_grad);
    lv_style_set_bg_grad_dir(&style_glass_card, LV_GRAD_DIR_VER);
    lv_style_set_bg_grad_opa(&style_glass_card, LV_OPA_40);
    lv_style_set_border_width(&style_glass_card, 1);
    lv_style_set_border_color(&style_glass_card, current_theme->glass_border);
    lv_style_set_border_opa(&style_glass_card, LV_OPA_COVER);
    lv_style_set_outline_width(&style_glass_card, 0);
    lv_style_set_outline_color(&style_glass_card, current_theme->glass_outline);
    lv_style_set_outline_opa(&style_glass_card, LV_OPA_30);
    lv_style_set_outline_pad(&style_glass_card, 0);
    lv_style_set_shadow_color(&style_glass_card, current_theme->glass_shadow);
    lv_style_set_shadow_opa(&style_glass_card, LV_OPA_20);
    lv_style_set_shadow_offset_y(&style_glass_card, 5);

    inited = true;

    /* Apply time-of-day accent if enabled, then poll each minute. */
    ui_theme_sync_time_of_day(true);
    if (!time_follow_timer) {
        time_follow_timer = lv_timer_create(time_follow_timer_cb, 60 * 1000, NULL);
    }
}

void ui_theme_set(const char * theme_name) {
    if (strcmp(theme_name, "Dark") == 0) {
        memcpy(&active_theme, &theme_dark, sizeof(ui_theme_t));
    } else {
        memcpy(&active_theme, &theme_light, sizeof(ui_theme_t));
    }
    // Note: To fully support runtime switching, we would need to:
    // 1. Re-initialize styles (or use theme callbacks)
    // 2. Invalidate all objects to trigger redraw
    // For now, this just sets the state for new objects or manual refresh.
    
    // Simple style update for shared styles
    if (inited) {
        lv_style_set_bg_color(&style_card, current_theme->bg_card);
        lv_style_set_shadow_color(&style_card, current_theme->shadow_default);
        lv_style_set_bg_color(&style_slider_main, current_theme->border_default);
        lv_style_set_bg_color(&style_slider_indicator, current_theme->color_primary);
        lv_style_set_bg_color(&style_slider_knob, current_theme->color_primary);
        lv_style_set_text_color(&style_text_accent, current_theme->text_accent);
        lv_style_set_bg_color(&style_bg_desktop, current_theme->bg_desktop);

        /* Glass card colors follow theme */
        lv_style_set_bg_color(&style_glass_card, current_theme->glass_bg);
        lv_style_set_bg_grad_color(&style_glass_card, current_theme->glass_bg_grad);
        lv_style_set_border_color(&style_glass_card, current_theme->glass_border);
        lv_style_set_outline_color(&style_glass_card, current_theme->glass_outline);
        lv_style_set_shadow_color(&style_glass_card, current_theme->glass_shadow);
    }

    /* Trigger full redraw so shared styles (desktop bg, glass cards, sliders) repaint. */
    lv_obj_invalidate(lv_scr_act());
}

void ui_theme_set_color_primary(lv_color_t color) {
    /* Safe modification of mutable active_theme */
    active_theme.color_primary = color;
    active_theme.color_secondary = ui_theme_primary_surface_bottom(color);
    active_theme.text_accent = color;
    active_theme.border_focus = color;

    /* Update Native LVGL Theme */
    /* This mimics lv_demo_widgets: re-initializing the default theme applies the new primary color
       to all standard widgets (Switch, Checkbox, Slider, etc.) that use the default theme styles. */
    bool is_dark = (strcmp(active_theme.name, "Dark") == 0);
    lv_theme_default_init(NULL, 
                          active_theme.color_primary,  /* Primary color */
                          active_theme.color_secondary, /* Secondary color */
                          is_dark, /* 1 for dark mode, 0 for light */
                          LV_FONT_DEFAULT);

    /* Theme colors stay on interactive accents. Keep the desktop surface neutral. */

    /* Save to persistent storage */
    sys_settings_t *settings = sys_settings_get();
    settings->theme_color = lv_color_to_int(color);
    sys_settings_save();

    /* 更新共享样式 (For our custom components) */
    if (inited) {
        lv_style_set_bg_color(&style_slider_indicator, color);
        lv_style_set_bg_color(&style_slider_knob, color);
        lv_style_set_text_color(&style_text_accent, color);
    }
    
    /* Trigger full redraw to apply native theme changes */
    lv_obj_invalidate(lv_scr_act());
}

void ui_theme_set_gradient_preset(uint32_t preset_id)
{
    /* Manual pick locks the accent until follow-time is turned on again. */
    sys_settings_t *settings = sys_settings_get();
    settings->theme_follow_time = false;
    apply_gradient_preset_runtime(preset_id, true);
    last_synced_hour = -1;
}

lv_style_t* ui_theme_get_card_style(void) {
    if (!inited) ui_theme_init();
    return &style_card;
}

lv_style_t* ui_theme_get_slider_main_style(void) {
    if (!inited) ui_theme_init();
    return &style_slider_main;
}

lv_style_t* ui_theme_get_slider_indicator_style(void) {
    if (!inited) ui_theme_init();
    return &style_slider_indicator;
}

lv_style_t* ui_theme_get_slider_knob_style(void) {
    if (!inited) ui_theme_init();
    return &style_slider_knob;
}

lv_style_t* ui_theme_get_text_accent_style(void) {
    if (!inited) ui_theme_init();
    return &style_text_accent;
}

lv_style_t* ui_theme_get_desktop_bg_style(void) {
    if (!inited) ui_theme_init();
    return &style_bg_desktop;
}

lv_style_t* ui_theme_get_glass_card_style(void) {
    if (!inited) ui_theme_init();
    return &style_glass_card;
}
