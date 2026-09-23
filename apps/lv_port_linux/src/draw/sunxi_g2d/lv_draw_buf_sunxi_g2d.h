/**
 * @file lv_draw_buf_sunxi_g2d.h
 * @brief 覆盖 LVGL draw buf 分配器，使 lv_draw_buf_t 全部走 ION 内存并登记进 buf_map。
 *
 * 这是内存桥接的核心：装好 handlers 后，LVGL 内部分配的 draw_buf（含 display
 * draw buffer、layer buffer、image decode buffer）自动变成 g2d 可访问的 ION 内存，
 * 且 draw unit 的 evaluate_cb 可通过 buf_map 判别。
 */

#ifndef LV_DRAW_BUF_SUNXI_G2D_H
#define LV_DRAW_BUF_SUNXI_G2D_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#if LV_USE_SUNXI_G2D
/* 安装 ION 后端的 draw buf handlers。须在 display draw buffer 分配前调用。 */
void lv_draw_buf_sunxi_g2d_init_handlers(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LV_DRAW_BUF_SUNXI_G2D_H */
