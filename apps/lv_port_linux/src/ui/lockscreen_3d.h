#ifndef UI_LOCKSCREEN_3D_H
#define UI_LOCKSCREEN_3D_H
#include "lvgl.h"
/* Parent owns the image; stop before deleting that image. */
void ui_lockscreen_3d_start(lv_obj_t *parent, lv_obj_t *fallback);
void ui_lockscreen_3d_stop(void);
#endif
