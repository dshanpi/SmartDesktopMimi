#include "desktop_all_apps.h"
#include "ui_fonts.h"
#include "../system/app_manager.h"
#include "../system/app_registry.h"

#define ALL_APPS_CARD_WIDTH 208
#define ALL_APPS_CARD_HEIGHT 202
#define ALL_APPS_ICON_SIZE 92

static lv_obj_t *all_apps_overlay;
static lv_obj_t *all_apps_first_button;

static void set_non_interactive(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_generated_icon(lv_obj_t *parent,
                                       const app_registry_entry_t *entry)
{
    lv_obj_t *plate = lv_obj_create(parent);
    lv_obj_set_size(plate, ALL_APPS_ICON_SIZE, ALL_APPS_ICON_SIZE);
    lv_obj_set_style_radius(plate, 24, 0);
    lv_obj_set_style_bg_color(plate, lv_color_hex(entry->accent_color), 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(plate, 0, 0);
    lv_obj_set_style_shadow_color(plate, lv_color_hex(entry->accent_color), 0);
    lv_obj_set_style_shadow_opa(plate, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(plate, 14, 0);
    lv_obj_set_style_shadow_offset_y(plate, 5, 0);
    lv_obj_set_style_pad_all(plate, 0, 0);
    set_non_interactive(plate);

    lv_obj_t *symbol = lv_label_create(plate);
    const char *symbol_text = LV_SYMBOL_DIRECTORY;
    if (entry->id == APP_ID_HDMI_MCP)
        symbol_text = LV_SYMBOL_EYE_OPEN;
    else if (entry->id == APP_ID_OTA)
        symbol_text = LV_SYMBOL_DOWNLOAD;
    lv_label_set_text(symbol, symbol_text);
    /* LVGL symbols live in the built-in Montserrat fonts, not the runtime
     * CJK/Inter text fonts used by the rest of the product UI. */
    lv_obj_set_style_text_font(symbol, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(symbol, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(symbol);
    set_non_interactive(symbol);
    return plate;
}

static lv_obj_t *create_app_icon(lv_obj_t *parent,
                                 const app_registry_entry_t *entry)
{
    if (entry->icon_type == APP_ICON_GENERATED || !entry->icon_src)
        return create_generated_icon(parent, entry);

    lv_obj_t *icon = lv_image_create(parent);
    lv_image_set_src(icon, entry->icon_src);
    set_non_interactive(icon);
    return icon;
}

void ui_all_apps_hide(void)
{
    if (!all_apps_overlay || !lv_obj_is_valid(all_apps_overlay))
        return;
    lv_obj_add_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void close_click_cb(lv_event_t *event)
{
    (void)event;
    ui_all_apps_hide();
}

static void app_click_cb(lv_event_t *event)
{
    const app_registry_entry_t *entry = lv_event_get_user_data(event);
    if (!entry)
        return;
    ui_all_apps_hide();
    app_manager_open(entry->id);
}

static void overlay_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY)
        return;
    uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_HOME)
        ui_all_apps_hide();
}

static lv_obj_t *create_app_card(lv_obj_t *grid,
                                 const app_registry_entry_t *entry)
{
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_size(card, ALL_APPS_CARD_WIDTH, ALL_APPS_CARD_HEIGHT);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xE7EAF0), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x8692A8), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(card, 18, 0);
    lv_obj_set_style_shadow_offset_y(card, 7, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_scale(card, 248, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(card, 4, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(card,
                                   lv_color_hex(entry->accent_color),
                                   LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(card, LV_OPA_50, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(card, app_click_cb, LV_EVENT_CLICKED, (void *)entry);

    lv_obj_t *icon = create_app_icon(card, entry);
    lv_obj_set_style_flex_grow(icon, 0, 0);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, entry->name);
    lv_obj_set_width(name, ALL_APPS_CARD_WIDTH - 30);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, ui_font_h2(), 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0x263340), 0);
    set_non_interactive(name);

    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_set_size(accent, 34, 5);
    lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(entry->accent_color), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    set_non_interactive(accent);
    return card;
}

void ui_all_apps_create(lv_obj_t *parent)
{
    all_apps_overlay = lv_obj_create(parent);
    lv_obj_set_size(all_apps_overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(all_apps_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(all_apps_overlay, lv_color_hex(0xF3F5F9), 0);
    lv_obj_set_style_bg_grad_color(all_apps_overlay, lv_color_hex(0xE9EEF7), 0);
    lv_obj_set_style_bg_grad_dir(all_apps_overlay, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(all_apps_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(all_apps_overlay, 0, 0);
    lv_obj_set_style_radius(all_apps_overlay, 0, 0);
    lv_obj_set_style_pad_all(all_apps_overlay, 0, 0);
    lv_obj_add_flag(all_apps_overlay,
                    LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(all_apps_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(all_apps_overlay, overlay_key_cb, LV_EVENT_KEY, NULL);

    lv_obj_t *header = lv_obj_create(all_apps_overlay);
    lv_obj_set_size(header, lv_pct(100), 104);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_90, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 48, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "All Apps");
    lv_obj_set_style_text_font(title, ui_font_h1(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x202B38), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, -8);

    lv_obj_t *subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, "Choose an app. Your favorites stay on the desktop.");
    lv_obj_set_style_text_font(subtitle, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x748092), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lv_obj_t *close = lv_button_create(header);
    lv_obj_set_size(close, 60, 60);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(close, 20, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(0xEDF0F5), 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_add_event_cb(close, close_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_symbol = lv_label_create(close);
    lv_label_set_text(close_symbol, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_symbol, lv_color_hex(0x354152), 0);
    lv_obj_center(close_symbol);

    lv_obj_t *grid = lv_obj_create(all_apps_overlay);
    lv_obj_set_size(grid, 944, 632);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 14, 0);
    lv_obj_set_style_pad_row(grid, 24, 0);
    lv_obj_set_style_pad_column(grid, 24, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE |
                          LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);

    size_t app_count = app_registry_get_launcher_count();
    for (size_t i = 0; i < app_count; i++) {
        const app_registry_entry_t *entry =
            app_registry_get_launcher_entry(i);
        if (!entry)
            continue;
        lv_obj_t *card = create_app_card(grid, entry);
        if (!all_apps_first_button)
            all_apps_first_button = card;
    }
}

void ui_all_apps_show(void)
{
    if (!all_apps_overlay || !lv_obj_is_valid(all_apps_overlay))
        return;
    lv_obj_clear_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(all_apps_overlay);
    if (all_apps_first_button && lv_obj_is_valid(all_apps_first_button)) {
        lv_group_t *group = lv_group_get_default();
        if (group) {
            lv_group_add_obj(group, all_apps_first_button);
            lv_group_focus_obj(all_apps_first_button);
        }
    }
}
