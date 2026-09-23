/**
 * @file service_led.h
 * @brief Shared LED strip ownership + priority arbitration (Backend only).
 *
 * The strip is a public resource: only this service writes the device via
 * led_strip_hal. Other services call request/release or the on_* helpers.
 */
#ifndef SERVICE_LED_H
#define SERVICE_LED_H

#include "backend_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_CLIENT_SYSTEM = 0,  /* boot / idle / global error */
    LED_CLIENT_NETWORK,     /* wifi */
    LED_CLIENT_BT,
    LED_CLIENT_AI,
    LED_CLIENT_OTA,
    LED_CLIENT_NOTIFY,
    LED_CLIENT_SETTINGS,
    LED_CLIENT_MAX
} led_client_t;

typedef enum {
    LED_SCENE_OFF = 0,
    LED_SCENE_BOOT,
    LED_SCENE_IDLE,             /* dim breath */
    LED_SCENE_WIFI_CONNECTING,  /* blue breath */
    LED_SCENE_WIFI_OK,          /* green flash then auto-release */
    LED_SCENE_WIFI_FAIL,        /* brief amber flash */
    LED_SCENE_AI_CONNECTING,
    LED_SCENE_AI_LISTENING,
    LED_SCENE_AI_THINKING,
    LED_SCENE_AI_SPEAKING,
    LED_SCENE_AI_ERROR,
    LED_SCENE_OTA_PROGRESS,     /* param = 0–100 fill */
    LED_SCENE_OTA_APPLYING,
    LED_SCENE_ERROR,            /* red blink */
    LED_SCENE_NOTIFY,           /* generic pulse; param unused */
    LED_SCENE_SWIPE_LEFT,       /* UI dock: comet left */
    LED_SCENE_SWIPE_RIGHT,      /* UI dock: comet right */
} led_scene_t;

void service_led_init(void);
void service_led_deinit(void);

/**
 * Claim the strip for @p who with a scene. Higher priority preempts lower.
 * @param param scene-specific (e.g. OTA progress 0–100)
 * @param ttl_ms auto-release after this many ms; 0 = hold until release/replace
 */
void service_led_request(led_client_t who, led_scene_t scene, int param, int ttl_ms);

/** Drop this client's claim; next-highest active client is restored. */
void service_led_release(led_client_t who);

void service_led_set_enabled(bool on);
bool service_led_get_enabled(void);

void service_led_set_brightness(int percent);
int service_led_get_brightness(void);

/* Convenience hooks — map domain status → request/release. */
void service_led_on_ai_status(const ai_status_t *st);
void service_led_on_ota_status(const ota_status_t *st);
void service_led_on_wifi_connecting(void);
void service_led_on_wifi_connected(void);
void service_led_on_wifi_disconnected(void);
void service_led_on_wifi_error(void);

/** UI dock swipe: dir < 0 left, dir > 0 right. Short TTL, low priority. */
void service_led_on_ui_swipe(int dir);

/** Handle middleware LED command from UI. */
void service_led_handle_command(const led_cmd_t *cmd);

#endif /* SERVICE_LED_H */
