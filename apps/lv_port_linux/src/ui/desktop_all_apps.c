#include "desktop_all_apps.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
#include "theme/theme.h"
#include "mimi_ui.h"
#include "../system/app_manager.h"
#include "../system/app_registry.h"

#define ALL_APPS_CARD_WIDTH 214
#define ALL_APPS_CARD_HEIGHT 202
#define ALL_APPS_ICON_SIZE 56
#define ALL_APPS_COLUMN_COUNT 4

static lv_obj_t *all_apps_overlay;

bool ui_all_apps_is_visible(void)
{
    return all_apps_overlay && lv_obj_is_valid(all_apps_overlay) &&
           !lv_obj_has_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *all_apps_grid;
static int32_t all_apps_focus_index;

static void set_non_interactive(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_generated_icon(lv_obj_t *parent,
                                       const app_registry_entry_t *entry)
{
    lv_obj_t *plate = lv_obj_create(parent);
    lv_obj_set_size(plate, ALL_APPS_ICON_SIZE, ALL_APPS_ICON_SIZE);
    lv_obj_set_style_radius(plate, 12, 0);
    lv_obj_set_style_bg_color(plate, lv_color_hex(entry->accent_color), 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(plate, 0, 0);
    lv_obj_set_style_shadow_color(plate, lv_color_hex(entry->accent_color), 0);
    lv_obj_set_style_shadow_opa(plate, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(plate, 0, 0);
    lv_obj_set_style_shadow_offset_y(plate, 5, 0);
    lv_obj_set_style_pad_all(plate, 0, 0);
    set_non_interactive(plate);

    lv_obj_t *symbol = lv_label_create(plate);
    const char *symbol_text = LV_SYMBOL_DIRECTORY;
    switch (entry->id) {
    case APP_ID_AI: symbol_text = LV_SYMBOL_AUDIO; break;
    case APP_ID_BT: symbol_text = LV_SYMBOL_BLUETOOTH; break;
    case APP_ID_WIFI: symbol_text = LV_SYMBOL_WIFI; break;
    case APP_ID_SETTING: symbol_text = LV_SYMBOL_SETTINGS; break;
    case APP_ID_VIDEO: symbol_text = LV_SYMBOL_VIDEO; break;
    default: break;
    }
    if (entry->id == APP_ID_HDMI_MCP)
        symbol_text = LV_SYMBOL_EYE_OPEN;
    else if (entry->id == APP_ID_OTA)
        symbol_text = LV_SYMBOL_DOWNLOAD;
    lv_label_set_text(symbol, symbol_text);
    /* LVGL symbols live in the built-in Montserrat fonts, not the runtime
     * CJK/Inter text fonts used by the rest of the product UI. */
    lv_obj_set_style_text_font(symbol, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(symbol, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(symbol);
    set_non_interactive(symbol);
    return plate;
}

static lv_obj_t *create_app_icon(lv_obj_t *parent,
                                 const app_registry_entry_t *entry)
{
    return create_generated_icon(parent, entry);
}

void ui_all_apps_hide(void)
{
    if (!all_apps_overlay || !lv_obj_is_valid(all_apps_overlay))
        return;
    if (all_apps_grid && lv_obj_is_valid(all_apps_grid)) {
        lv_group_remove_obj(all_apps_grid);
        uint32_t count = lv_obj_get_child_count(all_apps_grid);
        if (count > 0 && all_apps_focus_index >= 0 &&
            (uint32_t)all_apps_focus_index < count) {
            lv_obj_remove_state(lv_obj_get_child(all_apps_grid,
                                                 all_apps_focus_index),
                                LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        }
    }
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

static void focus_app_card(int32_t index)
{
    if (!all_apps_grid || !lv_obj_is_valid(all_apps_grid)) return;
    uint32_t count = lv_obj_get_child_count(all_apps_grid);
    if (count == 0) return;

    if (index < 0) index = 0;
    if ((uint32_t)index >= count) index = (int32_t)count - 1;
    if (all_apps_focus_index >= 0 &&
        (uint32_t)all_apps_focus_index < count) {
        lv_obj_remove_state(lv_obj_get_child(all_apps_grid,
                                             all_apps_focus_index),
                            LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    }

    all_apps_focus_index = index;
    lv_obj_t *card = lv_obj_get_child(all_apps_grid, all_apps_focus_index);
    lv_obj_add_state(card, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    lv_obj_scroll_to_view(card, LV_ANIM_ON);
}

static void grid_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(event);
    uint32_t count = all_apps_grid ? lv_obj_get_child_count(all_apps_grid) : 0;
    if (key == LV_KEY_ESC || key == LV_KEY_HOME) {
        ui_all_apps_hide();
        return;
    }
    if (count == 0) return;

    int32_t next = all_apps_focus_index;
    if (key == LV_KEY_LEFT) {
        next = (next + (int32_t)count - 1) % (int32_t)count;
    } else if (key == LV_KEY_RIGHT) {
        next = (next + 1) % (int32_t)count;
    } else if (key == LV_KEY_UP) {
        if (next >= ALL_APPS_COLUMN_COUNT) next -= ALL_APPS_COLUMN_COUNT;
    } else if (key == LV_KEY_DOWN) {
        if ((uint32_t)(next + ALL_APPS_COLUMN_COUNT) < count)
            next += ALL_APPS_COLUMN_COUNT;
    } else if (key == LV_KEY_ENTER) {
        lv_obj_t *card = lv_obj_get_child(all_apps_grid,
                                          all_apps_focus_index);
        lv_obj_send_event(card, LV_EVENT_CLICKED, NULL);
        return;
    } else {
        return;
    }
    focus_app_card(next);
}

static lv_obj_t *create_app_card(lv_obj_t *grid,
                                 const app_registry_entry_t *entry)
{
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_size(card, ALL_APPS_CARD_WIDTH, ALL_APPS_CARD_HEIGHT);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, mimi_ui_color(MIMI_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, mimi_ui_color(MIMI_BORDER), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x8692A8), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
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
    lv_label_set_text(name, ui_tr(entry->name_id));
    lv_obj_set_width(name, ALL_APPS_CARD_WIDTH - 36);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(name, mimi_ui_color(MIMI_TEXT), 0);
    set_non_interactive(name);

    const char *detail = "";
    switch (entry->id) {
    case APP_ID_WIFI: detail = ui_tr(UI_STR_HOME_SCAN_CONNECT); break;
    case APP_ID_AI: detail = ui_i18n_is_zh() ? "语音与物联" : "Voice & IoT"; break;
    case APP_ID_BT: detail = ui_i18n_is_zh() ? "播放手机音乐" : "Play phone audio"; break;
    case APP_ID_VIDEO: detail = ui_i18n_is_zh() ? "microSD 媒体库" : "microSD library"; break;
    case APP_ID_SETTING: detail = ui_tr(UI_STR_HOME_DISPLAY_SOUND); break;
    case APP_ID_OTA: detail = ui_i18n_is_zh() ? "版本与安装" : "Version & install"; break;
    case APP_ID_HDMI_MCP: detail = ui_i18n_is_zh() ? "AI 控制电脑" : "AI computer control"; break;
    default: detail = ui_i18n_is_zh() ? "扩展设备能力" : "Extend capabilities"; break;
    }
    lv_obj_t *hint = mimi_ui_text(card, detail, ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, ALL_APPS_CARD_WIDTH - 36);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    return card;
}

void ui_all_apps_create(lv_obj_t *parent)
{
    all_apps_overlay = lv_obj_create(parent);
    lv_obj_set_size(all_apps_overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(all_apps_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(all_apps_overlay, mimi_ui_color(MIMI_BG), 0);
    lv_obj_set_style_bg_grad_color(all_apps_overlay, mimi_ui_color(MIMI_BG), 0);
    lv_obj_set_style_bg_grad_dir(all_apps_overlay, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(all_apps_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(all_apps_overlay, 0, 0);
    lv_obj_set_style_radius(all_apps_overlay, 0, 0);
    lv_obj_set_style_pad_all(all_apps_overlay, 0, 0);
    lv_obj_add_flag(all_apps_overlay,
                    LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(all_apps_overlay, LV_OBJ_FLAG_SCROLLABLE);

    mimi_ui_header(all_apps_overlay, ui_tr(UI_STR_ALL_APPS), close_click_cb);
    lv_obj_t *body = mimi_ui_body(all_apps_overlay);
    lv_obj_t *heading = mimi_ui_text(body,
        ui_i18n_is_zh() ? "需要什么，就从这里开始" : "Everything starts here",
        ui_font_h2(), MIMI_TEXT);
    lv_obj_set_pos(heading, 0, 8);
    lv_obj_t *hint = mimi_ui_text(body, ui_tr(UI_STR_ALL_APPS_HINT),
                                 ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_pos(hint, 0, 56);
    lv_obj_update_layout(body);
    all_apps_grid = lv_obj_create(body);
    mimi_ui_clear(all_apps_grid);
    lv_obj_set_style_pad_all(all_apps_grid, 4, 0);
    lv_obj_set_size(all_apps_grid, lv_pct(100), lv_obj_get_height(body) - 120);
    lv_obj_set_pos(all_apps_grid, 0, 120);
    lv_obj_set_style_pad_row(all_apps_grid, 16, 0);
    lv_obj_set_style_pad_column(all_apps_grid, 16, 0);
    lv_obj_set_flex_flow(all_apps_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(all_apps_grid, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(all_apps_grid, LV_OBJ_FLAG_SCROLLABLE |
                                   LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                   LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_scroll_dir(all_apps_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(all_apps_grid, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(all_apps_grid, grid_key_cb, LV_EVENT_KEY, NULL);

    size_t app_count = app_registry_get_launcher_count();
    for (size_t i = 0; i < app_count; i++) {
        const app_registry_entry_t *entry =
            app_registry_get_launcher_entry(i);
        if (!entry)
            continue;
        create_app_card(all_apps_grid, entry);
    }
    all_apps_focus_index = 0;
}

void ui_all_apps_show(void)
{
    if (!all_apps_overlay || !lv_obj_is_valid(all_apps_overlay))
        return;
    lv_obj_clear_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(all_apps_overlay);
    if (all_apps_grid && lv_obj_is_valid(all_apps_grid)) {
        lv_group_t *group = lv_group_get_default();
        if (group) {
            lv_group_add_obj(group, all_apps_grid);
            lv_group_focus_obj(all_apps_grid);
        }
        focus_app_card(0);
    }
}
