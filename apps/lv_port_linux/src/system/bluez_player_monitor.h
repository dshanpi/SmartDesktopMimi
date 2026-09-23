#ifndef BLUEZ_PLAYER_MONITOR_H
#define BLUEZ_PLAYER_MONITOR_H

#include <stdbool.h>
#include "backend_types.h"

typedef void (*bluez_player_state_cb_t)(bool available,
                                        bt_playback_state_t state,
                                        const char *object_path);

bool bluez_player_monitor_init(bluez_player_state_cb_t callback);
void bluez_player_monitor_update(void);
void bluez_player_monitor_deinit(void);

#endif
