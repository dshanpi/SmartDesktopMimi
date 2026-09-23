#include "service_hdmi_preview.h"

#include "../middleware/middleware.h"
#include "backend_types.h"
#include "log/app_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define HDMI_PREVIEW_BIN_DEFAULT "/usr/bin/hdmi_preview"
#define HDMI_PREVIEW_STDIO_FILE "/dev/null"
#define HDMI_STATUS_SOCKET_DEFAULT "/tmp/hdmi_preview_status.sock"
#define HDMI_STATUS_INTERVAL_US 1000000
#define HDMI_IPKVM_OWNER_DEFAULT "/var/run/aitvbox/hdmi-ipkvm.lock"
#define HDMI_SHARED_CAPTURE_DEFAULT "/var/run/aitvbox/capture.sock"

static struct {
    atomic_bool running;
    pthread_t thread;
    pid_t preview_pid;
    int status_fd;
    hdmi_preview_status_t last_status;
    char bin_path[256];
    char socket_path[108];
} g_hdmi = {
    .running = false,
    .thread = 0,
    .preview_pid = -1,
    .status_fd = -1,
};

/* HDMI 检测开关（用户可控）。关闭时 monitor 线程与子进程都不跑。 */
static atomic_bool g_hdmi_enabled = false;
/* bin 可执行检查结果：不可执行则 set_enabled(true) 无效。 */
static bool g_hdmi_available = false;
static bool g_shared_capture = false;
static pthread_mutex_t g_hdmi_state_lock = PTHREAD_MUTEX_INITIALIZER;

static bool ipkvm_owns_hdmi(void)
{
    const char *path = getenv("AITVBOX_HDMI_OWNER_FILE");
    if (!path || !path[0]) {
        path = HDMI_IPKVM_OWNER_DEFAULT;
    }
    return access(path, F_OK) == 0;
}

static int shared_capture_command(const char *command,
                                  char *response,
                                  size_t response_size)
{
    const char *socket_path = getenv("AITVBOX_CAPTURE_SOCKET");
    struct sockaddr_un address;
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    int fd;
    ssize_t length;

    if (!command || !response || response_size < 2) return -1;
    if (!socket_path || !socket_path[0]) {
        socket_path = HDMI_SHARED_CAPTURE_DEFAULT;
    }

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        write(fd, command, strlen(command)) != (ssize_t)strlen(command)) {
        close(fd);
        return -1;
    }
    length = read(fd, response, response_size - 1);
    close(fd);
    if (length <= 0) return -1;
    response[length] = '\0';
    return 0;
}

static int shared_capture_set_display(bool enabled)
{
    char response[96];
    return shared_capture_command(enabled ? "DISPLAY 1\n" : "DISPLAY 0\n",
                                  response, sizeof(response));
}

static int shared_capture_status(bool *live, bool *display_enabled)
{
    char response[768];
    if (!live || !display_enabled ||
        shared_capture_command("STATUS\n", response, sizeof(response)) != 0) {
        return -1;
    }
    *live = strstr(response, "\"state\":\"live\"") != NULL;
    *display_enabled = strstr(response, "\"display\":true") != NULL;
    return 0;
}

static const char *state_name(int32_t state)
{
    switch ((hdmi_preview_state_t)state) {
        case HDMI_PREVIEW_STATE_IDLE: return "idle";
        case HDMI_PREVIEW_STATE_LOCKED: return "locked";
        case HDMI_PREVIEW_STATE_RUNNING: return "running";
        case HDMI_PREVIEW_STATE_NO_SIGNAL: return "no_signal";
        case HDMI_PREVIEW_STATE_ERROR: return "error";
        default: return "unknown";
    }
}

static bool status_equal(const hdmi_preview_status_t *a, const hdmi_preview_status_t *b)
{
    return a->active == b->active &&
           a->signal_locked == b->signal_locked &&
           a->enabled == b->enabled &&
           a->state == b->state &&
           a->exit_code == b->exit_code;
}

static void publish_status(bool active, bool signal_locked, hdmi_preview_state_t state, int32_t exit_code)
{
    hdmi_preview_status_t status;

    memset(&status, 0, sizeof(status));
    status.active = active;
    status.signal_locked = signal_locked;
    status.enabled = atomic_load(&g_hdmi_enabled);
    status.state = state;
    status.exit_code = exit_code;

    pthread_mutex_lock(&g_hdmi_state_lock);
    bool changed = !status_equal(&status, &g_hdmi.last_status);
    if (changed) {
        g_hdmi.last_status = status;
    }
    pthread_mutex_unlock(&g_hdmi_state_lock);

    if (changed) {
        APP_LOGI("hdmi-preview", "status active=%d locked=%d state=%s exit=%d",
                 status.active, status.signal_locked, state_name(status.state), status.exit_code);
    }

    mw_publish(TOPIC_HDMI_PREVIEW_STATUS, &status, sizeof(status), MW_DIR_BACKEND_TO_UI);
}

static void publish_last_status(void)
{
    hdmi_preview_status_t status;

    pthread_mutex_lock(&g_hdmi_state_lock);
    g_hdmi.last_status.enabled = atomic_load(&g_hdmi_enabled);
    status = g_hdmi.last_status;
    pthread_mutex_unlock(&g_hdmi_state_lock);
    mw_publish(TOPIC_HDMI_PREVIEW_STATUS, &status, sizeof(status),
               MW_DIR_BACKEND_TO_UI);
}

static void redirect_child_output(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0) {
        fd = open("/dev/null", O_WRONLY);
    }
    if (fd < 0) {
        return;
    }

    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}

static int decode_wait_status(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    return -1;
}

static pid_t start_preview_process(void)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        APP_LOGE("hdmi-preview", "fork preview failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) {
            _exit(1);
        }
        setenv("HDMI_PREVIEW_STATUS_SOCK", g_hdmi.socket_path, 1);
        redirect_child_output(HDMI_PREVIEW_STDIO_FILE);
        /* Display Mode needs the long-running snapshot/status service and
         * local video/audio output. Plain --service stays available to the
         * browser Agent as a headless capture provider.
         */
        if (g_shared_capture) {
            execl(g_hdmi.bin_path, g_hdmi.bin_path,
                  "--audio-only", (char *)NULL);
        } else {
            execl(g_hdmi.bin_path, g_hdmi.bin_path,
                  "--service", "--display", (char *)NULL);
        }
        _exit(127);
    }

    setpgid(pid, pid);
    g_hdmi.preview_pid = pid;
    APP_LOGI("hdmi-preview", "started hdmi_preview pid=%d", pid);
    return pid;
}

static void stop_preview_process(void)
{
    pid_t pid = g_hdmi.preview_pid;
    int status;

    if (pid <= 0) {
        return;
    }

    kill(-pid, SIGTERM);
    for (int i = 0; i < 10; i++) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid || (ret < 0 && errno == ECHILD)) {
            g_hdmi.preview_pid = -1;
            return;
        }
        usleep(100000);
    }

    kill(-pid, SIGKILL);
    waitpid(pid, &status, 0);
    g_hdmi.preview_pid = -1;
}

static int open_status_socket(void)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        APP_LOGE("hdmi-preview", "status socket create failed: %s", strerror(errno));
        return -1;
    }

    unlink(g_hdmi.socket_path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_hdmi.socket_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        APP_LOGE("hdmi-preview", "status socket bind %s failed: %s",
                 g_hdmi.socket_path, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void publish_from_status_text(const char *msg)
{
    if (!msg) return;

    if (strncmp(msg, "LOCKED", 6) == 0) {
        publish_status(true, true, HDMI_PREVIEW_STATE_LOCKED, 0);
    } else if (strncmp(msg, "RUNNING", 7) == 0) {
        publish_status(true, true, HDMI_PREVIEW_STATE_RUNNING, 0);
    } else if (strncmp(msg, "NO_SIGNAL", 9) == 0) {
        publish_status(false, false, HDMI_PREVIEW_STATE_NO_SIGNAL, 1);
    } else if (strncmp(msg, "ERROR", 5) == 0) {
        publish_status(false, false, HDMI_PREVIEW_STATE_ERROR, -1);
    } else {
        APP_LOGW("hdmi-preview", "unknown hdmi_preview status: %s", msg);
    }
}

static bool preview_process_alive(pid_t pid)
{
    int status;
    pid_t ret;

    if (pid <= 0) return false;

    ret = waitpid(pid, &status, WNOHANG);
    if (ret == 0) return true;
    if (ret == pid) {
        APP_LOGW("hdmi-preview", "hdmi_preview service exited with %d", decode_wait_status(status));
        g_hdmi.preview_pid = -1;
        return false;
    }
    if (errno != EINTR) {
        APP_LOGE("hdmi-preview", "wait preview service failed: %s", strerror(errno));
        g_hdmi.preview_pid = -1;
        return false;
    }

    return true;
}

static void *monitor_thread_main(void *arg)
{
    (void)arg;

    publish_status(false, false, HDMI_PREVIEW_STATE_IDLE, 0);

    while (atomic_load(&g_hdmi.running)) {
        fd_set fds;
        struct timeval tv;
        int ret;

        if (g_shared_capture) {
            bool live = false;
            bool display_enabled = false;
            if (g_hdmi.preview_pid <= 0) {
                (void)start_preview_process();
            } else if (!preview_process_alive(g_hdmi.preview_pid)) {
                publish_status(false, false, HDMI_PREVIEW_STATE_ERROR, -1);
                sleep(1);
                continue;
            }
            (void)shared_capture_set_display(true);
            if (shared_capture_status(&live, &display_enabled) == 0) {
                publish_status(live && display_enabled,
                               live,
                               live && display_enabled
                                   ? HDMI_PREVIEW_STATE_RUNNING
                                   : HDMI_PREVIEW_STATE_NO_SIGNAL,
                               0);
            } else {
                publish_status(false, false, HDMI_PREVIEW_STATE_NO_SIGNAL, 0);
            }
            sleep(1);
            continue;
        }

        if (ipkvm_owns_hdmi()) {
            stop_preview_process();
            publish_status(false, false, HDMI_PREVIEW_STATE_IDLE, 0);
            sleep(1);
            continue;
        }

        if (g_hdmi.preview_pid <= 0) {
            if (start_preview_process() < 0) {
                publish_status(false, false, HDMI_PREVIEW_STATE_ERROR, -errno);
                sleep(2);
                continue;
            }
            publish_status(false, false, HDMI_PREVIEW_STATE_NO_SIGNAL, 0);
        } else if (!preview_process_alive(g_hdmi.preview_pid)) {
            publish_status(false, false, HDMI_PREVIEW_STATE_ERROR, -1);
            sleep(1);
            continue;
        }

        FD_ZERO(&fds);
        FD_SET(g_hdmi.status_fd, &fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        ret = select(g_hdmi.status_fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(g_hdmi.status_fd, &fds)) {
            char buf[96];
            ssize_t n = recv(g_hdmi.status_fd, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                publish_from_status_text(buf);
            }
        } else if (ret == 0) {
            /* Always refresh enabled so a stale last_status cannot fight the UI switch. */
            publish_last_status();
        } else if (errno != EINTR) {
            APP_LOGE("hdmi-preview", "status socket select failed: %s", strerror(errno));
            sleep(1);
        }
    }

    stop_preview_process();
    publish_status(false, false, HDMI_PREVIEW_STATE_IDLE, 0);
    return NULL;
}

/* 拉起 monitor 线程（幂等：已在跑则跳过）。打开状态 socket + 起线程。 */
static bool start_monitor(void)
{
    if (atomic_load(&g_hdmi.running)) {
        return true;
    }
    if (!g_hdmi_available) {
        APP_LOGW("hdmi-preview", "binary not available, cannot start monitor");
        return false;
    }
    if (ipkvm_owns_hdmi()) {
        APP_LOGI("hdmi-preview", "IPKVM currently owns HDMI input; monitor will wait");
    }

    g_hdmi.status_fd = open_status_socket();
    if (g_hdmi.status_fd < 0) {
        return false;
    }

    atomic_store(&g_hdmi.running, true);
    if (pthread_create(&g_hdmi.thread, NULL, monitor_thread_main, NULL) != 0) {
        atomic_store(&g_hdmi.running, false);
        close(g_hdmi.status_fd);
        g_hdmi.status_fd = -1;
        unlink(g_hdmi.socket_path);
        APP_LOGE("hdmi-preview", "failed to start monitor thread");
        return false;
    }

    APP_LOGI("hdmi-preview", "monitor started, bin=%s sock=%s", g_hdmi.bin_path, g_hdmi.socket_path);
    return true;
}

/* 停止 monitor 线程（幂等：未在跑则跳过）。杀子进程、join、关 socket。 */
static void stop_monitor(void)
{
    if (!atomic_load(&g_hdmi.running)) {
        return;
    }

    atomic_store(&g_hdmi.running, false);
    /*
     * The monitor thread exclusively owns preview_pid and waitpid(). It wakes
     * from select within one second, stops the child, then exits. Keeping all
     * process lifecycle operations on that thread avoids double wait/kill
     * races during rapid HDMI/IPKVM ownership changes.
     */
    pthread_join(g_hdmi.thread, NULL);
    if (g_hdmi.status_fd >= 0) {
        close(g_hdmi.status_fd);
        g_hdmi.status_fd = -1;
    }
    unlink(g_hdmi.socket_path);
    g_hdmi.thread = 0;
    g_hdmi.preview_pid = -1;
    APP_LOGI("hdmi-preview", "monitor stopped");
}

void service_hdmi_preview_init(void)
{
    const char *env_path = getenv("HDMI_PREVIEW_BIN");
    const char *sock_path = getenv("HDMI_PREVIEW_STATUS_SOCK");
    const char *shared_capture = getenv("AITVBOX_SHARED_CAPTURE");

    memset(&g_hdmi.last_status, 0, sizeof(g_hdmi.last_status));
    g_hdmi.last_status.state = HDMI_PREVIEW_STATE_IDLE;
    g_hdmi.preview_pid = -1;
    g_hdmi.status_fd = -1;

    snprintf(g_hdmi.bin_path, sizeof(g_hdmi.bin_path), "%s",
             (env_path && env_path[0]) ? env_path : HDMI_PREVIEW_BIN_DEFAULT);
    snprintf(g_hdmi.socket_path, sizeof(g_hdmi.socket_path), "%s",
             (sock_path && sock_path[0]) ? sock_path : HDMI_STATUS_SOCKET_DEFAULT);

    atomic_store(&g_hdmi_enabled, false);
    g_shared_capture = shared_capture && strcmp(shared_capture, "1") == 0;

    if (!g_shared_capture && access(g_hdmi.bin_path, X_OK) != 0) {
        APP_LOGW("hdmi-preview", "%s is not executable, HDMI preview monitor disabled", g_hdmi.bin_path);
        g_hdmi_available = false;
        return;
    }
    g_hdmi_available = true;

    APP_LOGI("hdmi-preview",
             "initialized (detection off by default), bin=%s sock=%s shared_capture=%d",
             g_hdmi.bin_path, g_hdmi.socket_path, g_shared_capture);
}

void service_hdmi_preview_set_enabled(bool enable)
{
    atomic_store(&g_hdmi_enabled, enable);

    /*
     * Ack the UI immediately with the new enabled flag. stop_monitor() can
     * block for ~1s while joining the worker; if we publish only after that,
     * the top-bar switch feels stuck / ignores taps while pending.
     */
    publish_status(false, false, HDMI_PREVIEW_STATE_IDLE, 0);

    if (enable) {
        start_monitor();
    } else {
        stop_monitor();
        if (g_shared_capture) {
            (void)shared_capture_set_display(false);
        }
        /* Final idle snapshot after worker is fully stopped. */
        publish_status(false, false, HDMI_PREVIEW_STATE_IDLE, 0);
    }

    APP_LOGI("hdmi-preview", "set_enabled=%d running=%d",
             enable, atomic_load(&g_hdmi.running));
}

void service_hdmi_preview_send_status(void)
{
    /* 重发最近一帧快照，并刷新 enabled 字段以反映当前开关态。 */
    publish_last_status();
}

void service_hdmi_preview_deinit(void)
{
    stop_monitor();
    if (g_shared_capture) {
        (void)shared_capture_set_display(false);
    }
    atomic_store(&g_hdmi_enabled, false);
    g_hdmi_available = false;
}
