/**
 * @file lv_draw_sunxi_g2d.c
 * @brief 全志 sunxi-g2d v9 draw unit 实现（Phase 1：不透明 FILL）。
 *
 * 结构照搬 fork 内 lvgl/src/draw/nxp/g2d/ 的 v9 draw unit 模板；
 * 底层换为 sunxi_g2d_utils（ION + /dev/g2d ioctl）。area 映射照 NXP lv_draw_g2d_fill.c。
 */

#include "lv_draw_sunxi_g2d.h"

#if LV_USE_SUNXI_G2D

#include <stdio.h>
#include "lvgl/src/draw/lv_draw_private.h"
#include "lvgl/src/draw/lv_draw_rect.h"
#include "lvgl/src/misc/lv_area_private.h"

#include "lv_draw_buf_sunxi_g2d.h"
#include "lv_sunxi_g2d_buf_map.h"
#include "sunxi_g2d_utils.h"

#define DRAW_UNIT_ID_SUNXI_G2D 8

static int g_eval_dbg = 0;   /* evaluate 调试：前 20 次 FILL 打印属性 */
static int g_disp_dbg = 0;  /* dispatch 调试：前 20 次打印找到的任务 */

typedef struct {
    lv_draw_unit_t base_unit;
    lv_draw_task_t * task_act;
} lv_draw_sunxi_g2d_unit_t;

static int32_t _evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task);
static int32_t _dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer);
static int32_t _delete(lv_draw_unit_t * draw_unit);
static void _execute_drawing(lv_draw_task_t * t);
static void _fill(lv_draw_task_t * t);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_sunxi_g2d_init(void)
{
    sunxi_g2d_buf_map_init();

    /* 打开 /dev/g2d + ION。失败则不装 handlers、不注册 unit → 纯 SW（UI 仍正常）。
     * 注意：必须先 open 成功再装 handlers，否则 LVGL 的 draw_buf 分配会走坏掉的 ION 路径。 */
    if(!sunxi_g2d_open()) {
        fprintf(stderr, "sunxi_g2d: init failed, fallback to pure SW rendering\n");
        return;
    }

    /* 装 ION draw buf handlers：之后 LVGL 分配的 draw_buf 走 ION（失败回退 malloc）并入 buf_map */
    lv_draw_buf_sunxi_g2d_init_handlers();

    /* 注册 draw unit */
    lv_draw_sunxi_g2d_unit_t * u = (lv_draw_sunxi_g2d_unit_t *) lv_draw_create_unit(sizeof(*u));
    u->base_unit.evaluate_cb = _evaluate;
    u->base_unit.dispatch_cb = _dispatch;
    u->base_unit.delete_cb   = _delete;
    u->base_unit.name        = "SUNXI_G2D";
    u->task_act              = NULL;

    fprintf(stderr, "sunxi_g2d: draw unit registered (FILL, RGB565, area>=12100)\n");
}

void lv_draw_sunxi_g2d_deinit(void)
{
    sunxi_g2d_close();
    sunxi_g2d_buf_map_deinit();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static int32_t _evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task)
{
    LV_UNUSED(draw_unit);

    const lv_draw_dsc_base_t * base = (const lv_draw_dsc_base_t *) task->draw_dsc;
    if(base == NULL || base->layer == NULL) return 0;

    /* 安全网：目标 draw_buf 必须是 ION（命中 buf_map），否则不认领 */
    lv_draw_buf_t * draw_buf = base->layer->draw_buf;
    if(draw_buf == NULL || draw_buf->data == NULL) return 0;
    if(sunxi_g2d_buf_map_search(draw_buf->data) == NULL) return 0;

    /* Phase 1：RGB565 / ARGB8888（本机 fb 实际 32bpp=ARGB8888） */
    if(base->layer->color_format != LV_COLOR_FORMAT_RGB565 &&
       base->layer->color_format != LV_COLOR_FORMAT_ARGB8888) return 0;

    if(task->type == LV_DRAW_TASK_TYPE_FILL) {
        const lv_draw_fill_dsc_t * dsc = (const lv_draw_fill_dsc_t *) task->draw_dsc;
        if(dsc == NULL) return 0;
        if(g_eval_dbg < 20) {
            g_eval_dbg++;
            fprintf(stderr, "sunxi_g2d eval FILL: cf=%d radius=%ld opa=%d grad=%d area=%ld score=%d in_map=%d\n",
                    (int)base->layer->color_format, (long)dsc->radius, (int)dsc->opa,
                    (int)dsc->grad.dir, (long)lv_area_get_size(&task->area),
                    (int)task->preference_score,
                    sunxi_g2d_buf_map_search(draw_buf->data) ? 1 : 0);
        }
        if(dsc->radius != 0) return 0;                       /* 圆角留给 SW */
        if(dsc->grad.dir != LV_GRAD_DIR_NONE) return 0;      /* 渐变留给 SW */
        if(dsc->opa < LV_OPA_MAX) return 0;                  /* alpha fill 留给 Phase 2 */
        /* 小面积留给 SW（g2d 启动开销不划算，参考 v8 阈值 12100≈110x110） */
        if(lv_area_get_size(&task->area) < 12100) return 0;

        /* 无条件标记给 g2d（score 70 优于 SW 的 100）。Phase 1 无更优 unit，不需 if 守卫。 */
        task->preference_score = 70;
        task->preferred_draw_unit_id = DRAW_UNIT_ID_SUNXI_G2D;
        return 1;
    }

    return 0;
}

static int32_t _dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer)
{
    lv_draw_sunxi_g2d_unit_t * u = (lv_draw_sunxi_g2d_unit_t *) draw_unit;

    if(u->task_act) return 0;  /* 忙 */

    lv_draw_task_t * t = lv_draw_get_available_task(layer, NULL, DRAW_UNIT_ID_SUNXI_G2D);
    if(g_disp_dbg < 20) {
        g_disp_dbg++;
        fprintf(stderr, "sunxi_g2d dispatch: t=%p preferred=%d state=%d type=%d\n",
                (void *)t, t ? (int)t->preferred_draw_unit_id : -1,
                t ? (int)t->state : -1, t ? (int)t->type : -1);
    }
    if(t == NULL || t->preferred_draw_unit_id != DRAW_UNIT_ID_SUNXI_G2D) {
        return LV_DRAW_UNIT_IDLE;
    }

    if(lv_draw_layer_alloc_buf(layer) == NULL) {
        return LV_DRAW_UNIT_IDLE;
    }

    t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
    u->task_act = t;

    _execute_drawing(t);

    u->task_act->state = LV_DRAW_TASK_STATE_FINISHED;
    u->task_act = NULL;

    lv_draw_dispatch_request();
    return 1;
}

static int32_t _delete(lv_draw_unit_t * draw_unit)
{
    LV_UNUSED(draw_unit);
    lv_draw_sunxi_g2d_deinit();
    return 0;
}

static void _execute_drawing(lv_draw_task_t * t)
{
    lv_layer_t * layer = t->target_layer;
    lv_draw_buf_t * draw_buf = layer->draw_buf;
    fprintf(stderr, "sunxi_g2d exec: type=%d data=%p in_map=%d\n",
            (int)t->type, draw_buf ? (void *)draw_buf->data : NULL,
            (draw_buf && draw_buf->data) ? (sunxi_g2d_buf_map_search(draw_buf->data) ? 1 : 0) : 0);
    if(draw_buf == NULL) return;

    /* g2d 前 flush cache（writeback+invalidate）：把 CPU 脏数据写回 RAM，并使 cache 失效，
     * g2d 读到最新、g2d 写后 CPU 下次读会从 RAM 重新加载。 */
    lv_draw_buf_invalidate_cache(draw_buf, NULL);

    if(t->type == LV_DRAW_TASK_TYPE_FILL) {
        _fill(t);
    }
}

static void _fill(lv_draw_task_t * t)
{
    const lv_draw_fill_dsc_t * dsc = (const lv_draw_fill_dsc_t *) t->draw_dsc;
    lv_layer_t * layer = t->target_layer;
    lv_draw_buf_t * draw_buf = layer->draw_buf;
    if(dsc == NULL || draw_buf == NULL || draw_buf->data == NULL) return;

    /* area → buffer 相对坐标（参考 NXP lv_draw_g2d_fill.c） */
    lv_area_t rel_coords;
    lv_area_copy(&rel_coords, &t->area);
    lv_area_move(&rel_coords, -layer->buf_area.x1, -layer->buf_area.y1);

    lv_area_t rel_clip;
    lv_area_copy(&rel_clip, &t->clip_area);
    lv_area_move(&rel_clip, -layer->buf_area.x1, -layer->buf_area.y1);

    lv_area_t blend_area;
    if(!lv_area_intersect(&blend_area, &rel_coords, &rel_clip)) return;

    sunxi_g2d_buf_t * dst = sunxi_g2d_buf_map_search(draw_buf->data);
    fprintf(stderr, "sunxi_g2d _fill: dst=%p cf=%d blend=%ldx%ld\n",
            (void *)dst, (int)draw_buf->header.cf,
            (long)lv_area_get_width(&blend_area), (long)lv_area_get_height(&blend_area));
    if(dst == NULL) return;  /* 非 ION，安全网拦下 */

    g2d_fmt_enh fmt;
    uint32_t color;
    if(draw_buf->header.cf == LV_COLOR_FORMAT_ARGB8888) {
        fmt = G2D_FORMAT_ARGB8888;
        color = lv_color_to_u32(dsc->color);
    }
    else {
        fmt = G2D_FORMAT_RGB565;
        color = (uint32_t)lv_color_to_u16(dsc->color);
    }

    sunxi_g2d_fill_rect(dst,
                        draw_buf->header.w, draw_buf->header.h,
                        fmt, color,
                        blend_area.x1, blend_area.y1,
                        (uint32_t)lv_area_get_width(&blend_area),
                        (uint32_t)lv_area_get_height(&blend_area));
}

#endif /* LV_USE_SUNXI_G2D */
