#include "wifi_hal.h"

#include "../platform/wifi/aw_wifimg_driver.h"
#include <stddef.h>

static const wifi_driver_ops_t *g_ops = NULL;

static const wifi_driver_ops_t *wifi_hal_select_driver(void)
{
    return aw_wifimg_driver_get_ops();
}

int wifi_hal_init(wifi_hal_event_cb_t cb, void *user_data)
{
    g_ops = wifi_hal_select_driver();
    if (!g_ops || !g_ops->init) return -1;
    return g_ops->init(cb, user_data);
}

void wifi_hal_deinit(void)
{
    if (g_ops && g_ops->deinit) g_ops->deinit();
    g_ops = NULL;
}

int wifi_hal_scan(void)
{
    if (!g_ops || !g_ops->scan) return -1;
    return g_ops->scan();
}

int wifi_hal_connect(const char *ssid, const char *password)
{
    if (!g_ops || !g_ops->connect) return -1;
    return g_ops->connect(ssid, password);
}

int wifi_hal_disconnect(void)
{
    if (!g_ops || !g_ops->disconnect) return -1;
    return g_ops->disconnect();
}
