/**
 * @file sunxi_g2d_utils.c
 * @brief Allwinner sunxi-g2d 底层封装实现。
 */

#include "sunxi_g2d_utils.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ion_mem_alloc.h>

static int g_g2d_fd = -1;
static struct SunxiMemOpsS * g_memops = NULL;
static unsigned long g_fill_count = 0;   /* g2d 命中计数（验证用） */

bool sunxi_g2d_open(void)
{
    if(g_g2d_fd >= 0) {
        return true;
    }

    g_g2d_fd = open("/dev/g2d", O_RDWR);
    if(g_g2d_fd < 0) {
        perror("sunxi_g2d: open /dev/g2d failed");
        g_g2d_fd = -1;
        return false;
    }

    g_memops = GetMemAdapterOpsS();
    if(g_memops == NULL) {
        perror("sunxi_g2d: GetMemAdapterOpsS failed");
        close(g_g2d_fd);
        g_g2d_fd = -1;
        return false;
    }
    if(SunxiMemOpen(g_memops) != 0) {
        perror("sunxi_g2d: SunxiMemOpen failed");
        g_memops = NULL;
        close(g_g2d_fd);
        g_g2d_fd = -1;
        return false;
    }

    fprintf(stderr, "sunxi_g2d: /dev/g2d opened, ION ready\n");
    return true;
}

void sunxi_g2d_close(void)
{
    if(g_memops) {
        SunxiMemClose(g_memops);
        g_memops = NULL;
    }
    if(g_g2d_fd >= 0) {
        close(g_g2d_fd);
        g_g2d_fd = -1;
    }
}

sunxi_g2d_buf_t * sunxi_g2d_alloc(int size)
{
    if(g_memops == NULL || size <= 0) {
        return NULL;
    }

    void * vaddr = SunxiMemPalloc(g_memops, size);
    if(vaddr == NULL) {
        fprintf(stderr, "sunxi_g2d: ion palloc %d failed\n", size);
        return NULL;
    }

    void * phy = SunxiMemGetPhysicAddressCpu(g_memops, vaddr);
    if(phy == NULL) {
        fprintf(stderr, "sunxi_g2d: get phyaddr failed\n");
        SunxiMemPfree(g_memops, vaddr);
        return NULL;
    }

    int fd = SunxiMemGetBufferFd(g_memops, vaddr);
    if(fd < 0) {
        fprintf(stderr, "sunxi_g2d: get dma_buf fd failed\n");
        SunxiMemPfree(g_memops, vaddr);
        return NULL;
    }

    sunxi_g2d_buf_t * buf = (sunxi_g2d_buf_t *)malloc(sizeof(sunxi_g2d_buf_t));
    if(buf == NULL) {
        SunxiMemPfree(g_memops, vaddr);
        return NULL;
    }
    buf->vaddr = vaddr;
    buf->phy = phy;
    buf->fd = fd;
    buf->size = size;
    return buf;
}

void sunxi_g2d_free(sunxi_g2d_buf_t * buf)
{
    if(buf == NULL) {
        return;
    }
    if(g_memops && buf->vaddr) {
        SunxiMemPfree(g_memops, buf->vaddr);
    }
    free(buf);
}

void sunxi_g2d_flush_cache(sunxi_g2d_buf_t * buf)
{
    if(buf == NULL || g_memops == NULL || buf->vaddr == NULL) {
        return;
    }
    SunxiMemFlushCache(g_memops, buf->vaddr, buf->size);
}

int sunxi_g2d_fill_rect(sunxi_g2d_buf_t * dst, uint32_t dst_w, uint32_t dst_h,
                        g2d_fmt_enh fmt, uint32_t color,
                        int32_t clip_x, int32_t clip_y, uint32_t clip_w, uint32_t clip_h)
{
    if(dst == NULL || g_g2d_fd < 0) {
        fprintf(stderr, "sunxi_g2d fill_rect BAIL: dst=%p fd=%d\n", (void *)dst, g_g2d_fd);
        return -1;
    }

    g2d_fillrect_h info;
    memset(&info, 0, sizeof(info));

    info.dst_image_h.format      = fmt;
    info.dst_image_h.color       = color;
    info.dst_image_h.alpha       = 255;
    info.dst_image_h.mode        = G2D_PIXEL_ALPHA;  /* 不透明填充：alpha=255 直接覆盖 */
    info.dst_image_h.width       = dst_w;
    info.dst_image_h.height      = dst_h;
    info.dst_image_h.clip_rect.x = clip_x;
    info.dst_image_h.clip_rect.y = clip_y;
    info.dst_image_h.clip_rect.w = clip_w;
    info.dst_image_h.clip_rect.h = clip_h;
    /* A133 g2d 有 IOMMU：走 dma_buf fd 模式（use_phy_addr=0），驱动 g2d_dma_map 建 IOMMU 映射。
     * use_phy_addr=1 直传物理地址会被拒（EPERM）。 */
    info.dst_image_h.fd          = dst->fd;
    info.dst_image_h.use_phy_addr = 0;

    int r = ioctl(g_g2d_fd, G2D_CMD_FILLRECT_H, &info);
    fprintf(stderr, "sunxi_g2d fill_rect ioctl=%d fmt=%d fd=%d w=%u h=%u clip=%u,%u,%u,%u\n",
            r, (int)fmt, dst->fd, dst_w, dst_h, clip_x, clip_y, clip_w, clip_h);
    if(r < 0) {
        perror("sunxi_g2d: G2D_CMD_FILLRECT_H failed");
        return -1;
    }

    g_fill_count++;
    /* 验证用：前 5 次总打印（上机即可确认 g2d 被命中）；
     * 之后需设环境变量 SUNXI_G2D_STATS 才按每 100 次打印，避免生产噪音。 */
    if(g_fill_count <= 5 ||
       (getenv("SUNXI_G2D_STATS") && (g_fill_count % 100 == 0))) {
        fprintf(stderr, "sunxi_g2d: fill #%lu dst=%ux%u clip=[%ld,%ld,%ux%u]\n",
                g_fill_count, dst_w, dst_h,
                (long)clip_x, (long)clip_y, clip_w, clip_h);
    }
    return 0;
}
