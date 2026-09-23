/* service_ota.c — AITVBox OTA 升级服务（lv_backend 内）。
 *
 * 照 service_hdmi_preview.c 的 fork+execl 模式管理外部进程（wget/swupdate/
 * fw_printenv/fw_setenv/reboot）。耗时操作起独立线程，避免阻塞 backend 10ms 主循环。
 * 状态经 TOPIC_OTA_STATUS 上报给 UI。
 *
 * 流程：
 *   CHECK    —— wget 拉版本 JSON，解析 version/url，比对 /etc/aitvbox-version
 *   DOWNLOAD —— libcurl 下载 .swu 到 /mnt/UDISK/upgrade.swu
 *   APPLY    —— fw_printenv 判当前槽 → swupdate -i 写非活动槽 -e stable,<dir>
 *               → 成功后 reboot
 *   COMMIT   —— 新槽启动后 health check → fw_setenv upgrade_available 0
 *
 * 回滚：APPLY 时 sw-description 已写 upgrade_available=1/bootcount=0/bootlimit/
 *       方向化 altbootcmd。新槽起不来、bootcount 超 bootlimit → U-Boot altbootcmd
 *       切回旧槽。COMMIT 成功才清 upgrade_available 停止计数。
 *
 * 版本 JSON（服务端）约定最简格式：
 *   {"version":"1.0.1","url":"https://host/x.swu","sha256":"<hex>"}
 *   用字符串扫描解析，不引入 JSON 库。
 */

#include "service_ota.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "backend_types.h"
#include "log/app_log.h"
#include "service_led.h"
#include "../middleware/middleware.h"
#include <curl/curl.h>

#define OTA_MODULE "ota-service"

#define LOCAL_VERSION_FILE   "/etc/aitvbox-version"
#define SWU_DOWNLOAD_PATH    "/mnt/UDISK/upgrade.swu"
#define SWU_DOWNLOAD_TMP     "/mnt/UDISK/upgrade.swu.tmp"
#define SWUPDATE_BIN         "/sbin/swupdate"
#define SWUPDATE_PUBKEY      "/etc/swupdate_public.pem"
#define WGET_BIN             "/usr/bin/wget"
#define FW_PRINTENV_BIN      "/usr/sbin/fw_printenv"
#define FW_SETENV_BIN        "/usr/sbin/fw_setenv"
#define REBOOT_BIN           "/sbin/reboot"
#define OTA_STDIO_LOG        "/tmp/ota.log"
#define UDISK_DIR            "/mnt/UDISK"

#define OTA_CHECK_URL_DEFAULT \
    "https://ota.example.com/a133-b6/latest.json"
#define SHA256SUM_BIN        "/usr/bin/sha256sum"

/* 进程派生后的输出重定向（日志）。 */
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

/* 同步运行一个外部命令，返回退出码（WEXITSTATUS）或负 errno 风格。
 * stdout/stderr 重定向到 OTA_STDIO_LOG。阻塞调用，仅用于短命令。 */
static int run_cmd_sync(const char *bin, const char *arg1, const char *arg2,
                        const char *arg3, const char *arg4, const char *arg5)
{
    pid_t pid = fork();
    if (pid < 0) {
        APP_LOGE(OTA_MODULE, "fork %s failed: %s", bin, strerror(errno));
        return -errno;
    }
    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) {
            _exit(1);
        }
        redirect_child_output(OTA_STDIO_LOG);
        execl(bin, bin, arg1, arg2, arg3, arg4, arg5, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -errno;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    return -1;
}

/* 全局状态。 */
static struct {
    pthread_mutex_t lock;
    ota_status_t status;
    bool initialized;
    /* 下载/检查用的最新 URL（CHECK 填入，DOWNLOAD 复用） */
    char remote_url[400];
    char remote_version[64];
    char remote_sha256[65];   /* 下载后校验用，空则不校验 */
    char check_url[400];      /* CHECK 命令传入的版本 JSON URL，空则用默认 */
    /* 后台线程 */
    pthread_t worker;
    bool worker_running;
    /* 进程内状态监听器（service_cloud 注册，联动上报云端 ota/status） */
    void (*status_listener)(const ota_status_t *st);
} g_ota;

static void publish_status(void)
{
    ota_status_t snap;
    pthread_mutex_lock(&g_ota.lock);
    snap = g_ota.status;
    pthread_mutex_unlock(&g_ota.lock);
    mw_publish(TOPIC_OTA_STATUS, &snap, sizeof(snap), MW_DIR_BACKEND_TO_UI);
    /* 通知进程内监听器（云上报）。mw_publish 只走后端→UI IPC，不回环后端进程内，
     * 故单独通知监听器。 */
    if (g_ota.status_listener) {
        g_ota.status_listener(&snap);
    }
    /* LED strip is a shared resource owned by service_led. */
    service_led_on_ota_status(&snap);
}

void service_ota_set_status_listener(void (*cb)(const ota_status_t *st))
{
    g_ota.status_listener = cb;
}

static void set_state(ota_state_t state, int32_t error_code, int32_t progress)
{
    pthread_mutex_lock(&g_ota.lock);
    g_ota.status.state = state;
    if (error_code != -1) {
        g_ota.status.error_code = error_code;
    }
    if (progress != -1) {
        g_ota.status.progress = progress;
    }
    pthread_mutex_unlock(&g_ota.lock);
    publish_status();
}

/* 读 /etc/aitvbox-version 的 AITVBOX_VERSION= 字段。 */
static void read_local_version(char *out, size_t out_len)
{
    FILE *fp = fopen(LOCAL_VERSION_FILE, "r");
    if (!fp) {
        snprintf(out, out_len, "unknown");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "AITVBOX_VERSION=", 16) == 0) {
            char *v = line + 16;
            size_t n = strlen(v);
            while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r' ||
                             v[n - 1] == ' ' || v[n - 1] == '\t')) {
                v[--n] = '\0';
            }
            snprintf(out, out_len, "%s", v);
            fclose(fp);
            return;
        }
    }
    fclose(fp);
    snprintf(out, out_len, "unknown");
}

/* 从 JSON 文本里抽取 "key":"value" 的 value 到 out。
 * 最简扫描，不校验完整 JSON 语法。失败返回 -1。 */
static int json_extract_string(const char *json, const char *key,
                               char *out, size_t out_len)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return -1;
    }
    p += strlen(pat);
    const char *end = strchr(p, '"');
    if (!end) {
        return -1;
    }
    size_t n = (size_t)(end - p);
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

/* 抓取 URL 内容到内存（用于版本 JSON，预期很小）。返回内容长度，-1 失败。 */
static int wget_to_memory(const char *url, char *buf, size_t buf_len)
{
    char tmp[] = "/tmp/ota_check_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) {
        return -1;
    }
    close(fd);

    pid_t pid = fork();
    if (pid < 0) {
        unlink(tmp);
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        redirect_child_output(OTA_STDIO_LOG);
        execl(WGET_BIN, WGET_BIN, "-q", "-O", tmp, url, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    int rc = -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        FILE *fp = fopen(tmp, "r");
        if (fp) {
            size_t n = fread(buf, 1, buf_len - 1, fp);
            buf[n] = '\0';
            fclose(fp);
            rc = (int)n;
        }
    }
    unlink(tmp);
    return rc;
}

/* fw_printenv <var>：读到 out。成功 0。 */
static int fw_getenv(const char *var, char *out, size_t out_len)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        /* fw_printenv 输出形如 boot_partition=bootA */
        execl(FW_PRINTENV_BIN, FW_PRINTENV_BIN, "-n", var, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }
    char line[128] = {0};
    if (fgets(line, sizeof(line), fp) == NULL) {
        line[0] = '\0';
    }
    fclose(fp);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
    if (n == 0) {
        return -1;
    }
    if (n >= out_len) {
        return -1;
    }
    memcpy(out, line, n + 1);
    return 0;
}

/* 读 /proc/cmdline 的 root=，映射当前实际槽：'A' / 'B' / 0(未知)。
 * rootfsA=mmcblk0p4，rootfsB=mmcblk0p6（见设备分区表 by-name）。
 * 只看 root= 紧跟的这一个 token——cmdline 里 partitions=...rootfsA@mmcblk0p4...
 * 也会出现 mmcblk0p4/p6，全局 strstr 会误判，故取 root= 后到下一个空格为止。 */
static char actual_slot_from_cmdline(void)
{
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    char buf[1024] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    const char *p = strstr(buf, "root=");
    if (!p) {
        return 0;
    }
    p += 5;  /* 跳过 "root="，指向 root 设备路径 */
    /* 截取到下一个空白为止，避免被后面的 partitions= 里的分区名干扰。 */
    char tok[128] = {0};
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < sizeof(tok) - 1) {
        tok[i++] = *p++;
    }
    if (strstr(tok, "mmcblk0p4")) {
        return 'A';
    }
    if (strstr(tok, "mmcblk0p6")) {
        return 'B';
    }
    return 0;
}

/* 暴露当前启动槽给云上报用（读 /proc/cmdline 的 root=）。返回 'A'/'B'/0。 */
char service_ota_get_boot_slot(void)
{
    return actual_slot_from_cmdline();
}

/* 前置声明：service_ota_is_package_ready 早于定义使用。 */
static int sha256_of_file(const char *path, char *out, size_t out_len);

/* 本地升级包是否就绪：文件存在 + sha256 匹配。供云端重下载守卫 / 开机兜底判断。 */
bool service_ota_is_package_ready(const char *expect_sha256)
{
    if (access(SWU_DOWNLOAD_PATH, R_OK) != 0) {
        return false;
    }
    if (!expect_sha256 || expect_sha256[0] == '\0') {
        return true; /* 无期望 sha256，只校验存在 */
    }
    char got[65] = {0};
    if (sha256_of_file(SWU_DOWNLOAD_PATH, got, sizeof(got)) != 0) {
        return false;
    }
    return strcasecmp(got, expect_sha256) == 0;
}

/* 标记包已下载完成：跳过下载场景下推一次 DOWNLOAD_DONE 给 UI，触发安装弹窗。 */
void service_ota_mark_package_downloaded(void)
{
    if (access(SWU_DOWNLOAD_PATH, R_OK) != 0) {
        return;  /* 包不在，不伪造状态 */
    }
    set_state(OTA_STATE_DOWNLOAD_DONE, 0, 100);
    APP_LOGI(OTA_MODULE, "package marked downloaded (skip-download path): %s", SWU_DOWNLOAD_PATH);
}

/* 计算 file 的 sha256（前 64 hex 小写）到 out。成功 0。用 /usr/bin/sha256sum。 */
static int sha256_of_file(const char *path, char *out, size_t out_len)
{
    if (out_len < 65) return -1;
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execl(SHA256SUM_BIN, SHA256SUM_BIN, path, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) { close(pipefd[0]); waitpid(pid, NULL, 0); return -1; }
    char line[256] = {0};
    if (fgets(line, sizeof(line), fp) == NULL) line[0] = '\0';
    fclose(fp);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
    /* sha256sum 输出形如 "<64hex>  <path>"，取前 64 个 hex 字符。 */
    size_t i = 0;
    while (i < 64 && line[i] && i < out_len - 1) {
        out[i] = line[i];
        i++;
    }
    out[i] = '\0';
    return (i == 64) ? 0 : -1;
}

/* 判定当前活动槽，返回升级方向（now_A_next_B / now_B_next_A）或 NULL。
 *
 * 双重判定，防 B→B 刷活动槽事故：
 *   1. boot_partition env（uboot 启动时设，反映当前槽）
 *   2. /proc/cmdline 的 root=（内核实际挂载的 rootfs 分区，即真实活动槽）
 * 两者必须一致才放行。若 env 与实际槽不符（如手动 fw_setenv 改过未重启、
 * 或回滚中途触发 OTA），拒绝 APPLY——宁可报错也不刷错槽。 */
static const char *direction_for_current_slot(void)
{
    char slot[32];
    if (fw_getenv("boot_partition", slot, sizeof(slot)) < 0) {
        APP_LOGE(OTA_MODULE, "fw_printenv boot_partition failed");
        return NULL;
    }
    const char *dir;
    char env_slot;
    if (strcmp(slot, "bootA") == 0) {
        dir = "now_A_next_B";
        env_slot = 'A';
    } else if (strcmp(slot, "bootB") == 0) {
        dir = "now_B_next_A";
        env_slot = 'B';
    } else {
        APP_LOGE(OTA_MODULE, "unknown boot_partition: %s", slot);
        return NULL;
    }

    char actual = actual_slot_from_cmdline();
    if (actual == 0) {
        APP_LOGW(OTA_MODULE, "cannot determine actual slot from cmdline, trust env (%c)", env_slot);
        return dir;
    }
    if (actual != env_slot) {
        APP_LOGE(OTA_MODULE,
                 "slot mismatch: boot_partition=%c but actual root=%c, refuse APPLY (avoid flashing active slot)",
                 env_slot, actual);
        return NULL;
    }
    return dir;
}

/* ---------- 后台工作线程：CHECK ---------- */
static void *worker_check(void *arg)
{
    (void)arg;
    const char *url = g_ota.check_url[0] ? g_ota.check_url : OTA_CHECK_URL_DEFAULT;
    set_state(OTA_STATE_CHECKING, 0, 0);

    char json[1024];
    int n = wget_to_memory(url, json, sizeof(json));
    if (n < 0) {
        APP_LOGE(OTA_MODULE, "check: wget failed");
        set_state(OTA_STATE_ERROR, 1, 0);
        goto done;
    }
    char remote_ver[64] = {0};
    char remote_url[400] = {0};
    char remote_sha[65] = {0};
    if (json_extract_string(json, "version", remote_ver, sizeof(remote_ver)) < 0 ||
        json_extract_string(json, "url", remote_url, sizeof(remote_url)) < 0) {
        APP_LOGE(OTA_MODULE, "check: parse version JSON failed");
        set_state(OTA_STATE_ERROR, 2, 0);
        goto done;
    }
    json_extract_string(json, "sha256", remote_sha, sizeof(remote_sha));  /* 可选 */
    pthread_mutex_lock(&g_ota.lock);
    snprintf(g_ota.remote_version, sizeof(g_ota.remote_version), "%s", remote_ver);
    snprintf(g_ota.remote_url, sizeof(g_ota.remote_url), "%s", remote_url);
    snprintf(g_ota.remote_sha256, sizeof(g_ota.remote_sha256), "%s", remote_sha);
    snprintf(g_ota.status.version_remote, sizeof(g_ota.status.version_remote),
             "%s", remote_ver);
    pthread_mutex_unlock(&g_ota.lock);

    char local_ver[64];
    read_local_version(local_ver, sizeof(local_ver));
    pthread_mutex_lock(&g_ota.lock);
    snprintf(g_ota.status.version_local, sizeof(g_ota.status.version_local),
             "%s", local_ver);
    pthread_mutex_unlock(&g_ota.lock);

    APP_LOGI(OTA_MODULE, "check: local=%s remote=%s", local_ver, remote_ver);
    if (strcmp(local_ver, remote_ver) != 0) {
        pthread_mutex_lock(&g_ota.lock);
        g_ota.status.update_available = true;
        pthread_mutex_unlock(&g_ota.lock);
        set_state(OTA_STATE_UPDATE_AVAILABLE, 0, 0);
    } else {
        pthread_mutex_lock(&g_ota.lock);
        g_ota.status.update_available = false;
        pthread_mutex_unlock(&g_ota.lock);
        set_state(OTA_STATE_UP_TO_DATE, 0, 0);
    }

done:
    pthread_mutex_lock(&g_ota.lock);
    g_ota.worker_running = false;
    pthread_mutex_unlock(&g_ota.lock);
    return NULL;
}

/* ---------- 后台工作线程：DOWNLOAD ---------- */

/* libcurl 写文件回调：把下载块追加写入 FILE*。 */
static size_t curl_write_file_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    FILE *fp = (FILE *)userdata;
    return fwrite(ptr, size, nmemb, fp) * size; /* fwrite 返回 nmemb，×size 还原字节数 */
}

/* libcurl 进度回调：算百分比，整百分点变化时上报（避免高频 publish 刷屏）。
 * userdata 指向 int 存上次上报的百分点。 */
static int curl_progress_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal; (void)ulnow;
    int *last_pct = (int *)userdata;
    if (dltotal <= 0) return 0;
    int pct = (int)((dlnow * 100) / dltotal);
    if (pct < 0) pct = 0;
    if (pct > 99) pct = 99; /* 留 100 给 DOWNLOAD_DONE */
    if (pct != *last_pct) {
        *last_pct = pct;
        set_state(OTA_STATE_DOWNLOADING, -1, pct);
    }
    return 0;
}

/* 用 libcurl 下载 url 到 path（HTTPS，与 provision 同源；设备 busybox wget 不支持 https）。
 * 下载过程中按整百分点上报进度。返回 0 成功。 */
static int curl_download(const char *url, const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        APP_LOGE(OTA_MODULE, "download: open %s failed (%s)", path, strerror(errno));
        return -1;
    }

    int last_pct = -1;
    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        APP_LOGE(OTA_MODULE, "download: curl_easy_init failed");
        return -1;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &last_pct);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    /* 下载大文件不能给太短超时；用低速超时防卡死。 */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);  /* 低于 1KB/s */
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);     /* 持续 60s 则失败 */
    /* OTA metadata, package hash and signature all matter; TLS must also
     * authenticate the server to prevent downgrade and availability attacks. */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (rc != CURLE_OK || http_code != 200) {
        APP_LOGE(OTA_MODULE, "download: curl failed rc=%d http=%ld (%s)",
                 (int)rc, http_code, curl_easy_strerror(rc));
        return -1;
    }
    return 0;
}

static void *worker_download(void *arg)
{
    (void)arg;
    set_state(OTA_STATE_DOWNLOADING, 0, 0);

    mkdir(UDISK_DIR, 0755);
    const char *url = g_ota.remote_url;
    if (url[0] == '\0') {
        APP_LOGE(OTA_MODULE, "download: no remote URL (run CHECK first)");
        set_state(OTA_STATE_ERROR, 3, 0);
        goto done;
    }

    /* libcurl 下载（支持 HTTPS，设备 busybox wget 不支持 SSL）。
     * 下载中按整百分点经 curl_progress_cb 上报进度。 */
    if (curl_download(url, SWU_DOWNLOAD_TMP) != 0) {
        set_state(OTA_STATE_ERROR, 4, 0);
        goto done;
    }
    /* sha256 校验：若命令带了 remote_sha256，下完比对，不符则删包报错（不进 DOWNLOAD_DONE）。
     * 完整性最终仍由 swupdate 的 RSA 签名兜底，这里是下载阶段的早筛。 */
    pthread_mutex_lock(&g_ota.lock);
    char want_sha[65];
    snprintf(want_sha, sizeof(want_sha), "%s", g_ota.remote_sha256);
    pthread_mutex_unlock(&g_ota.lock);
    if (want_sha[0]) {
        char got_sha[65] = {0};
        if (sha256_of_file(SWU_DOWNLOAD_TMP, got_sha, sizeof(got_sha)) != 0) {
            APP_LOGE(OTA_MODULE, "download: sha256sum failed");
            unlink(SWU_DOWNLOAD_TMP);
            set_state(OTA_STATE_ERROR, 9, 0);
            goto done;
        }
        if (strcasecmp(got_sha, want_sha) != 0) {
            APP_LOGE(OTA_MODULE, "download: sha256 mismatch (want=%.16s.. got=%.16s..)",
                     want_sha, got_sha);
            unlink(SWU_DOWNLOAD_TMP);
            set_state(OTA_STATE_ERROR, 9, 0);
            goto done;
        }
        APP_LOGI(OTA_MODULE, "download: sha256 ok (%.16s..)", got_sha);
    }
    if (rename(SWU_DOWNLOAD_TMP, SWU_DOWNLOAD_PATH) < 0) {
        set_state(OTA_STATE_ERROR, -errno, 0);
        goto done;
    }
    set_state(OTA_STATE_DOWNLOAD_DONE, 0, 100);
    APP_LOGI(OTA_MODULE, "download done: %s", SWU_DOWNLOAD_PATH);

done:
    pthread_mutex_lock(&g_ota.lock);
    g_ota.worker_running = false;
    pthread_mutex_unlock(&g_ota.lock);
    return NULL;
}

/* ---------- 后台工作线程：APPLY ---------- */
static void *worker_apply(void *arg)
{
    (void)arg;
    /* 分段进度：10=准备，30=写入中，80=写完待重启，100=REBOOTING。
     * 真实 swupdate 百分比需起进度 socket，本期用阶段档位配 spinner 足够。 */
    set_state(OTA_STATE_APPLYING, 0, 10);

    const char *direction = direction_for_current_slot();
    if (!direction) {
        set_state(OTA_STATE_ERROR, 5, 0);
        goto done;
    }
    if (access(SWU_DOWNLOAD_PATH, R_OK) != 0) {
        APP_LOGE(OTA_MODULE, "apply: %s not found (run DOWNLOAD first)", SWU_DOWNLOAD_PATH);
        set_state(OTA_STATE_ERROR, 6, 0);
        goto done;
    }

    APP_LOGI(OTA_MODULE, "apply: swupdate -e stable,%s", direction);

    /* swupdate 参数超过 run_cmd_sync 的固定槽位，直接 fork+execl。
     * -i <path> -e stable,<direction> -k <key>，方向由当前槽决定。
     * -e/--select 是 required_argument，期望单个逗号分隔参数 "<software>,<mode>"
     * （parse_image_selector 用 strchr 找逗号拆分），不能拆成两个 token。 */
    char sel[64];
    snprintf(sel, sizeof(sel), "stable,%s", direction);
    pid_t pid = fork();
    if (pid < 0) {
        set_state(OTA_STATE_ERROR, -errno, 0);
        goto done;
    }
    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        redirect_child_output(OTA_STDIO_LOG);
        execl(SWUPDATE_BIN, SWUPDATE_BIN,
              "-i", SWU_DOWNLOAD_PATH,
              "-e", sel,
              "-k", SWUPDATE_PUBKEY,
              (char *)NULL);
        _exit(127);
    }
    /* swupdate 已启动，进入写入阶段。 */
    set_state(OTA_STATE_APPLYING, -1, 30);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        APP_LOGE(OTA_MODULE, "apply: swupdate failed (exit=%d)",
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        set_state(OTA_STATE_ERROR, 7, 0);
        goto done;
    }

    /* swupdate 成功：env 已切到新槽 + upgrade_available=1。重启进新槽。 */
    set_state(OTA_STATE_APPLYING, -1, 80);
    set_state(OTA_STATE_REBOOTING, 0, 100);
    APP_LOGI(OTA_MODULE, "apply done, rebooting into new slot");
    sync();
    run_cmd_sync(REBOOT_BIN, "-f", NULL, NULL, NULL, NULL);

done:
    pthread_mutex_lock(&g_ota.lock);
    g_ota.worker_running = false;
    pthread_mutex_unlock(&g_ota.lock);
    return NULL;
}

/* ---------- 后台工作线程：COMMIT ---------- */
static void *worker_commit(void *arg)
{
    (void)arg;
    set_state(OTA_STATE_COMMITTING, 0, 0);

    char avail[16] = {0};
    if (fw_getenv("upgrade_available", avail, sizeof(avail)) < 0 ||
        strcmp(avail, "1") != 0) {
        /* 非升级态，无需 commit。 */
        APP_LOGI(OTA_MODULE, "commit: not in upgrade state, skip");
        set_state(OTA_STATE_IDLE, 0, 0);
        goto done;
    }

    /* Health check：backend 自身已起来（能跑这段说明系统基本正常）。
     * 进一步可检查 socket/关键服务；此处以 backend 存活为最低判据。
     * 通过则清 upgrade_available，停止 bootcount 计数。 */
    int rc = run_cmd_sync(FW_SETENV_BIN, "upgrade_available", "0",
                          NULL, NULL, NULL);
    if (rc != 0) {
        APP_LOGE(OTA_MODULE, "commit: fw_setenv failed rc=%d", rc);
        set_state(OTA_STATE_ERROR, 8, 0);
        goto done;
    }
    /* 顺手清 bootcount。 */
    run_cmd_sync(FW_SETENV_BIN, "bootcount", "0", NULL, NULL, NULL);
    APP_LOGI(OTA_MODULE, "commit: upgrade confirmed, upgrade_available=0");
    set_state(OTA_STATE_IDLE, 0, 0);

done:
    pthread_mutex_lock(&g_ota.lock);
    g_ota.worker_running = false;
    pthread_mutex_unlock(&g_ota.lock);
    return NULL;
}

/* 起一个工作线程（保证同时只有一个）。 */
static bool start_worker(void *(*fn)(void *))
{
    bool ok = false;
    pthread_mutex_lock(&g_ota.lock);
    if (!g_ota.worker_running) {
        g_ota.worker_running = true;
        ok = true;
    }
    pthread_mutex_unlock(&g_ota.lock);
    if (!ok) {
        APP_LOGW(OTA_MODULE, "OTA busy, ignore new command");
        return false;
    }
    if (pthread_create(&g_ota.worker, NULL, fn, NULL) != 0) {
        pthread_mutex_lock(&g_ota.lock);
        g_ota.worker_running = false;
        pthread_mutex_unlock(&g_ota.lock);
        APP_LOGE(OTA_MODULE, "pthread_create failed: %s", strerror(errno));
        return false;
    }
    pthread_detach(g_ota.worker);
    return true;
}

void service_ota_handle_command(const ota_cmd_t *cmd)
{
    if (!cmd) {
        return;
    }
    switch (cmd->action) {
    case OTA_CMD_CHECK:
        if (cmd->url[0]) {
            pthread_mutex_lock(&g_ota.lock);
            snprintf(g_ota.check_url, sizeof(g_ota.check_url), "%s", cmd->url);
            pthread_mutex_unlock(&g_ota.lock);
        }
        start_worker(worker_check);
        break;
    case OTA_CMD_DOWNLOAD:
        /* 若命令直接给了 .swu URL，优先用它；否则用 CHECK 解析到的 URL。 */
        if (cmd->url[0]) {
            pthread_mutex_lock(&g_ota.lock);
            snprintf(g_ota.remote_url, sizeof(g_ota.remote_url), "%s", cmd->url);
            snprintf(g_ota.remote_sha256, sizeof(g_ota.remote_sha256), "%s", cmd->sha256);
            pthread_mutex_unlock(&g_ota.lock);
        }
        start_worker(worker_download);
        break;
    case OTA_CMD_APPLY:
        start_worker(worker_apply);
        break;
    case OTA_CMD_COMMIT:
        start_worker(worker_commit);
        break;
    case OTA_CMD_GET_STATUS:
        publish_status();
        break;
    case OTA_CMD_CANCEL:
        /* Phase 1：取消未实现（需 kill 下载进程），留 TODO。 */
        APP_LOGW(OTA_MODULE, "CANCEL not implemented in Phase 1");
        break;
    default:
        break;
    }
}

void service_ota_init(void)
{
    pthread_mutex_init(&g_ota.lock, NULL);
    memset(&g_ota.status, 0, sizeof(g_ota.status));
    g_ota.status.state = OTA_STATE_IDLE;
    g_ota.initialized = true;
    read_local_version(g_ota.status.version_local,
                       sizeof(g_ota.status.version_local));
    APP_LOGI(OTA_MODULE, "init: local version=%s",
             g_ota.status.version_local);
    publish_status();

    /* 启动时若处于升级待确认态（upgrade_available=1），自动起 COMMIT：
     * health check 通过则清 upgrade_available，停止 bootcount 计数；
     * 失败不主动回滚，交 U-Boot bootlimit。 */
    char avail[16] = {0};
    if (fw_getenv("upgrade_available", avail, sizeof(avail)) == 0 &&
        strcmp(avail, "1") == 0) {
        APP_LOGI(OTA_MODULE, "init: upgrade_available=1, auto-commit");
        start_worker(worker_commit);
    }
}

void service_ota_update(void)
{
    /* Phase 1：无周期性轮询任务（CHECK 由 UI 触发）。预留。 */
}

void service_ota_deinit(void)
{
    if (!g_ota.initialized) {
        return;
    }
    g_ota.initialized = false;
    pthread_mutex_destroy(&g_ota.lock);
}
