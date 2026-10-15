/* Public implementation: never reads a license or initializes a device. */
#include "mimi_device_pairing.h"
int mimi_tuya_pairing_enabled(void) { return 0; }
int mimi_tuya_pairing_init(tuya_iot_client_t *client, tuya_iot_license_t *license,
                          const tuya_iot_config_t *base)
{
    (void)client; (void)license; (void)base;
    return MIMI_PAIRING_DISABLED;
}
