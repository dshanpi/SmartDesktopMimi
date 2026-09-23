/* service_cloud.c — 100ask Cloud 服务（lv_backend 内），方式1（SoC 芯片签名自主注册）。
 *
 * 流程（service_cloud_init）：
 *   1. 读 cpuid（iot_read_chipid → /sys/class/sunxi_info/sys_info）→ deviceId
 *   2. 读 device_sig（iot_read_device_sig → /etc/100ask/device_sig，出厂签名）
 *   3. 读缓存 secret（/etc/100ask/secret）：
 *        有 → 直接用；无 → provision 注册
 *   4. provision（仅首次）：libcurl HTTPS POST https://www.100ask.net/api/device/provision
 *        application/x-www-form-urlencoded deviceId/name/chipid/signature → 解析 secret → 原子缓存
 *      用户进入设置页时再用 deviceId+secret 按需刷新短期 bindToken，工厂阶段 token 不落盘
 *   5. iot_init(deviceId=cpuid, secret, mqtt_host=120.76.140.213:1883) → iot_connect → 上报版本
 *
 * 云端 OTA：on_ota 回调 → service_ota_handle_command(OTA_CMD_DOWNLOAD, url) 静默下载
 *   （复用 service_ota 的 wget 下载 + swupdate A/B + 回滚），下载完成经 TOPIC_CLOUD_STATUS
 *   通知 UI 弹安装模态框。
 *
 * 服务器配置（可选）：/etc/100ask/cloud.conf 只放 MQTT_HOST/PORT/USE_TLS/CA_FILE/PROVISION_URL。
 * 缺失用默认值（120.76.140.213:1883 + www.100ask.net provision）——联调零配置。
 * 凭据不进 cloud.conf：deviceId=cpuid 自动读，secret=注册获取并缓存，device_sig=出厂烧录。
 */

#include "service_cloud.h"

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
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "backend_types.h"
#include "log/app_log.h"
#include "service_ota.h"
#include "service_cloud_validation.h"
#include "service_sensor.h"
#include "service_wifi.h"
#include "../middleware/middleware.h"
#include "100ask_iot.h"
#include <curl/curl.h>
#include <json-c/json.h>

#define CLOUD_MODULE "cloud"
#define CLOUD_CONF_FILE     "/etc/100ask/cloud.conf"
#define SECRET_CACHE_FILE   "/etc/100ask/secret"
#define LEGACY_BIND_TOKEN_FILE "/etc/100ask/bind_token"
#define LOCAL_VERSION_FILE  "/etc/aitvbox-version"
#define CLOUD_RECONNECT_INTERVAL_MS 5000
#define CLOUD_IOT_LOOP_TIMEOUT_MS 100
#define CLOUD_HEARTBEAT_INTERVAL_MS 60000
#define CLOUD_SENSOR_REPORT_INTERVAL_MS 30000
#define CLOUD_CREDENTIAL_RETRY_MS 1000
#define CLOUD_PROVISION_RETRY_INITIAL_MS 5000
#define CLOUD_PROVISION_RETRY_MAX_MS 300000
#define BIND_TOKEN_VALIDITY_SEC 3600
#define WGET_BIN            "/usr/bin/wget"
#define CLOUD_LOG_FILE      "/tmp/cloud.log"

#define MQTT_HOST_DEFAULT      "120.76.140.213"
#define MQTT_PORT_DEFAULT      1883
#define PROVISION_URL_DEFAULT  "https://www.100ask.net/api/device/provision"
#define LATEST_JSON_URL_DEFAULT \
    "https://dl.100ask.net/Hardware/MPU/ai-desktop/ai-desktop-system.json"
#define OTA_CHECK_BUF_SIZE     4096
#define OTA_TARGET_VER_LEN     64
#define OTA_TARGET_SHA_LEN     65
#define OTA_TARGET_URL_LEN     512
/* 用户点「稍后」后，把提醒截止时间 + 目标版本 + 目标 sha256 持久化到 /overlay，
 * 24h 内同版本不重弹；开机兜底校验包是否仍是同一个。文件格式（单行）：
 *   <until_ts> <target_version> <target_sha256> */
#define OTA_LATER_FILE         "/etc/100ask/ota_later"
#define OTA_LATER_DURATION_SEC (24 * 60 * 60)  /* 稍后 24h */
#define OTA_LATER_MIN_YEAR     2025            /* 低于此年份视为时间未校准 */

static struct {
    bool initialized;
    bool configured;          /* cpuid + device_sig + secret 齐全，可连云 */
    bool have_credentials;    /* cpuid + device_sig 读取成功（可 provision） */
    bool provisioning;        /* 正在 provision（避免重复） */
    bool bind_refresh_requested; /* UI 按需请求短期绑定码 */
    int64_t bind_token_expires_ms; /* 本机倒计时；不持久化过期绑定码 */
    bool mqtt_started;        /* MQTT 已 init+connect（无需再 provision） */
    iot_config_t iot_cfg;
    char provision_url[256];
    char latest_json_url[512];
    char cpuid[256];
    char device_sig[512];
    cloud_status_t status;
    cloud_status_t last_published;
    bool has_published;
    int64_t next_reconnect_ms;
    int64_t next_provision_ms;  /* provision 重试退避 */
    int64_t provision_retry_delay_ms; /* 5s 起步，指数退避到 5min */
    int64_t next_credential_check_ms; /* 等出厂助手写入 device_sig */
    int64_t last_heartbeat_ms;  /* 上次心跳时间 */
    bool tuya_license_ready;    /* 涂鸦运行时已接受当前 License */
    bool tuya_license_report_pending; /* 等 100ask MQTT 可用后可靠上报 */
    char local_version[64];
    /* 云端 OTA 检查（收到 action:check 后异步处理） */
    bool check_pending;       /* 收到 check，待 update 处理 */
    bool check_in_progress;   /* 正在拉 latest.json/比对（避免重入） */
    bool mandatory;           /* latest.json mandatory=true，强制安装 */
    char target_version[OTA_TARGET_VER_LEN];  /* 上报 ota/status 用 */
    char target_url[OTA_TARGET_URL_LEN];
    char target_sha256[OTA_TARGET_SHA_LEN];
    /* OTA 状态联动：上次上报到云的本地 OTA 状态，变化时才上报 */
    ota_state_t last_reported_ota_state;
    /* 温湿度上云节流 */
    int64_t last_sensor_report_ms;
} g_cloud;

typedef enum {
    CLOUD_HTTP_JOB_NONE = 0,
    CLOUD_HTTP_JOB_PROVISION,
    CLOUD_HTTP_JOB_BIND_TOKEN,
} cloud_http_job_kind_t;

typedef struct {
    uint64_t generation;
    cloud_http_job_kind_t kind;
    char url[256];
    char cpuid[256];
    char device_sig[512];
    char firmware_version[64];
    char device_secret[256];
    int result;
    int http_status;
    char result_secret[256];
    char result_bind_token[128];
} cloud_http_job_t;

static pthread_mutex_t g_http_job_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_curl_init_once = PTHREAD_ONCE_INIT;
static CURLcode g_curl_init_result = CURLE_OK;
static bool g_http_job_running;
static bool g_http_job_done;
static uint64_t g_http_generation;
static cloud_http_job_t g_http_job;

static void init_curl_once(void)
{
    g_curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
}

/* Volatile writes keep transient credentials from surviving in reusable stack
 * and heap storage after an HTTP request finishes. */
static void secure_clear(void *buffer, size_t length)
{
    volatile unsigned char *p = (volatile unsigned char *)buffer;
    while (length-- > 0) *p++ = 0;
}

static bool configure_https_transport(CURL *curl, bool allow_redirects)
{
    if (!curl) return false;
    return curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                            allow_redirects ? 1L : 0L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                            CURLPROTO_HTTPS) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                            CURLPROTO_HTTPS) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK;
}

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* 读 /etc/aitvbox-version 的 AITVBOX_VERSION= 字段。 */
static void read_local_version(char *out, size_t out_len)
{
    FILE *fp = fopen(LOCAL_VERSION_FILE, "r");
    if (!fp) { snprintf(out, out_len, "unknown"); return; }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "AITVBOX_VERSION=", 16) == 0) {
            char *v = line + 16;
            size_t n = strlen(v);
            while (n > 0 && (v[n-1] == '\n' || v[n-1] == '\r' || v[n-1] == ' ')) v[--n] = '\0';
            snprintf(out, out_len, "%s", v);
            fclose(fp);
            return;
        }
    }
    fclose(fp);
    snprintf(out, out_len, "unknown");
}

/* URL-encode 一段（仅字母数字不编码，其余 %HH）。供 provision urlencoded body 用。 */
static void url_encode(char *dst, size_t dst_len, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    if (!dst || dst_len == 0) return;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 1 < dst_len; p++) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            dst[o++] = (char)*p;
        } else {
            if (o + 3 >= dst_len) break;
            dst[o++] = '%';
            dst[o++] = hex[*p >> 4];
            dst[o++] = hex[*p & 0x0F];
        }
    }
    dst[o] = '\0';
}

/* JSON 字段抽取：{"key": "value"} → value 到 out。strstr 风格，不引 JSON 库。
 * 兼容冒号后有无空格（latest.json 是 "key": "v" 带空格，provision 响应可能无空格）。 */
static int json_extract_string(const char *json, const char *key, char *out, size_t out_len)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;  /* 跳过冒号后的空白 */
    if (*p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t n = (size_t)(end - p);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

/* 解析 cloud.conf：只放服务器配置（KEY=VALUE），缺失字段保留默认值。 */
static void load_cloud_conf(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;
        char *nl = strpbrk(val, "\r\n"); if (nl) *nl = '\0';
        if (strcmp(key, "MQTT_HOST") == 0) snprintf(g_cloud.iot_cfg.mqtt_host, sizeof(g_cloud.iot_cfg.mqtt_host), "%s", val);
        else if (strcmp(key, "MQTT_PORT") == 0) g_cloud.iot_cfg.mqtt_port = atoi(val);
        else if (strcmp(key, "USE_TLS") == 0) g_cloud.iot_cfg.use_tls = (atoi(val) != 0 || strcasecmp(val, "true") == 0);
        else if (strcmp(key, "CA_FILE") == 0) snprintf(g_cloud.iot_cfg.ca_file, sizeof(g_cloud.iot_cfg.ca_file), "%s", val);
        else if (strcmp(key, "PROVISION_URL") == 0) snprintf(g_cloud.provision_url, sizeof(g_cloud.provision_url), "%s", val);
        else if (strcmp(key, "LATEST_JSON_URL") == 0) snprintf(g_cloud.latest_json_url, sizeof(g_cloud.latest_json_url), "%s", val);
    }
    fclose(fp);
}

static bool atomic_write_text_file(const char *path, const char *value, mode_t mode)
{
    char tmp[320];
    char line[320];
    int fd = -1;
    int dir_fd = -1;
    bool ok = false;
    size_t len;

    if (!path || !value) return false;
    if (snprintf(line, sizeof(line), "%s\n", value) >= (int)sizeof(line)) return false;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp)) return false;

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) goto done;
    len = strlen(line);
    for (size_t written = 0; written < len;) {
        ssize_t n = write(fd, line + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            goto done;
        }
        written += (size_t)n;
    }
    if (fchmod(fd, mode) != 0 || fsync(fd) != 0 || close(fd) != 0) {
        fd = -1;
        goto done;
    }
    fd = -1;
    if (rename(tmp, path) != 0) goto done;

    dir_fd = open("/etc/100ask", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0 || fsync(dir_fd) != 0) goto done;
    ok = true;

done:
    if (fd >= 0) close(fd);
    if (dir_fd >= 0) close(dir_fd);
    if (!ok) unlink(tmp);
    return ok;
}

static bool load_cached_secret(char *out, size_t out_len)
{
    FILE *fp = fopen(SECRET_CACHE_FILE, "r");
    if (!fp) return false;
    if (fgets(out, (int)out_len, fp) == NULL) { fclose(fp); return false; }
    fclose(fp);
    char *nl = strpbrk(out, "\r\n"); if (nl) *nl = '\0';
    return service_cloud_credential_value_valid(
        out, SERVICE_CLOUD_CREDENTIAL_MIN_LENGTH,
        SERVICE_CLOUD_CREDENTIAL_MAX_LENGTH);
}

static bool save_cached_secret(const char *secret)
{
    if (!service_cloud_credential_value_valid(
            secret, SERVICE_CLOUD_CREDENTIAL_MIN_LENGTH,
            SERVICE_CLOUD_CREDENTIAL_MAX_LENGTH)) {
        APP_LOGW(CLOUD_MODULE, "refusing to save malformed device secret");
        return false;
    }
    if (!atomic_write_text_file(SECRET_CACHE_FILE, secret, 0600)) {
        APP_LOGW(CLOUD_MODULE, "save secret atomically failed: %s", strerror(errno));
        return false;
    }
    return true;
}

/* provision：用 libcurl 做 HTTPS POST（application/x-www-form-urlencoded），
 * 解析返回的 secret。返回 0 成功。设备 busybox wget 不支持 https/--header，故用 libcurl。
 * libcurl + libssl 设备 rootfs 已自带（薄包依赖 libcurl）。 */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} curl_buffer_t;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    curl_buffer_t *r = userdata;
    size_t total = size * nmemb;
    size_t copy = (r->len + total < r->cap) ? total : (r->cap - r->len);
    if (copy > 0) {
        memcpy(r->buf + r->len, ptr, copy);
        r->len += copy;
        r->buf[r->len] = '\0';
    }
    if (copy != total) r->overflow = true;
    return total;
}

static int json_response_string(const char *json, const char *key,
                                char *out, size_t out_len)
{
    struct json_object *root = NULL;
    struct json_object *container = NULL;
    struct json_object *item = NULL;
    const char *value;
    size_t len;
    int ret = -1;

    if (!json || !key || !out || out_len == 0) return -1;
    out[0] = '\0';
    root = json_tokener_parse(json);
    if (!root || !json_object_is_type(root, json_type_object)) goto done;
    container = root;
    if (!json_object_object_get_ex(container, key, &item)) {
        struct json_object *data = NULL;
        if (!json_object_object_get_ex(root, "data", &data) ||
            !json_object_is_type(data, json_type_object) ||
            !json_object_object_get_ex(data, key, &item)) {
            goto done;
        }
    }
    if (!json_object_is_type(item, json_type_string)) goto done;
    value = json_object_get_string(item);
    len = value ? strlen(value) : 0;
    if (len == 0 || len >= out_len) goto done;
    memcpy(out, value, len + 1);
    ret = 0;

done:
    if (root) json_object_put(root);
    return ret;
}

static int do_provision(const char *url, const char *firmware_version,
                        const char *cpuid, const char *device_sig,
                        char *secret_out, size_t secret_len,
                        int *http_status_out)
{
    char resp[4096] = {0};
    char returned_device_id[256] = {0};
    curl_buffer_t wr = { resp, sizeof(resp) - 1, 0, false };
    int ret = -1;

    if (http_status_out) *http_status_out = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        APP_LOGE(CLOUD_MODULE, "curl_easy_init failed");
        return -1;
    }

    /* application/x-www-form-urlencoded body（后台文档第 2.1 节明确要求 urlencoded）：
     * deviceId/name/chipid/signature + 可选 deviceModel/firmwareVersion。
     * 用已有 url_encode 转义各字段值。 */
    char enc_devid[512], enc_name[512], enc_chipid[512], enc_sig[2048], enc_fw[128];
    url_encode(enc_devid, sizeof(enc_devid), cpuid);
    /* name 统一显示名，不参与认证（认证靠 deviceId=完整 cpuid）；多台设备同名，
     * 后台靠 deviceId 区分。仅 provision 时上报一次。 */
    url_encode(enc_name, sizeof(enc_name), "ai-desktop");
    url_encode(enc_chipid, sizeof(enc_chipid), cpuid);
    url_encode(enc_sig, sizeof(enc_sig), device_sig);
    const char *fw = (firmware_version && firmware_version[0]) ? firmware_version : "unknown";
    url_encode(enc_fw, sizeof(enc_fw), fw);
    char body[4096];
    snprintf(body, sizeof(body),
             "deviceId=%s&name=%s&chipid=%s&signature=%s&deviceModel=AITVBox&firmwareVersion=%s",
             enc_devid, enc_name, enc_chipid, enc_sig, enc_fw);

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
    if (!hdrs) {
        APP_LOGE(CLOUD_MODULE, "provision request header allocation failed");
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    /* This request carries a device signature. Never redirect its POST body. */
    if (!configure_https_transport(curl, false)) {
        APP_LOGE(CLOUD_MODULE, "failed to configure strict HTTPS transport");
        goto cleanup;
    }

    APP_LOGI(CLOUD_MODULE, "provision POST %s", url);
    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_status_out) *http_status_out = (int)http_code;

    if (rc != CURLE_OK) {
        APP_LOGW(CLOUD_MODULE, "provision curl failed: rc=%d %s", (int)rc, curl_easy_strerror(rc));
    } else if (wr.overflow) {
        APP_LOGW(CLOUD_MODULE, "provision response exceeds %zu bytes", sizeof(resp) - 1);
    } else if (http_code != 200) {
        APP_LOGW(CLOUD_MODULE, "provision rejected (http=%ld)", http_code);
    } else if (json_response_string(resp, "secret", secret_out, secret_len) == 0 &&
               json_response_string(resp, "deviceId", returned_device_id,
                                    sizeof(returned_device_id)) == 0) {
        if (strcmp(returned_device_id, cpuid) != 0) {
            APP_LOGE(CLOUD_MODULE,
                     "provision returned deviceId=%s but local MQTT username=%s; backend chipid binding mismatch",
                     returned_device_id, cpuid);
            ret = -2;
            goto cleanup;
        }
        if (!service_cloud_credential_value_valid(
                secret_out, SERVICE_CLOUD_CREDENTIAL_MIN_LENGTH,
                SERVICE_CLOUD_CREDENTIAL_MAX_LENGTH)) {
            APP_LOGW(CLOUD_MODULE, "provision returned malformed secret");
            goto cleanup;
        }
        ret = 0;
        APP_LOGI(CLOUD_MODULE, "provision ok (http=%ld), secret obtained", http_code);
    } else {
        APP_LOGW(CLOUD_MODULE, "provision response schema invalid (http=%ld)", http_code);
    }

cleanup:
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    if (ret != 0 && secret_out && secret_len > 0) {
        secure_clear(secret_out, secret_len);
    }
    secure_clear(body, sizeof(body));
    secure_clear(enc_sig, sizeof(enc_sig));
    secure_clear(resp, sizeof(resp));
    return ret;
}

static int do_refresh_bind_token(const char *url, const char *cpuid,
                                 const char *device_secret,
                                 char *token_out, size_t token_len,
                                 int *http_status_out)
{
    char resp[2048] = {0};
    char returned_device_id[256] = {0};
    char enc_devid[512], enc_secret[1024], body[1800];
    curl_buffer_t wr = { resp, sizeof(resp) - 1, 0, false };
    CURL *curl = NULL;
    struct curl_slist *hdrs = NULL;
    int ret = -1;
    long http_code = 0;

    if (http_status_out) *http_status_out = 0;
    if (token_out && token_len > 0) token_out[0] = '\0';
    url_encode(enc_devid, sizeof(enc_devid), cpuid);
    url_encode(enc_secret, sizeof(enc_secret), device_secret);
    snprintf(body, sizeof(body), "deviceId=%s&secret=%s", enc_devid, enc_secret);

    curl = curl_easy_init();
    if (!curl) {
        secure_clear(body, sizeof(body));
        secure_clear(enc_secret, sizeof(enc_secret));
        return -1;
    }
    hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
    if (!hdrs) {
        APP_LOGE(CLOUD_MODULE, "bind token request header allocation failed");
        goto done;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    if (!configure_https_transport(curl, false)) {
        APP_LOGE(CLOUD_MODULE, "failed to configure strict HTTPS transport");
        goto done;
    }

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_status_out) *http_status_out = (int)http_code;
    if (rc != CURLE_OK) {
        APP_LOGW(CLOUD_MODULE, "bind token refresh failed: rc=%d %s",
                 (int)rc, curl_easy_strerror(rc));
        goto done;
    }
    if (wr.overflow || http_code != 200) {
        APP_LOGW(CLOUD_MODULE, "bind token refresh rejected (http=%ld overflow=%d)",
                 http_code, wr.overflow ? 1 : 0);
        goto done;
    }
    if (json_response_string(resp, "deviceId", returned_device_id,
                             sizeof(returned_device_id)) != 0 ||
        strcmp(returned_device_id, cpuid) != 0 ||
        json_response_string(resp, "bindToken", token_out, token_len) != 0 ||
        !service_cloud_bind_token_valid(token_out)) {
        APP_LOGW(CLOUD_MODULE, "bind token response schema invalid");
        goto done;
    }
    ret = 0;

done:
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    if (ret != 0 && token_out && token_len > 0) {
        secure_clear(token_out, token_len);
    }
    secure_clear(body, sizeof(body));
    secure_clear(enc_secret, sizeof(enc_secret));
    secure_clear(resp, sizeof(resp));
    return ret;
}

static void *cloud_http_worker(void *arg)
{
    (void)arg;
    cloud_http_job_t local;

    pthread_mutex_lock(&g_http_job_lock);
    local = g_http_job;
    secure_clear(g_http_job.device_sig, sizeof(g_http_job.device_sig));
    secure_clear(g_http_job.device_secret, sizeof(g_http_job.device_secret));
    pthread_mutex_unlock(&g_http_job_lock);

    if (local.kind == CLOUD_HTTP_JOB_PROVISION) {
        local.result = do_provision(local.url, local.firmware_version,
                                    local.cpuid, local.device_sig,
                                    local.result_secret,
                                    sizeof(local.result_secret),
                                    &local.http_status);
    } else if (local.kind == CLOUD_HTTP_JOB_BIND_TOKEN) {
        local.result = do_refresh_bind_token(local.url, local.cpuid,
                                             local.device_secret,
                                             local.result_bind_token,
                                             sizeof(local.result_bind_token),
                                             &local.http_status);
    } else {
        local.result = -1;
    }

    secure_clear(local.device_sig, sizeof(local.device_sig));
    secure_clear(local.device_secret, sizeof(local.device_secret));

    pthread_mutex_lock(&g_http_job_lock);
    if (local.generation == g_http_generation) {
        g_http_job = local;
        g_http_job_done = true;
    } else {
        secure_clear(&g_http_job, sizeof(g_http_job));
        g_http_job_done = false;
    }
    g_http_job_running = false;
    pthread_mutex_unlock(&g_http_job_lock);
    secure_clear(&local, sizeof(local));
    return NULL;
}

static bool start_cloud_http_job(cloud_http_job_kind_t kind)
{
    cloud_http_job_t job;
    pthread_t thread;
    int create_rc;

    pthread_once(&g_curl_init_once, init_curl_once);
    if (g_curl_init_result != CURLE_OK) {
        APP_LOGE(CLOUD_MODULE, "libcurl global init failed: %s",
                 curl_easy_strerror(g_curl_init_result));
        return false;
    }

    memset(&job, 0, sizeof(job));
    job.kind = kind;
    snprintf(job.url, sizeof(job.url), "%s",
             g_cloud.provision_url[0] ? g_cloud.provision_url : PROVISION_URL_DEFAULT);
    snprintf(job.cpuid, sizeof(job.cpuid), "%s", g_cloud.cpuid);
    snprintf(job.device_sig, sizeof(job.device_sig), "%s", g_cloud.device_sig);
    snprintf(job.firmware_version, sizeof(job.firmware_version), "%s", g_cloud.local_version);
    snprintf(job.device_secret, sizeof(job.device_secret), "%s", g_cloud.iot_cfg.device_secret);

    pthread_mutex_lock(&g_http_job_lock);
    if (g_http_job_running || g_http_job_done) {
        pthread_mutex_unlock(&g_http_job_lock);
        secure_clear(&job, sizeof(job));
        return false;
    }
    job.generation = g_http_generation;
    g_http_job = job;
    g_http_job_running = true;
    pthread_mutex_unlock(&g_http_job_lock);
    secure_clear(&job, sizeof(job));

    create_rc = pthread_create(&thread, NULL, cloud_http_worker, NULL);
    if (create_rc != 0) {
        APP_LOGE(CLOUD_MODULE, "start cloud HTTP worker failed: %s", strerror(create_rc));
        pthread_mutex_lock(&g_http_job_lock);
        g_http_job_running = false;
        secure_clear(&g_http_job, sizeof(g_http_job));
        pthread_mutex_unlock(&g_http_job_lock);
        return false;
    }
    pthread_detach(thread);
    return true;
}

static bool take_cloud_http_result(cloud_http_job_t *out)
{
    bool available = false;
    pthread_mutex_lock(&g_http_job_lock);
    if (g_http_job_done) {
        if (out) *out = g_http_job;
        secure_clear(&g_http_job, sizeof(g_http_job));
        g_http_job_done = false;
        available = true;
    }
    pthread_mutex_unlock(&g_http_job_lock);
    return available;
}

/* GET 一个 URL 到内存（latest.json 用，复用 curl_write_cb 模式）。返回 0 成功。 */
static int http_get(const char *url, char *buf, size_t buf_len)
{
    curl_buffer_t wr = { buf, buf_len - 1, 0, false };
    buf[0] = '\0';
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    if (!configure_https_transport(curl, true)) {
        APP_LOGE(CLOUD_MODULE, "failed to configure strict HTTPS transport");
        curl_easy_cleanup(curl);
        return -1;
    }
    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK || http_code != 200 || wr.overflow) {
        APP_LOGW(CLOUD_MODULE, "http_get %s failed rc=%d http=%ld", url, (int)rc, http_code);
        return -1;
    }
    return 0;
}

/* JSON 布尔字段抽取："key":true/false → 1/0。找不到返回 default_val。 */
static int json_extract_bool(const char *json, const char *key, int default_val)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return default_val;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return default_val;
}

/* semver 整数比对：a<b→-1, a==b→0, a>b→1。非法版本视为 (0,0,0)。
 * 三段按整数比（非字典序），参考 versioning.md 第 4.1 节。 */
static int version_cmp(const char *a, const char *b)
{
    int av[3] = {0,0,0}, bv[3] = {0,0,0};
    sscanf(a ? a : "", "%d.%d.%d", &av[0], &av[1], &av[2]);
    sscanf(b ? b : "", "%d.%d.%d", &bv[0], &bv[1], &bv[2]);
    for (int i = 0; i < 3; i++) {
        if (av[i] < bv[i]) return -1;
        if (av[i] > bv[i]) return 1;
    }
    return 0;
}

static void publish_cloud_status(void)
{
    mw_publish(TOPIC_CLOUD_STATUS, &g_cloud.status, sizeof(g_cloud.status),
               MW_DIR_BACKEND_TO_UI);
    g_cloud.last_published = g_cloud.status;
    g_cloud.has_published = true;
}

static bool status_equal(const cloud_status_t *a, const cloud_status_t *b)
{
    return a->state == b->state &&
           a->bind_token_refreshing == b->bind_token_refreshing &&
           a->bind_token_error == b->bind_token_error &&
           a->ota_pending == b->ota_pending &&
           a->ota_mandatory == b->ota_mandatory &&
           strcmp(a->device_id, b->device_id) == 0 &&
           strcmp(a->bind_token, b->bind_token) == 0 &&
           strcmp(a->ota_version, b->ota_version) == 0;
}

/* ---- 「稍后」延后提醒持久化 ----
 * /etc/100ask/ota_later 单行：<until_ts> <target_version> <target_sha256>
 * 用户点稍后时写入；check 时读出比对；开机兜底也读。 */

/* 时间是否已校准（年份 >= OTA_LATER_MIN_YEAR）。开机初期未 NTP 校时时返回 false。 */
static bool time_is_sane(void)
{
    time_t t = time(NULL);
    struct tm tm;
    if (!localtime_r(&t, &tm)) return false;
    return tm.tm_year + 1900 >= OTA_LATER_MIN_YEAR;
}

/* 读 ota_later。成功返回 0 并填出字段；文件不存在/格式错返回 -1。 */
static int ota_later_read(time_t *until, char *ver, size_t ver_len,
                          char *sha, size_t sha_len)
{
    FILE *fp = fopen(OTA_LATER_FILE, "r");
    if (!fp) return -1;
    long ts = 0;
    char v[OTA_TARGET_VER_LEN] = {0};
    char s[OTA_TARGET_SHA_LEN] = {0};
    int n = fscanf(fp, "%ld %63s %64s", &ts, v, s);
    fclose(fp);
    if (n < 1 || ts <= 0) return -1;
    if (until) *until = (time_t)ts;
    if (ver && ver_len) snprintf(ver, ver_len, "%s", v);
    if (sha && sha_len) snprintf(sha, sha_len, "%s", s);
    return 0;
}

/* 写 ota_later：当前时间 + OTA_LATER_DURATION_SEC，带目标版本与 sha256。 */
static void ota_later_write(const char *ver, const char *sha)
{
    FILE *fp = fopen(OTA_LATER_FILE, "w");
    if (!fp) return;
    fprintf(fp, "%ld %s %s\n", (long)(time(NULL) + OTA_LATER_DURATION_SEC),
            ver ? ver : "", sha ? sha : "");
    fclose(fp);
}

static void ota_later_clear(void)
{
    unlink(OTA_LATER_FILE);
}

/* ---- SDK 回调 ---- */

static void on_status(iot_status_t status)
{
    cloud_state_t new_state;
    switch (status) {
        case IOT_STATUS_CONNECTED: new_state = CLOUD_STATE_CONNECTED; break;
        case IOT_STATUS_INIT:      new_state = CLOUD_STATE_CONNECTING; break;
        case IOT_STATUS_OFFLINE:
        case IOT_STATUS_ERROR:
        default:                   new_state = CLOUD_STATE_DISCONNECTED; break;
    }
    if (g_cloud.status.state != new_state) {
        g_cloud.status.state = new_state;
        APP_LOGI(CLOUD_MODULE, "cloud state=%d", new_state);
    }
    /* 连上后上报当前版本（带 boot_slot + committed），让云端知道是否需要推 OTA */
    if (status == IOT_STATUS_CONNECTED && g_cloud.local_version[0]) {
        char slot = service_ota_get_boot_slot();
        char slot_s[2] = {0, 0};
        if (slot == 'A' || slot == 'B') { slot_s[0] = slot; }
        iot_report_version(g_cloud.local_version, slot_s, "committed");
        g_cloud.last_heartbeat_ms = now_ms();  /* 连上即重置心跳计时 */
        /* 每次重连都重发当前验收状态，避免云端在工单重置后沿用旧结果。 */
        g_cloud.tuya_license_report_pending = true;
    }
}

/* 收到后台 {"action":"check"}：不在 MQTT 回调里阻塞，置标志由 update 异步处理。 */
static void on_ota_check(void)
{
    APP_LOGI(CLOUD_MODULE, "cloud ota check received");
    g_cloud.check_pending = true;
}

static void on_cmd(const char *cmd, const char *payload)
{
    (void)payload;
    APP_LOGI(CLOUD_MODULE, "cloud cmd: %s", cmd ? cmd : "?");
}

/* 把本地 OTA 状态机映射成云端 ota/status 字符串。返回 NULL 表示无需上报。 */
static const char *ota_state_to_cloud(ota_state_t s)
{
    switch (s) {
    case OTA_STATE_CHECKING:         return "checking";
    case OTA_STATE_UPDATE_AVAILABLE: return "update_available";
    case OTA_STATE_UP_TO_DATE:       return "no_update";
    case OTA_STATE_DOWNLOADING:      return "downloading";
    case OTA_STATE_DOWNLOAD_DONE:    return "downloaded";
    case OTA_STATE_APPLYING:         return "installing";
    case OTA_STATE_REBOOTING:        return "rebooting";
    case OTA_STATE_COMMITTING:       return "health_check";
    case OTA_STATE_ERROR:            return "failed";
    case OTA_STATE_IDLE:             return "completed";  /* commit 后回 IDLE 视为完成 */
    default:                         return NULL;
    }
}

/* service_ota 状态监听回调：本地 OTA 状态变化时联动上报到云端 ota/status。 */
static void on_ota_status(const ota_status_t *st)
{
    if (!st) return;
    if (st->state == g_cloud.last_reported_ota_state) return;  /* 仅变化时上报 */
    g_cloud.last_reported_ota_state = st->state;

    const char *cloud_st = ota_state_to_cloud(st->state);
    if (!cloud_st) return;

    char slot = service_ota_get_boot_slot();
    char slot_s[2] = {0, 0};
    if (slot == 'A' || slot == 'B') slot_s[0] = slot;

    /* COMPLETED 后再发一次 version committed（文档第 7.6 节）。
     * 注意：IDLE 也用于初始空闲态，仅当刚走过 commit 才发——用 upgrade_available 已清作判据。 */
    if (st->state == OTA_STATE_IDLE) {
        iot_report_ota_status(cloud_st, g_cloud.local_version,
                              g_cloud.target_version[0] ? g_cloud.target_version : "",
                              100, slot_s, "Firmware update completed");
        iot_report_version(g_cloud.local_version, slot_s, "committed");
    } else {
        iot_report_ota_status(cloud_st, g_cloud.local_version,
                              g_cloud.target_version[0] ? g_cloud.target_version : "",
                              st->progress, slot_s, "");
    }
}

/* service_sensor 监听回调：AHT20 每次读到有效数据时调。30s 节流上云一次
 * （后台 t_sensor_data 表，频次过高会爆）。本机 UI 仍 3s 刷新不受影响。
 * 上报两条单条 sensor 消息：temperature(°C) + humidity(%)，格式见后台文档第 6 节。 */
static void on_sensor(const sensor_status_t *st)
{
    if (!st || !st->valid) return;
    if (iot_get_status() != IOT_STATUS_CONNECTED) return;
    int64_t now = now_ms();
    if (now - g_cloud.last_sensor_report_ms < CLOUD_SENSOR_REPORT_INTERVAL_MS) return;
    g_cloud.last_sensor_report_ms = now;

    /* temp_mC 毫摄氏度 → ℃ (除 1000)，humi_mpermil 千分比 → % (除 1000)。 */
    double temp_c = st->temp_mC / 1000.0;
    double humi_p = st->humi_mpermil / 1000.0;
    /* 一条批量消息带温湿度两项（后台文档第 6 节批量格式 {"data":[...]}）。 */
    iot_sensor_t items[2];
    memset(items, 0, sizeof(items));
    snprintf(items[0].type, sizeof(items[0].type), "temperature");
    items[0].value = temp_c;
    snprintf(items[0].unit, sizeof(items[0].unit), "°C");
    snprintf(items[1].type, sizeof(items[1].type), "humidity");
    items[1].value = humi_p;
    snprintf(items[1].unit, sizeof(items[1].unit), "%%");
    iot_report_sensors(items, 2);
    APP_LOGI(CLOUD_MODULE, "sensor reported: temp=%.1f°C humi=%.1f%%", temp_c, humi_p);
}

/* ---- 公开 API ---- */

static bool fixed_hex_string_valid(const char *value, size_t length)
{
    if (!value || strlen(value) != length) return false;
    for (size_t i = 0; i < length; i++) {
        char ch = value[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) return false;
    }
    return true;
}

static bool device_signature_format_valid(const char *value)
{
    size_t len = value ? strlen(value) : 0;
    if (len < 128 || len > 160 || (len & 1U) != 0) return false;
    for (size_t i = 0; i < len; i++) {
        char ch = value[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) return false;
    }
    return true;
}

static bool try_load_device_credentials(void)
{
    if (!g_cloud.cpuid[0]) {
        if (iot_read_chipid(g_cloud.cpuid, sizeof(g_cloud.cpuid)) != 0 ||
            !fixed_hex_string_valid(g_cloud.cpuid, 32)) {
            g_cloud.cpuid[0] = '\0';
            APP_LOGW(CLOUD_MODULE, "device CPUID unavailable or malformed; waiting for retry");
            return false;
        }
        for (size_t i = 0; g_cloud.cpuid[i]; i++) {
            if (g_cloud.cpuid[i] >= 'A' && g_cloud.cpuid[i] <= 'F') {
                g_cloud.cpuid[i] = (char)(g_cloud.cpuid[i] - 'A' + 'a');
            }
        }
        /* fixed_hex_string_valid() guarantees an exact 32-byte CPUID. */
        memcpy(g_cloud.iot_cfg.device_id, g_cloud.cpuid, 33);
        snprintf(g_cloud.status.device_id, sizeof(g_cloud.status.device_id), "%.63s", g_cloud.cpuid);
    }

    if (iot_read_device_sig(g_cloud.device_sig, sizeof(g_cloud.device_sig)) != 0 ||
        !device_signature_format_valid(g_cloud.device_sig)) {
        g_cloud.device_sig[0] = '\0';
        APP_LOGW(CLOUD_MODULE, "device_sig unavailable or malformed; waiting for factory assistant");
        return false;
    }

    char secret[256] = {0};
    if (load_cached_secret(secret, sizeof(secret))) {
        snprintf(g_cloud.iot_cfg.device_secret, sizeof(g_cloud.iot_cfg.device_secret), "%s", secret);
        g_cloud.configured = true;
        APP_LOGI(CLOUD_MODULE, "device credentials loaded; cached secret present");
    } else {
        g_cloud.iot_cfg.device_secret[0] = '\0';
        g_cloud.configured = false;
        g_cloud.next_provision_ms = now_ms();
        APP_LOGI(CLOUD_MODULE, "device signature loaded; provision required");
    }
    secure_clear(secret, sizeof(secret));
    g_cloud.have_credentials = true;
    return true;
}

void service_cloud_init(void)
{
    memset(&g_cloud, 0, sizeof(g_cloud));
    pthread_mutex_lock(&g_http_job_lock);
    g_http_generation++;
    if (!g_http_job_running) {
        secure_clear(&g_http_job, sizeof(g_http_job));
        g_http_job_done = false;
    }
    pthread_mutex_unlock(&g_http_job_lock);
    g_cloud.status.state = CLOUD_STATE_DISCONNECTED;
    g_cloud.provision_retry_delay_ms = CLOUD_PROVISION_RETRY_INITIAL_MS;
    pthread_once(&g_curl_init_once, init_curl_once);
    if (g_curl_init_result != CURLE_OK) {
        APP_LOGE(CLOUD_MODULE, "libcurl global init failed: %s",
                 curl_easy_strerror(g_curl_init_result));
    }
    read_local_version(g_cloud.local_version, sizeof(g_cloud.local_version));

    /* 服务器配置默认值 + cloud.conf 覆盖（缺失用默认，联调零配置） */
    memset(&g_cloud.iot_cfg, 0, sizeof(g_cloud.iot_cfg));
    snprintf(g_cloud.iot_cfg.mqtt_host, sizeof(g_cloud.iot_cfg.mqtt_host), "%s", MQTT_HOST_DEFAULT);
    g_cloud.iot_cfg.mqtt_port = MQTT_PORT_DEFAULT;
    g_cloud.iot_cfg.keepalive = 60;
    g_cloud.iot_cfg.use_tls = false;
    snprintf(g_cloud.provision_url, sizeof(g_cloud.provision_url), "%s", PROVISION_URL_DEFAULT);
    snprintf(g_cloud.latest_json_url, sizeof(g_cloud.latest_json_url), "%s", LATEST_JSON_URL_DEFAULT);
    load_cloud_conf(CLOUD_CONF_FILE);
    /* 固件版本供 SDK 上线 status / version 上报用 */
    snprintf(g_cloud.iot_cfg.firmware_version, sizeof(g_cloud.iot_cfg.firmware_version),
             "%s", g_cloud.local_version[0] ? g_cloud.local_version : "unknown");

    /* 注册本地 OTA 状态监听：service_ota 状态变化时联动上报到云端 ota/status。
     * （mw_publish 是后端→UI 的 IPC，不回环后端进程内订阅，故用进程内 listener 而非 mw_subscribe。） */
    service_ota_set_status_listener(on_ota_status);
    /* 注册温湿度监听：AHT20 读到有效数据时 30s 节流上云 sensor topic。 */
    service_sensor_set_listener(on_sensor);

    /* 旧固件曾把一次性 bindToken 落盘并永久显示。新版始终按用户操作实时申请。 */
    unlink(LEGACY_BIND_TOKEN_FILE);

    /* 出厂助手可能在 backend 启动后才写入 device_sig；首次未读到时由 update 每秒重试，
     * 不再要求人工重启 backend。 */
    if (!try_load_device_credentials()) {
        g_cloud.next_credential_check_ms = now_ms() + CLOUD_CREDENTIAL_RETRY_MS;
    }

    /* 开机兜底：若上次有已下载完成的升级包且用户未在「稍后」静默期内，
     * 主动置 ota_pending 触发 UI 弹安装提示（重启后重新提醒）。
     * ota_later 文件里存了 target_version/target_sha256，据此校验本地包是否同一份。 */
    {
        time_t until = 0;
        char later_ver[OTA_TARGET_VER_LEN] = {0};
        char later_sha[OTA_TARGET_SHA_LEN] = {0};
        bool has_later = (ota_later_read(&until, later_ver, sizeof(later_ver),
                                         later_sha, sizeof(later_sha)) == 0);
        bool in_defer = false;
        if (has_later && time_is_sane() && time(NULL) < until) {
            in_defer = true;  /* 仍在稍后静默期，不弹 */
        }
        if (!in_defer) {
            /* 不在静默期：若本地包就绪（用 ota_later 的 sha256，没有就空校验存在性），
             * 重新提示安装。 */
            const char *chk_sha = (has_later && later_sha[0]) ? later_sha : NULL;
            if (service_ota_is_package_ready(chk_sha)) {
                snprintf(g_cloud.target_version, sizeof(g_cloud.target_version), "%s",
                         has_later ? later_ver : "");
                snprintf(g_cloud.target_sha256, sizeof(g_cloud.target_sha256), "%s",
                         has_later ? later_sha : "");
                g_cloud.status.ota_pending = true;
                g_cloud.status.ota_mandatory = false;  /* 兜底重弹不强制 */
                snprintf(g_cloud.status.ota_version, sizeof(g_cloud.status.ota_version), "%s",
                         has_later ? later_ver : "");
                APP_LOGI(CLOUD_MODULE, "boot: package ready, re-prompt install (ver=%s)", later_ver);
                /* 主动推一次 DOWNLOAD_DONE 触发 UI 弹窗（若 UI 尚未订阅，消息丢失，
                 * 但 ota_pending=true 会在 cloud_status 里持续携带，下次 check 也会重弹）。 */
                service_ota_mark_package_downloaded();
            }
            if (has_later) ota_later_clear();  /* 静默期已过，清记录 */
        }
    }

    g_cloud.initialized = true;
    publish_cloud_status();
}

/* 处理云端 {"action":"check"}：拉 latest.json → 比对版本 → 触发下载。
 * 在 update 主循环里异步跑（不阻塞 MQTT loop）。mandatory 强制即使同版本也下载。 */
static void process_ota_check(void)
{
    if (g_cloud.check_in_progress) return;
    g_cloud.check_in_progress = true;
    g_cloud.check_pending = false;

    char slot = service_ota_get_boot_slot();
    char slot_s[2] = {0, 0};
    if (slot == 'A' || slot == 'B') slot_s[0] = slot;
    iot_report_ota_status("checking", g_cloud.local_version, "", 0, slot_s, "Checking for updates");

    const char *url = g_cloud.latest_json_url[0] ? g_cloud.latest_json_url
                                                 : LATEST_JSON_URL_DEFAULT;
    char json[OTA_CHECK_BUF_SIZE] = {0};
    if (http_get(url, json, sizeof(json)) != 0) {
        iot_report_ota_status("failed", g_cloud.local_version, "", 0, slot_s, "Could not retrieve version information");
        g_cloud.check_in_progress = false;
        return;
    }

    char ver[OTA_TARGET_VER_LEN] = {0};
    char fwurl[OTA_TARGET_URL_LEN] = {0};
    char sha[OTA_TARGET_SHA_LEN] = {0};
    if (json_extract_string(json, "version", ver, sizeof(ver)) < 0 ||
        json_extract_string(json, "url", fwurl, sizeof(fwurl)) < 0) {
        iot_report_ota_status("failed", g_cloud.local_version, "", 0, slot_s, "Invalid version information");
        g_cloud.check_in_progress = false;
        return;
    }
    size_t fwurl_len = strlen(fwurl);
    if (fwurl_len >= sizeof(((ota_cmd_t *)0)->url)) {
        iot_report_ota_status("failed", g_cloud.local_version, ver, 0, slot_s,
                              "Update URL is too long");
        g_cloud.check_in_progress = false;
        return;
    }
    json_extract_string(json, "sha256", sha, sizeof(sha));  /* 可选 */
    int enabled = json_extract_bool(json, "enabled", 1);
    int mandatory = json_extract_bool(json, "mandatory", 0);
    g_cloud.mandatory = (mandatory != 0);

    APP_LOGI(CLOUD_MODULE, "ota check: local=%s remote=%s enabled=%d mandatory=%d",
             g_cloud.local_version, ver, enabled, mandatory);

    if (!enabled) {
        iot_report_ota_status("no_update", g_cloud.local_version, ver, 0, slot_s, "Firmware release is not enabled");
        g_cloud.check_in_progress = false;
        return;
    }

    /* 比对：mandatory 强制时即使同版本/旧版本也下载安装；否则仅 remote > local 才下载。 */
    int cmp = version_cmp(ver, g_cloud.local_version);
    if (!g_cloud.mandatory && cmp <= 0) {
        iot_report_ota_status("no_update", g_cloud.local_version, ver, 0, slot_s, "Already up to date");
        g_cloud.check_in_progress = false;
        return;
    }

    /* 「稍后」延后提醒：若用户对该版本点过稍后且未过 until，本次不弹窗。
     * 时间未校准时（开机初期）跳过此判定，避免误判 until。 */
    if (time_is_sane()) {
        time_t until = 0;
        char later_ver[OTA_TARGET_VER_LEN] = {0};
        if (ota_later_read(&until, later_ver, sizeof(later_ver), NULL, 0) == 0 &&
            strcmp(later_ver, ver) == 0 && time(NULL) < until) {
            APP_LOGI(CLOUD_MODULE, "ota check: deferred (later until %ld, ver %s)", (long)until, ver);
            iot_report_ota_status("no_update", g_cloud.local_version, ver, 0, slot_s, "Update postponed; reminder pending");
            g_cloud.check_in_progress = false;
            return;
        }
        /* until 已过或版本变了：清掉旧的稍后记录，正常流程。 */
        ota_later_clear();
    }

    /* 有新版（或强制）：记目标信息，上报 update_available。
     * 复用 service_ota 的 DOWNLOAD 链路（带 sha256 校验）。 */
    snprintf(g_cloud.target_version, sizeof(g_cloud.target_version), "%s", ver);
    snprintf(g_cloud.target_url, sizeof(g_cloud.target_url), "%s", fwurl);
    snprintf(g_cloud.target_sha256, sizeof(g_cloud.target_sha256), "%s", sha);

    iot_report_ota_status("update_available", g_cloud.local_version, ver, 0, slot_s,
                          g_cloud.mandatory ? "Mandatory update available" : "Update available");

    g_cloud.status.ota_pending = true;
    g_cloud.status.ota_mandatory = g_cloud.mandatory;
    snprintf(g_cloud.status.ota_version, sizeof(g_cloud.status.ota_version), "%s", ver);
    publish_cloud_status();

    /* 重下载守卫：若本地包已就绪且 sha256 匹配，不重复下载，直接让 UI 弹安装提示。
     * 跳过下载不会进 service_ota 下载流程，需主动推一次 DOWNLOAD_DONE 让 UI 弹窗。 */
    if (service_ota_is_package_ready(sha)) {
        APP_LOGI(CLOUD_MODULE, "ota check: package already ready (sha256=%c%c..), skip download",
                 sha[0] ? sha[0] : '-', sha[1] ? sha[1] : '-');
        iot_report_ota_status("downloaded", g_cloud.local_version, ver, 100, slot_s,
                              "Update package ready to install");
        service_ota_mark_package_downloaded();
    } else {
        ota_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.action = OTA_CMD_DOWNLOAD;
        memcpy(cmd.url, fwurl, fwurl_len + 1);
        snprintf(cmd.sha256, sizeof(cmd.sha256), "%s", sha);
        service_ota_handle_command(&cmd);
    }

    g_cloud.check_in_progress = false;
}

/* 启动 MQTT：iot_init + connect（有 secret 后调）。返回 0 成功连上/连接中。 */
static int start_mqtt(void)
{
    if (iot_init(&g_cloud.iot_cfg) != 0) {
        APP_LOGE(CLOUD_MODULE, "iot_init failed");
        return -1;
    }
    iot_set_status_callback(on_status);
    iot_set_ota_check_callback(on_ota_check);
    iot_set_cmd_callback(on_cmd);

    g_cloud.status.state = CLOUD_STATE_CONNECTING;
    if (iot_connect() == 0) {
        APP_LOGI(CLOUD_MODULE, "connecting to %s:%d ...", g_cloud.iot_cfg.mqtt_host, g_cloud.iot_cfg.mqtt_port);
        return 0;
    }
    APP_LOGW(CLOUD_MODULE, "iot_connect failed, will retry");
    g_cloud.status.state = CLOUD_STATE_DISCONNECTED;
    g_cloud.next_reconnect_ms = now_ms() + CLOUD_RECONNECT_INTERVAL_MS;
    return -1;
}

void service_cloud_update(void)
{
    if (!g_cloud.initialized) return;

    int64_t now = now_ms();
    if (!g_cloud.have_credentials) {
        if (now >= g_cloud.next_credential_check_ms) {
            if (!try_load_device_credentials()) {
                g_cloud.next_credential_check_ms = now + CLOUD_CREDENTIAL_RETRY_MS;
            } else {
                publish_cloud_status();
            }
        }
        return;
    }

    cloud_http_job_t completed;
    if (take_cloud_http_result(&completed)) {
        if (completed.kind == CLOUD_HTTP_JOB_PROVISION) {
            g_cloud.provisioning = false;
            if (completed.result == 0 && save_cached_secret(completed.result_secret)) {
                snprintf(g_cloud.iot_cfg.device_secret,
                         sizeof(g_cloud.iot_cfg.device_secret), "%s",
                         completed.result_secret);
                g_cloud.configured = true;
                g_cloud.provision_retry_delay_ms = CLOUD_PROVISION_RETRY_INITIAL_MS;
                APP_LOGI(CLOUD_MODULE, "provision secret saved atomically");
            } else {
                APP_LOGW(CLOUD_MODULE,
                         "provision not committed locally; retry in %llds",
                         (long long)(g_cloud.provision_retry_delay_ms / 1000));
                g_cloud.next_provision_ms = now + g_cloud.provision_retry_delay_ms;
                if (g_cloud.provision_retry_delay_ms < CLOUD_PROVISION_RETRY_MAX_MS) {
                    g_cloud.provision_retry_delay_ms *= 2;
                    if (g_cloud.provision_retry_delay_ms > CLOUD_PROVISION_RETRY_MAX_MS) {
                        g_cloud.provision_retry_delay_ms = CLOUD_PROVISION_RETRY_MAX_MS;
                    }
                }
            }
        } else if (completed.kind == CLOUD_HTTP_JOB_BIND_TOKEN) {
            g_cloud.status.bind_token_refreshing = false;
            if (completed.result == 0) {
                snprintf(g_cloud.status.bind_token,
                         sizeof(g_cloud.status.bind_token), "%s",
                         completed.result_bind_token);
                g_cloud.status.bind_token_error = 0;
                g_cloud.status.bind_token_expires_in = BIND_TOKEN_VALIDITY_SEC;
                g_cloud.bind_token_expires_ms = now +
                    (int64_t)BIND_TOKEN_VALIDITY_SEC * 1000;
                APP_LOGI(CLOUD_MODULE, "fresh bind token obtained (value hidden)");
            } else {
                g_cloud.status.bind_token[0] = '\0';
                g_cloud.status.bind_token_expires_in = 0;
                g_cloud.status.bind_token_error = completed.http_status > 0 ?
                    completed.http_status : -EIO;
                APP_LOGW(CLOUD_MODULE, "bind token refresh failed (http=%d)",
                         completed.http_status);
            }
            publish_cloud_status();
        }
        secure_clear(&completed, sizeof(completed));
    }

    if (g_cloud.status.bind_token[0] && g_cloud.bind_token_expires_ms > 0) {
        int64_t remaining_ms = g_cloud.bind_token_expires_ms - now;
        if (remaining_ms <= 0) {
            g_cloud.status.bind_token[0] = '\0';
            g_cloud.status.bind_token_expires_in = 0;
            g_cloud.bind_token_expires_ms = 0;
            publish_cloud_status();
        } else {
            g_cloud.status.bind_token_expires_in =
                (uint32_t)((remaining_ms + 999) / 1000);
        }
    }

    if (g_cloud.bind_refresh_requested && g_cloud.configured &&
        service_wifi_is_network_ready()) {
        if (start_cloud_http_job(CLOUD_HTTP_JOB_BIND_TOKEN)) {
            g_cloud.bind_refresh_requested = false;
        }
    }

    /* MQTT 已启动：驱动 loop + 重连 */
    if (g_cloud.mqtt_started) {
        int rc = iot_loop(CLOUD_IOT_LOOP_TIMEOUT_MS);
        if (rc == 0 && iot_get_status() != IOT_STATUS_CONNECTED) {
            rc = -1;
        }

        cloud_state_t before = g_cloud.status.state;
        if (rc == 0 && iot_get_status() == IOT_STATUS_CONNECTED) {
            if (g_cloud.status.state != CLOUD_STATE_CONNECTED) {
                g_cloud.status.state = CLOUD_STATE_CONNECTED;
            }
        } else {
            if (g_cloud.status.state != CLOUD_STATE_CONNECTING) {
                g_cloud.status.state = CLOUD_STATE_DISCONNECTED;
            }
            int64_t now = now_ms();
            if (now >= g_cloud.next_reconnect_ms) {
                APP_LOGI(CLOUD_MODULE, "reconnect attempt");
                iot_disconnect();
                if (iot_connect() == 0) {
                    g_cloud.status.state = CLOUD_STATE_CONNECTING;
                }
                g_cloud.next_reconnect_ms = now + CLOUD_RECONNECT_INTERVAL_MS;
            }
        }

        if (before != g_cloud.status.state || !g_cloud.has_published ||
            !status_equal(&g_cloud.status, &g_cloud.last_published)) {
            publish_cloud_status();
        }

        /* 已连云：处理云端 OTA 检查通知（异步，不阻塞 MQTT loop） */
        if (g_cloud.check_pending) {
            process_ota_check();
        }

        /* 心跳保活：每 60s 主动发 online heartbeat 刷官网 last_seen_at。
         * PINGREQ/PINGRESP 由 mosquitto 库在 iot_loop 里自动发，这里只补业务心跳。 */
        if (iot_get_status() == IOT_STATUS_CONNECTED) {
            int64_t now = now_ms();
            if (g_cloud.tuya_license_report_pending) {
                const char *payload = g_cloud.tuya_license_ready ?
                    "{\"tuya_license_ready\":true,\"source\":\"device_runtime\"}" :
                    "{\"tuya_license_ready\":false,\"source\":\"device_runtime\"}";
                if (iot_report_status(payload) == 0) {
                    g_cloud.tuya_license_report_pending = false;
                    APP_LOGI(CLOUD_MODULE, "tuya license runtime state reported: %s",
                             g_cloud.tuya_license_ready ? "ready" : "not_ready");
                }
            }
            if (now - g_cloud.last_heartbeat_ms >= CLOUD_HEARTBEAT_INTERVAL_MS) {
                g_cloud.last_heartbeat_ms = now;
                iot_report_status("{\"online\":true,\"source\":\"heartbeat\"}");
            }
        }
        return;
    }

    /* MQTT 未启动：等网络就绪 → provision（若需）→ start_mqtt */
    if (!service_wifi_is_network_ready()) {
        return;  /* 网络没好，等下一轮 */
    }

    /* 需要 provision（无 secret） */
    if (!g_cloud.configured) {
        if (g_cloud.provisioning || now < g_cloud.next_provision_ms) {
            return;
        }
        if (!start_cloud_http_job(CLOUD_HTTP_JOB_PROVISION)) {
            g_cloud.next_provision_ms = now + g_cloud.provision_retry_delay_ms;
            return;
        }
        g_cloud.provisioning = true;
        APP_LOGI(CLOUD_MODULE, "provision started in background");
        return;
    }

    /* 有 secret 了，启动 MQTT */
    if (start_mqtt() == 0 || iot_get_status() != IOT_STATUS_ERROR) {
        g_cloud.mqtt_started = true;
    }
    publish_cloud_status();
}

void service_cloud_set_tuya_license_ready(bool ready)
{
    if (!g_cloud.initialized || g_cloud.tuya_license_ready == ready) return;

    g_cloud.tuya_license_ready = ready;
    g_cloud.tuya_license_report_pending = true;
    APP_LOGI(CLOUD_MODULE, "tuya license runtime state changed: %s",
             ready ? "ready" : "not_ready");
}

void service_cloud_clear_ota_pending(void)
{
    if (!g_cloud.initialized) return;
    if (g_cloud.status.ota_pending) {
        /* 用户点「稍后」：记录延后提醒（同版本 24h 内不重弹）。
         * 用当前 target_version/target_sha256，开机兜底也据此校验包是否同一份。 */
        ota_later_write(g_cloud.target_version, g_cloud.target_sha256);
        APP_LOGI(CLOUD_MODULE, "ota deferred by user (ver=%s, %ldh)",
                 g_cloud.target_version, (long)(OTA_LATER_DURATION_SEC / 3600));
        g_cloud.status.ota_pending = false;
        g_cloud.status.ota_mandatory = false;
        g_cloud.status.ota_version[0] = '\0';
        publish_cloud_status();
    }
}

void service_cloud_send_status(void)
{
    publish_cloud_status();
}

void service_cloud_refresh_bind_token(void)
{
    if (!g_cloud.initialized || !g_cloud.have_credentials || !g_cloud.configured) {
        g_cloud.status.bind_token_refreshing = false;
        g_cloud.status.bind_token_error = -ENOKEY;
        g_cloud.status.bind_token_expires_in = 0;
        g_cloud.status.bind_token[0] = '\0';
        publish_cloud_status();
        return;
    }

    /* 设置页连点或重建时不重复发起 HTTP 请求。 */
    if (g_cloud.status.bind_token_refreshing) return;

    g_cloud.bind_refresh_requested = true;
    g_cloud.status.bind_token_refreshing = true;
    g_cloud.status.bind_token_error = 0;
    g_cloud.status.bind_token_expires_in = 0;
    g_cloud.status.bind_token[0] = '\0';
    g_cloud.bind_token_expires_ms = 0;
    publish_cloud_status();
}

void service_cloud_deinit(void)
{
    if (g_cloud.initialized && g_cloud.configured) {
        iot_disconnect();
        iot_cleanup();
    }
    service_ota_set_status_listener(NULL);
    service_sensor_set_listener(NULL);
    secure_clear(&g_cloud, sizeof(g_cloud));

    pthread_mutex_lock(&g_http_job_lock);
    g_http_generation++;
    if (!g_http_job_running) {
        secure_clear(&g_http_job, sizeof(g_http_job));
        g_http_job_done = false;
    }
    pthread_mutex_unlock(&g_http_job_lock);
}
