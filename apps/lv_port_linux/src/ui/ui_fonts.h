#ifndef UI_FONTS_H
#define UI_FONTS_H

#include "../../lvgl/lvgl.h"

void ui_fonts_init(void);

const lv_font_t *ui_font_h1(void);
const lv_font_t *ui_font_h2(void);
const lv_font_t *ui_font_h3(void);
const lv_font_t *ui_font_body_lg(void);
const lv_font_t *ui_font_body_md(void);
const lv_font_t *ui_font_clock_meta(void);

#endif /* UI_FONTS_H */
