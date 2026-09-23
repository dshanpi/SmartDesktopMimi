#ifndef DESKTOP_HOME_TOKENS_H
#define DESKTOP_HOME_TOKENS_H

#include "../../lvgl/lvgl.h"

/* Mimi desktop palette. Kept separate from the app theme so the desktop can
 * stay low-glare while existing full-screen applications retain their proven
 * light and dark theme behaviour. */
#define HOME_BG                 lv_color_hex(0x0B0F17)
#define HOME_SURFACE            lv_color_hex(0x151B27)
#define HOME_SURFACE_RAISED     lv_color_hex(0x1B2332)
#define HOME_SURFACE_PRESSED    lv_color_hex(0x222D40)
#define HOME_BORDER             lv_color_hex(0x2B3548)
#define HOME_TEXT               lv_color_hex(0xF4F7FB)
#define HOME_TEXT_MUTED         lv_color_hex(0x9CA9BC)
#define HOME_CYAN               lv_color_hex(0x55D6E8)
#define HOME_BLUE               lv_color_hex(0x4C7DFF)
#define HOME_VIOLET             lv_color_hex(0xA68BFF)
#define HOME_CORAL              lv_color_hex(0xFF8A6B)
#define HOME_GREEN              lv_color_hex(0x63D8B0)

#define HOME_SCREEN_W           1024
#define HOME_SCREEN_H           768
#define HOME_CONTENT_W          976
#define HOME_STATUS_H           64
#define HOME_DASHBOARD_H        550
#define HOME_DOCK_H             114
#define HOME_GAP                10
#define HOME_RADIUS             22
#define HOME_RADIUS_SM          16

#endif /* DESKTOP_HOME_TOKENS_H */
