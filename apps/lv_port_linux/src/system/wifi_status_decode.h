#ifndef MIMI_WIFI_STATUS_DECODE_H
#define MIMI_WIFI_STATUS_DECODE_H
#include <stddef.h>
#include <string.h>
#include "backend_types.h"

/* V1 retains its native ABI. Validate byte representations before typed access,
 * and copy to aligned storage. Rejection leaves the destination unchanged. */
_Static_assert(sizeof(bool) == 1, "V1 WiFi ABI requires one-byte bool");
static inline int wifi_ap_decode(const void *data, size_t size, wifi_ap_info_t *out)
{
    if (!data || !out || size != sizeof(*out)) return 0;
    const unsigned char *b = data;
    if (!b[offsetof(wifi_ap_info_t, ssid)] ||
        !memchr(b + offsetof(wifi_ap_info_t, ssid), 0, sizeof(out->ssid)) ||
        b[offsetof(wifi_ap_info_t, encrypted)] > 1 ||
        b[offsetof(wifi_ap_info_t, connected)] > 1) return 0;
    memcpy(out, data, sizeof(*out));
    return 1;
}
static inline int wifi_runtime_decode(const void *data, size_t size, wifi_runtime_status_t *out)
{
    if (!data || !out || size != sizeof(*out)) return 0;
    const unsigned char *b = data;
    if (!memchr(b + offsetof(wifi_runtime_status_t, connected_ssid), 0,
                sizeof(out->connected_ssid)) ||
        b[offsetof(wifi_runtime_status_t, enabled)] > 1 ||
        b[offsetof(wifi_runtime_status_t, scanning)] > 1 ||
        b[offsetof(wifi_runtime_status_t, connected)] > 1) return 0;
    memcpy(out, data, sizeof(*out));
    return 1;
}
#endif
