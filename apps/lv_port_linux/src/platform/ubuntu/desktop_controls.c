/* Ubuntu windowed desktop has no board LCD or fixed ALSA mixer control.
 * Until a host provider is configured, report unsupported; never apply the
 * A133 numid or display ioctl to an unrelated host device. */
#include "../../system/backlight_control.h"
#include "../../system/volume_control.h"
#include <errno.h>

int sys_backlight_set_percent(int percent) { (void)percent; errno=ENOTSUP; return -1; }
int sys_backlight_get_percent(int *out) { (void)out; errno=ENOTSUP; return -1; }
void sys_backlight_apply_saved_setting(void) {}
int sys_volume_set_percent(int percent) { (void)percent; errno=ENOTSUP; return -1; }
int sys_volume_get_percent(int *out) { (void)out; errno=ENOTSUP; return -1; }
void sys_volume_apply_saved_setting(void) {}
