#ifndef BT_PCM_PLAYER_H
#define BT_PCM_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

/* Explicit playback path for the decoded S16_LE PCM delivered by libbtmg. */
bool bt_pcm_player_start(void);
void bt_pcm_player_stop(void);
void bt_pcm_player_set_active(bool active);
void bt_pcm_player_push(uint16_t channels, uint16_t sampling,
                        const uint8_t *data, uint32_t len);

#endif /* BT_PCM_PLAYER_H */
