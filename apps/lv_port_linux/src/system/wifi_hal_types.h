#ifndef WIFI_HAL_TYPES_H
#define WIFI_HAL_TYPES_H

#include <stdbool.h>

typedef enum {
    WIFI_HAL_EVT_SCAN_RESULT = 0,
    WIFI_HAL_EVT_SCAN_DONE,
    WIFI_HAL_EVT_CONNECTED,
    WIFI_HAL_EVT_DISCONNECTED,
    WIFI_HAL_EVT_ERROR
} wifi_hal_event_type_t;

typedef struct {
    wifi_hal_event_type_t type;
    int error_code;
    char ssid[33];
    int rssi_level;   /* 0..4 */
    bool encrypted;
} wifi_hal_event_t;

typedef void (*wifi_hal_event_cb_t)(const wifi_hal_event_t *event, void *user_data);

#endif /* WIFI_HAL_TYPES_H */
