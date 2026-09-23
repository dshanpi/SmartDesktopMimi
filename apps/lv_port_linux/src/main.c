/*******************************************************************
 *
 * main.c - LVGL simulator for GNU/Linux (UI 进程)
 *
 * 架构说明:
 * - UI 进程：运行 LVGL 主循环，处理显示和输入
 * - Backend 进程：独立进程，通过 IPC 与 UI 通信
 * - IPC: Unix Domain Socket
 *
 * 使用方法:
 *   1. 先启动 Backend 进程：./build/bin/lv_backend
 *   2. 再启动 UI 进程：./build/bin/lvglsim
 *
 * Based on the original file from the repository
 *
 * @note eventually this file won't contain a main function and will
 * become a library supporting all major operating systems
 *
 * To see how each driver is initialized check the
 * 'src/lib/display_backends' directory
 *
 * - Clean up
 * - Support for multiple backends at once
 *   2025 EDGEMTech Ltd.
 *
 * Author: EDGEMTech Ltd, Erik Tagirov (erik.tagirov@edgemtech.ch)
 *
 ******************************************************************/
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#include "lvgl/lvgl.h"
#if LV_USE_SUNXI_G2D
#include "src/draw/sunxi_g2d/lv_draw_sunxi_g2d.h"
#endif

#include "src/lib/driver_backends.h"
#include "src/ui/ui.h"
#include "src/lib/simulator_util.h"
#include "src/lib/simulator_settings.h"
#include "src/system/backlight_control.h"
#include "src/system/settings.h"
#include "src/system/volume_control.h"
#include "src/system/config/app_config.h"
#include "src/system/app_manager.h"
#include "src/ui/animate/animate_core_c.h"
#include "src/middleware/middleware.h"
#include "src/ipc/ipc_socket.h"

/* Internal functions */
static void configure_simulator(int argc, char ** argv);
static void print_lvgl_version(void);
static void print_usage(void);
static void * lvgl_tick_thread(void * arg);
static void mw_timer_callback(lv_timer_t * timer);
static void heartbeat_timer_callback(lv_timer_t * timer);
static void write_boot_progress(int progress);

/* contains the name of the selected backend if user
 * has specified one on the command line */
static char * selected_backend;

/* Global simulator settings, defined in lv_linux_backend.c */
extern simulator_settings_t settings;

/* LVGL tick increment thread - 5ms period */
static void * lvgl_tick_thread(void * arg)
{
    (void)arg;
    while(1) {
        lv_tick_inc(5);
        usleep(5000); /* 5ms */
    }
    return NULL;
}

/* Timer callback wrapper for mw_process_ui_messages */
static void mw_timer_callback(lv_timer_t * timer)
{
    (void)timer;
    mw_process_ui_messages();
}

/* Timer callback for NumberFlow update */
static void number_flow_timer_callback(lv_timer_t * timer)
{
    (void)timer;
    animate_update_all();
}

/* Heartbeat timer callback - send heartbeat to backend */
static void heartbeat_timer_callback(lv_timer_t * timer)
{
    (void)timer;
    extern int mw_get_socket_fd(void);
    int fd = mw_get_socket_fd();
    if (fd >= 0) {
        ipc_client_heartbeat(fd);
    }
}

static void write_boot_progress(int progress)
{
    const char * path = getenv("BOOT_PROGRESS_FILE");
    FILE * fp;

    if(!path || !*path) {
        path = "/tmp/boot_progress";
    }

    fp = fopen(path, "w");
    if(fp) {
        fprintf(fp, "%d\n", progress);
        fclose(fp);
    }
}

/**
 * @brief Print LVGL version
 */
static void print_lvgl_version(void)
{
    fprintf(stdout, "%d.%d.%d-%s\n",
            LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR,
            LVGL_VERSION_PATCH,
            LVGL_VERSION_INFO);
}

/**
 * @brief Print usage information
 */
static void print_usage(void)
{
    fprintf(stdout, "\nlvglsim [-V] [-B] [-f] [-m] [-b backend_name] [-W window_width] [-H window_height]\n\n");
    fprintf(stdout, "-V print LVGL version\n");
    fprintf(stdout, "-B list supported backends\n");
    fprintf(stdout, "-f fullscreen\n");
    fprintf(stdout, "-m maximize\n");
    fprintf(stdout, "-a auto-start backend process\n");
}

/**
 * @brief Configure simulator
 * @description process arguments received by the program to select
 * appropriate options
 * @param argc the count of arguments in argv
 * @param argv The arguments
 */
static void configure_simulator(int argc, char ** argv)
{
    int opt = 0;
    const app_config_t *cfg = app_config_get();

    selected_backend = NULL;
    driver_backends_register();

    const char * env_w = getenv("LV_SIM_WINDOW_WIDTH");
    const char * env_h = getenv("LV_SIM_WINDOW_HEIGHT");
    /* Default values */
    {
        char default_w[16];
        char default_h[16];
        snprintf(default_w, sizeof(default_w), "%d", cfg->sim_window_width);
        snprintf(default_h, sizeof(default_h), "%d", cfg->sim_window_height);
        settings.window_width = atoi(env_w ? env_w : default_w);
        settings.window_height = atoi(env_h ? env_h : default_h);
    }

    /* Parse the command-line options. */
    while((opt = getopt(argc, argv, "b:fmW:H:BVha")) != -1) {
        switch(opt) {
            case 'h':
                print_usage();
                exit(EXIT_SUCCESS);
                break;
            case 'V':
                print_lvgl_version();
                exit(EXIT_SUCCESS);
                break;
            case 'B':
                driver_backends_print_supported();
                exit(EXIT_SUCCESS);
                break;
            case 'b':
                if(driver_backends_is_supported(optarg) == 0) {
                    die("error no such backend: %s\n", optarg);
                }
                selected_backend = strdup(optarg);
                break;
            case 'f':
                settings.fullscreen = true;
                break;
            case 'm':
                settings.maximize = true;
                break;
            case 'W':
                settings.window_width = atoi(optarg);
                break;
            case 'H':
                settings.window_height = atoi(optarg);
                break;
            case 'a':
                /* Auto-start backend process - TODO */
                break;
            case ':':
                print_usage();
                die("Option -%c requires an argument.\n", optopt);
                break;
            case '?':
                print_usage();
                die("Unknown option -%c.\n", optopt);
        }
    }
}

/**
 * @brief Entry point - UI 进程
 * @description Start the UI application with IPC connection to Backend
 * @param argc the count of arguments in argv
 * @param argv The arguments
 */
int main(int argc, char ** argv)
{
    pthread_t tick_thread;
    lv_timer_t * mw_timer;

    app_config_init();
    setenv("TZ", "CST-8", 0);
    tzset();
    configure_simulator(argc, argv);

    /* Initialize LVGL. */
    lv_init();

#if LV_USE_SUNXI_G2D
    /* 注册全志 g2d draw unit + 装 ION draw buf handlers + 打开 /dev/g2d。
     * 须在 display 创建前调用：handlers 先装好，display 的 draw_buf 分配才会走 ION 入 buf_map。 */
    lv_draw_sunxi_g2d_init();
#endif

    /* Initialize Middleware (UI 进程模式 - 作为客户端连接 Backend) */
    if (mw_init(false) != 0) {
        fprintf(stderr, "\n[UI] Failed to connect to backend process!\n");
        fprintf(stderr, "[UI] Please start the backend first:\n");
        fprintf(stderr, "[UI]   ./build/bin/lv_backend &\n");
        fprintf(stderr, "[UI] Or use auto-start option if available.\n\n");
        exit(EXIT_FAILURE);
    }

    /* Initialize the configured backend */
    if(driver_backends_init_backend(selected_backend) == -1) {
        die("Failed to initialize display backend");
    }

    /* Create LVGL tick increment thread */
    if(pthread_create(&tick_thread, NULL, lvgl_tick_thread, NULL) != 0) {
        die("Failed to create tick thread");
    }
    pthread_detach(tick_thread);

    /* Initialize System Settings (Load from file) */
    sys_settings_init();
    sys_backlight_apply_saved_setting();
    sys_volume_apply_saved_setting();
    app_manager_init();

    /* Enable for EVDEV support */
#if LV_USE_EVDEV
    if(driver_backends_init_backend("EVDEV") == -1) {
        die("Failed to initialize evdev");
    }
#endif

    /* Create UI */
    ui_init();

    /* Force a full-screen redraw on the first frame so that no boot-play
     * residue remains visible after the handover. */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    write_boot_progress(100);

    /* Create a timer to process Backend->UI messages periodically (every 10ms)
     * This runs in the LVGL/UI thread context */
    mw_timer = lv_timer_create(mw_timer_callback, 10, NULL);
    if(!mw_timer) {
        die("Failed to create middleware timer");
    }

    /* Create a timer to send heartbeat to Backend (every 5 seconds) */
    lv_timer_t * heartbeat_timer = lv_timer_create(heartbeat_timer_callback, 5000, NULL);
    if(!heartbeat_timer) {
        die("Failed to create heartbeat timer");
    }

    /* Create a timer for NumberFlow animation update (every 16ms ~60fps) */
    lv_timer_t * number_flow_timer = lv_timer_create(number_flow_timer_callback, 16, NULL);
    if(!number_flow_timer) {
        die("Failed to create number_flow timer");
    }

    /* Enter the run loop of the selected backend
     * This loop handles display refresh and input events. */
    driver_backends_run_loop();

    /* Cleanup (not reached in normal operation) */
    mw_deinit();

    return 0;
}
