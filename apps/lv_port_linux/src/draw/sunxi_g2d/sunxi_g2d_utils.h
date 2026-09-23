/**
 * @file sunxi_g2d_utils.h
 * @brief Allwinner sunxi-g2d 底层封装：ION 物理连续内存 + /dev/g2d ioctl。
 *
 * 不依赖 LVGL，可单独测试。上层（draw buf handlers / draw unit）通过本层
 * 分配 ION 内存、取物理地址、刷 cache、调用 g2d 硬件填充。
 *
 * 内存模型：libuapi 的 ION 分配器（GetMemAdapterOpsS），DMA heap 物理连续，
 * cached。g2d 用 use_phy_addr=1 + 物理地址访问。每次 g2d 操作前需 flush cache
 * （SunxiMemFlushCache 做 writeback+invalidate），保证 CPU↔g2d 一致性。
 */

#ifndef SUNXI_G2D_UTILS_H
#define SUNXI_G2D_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "g2d_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ION 分配的 buffer 描述：vaddr 给 CPU 画/读，phy/fd 给 g2d 访问。 */
typedef struct {
    void * vaddr;   /* CPU 虚拟地址（ion mmap，page-aligned） */
    void * phy;     /* 物理地址（调试用；A133 有 IOMMU，g2d 实际走 fd 模式） */
    int    fd;      /* dma_buf fd（g2d use_phy_addr=0 模式，驱动建 IOMMU 映射） */
    int    size;    /* 字节数 */
} sunxi_g2d_buf_t;

/* 打开 /dev/g2d + 初始化 ION（SunxiMemSetup）。幂等，重复调用安全。成功返回 true。 */
bool sunxi_g2d_open(void);

/* 关闭 /dev/g2d + ION shutdown。 */
void sunxi_g2d_close(void);

/* ION 分配 size 字节物理连续内存，返回描述符（含 vaddr/phy）。失败返回 NULL。 */
sunxi_g2d_buf_t * sunxi_g2d_alloc(int size);

/* 释放 ION 内存 + 描述符。buf 可为 NULL。 */
void sunxi_g2d_free(sunxi_g2d_buf_t * buf);

/* flush cache（writeback+invalidate），g2d 访问前后用于保持一致性。buf 可为 NULL。 */
void sunxi_g2d_flush_cache(sunxi_g2d_buf_t * buf);

/* 不透明矩形填充：在 dst 的 (clip_x,clip_y,clip_w,clip_h) 区域填 color。
 * dst_w/dst_h 为 buffer 整体宽高（g2d 按 width*bpp 算行距，须与 LVGL stride 一致）。
 * fmt/color 按 g2d 格式。成功返回 0，失败 -1。 */
int sunxi_g2d_fill_rect(sunxi_g2d_buf_t * dst, uint32_t dst_w, uint32_t dst_h,
                        g2d_fmt_enh fmt, uint32_t color,
                        int32_t clip_x, int32_t clip_y, uint32_t clip_w, uint32_t clip_h);

#ifdef __cplusplus
}
#endif

#endif /* SUNXI_G2D_UTILS_H */
