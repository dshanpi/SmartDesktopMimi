#ifndef OTA_APP_H
#define OTA_APP_H

#include "../app.h"

extern AppDescriptor app_ota;

void ota_app_init(void);
void ota_app_close(void);

#endif /* OTA_APP_H */
