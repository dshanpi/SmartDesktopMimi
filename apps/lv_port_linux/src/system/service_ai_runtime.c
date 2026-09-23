#include "service_ai.h"

#include "../middleware/middleware.h"
#include "log/app_log.h"
#include "service_bt.h"
#include "service_led.h"
#include "service_wifi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define XIAOZHI_UI_PORT_UP 5678
#define XIAOZHI_UI_PORT_DOWN 5679
#define AI_ONLINE_TIMEOUT_MS 30000
#define AI_LOG_HEARTBEAT_MS 60000
#define AI_PROCESS_RESTART_BACKOFF_MS 2000
#define AI_BT_RESUME_RETRY_DELAY_MS 700
#define AI_BT_RESUME_RETRY_COUNT 2
#define XIAOZHI_CONTROL_CENTER_BIN_DEFAULT "/mnt/UDISK/control_center"
#define XIAOZHI_SOUND_APP_BIN_DEFAULT "/mnt/UDISK/sound_app"
#define XIAOZHI_SHERPA_LIB_DIR_DEFAULT "/mnt/UDISK/sherpa-onnx-aarch64-shared-cpu/lib"
#define XIAOZHI_SHERPA_MODEL_DIR_DEFAULT "/mnt/UDISK/models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile"
#define XIAOZHI_CONTROL_CENTER_LOG_DEFAULT "/tmp/control_center.log"
#define XIAOZHI_SOUND_APP_LOG_DEFAULT "/tmp/sound_app.log"
#define TUYA_CHAT_BOT_BIN_DEFAULT "/usr/bin/your_chat_bot_QIO_1.0.1.bin"
#define TUYA_CHAT_BOT_LOG_DEFAULT "/tmp/tuya_chat_bot.log"
#define TUYA_LICENSE_FILE_DEFAULT "/factory/tuya/license.env"

typedef enum {
    AI_RUNTIME_TUYA = 0,
    AI_RUNTIME_XIAOZHI,
} ai_runtime_t;

static struct {
    bool initialized;
    bool manage_processes;
    ai_runtime_t runtime;
    int udp_fd;
    int64_t last_rx_ms;
    int64_t last_log_ms;
    ai_status_t status;
    ai_status_t last_logged_status;
    ai_text_t last_text;
    bool has_logged_status;
    bool has_last_text;
    pid_t control_center_pid;
    pid_t sound_app_pid;
    pid_t tuya_pid;
    int64_t next_control_center_restart_ms;
    int64_t next_sound_app_restart_ms;
    int64_t next_tuya_restart_ms;
    int64_t next_bt_resume_retry_ms;
    int bt_resume_retries_left;
    bool tuya_license_valid;
    bool tuya_license_state_known;
    char control_center_bin[256];
    char sound_app_bin[256];
    char tuya_bin[256];
    char sherpa_lib_dir[256];
    char sherpa_model_dir[256];
    char control_center_log[256];
    char sound_app_log[256];
    char tuya_log[256];
} g_ai = {
    .udp_fd = -1,
    .control_center_pid = -1,
    .sound_app_pid = -1,
    .tuya_pid = -1,
};

static bool control_center_is_online(void);
static void resume_bt_if_paused_by_ai(void);

static bool ai_network_ready(void)
{
    return service_wifi_is_network_ready();
}

static int64_t now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void copy_env_or_default(char *dst, size_t dst_size,
                                const char *env_name, const char *fallback)
{
    const char *value = getenv(env_name);

    if (!dst || dst_size == 0) return;
    if (!value || value[0] == '\0') value = fallback;
    snprintf(dst, dst_size, "%s", value ? value : "");
}

static bool env_flag_enabled(const char *name, bool default_value)
{
    const char *value = getenv(name);

    if (!value || value[0] == '\0') return default_value;
    if (strcmp(value, "0") == 0 ||
        strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0) {
        return false;
    }
    return true;
}

static ai_runtime_t runtime_from_env(void)
{
    const char *value = getenv("LV_AI_RUNTIME");

    if (value && strcasecmp(value, "xiaozhi") == 0) {
        return AI_RUNTIME_XIAOZHI;
    }
    return AI_RUNTIME_TUYA;
}

static const char *runtime_name(void)
{
    return g_ai.runtime == AI_RUNTIME_XIAOZHI ? "xiaozhi" : "tuya";
}

static bool is_tuya_runtime(void)
{
    return g_ai.runtime == AI_RUNTIME_TUYA;
}

static char *trim_license_field(char *value)
{
    char *end;

    while (*value == ' ' || *value == '\t') value++;
    end = value + strlen(value);
    while (end > value &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = '\0';
    }
    return value;
}

static bool license_value_valid(const char *value, size_t expected_length)
{
    size_t i;

    if (strlen(value) != expected_length) return false;
    for (i = 0; i < expected_length; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (ch <= 0x20 || ch > 0x7e) return false;
    }
    return true;
}

/* Keep this preflight in lockstep with TuyaOpen's tkl_flash parser.  The
 * backend must not call a credential-blocked process "online" merely because
 * its waiting loop is alive.  Credential contents are intentionally never
 * logged or copied outside this stack buffer. */
static bool tuya_license_file_valid(void)
{
    const char *path = getenv("TUYA_LICENSE_FILE");
    struct stat file_stat;
    char content[256];
    size_t used = 0;
    bool uuid_seen = false;
    bool authkey_seen = false;
    int flags = O_RDONLY;
    int fd;
    char *saveptr = NULL;
    char *line;

    if (!path || path[0] == '\0') path = TUYA_LICENSE_FILE_DEFAULT;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(path, flags);
    if (fd < 0) return false;
    if (fstat(fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_uid != 0 ||
        (file_stat.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        close(fd);
        return false;
    }
    while (used < sizeof(content) - 1) {
        ssize_t count = read(fd, content + used, sizeof(content) - 1 - used);
        if (count < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        if (count == 0) break;
        used += (size_t)count;
    }
    if (used == sizeof(content) - 1) {
        char extra;
        if (read(fd, &extra, 1) != 0) {
            close(fd);
            return false;
        }
    }
    close(fd);
    content[used] = '\0';

    for (line = strtok_r(content, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        char *key = trim_license_field(line);
        char *separator;
        char *value;

        if (*key == '\0' || *key == '#') continue;
        separator = strchr(key, '=');
        if (!separator) return false;
        *separator = '\0';
        key = trim_license_field(key);
        value = trim_license_field(separator + 1);
        if (strcmp(key, "TUYA_OPENSDK_UUID") == 0) {
            if (uuid_seen || !license_value_valid(value, 20)) return false;
            uuid_seen = true;
        } else if (strcmp(key, "TUYA_OPENSDK_AUTHKEY") == 0) {
            if (authkey_seen || !license_value_valid(value, 32)) return false;
            authkey_seen = true;
        } else {
            return false;
        }
    }
    return uuid_seen && authkey_seen;
}

static bool refresh_tuya_license_state(void)
{
    bool valid = tuya_license_file_valid();

    if (!g_ai.tuya_license_state_known || valid != g_ai.tuya_license_valid) {
        APP_LOGI("ai", "tuya factory license %s",
                 valid ? "passed preflight" : "is missing or invalid");
        g_ai.tuya_license_state_known = true;
        g_ai.tuya_license_valid = valid;
    }
    return valid;
}

static void redirect_child_output(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0) {
        fd = open("/dev/null", O_WRONLY);
    }
    if (fd < 0) return;

    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);
}

static int decode_wait_status(int status)
{
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -1;
}

static bool child_process_alive(pid_t *pid, const char *name, bool log_exit)
{
    int status;
    pid_t ret;

    if (!pid || *pid <= 0) return false;

    ret = waitpid(*pid, &status, WNOHANG);
    if (ret == 0) return true;
    if (ret == *pid) {
        if (log_exit) {
            APP_LOGW("ai", "%s exited with %d", name, decode_wait_status(status));
        }
        *pid = -1;
        return false;
    }
    if (errno != EINTR) {
        APP_LOGE("ai", "wait %s failed: %s", name, strerror(errno));
        *pid = -1;
        return false;
    }

    return true;
}

static bool sound_app_is_running(void)
{
    if (is_tuya_runtime()) {
        return g_ai.status.state == AI_STATE_LISTENING ||
               g_ai.status.state == AI_STATE_THINKING ||
               g_ai.status.state == AI_STATE_SPEAKING;
    }

    if (g_ai.manage_processes) {
        return child_process_alive(&g_ai.sound_app_pid, "sound_app", false);
    }

    return g_ai.status.control_center_running;
}

static void stop_child_process(pid_t *pid, const char *name)
{
    int status;

    if (!pid || *pid <= 0) return;

    kill(-(*pid), SIGTERM);
    for (int i = 0; i < 10; i++) {
        pid_t ret = waitpid(*pid, &status, WNOHANG);
        if (ret == *pid || (ret < 0 && errno == ECHILD)) {
            *pid = -1;
            return;
        }
        usleep(100000);
    }

    APP_LOGW("ai", "forcing %s to exit", name);
    kill(-(*pid), SIGKILL);
    waitpid(*pid, &status, 0);
    *pid = -1;
}

static pid_t start_tuya_process(void)
{
    pid_t pid = fork();

    if (pid < 0) {
        APP_LOGE("ai", "fork tuya chat bot failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(1);
        redirect_child_output(g_ai.tuya_log);
        execl(g_ai.tuya_bin, g_ai.tuya_bin, (char *)NULL);
        _exit(127);
    }

    setpgid(pid, pid);
    g_ai.tuya_pid = pid;
    APP_LOGI("ai", "started tuya chat bot pid=%d", pid);
    return pid;
}

static pid_t start_control_center_process(void)
{
    pid_t pid = fork();

    if (pid < 0) {
        APP_LOGE("ai", "fork control_center failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(1);
        redirect_child_output(g_ai.control_center_log);
        execl(g_ai.control_center_bin, g_ai.control_center_bin, (char *)NULL);
        _exit(127);
    }

    setpgid(pid, pid);
    g_ai.control_center_pid = pid;
    APP_LOGI("ai", "started control_center pid=%d", pid);
    return pid;
}

static pid_t start_sound_app_process(void)
{
    pid_t pid = fork();
    char ld_library_path[512];
    const char *prev_ld_library_path = getenv("LD_LIBRARY_PATH");

    if (pid < 0) {
        APP_LOGE("ai", "fork sound_app failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(1);

        if (g_ai.sherpa_lib_dir[0] != '\0') {
            if (prev_ld_library_path && prev_ld_library_path[0] != '\0') {
                snprintf(ld_library_path, sizeof(ld_library_path), "%s:%s",
                         g_ai.sherpa_lib_dir, prev_ld_library_path);
            } else {
                snprintf(ld_library_path, sizeof(ld_library_path), "%s",
                         g_ai.sherpa_lib_dir);
            }
            setenv("LD_LIBRARY_PATH", ld_library_path, 1);
        }
        if (g_ai.sherpa_model_dir[0] != '\0') {
            setenv("SHERPA_ONNX_KWS_MODEL_DIR", g_ai.sherpa_model_dir, 1);
        }

        redirect_child_output(g_ai.sound_app_log);
        execl(g_ai.sound_app_bin, g_ai.sound_app_bin, "--wake-notify", (char *)NULL);
        _exit(127);
    }

    setpgid(pid, pid);
    g_ai.sound_app_pid = pid;
    APP_LOGI("ai", "started sound_app pid=%d", pid);
    return pid;
}

static void refresh_tuya_process_status(void)
{
    bool alive;

    if (!g_ai.manage_processes) return;

    alive = child_process_alive(&g_ai.tuya_pid, "tuya_chat_bot", true);
    if (!alive && g_ai.next_tuya_restart_ms == 0) {
        g_ai.next_tuya_restart_ms = now_ms() + AI_PROCESS_RESTART_BACKOFF_MS;
    }

    /*
     * 进程退出后必须清掉上一次会话留下的绑定/联网结果。否则新 License
     * 尚未完成初始化，仅因进程被重新拉起就可能沿用旧 bind_url 误报产测通过。
     */
    if (!alive) {
        g_ai.status.tuya_bound = false;
        g_ai.status.tuya_bind_qr_pending = false;
        g_ai.status.bind_url[0] = '\0';
    }

    g_ai.status.control_center_running = alive;
    g_ai.status.sound_app_running = sound_app_is_running();
}

static void refresh_managed_process_status(void)
{
    bool control_alive;
    bool sound_alive;

    if (!g_ai.manage_processes) return;

    control_alive = child_process_alive(&g_ai.control_center_pid, "control_center", true);
    sound_alive = child_process_alive(&g_ai.sound_app_pid, "sound_app", true);

    if (!control_alive && g_ai.next_control_center_restart_ms == 0) {
        g_ai.next_control_center_restart_ms = now_ms() + AI_PROCESS_RESTART_BACKOFF_MS;
    }
    if (!sound_alive && g_ai.next_sound_app_restart_ms == 0) {
        g_ai.next_sound_app_restart_ms = now_ms() + AI_PROCESS_RESTART_BACKOFF_MS;
    }

    g_ai.status.control_center_running = control_alive || control_center_is_online();
    g_ai.status.sound_app_running = sound_alive;
}

static void ensure_runtime_processes(void)
{
    int64_t now;

    if (g_ai.manage_processes && is_tuya_runtime() &&
        !refresh_tuya_license_state()) {
        stop_child_process(&g_ai.tuya_pid, "tuya_chat_bot");
        g_ai.next_tuya_restart_ms = 0;
        g_ai.last_rx_ms = 0;
        g_ai.status.control_center_running = false;
        g_ai.status.sound_app_running = false;
        g_ai.status.tuya_bound = false;
        g_ai.status.tuya_bind_qr_pending = false;
        g_ai.status.bind_url[0] = '\0';
        g_ai.status.state = ai_network_ready() ? AI_STATE_CONNECTING :
                                                 AI_STATE_NETWORK_UNAVAILABLE;
        g_ai.status.last_error_code = ai_network_ready() ? -EACCES : -ENETDOWN;
        resume_bt_if_paused_by_ai();
        return;
    }

    if (!ai_network_ready()) {
        if (g_ai.manage_processes && !is_tuya_runtime()) {
            stop_child_process(&g_ai.sound_app_pid, "sound_app");
            stop_child_process(&g_ai.control_center_pid, "control_center");
            g_ai.next_control_center_restart_ms = 0;
            g_ai.next_sound_app_restart_ms = 0;
        }
        g_ai.last_rx_ms = 0;
        if (is_tuya_runtime()) {
            refresh_tuya_process_status();
        } else {
            g_ai.status.control_center_running = false;
        }
        g_ai.status.sound_app_running = false;
        g_ai.status.last_error_code = -ENETDOWN;
        if (g_ai.status.state != AI_STATE_NETWORK_UNAVAILABLE) {
            APP_LOGI("ai", "network unavailable, keep %s offline", runtime_name());
            g_ai.status.state = AI_STATE_NETWORK_UNAVAILABLE;
            resume_bt_if_paused_by_ai();
        }
        return;
    }

    if (g_ai.status.state == AI_STATE_NETWORK_UNAVAILABLE) {
        APP_LOGI("ai", "network ready, start %s service", runtime_name());
        g_ai.status.state = AI_STATE_CONNECTING;
        g_ai.status.last_error_code = 0;
    }

    if (!g_ai.manage_processes) return;

    if (is_tuya_runtime()) {
        refresh_tuya_process_status();
        now = now_ms();

        if (g_ai.tuya_pid <= 0 && now >= g_ai.next_tuya_restart_ms) {
            start_tuya_process();
            g_ai.status.control_center_running = g_ai.tuya_pid > 0;
            g_ai.next_tuya_restart_ms =
                g_ai.tuya_pid > 0 ? 0 : now + AI_PROCESS_RESTART_BACKOFF_MS;
        }

        if (g_ai.status.control_center_running &&
            (g_ai.status.state == AI_STATE_CONNECTING ||
             g_ai.status.state == AI_STATE_NETWORK_UNAVAILABLE) &&
            ai_network_ready()) {
            g_ai.status.state = AI_STATE_IDLE;
            g_ai.status.last_error_code = 0;
        }
        return;
    }

    refresh_managed_process_status();
    now = now_ms();

    if (g_ai.control_center_pid <= 0 &&
        now >= g_ai.next_control_center_restart_ms) {
        start_control_center_process();
        g_ai.status.control_center_running = g_ai.control_center_pid > 0;
        g_ai.next_control_center_restart_ms =
            g_ai.control_center_pid > 0 ? 0 : now + AI_PROCESS_RESTART_BACKOFF_MS;
    }

    if (g_ai.sound_app_pid <= 0 &&
        now >= g_ai.next_sound_app_restart_ms) {
        start_sound_app_process();
        g_ai.status.sound_app_running = g_ai.sound_app_pid > 0;
        g_ai.next_sound_app_restart_ms =
            g_ai.sound_app_pid > 0 ? 0 : now + AI_PROCESS_RESTART_BACKOFF_MS;
    }

    if (!control_center_is_online() &&
        g_ai.status.control_center_running &&
        g_ai.status.state == AI_STATE_IDLE) {
        g_ai.status.state = AI_STATE_CONNECTING;
    }
}

static const char *ai_state_name(ai_state_t state)
{
    switch (state) {
        case AI_STATE_IDLE: return "idle";
        case AI_STATE_CONNECTING: return "connecting";
        case AI_STATE_LISTENING: return "listening";
        case AI_STATE_THINKING: return "thinking";
        case AI_STATE_SPEAKING: return "speaking";
        case AI_STATE_NETWORK_UNAVAILABLE: return "network_unavailable";
        case AI_STATE_ERROR: return "error";
        default: return "unknown";
    }
}

static bool status_equal(const ai_status_t *a, const ai_status_t *b)
{
    return a->state == b->state &&
           a->chat_mode == b->chat_mode &&
           a->free_chat_active == b->free_chat_active &&
           a->control_center_running == b->control_center_running &&
           a->sound_app_running == b->sound_app_running &&
           a->bt_paused_by_ai == b->bt_paused_by_ai &&
           a->tuya_bound == b->tuya_bound &&
           a->tuya_bind_qr_pending == b->tuya_bind_qr_pending &&
           a->last_error_code == b->last_error_code &&
           strcmp(a->emotion, b->emotion) == 0 &&
           strcmp(a->bind_url, b->bind_url) == 0;
}

static void log_status_if_needed(bool force)
{
    int64_t now = now_ms();
    bool changed = !g_ai.has_logged_status ||
                   !status_equal(&g_ai.status, &g_ai.last_logged_status);
    bool heartbeat = (now - g_ai.last_log_ms) >= AI_LOG_HEARTBEAT_MS;

    if (!force && !changed && !heartbeat) return;

    APP_LOGI("ai", "state=%s mode=%d free=%d control=%s audio=%s bt_paused=%d bound=%d qr=%d err=%d emotion=%s",
             ai_state_name(g_ai.status.state),
             g_ai.status.chat_mode,
             g_ai.status.free_chat_active,
             g_ai.status.control_center_running ? "online" : "offline",
             g_ai.status.sound_app_running ? "active" : "idle",
             g_ai.status.bt_paused_by_ai,
             g_ai.status.tuya_bound,
             g_ai.status.tuya_bind_qr_pending,
             g_ai.status.last_error_code,
             g_ai.status.emotion[0] ? g_ai.status.emotion : "none");

    g_ai.last_logged_status = g_ai.status;
    g_ai.has_logged_status = true;
    g_ai.last_log_ms = now;
}

static void publish_status(void)
{
    mw_publish(TOPIC_AI_STATUS, &g_ai.status, sizeof(g_ai.status), MW_DIR_BACKEND_TO_UI);
    service_led_on_ai_status(&g_ai.status);
    log_status_if_needed(false);
}

static void publish_text(const char *text)
{
    ai_text_t ai_text;

    memset(&ai_text, 0, sizeof(ai_text));
    if (text) snprintf(ai_text.text, sizeof(ai_text.text), "%s", text);
    g_ai.last_text = ai_text;
    g_ai.has_last_text = ai_text.text[0] != '\0';
    mw_publish(TOPIC_AI_TEXT, &ai_text, sizeof(ai_text), MW_DIR_BACKEND_TO_UI);
}

static void resume_bt_if_paused_by_ai(void)
{
    if (!g_ai.status.bt_paused_by_ai) return;

    g_ai.status.bt_paused_by_ai = false;
    if (service_bt_is_a2dp_connected()) {
        service_bt_avrcp_play();
        g_ai.bt_resume_retries_left = AI_BT_RESUME_RETRY_COUNT;
        g_ai.next_bt_resume_retry_ms = now_ms() + AI_BT_RESUME_RETRY_DELAY_MS;
    } else {
        APP_LOGI("ai", "skip bt resume: no A2DP device connected");
        g_ai.bt_resume_retries_left = 0;
        g_ai.next_bt_resume_retry_ms = 0;
    }
}

static void pause_bt_if_streaming_by_ai(void)
{
    if (service_bt_is_a2dp_connected() && service_bt_is_audio_streaming()) {
        service_bt_avrcp_pause();
        g_ai.status.bt_paused_by_ai = true;
        g_ai.bt_resume_retries_left = 0;
        g_ai.next_bt_resume_retry_ms = 0;
        usleep(200000);
    }
}

static void service_ai_retry_bt_resume_if_needed(int64_t now)
{
    if (g_ai.bt_resume_retries_left <= 0 || g_ai.next_bt_resume_retry_ms <= 0) return;
    if (now < g_ai.next_bt_resume_retry_ms) return;

    if (!service_bt_is_a2dp_connected() || service_bt_is_audio_streaming()) {
        g_ai.bt_resume_retries_left = 0;
        g_ai.next_bt_resume_retry_ms = 0;
        return;
    }

    APP_LOGI("ai", "retry bt resume, retries left=%d", g_ai.bt_resume_retries_left);
    service_bt_avrcp_play();
    g_ai.bt_resume_retries_left--;
    if (g_ai.bt_resume_retries_left > 0) {
        g_ai.next_bt_resume_retry_ms = now + AI_BT_RESUME_RETRY_DELAY_MS;
    } else {
        g_ai.next_bt_resume_retry_ms = 0;
    }
}

static bool control_center_is_online(void)
{
    int64_t last_rx_age_ms;

    if (!g_ai.status.control_center_running || g_ai.last_rx_ms <= 0) return false;

    last_rx_age_ms = now_ms() - g_ai.last_rx_ms;
    return last_rx_age_ms >= 0 && last_rx_age_ms <= AI_ONLINE_TIMEOUT_MS;
}

static int udp_send_json(const char *json)
{
    struct sockaddr_in addr;
    ssize_t sent;

    if (g_ai.udp_fd < 0 || !json) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(XIAOZHI_UI_PORT_UP);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    sent = sendto(g_ai.udp_fd, json, strlen(json), 0,
                  (const struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        g_ai.status.last_error_code = -errno;
        APP_LOGW("ai", "send command failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static const char *find_json_field_value(const char *buf, size_t len,
                                         const char *field)
{
    char pattern[64];
    size_t pattern_len;
    const char *end;

    if (!buf || !field) return NULL;
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    pattern_len = strlen(pattern);
    if (pattern_len == 0 || pattern_len >= sizeof(pattern) || len < pattern_len) {
        return NULL;
    }

    end = buf + len;
    for (const char *p = buf; p + pattern_len <= end; p++) {
        if (p > buf && p[-1] == '\\') continue;
        if (memcmp(p, pattern, pattern_len) != 0) continue;

        p += pattern_len;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p >= end || *p != ':') continue;
        p++;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        return p < end ? p : NULL;
    }

    return NULL;
}

static bool parse_json_state(const char *buf, size_t len, int *state)
{
    const char *p;

    if (!buf || !state) return false;
    p = find_json_field_value(buf, len, "state");
    if (!p) return false;
    if (*p < '0' || *p > '9') return false;
    *state = atoi(p);
    return true;
}

static bool parse_json_string_field(const char *buf, size_t len,
                                    const char *field,
                                    char *out, size_t out_size)
{
    const char *p;
    const char *end;
    size_t n = 0;

    if (!buf || !field || !out || out_size == 0) return false;
    p = find_json_field_value(buf, len, field);
    if (!p || *p != '"') return false;
    p++;
    end = buf + len;

    while (p < end && n + 1 < out_size) {
        if (*p == '"') {
            out[n] = '\0';
            return n > 0;
        }
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case '"':
                case '\\':
                case '/':
                    out[n++] = *p++;
                    break;
                case 'n':
                    out[n++] = '\n';
                    p++;
                    break;
                case 'r':
                    out[n++] = '\r';
                    p++;
                    break;
                case 't':
                    out[n++] = '\t';
                    p++;
                    break;
                default:
                    out[n++] = *p++;
                    break;
            }
        } else {
            out[n++] = *p++;
        }
    }

    out[n] = '\0';
    return n > 0;
}

static bool parse_json_text(const char *buf, size_t len, char *out, size_t out_size)
{
    return parse_json_string_field(buf, len, "text", out, out_size);
}

static bool parse_json_bool_field(const char *buf, size_t len,
                                  const char *field, bool *out)
{
    const char *p;
    const char *end;

    if (!buf || !field || !out) return false;
    p = find_json_field_value(buf, len, field);
    if (!p) return false;

    end = buf + len;
    if (p + 4 <= end && memcmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (p + 5 <= end && memcmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    if (p < end && (*p == '0' || *p == '1')) {
        *out = *p == '1';
        return true;
    }
    return false;
}

static ai_state_t map_xiaozhi_state(int state)
{
    enum {
        XZ_UNKNOWN = 0,
        XZ_STARTING,
        XZ_WIFI_CONFIGURING,
        XZ_IDLE,
        XZ_CONNECTING,
        XZ_LISTENING,
        XZ_SPEAKING,
        XZ_UPGRADING,
        XZ_ACTIVATING,
        XZ_FATAL_ERROR,
    };

    switch (state) {
        case XZ_IDLE: return AI_STATE_IDLE;
        case XZ_CONNECTING:
        case XZ_STARTING:
        case XZ_ACTIVATING:
            return AI_STATE_CONNECTING;
        case XZ_LISTENING: return AI_STATE_LISTENING;
        case XZ_SPEAKING: return AI_STATE_SPEAKING;
        case XZ_FATAL_ERROR: return AI_STATE_ERROR;
        default: return g_ai.status.state;
    }
}

static ai_state_t map_tuya_state(const char *state)
{
    if (!state) return g_ai.status.state;
    if (strcmp(state, "idle") == 0) return AI_STATE_IDLE;
    if (strcmp(state, "connecting") == 0) return AI_STATE_CONNECTING;
    if (strcmp(state, "listening") == 0) return AI_STATE_LISTENING;
    if (strcmp(state, "thinking") == 0) return AI_STATE_THINKING;
    if (strcmp(state, "speaking") == 0) return AI_STATE_SPEAKING;
    if (strcmp(state, "network_unavailable") == 0) return AI_STATE_NETWORK_UNAVAILABLE;
    if (strcmp(state, "error") == 0) return AI_STATE_ERROR;
    return g_ai.status.state;
}

static ai_chat_mode_t map_tuya_mode(const char *mode)
{
    if (!mode) return g_ai.status.chat_mode;
    if (strcasecmp(mode, "free") == 0) return AI_CHAT_MODE_FREE;
    if (strcasecmp(mode, "wakeup") == 0) return AI_CHAT_MODE_WAKEUP;
    return AI_CHAT_MODE_UNKNOWN;
}

static void handle_tuya_message(const char *buf, size_t len)
{
    char emotion[AI_EMOTION_NAME_MAX];
    char bind_url[AI_BIND_URL_MAX];
    char mode[32];
    char state[32];
    char text[sizeof(((ai_text_t *)0)->text)];
    ai_state_t old_state;
    bool bound;
    bool publish_needed = false;

    g_ai.last_rx_ms = now_ms();
    g_ai.status.control_center_running = true;

    if (parse_json_text(buf, len, text, sizeof(text))) {
        publish_text(text);
    }

    if (parse_json_bool_field(buf, len, "bound", &bound)) {
        g_ai.status.tuya_bound = bound;
        g_ai.status.tuya_bind_qr_pending = !bound && g_ai.status.bind_url[0] != '\0';
        if (bound) {
            g_ai.status.bind_url[0] = '\0';
            g_ai.status.tuya_bind_qr_pending = false;
        }
        g_ai.status.last_error_code = 0;
        publish_needed = true;
    }

    if (parse_json_string_field(buf, len, "bind_url", bind_url, sizeof(bind_url))) {
        snprintf(g_ai.status.bind_url, sizeof(g_ai.status.bind_url), "%s", bind_url);
        g_ai.status.tuya_bound = false;
        g_ai.status.tuya_bind_qr_pending = true;
        g_ai.status.last_error_code = 0;
        publish_needed = true;
    }

    if (parse_json_string_field(buf, len, "emotion", emotion, sizeof(emotion))) {
        snprintf(g_ai.status.emotion, sizeof(g_ai.status.emotion), "%s", emotion);
        g_ai.status.last_error_code = 0;
        publish_needed = true;
    }

    if (parse_json_string_field(buf, len, "mode", mode, sizeof(mode))) {
        ai_chat_mode_t chat_mode = map_tuya_mode(mode);

        if (chat_mode != AI_CHAT_MODE_UNKNOWN) {
            g_ai.status.chat_mode = chat_mode;
            g_ai.status.free_chat_active = chat_mode == AI_CHAT_MODE_FREE;
            g_ai.status.last_error_code = 0;
            publish_needed = true;
        }
    }

    if (parse_json_string_field(buf, len, "state", state, sizeof(state))) {
        old_state = g_ai.status.state;
        g_ai.status.state = map_tuya_state(state);
        g_ai.status.sound_app_running = sound_app_is_running();
        g_ai.status.last_error_code = 0;

        if (g_ai.status.state == AI_STATE_IDLE ||
            g_ai.status.state == AI_STATE_CONNECTING ||
            g_ai.status.state == AI_STATE_LISTENING ||
            g_ai.status.state == AI_STATE_NETWORK_UNAVAILABLE ||
            g_ai.status.state == AI_STATE_ERROR) {
            g_ai.status.emotion[0] = '\0';
        }

        if (g_ai.status.state == AI_STATE_LISTENING ||
            g_ai.status.state == AI_STATE_THINKING ||
            g_ai.status.state == AI_STATE_SPEAKING) {
            pause_bt_if_streaming_by_ai();
        }

        if (g_ai.status.state == AI_STATE_IDLE &&
            old_state != AI_STATE_IDLE) {
            resume_bt_if_paused_by_ai();
        }

        publish_status();
        return;
    }

    if (publish_needed) {
        g_ai.status.sound_app_running = sound_app_is_running();
        publish_status();
    }
}

static void handle_xiaozhi_message(const char *buf, size_t len)
{
    int xz_state;
    char text[sizeof(((ai_text_t *)0)->text)];
    char event[32];
    char keyword[96];

    g_ai.last_rx_ms = now_ms();
    g_ai.status.control_center_running = true;
    g_ai.status.sound_app_running = sound_app_is_running();

    if (parse_json_string_field(buf, len, "event", event, sizeof(event)) &&
        event[0] != '\0') {
        if (strcmp(event, "wake_detected") == 0) {
            if (parse_json_string_field(buf, len, "keyword", keyword, sizeof(keyword))) {
                APP_LOGI("ai", "wake detected: %s", keyword);
            } else {
                APP_LOGI("ai", "wake detected");
            }
            pause_bt_if_streaming_by_ai();
            publish_status();
        } else if (strcmp(event, "wake_cancelled") == 0) {
            if (parse_json_string_field(buf, len, "keyword", keyword, sizeof(keyword))) {
                APP_LOGI("ai", "wake cancelled: %s", keyword);
            } else {
                APP_LOGI("ai", "wake cancelled");
            }
            if (g_ai.status.state == AI_STATE_IDLE) {
                resume_bt_if_paused_by_ai();
                publish_status();
            }
        } else if (strcmp(event, "wake") == 0) {
            if (parse_json_string_field(buf, len, "keyword", keyword, sizeof(keyword))) {
                APP_LOGI("ai", "wake confirmed: %s", keyword);
            } else {
                APP_LOGI("ai", "wake confirmed");
            }
            pause_bt_if_streaming_by_ai();
        }
    }

    if (parse_json_text(buf, len, text, sizeof(text))) {
        publish_text(text);
    }

    if (parse_json_state(buf, len, &xz_state)) {
        g_ai.status.state = map_xiaozhi_state(xz_state);
        g_ai.status.sound_app_running = sound_app_is_running();

        if (g_ai.status.state == AI_STATE_LISTENING) {
            pause_bt_if_streaming_by_ai();
        }

        if (g_ai.status.state == AI_STATE_IDLE) {
            resume_bt_if_paused_by_ai();
        }
        publish_status();
    }
}

static void handle_ai_message(const char *buf, size_t len)
{
    char runtime[32];

    if (parse_json_string_field(buf, len, "runtime", runtime, sizeof(runtime)) &&
        strcmp(runtime, "tuya") == 0) {
        handle_tuya_message(buf, len);
        return;
    }

    if (is_tuya_runtime()) {
        handle_tuya_message(buf, len);
    } else {
        handle_xiaozhi_message(buf, len);
    }
}

static int open_udp_bridge(void)
{
    struct sockaddr_in addr;
    int flags;
    int opt = 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        APP_LOGE("ai", "create UDP socket failed: %s", strerror(errno));
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(XIAOZHI_UI_PORT_DOWN);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        APP_LOGE("ai", "bind UDP port %d failed: %s", XIAOZHI_UI_PORT_DOWN, strerror(errno));
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

void service_ai_init(void)
{
    if (g_ai.initialized) return;

    g_ai.runtime = runtime_from_env();
    g_ai.manage_processes = is_tuya_runtime() ?
                            env_flag_enabled("LV_AI_MANAGE_TUYA", true) :
                            env_flag_enabled("LV_AI_MANAGE_XIAOZHI", true);
    copy_env_or_default(g_ai.control_center_bin, sizeof(g_ai.control_center_bin),
                        "LV_AI_CONTROL_CENTER_BIN", XIAOZHI_CONTROL_CENTER_BIN_DEFAULT);
    copy_env_or_default(g_ai.sound_app_bin, sizeof(g_ai.sound_app_bin),
                        "LV_AI_SOUND_APP_BIN", XIAOZHI_SOUND_APP_BIN_DEFAULT);
    copy_env_or_default(g_ai.tuya_bin, sizeof(g_ai.tuya_bin),
                        "LV_AI_TUYA_BIN", TUYA_CHAT_BOT_BIN_DEFAULT);
    if (!getenv("LV_AI_TUYA_BIN") || getenv("LV_AI_TUYA_BIN")[0] == '\0') {
        copy_env_or_default(g_ai.tuya_bin, sizeof(g_ai.tuya_bin),
                            "TUYA_CHAT_BOT_BIN", g_ai.tuya_bin);
    }
    copy_env_or_default(g_ai.sherpa_lib_dir, sizeof(g_ai.sherpa_lib_dir),
                        "LV_AI_SHERPA_LIB_DIR", XIAOZHI_SHERPA_LIB_DIR_DEFAULT);
    copy_env_or_default(g_ai.sherpa_model_dir, sizeof(g_ai.sherpa_model_dir),
                        "LV_AI_SHERPA_MODEL_DIR", XIAOZHI_SHERPA_MODEL_DIR_DEFAULT);
    copy_env_or_default(g_ai.control_center_log, sizeof(g_ai.control_center_log),
                        "LV_AI_CONTROL_CENTER_LOG", XIAOZHI_CONTROL_CENTER_LOG_DEFAULT);
    copy_env_or_default(g_ai.sound_app_log, sizeof(g_ai.sound_app_log),
                        "LV_AI_SOUND_APP_LOG", XIAOZHI_SOUND_APP_LOG_DEFAULT);
    copy_env_or_default(g_ai.tuya_log, sizeof(g_ai.tuya_log),
                        "LV_AI_TUYA_LOG", TUYA_CHAT_BOT_LOG_DEFAULT);

    if (g_ai.manage_processes) {
        if (is_tuya_runtime() && access(g_ai.tuya_bin, X_OK) != 0) {
            APP_LOGW("ai", "%s is not executable, falling back to external tuya service",
                     g_ai.tuya_bin);
            g_ai.manage_processes = false;
        } else if (!is_tuya_runtime() && access(g_ai.control_center_bin, X_OK) != 0) {
            APP_LOGW("ai", "%s is not executable, falling back to external control_center",
                     g_ai.control_center_bin);
            g_ai.manage_processes = false;
        } else if (!is_tuya_runtime() && access(g_ai.sound_app_bin, X_OK) != 0) {
            APP_LOGW("ai", "%s is not executable, falling back to external sound_app",
                     g_ai.sound_app_bin);
            g_ai.manage_processes = false;
        }
    }

    memset(&g_ai.status, 0, sizeof(g_ai.status));
    g_ai.status.state = AI_STATE_IDLE;
    g_ai.status.chat_mode = is_tuya_runtime() ? AI_CHAT_MODE_WAKEUP : AI_CHAT_MODE_UNKNOWN;
    g_ai.status.free_chat_active = false;
    g_ai.udp_fd = open_udp_bridge();
    if (g_ai.udp_fd < 0) {
        g_ai.status.state = AI_STATE_ERROR;
        g_ai.status.last_error_code = -errno;
    }

    g_ai.initialized = true;
    if (g_ai.manage_processes) {
        if (is_tuya_runtime()) {
            APP_LOGI("ai", "service initialized, managing tuya service via %s",
                     g_ai.tuya_bin);
        } else {
            APP_LOGI("ai", "service initialized, managing xiaozhi services via %s and %s",
                     g_ai.control_center_bin, g_ai.sound_app_bin);
        }
        ensure_runtime_processes();
    } else {
        APP_LOGI("ai", "service initialized, expecting external %s service on UDP %d/%d",
                 runtime_name(), XIAOZHI_UI_PORT_UP, XIAOZHI_UI_PORT_DOWN);
    }
    publish_status();
}

void service_ai_deinit(void)
{
    if (!g_ai.initialized) return;

    service_ai_stop_listen();
    stop_child_process(&g_ai.sound_app_pid, "sound_app");
    stop_child_process(&g_ai.control_center_pid, "control_center");
    stop_child_process(&g_ai.tuya_pid, "tuya_chat_bot");
    if (g_ai.udp_fd >= 0) {
        close(g_ai.udp_fd);
        g_ai.udp_fd = -1;
    }
    g_ai.initialized = false;
}

void service_ai_start_listen(void)
{
    if (!g_ai.initialized) return;

    if (is_tuya_runtime()) {
        if (!g_ai.tuya_license_valid) {
            publish_text("Tuya credentials are missing. Complete factory provisioning first.");
            g_ai.status.state = AI_STATE_CONNECTING;
            g_ai.status.last_error_code = -EACCES;
            publish_status();
            return;
        }
        g_ai.status.control_center_running = g_ai.manage_processes ?
                                             child_process_alive(&g_ai.tuya_pid, "tuya_chat_bot", false) :
                                             g_ai.status.control_center_running;
        g_ai.status.sound_app_running = sound_app_is_running();
        publish_text("Say the wake phrase to start a Tuya voice conversation.");
        publish_status();
        return;
    }

    if (!ai_network_ready()) {
        g_ai.status.state = AI_STATE_NETWORK_UNAVAILABLE;
        g_ai.status.control_center_running = false;
        g_ai.status.sound_app_running = false;
        g_ai.status.last_error_code = -ENETDOWN;
        APP_LOGW("ai", "network is unavailable, ignore start_listen");
        publish_text("Connect to Wi-Fi before using the voice assistant.");
        publish_status();
        return;
    }

    if (!control_center_is_online()) {
        g_ai.status.state = AI_STATE_CONNECTING;
        g_ai.status.sound_app_running = sound_app_is_running();
        g_ai.status.last_error_code = -ENOTCONN;
        APP_LOGW("ai", "control_center is not online yet, ignore start_listen");
        publish_status();
        return;
    }

    if (g_ai.status.state != AI_STATE_IDLE) {
        g_ai.status.last_error_code = -EINPROGRESS;
        APP_LOGW("ai", "assistant is not idle, ignore start_listen: state=%s",
                 ai_state_name(g_ai.status.state));
        publish_status();
        return;
    }

    /*
     * Only real A2DP audio streaming conflicts with Xiaozhi capture/playback.
     * AVRCP "playing" can be stale or metadata-only, so do not use it to
     * decide whether to pause/resume the user's phone.
     */
    g_ai.status.bt_paused_by_ai = false;
    if (service_bt_is_a2dp_connected() && service_bt_is_audio_streaming()) {
        service_bt_avrcp_pause();
        g_ai.status.bt_paused_by_ai = true;
        usleep(200000);
    }

    if (udp_send_json("{\"cmd\":\"start_listen\"}") == 0) {
        g_ai.status.state = AI_STATE_LISTENING;
        g_ai.status.sound_app_running = true;
        g_ai.status.last_error_code = 0;
    } else {
        g_ai.status.state = AI_STATE_ERROR;
    }
    publish_status();
}

void service_ai_stop_listen(void)
{
    if (!g_ai.initialized) return;

    if (is_tuya_runtime()) {
        if (g_ai.status.free_chat_active) {
            udp_send_json("{\"runtime\":\"aitvbox\",\"cmd\":\"exit_free_chat\"}");
            g_ai.status.chat_mode = AI_CHAT_MODE_WAKEUP;
            g_ai.status.free_chat_active = false;
        }
        g_ai.status.sound_app_running = sound_app_is_running();
        if (g_ai.status.state == AI_STATE_LISTENING ||
            g_ai.status.state == AI_STATE_THINKING ||
            g_ai.status.state == AI_STATE_SPEAKING) {
            g_ai.status.state = ai_network_ready() ? AI_STATE_IDLE : AI_STATE_NETWORK_UNAVAILABLE;
            resume_bt_if_paused_by_ai();
        }
        publish_status();
        return;
    }

    udp_send_json("{\"cmd\":\"stop_listen\"}");
    g_ai.status.sound_app_running = sound_app_is_running();
    g_ai.status.state = ai_network_ready() ? AI_STATE_IDLE : AI_STATE_NETWORK_UNAVAILABLE;

    resume_bt_if_paused_by_ai();

    publish_status();
}

void service_ai_enter_free_chat(void)
{
    if (!g_ai.initialized) return;

    if (!is_tuya_runtime()) {
        service_ai_start_listen();
        return;
    }

    if (!ai_network_ready()) {
        g_ai.status.state = AI_STATE_NETWORK_UNAVAILABLE;
        g_ai.status.last_error_code = -ENETDOWN;
        publish_text("Connect to Wi-Fi before starting Free Chat.");
        publish_status();
        return;
    }

    if (!g_ai.tuya_license_valid) {
        g_ai.status.state = AI_STATE_CONNECTING;
        g_ai.status.last_error_code = -EACCES;
        publish_text("Tuya credentials are missing. Complete factory provisioning first.");
        publish_status();
        return;
    }

    g_ai.status.control_center_running = g_ai.manage_processes ?
                                         child_process_alive(&g_ai.tuya_pid, "tuya_chat_bot", false) :
                                         g_ai.status.control_center_running;
    if (!g_ai.status.control_center_running) {
        g_ai.status.state = AI_STATE_CONNECTING;
        g_ai.status.last_error_code = -ENOTCONN;
        publish_text("The Tuya voice service is starting. Please wait.");
        publish_status();
        return;
    }

    if (!g_ai.status.tuya_bound) {
        g_ai.status.tuya_bind_qr_pending = g_ai.status.bind_url[0] != '\0';
        g_ai.status.last_error_code = -EACCES;
        publish_text(g_ai.status.tuya_bind_qr_pending ?
                     "Scan the QR code in the Smart Life app before starting Free Chat." :
                     "Retrieving the device pairing QR code. Please wait.");
        publish_status();
        return;
    }

    pause_bt_if_streaming_by_ai();

    if (udp_send_json("{\"runtime\":\"aitvbox\",\"cmd\":\"enter_free_chat\",\"wake\":true}") == 0) {
        g_ai.status.state = AI_STATE_CONNECTING;
        g_ai.status.sound_app_running = true;
        g_ai.status.last_error_code = 0;
        publish_text("Opening Free Chat.");
    } else {
        g_ai.status.state = AI_STATE_ERROR;
        resume_bt_if_paused_by_ai();
    }
    publish_status();
}

void service_ai_exit_free_chat(void)
{
    if (!g_ai.initialized) return;

    if (!is_tuya_runtime()) {
        service_ai_stop_listen();
        return;
    }

    if (udp_send_json("{\"runtime\":\"aitvbox\",\"cmd\":\"exit_free_chat\"}") != 0) {
        g_ai.status.last_error_code = -errno;
    }

    g_ai.status.chat_mode = AI_CHAT_MODE_WAKEUP;
    g_ai.status.free_chat_active = false;
    g_ai.status.sound_app_running = false;
    g_ai.status.state = ai_network_ready() ? AI_STATE_IDLE : AI_STATE_NETWORK_UNAVAILABLE;

    resume_bt_if_paused_by_ai();
    publish_status();
}

void service_ai_send_status(void)
{
    publish_status();
    if (g_ai.has_last_text) {
        mw_publish(TOPIC_AI_TEXT, &g_ai.last_text, sizeof(g_ai.last_text), MW_DIR_BACKEND_TO_UI);
    }
}

void service_ai_update(void)
{
    char buf[512];
    ssize_t n;
    int64_t now;
    ai_status_t before_status;

    if (!g_ai.initialized) return;

    before_status = g_ai.status;
    ensure_runtime_processes();
    if (!status_equal(&before_status, &g_ai.status)) {
        publish_status();
    }

    if (g_ai.status.state == AI_STATE_NETWORK_UNAVAILABLE) {
        return;
    }

    if (g_ai.udp_fd >= 0) {
        while ((n = recv(g_ai.udp_fd, buf, sizeof(buf) - 1, 0)) > 0) {
            buf[n] = '\0';
            handle_ai_message(buf, (size_t)n);
        }
    }

    now = now_ms();
    service_ai_retry_bt_resume_if_needed(now);
    if (!is_tuya_runtime() &&
        g_ai.status.control_center_running &&
        g_ai.last_rx_ms > 0 &&
        now - g_ai.last_rx_ms > AI_ONLINE_TIMEOUT_MS) {
        g_ai.status.control_center_running = g_ai.manage_processes && g_ai.control_center_pid > 0;
        g_ai.status.sound_app_running = sound_app_is_running();
        if (g_ai.status.state != AI_STATE_IDLE) {
            g_ai.status.state = AI_STATE_ERROR;
            g_ai.status.last_error_code = -ETIMEDOUT;
        }
        publish_status();
    } else {
        log_status_if_needed(false);
    }
}

bool service_ai_blocks_bt_playback(void)
{
    if (!g_ai.initialized) return false;

    return g_ai.status.state == AI_STATE_LISTENING ||
           g_ai.status.state == AI_STATE_THINKING ||
           g_ai.status.state == AI_STATE_SPEAKING;
}

bool service_ai_is_tuya_license_ready(void)
{
    if (!g_ai.initialized || !is_tuya_runtime() ||
        !g_ai.status.control_center_running) {
        return false;
    }

    /*
     * 未绑定的新机收到 bind_url，说明 SDK 已读取并接受 UUID/AuthKey，且已走到
     * 云端绑定阶段；已绑定/返修设备则会通过 MQTT_CONNECTED 上报 bound=true。
     */
    return g_ai.status.tuya_bound ||
           (g_ai.status.tuya_bind_qr_pending && g_ai.status.bind_url[0] != '\0');
}
