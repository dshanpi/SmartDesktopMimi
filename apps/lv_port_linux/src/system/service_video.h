#ifndef SERVICE_VIDEO_H
#define SERVICE_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the video service
 */
void service_video_init(void);

/**
 * @brief Update the video service (check status, publish updates)
 */
void service_video_update(void);

/**
 * @brief Deinitialize the video service
 */
void service_video_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // SERVICE_VIDEO_H
