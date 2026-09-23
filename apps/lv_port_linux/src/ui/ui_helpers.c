#include "ui_helpers.h"
#include "ui.h"
#include "theme/theme.h"
#include "cloud_ota_modal.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "../system/app_manager.h"
#include "../system/backend_types.h"
#include "../system/service_wifi.h"
#include "desktop_clock.h"

/* 图片声明（refresh_wifi_icon_visual 使用） */
LV_IMAGE_DECLARE(close_wifi);
LV_IMAGE_DECLARE(wireless_network_wifi);

/* =======================
 * 引用 ui.c 中的全局变量
 * ======================= */
extern lv_obj_t *wifi_icon_obj;
extern lv_obj_t *cloud_icon_obj;
extern lv_obj_t *cloud_status_dot_obj;
extern lv_obj_t *cloud_progress_track_obj;
extern lv_obj_t *cloud_progress_fill_obj;
extern bool wifi_connected;
extern char wifi_connected_ssid[33];
extern bool wifi_runtime_enabled;
extern bool wifi_runtime_scanning;
extern bool wifi_has_ip;
extern bool wifi_startup_scan_decided;
extern lv_timer_t *wifi_startup_scan_timer;


extern lv_obj_t *power_label;

/* Cloud badge OTA feedback (top bar). */
#define CLOUD_DOT_GRAY   0xA6B2BAU
#define CLOUD_DOT_GREEN  0x55A579U
#define CLOUD_DOT_BLUE   0x5B9BD5U
#define CLOUD_DOT_ORANGE 0xE0A14FU
#define CLOUD_DOT_RED    0xD96060U
#define CLOUD_PROGRESS_TRACK_W 40

/* =======================
 * 工具函数
 * ======================= */

void apply_debug_outline(lv_obj_t *obj, lv_color_t color)
{
#if UI_LAYOUT_DEBUG
    if (!obj) return;
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
#else
    (void)obj;
    (void)color;
#endif
}

void apply_glass_card_style(lv_obj_t *obj, lv_coord_t radius, lv_opa_t bg_opa, lv_coord_t shadow_w)
{
    if (!obj) return;
    /* One quiet material layer: surface, border and a restrained tinted shadow. */
    lv_obj_add_style(obj, ui_theme_get_glass_card_style(), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_opa(obj, bg_opa, 0);
    lv_obj_set_style_shadow_width(obj, shadow_w, 0);
    lv_obj_set_style_clip_corner(obj, true, 0);
}

/* =======================
 * WiFi 图标刷新
 * ======================= */

#define WIFI_STARTUP_SCAN_GRACE_MS 1500

static void cancel_wifi_startup_scan_timer(void)
{
    if (!wifi_startup_scan_timer) return;
    lv_timer_delete(wifi_startup_scan_timer);
    wifi_startup_scan_timer = NULL;
}

static void wifi_startup_scan_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_startup_scan_timer = NULL;

    if (wifi_startup_scan_decided) return;

    if (wifi_runtime_enabled && !wifi_connected && !wifi_runtime_scanning && !wifi_has_ip) {
        wifi_cmd_t cmd = { .action = CMD_SCAN };
        mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
        printf("[UI] Startup: sent WiFi scan request after grace period\n");
    } else {
        printf("[UI] Startup: skip delayed scan (enabled=%d connected=%d scanning=%d has_ip=%d)\n",
               wifi_runtime_enabled, wifi_connected, wifi_runtime_scanning, wifi_has_ip);
    }

    wifi_startup_scan_decided = true;
}

static void schedule_wifi_startup_scan_timer(void)
{
    if (wifi_startup_scan_timer) return;
    wifi_startup_scan_timer = lv_timer_create(wifi_startup_scan_timer_cb, WIFI_STARTUP_SCAN_GRACE_MS, NULL);
    if (wifi_startup_scan_timer) {
        lv_timer_set_repeat_count(wifi_startup_scan_timer, 1);
    }
}

void refresh_wifi_icon_visual(void)
{
    if (!wifi_icon_obj) return;
    if (!wifi_runtime_enabled || !wifi_connected) {
        lv_image_set_src(wifi_icon_obj, &close_wifi);
        lv_obj_set_style_opa(wifi_icon_obj, LV_OPA_COVER, 0);
    } else {
        lv_image_set_src(wifi_icon_obj, &wireless_network_wifi);
        lv_obj_set_style_opa(wifi_icon_obj, LV_OPA_COVER, 0);
    }
}

/* =======================
 * WiFi 状态回调
 * ======================= */

void wifi_status_callback(const mw_msg_t *msg)
{
    if (!msg || msg->data_len < sizeof(wifi_ap_info_t)) {
        return;
    }

    const wifi_ap_info_t *wifi_info = (const wifi_ap_info_t *)msg->data;

    if (strcmp(wifi_info->ssid, "__DISCONNECTED__") == 0) {
        wifi_connected = false;
        wifi_connected_ssid[0] = '\0';
        refresh_wifi_icon_visual();
        return;
    }

    if (strcmp(wifi_info->ssid, "__SCAN_COMPLETE__") == 0) {
        refresh_wifi_icon_visual();
        return;
    }

    if (wifi_info->connected) {
        wifi_connected = true;
        snprintf(wifi_connected_ssid, sizeof(wifi_connected_ssid), "%s", wifi_info->ssid);
        refresh_wifi_icon_visual();
    } else if (strlen(wifi_info->ssid) > 0) {
        if (wifi_connected && strcmp(wifi_connected_ssid, wifi_info->ssid) == 0) {
            wifi_connected = false;
            wifi_connected_ssid[0] = '\0';
            refresh_wifi_icon_visual();
        }
    }
}

void wifi_runtime_callback(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_WIFI_RUNTIME || msg->data_len != sizeof(wifi_runtime_status_t)) {
        return;
    }

    const wifi_runtime_status_t *runtime = (const wifi_runtime_status_t *)msg->data;
    wifi_runtime_enabled = runtime->enabled;
    wifi_runtime_scanning = runtime->scanning;
    wifi_connected = runtime->connected;
    snprintf(wifi_connected_ssid, sizeof(wifi_connected_ssid), "%s", runtime->connected_ssid);

    if (!wifi_startup_scan_decided) {
        if (!runtime->enabled) {
            wifi_startup_scan_decided = true;
            cancel_wifi_startup_scan_timer();
            printf("[UI] Startup: skip scan (wifi disabled)\n");
        } else if (runtime->connected || runtime->scanning || wifi_has_ip) {
            wifi_startup_scan_decided = true;
            cancel_wifi_startup_scan_timer();
            printf("[UI] Startup: skip scan (enabled=%d connected=%d scanning=%d has_ip=%d)\n",
                   runtime->enabled, runtime->connected, runtime->scanning, wifi_has_ip);
        } else {
            schedule_wifi_startup_scan_timer();
            printf("[UI] Startup: waiting before first scan (enabled=%d connected=%d scanning=%d has_ip=%d)\n",
                   runtime->enabled, runtime->connected, runtime->scanning, wifi_has_ip);
        }
    }

    refresh_wifi_icon_visual();
}

/* =======================
 * 传感器状态回调
 * ======================= */

void network_info_callback(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_NETWORK_INFO || msg->data_len != sizeof(network_info_t)) {
        return;
    }

    /* IP 文字已从顶栏移除，但 network_info 仍驱动 wifi_has_ip 用于开机扫描判定。 */
    const network_info_t *info = (const network_info_t *)msg->data;
    wifi_has_ip = info->has_ip && info->ip_addr[0] != '\0';
    if (!wifi_startup_scan_decided && wifi_has_ip) {
        wifi_startup_scan_decided = true;
        cancel_wifi_startup_scan_timer();
        printf("[UI] Startup: skip scan (network already has IP %s)\n", info->ip_addr);
    }
}

void sensor_status_callback(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_SENSOR_STATUS || msg->data_len != sizeof(sensor_status_t)) {
        return;
    }

    const sensor_status_t *s = (const sensor_status_t *)msg->data;

    /* 温湿度（clock 模块） */
    ui_clock_set_sensor(s->valid, s->temp_mC / 1000, s->humi_mpermil / 1000);

    /* INA219 整机功耗/电流/电压（顶栏中间，独立于温湿度） */
    if (power_label) {
        if (!s->ina_valid) {
            lv_label_set_text(power_label, "--.-V ---mA --.-W");
        } else {
            lv_label_set_text_fmt(power_label, "%.1fV %dmA %.1fW",
                                  s->bus_mV / 1000.0, s->current_mA, s->power_uW / 1000000.0);
        }
    }
}

void hdmi_preview_status_callback(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_HDMI_PREVIEW_STATUS ||
        msg->data_len != sizeof(hdmi_preview_status_t)) {
        return;
    }

    hdmi_ui_handle_status((const hdmi_preview_status_t *)msg->data);
}

/* =======================
 * 100ask Cloud 状态回调
 * ======================= */

/* 记录云端推送的待安装状态，供 cloud_ota_status_callback 判断弹窗时机。 */
static bool cloud_ota_pending = false;
static bool cloud_ota_mandatory = false;
static char cloud_ota_version[64] = {0};
static cloud_state_t cloud_conn_state = CLOUD_STATE_DISCONNECTED;
static ota_state_t cloud_ota_state = OTA_STATE_IDLE;
static int32_t cloud_ota_progress = 0;
static int32_t cloud_ota_progress_drawn = -1;
static bool cloud_dot_blinking = false;

static void cloud_dot_blink_exec(void *obj, int32_t v)
{
    if (obj) lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void cloud_dot_stop_blink(void)
{
    if (!cloud_status_dot_obj) return;
    lv_anim_delete(cloud_status_dot_obj, cloud_dot_blink_exec);
    lv_obj_set_style_opa(cloud_status_dot_obj, LV_OPA_COVER, 0);
    cloud_dot_blinking = false;
}

static void cloud_dot_start_blink(void)
{
    if (!cloud_status_dot_obj) return;
    if (cloud_dot_blinking) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, cloud_status_dot_obj);
    lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_time(&a, 700);
    lv_anim_set_playback_time(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, cloud_dot_blink_exec);
    lv_anim_start(&a);
    cloud_dot_blinking = true;
}

static void cloud_progress_set_visible(bool visible, int32_t progress)
{
    if (!cloud_progress_track_obj || !cloud_progress_fill_obj) return;

    if (!visible) {
        lv_obj_add_flag(cloud_progress_track_obj, LV_OBJ_FLAG_HIDDEN);
        cloud_ota_progress_drawn = -1;
        return;
    }

    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    /* Throttle redraws (no G2D): only paint when progress moves ≥2%. */
    if (cloud_ota_progress_drawn >= 0 &&
        progress != 100 &&
        progress != 0 &&
        (progress - cloud_ota_progress_drawn) < 2 &&
        (cloud_ota_progress_drawn - progress) < 2) {
        lv_obj_clear_flag(cloud_progress_track_obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    int32_t w = (CLOUD_PROGRESS_TRACK_W * progress) / 100;
    if (progress > 0 && w < 2) w = 2;
    lv_obj_set_width(cloud_progress_fill_obj, w);
    lv_obj_clear_flag(cloud_progress_track_obj, LV_OBJ_FLAG_HIDDEN);
    cloud_ota_progress_drawn = progress;
}

/* OTA stage wins over bare connection color so silent download is visible. */
static void refresh_cloud_badge(void)
{
    if (!cloud_status_dot_obj) return;

    bool show_progress = false;
    bool blink = false;
    uint32_t dot_hex = CLOUD_DOT_GRAY;
    int32_t bar_progress = cloud_ota_progress;

    if (cloud_ota_state == OTA_STATE_DOWNLOADING ||
        cloud_ota_state == OTA_STATE_CHECKING) {
        /* Blue + bottom bar: only download / check have real percent progress. */
        dot_hex = CLOUD_DOT_BLUE;
        show_progress = true;
        if (cloud_ota_state == OTA_STATE_CHECKING && bar_progress <= 0) {
            bar_progress = 0;
        }
    } else if (cloud_ota_state == OTA_STATE_APPLYING ||
               cloud_ota_state == OTA_STATE_REBOOTING) {
        /*
         * Install/reboot: swupdate percent is not meaningful on the badge.
         * Keep a solid blue dot only — no progress bar under the cloud icon.
         */
        dot_hex = CLOUD_DOT_BLUE;
        show_progress = false;
        blink = false;
    } else if (cloud_ota_state == OTA_STATE_ERROR) {
        dot_hex = CLOUD_DOT_RED;
    } else if (cloud_ota_pending ||
               cloud_ota_state == OTA_STATE_DOWNLOAD_DONE) {
        /* Amber blink: package ready, waiting for install confirmation. */
        dot_hex = CLOUD_DOT_ORANGE;
        blink = true;
    } else if (cloud_conn_state == CLOUD_STATE_CONNECTED) {
        dot_hex = CLOUD_DOT_GREEN;
    } else if (cloud_conn_state == CLOUD_STATE_CONNECTING) {
        dot_hex = CLOUD_DOT_BLUE;
        blink = true;
    } else if (cloud_conn_state == CLOUD_STATE_UNAVAILABLE) {
        dot_hex = CLOUD_DOT_GRAY;
    } else {
        dot_hex = CLOUD_DOT_GRAY;
    }

    lv_obj_set_style_bg_color(cloud_status_dot_obj, lv_color_hex(dot_hex), 0);

    if (blink) {
        cloud_dot_start_blink();
    } else {
        cloud_dot_stop_blink();
    }

    cloud_progress_set_visible(show_progress, bar_progress);
}

void cloud_status_callback(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_CLOUD_STATUS ||
        msg->data_len != sizeof(cloud_status_t)) {
        return;
    }
    const cloud_status_t *s = (const cloud_status_t *)msg->data;

    cloud_conn_state = s->state;

    /* 记录云端 OTA 推送状态，供 OTA 下载完成时弹窗 */
    cloud_ota_pending = s->ota_pending;
    cloud_ota_mandatory = s->ota_mandatory;
    snprintf(cloud_ota_version, sizeof(cloud_ota_version), "%s", s->ota_version);

    refresh_cloud_badge();

    /* 若推送已取消（用户在别处清了）且弹窗还在 PROMPT 态，关掉。
     * INSTALLING 态不打断（安装过程不可中断）。 */
    if (!s->ota_pending && cloud_ota_modal_is_shown()) {
        cloud_ota_modal_hide();
    }
}

/* OTA 状态回调：驱动顶栏云徽章 + 安装弹窗状态机。
 *  - DOWNLOADING → 蓝点 + 底边进度条（静默下载可视化）
 *  - DOWNLOAD_DONE + ota_pending + 未弹 → 橙点闪 + PROMPT 弹窗
 *  - APPLYING/REBOOTING → 蓝点常亮，无进度条；弹窗 spinner「正在安装」
 *  - ERROR → 红点 + 关弹窗 */
void cloud_ota_status_callback(const mw_msg_t *msg)
{
    if (!msg || msg->topic != TOPIC_OTA_STATUS ||
        msg->data_len != sizeof(ota_status_t)) {
        return;
    }
    const ota_status_t *s = (const ota_status_t *)msg->data;

    cloud_ota_state = s->state;
    cloud_ota_progress = s->progress;
    refresh_cloud_badge();

    /* 云端推送待安装 + 下载完成 + 模态未弹 → 弹窗提示安装 */
    if (cloud_ota_pending && s->state == OTA_STATE_DOWNLOAD_DONE &&
        !cloud_ota_modal_is_shown()) {
        cloud_ota_modal_show(cloud_ota_version, cloud_ota_mandatory);
    }

    /* APPLYING：切到 INSTALLING 态（点安装时已即时切，此处兜底）。 */
    if (s->state == OTA_STATE_APPLYING && cloud_ota_modal_is_shown()) {
        cloud_ota_modal_enter_installing();
    }

    /* ERROR：安装失败，关弹窗（用户可去设置→OTA 页查看错误）。 */
    if (s->state == OTA_STATE_ERROR && cloud_ota_modal_is_shown()) {
        cloud_ota_modal_hide();
    }
}

/* =======================
 * 时钟定时器回调
 * ======================= */
