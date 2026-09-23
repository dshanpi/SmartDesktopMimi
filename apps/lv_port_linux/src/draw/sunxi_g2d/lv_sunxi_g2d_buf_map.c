/**
 * @file lv_sunxi_g2d_buf_map.c
 */

#include "lv_sunxi_g2d_buf_map.h"

#include <string.h>
#include <stdio.h>

#define SUNXI_G2D_BUF_MAP_MAX 64

static sunxi_g2d_buf_t * g_map[SUNXI_G2D_BUF_MAP_MAX];

void sunxi_g2d_buf_map_init(void)
{
    memset(g_map, 0, sizeof(g_map));
}

void sunxi_g2d_buf_map_deinit(void)
{
    /* 表本身不持有 buffer 生命周期（由 handlers 的 free_cb 释放），仅清指针。 */
    memset(g_map, 0, sizeof(g_map));
}

void sunxi_g2d_buf_map_insert(sunxi_g2d_buf_t * buf)
{
    if(buf == NULL || buf->vaddr == NULL) {
        return;
    }
    /* 先找已有同 vaddr 槽位（更新） */
    for(int i = 0; i < SUNXI_G2D_BUF_MAP_MAX; i++) {
        if(g_map[i] && g_map[i]->vaddr == buf->vaddr) {
            g_map[i] = buf;
            return;
        }
    }
    /* 找空槽 */
    for(int i = 0; i < SUNXI_G2D_BUF_MAP_MAX; i++) {
        if(g_map[i] == NULL) {
            g_map[i] = buf;
            return;
        }
    }
    fprintf(stderr, "sunxi_g2d_buf_map: full (%d), increase SUNXI_G2D_BUF_MAP_MAX\n",
            SUNXI_G2D_BUF_MAP_MAX);
}

sunxi_g2d_buf_t * sunxi_g2d_buf_map_search(const void * vaddr)
{
    if(vaddr == NULL) {
        return NULL;
    }
    for(int i = 0; i < SUNXI_G2D_BUF_MAP_MAX; i++) {
        if(g_map[i] && g_map[i]->vaddr == vaddr) {
            return g_map[i];
        }
    }
    return NULL;
}

sunxi_g2d_buf_t * sunxi_g2d_buf_map_take(const void * vaddr)
{
    if(vaddr == NULL) {
        return NULL;
    }
    for(int i = 0; i < SUNXI_G2D_BUF_MAP_MAX; i++) {
        if(g_map[i] && g_map[i]->vaddr == vaddr) {
            sunxi_g2d_buf_t * buf = g_map[i];
            g_map[i] = NULL;
            return buf;
        }
    }
    return NULL;
}
