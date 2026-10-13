/* The product lock screen was cancelled. Preserve callers without starting
 * idle timers, taking input focus, loading cat assets or spawning a renderer. */
#include "lockscreen.h"

void ui_lockscreen_init(lv_obj_t *home) { (void)home; }
void ui_lockscreen_set_home(lv_obj_t *home) { (void)home; }
bool ui_lockscreen_show(void) { return false; }
bool ui_lockscreen_is_active(void) { return false; }
void ui_lockscreen_wake(void) { }
bool ui_lockscreen_handle_key(lv_indev_t *indev) { (void)indev; return false; }
