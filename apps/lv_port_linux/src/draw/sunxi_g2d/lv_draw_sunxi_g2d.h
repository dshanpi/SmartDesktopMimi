/**
 * @file lv_draw_sunxi_g2d.h
 * @brief 全志 sunxi-g2d v9 draw unit（Phase 1：仅不透明矩形填充）。
 *
 * 调 lv_draw_sunxi_g2d_init() 注册。与内置 lv_draw_sw 单元共存：
 * evaluate 仅在目标 draw_buf 命中 ION buf_map、且为 RGB565 不透明无圆角无渐变 FILL 时
 * 才认领（preference_score=70，优于 SW 的 100）；其余回退 SW。
 */

#ifndef LV_DRAW_SUNXI_G2D_H
#define LV_DRAW_SUNXI_G2D_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#if LV_USE_SUNXI_G2D
/* 注册 g2d draw unit + 装 ION draw buf handlers + 打开 /dev/g2d。
 * 须在 lv_init() 之后、display 创建之前调用。 */
void lv_draw_sunxi_g2d_init(void);

void lv_draw_sunxi_g2d_deinit(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LV_DRAW_SUNXI_G2D_H */
