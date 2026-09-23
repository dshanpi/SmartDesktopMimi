#ifndef WIFI_HAL_H
#define WIFI_HAL_H

#include "wifi_hal_types.h"

typedef struct {
    int (*init)(wifi_hal_event_cb_t cb, void *user_data);
    void (*deinit)(void);
    int (*scan)(void);
    int (*connect)(const char *ssid, const char *password);
    int (*disconnect)(void);
} wifi_driver_ops_t;

int wifi_hal_init(wifi_hal_event_cb_t cb, void *user_data);
void wifi_hal_deinit(void);
int wifi_hal_scan(void);
int wifi_hal_connect(const char *ssid, const char *password);
int wifi_hal_disconnect(void);

#endif /* WIFI_HAL_H */
