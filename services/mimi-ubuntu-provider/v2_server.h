#ifndef MIMI_UBUNTU_V2_SERVER_H
#define MIMI_UBUNTU_V2_SERVER_H
/* Start only while holding the V1 private directory singleton lock. */
int ubuntu_v2_start(const char *v1_path);
int ubuntu_v2_step(void);
void ubuntu_v2_stop(void);
#endif
