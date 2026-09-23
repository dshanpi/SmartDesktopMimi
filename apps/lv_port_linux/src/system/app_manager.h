#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "../apps/app.h"

/* 初始化应用管理器 */
void app_manager_init(void);

/* 打开指定应用 */
void app_manager_open(app_id_t id);

/* 返回主界面 */
void app_manager_back_home(void);

/* 显示/隐藏 HDMI 预览用的透明屏幕 */
void app_manager_show_hdmi_preview_screen(void);
void app_manager_hide_hdmi_preview_screen(void);

#endif // APP_MANAGER_H
