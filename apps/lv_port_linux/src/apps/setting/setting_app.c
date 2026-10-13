#include "../../ui/desktop_update.h"
#include "setting_app.h"
#include "../../system/app_manager.h"
#include "../../system/device_identity.h"
#include "../../system/backlight_control.h"
#include "../../system/volume_control.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"
#include "../../ui/desktop_clock.h"
#include "../../ui/ui.h"
#include "../../ui/ui_i18n.h"
#include "../../ui/mimi_ui.h"
#include "../../system/settings.h"
#include "../../system/backend_types.h"
#include "../../middleware/middleware.h"
#include <stdio.h>
#include <string.h>


static bool settings_dirty = false;
static lv_obj_t *schema_label = NULL;
static bool setting_active;
static bool rebuild_pending;
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
    if (bind_code_label) lv_label_set_text(bind_code_label, ui_tr(UI_STR_LOADING));
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
        lv_label_set_text(bind_code_label, ui_tr(UI_STR_SETTINGS_100ASK_UNAVAILABLE));
        if (bind_code_item) lv_obj_clear_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
    } else if (status->bind_token_refreshing) {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, ui_tr(UI_STR_LOADING));
    } else if (status->bind_token[0]) {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, status->bind_token);
    } else if (status->bind_token_error != 0) {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, ui_tr(UI_STR_SETTINGS_100ASK_RETRY));
    } else {
        if (bind_code_item) lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(bind_code_label, ui_tr(UI_STR_SETTINGS_100ASK_TAP_CODE));
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

static void desktop_update_event(lv_event_t *event)
{
    (void)event;
    desktop_update_show();
}

static void setting_init(void);

static void setting_rebuild_async(void *unused)
{
    (void)unused;
    lv_obj_t *old = lv_scr_act();
    rebuild_pending = false;
    if (!setting_active) return;
    setting_init();
    if (old) lv_obj_del_async(old);
}

static void schedule_rebuild(void)
{
    if (!rebuild_pending) {
        rebuild_pending = true;
        lv_async_call(setting_rebuild_async, NULL);
    }
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
    lv_obj_set_style_border_color(dot, mimi_ui_color(MIMI_SURFACE), 0);
    lv_obj_set_style_outline_width(dot, 2, 0);
    lv_obj_set_style_outline_color(dot, mimi_ui_color(MIMI_CORAL), 0);
    lv_obj_t *marker = lv_obj_get_child(dot, 0);
    if (marker) lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
    schedule_rebuild();
}

static void follow_time_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    printf("Theme follow-time: %s\n", on ? "on" : "off");
    ui_theme_set_follow_time(on);
    if (on) {
        ui_clock_apply_theme();
        schedule_rebuild();
    }
}

static void locale_mode_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    sys_locale_mode_t mode = (sys_locale_mode_t)(intptr_t)
                             lv_event_get_user_data(e);
    ui_i18n_set_mode(mode);
    ui_rebuild_home();
    schedule_rebuild();
}

static void appearance_mode_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    sys_appearance_mode_t mode = (sys_appearance_mode_t)(intptr_t)
                                 lv_event_get_user_data(e);
    ui_theme_set_appearance(mode);
    ui_rebuild_home();
    schedule_rebuild();
}

static lv_obj_t *create_segment(lv_obj_t *parent, const char *text,
                                bool selected, lv_event_cb_t callback,
                                intptr_t value)
{
    lv_obj_t *button = lv_button_create(parent);
    mimi_ui_button_style(button, selected);
    lv_obj_set_size(button, 1, 48);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_bg_color(button,
                              selected ? mimi_ui_color(MIMI_CORAL) : mimi_ui_color(MIMI_SURFACE_ALT), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, selected ? 0 : 1, 0);
    lv_obj_set_style_border_color(button, mimi_ui_color(MIMI_BORDER), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, (void *)value);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(label,
                                selected ? lv_color_white() : mimi_ui_color(MIMI_TEXT), 0);
    lv_obj_center(label);
    return button;
}

static void create_preference_row(lv_obj_t *parent, const char *title,
                                  const char *labels[3], int selected,
                                  lv_event_cb_t callback)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 80);
    make_transparent(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, 4, 0);
    lv_obj_t *title_label = lv_label_create(row);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(title_label, mimi_ui_color(MIMI_MUTED), 0);
    lv_obj_t *segments = lv_obj_create(row);
    lv_obj_set_size(segments, lv_pct(100), 48);
    lv_obj_set_style_pad_column(segments, 8, 0);
    make_transparent(segments);
    lv_obj_set_flex_flow(segments, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(segments, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < 3; i++)
        create_segment(segments, labels[i], i == selected, callback, i);
}

static void setting_ai_status_cb(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_AI_STATUS ||
        msg->data_len != sizeof(ai_status_t) || !schema_label) return;
    const ai_status_t *status = (const ai_status_t *)msg->data;
    if (status->tuya_schema_known) {
        lv_label_set_text_fmt(schema_label,
            ui_i18n_is_zh() ? "%u / %u 个功能可用" : "%u / %u functions available",
            (unsigned)status->tuya_schema_supported_dps,
            (unsigned)status->tuya_schema_required_dps);
    } else {
        lv_label_set_text(schema_label, ui_i18n_is_zh() ? "等待云端状态" : "Waiting for cloud status");
    }
}

static lv_obj_t *create_section(lv_obj_t *parent, const char *title,
                                 int x, int y, int width, int height)
{
    lv_obj_t *section = lv_obj_create(parent);
    mimi_ui_surface(section, 16);
    lv_obj_set_pos(section, x, y);
    lv_obj_set_size(section, width, height);
    lv_obj_set_style_pad_all(section, 22, 0);
    lv_obj_set_style_pad_row(section, 14, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    mimi_ui_text(section, title, ui_font_h2(), MIMI_TEXT);
    return section;
}

static void create_horizontal_control(lv_obj_t *parent, const char *title,
                                       int min_value, int value, lv_event_cb_t callback)
{
    lv_obj_t *row = lv_obj_create(parent);
    mimi_ui_clear(row);
    lv_obj_set_size(row, lv_pct(100), 60);
    mimi_ui_text(row, title, ui_font_body_md(), MIMI_MUTED);
    lv_obj_t *value_label = mimi_ui_text(row, "", ui_font_body_md(), MIMI_TEXT);
    lv_label_set_text_fmt(value_label, "%d%%", value);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, lv_pct(94), 8);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_slider_set_range(slider, min_value, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, mimi_ui_color(MIMI_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, mimi_ui_color(MIMI_CORAL), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, mimi_ui_color(MIMI_SURFACE), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 3, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, mimi_ui_color(MIMI_CORAL), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 10, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 20);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, value_label);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_RELEASED, value_label);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_PRESS_LOST, value_label);
}

static lv_obj_t *create_info_row(lv_obj_t *parent, const char *key,
                                 const char *value, lv_obj_t **out)
{
    lv_obj_t *row = lv_obj_create(parent);
    mimi_ui_clear(row);
    lv_obj_set_size(row, lv_pct(100), 88);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, mimi_ui_color(MIMI_BORDER), 0);
    mimi_ui_text(row, key, ui_font_body_md(), MIMI_TEXT);
    lv_obj_t *label = mimi_ui_text(row, value, ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_pos(label, 0, 30);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    if (out) *out = label;
    return row;
}

static void setting_init(void)
{
    setting_active = true;
    /* Rebuilds replace the widgets, not their middleware subscriptions. */
    mw_unsubscribe(TOPIC_CLOUD_STATUS, setting_cloud_status_cb);
    mw_unsubscribe(TOPIC_AI_STATUS, setting_ai_status_cb);
    lv_obj_t *scr = lv_obj_create(NULL);
    mimi_ui_header(scr, ui_tr(UI_STR_APP_SETTINGS), back_event_handler);
    lv_obj_t *body = mimi_ui_body(scr);
    lv_obj_update_layout(body);
    int width = lv_obj_get_width(body);
    int height = lv_obj_get_height(body);
    int left_width = (width - 18) / 2;
    sys_settings_t *settings = sys_settings_get();
    lv_obj_t *appearance = create_section(body,
        ui_i18n_is_zh() ? "外观与语言" : "Appearance & language",
        0, 0, left_width, 260);
    const char *languages[] = {ui_tr(UI_STR_SETTINGS_LANGUAGE_AUTO),
        ui_tr(UI_STR_SETTINGS_LANGUAGE_ZH), ui_tr(UI_STR_SETTINGS_LANGUAGE_EN)};
    const char *modes[] = {ui_tr(UI_STR_SETTINGS_THEME_AUTO),
        ui_tr(UI_STR_SETTINGS_THEME_LIGHT), ui_tr(UI_STR_SETTINGS_THEME_DARK)};
    create_preference_row(appearance, ui_tr(UI_STR_SETTINGS_LANGUAGE), languages,
        (int)ui_i18n_mode(), locale_mode_cb);
    create_preference_row(appearance, ui_tr(UI_STR_SETTINGS_THEME), modes,
        (int)ui_theme_get_appearance(), appearance_mode_cb);

    lv_obj_t *sound = create_section(body, ui_tr(UI_STR_SETTINGS_DISPLAY_SOUND),
        0, 278, left_width, height - 278);
    lv_obj_set_style_pad_all(sound, 18, 0);
    lv_obj_set_style_pad_row(sound, 6, 0);
    create_horizontal_control(sound, ui_tr(UI_STR_SETTINGS_BRIGHTNESS),
        20, settings->brightness, brightness_slider_cb);
    create_horizontal_control(sound, ui_tr(UI_STR_SETTINGS_VOLUME),
        0, settings->volume, volume_slider_cb);
    lv_obj_t *follow = lv_obj_create(sound);
    mimi_ui_clear(follow);
    lv_obj_set_size(follow, lv_pct(100), 48);
    lv_obj_t *follow_label = mimi_ui_text(follow, ui_tr(UI_STR_SETTINGS_FOLLOW_TIME),
        ui_font_body_md(), MIMI_MUTED);
    lv_obj_align(follow_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sw = lv_switch_create(follow);
    lv_obj_set_size(sw, 52, 30);
    lv_obj_set_ext_click_area(sw, 9);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, mimi_ui_color(MIMI_TEAL), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (ui_theme_get_follow_time()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, follow_time_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *colors = lv_obj_create(sound);
    mimi_ui_clear(colors);
    lv_obj_set_size(colors, lv_pct(100), 48);
    lv_obj_set_flex_flow(colors, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(colors, 16, 0);
    static uint32_t preset_ids[UI_THEME_GRADIENT_PRESET_COUNT] = {0, 1, 2, 3, 4};
    for (uint32_t i = 0; i < UI_THEME_GRADIENT_PRESET_COUNT; ++i) {
        lv_obj_t *dot = lv_button_create(colors);
        mimi_ui_button_style(dot, false);
        lv_obj_set_size(dot, 48, 48);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, ui_theme_get_gradient_color(i, 0), 0);
        lv_obj_t *marker = lv_label_create(dot);
        lv_label_set_text(marker, LV_SYMBOL_OK);
        lv_obj_set_style_text_font(marker, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(marker, lv_color_white(), 0);
        lv_obj_center(marker);
        if (i != ui_theme_get_gradient_preset_id()) lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(dot, color_event_cb, LV_EVENT_CLICKED, &preset_ids[i]);
    }

    lv_obj_t *device = create_section(body, ui_i18n_is_zh() ? "设备信息" : "Device info",
        left_width + 18, 0, width - left_width - 18, height);
    char version[64];
    read_system_version(version, sizeof(version));
    create_info_row(device, ui_tr(UI_STR_SETTINGS_DEVICE_NAME), APP_DEVICE_NAME, NULL);
    create_info_row(device, ui_tr(UI_STR_SETTINGS_SYSTEM_VERSION), version, NULL);
    bind_code_item = create_info_row(device, ui_tr(UI_STR_SETTINGS_100ASK_PAIRING),
        ui_tr(UI_STR_LOADING), &bind_code_label);
    lv_obj_add_flag(bind_code_item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bind_code_item, bind_token_click_cb, LV_EVENT_CLICKED, NULL);
    create_info_row(device, ui_i18n_is_zh() ? "涂鸦物联模型" : "Tuya IoT schema",
        ui_i18n_is_zh() ? "等待云端状态" : "Waiting for cloud status", &schema_label);
    lv_obj_t *preview = lv_obj_create(device);
    mimi_ui_surface(preview, 10);
    lv_obj_set_size(preview, lv_pct(100), 112);
    lv_obj_set_style_bg_color(preview, mimi_ui_color(MIMI_SURFACE_ALT), 0);
    lv_obj_set_style_pad_all(preview, 16, 0);
    mimi_ui_text(preview, ui_i18n_is_zh() ? "桌面应用更新" : "Desktop application update", ui_font_h3(), MIMI_TEXT);
    lv_obj_add_flag(preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(preview, desktop_update_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mode = mimi_ui_text(preview, ui_theme_is_dark() ?
        (ui_i18n_is_zh() ? "当前使用暗色模式" : "Dark mode is active") :
        (ui_i18n_is_zh() ? "当前使用亮色模式" : "Light mode is active"), ui_font_body_md(), MIMI_MUTED);
    lv_obj_align(mode, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    mw_subscribe(TOPIC_CLOUD_STATUS, setting_cloud_status_cb);
    mw_subscribe(TOPIC_AI_STATUS, setting_ai_status_cb);
    request_fresh_bind_token();
    ai_cmd_t cmd = {.action = AI_CMD_GET_STATUS};
    mw_publish(TOPIC_AI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    lv_scr_load(scr);
}

static void setting_close(void)
{
    setting_active = false;
    if (rebuild_pending) lv_async_call_cancel(setting_rebuild_async, NULL);
    rebuild_pending = false;
    bind_code_label = NULL;
    bind_code_item = NULL;
    schema_label = NULL;
    mw_unsubscribe(TOPIC_CLOUD_STATUS, setting_cloud_status_cb);
    mw_unsubscribe(TOPIC_AI_STATUS, setting_ai_status_cb);
    if (settings_dirty) {
        sys_settings_save();
        settings_dirty = false;
    }
}

AppDescriptor app_setting = {
    .id = APP_ID_SETTING, .name = "setting",
    .init = setting_init, .close = setting_close
};
