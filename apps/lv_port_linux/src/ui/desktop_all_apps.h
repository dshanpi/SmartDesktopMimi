#ifndef DESKTOP_ALL_APPS_H
#define DESKTOP_ALL_APPS_H

#include "../../lvgl/lvgl.h"

/* Full-screen application library shown from the compact desktop Dock. */
void ui_all_apps_create(lv_obj_t *parent);
void ui_all_apps_show(void);
void ui_all_apps_hide(void);

#endif /* DESKTOP_ALL_APPS_H */
