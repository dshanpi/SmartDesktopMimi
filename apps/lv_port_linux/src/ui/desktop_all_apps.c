#include "desktop_all_apps.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
#include "theme/theme.h"
#include "mimi_ui.h"
#include "desktop_update.h"
#include "../system/app_manager.h"
#include "../system/app_registry.h"

#define ALL_APPS_CARD_WIDTH 214
#define ALL_APPS_CARD_HEIGHT 162
#define ALL_APPS_ICON_SIZE 56
#define ALL_APPS_COLUMN_COUNT 4

static const app_registry_entry_t desktop_update_entry = {
    .id = APP_ID_NONE, .name = "Desktop Update", .name_id = UI_STR_HOME_UPDATES,
    .accent_color = 0x30B27A, .show_in_launcher = true,
};
static lv_obj_t *all_apps_overlay;

bool ui_all_apps_is_visible(void)
{
    return all_apps_overlay && lv_obj_is_valid(all_apps_overlay) &&
           !lv_obj_has_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *all_apps_grid;
static int32_t all_apps_focus_index;
static lv_obj_t *all_apps_cards[APP_ID_MAX];
static uint32_t all_apps_count;
static int32_t all_apps_columns = ALL_APPS_COLUMN_COUNT;
static int32_t all_apps_card_width = ALL_APPS_CARD_WIDTH;
static int32_t all_apps_card_height = ALL_APPS_CARD_HEIGHT;
static lv_obj_t *return_focus;
static lv_obj_t *drawer_sheet;

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
    case APP_ID_IOT: symbol_text = LV_SYMBOL_HOME; break;
    case APP_ID_VOICE: symbol_text = LV_SYMBOL_AUDIO; break;
    case APP_ID_AI: symbol_text = LV_SYMBOL_AUDIO; break;
    case APP_ID_BT: symbol_text = LV_SYMBOL_BLUETOOTH; break;
    case APP_ID_WIFI: symbol_text = LV_SYMBOL_WIFI; break;
    case APP_ID_SETTING: symbol_text = LV_SYMBOL_SETTINGS; break;
    case APP_ID_VIDEO: symbol_text = LV_SYMBOL_VIDEO; break;
    default: break;
    }
    if (entry->id == APP_ID_HDMI_MCP)
        symbol_text = LV_SYMBOL_EYE_OPEN;
    else if (entry->id == APP_ID_OTA || entry == &desktop_update_entry)
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
    if (!all_apps_overlay || !lv_obj_is_valid(all_apps_overlay)) return;
    if (all_apps_grid && lv_obj_is_valid(all_apps_grid)) lv_group_remove_obj(all_apps_grid);
    lv_obj_add_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN);
    if (return_focus && lv_obj_is_valid(return_focus)) lv_group_focus_obj(return_focus);
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
    for (uint32_t i=0; i<all_apps_count; ++i)
        if (all_apps_cards[i] == lv_event_get_target(event)) all_apps_focus_index=(int32_t)i;
    ui_all_apps_hide();
    if (entry == &desktop_update_entry) desktop_update_show();
    else app_manager_open(entry->id);
}

static void focus_app_card(int32_t index)
{
    if (!all_apps_grid || !lv_obj_is_valid(all_apps_grid)) return;
    uint32_t count = all_apps_count;
    if (count == 0) return;

    if (index < 0) index = 0;
    if ((uint32_t)index >= count) index = (int32_t)count - 1;
    if (all_apps_focus_index >= 0 &&
        (uint32_t)all_apps_focus_index < count) {
        lv_obj_remove_state(all_apps_cards[all_apps_focus_index],
                            LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    }

    all_apps_focus_index = index;
    lv_obj_t *card = all_apps_cards[all_apps_focus_index];
    lv_obj_add_state(card, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    lv_obj_scroll_to_view_recursive(card, LV_ANIM_ON);
}

static void grid_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(event);
    uint32_t count = all_apps_grid ? all_apps_count : 0;
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
        if (next >= all_apps_columns) next -= all_apps_columns;
    } else if (key == LV_KEY_DOWN) {
        if ((uint32_t)(next + all_apps_columns) < count)
            next += all_apps_columns;
    } else if (key == LV_KEY_ENTER) {
        lv_obj_t *card = all_apps_cards[all_apps_focus_index];
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
    lv_obj_set_size(card, all_apps_card_width, all_apps_card_height);
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
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
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
    lv_obj_set_width(name, all_apps_card_width - 20);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(name, mimi_ui_color(MIMI_TEXT), 0);
    set_non_interactive(name);

    const char *detail = "";
    switch (entry->id) {
    case APP_ID_PAIRING: detail = ui_i18n_is_zh() ? "连接你的账号" : "Connect your account"; break;
    case APP_ID_NONE: detail = ui_i18n_is_zh() ? "桌面版本与安装" : "Desktop version & install"; break;
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
    lv_obj_set_width(hint, all_apps_card_width - 20);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    return card;
}

static void shade_click_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == all_apps_overlay) ui_all_apps_hide();
}

static void grip_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *input = lv_indev_get_act();
    if (input && lv_indev_get_gesture_dir(input) == LV_DIR_BOTTOM) ui_all_apps_hide();
}

static void drawer_slide(void *obj, int32_t offset)
{
    lv_obj_set_style_translate_y(obj, offset, 0);
}

void ui_all_apps_create(lv_obj_t *parent)
{
    all_apps_focus_index = 0;
    all_apps_count = 0;
    return_focus = NULL;
    all_apps_overlay = lv_obj_create(parent);
    mimi_ui_clear(all_apps_overlay);
    lv_obj_set_size(all_apps_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(all_apps_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(all_apps_overlay, LV_OPA_40, 0);
    lv_obj_add_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(all_apps_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(all_apps_overlay, shade_click_cb, LV_EVENT_CLICKED, NULL);

    drawer_sheet = lv_obj_create(all_apps_overlay);
    mimi_ui_clear(drawer_sheet);
    lv_obj_set_size(drawer_sheet, lv_pct(100), lv_pct(72));
    lv_obj_align(drawer_sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(drawer_sheet, mimi_ui_color(MIMI_BG), 0);
    lv_obj_set_style_bg_opa(drawer_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(drawer_sheet, 24, 0);
    lv_obj_clear_flag(drawer_sheet, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *grip = lv_button_create(drawer_sheet);
    lv_obj_set_size(grip, 180, 56);
    lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(grip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(grip, 0, 0);
    lv_obj_add_event_cb(grip, close_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(grip, grip_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(grip, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *grip_line = lv_obj_create(grip);
    lv_obj_set_size(grip_line, 48, 4);
    lv_obj_center(grip_line);
    lv_obj_set_style_bg_color(grip_line, mimi_ui_color(MIMI_MUTED), 0);
    lv_obj_set_style_border_width(grip_line, 0, 0);
    set_non_interactive(grip_line);
    lv_obj_t *title = mimi_ui_text(drawer_sheet, ui_tr(UI_STR_ALL_APPS), ui_font_h2(), MIMI_TEXT);
    lv_obj_set_pos(title, 24, 56);
    lv_obj_t *hint = mimi_ui_text(drawer_sheet, ui_i18n_is_zh() ? "左右滑动 · 下滑收起" : "Swipe sideways · pull handle down to close", ui_font_body_md(), MIMI_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_update_layout(drawer_sheet);
    int32_t width = lv_obj_get_width(drawer_sheet) - 48;
    int32_t height = lv_obj_get_height(drawer_sheet) - 148;
    all_apps_columns = width >= 800 ? 4 : width >= 580 ? 3 : 2;
    all_apps_card_width = (width - (all_apps_columns - 1) * 12) / all_apps_columns;
    all_apps_card_height = (height - 12) / 2;
    all_apps_grid = lv_obj_create(drawer_sheet);
    mimi_ui_clear(all_apps_grid);
    lv_obj_set_size(all_apps_grid, width, height);
    lv_obj_set_pos(all_apps_grid, 24, 100);
    lv_obj_set_flex_flow(all_apps_grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(all_apps_grid, 24, 0);
    lv_obj_add_flag(all_apps_grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_scroll_dir(all_apps_grid, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(all_apps_grid, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(all_apps_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(all_apps_grid, grid_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *page = NULL;
    size_t count = app_registry_get_launcher_count() + 1;
    for (size_t i = 0; i < count && all_apps_count < APP_ID_MAX; ++i) {
        const app_registry_entry_t *entry = i + 1 == count ? &desktop_update_entry : app_registry_get_launcher_entry(i);
        if (!entry) continue;
        if (all_apps_count % (all_apps_columns * 2) == 0) {
            page = lv_obj_create(all_apps_grid);
            mimi_ui_clear(page);
            lv_obj_set_size(page, width, height);
            lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_column(page, 12, 0);
            lv_obj_set_style_pad_row(page, 12, 0);
            lv_obj_add_flag(page, LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
            lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        }
        all_apps_cards[all_apps_count++] = create_app_card(page, entry);
    }
}

void ui_all_apps_show(void)
{
    if (!all_apps_overlay || !lv_obj_is_valid(all_apps_overlay) || ui_all_apps_is_visible()) return;
    lv_group_t *group = lv_group_get_default();
    return_focus = group ? lv_group_get_focused(group) : NULL;
    lv_obj_clear_flag(all_apps_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(all_apps_overlay);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, drawer_sheet);
    lv_anim_set_values(&animation, lv_obj_get_height(drawer_sheet), 0);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_exec_cb(&animation, drawer_slide);
    lv_anim_start(&animation);
    if (group) { lv_group_add_obj(group, all_apps_grid); lv_group_focus_obj(all_apps_grid); }
    lv_indev_t *input=lv_indev_get_act();
    if (!input || lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER)
        focus_app_card(all_apps_focus_index);
}
