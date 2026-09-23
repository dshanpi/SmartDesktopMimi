#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "../../lvgl/lvgl.h"
#include "../middleware/middleware.h"

/* IPC 消息回调 */
void wifi_status_callback(const mw_msg_t *msg);
void wifi_runtime_callback(const mw_msg_t *msg);
void sensor_status_callback(const mw_msg_t *msg);
void network_info_callback(const mw_msg_t *msg);
void hdmi_preview_status_callback(const mw_msg_t *msg);
void cloud_status_callback(const mw_msg_t *msg);
void cloud_ota_status_callback(const mw_msg_t *msg);

/* 时钟定时器回调 */

/* WiFi 图标刷新 */
void refresh_wifi_icon_visual(void);

/* 工具函数 */
void apply_glass_card_style(lv_obj_t *obj, lv_coord_t radius, lv_opa_t bg_opa, lv_coord_t shadow_w);
void apply_debug_outline(lv_obj_t *obj, lv_color_t color);

#endif
