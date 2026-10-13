#ifndef DESKTOP_HOME_TOKENS_H
#define DESKTOP_HOME_TOKENS_H

#include "../../lvgl/lvgl.h"
#include "theme/theme.h"
#include "mimi_ui.h"

/* Mimi desktop palette.  The desktop chrome follows the selected appearance;
 * only the HDMI viewport remains dark because it represents video content. */
#define HOME_BG                 mimi_ui_color(MIMI_BG)
#define HOME_SURFACE            mimi_ui_color(MIMI_SURFACE)
#define HOME_SURFACE_RAISED     mimi_ui_color(MIMI_SURFACE_ALT)
#define HOME_SURFACE_PRESSED    mimi_ui_color(MIMI_BORDER)
#define HOME_BORDER             mimi_ui_color(MIMI_BORDER)
#define HOME_TEXT               mimi_ui_color(MIMI_TEXT)
#define HOME_TEXT_MUTED         mimi_ui_color(MIMI_MUTED)
#define HOME_TEXT_INVERSE       UI_TEXT_INVERSE
#define HOME_TEXT_INVERSE_MUTED lv_color_mix(UI_TEXT_INVERSE, UI_BG_INVERSE, 180)
/* The HDMI standby viewport stays dark in both appearances. */
#define HOME_DARK_TEXT          lv_color_hex(0xF7F8FC)
#define HOME_DARK_TEXT_MUTED    lv_color_hex(0xB8C1CC)
#define HOME_CYAN               mimi_ui_color(MIMI_CORAL)
#define HOME_BLUE               mimi_ui_color(MIMI_VIOLET)
#define HOME_VIOLET             lv_color_mix(mimi_ui_color(MIMI_VIOLET), mimi_ui_color(MIMI_CORAL), 120)
#define HOME_CORAL              mimi_ui_color(MIMI_CORAL)
#define HOME_GREEN              mimi_ui_color(MIMI_TEAL)

#define HOME_SCREEN_W           1024
#define HOME_SCREEN_H           768
#define HOME_CONTENT_X          56
#define HOME_DASHBOARD_Y        90
#define HOME_DOCK_Y             688
#define HOME_CONTENT_W          912
#define HOME_STATUS_H           68
#define HOME_DASHBOARD_H        576
#define HOME_DOCK_H             62
#define HOME_GAP                16
#define HOME_RADIUS             22
#define HOME_RADIUS_SM          16

#endif /* DESKTOP_HOME_TOKENS_H */
