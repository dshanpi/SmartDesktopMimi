/**
 * @file number_flow_c.h
 * @brief NumberFlow C 接口封装
 */
#pragma once
#include "../../lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 NumberFlow 控件
 * @param parent 父对象
 * @return NumberFlow 句柄
 */
void* number_flow_create(void* parent);

/**
 * @brief 初始化 NumberFlow
 * @param handle NumberFlow 句柄
 */
void number_flow_init(void* handle);

/**
 * @brief 设置字体
 * @param handle NumberFlow 句柄
 * @param font 字体指针
 */
void number_flow_set_font(void* handle, const lv_font_t* font);

/**
 * @brief 设置文字颜色
 * @param handle NumberFlow 句柄
 * @param color 颜色
 */
void number_flow_set_color(void* handle, lv_color_t color);

/**
 * @brief 设置前缀
 * @param handle NumberFlow 句柄
 * @param prefix 前缀字符串
 */
void number_flow_set_prefix(void* handle, const char* prefix);

/**
 * @brief 设置后缀
 * @param handle NumberFlow 句柄
 * @param suffix 后缀字符串
 */
void number_flow_set_suffix(void* handle, const char* suffix);

/**
 * @brief 设置数值（滚动动画）
 * @param handle NumberFlow 句柄
 * @param value 数值
 */
void number_flow_set_value(void* handle, int value);

/**
 * @brief 设置数值（无滚动，用于校时跳变 / 首次对齐）
 * @param handle NumberFlow 句柄
 * @param value 数值
 */
void number_flow_set_value_immediate(void* handle, int value);

/**
 * @brief 时/分专用：ease_out_cubic ~0.22s，无弹簧
 * @param handle NumberFlow 句柄
 */
void number_flow_set_clock_preset(void* handle);

/**
 * @brief 秒钟专用：ease_out_quad ~0.14s，更脆的 1Hz 节拍
 * @param handle NumberFlow 句柄
 */
void number_flow_set_second_preset(void* handle);

/**
 * @brief 设置最小显示位数（不足前补0）
 * @param handle NumberFlow 句柄
 * @param min_digits 最小位数
 */
void number_flow_set_min_digits(void* handle, int min_digits);

/**
 * @brief 每帧更新
 * @param handle NumberFlow 句柄
 */
void number_flow_update(void* handle);

/**
 * @brief 删除 NumberFlow
 * @param handle NumberFlow 句柄
 */
void number_flow_delete(void* handle);

/**
 * @brief 更新所有 NumberFlow 实例（全局更新）
 */
void number_flow_update_all(void);

#ifdef __cplusplus
}
#endif
