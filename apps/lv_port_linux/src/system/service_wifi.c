/**
 * @file service_wifi.c
 * @brief Wi-Fi 服务实现（业务层，底层通过 HAL 适配）
 */

#include "service_wifi.h"
#include "../middleware/middleware.h"
#include "config/app_config.h"
#include "wifi_hal.h"
#include "log/app_log.h"
#include "service_led.h"
#include "service_time.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum {
    WIFI_TASK_SCAN = 0,
    WIFI_TASK_CONNECT,
    WIFI_TASK_DISCONNECT,
    WIFI_TASK_ENABLE,
    WIFI_TASK_DISABLE
} wifi_task_type_t;

typedef struct {
    char ssid[33];
    char password[64];
} wifi_task_params_t;

typedef struct wifi_task_s {
    wifi_task_type_t type;
    wifi_task_params_t params;
    struct wifi_task_s *next;
} wifi_task_t;

static struct {
    wifi_task_t *head;
    wifi_task_t *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool running;
    bool thread_started;
    pthread_t thread_id;
} task_queue = {0};

static struct {
    bool enabled;
    bool scanning;
    bool connected;
    bool hal_ready;
    bool hal_init_pending;
    bool scan_pending;
    char connected_ssid[33];
    int last_error_code;
} wifi_state = {
    .enabled = false,
    .scanning = false,
    .connected = false,
    .last_error_code = 0,
};
static pthread_mutex_t wifi_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static void send_wifi_runtime_status(void);
static void wifi_queue_push(wifi_task_type_t type, const wifi_task_params_t *params);

/* ---- IP address monitor ---- */
#define IP_POLL_FAST_COUNT   6
#define IP_POLL_FAST_MS      500
#define IP_POLL_STEADY_MS    10000
#define IP_EXTERNAL_RECONCILE_MS 1000

static struct {
    bool active;
    bool force_send;
    int  fast_remain;
    int  tick_ms;
    char last_ip[16];
} ip_monitor = {0};
static int external_reconcile_tick_ms;

static void send_network_info(const network_info_t *info)
{
    mw_publish(TOPIC_NETWORK_INFO, info, sizeof(network_info_t), MW_DIR_BACKEND_TO_UI);
}

static bool ip_monitor_fetch(char *ip_out, size_t ip_size)
{
    const app_config_t *cfg = app_config_get();
    struct ifaddrs *ifap = NULL, *ifa;
    bool found = false;

    if (getifaddrs(&ifap) != 0) return false;

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, cfg->wifi_ifname) != 0) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        const char *addr = inet_ntoa(sin->sin_addr);
        if (!addr || strcmp(addr, "0.0.0.0") == 0) continue;

        snprintf(ip_out, ip_size, "%s", addr);
        found = true;
        break;
    }

    freeifaddrs(ifap);
    return found;
}

/*
 * The AIC8800 driver is loaded by a deferred boot worker because probing it
 * during the early init sequence can stall the product UI.  On a cold boot
 * the first wifi_hal_init() therefore runs before wlan0 exists and cannot
 * deliver a CONNECTED event.  The deferred worker still restores the saved
 * station and DHCP lease, so reconcile that real interface state here.  This
 * also keeps AI/cloud startup independent of init-script timing.
 */
static bool reconcile_external_wifi_connection(void)
{
    char current_ip[16] = {0};
    bool changed;
    bool request_hal_init = false;

    external_reconcile_tick_ms += 10; /* main loop runs at 10ms */
    if (external_reconcile_tick_ms < IP_EXTERNAL_RECONCILE_MS) return false;
    external_reconcile_tick_ms = 0;

    if (!ip_monitor_fetch(current_ip, sizeof(current_ip))) return false;

    pthread_mutex_lock(&wifi_state_mutex);
    changed = !wifi_state.enabled || !wifi_state.connected;
    wifi_state.enabled = true;
    wifi_state.connected = true;
    wifi_state.last_error_code = 0;
    if (!wifi_state.hal_ready && !wifi_state.hal_init_pending) {
        wifi_state.hal_init_pending = true;
        request_hal_init = true;
    }
    pthread_mutex_unlock(&wifi_state_mutex);

    /* An externally restored DHCP lease proves that wlan0 now exists, but it
     * does not initialize this process' wifimg/HAL instance.  Without this
     * retry the UI says Wi-Fi is enabled while every active scan returns -1. */
    if (request_hal_init) {
        APP_LOGI("wifi-service", "network interface appeared; retrying Wi-Fi HAL init");
        wifi_queue_push(WIFI_TASK_ENABLE, NULL);
    }

    if (changed) {
        APP_LOGI("wifi-service",
                 "reconciled externally restored Wi-Fi connection, ip=%s",
                 current_ip);
    }
    return changed;
}

void service_ip_monitor_poll(void)
{
    bool connected;
    bool enabled;

    pthread_mutex_lock(&wifi_state_mutex);
    connected = wifi_state.connected;
    enabled   = wifi_state.enabled;
    pthread_mutex_unlock(&wifi_state_mutex);

    /* Start / stop decisions based on WiFi state */
    if (!enabled || !connected) {
        if (reconcile_external_wifi_connection()) {
            enabled = true;
            connected = true;
            send_wifi_runtime_status();
            service_led_on_wifi_connected();
        }
    }
    if (!enabled || !connected) {
        if (ip_monitor.active) {
            /* Send one final empty IP, then go idle */
            network_info_t info;
            memset(&info, 0, sizeof(info));
            snprintf(info.ifname, sizeof(info.ifname), "%s",
                     app_config_get()->wifi_ifname);
            info.has_ip = false;
            info.ip_addr[0] = '\0';
            send_network_info(&info);
            ip_monitor.active = false;
            ip_monitor.last_ip[0] = '\0';
        }
        return;
    }

    /* WiFi just connected — start monitoring if not already */
    if (!ip_monitor.active) {
        ip_monitor.active = true;
        ip_monitor.fast_remain = IP_POLL_FAST_COUNT;
        ip_monitor.tick_ms = IP_POLL_FAST_MS; /* check immediately */
        ip_monitor.last_ip[0] = '\0';
    }

    /* Accumulate time */
    ip_monitor.tick_ms += 10; /* main loop runs at 10ms */

    int interval = (ip_monitor.fast_remain > 0) ? IP_POLL_FAST_MS : IP_POLL_STEADY_MS;
    if (ip_monitor.tick_ms < interval) return;
    ip_monitor.tick_ms = 0;

    /* Poll */
    char current_ip[16] = {0};
    bool has_ip = ip_monitor_fetch(current_ip, sizeof(current_ip));

    if (has_ip && ip_monitor.fast_remain > 0) {
        /* Got an IP — skip remaining fast polls, go straight to steady */
        ip_monitor.fast_remain = 0;
    } else if (ip_monitor.fast_remain > 0) {
        ip_monitor.fast_remain--;
    }

    bool should_send = ip_monitor.force_send;
    ip_monitor.force_send = false;

    /* Publish on change or on force-sync request */
    if (has_ip) {
        if (should_send || strcmp(current_ip, ip_monitor.last_ip) != 0) {
            network_info_t info;
            memset(&info, 0, sizeof(info));
            snprintf(info.ifname, sizeof(info.ifname), "%s",
                     app_config_get()->wifi_ifname);
            info.has_ip = true;
            snprintf(info.ip_addr, sizeof(info.ip_addr), "%s", current_ip);
            send_network_info(&info);
            snprintf(ip_monitor.last_ip, sizeof(ip_monitor.last_ip), "%s", current_ip);
            service_time_on_network_ready();
        }
    } else {
        if (should_send || ip_monitor.last_ip[0] != '\0') {
            network_info_t info;
            memset(&info, 0, sizeof(info));
            snprintf(info.ifname, sizeof(info.ifname), "%s",
                     app_config_get()->wifi_ifname);
            info.has_ip = false;
            info.ip_addr[0] = '\0';
            send_network_info(&info);
            ip_monitor.last_ip[0] = '\0';
        }
    }
}

void service_ip_monitor_sync(void)
{
    ip_monitor.force_send = true;
    ip_monitor.tick_ms = IP_POLL_FAST_MS; /* trigger immediately on next poll */
}

bool service_wifi_is_network_ready(void)
{
    bool enabled;
    bool connected;
    bool has_ip;

    pthread_mutex_lock(&wifi_state_mutex);
    enabled = wifi_state.enabled;
    connected = wifi_state.connected;
    has_ip = ip_monitor.active && ip_monitor.last_ip[0] != '\0';
    pthread_mutex_unlock(&wifi_state_mutex);

    return enabled && connected && has_ip;
}

static void send_wifi_status(const wifi_ap_info_t *ap)
{
    mw_publish(TOPIC_WIFI_STATUS, ap, sizeof(wifi_ap_info_t), MW_DIR_BACKEND_TO_UI);
}

static void send_wifi_runtime_status(void)
{
    wifi_runtime_status_t status;
    memset(&status, 0, sizeof(status));
    pthread_mutex_lock(&wifi_state_mutex);
    status.enabled = wifi_state.enabled;
    status.scanning = wifi_state.scanning;
    status.connected = wifi_state.connected;
    snprintf(status.connected_ssid, sizeof(status.connected_ssid), "%s", wifi_state.connected_ssid);
    status.last_error_code = wifi_state.last_error_code;
    pthread_mutex_unlock(&wifi_state_mutex);
    mw_publish(TOPIC_WIFI_RUNTIME, &status, sizeof(status), MW_DIR_BACKEND_TO_UI);
    service_ip_monitor_sync();
}

static void send_wifi_scan_complete(void)
{
    wifi_ap_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.ssid, sizeof(info.ssid), "__SCAN_COMPLETE__");
    send_wifi_status(&info);
    send_wifi_runtime_status();
}

static void send_wifi_disconnected_marker(void)
{
    wifi_ap_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.ssid, sizeof(info.ssid), "__DISCONNECTED__");
    send_wifi_status(&info);
    send_wifi_runtime_status();
}

static void wifi_hal_event_handler(const wifi_hal_event_t *event, void *user_data)
{
    wifi_ap_info_t info;
    bool enabled;
    bool connected;
    char connected_ssid[33];
    (void)user_data;
    if (!event) return;

    /* If service is disabled, ignore stale async events from previous scan/connect flow. */
    pthread_mutex_lock(&wifi_state_mutex);
    enabled = wifi_state.enabled;
    pthread_mutex_unlock(&wifi_state_mutex);
    if (!enabled && event->type != WIFI_HAL_EVT_ERROR) {
        return;
    }

    memset(&info, 0, sizeof(info));
    switch (event->type) {
        case WIFI_HAL_EVT_SCAN_RESULT:
            snprintf(info.ssid, sizeof(info.ssid), "%s", event->ssid);
            info.rssi = event->rssi_level;
            info.encrypted = event->encrypted;
            pthread_mutex_lock(&wifi_state_mutex);
            connected = wifi_state.connected;
            snprintf(connected_ssid, sizeof(connected_ssid), "%s", wifi_state.connected_ssid);
            pthread_mutex_unlock(&wifi_state_mutex);
            if (connected && connected_ssid[0] != '\0' && strcmp(connected_ssid, event->ssid) == 0) {
                info.connected = true;
            }
            send_wifi_status(&info);
            break;
        case WIFI_HAL_EVT_SCAN_DONE:
            pthread_mutex_lock(&wifi_state_mutex);
            wifi_state.scanning = false;
            pthread_mutex_unlock(&wifi_state_mutex);
            send_wifi_scan_complete();
            break;
        case WIFI_HAL_EVT_CONNECTED:
            pthread_mutex_lock(&wifi_state_mutex);
            wifi_state.connected = true;
            wifi_state.last_error_code = 0;
            if (event->ssid[0] != '\0') {
                snprintf(wifi_state.connected_ssid, sizeof(wifi_state.connected_ssid), "%s", event->ssid);
            }
            snprintf(connected_ssid, sizeof(connected_ssid), "%s", wifi_state.connected_ssid);
            pthread_mutex_unlock(&wifi_state_mutex);
            APP_LOGI("wifi-service", "WIFI_HAL_EVT_CONNECTED ssid=%s",
                     connected_ssid[0] ? connected_ssid : "<empty>");
            snprintf(info.ssid, sizeof(info.ssid), "%s", connected_ssid);
            info.connected = true;
            send_wifi_status(&info);
            send_wifi_runtime_status();
            service_led_on_wifi_connected();
            break;
        case WIFI_HAL_EVT_DISCONNECTED:
            pthread_mutex_lock(&wifi_state_mutex);
            snprintf(connected_ssid, sizeof(connected_ssid), "%s", wifi_state.connected_ssid);
            wifi_state.connected = false;
            wifi_state.connected_ssid[0] = '\0';
            pthread_mutex_unlock(&wifi_state_mutex);
            snprintf(info.ssid, sizeof(info.ssid), "%s", connected_ssid);
            info.connected = false;
            send_wifi_status(&info);
            send_wifi_runtime_status();
            service_led_on_wifi_disconnected();
            break;
        case WIFI_HAL_EVT_ERROR:
            APP_LOGW("wifi-service", "HAL event error: %d", event->error_code);
            pthread_mutex_lock(&wifi_state_mutex);
            wifi_state.last_error_code = event->error_code;
            pthread_mutex_unlock(&wifi_state_mutex);
            send_wifi_runtime_status();
            service_led_on_wifi_error();
            break;
    }
}

static void wifi_queue_init(void)
{
    pthread_mutex_init(&task_queue.mutex, NULL);
    pthread_cond_init(&task_queue.cond, NULL);
    task_queue.head = NULL;
    task_queue.tail = NULL;
    task_queue.running = true;
    task_queue.thread_started = false;
}

static void wifi_queue_push(wifi_task_type_t type, const wifi_task_params_t *params)
{
    wifi_task_t *task = malloc(sizeof(wifi_task_t));
    if (!task) return;

    task->type = type;
    if (params) memcpy(&task->params, params, sizeof(wifi_task_params_t));
    else memset(&task->params, 0, sizeof(wifi_task_params_t));
    task->next = NULL;

    pthread_mutex_lock(&task_queue.mutex);
    if (!task_queue.running) {
        pthread_mutex_unlock(&task_queue.mutex);
        free(task);
        return;
    }
    if (task_queue.tail) task_queue.tail->next = task;
    else task_queue.head = task;
    task_queue.tail = task;
    pthread_cond_signal(&task_queue.cond);
    pthread_mutex_unlock(&task_queue.mutex);
}

static void wifi_queue_push_front(wifi_task_type_t type, const wifi_task_params_t *params)
{
    wifi_task_t *task = malloc(sizeof(wifi_task_t));
    if (!task) return;

    task->type = type;
    if (params) memcpy(&task->params, params, sizeof(wifi_task_params_t));
    else memset(&task->params, 0, sizeof(wifi_task_params_t));
    task->next = NULL;

    pthread_mutex_lock(&task_queue.mutex);
    if (!task_queue.running) {
        pthread_mutex_unlock(&task_queue.mutex);
        free(task);
        return;
    }
    if (!task_queue.head) {
        task_queue.head = task;
        task_queue.tail = task;
    } else {
        task->next = task_queue.head;
        task_queue.head = task;
    }
    pthread_cond_signal(&task_queue.cond);
    pthread_mutex_unlock(&task_queue.mutex);
}

static void *wifi_worker_thread(void *arg)
{
    bool connected;
    bool hal_ready;
    bool scan_pending;
    (void)arg;
    APP_LOGI("wifi-service", "worker thread started");

    while (1) {
        wifi_task_t *task = NULL;
        pthread_mutex_lock(&task_queue.mutex);
        while (task_queue.head == NULL && task_queue.running) {
            pthread_cond_wait(&task_queue.cond, &task_queue.mutex);
        }
        if (!task_queue.running) {
            pthread_mutex_unlock(&task_queue.mutex);
            break;
        }

        task = task_queue.head;
        task_queue.head = task->next;
        if (task_queue.head == NULL) task_queue.tail = NULL;
        pthread_mutex_unlock(&task_queue.mutex);

        switch (task->type) {
            case WIFI_TASK_SCAN:
                send_wifi_runtime_status();
                if (wifi_hal_scan() != 0) {
                    bool still_scanning;
                    pthread_mutex_lock(&wifi_state_mutex);
                    still_scanning = wifi_state.scanning;
                    wifi_state.scanning = false;
                    if (wifi_state.last_error_code == 0) {
                        wifi_state.last_error_code = -2003;
                    }
                    pthread_mutex_unlock(&wifi_state_mutex);
                    APP_LOGE("wifi-service", "scan command failed");
                    /* Drivers normally emit SCAN_DONE even on failure.  The
                     * fallback prevents a permanent spinner when the command
                     * is rejected before callbacks are available. */
                    if (still_scanning) send_wifi_scan_complete();
                }
                break;
            case WIFI_TASK_CONNECT:
            {
                int ret = wifi_hal_connect(task->params.ssid, task->params.password);
                if (ret != 0) {
                    pthread_mutex_lock(&wifi_state_mutex);
                    wifi_state.last_error_code = -2002;
                    pthread_mutex_unlock(&wifi_state_mutex);
                    APP_LOGE("wifi-service", "connect command failed");
                    send_wifi_runtime_status();
                }
                break;
            }
            case WIFI_TASK_DISCONNECT:
                (void)wifi_hal_disconnect();
                break;
            case WIFI_TASK_ENABLE:
                pthread_mutex_lock(&wifi_state_mutex);
                hal_ready = wifi_state.hal_ready;
                pthread_mutex_unlock(&wifi_state_mutex);
                if (!hal_ready) {
                    /* Mark enabled before HAL init so a synchronous CONNECTED event
                     * emitted from wifi_hal_init() is treated as valid current state
                     * rather than being dropped as a stale event. */
                    pthread_mutex_lock(&wifi_state_mutex);
                    wifi_state.enabled = true;
                    wifi_state.last_error_code = 0;
                    pthread_mutex_unlock(&wifi_state_mutex);

                    if (wifi_hal_init(wifi_hal_event_handler, NULL) == 0) {
                        pthread_mutex_lock(&wifi_state_mutex);
                        wifi_state.hal_ready = true;
                        wifi_state.hal_init_pending = false;
                        scan_pending = wifi_state.scan_pending;
                        wifi_state.scan_pending = false;
                        connected = wifi_state.connected;
                        if (scan_pending) {
                            wifi_state.scanning = true;
                        } else if (!wifi_state.scanning && !connected) {
                            wifi_state.scanning = true;
                            scan_pending = true;
                        }
                        pthread_mutex_unlock(&wifi_state_mutex);
                        APP_LOGI("wifi-service", "wifi enabled");
                        send_wifi_runtime_status();
                        if (scan_pending) {
                            wifi_queue_push(WIFI_TASK_SCAN, NULL);
                        }
                    } else {
                        wifi_hal_deinit();
                        pthread_mutex_lock(&wifi_state_mutex);
                        wifi_state.enabled = false;
                        wifi_state.scanning = false;
                        wifi_state.connected = false;
                        wifi_state.hal_ready = false;
                        wifi_state.hal_init_pending = false;
                        wifi_state.scan_pending = false;
                        wifi_state.connected_ssid[0] = '\0';
                        wifi_state.last_error_code = -2001;
                        pthread_mutex_unlock(&wifi_state_mutex);
                        APP_LOGE("wifi-service", "enable wifi failed");
                        send_wifi_runtime_status();
                    }
                }
                break;
            case WIFI_TASK_DISABLE:
                /* Always deinit HAL here. service_wifi_set_enabled(false) updates UI state first,
                 * so relying on wifi_state.enabled would skip physical shutdown. */
                wifi_hal_deinit();
                pthread_mutex_lock(&wifi_state_mutex);
                wifi_state.enabled = false;
                wifi_state.scanning = false;
                wifi_state.connected = false;
                wifi_state.hal_ready = false;
                wifi_state.hal_init_pending = false;
                wifi_state.scan_pending = false;
                wifi_state.connected_ssid[0] = '\0';
                wifi_state.last_error_code = 0;
                pthread_mutex_unlock(&wifi_state_mutex);
                APP_LOGI("wifi-service", "wifi disabled");
                break;
        }
        free(task);
    }
    return NULL;
}

void service_wifi_init(void)
{
    APP_LOGI("wifi-service", "initializing");
    pthread_mutex_lock(&wifi_state_mutex);
    wifi_state.enabled = false;
    wifi_state.scanning = false;
    wifi_state.connected = false;
    wifi_state.hal_ready = false;
    wifi_state.hal_init_pending = false;
    wifi_state.scan_pending = false;
    wifi_state.connected_ssid[0] = '\0';
    wifi_state.last_error_code = 0;
    pthread_mutex_unlock(&wifi_state_mutex);
    external_reconcile_tick_ms = 0;
    send_wifi_runtime_status();

    wifi_queue_init();
    if (pthread_create(&task_queue.thread_id, NULL, wifi_worker_thread, NULL) != 0) {
        APP_LOGE("wifi-service", "failed to create worker thread");
        pthread_mutex_lock(&task_queue.mutex);
        task_queue.running = false;
        task_queue.thread_started = false;
        pthread_mutex_unlock(&task_queue.mutex);
        return;
    }
    pthread_mutex_lock(&task_queue.mutex);
    task_queue.thread_started = true;
    pthread_mutex_unlock(&task_queue.mutex);
}

void service_wifi_deinit(void)
{
    bool hal_ready;
    bool thread_started;
    wifi_task_t *task;
    APP_LOGI("wifi-service", "deinitializing");

    pthread_mutex_lock(&task_queue.mutex);
    task_queue.running = false;
    pthread_cond_broadcast(&task_queue.cond);
    thread_started = task_queue.thread_started;
    pthread_mutex_unlock(&task_queue.mutex);

    if (thread_started) {
        pthread_join(task_queue.thread_id, NULL);
        pthread_mutex_lock(&task_queue.mutex);
        task_queue.thread_started = false;
        pthread_mutex_unlock(&task_queue.mutex);
    }

    pthread_mutex_lock(&task_queue.mutex);
    while (task_queue.head) {
        task = task_queue.head;
        task_queue.head = task->next;
        free(task);
    }
    task_queue.tail = NULL;
    pthread_mutex_unlock(&task_queue.mutex);

    pthread_mutex_lock(&wifi_state_mutex);
    hal_ready = wifi_state.hal_ready;
    pthread_mutex_unlock(&wifi_state_mutex);
    if (hal_ready) {
        wifi_hal_deinit();
        pthread_mutex_lock(&wifi_state_mutex);
        wifi_state.enabled = false;
        wifi_state.hal_ready = false;
        wifi_state.last_error_code = 0;
        pthread_mutex_unlock(&wifi_state_mutex);
    }
}

void service_wifi_scan(void)
{
    bool enabled;
    bool scanning;
    bool hal_ready;
    bool request_hal_init = false;
    pthread_mutex_lock(&wifi_state_mutex);
    enabled = wifi_state.enabled;
    scanning = wifi_state.scanning;
    hal_ready = wifi_state.hal_ready;
    pthread_mutex_unlock(&wifi_state_mutex);
    if (!enabled) {
        APP_LOGW("wifi-service", "scan ignored: disabled");
        return;
    }
    if (!scanning) {
        pthread_mutex_lock(&wifi_state_mutex);
        wifi_state.scanning = true;
        wifi_state.last_error_code = 0;
        if (!hal_ready) {
            wifi_state.scan_pending = true;
            if (!wifi_state.hal_init_pending) {
                wifi_state.hal_init_pending = true;
                request_hal_init = true;
            }
        }
        pthread_mutex_unlock(&wifi_state_mutex);
        send_wifi_runtime_status();
        if (hal_ready) {
            wifi_queue_push(WIFI_TASK_SCAN, NULL);
        } else if (request_hal_init) {
            APP_LOGI("wifi-service", "scan requested before HAL ready; initializing now");
            wifi_queue_push_front(WIFI_TASK_ENABLE, NULL);
        }
    }
}

void service_wifi_connect(const char *ssid, const char *password)
{
    bool enabled;
    wifi_task_params_t params;
    pthread_mutex_lock(&wifi_state_mutex);
    enabled = wifi_state.enabled;
    pthread_mutex_unlock(&wifi_state_mutex);
    if (!enabled || !ssid) return;
    memset(&params, 0, sizeof(params));
    snprintf(params.ssid, sizeof(params.ssid), "%s", ssid);
    snprintf(params.password, sizeof(params.password), "%s", password ? password : "");
    /* Preserve target SSID for event matching when lower layer reports CONNECTED with empty SSID. */
    pthread_mutex_lock(&wifi_state_mutex);
    snprintf(wifi_state.connected_ssid, sizeof(wifi_state.connected_ssid), "%s", ssid);
    wifi_state.last_error_code = 0;
    pthread_mutex_unlock(&wifi_state_mutex);
    service_led_on_wifi_connecting();
    wifi_queue_push(WIFI_TASK_CONNECT, &params);
}

void service_wifi_disconnect(void)
{
    bool enabled;
    pthread_mutex_lock(&wifi_state_mutex);
    enabled = wifi_state.enabled;
    pthread_mutex_unlock(&wifi_state_mutex);
    if (!enabled) return;
    wifi_queue_push(WIFI_TASK_DISCONNECT, NULL);
}

void service_wifi_set_enabled(bool enabled)
{
    if (enabled) {
        pthread_mutex_lock(&wifi_state_mutex);
        if (!wifi_state.hal_ready && !wifi_state.hal_init_pending) {
            wifi_state.hal_init_pending = true;
        }
        pthread_mutex_unlock(&wifi_state_mutex);
        wifi_queue_push_front(WIFI_TASK_ENABLE, NULL);
        return;
    }

    /* Immediate state transition for UI responsiveness; physical deinit runs in worker. */
    pthread_mutex_lock(&wifi_state_mutex);
    wifi_state.enabled = false;
    wifi_state.scanning = false;
    wifi_state.connected = false;
    wifi_state.hal_init_pending = false;
    wifi_state.scan_pending = false;
    wifi_state.connected_ssid[0] = '\0';
    wifi_state.last_error_code = 0;
    pthread_mutex_unlock(&wifi_state_mutex);
    send_wifi_disconnected_marker();
    send_wifi_runtime_status();

    wifi_queue_push_front(WIFI_TASK_DISABLE, NULL);
}

void service_wifi_send_runtime_status(void)
{
    send_wifi_runtime_status();
}
