#ifndef VOLUME_CONTROL_H
#define VOLUME_CONTROL_H

int sys_volume_set_percent(int percent);
int sys_volume_get_percent(int *percent_out);
void sys_volume_apply_saved_setting(void);

#endif /* VOLUME_CONTROL_H */
