#include "backend_runtime.h"

#include "../system/service_ai.h"
#include "../system/service_bt.h"
#include "../system/service_cloud.h"
#include "../system/service_hdmi_preview.h"
#include "../system/service_led.h"
#include "../system/service_ota.h"
#include "../system/service_sensor.h"
#include "../system/service_time.h"
#include "../system/service_video.h"
#include "../system/service_wifi.h"

static bool g_runtime_started;

int backend_runtime_start(const backend_runtime_options_t *options)
{
    if (!options) return -1;
    if (g_runtime_started) return 0;

    /* Preserve the established order: LED owns its device before clients can
     * request scenes, while cloud starts after OTA and sensor listeners. */
    service_led_init();
    service_time_init();
    service_wifi_init();
    service_video_init();
    service_sensor_init();
    service_bt_init();
    service_ai_init();
    service_hdmi_preview_init();
    service_ota_init();
    service_cloud_init();

    service_wifi_set_enabled(options->wifi_enabled);
    service_bt_enable(options->bluetooth_enabled);
    service_hdmi_preview_set_enabled(options->hdmi_enabled);

    g_runtime_started = true;
    return 0;
}

void backend_runtime_tick(void)
{
    if (!g_runtime_started) return;

    /* Preserve the production poll order. Cross-service synchronization is
     * explicit here until those dependencies move behind domain events. */
    service_video_update();
    service_bt_update();
    service_ai_update();
    service_sensor_update();
    service_ota_update();
    service_cloud_set_tuya_license_ready(service_ai_is_tuya_license_ready());
    service_cloud_update();
    service_ip_monitor_poll();
    service_time_update();
}

void backend_runtime_stop(void)
{
    if (!g_runtime_started) return;

    /* Keep the previous shutdown order to avoid changing device behavior in
     * this structural refactor. */
    service_ota_deinit();
    service_cloud_deinit();
    service_hdmi_preview_deinit();
    service_ai_deinit();
    service_video_deinit();
    service_bt_deinit();
    service_wifi_deinit();
    service_sensor_deinit();
    service_time_deinit();
    service_led_deinit();

    g_runtime_started = false;
}
