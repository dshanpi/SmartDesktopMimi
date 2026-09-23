#ifndef UI_H
#define UI_H

#include "../../lvgl/lvgl.h"
#include "../system/backend_types.h"

void ui_init(void);

/* 处理后端上报的 HDMI 预览状态：同步顶栏开关、ack 后持久化、显示/隐藏 HDMI 透明屏。 */
void hdmi_ui_handle_status(const hdmi_preview_status_t *status);

#endif
