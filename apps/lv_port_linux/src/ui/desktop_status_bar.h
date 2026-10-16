#ifndef DESKTOP_STATUS_BAR_H
#define DESKTOP_STATUS_BAR_H

#include "../../lvgl/lvgl.h"
#include "../system/backend_types.h"

/* 创建 Mimi 桌面顶栏（品牌、WiFi/云状态、控制中心与 HDMI 开关）。*/
void ui_statusbar_create(lv_obj_t *parent);
/* Read-only projection of the existing system OTA event. */
void ui_statusbar_ota_notice(const ota_status_t *status);

#endif /* DESKTOP_STATUS_BAR_H */
