#include "desktop_dock.h"
#include "desktop_all_apps.h"
#include "desktop_home_tokens.h"
#include "ui_fonts.h"
#include "ui_i18n.h"

/* A single drawer handle replaces both former navigation rows. */
static void open_drawer(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED ||
        (code == LV_EVENT_KEY && (lv_event_get_key(e) == LV_KEY_ENTER ||
                                 lv_event_get_key(e) == LV_KEY_UP)) ||
        (code == LV_EVENT_GESTURE && lv_indev_get_act() &&
         lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP))
        ui_all_apps_show();
}

void ui_dock_create(lv_obj_t *parent)
{
    lv_obj_t *handle = lv_button_create(parent);
    lv_obj_set_size(handle, 200, 62);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_color(handle, HOME_SURFACE_RAISED, 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_set_style_shadow_width(handle, 0, 0);
    lv_obj_set_style_pad_all(handle, 0, 0);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(handle, open_drawer, LV_EVENT_ALL, NULL);
    lv_obj_t *grip = lv_obj_create(handle);
    lv_obj_set_size(grip, 44, 4);
    lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(grip, HOME_TEXT_MUTED, 0);
    lv_obj_set_style_border_width(grip, 0, 0);
    lv_obj_set_style_radius(grip, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(grip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(handle);
    lv_label_set_text(label, ui_tr(UI_STR_ALL_APPS));
    lv_obj_set_style_text_font(label, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(label, HOME_TEXT_MUTED, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_group_t *group = lv_group_get_default();
    if (group) { lv_group_add_obj(group, handle); lv_group_focus_obj(handle); }
}
