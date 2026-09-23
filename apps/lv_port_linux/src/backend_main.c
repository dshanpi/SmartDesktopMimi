/**
 * @file backend_main.c
 * @brief Backend 进程主入口 - 独立进程运行
 *
 * 架构说明：
 * - 独立于 UI 进程运行
 * - 通过 Unix Domain Socket 与 UI 通信
 * - 仅负责进程生命周期与组件装配
 * - 命令路由和服务生命周期分别由 backend/ 模块负责
 *
 * 使用方法:
 *   ./build/bin/lv_backend
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "middleware/middleware.h"
#include "backend/backend_command_router.h"
#include "backend/backend_runtime.h"
#include "backend/backend_v2_server.h"
#include "system/config/app_config.h"
#include "system/log/app_log.h"
#include "system/backlight_control.h"
#include "system/settings.h"
#include "system/volume_control.h"
#include "platform/platform_capabilities.h"

/*****************************************************************************
 *                                 全局状态
 *****************************************************************************/

static struct {
    atomic_bool running;
    atomic_bool should_exit;
    atomic_bool heartbeat_thread_started;
    pthread_t heartbeat_thread;
} g_backend = {
    .running = false,
    .should_exit = false,
    .heartbeat_thread_started = false,
};

/*****************************************************************************
 *                                 心跳线程
 *****************************************************************************/

/**
 * @brief 心跳线程 - 定期发送心跳到 UI
 * @note 当前版本不需要，保留用于未来扩展
 */
static void * heartbeat_thread_func(void * arg) {
    (void)arg;

    while (atomic_load(&g_backend.running) && !atomic_load(&g_backend.should_exit)) {
        /* 未来可以发送心跳到 UI 进程 */
        usleep(1000000); /* 1 秒 */
    }

    return NULL;
}

static int start_heartbeat_thread(void) {
    atomic_store(&g_backend.running, true);

    if (pthread_create(&g_backend.heartbeat_thread, NULL, heartbeat_thread_func, NULL) != 0) {
        perror("pthread_create heartbeat");
        atomic_store(&g_backend.running, false);
        return -1;
    }

    atomic_store(&g_backend.heartbeat_thread_started, true);
    return 0;
}

static void stop_heartbeat_thread(void) {
    atomic_store(&g_backend.should_exit, true);
    atomic_store(&g_backend.running, false);

    if (atomic_load(&g_backend.heartbeat_thread_started)) {
        pthread_join(g_backend.heartbeat_thread, NULL);
        atomic_store(&g_backend.heartbeat_thread_started, false);
    }
}

/*****************************************************************************
 *                                 信号处理
 *****************************************************************************/

static void signal_handler(int sig) {
    (void)sig;
    atomic_store(&g_backend.should_exit, true);
}

static void setup_signal_handlers(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
}

/*****************************************************************************
 *                                 PID 文件管理
 *****************************************************************************/

#define PID_FILE_PATH "/tmp/lv_backend.pid"

static void write_pid_file(void) {
    FILE *f = fopen(PID_FILE_PATH, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

static void remove_pid_file(void) {
    unlink(PID_FILE_PATH);
}

/*****************************************************************************
 *                                 主函数
 *****************************************************************************/

int main(int argc, char * argv[]) {
    (void)argc;
    (void)argv;
    sys_settings_t *settings;
    backend_runtime_options_t runtime_options;
    const aitvbox_platform_descriptor_t *platform;

    app_config_init();
    sys_settings_init();
    settings = sys_settings_get();
    platform = aitvbox_platform_get_descriptor();
    if (!platform || platform->abi_version != AITVBOX_PLATFORM_ABI_VERSION) {
        fprintf(stderr, "[Backend] Platform adapter ABI mismatch\n");
        return EXIT_FAILURE;
    }
    APP_LOGI("backend", "LVGL Backend Process starting");
    APP_LOGI("backend", "platform=%s soc=%s provider=%s abi=%u capabilities=0x%llx",
             platform->platform_id, platform->soc, platform->provider_version,
             platform->abi_version,
             (unsigned long long)platform->capabilities);

    /* 写入 PID 文件 */
    write_pid_file();

    /* 设置信号处理 */
    setup_signal_handlers();

    /* 初始化中间件 (Backend 模式) */
    if (mw_init(true) != 0) {
        fprintf(stderr, "[Backend] Failed to initialize middleware\n");
        return EXIT_FAILURE;
    }

    /* V2 is additive during the compatibility window. V1 remains available
     * if the new endpoint cannot be published on an older rootfs. */
    if (backend_v2_server_start(platform) != 0) {
        APP_LOGW("backend", "IPC v2 unavailable; continuing with V1 compatibility");
    }

    /* Publish the IPC endpoint before touching display and ALSA controls.
     * Those drivers may still be probing during S99; they must never hold the
     * boot screen at 84% merely because a best-effort setting is delayed. */
    APP_LOGI("backend", "IPC ready; applying hardware settings");
    sys_backlight_apply_saved_setting();
    sys_volume_apply_saved_setting();

    /* Application layer: register UI command routes. */
    if (backend_command_router_register() != 0) {
        fprintf(stderr, "[Backend] Failed to register command routes\n");
        mw_deinit();
        remove_pid_file();
        return EXIT_FAILURE;
    }

    /* Runtime layer: own service startup, polling and shutdown order. */
    runtime_options.wifi_enabled = settings->wifi_enabled;
    runtime_options.bluetooth_enabled = settings->bluetooth_enabled;
    runtime_options.hdmi_enabled = settings->hdmi_enabled;
    if (backend_runtime_start(&runtime_options) != 0) {
        fprintf(stderr, "[Backend] Failed to start service runtime\n");
        mw_deinit();
        remove_pid_file();
        return EXIT_FAILURE;
    }

    /* 启动心跳线程 */
    if (start_heartbeat_thread() != 0) {
        fprintf(stderr, "[Backend] Failed to start heartbeat thread\n");
    }

    APP_LOGI("backend", "ready to accept connections");

    // 5. 循环处理
    while (!atomic_load(&g_backend.should_exit)) {
        mw_process();
        mw_backend_check_heartbeat_timeouts();
        backend_runtime_tick();
        usleep(10000);
    }

    /* 清理 */
    APP_LOGI("backend", "shutting down");

    stop_heartbeat_thread();

    backend_runtime_stop();

    backend_v2_server_stop();

    mw_deinit();
    remove_pid_file();

    APP_LOGI("backend", "exit ok");
    return EXIT_SUCCESS;
}
