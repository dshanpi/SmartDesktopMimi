#include "desktop_dock.h"
#include "desktop_all_apps.h"
#include "desktop_home_tokens.h"
#include "ui_fonts.h"
#include "../system/app_manager.h"
#include "../apps/app.h"

#include <stdint.h>

#define NAV_ITEM_COUNT 4

typedef enum {
    NAV_HOME = 0,
    NAV_ASSISTANT,
    NAV_HDMI,
    NAV_MORE,
} nav_destination_t;

static lv_obj_t *nav_root;
static lv_obj_t *nav_items[NAV_ITEM_COUNT];
static lv_obj_t *nav_icons[NAV_ITEM_COUNT];
static lv_obj_t *nav_labels[NAV_ITEM_COUNT];
static lv_obj_t *nav_indicators[NAV_ITEM_COUNT];
static int nav_selected;

static void nav_open(int destination)
{
    switch ((nav_destination_t)destination) {
    case NAV_HOME:
        break;
    case NAV_ASSISTANT:
        app_manager_open(APP_ID_AI);
        break;
    case NAV_HDMI:
        app_manager_open(APP_ID_HDMI_MCP);
        break;
    case NAV_MORE:
        ui_all_apps_show();
        break;
    }
}

static void nav_apply_selection(int index)
{
    if (index < 0) index = 0;
    if (index >= NAV_ITEM_COUNT) index = NAV_ITEM_COUNT - 1;
    nav_selected = index;

    for (int i = 0; i < NAV_ITEM_COUNT; i++) {
        bool selected = i == nav_selected;
        lv_color_t color = selected ? HOME_CYAN : HOME_TEXT_MUTED;
        lv_obj_set_style_bg_opa(nav_items[i],
                                selected ? LV_OPA_10 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(nav_icons[i], color, 0);
        lv_obj_set_style_text_color(nav_labels[i], color, 0);
        if (selected) {
            lv_obj_clear_flag(nav_indicators[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(nav_indicators[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void nav_item_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    int destination = (int)(intptr_t)lv_event_get_user_data(event);
    nav_apply_selection(destination);
    nav_open(destination);
    /* Returning from an application always lands on the Home desktop. */
    if (destination != NAV_HOME) nav_apply_selection(NAV_HOME);
}

static void nav_key_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_LEFT) {
        nav_apply_selection(nav_selected - 1);
    } else if (key == LV_KEY_RIGHT) {
        nav_apply_selection(nav_selected + 1);
    } else if (key == LV_KEY_ENTER) {
        int destination = nav_selected;
        nav_open(destination);
        if (destination != NAV_HOME) nav_apply_selection(NAV_HOME);
    } else if (key == LV_KEY_HOME) {
        nav_apply_selection(NAV_HOME);
    }
}

static lv_obj_t *create_nav_item(lv_obj_t *parent, int index,
                                 const char *symbol, const char *name)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, 190, 90);
    lv_obj_set_style_radius(item, 18, 0);
    lv_obj_set_style_bg_color(item, HOME_CYAN, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(item, HOME_SURFACE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_shadow_width(item, 0, 0);
    lv_obj_set_style_pad_all(item, 0, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(item, nav_item_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)index);

    lv_obj_t *icon = lv_label_create(item);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, HOME_TEXT_MUTED, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label, HOME_TEXT_MUTED, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *indicator = lv_obj_create(item);
    lv_obj_set_size(indicator, 42, 4);
    lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(indicator, HOME_CYAN, 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(indicator, HOME_CYAN, 0);
    lv_obj_set_style_shadow_opa(indicator, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(indicator, 8, 0);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_pad_all(indicator, 0, 0);
    lv_obj_clear_flag(indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    nav_items[index] = item;
    nav_icons[index] = icon;
    nav_labels[index] = label;
    nav_indicators[index] = indicator;
    return item;
}

void ui_dock_create(lv_obj_t *parent)
{
    nav_root = lv_obj_create(parent);
    lv_obj_set_size(nav_root, HOME_CONTENT_W, HOME_DOCK_H);
    lv_obj_set_style_radius(nav_root, HOME_RADIUS, 0);
    lv_obj_set_style_bg_color(nav_root, lv_color_hex(0x101620), 0);
    lv_obj_set_style_bg_opa(nav_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav_root, 1, 0);
    lv_obj_set_style_border_color(nav_root, HOME_BORDER, 0);
    lv_obj_set_style_border_opa(nav_root, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(nav_root, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(nav_root, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(nav_root, 12, 0);
    lv_obj_set_style_shadow_offset_y(nav_root, 4, 0);
    lv_obj_set_style_pad_hor(nav_root, 38, 0);
    lv_obj_set_style_pad_ver(nav_root, 11, 0);
    lv_obj_set_style_pad_column(nav_root, 38, 0);
    lv_obj_clear_flag(nav_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(nav_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_flex_flow(nav_root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_root, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(nav_root, nav_key_event, LV_EVENT_KEY, NULL);

    create_nav_item(nav_root, NAV_HOME, LV_SYMBOL_HOME, "Home");
    create_nav_item(nav_root, NAV_ASSISTANT, LV_SYMBOL_AUDIO, "Assistant");
    create_nav_item(nav_root, NAV_HDMI, LV_SYMBOL_VIDEO, "HDMI");
    create_nav_item(nav_root, NAV_MORE, LV_SYMBOL_LIST, "More");
    nav_apply_selection(NAV_HOME);

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, nav_root);
        lv_group_focus_obj(nav_root);
    }
}
