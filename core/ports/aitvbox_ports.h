/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 09make.inc (零九智造)
 *
 * Stable platform boundary for product-owned code. Vendor headers, device
 * nodes and SoC conditionals belong in platforms/<id>, never in this file.
 */
#ifndef AITVBOX_PORTS_H
#define AITVBOX_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AITVBOX_PLATFORM_ABI_VERSION 2U

typedef enum {
    AITVBOX_OK = 0,
    AITVBOX_ERROR = -1,
    AITVBOX_UNSUPPORTED = -2,
    AITVBOX_INVALID_ARGUMENT = -3,
    AITVBOX_BUSY = -4,
    AITVBOX_TIMEOUT = -5,
    AITVBOX_PERMISSION_DENIED = -6
} aitvbox_status_t;

typedef uint64_t aitvbox_capability_mask_t;
typedef uint64_t aitvbox_platform_capability_t;

#define AITVBOX_CAP_LOCAL_DISPLAY    (UINT64_C(1) << 0)
#define AITVBOX_CAP_HDMI_CAPTURE     (UINT64_C(1) << 1)
#define AITVBOX_CAP_HDMI_AUDIO       (UINT64_C(1) << 2)
#define AITVBOX_CAP_HARDWARE_H264    (UINT64_C(1) << 3)
#define AITVBOX_CAP_USB_HID_GADGET   (UINT64_C(1) << 4)
#define AITVBOX_CAP_WIFI             (UINT64_C(1) << 5)
#define AITVBOX_CAP_BLUETOOTH_AUDIO  (UINT64_C(1) << 6)
#define AITVBOX_CAP_ENV_SENSOR       (UINT64_C(1) << 7)
#define AITVBOX_CAP_ADDRESSABLE_LED  (UINT64_C(1) << 8)
#define AITVBOX_CAP_AB_UPDATE        (UINT64_C(1) << 9)
#define AITVBOX_CAP_SECURE_IDENTITY  (UINT64_C(1) << 10)

typedef struct {
    uint32_t abi_version;
    const char *platform_id;
    const char *soc;
    const char *provider_version;
    aitvbox_capability_mask_t capabilities;
} aitvbox_platform_descriptor_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t fourcc;
    uint64_t monotonic_ns;
    const void *planes[3];
    size_t plane_sizes[3];
} aitvbox_video_frame_t;

/* ABI V2 keeps the original synchronous pointer frame intact while adding
 * explicit ownership and DMA-BUF metadata for zero-copy capture fan-out. */
typedef enum {
    AITVBOX_VIDEO_MEMORY_CPU = 0,
    AITVBOX_VIDEO_MEMORY_DMABUF = 1
} aitvbox_video_memory_t;

typedef struct {
    const void *data;
    int32_t dmabuf_fd;
    uint32_t offset;
    uint32_t stride;
    size_t size;
} aitvbox_video_plane_v2_t;

typedef struct {
    uint64_t frame_id;
    uint64_t monotonic_ns;
    uint32_t width;
    uint32_t height;
    uint32_t fourcc;
    uint32_t plane_count;
    aitvbox_video_memory_t memory;
    aitvbox_video_plane_v2_t planes[3];
    void *provider_token;
} aitvbox_video_frame_v2_t;

typedef struct {
    aitvbox_status_t (*open)(void *context);
    aitvbox_status_t (*close)(void *context);
    aitvbox_status_t (*start)(void *context);
    aitvbox_status_t (*stop)(void *context);
    aitvbox_status_t (*acquire_frame)(void *context,
                                      aitvbox_video_frame_v2_t *frame,
                                      uint32_t timeout_ms);
    void (*release_frame)(void *context, aitvbox_video_frame_v2_t *frame);
    void *context;
} aitvbox_capture_port_v2_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t bitrate;
    uint32_t fourcc;
} aitvbox_video_encoder_config_t;

typedef struct {
    const void *data;
    size_t size;
    uint64_t monotonic_ns;
    bool key_frame;
} aitvbox_encoded_packet_t;

typedef struct {
    aitvbox_status_t (*open)(void *context,
                             const aitvbox_video_encoder_config_t *config);
    aitvbox_status_t (*close)(void *context);
    aitvbox_status_t (*submit_frame)(void *context,
                                     const aitvbox_video_frame_v2_t *frame);
    aitvbox_status_t (*read_packet)(void *context,
                                    aitvbox_encoded_packet_t *packet,
                                    uint32_t timeout_ms);
    aitvbox_status_t (*request_key_frame)(void *context);
    void *context;
} aitvbox_video_encoder_port_t;

typedef struct {
    aitvbox_status_t (*open)(void *context);
    aitvbox_status_t (*close)(void *context);
    aitvbox_status_t (*start)(void *context);
    aitvbox_status_t (*stop)(void *context);
    aitvbox_status_t (*read_frame)(void *context, aitvbox_video_frame_t *frame,
                                   uint32_t timeout_ms);
    void *context;
} aitvbox_capture_port_t;

typedef struct {
    aitvbox_status_t (*keyboard_report)(void *context,
                                        const uint8_t report[8],
                                        uint32_t hold_ms);
    aitvbox_status_t (*relative_mouse_report)(void *context,
                                              const uint8_t report[4]);
    aitvbox_status_t (*release_all)(void *context);
    void *context;
} aitvbox_input_port_t;

typedef struct {
    aitvbox_status_t (*set_brightness)(void *context, uint8_t percent);
    aitvbox_status_t (*present)(void *context,
                                const aitvbox_video_frame_t *frame);
    void *context;
} aitvbox_display_port_t;

typedef struct {
    aitvbox_status_t (*set_volume)(void *context, uint8_t percent);
    aitvbox_status_t (*start_capture)(void *context);
    aitvbox_status_t (*stop_capture)(void *context);
    void *context;
} aitvbox_audio_port_t;

typedef struct {
    aitvbox_status_t (*connect)(void *context, const char *network_id,
                                const char *credential_ref);
    aitvbox_status_t (*disconnect)(void *context);
    aitvbox_status_t (*status_json)(void *context, char *buffer, size_t size);
    void *context;
} aitvbox_network_port_t;

typedef struct {
    aitvbox_status_t (*set_rgb)(void *context, uint8_t red, uint8_t green,
                                uint8_t blue);
    void *context;
} aitvbox_led_port_t;

typedef struct {
    aitvbox_status_t (*read_json)(void *context, char *buffer, size_t size);
    void *context;
} aitvbox_sensor_port_t;

typedef struct {
    aitvbox_status_t (*write_inactive_slot)(void *context, const char *image);
    aitvbox_status_t (*verify_inactive_slot)(void *context);
    aitvbox_status_t (*activate_inactive_slot)(void *context);
    aitvbox_status_t (*rollback)(void *context);
    void *context;
} aitvbox_update_port_t;

typedef struct {
    aitvbox_status_t (*get_public_id)(void *context, char *buffer, size_t size);
    aitvbox_status_t (*read_secret)(void *context, const char *name,
                                    void *buffer, size_t *size);
    void *context;
} aitvbox_identity_port_t;

typedef struct {
    const aitvbox_platform_descriptor_t *descriptor;
    const aitvbox_capture_port_t *capture;
    const aitvbox_input_port_t *input;
    const aitvbox_display_port_t *display;
    const aitvbox_audio_port_t *audio;
    const aitvbox_network_port_t *network;
    const aitvbox_led_port_t *led;
    const aitvbox_sensor_port_t *sensor;
    const aitvbox_update_port_t *update;
    const aitvbox_identity_port_t *identity;
    const aitvbox_capture_port_v2_t *capture_v2;
    const aitvbox_video_encoder_port_t *video_encoder;
} aitvbox_platform_provider_t;

#endif
