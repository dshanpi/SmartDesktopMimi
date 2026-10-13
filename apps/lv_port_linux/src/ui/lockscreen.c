#include "lockscreen.h"
#include "lockscreen_3d.h"
#include "desktop_all_apps.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
#include "lvgl/src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_IMAGE_DECLARE(sleeping_cat);
static lv_obj_t *home_screen, *sleep_screen, *cat, *clock_label, *date_label, *wake_hint, *wake_ring;
static lv_timer_t *idle_timer, *motion_timer;
static lv_font_t *clock_font;
static uint32_t eligible_since, animation_start;
static bool wake_pending;
static lv_timer_t *wake_request_timer;
static uint32_t wake_release_since;
static lv_obj_t *wake_shield;
static lv_timer_t *wake_shield_timer;
static uint32_t wake_shield_since;
typedef struct input_group_save {
    lv_indev_t *input;
    lv_group_t *group;
    struct input_group_save *next;
} input_group_save_t;
static input_group_save_t *saved_groups;
static lv_group_t *sleep_group;
static void restore_input_groups(void);

static void release_wake_shield(lv_timer_t *timer)
{
    /* evdev may drain additional contact frames after a screen load resets
     * LVGL's input state. Keep those frames on a transparent input shield. */
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if (lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED) {
            wake_shield_since = lv_tick_get();
            return;
        }
    }
    if (lv_tick_elaps(wake_shield_since) < 400U) return;
    restore_input_groups();
    if (wake_shield) lv_obj_delete(wake_shield);
    wake_shield = NULL;
    wake_shield_timer = NULL;
    lv_timer_delete(timer);
}

static void protect_wake_input(void)
{
    if (wake_shield) return;
    wake_shield = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(wake_shield);
    lv_obj_set_size(wake_shield, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(wake_shield, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wake_shield, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(wake_shield, 0, 0);
    lv_obj_update_layout(wake_shield);
    if (sleep_group) lv_group_add_obj(sleep_group, wake_shield);
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
        lv_indev_wait_release(i);
    wake_shield_since = lv_tick_get();
    wake_shield_timer = lv_timer_create(release_wake_shield, 50, NULL);
}

static void restore_input_groups(void)
{
    while (saved_groups) {
        input_group_save_t *saved = saved_groups;
        saved_groups = saved->next;
        for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
            if (i == saved->input && lv_indev_get_group(i) == sleep_group) {
                lv_indev_set_group(i, saved->group);
                break;
            }
        }
        lv_free(saved);
    }
    if (sleep_group) lv_group_delete(sleep_group);
    sleep_group = NULL;
}

static bool modal_visible(void)
{
    lv_obj_t *layer = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(layer); i++) {
        lv_obj_t *child = lv_obj_get_child(layer, i);
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) return true;
    }
    return false;
}

static bool home_eligible(void)
{
    return home_screen && lv_screen_active() == home_screen &&
           !ui_all_apps_is_visible() && !modal_visible();
}

static void set_text(lv_obj_t *label, const char *value)
{
    if (strcmp(lv_label_get_text(label), value)) lv_label_set_text(label, value);
}

static void update_clock(void)
{
    time_t now = time(NULL);
    struct tm local;
    char value[96];
    if (localtime_r(&now, &local) && local.tm_year >= 124 && local.tm_year <= 199) {
        snprintf(value, sizeof(value), "%02d:%02d", local.tm_hour, local.tm_min);
        set_text(clock_label, value);
        ui_i18n_format_date(value, sizeof(value), local.tm_year + 1900,
                            local.tm_mon + 1, local.tm_mday, local.tm_wday);
        set_text(date_label, value);
    } else {
        set_text(clock_label, "--:--");
        set_text(date_label, ui_tr(UI_STR_TIME_NOT_SET));
    }
    set_text(wake_hint, ui_tr(UI_STR_LOCKSCREEN_WAKE));
}

bool ui_lockscreen_is_active(void)
{
    return sleep_screen && lv_screen_active() == sleep_screen;
}

void ui_lockscreen_wake(void)
{
    if (!sleep_screen) return;
    ui_lockscreen_3d_stop();
    if (wake_request_timer) lv_timer_delete(wake_request_timer);
    wake_request_timer = NULL;
    wake_pending = false;
    fprintf(stderr, "[Lockscreen] wake; protecting input until release\n");
    lv_obj_t *old = sleep_screen;
    if (lv_screen_active() == old && home_screen) lv_screen_load(home_screen);
    protect_wake_input();
    sleep_screen = cat = clock_label = date_label = wake_hint = wake_ring = NULL;
    if (motion_timer) lv_timer_pause(motion_timer);
    eligible_since = lv_tick_get();
    lv_display_trigger_activity(NULL);
    lv_obj_delete_async(old);
}

static void wake_after_release(lv_timer_t *timer)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if (lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED) {
            wake_release_since = lv_tick_get();
            return;
        }
    }
    if (lv_tick_elaps(wake_release_since) < 300U) return;
    (void)timer;
    ui_lockscreen_wake();
}

static void request_wake(lv_indev_t *indev)
{
    if (indev) {
        lv_indev_wait_release(indev);
        lv_indev_stop_processing(indev);
    }
    if (!wake_pending) {
        fprintf(stderr, "[Lockscreen] wake input type=%d\n",
                indev ? (int)lv_indev_get_type(indev) : -1);
        wake_pending = true;
        wake_release_since = lv_tick_get();
        wake_request_timer = lv_timer_create(wake_after_release, 50, NULL);
    }
}

bool ui_lockscreen_handle_key(lv_indev_t *indev)
{
    if (wake_shield) { lv_indev_wait_release(indev); return true; }
    if (!ui_lockscreen_is_active()) return false;
    request_wake(indev);
    return true;
}

static void touch_wake(lv_event_t *event)
{
    (void)event;
    request_wake(lv_indev_active());
}

static void motion_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!ui_lockscreen_is_active()) return;
    /* Three tiny scale steps over six seconds: no continuously scaled
     * full-screen texture or real-time 3D. Only the small wake ring pulses at
     * 10 Hz. LVGL skips unchanged scales, bounding framebuffer work. */
    uint32_t phase = lv_tick_elaps(animation_start) % 6000U;
    int rise = phase < 3000U ? (int)phase : (int)(6000U - phase);
    lv_image_set_scale_y(cat, 256 + rise / 1500);
    lv_obj_set_style_border_opa(wake_ring, (lv_opa_t)(80 + rise / 30), 0);
}

static lv_obj_t *label_at(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 0, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

bool ui_lockscreen_show(void)
{
    if (sleep_screen || !home_eligible()) return false;
    sleep_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(sleep_screen);
    lv_obj_set_style_bg_color(sleep_screen, lv_color_hex(0x10191F), 0);
    lv_obj_set_style_bg_opa(sleep_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(sleep_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sleep_screen, LV_OBJ_FLAG_CLICKABLE);
    /* Consume the contact from its first press. Keep this screen until every
     * input has released, so no queued release can target the desktop. */
    lv_obj_add_event_cb(sleep_screen, touch_wake, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(sleep_screen, touch_wake, LV_EVENT_KEY, NULL);
    cat = lv_image_create(sleep_screen);
    lv_image_set_src(cat, &sleeping_cat);
    lv_image_set_pivot(cat, 512, 595);
    lv_obj_clear_flag(cat, LV_OBJ_FLAG_CLICKABLE);

#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
    if (!clock_font) clock_font = lv_tiny_ttf_create_file("A:/usr/share/fonts/SarasaUiSC-Regular.ttf", 96);
#endif
    clock_label = label_at(sleep_screen, clock_font ? clock_font : &lv_font_montserrat_48, 0xF5F6FA, 64);
    date_label = label_at(sleep_screen, ui_font_h2(), 0x9EA9BD, 174);
    wake_ring = lv_obj_create(sleep_screen);
    lv_obj_remove_style_all(wake_ring);
    lv_obj_set_size(wake_ring, 28, 28);
    lv_obj_align(wake_ring, LV_ALIGN_TOP_MID, 0, 648);
    lv_obj_set_style_radius(wake_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(wake_ring, 3, 0);
    lv_obj_set_style_border_color(wake_ring, lv_color_hex(0x9BAAC4), 0);
    lv_obj_clear_flag(wake_ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dot = lv_obj_create(wake_ring);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x9BAAC4), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_70, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    wake_hint = label_at(sleep_screen, ui_font_body_lg(), 0x9EA9BD, 697);
    update_clock();
    sleep_group = lv_group_create();
    lv_group_add_obj(sleep_group, sleep_screen);
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        lv_indev_type_t type = lv_indev_get_type(i);
        if (type != LV_INDEV_TYPE_KEYPAD && type != LV_INDEV_TYPE_ENCODER) continue;
        input_group_save_t *saved = lv_malloc(sizeof(*saved));
        if (!saved) { ui_lockscreen_wake(); return false; }
        *saved = (input_group_save_t){i, lv_indev_get_group(i), saved_groups};
        saved_groups = saved;
        lv_indev_set_group(i, sleep_group);
    }
    animation_start = lv_tick_get();
    lv_screen_load(sleep_screen);
    ui_lockscreen_3d_start(sleep_screen, cat);
    fprintf(stderr, "[Lockscreen] entered\n");
    if (!motion_timer) motion_timer = lv_timer_create(motion_tick, 100, NULL);
    else lv_timer_resume(motion_timer);
    motion_tick(NULL);
    return true;
}

static void idle_tick(lv_timer_t *timer)
{
    (void)timer;
    if (sleep_screen) {
        /* Cloud dialogs and asynchronous HDMI navigation take precedence. */
        if (!ui_lockscreen_is_active() || modal_visible()) ui_lockscreen_wake();
        else update_clock();
        return;
    }
    if (!home_eligible()) {
        eligible_since = lv_tick_get();
        return;
    }
    if (lv_tick_elaps(eligible_since) >= UI_LOCKSCREEN_IDLE_MS &&
        lv_display_get_inactive_time(NULL) >= UI_LOCKSCREEN_IDLE_MS) {
        /* Never replace a screen while a finger or remote key is held. */
        for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input))
            if (lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) return;
        ui_lockscreen_show();
    }
}

void ui_lockscreen_set_home(lv_obj_t *home)
{
    home_screen = home;
    eligible_since = lv_tick_get();
}

void ui_lockscreen_init(lv_obj_t *home)
{
    ui_lockscreen_set_home(home);
    if (!idle_timer) idle_timer = lv_timer_create(idle_tick, 500, NULL);
}
