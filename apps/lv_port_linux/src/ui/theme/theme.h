#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"
#include "theme_types.h"
#include "../ui_fonts.h"

/*****************************************************************************
 *                                 GLOBAL STATE
 *****************************************************************************/
extern const ui_theme_t * current_theme;

/* 0 下午靛蓝 · 1 上午天蓝 · 2 傍晚暖珊瑚 · 3 夜晚暮紫 · 4 清晨柔玫瑰 */
#define UI_THEME_GRADIENT_PRESET_COUNT 5U

/*****************************************************************************
 *                                 SEMANTICS
 *                        (Dynamic Macros)
 *****************************************************************************/

/* --- Backgrounds --- */
#define UI_BG_DEFAULT           (current_theme->bg_default)
#define UI_BG_CARD              (current_theme->bg_card)
#define UI_BG_INVERSE           (current_theme->bg_inverse)
#define UI_BG_DESKTOP           (current_theme->bg_desktop)

/* --- Text Colors --- */
#define UI_TEXT_PRIMARY         (current_theme->text_primary)
#define UI_TEXT_SECONDARY       (current_theme->text_secondary)
#define UI_TEXT_INVERSE         (current_theme->text_inverse)
#define UI_TEXT_ACCENT          (current_theme->text_accent)

/* --- Borders & Dividers --- */
#define UI_BORDER_DEFAULT       (current_theme->border_default)
#define UI_BORDER_FOCUS         (current_theme->border_focus)

/* --- Status / Feedback --- */
#define UI_COLOR_PRIMARY        (current_theme->color_primary)
#define UI_COLOR_SECONDARY      (current_theme->color_secondary)

/* --- Shadows --- */
#define UI_SHADOW_DEFAULT       (current_theme->shadow_default)

/* --- Desktop Specifics --- */
#define UI_DESKTOP_CARD_BG      (current_theme->desktop_card_bg)
#define UI_DESKTOP_DOCK_BG      (current_theme->desktop_dock_bg)
#define UI_DESKTOP_TEXT_MAIN    (current_theme->desktop_text_main)
#define UI_DESKTOP_BORDER       (current_theme->desktop_border)
#define UI_DOCK_APP_BORDER      (current_theme->dock_app_border)

/* --- Glassmorphism --- */
#define UI_GLASS_BG             (current_theme->glass_bg)
#define UI_GLASS_BG_GRAD        (current_theme->glass_bg_grad)
#define UI_GLASS_BORDER         (current_theme->glass_border)
#define UI_GLASS_OUTLINE        (current_theme->glass_outline)
#define UI_GLASS_SHADOW         (current_theme->glass_shadow)

/* --- Desktop Accents --- */
#define UI_DESKTOP_STRONG_TEXT  (current_theme->desktop_strong_text)
#define UI_DESKTOP_DIVIDER      (current_theme->desktop_divider)

/*****************************************************************************
 *                                 TYPOGRAPHY
 *****************************************************************************/

/* Declare Real Fonts */
LV_FONT_DECLARE(font_inter_semibold_32)
LV_FONT_DECLARE(font_inter_medium_24)
LV_FONT_DECLARE(font_inter_medium_22)
LV_FONT_DECLARE(font_inter_regular_18)
LV_FONT_DECLARE(font_inter_regular_16)

/* Semantic Text Styles */
#define UI_TEXT_H1              ui_font_h1()
#define UI_TEXT_H2              ui_font_h2()
#define UI_TEXT_H3              ui_font_h3()
#define UI_TEXT_BODY_LG         ui_font_body_lg()
#define UI_TEXT_BODY_MD         ui_font_body_md()
#define UI_TEXT_CAPTION         ui_font_body_md()

/*****************************************************************************
 *                                 LAYOUT
 *****************************************************************************/

/* Spacing Scale */
#define UI_SPACE_XS             8
#define UI_SPACE_SM             12
#define UI_SPACE_MD             16
#define UI_SPACE_LG             24
#define UI_SPACE_XL             32

/* Radius Scale */
#define UI_RADIUS_SM            4
#define UI_RADIUS_MD            8
#define UI_RADIUS_LG            16
#define UI_RADIUS_XL            24
#define UI_RADIUS_CIRCLE        LV_RADIUS_CIRCLE

/* Full-screen app navigation */
#define UI_APP_HEADER_H         64
#define UI_APP_HEADER_H_COMPACT 54
#define UI_APP_HEADER_PAD        UI_SPACE_SM
#define UI_APP_HEADER_PAD_COMPACT UI_SPACE_XS
#define UI_APP_NAV_SIDE          60
#define UI_APP_NAV_SIDE_COMPACT  52

/*****************************************************************************
 *                          COMPONENT SPECIFIC
 *****************************************************************************/

/* Setting App Specifics */
#define UI_SETTING_CARD_WIDTH   896
#define UI_SETTING_CARD_MIN_H   120
#define UI_SETTING_SLIDER_W     500
#define UI_SETTINGS_SLIDER_KNOB_SIZE     28
#define UI_SETTING_BACK_BTN_SIZE 80

/*****************************************************************************
 *                                 FUNCTIONS
 *****************************************************************************/

void ui_theme_init(void);
void ui_theme_set(const char * theme_name); // Switch theme by name
void ui_theme_set_color_primary(lv_color_t color); // Set primary color dynamically
void ui_theme_set_gradient_preset(uint32_t preset_id);
uint32_t ui_theme_get_gradient_preset_id(void);
const lv_grad_dsc_t *ui_theme_get_gradient(uint32_t preset_id);
const lv_grad_dsc_t *ui_theme_get_active_gradient(void);
lv_color_t ui_theme_get_gradient_color(uint32_t preset_id, uint32_t stop_id);
lv_color_t ui_theme_primary_surface_bottom(lv_color_t primary);
lv_color_t ui_theme_primary_surface_border(lv_color_t primary);
lv_color_t ui_theme_primary_surface_shadow(lv_color_t primary);
lv_color_t ui_theme_primary_surface_divider(lv_color_t primary);
lv_style_t* ui_theme_get_card_style(void);
lv_style_t* ui_theme_get_slider_main_style(void);
lv_style_t* ui_theme_get_slider_indicator_style(void);
lv_style_t* ui_theme_get_slider_knob_style(void);
lv_style_t* ui_theme_get_text_accent_style(void); // New shared style for accent text
lv_style_t* ui_theme_get_desktop_bg_style(void); // New shared style for desktop background
lv_style_t* ui_theme_get_glass_card_style(void); // Shared style for glass cards (colors only)

/* Time-of-day accent: 5 periods → gradient preset. See theme.c for hour map. */
uint32_t ui_theme_preset_for_hour(int hour);
uint32_t ui_theme_preset_for_now(void);
/* If follow-time is on, apply the period preset when it changes (or force). */
bool ui_theme_sync_time_of_day(bool force);
void ui_theme_set_follow_time(bool enable);
bool ui_theme_get_follow_time(void);

#endif /* UI_THEME_H */
