#ifndef APP_H
#define APP_H

#include "lvgl.h"

/* 定义应用 ID */
typedef enum {
    APP_ID_NONE = 0,
    APP_ID_TIME,
    APP_ID_VIDEO,
    APP_ID_WIFI,
    APP_ID_SETTING,
    APP_ID_AI,
    APP_ID_BT,
    APP_ID_OTA,
    APP_ID_USER_APPS,
    APP_ID_HDMI_MCP,
    APP_ID_MAX
} app_id_t;

/* 应用描述符接口 */
typedef struct {
    app_id_t id;
    const char * name;
    // const lv_image_dsc_t * icon; // 图标资源通常在 UI 层管理，这里可选
    void (*init)(void);          // 初始化并显示界面
    void (*close)(void);         // 关闭并销毁界面
} AppDescriptor;

#endif // APP_H
