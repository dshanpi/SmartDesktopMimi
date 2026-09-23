#ifndef WIFI_APP_H
#define WIFI_APP_H

#include "lvgl.h"
#include "../../system/app_manager.h"

extern AppDescriptor app_wifi;

void wifi_app_init(void);
void wifi_app_close(void);

#endif // WIFI_APP_H
