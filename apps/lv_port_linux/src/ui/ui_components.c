#include "ui_components.h"
#include "theme/theme.h"
#include <stdbool.h>

LV_IMG_DECLARE(arrow_back_ios_28dp_1F1F1F)

/* 按屏幕分辨率判断是否进入 compact 模式（与 setting/bt 原有判定一致）。
 * allow_compact=false 时恒为 false，保持固定标准尺寸。*/
static bool header_compute_compact(bool allow_compact)
{
    if (!allow_compact) return false;
    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int32_t h = lv_display_get_vertical_resolution(NULL);
    if (w <= 0) w = 1024;
    if (h <= 0) h = 768;
    return (w < 900 || h < 620);
}

lv_coord_t ui_app_header_height(bool allow_compact)
{
    return header_compute_compact(allow_compact) ? UI_APP_HEADER_H_COMPACT : UI_APP_HEADER_H;
}

lv_obj_t * ui_create_app_header(lv_obj_t *parent,
                                const char *title,
                                lv_event_cb_t back_cb,
                                bool allow_compact,
                                const lv_font_t *title_font)
{
    bool compact = header_compute_compact(allow_compact);
    lv_coord_t header_h = compact ? UI_APP_HEADER_H_COMPACT : UI_APP_HEADER_H;
    lv_coord_t nav_side = compact ? UI_APP_NAV_SIDE_COMPACT : UI_APP_NAV_SIDE;
    lv_coord_t pad = compact ? UI_APP_HEADER_PAD_COMPACT : UI_APP_HEADER_PAD;

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, lv_pct(100), header_h);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, UI_DESKTOP_BORDER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, pad, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_button_create(bar);
    lv_obj_set_size(back_btn, nav_side, header_h);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    if (back_cb) {
        lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *back_icon = lv_image_create(back_btn);
    lv_image_set_src(back_icon, &arrow_back_ios_28dp_1F1F1F);
    lv_obj_clear_flag(back_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(back_icon);

    lv_obj_t *ttl = lv_label_create(bar);
    lv_label_set_text(ttl, title ? title : "");
    lv_obj_set_size(ttl, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ttl, 1);
    lv_label_set_long_mode(ttl, LV_LABEL_LONG_DOT);
    const lv_font_t *font = title_font ? title_font : (compact ? ui_font_h3() : ui_font_h2());
    lv_obj_set_style_text_font(ttl, font, 0);
    lv_obj_set_style_text_color(ttl, UI_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(ttl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, nav_side, header_h);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    return bar;
}
