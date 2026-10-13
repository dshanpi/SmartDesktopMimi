#include "mimi_ui.h"

lv_color_t mimi_ui_color(mimi_color_t color)
{
    static const uint32_t light[] = {
        0xEEF1EF, 0xFAFBF8, 0xE4E9E6, 0x17242C, 0x6D797D,
        0xCDD5D1, 0xE9655B, 0x3F9E91, 0x626DAA, 0x172B4D,
    };
    static const uint32_t dark[] = {
        0x10191F, 0x17242B, 0x213139, 0xF0F3EF, 0x9BA9AA,
        0x34454C, 0xFA796E, 0x5FC4B4, 0x8D97D8, 0x273552,
    };
    if ((unsigned)color >= sizeof(light) / sizeof(light[0])) color = MIMI_TEXT;
    return lv_color_hex(ui_theme_is_dark() ? dark[color] : light[color]);
}

void mimi_ui_clear(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void mimi_ui_surface(lv_obj_t *obj, int32_t radius)
{
    mimi_ui_clear(obj);
    lv_obj_set_style_bg_color(obj, mimi_ui_color(MIMI_SURFACE), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, mimi_ui_color(MIMI_BORDER), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_text_color(obj, mimi_ui_color(MIMI_TEXT), 0);
}

void mimi_ui_button_style(lv_obj_t *button, bool primary)
{
    mimi_ui_surface(button, 9);
    lv_obj_set_style_min_height(button, MIMI_TOUCH_MIN, 0);
    lv_obj_set_style_pad_hor(button, 16, 0);
    lv_obj_set_style_pad_ver(button, 8, 0);
    lv_obj_set_style_bg_color(button,
        mimi_ui_color(primary ? MIMI_CORAL : MIMI_SURFACE), 0);
    lv_obj_set_style_text_color(button,
        primary ? lv_color_white() : mimi_ui_color(MIMI_TEXT), 0);
    lv_obj_set_style_bg_color(button, mimi_ui_color(MIMI_SURFACE_ALT), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, mimi_ui_color(MIMI_TEXT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, mimi_ui_color(MIMI_SURFACE_ALT), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, mimi_ui_color(MIMI_MUTED), LV_STATE_DISABLED);
    lv_obj_set_style_outline_color(button, mimi_ui_color(MIMI_CORAL), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUS_KEY);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

lv_obj_t *mimi_ui_text(lv_obj_t *parent, const char *text,
                      const lv_font_t *font, mimi_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, mimi_ui_color(color), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

lv_obj_t *mimi_ui_header(lv_obj_t *parent, const char *title,
                        lv_event_cb_t back_cb)
{
    lv_obj_t *bar = lv_obj_create(parent);
    mimi_ui_surface(bar, 0);
    lv_obj_set_size(bar, lv_pct(100), MIMI_HEADER_H);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t *back = lv_button_create(bar);
    mimi_ui_button_style(back, false);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_size(back, 112, 52);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 12, 0);
    if (back_cb) lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = mimi_ui_text(back, LV_SYMBOL_LEFT,
                                   &lv_font_montserrat_20, MIMI_MUTED);
    lv_obj_align(arrow, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *caption = mimi_ui_text(back, ui_i18n_is_zh() ? "返回" : "Back",
                                     ui_font_body_lg(), MIMI_MUTED);
    lv_obj_align(caption, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t *label = mimi_ui_text(bar, title, ui_font_h2(), MIMI_TEXT);
    lv_obj_set_width(label, lv_pct(65));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return bar;
}

lv_obj_t *mimi_ui_body(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, mimi_ui_color(MIMI_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    int32_t width = lv_display_get_horizontal_resolution(NULL);
    int32_t height = lv_display_get_vertical_resolution(NULL);
    lv_obj_t *body = lv_obj_create(parent);
    mimi_ui_clear(body);
    int32_t content_width = LV_MAX(1, width - MIMI_RAIL_W - 2 * MIMI_PAGE_PAD);
    lv_obj_set_size(body, content_width,
                         LV_MAX(1, height - MIMI_HEADER_H - 2 * MIMI_PAGE_PAD));
    lv_obj_set_pos(body, (width - content_width) / 2, MIMI_HEADER_H + MIMI_PAGE_PAD);
    return body;
}

void mimi_ui_keyboard_style(lv_obj_t *keyboard)
{
    mimi_ui_surface(keyboard, 12);
    lv_obj_set_style_bg_color(keyboard, mimi_ui_color(MIMI_SURFACE_ALT), 0);
    lv_obj_set_style_pad_all(keyboard, 8, 0);
    lv_obj_set_style_pad_row(keyboard, 8, 0);
    lv_obj_set_style_pad_column(keyboard, 5, 0);
    /* Keyboard symbols (backspace/enter) require the built-in symbol font. */
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, mimi_ui_color(MIMI_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, mimi_ui_color(MIMI_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, mimi_ui_color(MIMI_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 6, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, mimi_ui_color(MIMI_CORAL), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);
}
