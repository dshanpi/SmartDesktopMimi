#include "aw_wifimg_driver.h"

#include "../../system/config/app_config.h"
#include "../../system/log/app_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <linux/wireless.h>

#include <wifimg.h>
#include <wifi_log.h>

#define WPA_LOCAL_PATH_TEMPLATE "/tmp/wpa_ctrl_%d-%d"

typedef struct {
    int sock;
    struct sockaddr_un local;
    struct sockaddr_un dest;
} wpa_ctrl_t;

static wifi_hal_event_cb_t g_evt_cb = NULL;
static void *g_evt_user = NULL;
static bool g_enabled = false;
static char g_connected_ssid[33] = {0};

static bool is_safe_ifname(const char *ifname)
{
    if (!ifname || ifname[0] == '\0') return false;

    for (size_t i = 0; ifname[i] != '\0'; i++) {
        char c = ifname[i];
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '-' || c == '.' || c == ':';
        if (!ok) return false;
    }

    return true;
}

static void flush_netdev_ip_and_route(void)
{
    const app_config_t *cfg = app_config_get();
    char cmd[192];

    if (!is_safe_ifname(cfg->wifi_ifname)) {
        APP_LOGE("wifi-driver", "unsafe wifi interface name: %s", cfg->wifi_ifname);
        return;
    }

    /* Keep L3 state consistent with link state, avoid stale IP/default route after disconnect/off. */
    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s >/dev/null 2>&1", cfg->wifi_ifname);
    (void)system(cmd);
    snprintf(cmd, sizeof(cmd), "ip route del default dev %s >/dev/null 2>&1", cfg->wifi_ifname);
    (void)system(cmd);
}

static void emit_event(const wifi_hal_event_t *event)
{
    if (g_evt_cb) g_evt_cb(event, g_evt_user);
}

static wpa_ctrl_t *wpa_ctrl_open(const char *ctrl_path)
{
    wpa_ctrl_t *ctrl = malloc(sizeof(wpa_ctrl_t));
    if (!ctrl) return NULL;
    memset(ctrl, 0, sizeof(wpa_ctrl_t));

    ctrl->sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (ctrl->sock < 0) {
        free(ctrl);
        return NULL;
    }

    ctrl->local.sun_family = AF_UNIX;
    snprintf(ctrl->local.sun_path, sizeof(ctrl->local.sun_path), WPA_LOCAL_PATH_TEMPLATE, getpid(), rand() % 10000);

    if (bind(ctrl->sock, (struct sockaddr *)&ctrl->local, sizeof(ctrl->local)) < 0) {
        close(ctrl->sock);
        free(ctrl);
        return NULL;
    }

    ctrl->dest.sun_family = AF_UNIX;
    {
        size_t ctrl_path_len = strnlen(ctrl_path, sizeof(ctrl->dest.sun_path));
        if (ctrl_path_len >= sizeof(ctrl->dest.sun_path)) {
            APP_LOGE("wifi-driver", "wpa ctrl path too long");
            close(ctrl->sock);
            unlink(ctrl->local.sun_path);
            free(ctrl);
            return NULL;
        }
        memcpy(ctrl->dest.sun_path, ctrl_path, ctrl_path_len + 1);
    }

    if (connect(ctrl->sock, (struct sockaddr *)&ctrl->dest, sizeof(ctrl->dest)) < 0) {
        close(ctrl->sock);
        unlink(ctrl->local.sun_path);
        free(ctrl);
        return NULL;
    }
    return ctrl;
}

static void wpa_ctrl_close(wpa_ctrl_t *ctrl)
{
    if (!ctrl) return;
    close(ctrl->sock);
    unlink(ctrl->local.sun_path);
    free(ctrl);
}

static int wpa_ctrl_request_internal(wpa_ctrl_t *ctrl, const char *cmd, char *reply, size_t *reply_len)
{
    ssize_t len;
    struct timeval tv;
    if (send(ctrl->sock, cmd, strlen(cmd), 0) < 0) return -1;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(ctrl->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    len = recv(ctrl->sock, reply, *reply_len - 1, 0);
    if (len >= 0) {
        reply[len] = '\0';
        *reply_len = (size_t)len;
        return 0;
    }
    return -1;
}

static int get_connected_ssid_wpa_ctrl(char *ssid_buf, size_t buf_len)
{
    char reply[4096];
    size_t reply_len = sizeof(reply);
    const app_config_t *cfg = app_config_get();
    wpa_ctrl_t *ctrl = wpa_ctrl_open(cfg->wpa_ctrl_path);
    if (!ctrl) return -1;

    if (wpa_ctrl_request_internal(ctrl, "STATUS", reply, &reply_len) == 0) {
        int connected = 0;
        char temp_ssid[64] = {0};
        char *line = strtok(reply, "\n");
        while (line) {
            if (strncmp(line, "wpa_state=COMPLETED", 19) == 0) connected = 1;
            if (strncmp(line, "ssid=", 5) == 0) snprintf(temp_ssid, sizeof(temp_ssid), "%s", line + 5);
            line = strtok(NULL, "\n");
        }
        if (connected && strlen(temp_ssid) > 0) {
            snprintf(ssid_buf, buf_len, "%.*s", (int)(buf_len - 1), temp_ssid);
            wpa_ctrl_close(ctrl);
            return 0;
        }
    }
    wpa_ctrl_close(ctrl);
    return -1;
}

static int get_connected_ssid_linux(char *ssid_buf, size_t buf_len)
{
    int sock;
    struct iwreq wrq;
    const app_config_t *cfg = app_config_get();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        char essid[IW_ESSID_MAX_SIZE + 1] = {0};
        snprintf(wrq.ifr_name, IFNAMSIZ, "%s", cfg->wifi_ifname);
        wrq.u.essid.pointer = (caddr_t)essid;
        wrq.u.essid.length = IW_ESSID_MAX_SIZE;
        wrq.u.essid.flags = 0;
        if (ioctl(sock, SIOCGIWESSID, &wrq) >= 0) {
            close(sock);
            if (wrq.u.essid.flags && strlen(essid) > 0) {
                snprintf(ssid_buf, buf_len, "%.*s", (int)(buf_len - 1), essid);
                return 0;
            }
        } else {
            close(sock);
        }
    }
    return get_connected_ssid_wpa_ctrl(ssid_buf, buf_len);
}

static void wifi_msg_callback(wifi_msg_data_t *msg)
{
    wifi_hal_event_t evt;
    char current_ssid[33] = {0};
    if (!msg) return;
    memset(&evt, 0, sizeof(evt));

    if (msg->id == WIFI_MSG_ID_STA_CN_EVENT) {
        if (msg->data.event == WIFI_CONNECTED) {
            /* Some platforms may emit CONNECTED before cached SSID is populated. */
            if (g_connected_ssid[0] == '\0' &&
                get_connected_ssid_linux(current_ssid, sizeof(current_ssid)) == 0) {
                snprintf(g_connected_ssid, sizeof(g_connected_ssid), "%s", current_ssid);
            }
            evt.type = WIFI_HAL_EVT_CONNECTED;
            snprintf(evt.ssid, sizeof(evt.ssid), "%s", g_connected_ssid);
            emit_event(&evt);
        } else if (msg->data.event == WIFI_DISCONNECTED) {
            evt.type = WIFI_HAL_EVT_DISCONNECTED;
            snprintf(evt.ssid, sizeof(evt.ssid), "%s", g_connected_ssid);
            g_connected_ssid[0] = '\0';
            emit_event(&evt);
        }
    }
}

static int aw_init(wifi_hal_event_cb_t cb, void *user_data)
{
    int ret;
    char current_ssid[33] = {0};
    wifi_hal_event_t evt;

    g_evt_cb = cb;
    g_evt_user = user_data;
    memset(&evt, 0, sizeof(evt));

    /* Capture pre-existing connection state before any init,
     * because wifi_on() may reset the chip and break an already-established link. */
    if (get_connected_ssid_linux(current_ssid, sizeof(current_ssid)) == 0) {
        snprintf(g_connected_ssid, sizeof(g_connected_ssid), "%s", current_ssid);
    }

    ret = wifimanager_init();
    if (ret != 0) {
        APP_LOGE("wifi-driver", "wifimanager_init failed: %d", ret);
        return -1;
    }

    ret = wifi_on(WIFI_STATION);
    if (ret != 0) {
        APP_LOGE("wifi-driver", "wifi_on failed: %d", ret);
        wifimanager_deinit();
        return -1;
    }

    g_enabled = true;
    wifi_register_msg_cb(wifi_msg_callback, NULL);
    wifi_sta_auto_reconnect(true);

    /* If we captured a pre-existing connection, emit CONNECTED immediately
     * so that the IP monitor and time-sync start without waiting for the
     * potentially slow auto-reconnect callback.  A subsequent async
     * WIFI_CONNECTED callback from auto-reconnect is harmless — the
     * service layer treats duplicate CONNECTED events as idempotent. */
    if (g_connected_ssid[0] != '\0') {
        evt.type = WIFI_HAL_EVT_CONNECTED;
        snprintf(evt.ssid, sizeof(evt.ssid), "%s", g_connected_ssid);
        emit_event(&evt);
    } else if (get_connected_ssid_linux(current_ssid, sizeof(current_ssid)) == 0) {
        snprintf(g_connected_ssid, sizeof(g_connected_ssid), "%s", current_ssid);
        evt.type = WIFI_HAL_EVT_CONNECTED;
        snprintf(evt.ssid, sizeof(evt.ssid), "%s", current_ssid);
        emit_event(&evt);
    }
    APP_LOGI("wifi-driver", "aw_wifimg initialized");
    return 0;
}

static void aw_deinit(void)
{
    if (!g_enabled) return;
    wifi_off();
    flush_netdev_ip_and_route();
    wifimanager_deinit();
    g_enabled = false;
    g_connected_ssid[0] = '\0';
    APP_LOGI("wifi-driver", "aw_wifimg deinitialized");
}

static int aw_scan(void)
{
    wifi_scan_result_t scan_results[32] = {0};
    uint32_t bss_num = 0;
    wifi_hal_event_t evt;
    int ret;
    char current_ssid[33] = {0};

    if (!g_enabled) return -1;
    memset(&evt, 0, sizeof(evt));
    if (get_connected_ssid_linux(current_ssid, sizeof(current_ssid)) == 0) {
        snprintf(g_connected_ssid, sizeof(g_connected_ssid), "%s", current_ssid);
    }

    ret = wifi_get_scan_results(scan_results, &bss_num, 32);
    if (ret == 0 && bss_num > 0) {
        for (uint32_t i = 0; i < bss_num; i++) {
            memset(&evt, 0, sizeof(evt));
            evt.type = WIFI_HAL_EVT_SCAN_RESULT;
            snprintf(evt.ssid, sizeof(evt.ssid), "%s", scan_results[i].ssid);
            evt.rssi_level = (scan_results[i].rssi + 100) / 20;
            if (evt.rssi_level < 0) evt.rssi_level = 0;
            if (evt.rssi_level > 4) evt.rssi_level = 4;
            evt.encrypted = (scan_results[i].key_mgmt != WIFI_SEC_NONE);
            emit_event(&evt);
            usleep(10000);
        }
    } else {
        memset(&evt, 0, sizeof(evt));
        evt.type = WIFI_HAL_EVT_ERROR;
        evt.error_code = ret;
        emit_event(&evt);
    }

    memset(&evt, 0, sizeof(evt));
    evt.type = WIFI_HAL_EVT_SCAN_DONE;
    emit_event(&evt);
    return ret == 0 ? 0 : -1;
}

static int aw_connect(const char *ssid, const char *password)
{
    wifi_sta_cn_para_t cn_para = {0};
    int ret;

    if (!g_enabled || !ssid) return -1;
    wifi_sta_disconnect();
    usleep(200000);

    cn_para.ssid = (char *)ssid;
    cn_para.password = (char *)(password ? password : "");
    cn_para.sec = WIFI_SEC_WPA2_PSK;
    cn_para.fast_connect = true;

    ret = wifi_sta_connect(&cn_para);
    if (ret == 0) {
        snprintf(g_connected_ssid, sizeof(g_connected_ssid), "%s", ssid);
        APP_LOGI("wifi-driver", "connect command sent: %s", ssid);
    } else {
        APP_LOGE("wifi-driver", "connect failed: %d", ret);
    }
    return ret == 0 ? 0 : -1;
}

static int aw_disconnect(void)
{
    if (!g_enabled) return -1;
    wifi_sta_disconnect();
    flush_netdev_ip_and_route();
    return 0;
}

const wifi_driver_ops_t *aw_wifimg_driver_get_ops(void)
{
    static const wifi_driver_ops_t ops = {
        .init = aw_init,
        .deinit = aw_deinit,
        .scan = aw_scan,
        .connect = aw_connect,
        .disconnect = aw_disconnect,
    };
    return &ops;
}
