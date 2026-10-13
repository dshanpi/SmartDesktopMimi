#ifndef UI_LOCKSCREEN_H
#define UI_LOCKSCREEN_H

#include "../../lvgl/lvgl.h"

/* A touch-to-wake screen, not a password/security boundary. Only Home idles. */
#define UI_LOCKSCREEN_IDLE_MS 120000U
void ui_lockscreen_init(lv_obj_t *home);
void ui_lockscreen_set_home(lv_obj_t *home);
bool ui_lockscreen_show(void);
bool ui_lockscreen_is_active(void);
void ui_lockscreen_wake(void);
/* Consume remote-key wake presses before product navigation handles them. */
bool ui_lockscreen_handle_key(lv_indev_t *indev);

#endif
