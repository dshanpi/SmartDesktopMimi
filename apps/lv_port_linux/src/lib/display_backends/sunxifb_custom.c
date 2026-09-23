/**
 * @file sunxifb_custom.c
 *
 * Custom Framebuffer driver for Allwinner (Sunxi) SoCs
 * Implements double buffering (FBIOPAN_DISPLAY) and Cache Sync
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>

#include "lvgl/lvgl.h"
#if LV_USE_SUNXIFB
#include "../simulator_util.h"
#include "../backends.h"
#if LV_USE_SUNXI_G2D
#include "../../draw/sunxi_g2d/sunxi_g2d_utils.h"
#include "../../draw/sunxi_g2d/lv_sunxi_g2d_buf_map.h"
#endif

/*********************
 *      DEFINES
 *********************/
#ifndef SUNXIFB_PATH
#define SUNXIFB_PATH  "/dev/fb0"
#endif

/* Allwinner specific IOCTLs for Cache Management */
#define FBIO_CACHE_SYNC         0x4630
#define FBIO_ENABLE_CACHE       0x4631

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    int fbfd;
    char *fbp;
    char *screenfbp[2]; /* Pointers to double buffers */
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    long int screensize;
    uint32_t fbp_w;
    uint32_t fbp_h;
    uint32_t fbp_line_length;
    uint32_t fbnum;     /* Number of buffers (1 or 2) */
    uint32_t fbindex;   /* Current buffer index */
    bool dbuf_en;       /* Double buffering enabled */
} sunxifb_info_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_display_t * init_sunxifb(void);
static void run_loop_sunxifb(void);
static void sunxifb_flush(lv_display_t *drv, const lv_area_t *area, uint8_t *color_p);
static void sunxifb_cleanup(lv_event_t * e);
static void sunxifb_cache_sync(char *addr, size_t size);
static void sunxifb_clear_framebuffers(bool transparent);

/**********************
 *  STATIC VARIABLES
 **********************/

static char * backend_name = "SUNXIFB";
static sunxifb_info_t sinfo;



/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Register the backend
 */
int backend_init_sunxifb_custom(backend_t * backend)
{
    LV_ASSERT_NULL(backend);

    backend->handle->display = malloc(sizeof(display_backend_t));
    LV_ASSERT_NULL(backend->handle->display);

    backend->handle->display->init_display = init_sunxifb;
    backend->handle->display->run_loop = run_loop_sunxifb;
    backend->name = backend_name;
    backend->type = BACKEND_DISPLAY;

    return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Initialize the sunxifb driver
 */
static lv_display_t * init_sunxifb(void)
{
    memset(&sinfo, 0, sizeof(sunxifb_info_t));

    /* Open the framebuffer device */
    sinfo.fbfd = open(SUNXIFB_PATH, O_RDWR);
    if (sinfo.fbfd == -1) {
        perror("Error: cannot open framebuffer device");
        return NULL;
    }

    /* Get fixed screen information */
    if (ioctl(sinfo.fbfd, FBIOGET_FSCREENINFO, &sinfo.finfo) == -1) {
        perror("Error reading fixed information");
        close(sinfo.fbfd);
        return NULL;
    }

    /* Get variable screen information */
    if (ioctl(sinfo.fbfd, FBIOGET_VSCREENINFO, &sinfo.vinfo) == -1) {
        perror("Error reading variable information");
        close(sinfo.fbfd);
        return NULL;
    }

    /* Enable Cache for performance */
    uintptr_t args[2] = { 1, 0 };
    if (ioctl(sinfo.fbfd, FBIO_ENABLE_CACHE, args) < 0) {
        perror("Warning: FBIO_ENABLE_CACHE fail (might be slow)");
    }

    /* Calculate screensize */
    sinfo.screensize = sinfo.finfo.smem_len;

    /* Map the device to memory */
    sinfo.fbp = (char*) mmap(0, sinfo.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, sinfo.fbfd, 0);
    if ((intptr_t) sinfo.fbp == -1) {
        perror("Error: failed to map framebuffer device to memory");
        close(sinfo.fbfd);
        return NULL;
    }

    printf("SunxiFB: %dx%d, %dbpp, smem_len=%ld\n", 
           sinfo.vinfo.xres, sinfo.vinfo.yres, sinfo.vinfo.bits_per_pixel, sinfo.screensize);

    sinfo.fbp_w = sinfo.vinfo.xres;
    sinfo.fbp_h = sinfo.vinfo.yres;
    sinfo.fbp_line_length = sinfo.finfo.line_length;

    /* Detect Double Buffering Capability */
    uint32_t frame_size = sinfo.finfo.line_length * sinfo.vinfo.yres;
    sinfo.fbnum = (uint32_t)(sinfo.screensize / frame_size);
    
    if (sinfo.fbnum >= 2) {
        printf("SunxiFB: Double buffering enabled.\n");
        sinfo.dbuf_en = true;
        sinfo.screenfbp[0] = sinfo.fbp;
        sinfo.screenfbp[1] = sinfo.fbp + frame_size;
        
        /* Ensure we start at yoffset 0 */
        if (sinfo.vinfo.yoffset != 0) {
            sinfo.vinfo.yoffset = 0;
            ioctl(sinfo.fbfd, FBIOPAN_DISPLAY, &sinfo.vinfo);
        }
        sinfo.fbindex = 0;
    } else {
        printf("SunxiFB: Double buffering disabled (fbnum=%d).\n", sinfo.fbnum);
        sinfo.dbuf_en = false;
        sinfo.screenfbp[0] = sinfo.fbp;
        sinfo.fbindex = 0;
    }

    sunxifb_clear_framebuffers(false);

    /* Create LVGL Display */
    lv_display_t * disp = lv_display_create(sinfo.vinfo.xres, sinfo.vinfo.yres);
    if(disp == NULL) {
        munmap(sinfo.fbp, sinfo.screensize);
        close(sinfo.fbfd);
        return NULL;
    }

    /* Configure LVGL Display */
    lv_display_set_flush_cb(disp, sunxifb_flush);
    
    /* Allocate draw buffers */
    /* We use PARTIAL mode by default, let LVGL manage the small buffer */
    /* Alternatively, for pure double buffering without intermediate copy, we would need FULL mode */
    /* But to match sunxifb.c logic which accepts a buffer and copies it to FB, we stick to standard flow */
    
    /* Set color format based on bpp */
    switch(sinfo.vinfo.bits_per_pixel) {
        case 16:
            lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
            break;
        case 24:
            lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
            break;
        case 32:
            // Use ARGB8888 instead of XRGB8888 to support alpha channel
            lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
            break;
        default:
            LV_LOG_WARN("SunxiFB: Unsupported bpp %d", sinfo.vinfo.bits_per_pixel);
            break;
    }

    /* Allocate LVGL draw buffers */
    /* Use 1/10th of screen size for draw buffer (typical for embedded) */
#if LV_USE_SUNXI_G2D
    /* g2d 路径：draw buffer 须走 lv_draw_buf_create（→ 自定义 ION allocator → 入 buf_map），
     * g2d 才能访问。PARTIAL 模式，buffer 宽=屏宽、高=屏高/10（容量≈1/10 屏）。
     * stride=0 让 LVGL 按 STRIDE_ALIGN=1 打包，与 g2d 按 width*bpp 算行距一致。 */
    {
        uint32_t buf_w = sinfo.vinfo.xres;
        uint32_t buf_h = (sinfo.vinfo.yres + 9) / 10;  /* ceil(yres/10) */
        lv_color_format_t g2d_cf;
        switch(sinfo.vinfo.bits_per_pixel) {
            case 16:  g2d_cf = LV_COLOR_FORMAT_RGB565;  break;
            case 24:  g2d_cf = LV_COLOR_FORMAT_RGB888;  break;
            default:  g2d_cf = LV_COLOR_FORMAT_ARGB8888; break;
        }
        lv_draw_buf_t * dbuf1 = lv_draw_buf_create(buf_w, buf_h, g2d_cf, 0);
        if(dbuf1 == NULL) {
            fprintf(stderr, "sunxifb: g2d lv_draw_buf_create failed, fallback to malloc\n");
            uint32_t buf_size = sinfo.vinfo.xres * sinfo.vinfo.yres * (sinfo.vinfo.bits_per_pixel / 8) / 10;
            void * buf1 = malloc(buf_size);
            lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        }
        else {
            lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_PARTIAL);
            lv_display_set_draw_buffers(disp, dbuf1, NULL);
        }
    }
#else
    uint32_t buf_size = sinfo.vinfo.xres * sinfo.vinfo.yres * (sinfo.vinfo.bits_per_pixel / 8) / 10;
    void * buf1 = malloc(buf_size);
    void * buf2 = NULL; // Single draw buffer is enough as we copy to FB immediately

    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
    
    lv_display_add_event_cb(disp, sunxifb_cleanup, LV_EVENT_DELETE, NULL);

    return disp;
}

static void sunxifb_cache_sync(char *addr, size_t size)
{
    uintptr_t args[2];

    args[0] = (uintptr_t) addr;
    args[1] = (uintptr_t) size;
    ioctl(sinfo.fbfd, FBIO_CACHE_SYNC, args);
}

static void sunxifb_clear_framebuffers(bool transparent)
{
    uint32_t frame_size = sinfo.finfo.line_length * sinfo.vinfo.yres;
    uint32_t max_fbnum;
    uint32_t fbnum = sinfo.fbnum;

    if (!sinfo.fbp || frame_size == 0 || sinfo.screensize <= 0) {
        return;
    }

    max_fbnum = (uint32_t) ((uint64_t) sinfo.screensize / frame_size);
    if (max_fbnum == 0) {
        return;
    }

    if (fbnum == 0 || fbnum > max_fbnum) {
        fbnum = max_fbnum;
    }

    for (uint32_t i = 0; i < fbnum; i++) {
        char *fbp = sinfo.fbp + ((size_t) i * frame_size);

        if (sinfo.vinfo.bits_per_pixel == 32) {
            uint32_t *pixel = (uint32_t *) fbp;
            size_t count = frame_size / sizeof(uint32_t);

            for (size_t j = 0; j < count; j++) {
                pixel[j] = transparent ? 0x00000000 : 0xFF000000;
            }
        } else {
            memset(fbp, 0, frame_size);
        }

        sunxifb_cache_sync(fbp, frame_size);
    }

    if (sinfo.dbuf_en) {
        sinfo.vinfo.yoffset = 0;
        if (ioctl(sinfo.fbfd, FBIOPAN_DISPLAY, &sinfo.vinfo) < 0) {
            perror("Warning: FBIOPAN_DISPLAY fail while clearing framebuffer");
        }
        sinfo.fbindex = 0;
    }
}

bool sunxifb_set_transparent_overlay(bool transparent)
{
    if (!sinfo.fbp || sinfo.vinfo.bits_per_pixel != 32 ||
        sinfo.vinfo.transp.length == 0) {
        LV_LOG_WARN("SunxiFB: HDMI overlay requires a 32-bit framebuffer with alpha");
        return false;
    }

    sunxifb_clear_framebuffers(transparent);
    LV_LOG_INFO("SunxiFB: HDMI overlay transparency=%d", transparent);
    return true;
}

static void sunxifb_cleanup(lv_event_t * e)
{
    LV_UNUSED(e);
    // Free buffers managed by LVGL if any (here we malloced buf1 manually so we should free it if we tracked it)
    // For simplicity, we assume OS cleanup or specific tracking struct if needed.
    // Ideally we should attach the buffer pointer to driver data to free it here.
    
    if (sinfo.fbp && sinfo.fbp != (char*)-1) {
        munmap(sinfo.fbp, sinfo.screensize);
    }
    if (sinfo.fbfd >= 0) {
        close(sinfo.fbfd);
    }
}

static void sunxifb_flush(lv_display_t *drv, const lv_area_t *area, uint8_t *color_p)
{
    if (!sinfo.fbp) {
        lv_display_flush_ready(drv);
        return;
    }

    int32_t act_x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t act_y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t act_x2 = area->x2 > (int32_t)sinfo.fbp_w - 1 ? (int32_t)sinfo.fbp_w - 1 : area->x2;
    int32_t act_y2 = area->y2 > (int32_t)sinfo.fbp_h - 1 ? (int32_t)sinfo.fbp_h - 1 : area->y2;

    lv_coord_t w = (act_x2 - act_x1 + 1);
    long int location = 0;
    
    /* Target buffer is the CURRENT back buffer (if double buffered) or just the single buffer */
    /* Note: sunxifb logic is: write to the hidden buffer, then swap */
    char * target_fbp;
    
    if (sinfo.dbuf_en) {
        /* If double buffered, we write to the 'next' buffer (not the currently displayed one) */
        /* sinfo.fbindex points to the currently DISPLAYED buffer */
        /* So we write to !sinfo.fbindex */
        target_fbp = sinfo.screenfbp[!sinfo.fbindex];
    } else {
        target_fbp = sinfo.screenfbp[0];
    }

#if LV_USE_SUNXI_G2D
    /* g2d 可能刚写了 draw buffer（此时 CPU cache 为 stale）；CPU 读前 flush cache
     * （writeback+invalidate）。SW 写的情况同样安全：writeback 落 RAM，invalidate 后重载一致。
     * color_p 即 draw_buf->data（ION vaddr），命中 buf_map 才刷。 */
    {
        sunxi_g2d_buf_t * sbuf = sunxi_g2d_buf_map_search(color_p);
        if(sbuf) {
            sunxi_g2d_flush_cache(sbuf);
        }
    }
#endif

    /* Copy Data */
    /* Optimized copy based on bpp */
    if (sinfo.vinfo.bits_per_pixel == 32 || sinfo.vinfo.bits_per_pixel == 24) {
        uint32_t *fbp32 = (uint32_t*) target_fbp;
        int32_t y;
        for (y = act_y1; y <= act_y2; y++) {
            location = (act_x1 + sinfo.vinfo.xoffset) + (y + sinfo.vinfo.yoffset) * sinfo.finfo.line_length / 4;
            /* Correction: vinfo.yoffset is for PAN, but here we write to absolute memory address */
            /* The target_fbp already points to the start of the correct frame */
            /* So we only need offset within that frame */
             location = act_x1 + y * (sinfo.finfo.line_length / 4);
             
            memcpy(&fbp32[location], (uint32_t*) color_p, w * 4);
            color_p += w * 4; // LVGL buffer is contiguous
        }
    } else if (sinfo.vinfo.bits_per_pixel == 16) {
        uint16_t *fbp16 = (uint16_t*) target_fbp;
        int32_t y;
        for (y = act_y1; y <= act_y2; y++) {
            location = act_x1 + y * (sinfo.finfo.line_length / 2);
            memcpy(&fbp16[location], (uint16_t*) color_p, w * 2);
            color_p += w * 2;
        }
    }

    /* Handle Double Buffering Swap */
    /* Only swap if this is the LAST part of the frame update */
    if (lv_display_flush_is_last(drv)) {
        if (sinfo.dbuf_en) {
            /* 1. Sync Cache */
            sunxifb_cache_sync(target_fbp, sinfo.finfo.line_length * sinfo.vinfo.yres);

            /* 2. Pan Display */
            sinfo.vinfo.yoffset = (!sinfo.fbindex) * sinfo.vinfo.yres;
            if (ioctl(sinfo.fbfd, FBIOPAN_DISPLAY, &sinfo.vinfo) < 0) {
                perror("Error: FBIOPAN_DISPLAY fail");
            }

            /* 3. Update Index */
            sinfo.fbindex = !sinfo.fbindex;

            /* 4. Copy content to the new back buffer to keep it in sync? */
            /* If we are doing partial updates, the back buffer must have the previous frame's content */
            /* Since we just swapped, the NEW back buffer (old front buffer) has the OLD frame content */
            /* plus whatever we just drew? No. */
            /* If we draw PARTIAL, we update a region. */
            /* If we swap, the new front buffer has the new region. */
            /* The new back buffer (old front) is MISSING that new region update. */
            /* So we must sync the buffers. */

            char * new_back_buffer = sinfo.screenfbp[!sinfo.fbindex];
            char * new_front_buffer = sinfo.screenfbp[sinfo.fbindex];

            memcpy(new_back_buffer, new_front_buffer, sinfo.finfo.line_length * sinfo.vinfo.yres);
        }

    }

    lv_display_flush_ready(drv);
}

/**
 * The run loop
 */
static void run_loop_sunxifb(void)
{
    /* Handle LVGL tasks */
    while(true) {
        lv_timer_handler();
        usleep(5000); /* Sleep for 5 milliseconds */
    }
}

#endif /*LV_USE_SUNXIFB*/
