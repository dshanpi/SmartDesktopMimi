#ifndef TPLAYER_WRAPPER_H
#define TPLAYER_WRAPPER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TPLAYER_STATUS_STOPPED,
    TPLAYER_STATUS_PLAYING,
    TPLAYER_STATUS_PAUSED,
    TPLAYER_STATUS_PREPARING,
    TPLAYER_STATUS_ERROR
} tplayer_status_t;

/**
 * @brief Initialize the TPlayer wrapper
 * @return 0 on success, -1 on failure
 */
int tplayer_wrapper_init(void);

/**
 * @brief Destroy the TPlayer wrapper and release resources
 */
void tplayer_wrapper_destroy(void);

/**
 * @brief Set the display rectangle for video output
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @return 0 on success, -1 on failure
 */
int tplayer_wrapper_set_display_rect(int x, int y, int w, int h);

/**
 * @brief Play a media file from URL/Path
 * @param url Path to the media file
 * @return 0 on success, -1 on failure
 */
int tplayer_wrapper_play_url(const char *url);

/**
 * @brief Pause playback
 * @return 0 on success, -1 on failure
 */
int tplayer_wrapper_pause(void);

/**
 * @brief Resume playback
 * @return 0 on success, -1 on failure
 */
int tplayer_wrapper_resume(void);

/**
 * @brief Stop playback
 * @return 0 on success, -1 on failure
 */
int tplayer_wrapper_stop(void);

/**
 * @brief Seek to a specific position
 * @param seconds Position in seconds
 * @return 0 on success, -1 on failure
 */
int tplayer_wrapper_seek(int seconds);

/**
 * @brief Get current playback duration
 * @return Duration in seconds, or 0 if not available
 */
int tplayer_wrapper_get_duration(void);

/**
 * @brief Get current playback position
 * @return Position in seconds, or 0 if not available
 */
int tplayer_wrapper_get_position(void);

/**
 * @brief Get current player status
 * @return Current status
 */
tplayer_status_t tplayer_wrapper_get_status(void);

/**
 * @brief Set loop playback mode
 * @param loop true to enable loop, false to disable
 */
void tplayer_wrapper_set_loop(bool loop);

/**
 * @brief Check if player is playing
 * @return true if playing, false otherwise
 */
bool tplayer_wrapper_is_playing(void);

/**
 * @brief Last human-readable error (UTF-8), empty if none.
 * Set when play_url rejects over-spec content or other failures.
 */
const char *tplayer_wrapper_get_last_error(void);
void tplayer_wrapper_clear_error(void);

/**
 * @brief Best-effort probe of container video track (width/height/fps).
 * @return 0 on success, -1 if unreadable / not found.
 */
int tplayer_wrapper_probe_media(const char *path, int *out_w, int *out_h, int *out_fps);

/**
 * @brief Dimensions from last successful Prepare (player MediaInfo), else 0.
 */
void tplayer_wrapper_get_prepared_size(int *out_w, int *out_h);

/**
 * @brief Probe file against product decode budget (max 4K@30).
 * @return true if playable within budget; false if unsupported.
 * On false, writes a short Chinese reason into @p reason when provided.
 * If probe fails, returns true (allow play attempt).
 */
bool tplayer_wrapper_is_supported(const char *path, char *reason, size_t reason_len);

#ifdef __cplusplus
}
#endif

#endif // TPLAYER_WRAPPER_H
