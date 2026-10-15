/* SmartDesktopMimi product pairing ABI v1. No credentials in this interface. */
#ifndef MIMI_DEVICE_PAIRING_H
#define MIMI_DEVICE_PAIRING_H
#include "tuya_iot.h"
#define MIMI_PAIRING_ABI 1
#define MIMI_PAIRING_DISABLED (-6801)
#define MIMI_PAIRING_INVALID (-6802)
int mimi_tuya_pairing_enabled(void);
int mimi_tuya_pairing_init(tuya_iot_client_t *client, tuya_iot_license_t *license,
                          const tuya_iot_config_t *base);
#endif
