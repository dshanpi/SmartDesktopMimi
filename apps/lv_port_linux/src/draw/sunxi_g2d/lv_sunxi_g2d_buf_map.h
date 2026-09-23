/**
 * @file lv_sunxi_g2d_buf_map.h
 * @brief vaddr → sunxi_g2d_buf_t* 映射表。
 *
 * 作用（安全网）：draw unit 的 evaluate_cb 只在目标 draw_buf 命中本表时才认领任务，
 * 保证 g2d 永远只碰 ION 内存；非 ION buffer（如纯 SW 路径的 malloc buf）一律回退 SW。
 *
 * Phase 1 用定长数组线性查找（draw buf 数量少），后续若需要可换哈希表。
 */

#ifndef LV_SUNXI_G2D_BUF_MAP_H
#define LV_SUNXI_G2D_BUF_MAP_H

#include "sunxi_g2d_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

void sunxi_g2d_buf_map_init(void);
void sunxi_g2d_buf_map_deinit(void);

/* 登记一个 buffer（按 vaddr）。重复 vaddr 视为更新。 */
void sunxi_g2d_buf_map_insert(sunxi_g2d_buf_t * buf);

/* 按 vaddr 查找，找不到返回 NULL。 */
sunxi_g2d_buf_t * sunxi_g2d_buf_map_search(const void * vaddr);

/* 按 vaddr 取出并从表中移除（不释放 buf），返回取出的 buf，找不到返回 NULL。
 * 调用方负责 sunxi_g2d_free()。 */
sunxi_g2d_buf_t * sunxi_g2d_buf_map_take(const void * vaddr);

#ifdef __cplusplus
}
#endif

#endif /* LV_SUNXI_G2D_BUF_MAP_H */
