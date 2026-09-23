/**
 * @file lv_draw_buf_sunxi_g2d.c
 */

#include "lv_draw_buf_sunxi_g2d.h"

#if LV_USE_SUNXI_G2D

#include "lvgl/src/draw/lv_draw_buf_private.h"
#include "sunxi_g2d_utils.h"
#include "lv_sunxi_g2d_buf_map.h"

static void * _buf_malloc(size_t size, lv_color_format_t color_format)
{
    LV_UNUSED(color_format);
    sunxi_g2d_buf_t * buf = sunxi_g2d_alloc((int)size);
    if(buf) {
        sunxi_g2d_buf_map_insert(buf);
        return buf->vaddr;
    }
    /* ION 失败 → 回退 LVGL 默认堆（非 ION，g2d 不接管，SW 兜底） */
    return lv_malloc(size);
}

static void _buf_free(void * ptr)
{
    if(ptr == NULL) {
        return;
    }
    sunxi_g2d_buf_t * buf = sunxi_g2d_buf_map_take(ptr);
    if(buf) {
        sunxi_g2d_free(buf);   /* ION */
    }
    else {
        lv_free(ptr);          /* malloc 回退的 */
    }
}

static void _cache_op(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    LV_UNUSED(area);
    if(draw_buf == NULL || draw_buf->data == NULL) {
        return;
    }
    sunxi_g2d_buf_t * buf = sunxi_g2d_buf_map_search(draw_buf->data);
    if(buf) {
        sunxi_g2d_flush_cache(buf);
    }
}

void lv_draw_buf_sunxi_g2d_init_handlers(void)
{
    lv_draw_buf_handlers_t * h = lv_draw_buf_get_handlers();
    h->buf_malloc_cb      = _buf_malloc;
    h->buf_free_cb        = _buf_free;
    h->invalidate_cache_cb = _cache_op;
    h->flush_cache_cb     = _cache_op;
}

#endif /* LV_USE_SUNXI_G2D */
