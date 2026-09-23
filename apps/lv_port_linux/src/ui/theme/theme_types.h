#ifndef UI_THEME_TYPES_H
#define UI_THEME_TYPES_H

#include "lvgl.h"

/*********************
 *      TYPES
 *********************/

/* 1. Primitive Palette (Raw Colors) */
typedef struct {
    lv_color_t white;
    lv_color_t gray_50;
    lv_color_t gray_100;
    lv_color_t gray_200;
    lv_color_t gray_300;
    lv_color_t gray_400;
    lv_color_t gray_500;
    lv_color_t gray_600;
    lv_color_t gray_700;
    lv_color_t gray_800;
    lv_color_t gray_900;
    lv_color_t black;
    
    lv_color_t cyan_500;
    lv_color_t blue_600;
    lv_color_t orange_500;
    lv_color_t purple_600;
} ui_theme_palette_t;

/* 2. Semantic Theme (Functional Tokens) */
typedef struct {
    const char * name;
    
    /* Backgrounds */
    lv_color_t bg_default;
    lv_color_t bg_card;
    lv_color_t bg_inverse;
    lv_color_t bg_desktop;
    
    /* Text */
    lv_color_t text_primary;
    lv_color_t text_secondary;
    lv_color_t text_inverse;
    lv_color_t text_accent;
    
    /* Borders */
    lv_color_t border_default;
    lv_color_t border_focus;
    
    /* Status */
    lv_color_t color_primary;
    lv_color_t color_secondary;
    
    /* Shadows */
    lv_color_t shadow_default;
    
    /* Desktop Specifics */
    lv_color_t desktop_card_bg;
    lv_color_t desktop_dock_bg;
    lv_color_t desktop_text_main;
    lv_color_t desktop_border;
    lv_color_t dock_app_border;

    /* Glassmorphism cards (半透明玻璃卡片配色) */
    lv_color_t glass_bg;
    lv_color_t glass_bg_grad;
    lv_color_t glass_border;
    lv_color_t glass_outline;
    lv_color_t glass_shadow;

    /* Desktop accents */
    lv_color_t desktop_strong_text; /* 时钟数字/日期/温湿度等深色文字 */
    lv_color_t desktop_divider;     /* 卡内分隔线 */

} ui_theme_t;

#endif /* UI_THEME_TYPES_H */
