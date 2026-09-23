#ifndef DESKTOP_CLOCK_H
#define DESKTOP_CLOCK_H

#include "../../lvgl/lvgl.h"

/* 创建 Mimi 首页仪表盘（AI、HDMI、快捷入口、时间与环境状态）。*/
void ui_clock_create(lv_obj_t *parent);

/* Apply a new accent color to the persistent home clock card immediately. */
void ui_clock_apply_theme(void);

/* 传感器温湿度更新。valid=false 显示占位；temp_c 单位 °C，humi_pct 单位 %。*/
void ui_clock_set_sensor(bool valid, int temp_c, int humi_pct);

#endif /* DESKTOP_CLOCK_H */
