#ifndef SERVICE_BT_H
#define SERVICE_BT_H

#include <stdbool.h>

void service_bt_init(void);
void service_bt_deinit(void);
void service_bt_enable(bool enable);
void service_bt_disconnect_device(const char *bd_addr);
void service_bt_set_volume(int vol);
void service_bt_set_adapter_name(const char *name);
void service_bt_avrcp_play(void);
void service_bt_avrcp_pause(void);
void service_bt_avrcp_next(void);
void service_bt_avrcp_prev(void);
void service_bt_send_runtime_status(void);
void service_bt_update(void);
bool service_bt_is_a2dp_connected(void);
bool service_bt_is_audio_streaming(void);
bool service_bt_is_playing(void);

#endif /* SERVICE_BT_H */
