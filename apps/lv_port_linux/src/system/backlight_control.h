#ifndef BACKLIGHT_CONTROL_H
#define BACKLIGHT_CONTROL_H

int sys_backlight_set_percent(int percent);
int sys_backlight_get_percent(int *percent_out);
void sys_backlight_apply_saved_setting(void);

#endif /* BACKLIGHT_CONTROL_H */
