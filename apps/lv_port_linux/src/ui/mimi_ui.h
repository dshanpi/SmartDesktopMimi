#ifndef MIMI_UI_H
#define MIMI_UI_H

#include "theme/theme.h"
#include "ui_i18n.h"

/* Opt-in visual system. Assistant and Free Chat retain their existing theme. */
typedef enum {
    MIMI_BG, MIMI_SURFACE, MIMI_SURFACE_ALT, MIMI_TEXT, MIMI_MUTED,
    MIMI_BORDER, MIMI_CORAL, MIMI_TEAL, MIMI_VIOLET, MIMI_NAVY,
} mimi_color_t;

#define MIMI_HEADER_H 68
#define MIMI_RAIL_W 64
#define MIMI_PAGE_PAD 24
#define MIMI_TOUCH_MIN 48

lv_color_t mimi_ui_color(mimi_color_t color);
void mimi_ui_clear(lv_obj_t *obj);
void mimi_ui_surface(lv_obj_t *obj, int32_t radius);
void mimi_ui_button_style(lv_obj_t *button, bool primary);
lv_obj_t *mimi_ui_text(lv_obj_t *parent, const char *text,
                      const lv_font_t *font, mimi_color_t color);
lv_obj_t *mimi_ui_header(lv_obj_t *parent, const char *title,
                        lv_event_cb_t back_cb);
lv_obj_t *mimi_ui_body(lv_obj_t *parent);
void mimi_ui_keyboard_style(lv_obj_t *keyboard);

#endif
