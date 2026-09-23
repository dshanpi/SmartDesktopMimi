/**
 * @file tuya_main.c
 * @brief Implements main audio functionality for IoT device
 *
 * This source file provides the implementation of the main audio functionalities
 * required for an IoT device. It includes functionality for audio processing,
 * device initialization, event handling, and network communication. The
 * implementation supports audio volume control, data point processing, and
 * interaction with the Tuya IoT platform. This file is essential for developers
 * working on IoT applications that require audio capabilities and integration
 * with the Tuya IoT ecosystem.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_cloud_types.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "tal_api.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"
#include "netmgr.h"
#include "tkl_output.h"
#include "tal_cli.h"
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "netconn_wifi.h"
#else
// Stub WiFi functions for non-WiFi platforms (e.g., Ubuntu with wired)
#include "tkl_wifi_stub.h"
#endif
#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
#include "netconn_wired.h"
#endif
#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
#include "lwip_init.h"
#endif

#include "board_com_api.h"

#include "app_chat_bot.h"
#include "reset_netcfg.h"

#if defined(ENABLE_BATTERY) && (ENABLE_BATTERY == 1)
#include "app_battery.h"
#endif

#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
#include "qrencode_print.h"
#endif

/* Tuya device handle */
tuya_iot_client_t ai_client;

/* Tuya license information (uuid authkey) */
tuya_iot_license_t license;

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

#define DPID_POWER  1
#define DPID_VOLUME 3
#define LICENSE_RETRY_INTERVAL_MS 5000

/*
 * The cloud schema and MQTT session are not guaranteed to be ready for a DP
 * report from inside the first connected callback.  Keep the callback short,
 * then retry the initial state asynchronously.  The last delay is repeated so
 * a transient cloud/schema failure cannot leave the mobile panel stale until
 * the process is restarted.
 */
static const TIME_MS DP_ONLINE_RETRY_DELAYS_MS[] = {1000, 2000, 5000, 10000, 30000};
static TIMER_ID      sg_dp_online_sync_timer = NULL;
static BOOL_T        sg_mqtt_connected = FALSE;
static uint8_t       sg_dp_online_retry_index = 0;

/**
 * @brief user defined log output api, in this demo, it will use uart0 as log-tx
 *
 * @param str log string
 * @return void
 */
void user_log_output_cb(const char *str)
{
    if (str == NULL) {
        return;
    }

    fputs(str, stdout);
    fflush(stdout);
}

/**
 * @brief user defined upgrade notify callback, it will notify device a OTA request received
 *
 * @param client device info
 * @param upgrade the upgrade request info
 * @return void
 */
void user_upgrade_notify_on(tuya_iot_client_t *client, cJSON *upgrade)
{
    PR_INFO("----- Upgrade information -----");
    if (!upgrade) {
        PR_WARN("upgrade JSON is NULL");
        return;
    }

    cJSON *type_item    = cJSON_GetObjectItem(upgrade, "type");
    cJSON *version_item = cJSON_GetObjectItem(upgrade, "version");
    cJSON *size_item    = cJSON_GetObjectItem(upgrade, "size");
    cJSON *md5_item     = cJSON_GetObjectItem(upgrade, "md5");
    cJSON *hmac_item    = cJSON_GetObjectItem(upgrade, "hmac");
    cJSON *url_item     = cJSON_GetObjectItem(upgrade, "url");
    cJSON *https_item   = cJSON_GetObjectItem(upgrade, "httpsUrl");

    PR_INFO("OTA Channel: %d", cJSON_IsNumber(type_item) ? type_item->valueint : -1);
    PR_INFO("Version: %s", cJSON_IsString(version_item) ? version_item->valuestring : "N/A");
    PR_INFO("Size: %s", cJSON_IsString(size_item) ? size_item->valuestring : "N/A");
    PR_INFO("MD5: %s", cJSON_IsString(md5_item) ? md5_item->valuestring : "N/A");
    PR_INFO("HMAC: %s", cJSON_IsString(hmac_item) ? hmac_item->valuestring : "N/A");
    PR_INFO("URL: %s", cJSON_IsString(url_item) ? url_item->valuestring : "N/A");
    PR_INFO("HTTPS URL: %s", cJSON_IsString(https_item) ? https_item->valuestring : "N/A");
}

OPERATE_RET audio_dp_obj_proc(dp_obj_recv_t *dpobj)
{
    uint32_t index = 0;
    for (index = 0; index < dpobj->dpscnt; index++) {
        dp_obj_t *dp = dpobj->dps + index;
        PR_DEBUG("idx:%d dpid:%d type:%d ts:%u", index, dp->id, dp->type, dp->time_stamp);

        switch (dp->id) {
        case DPID_VOLUME: {
            uint8_t volume = dp->value.dp_value;
            PR_DEBUG("volume:%d", volume);
            ai_chat_set_volume(volume);
#if defined(ENABLE_CHAT_DISPLAY) && (ENABLE_CHAT_DISPLAY == 1)
            char volume_str[20] = {0};
            snprintf(volume_str, sizeof(volume_str), "%s%d", VOLUME, volume);
            ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)volume_str, strlen(volume_str));
#endif
            break;
        }
        default:
            break;
        }
    }

    return OPRT_OK;
}

OPERATE_RET ai_audio_volume_upload(void)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    dp_obj_t           dp_obj = {0};

    uint8_t volume = ai_chat_get_volume();

    dp_obj.id             = DPID_VOLUME;
    dp_obj.type           = PROP_VALUE;
    dp_obj.value.dp_value = volume;

    PR_DEBUG("DP upload volume:%d", volume);

    return tuya_iot_dp_obj_report(client, client->activate.devid, &dp_obj, 1, 0);
}

static OPERATE_RET ai_device_power_upload(void)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    dp_obj_t dp_obj = {0};

    /* DP 1 is the product's master switch.  Leaving it at its schema default
     * false makes some Smart Life panels disable voice/persona controls even
     * though MQTT and the conversational channel are connected. */
    dp_obj.id = DPID_POWER;
    dp_obj.type = PROP_BOOL;
    dp_obj.value.dp_bool = true;

    return tuya_iot_dp_obj_report(client, client->activate.devid, &dp_obj, 1, 0);
}

static void ai_device_online_schedule(void)
{
    OPERATE_RET ret;
    size_t delay_count = sizeof(DP_ONLINE_RETRY_DELAYS_MS) /
                         sizeof(DP_ONLINE_RETRY_DELAYS_MS[0]);
    TIME_MS delay_ms;

    if (NULL == sg_dp_online_sync_timer || FALSE == sg_mqtt_connected) {
        return;
    }

    if (sg_dp_online_retry_index >= delay_count) {
        sg_dp_online_retry_index = (uint8_t)(delay_count - 1);
    }
    delay_ms = DP_ONLINE_RETRY_DELAYS_MS[sg_dp_online_retry_index];

    if (tal_sw_timer_is_running(sg_dp_online_sync_timer)) {
        tal_sw_timer_stop(sg_dp_online_sync_timer);
    }
    ret = tal_sw_timer_start(sg_dp_online_sync_timer, delay_ms, TAL_TIMER_ONCE);
    if (OPRT_OK != ret) {
        PR_ERR("DP online sync timer start failed: ret=%d delay_ms=%u", ret, delay_ms);
    } else {
        PR_DEBUG("DP online sync scheduled: delay_ms=%u attempt=%u", delay_ms,
                 (unsigned int)sg_dp_online_retry_index + 1);
    }
}

static void ai_device_online_sync_timer_cb(TIMER_ID timer_id, void *arg)
{
    OPERATE_RET power_ret;
    OPERATE_RET volume_ret;
    size_t delay_count = sizeof(DP_ONLINE_RETRY_DELAYS_MS) /
                         sizeof(DP_ONLINE_RETRY_DELAYS_MS[0]);

    (void)timer_id;
    (void)arg;

    if (FALSE == sg_mqtt_connected) {
        return;
    }

    /* Report separately: a product-schema mismatch on DP1 must not suppress
     * the valid volume state, and vice versa. */
    power_ret = ai_device_power_upload();
    volume_ret = ai_audio_volume_upload();

    if (OPRT_OK == power_ret && OPRT_OK == volume_ret) {
        sg_dp_online_retry_index = 0;
        PR_INFO("DP online sync accepted: power_ret=%d volume_ret=%d volume=%d",
                power_ret, volume_ret, ai_chat_get_volume());
        return;
    }

    if (sg_dp_online_retry_index + 1 < delay_count) {
        sg_dp_online_retry_index++;
    }
    PR_ERR("DP online sync failed: power_ret=%d volume_ret=%d next_delay_ms=%u",
           power_ret, volume_ret,
           DP_ONLINE_RETRY_DELAYS_MS[sg_dp_online_retry_index]);
    ai_device_online_schedule();
}

/**
 * @brief user defined event handler
 *
 * @param client device info
 * @param event the event info
 * @return void
 */
void user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    PR_DEBUG("Tuya Event ID:%d(%s)", event->id, EVENT_ID2STR(event->id));
    PR_INFO("Device Free heap %d", tal_system_get_free_heap_size());

    switch (event->id) {
    case TUYA_EVENT_BIND_START:
        PR_INFO("Device Bind Start!");
        app_chat_bot_main_ui_send_bound(FALSE);

#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
        ai_audio_player_alert(AI_AUDIO_ALERT_NETWORK_CFG);
#endif

        break;

    /* Print the QRCode for Tuya APP bind */
    case TUYA_EVENT_DIRECT_MQTT_CONNECTED: {
#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
        char buffer[255];
        sprintf(buffer, "https://smartapp.tuya.com/s/p?p=%s&uuid=%s&v=2.0", TUYA_PRODUCT_ID, license.uuid);
        PR_INFO("Tuya APP bind URL: %s", buffer);
        app_chat_bot_main_ui_send_bind_url(buffer);
        PR_INFO("If QR code is not visible, generate a QR code from the URL above.");
        qrcode_string_output(buffer, user_log_output_cb, 0);
#endif
    } break;

    case TUYA_EVENT_BIND_TOKEN_ON:
        break;

    /* MQTT with tuya cloud is connected, device online */
    case TUYA_EVENT_MQTT_CONNECTED:
        PR_INFO("Device MQTT Connected!");
        app_chat_bot_main_ui_send_bound(TRUE);

        sg_mqtt_connected = TRUE;
        sg_dp_online_retry_index = 0;

#if defined(ENABLE_CHAT_DISPLAY) && (ENABLE_CHAT_DISPLAY == 1)
        UI_WIFI_STATUS_E wifi_status = UI_WIFI_STATUS_GOOD;
        ai_ui_disp_msg(AI_UI_DISP_NETWORK, (uint8_t *)&wifi_status, sizeof(UI_WIFI_STATUS_E));
#endif
        ai_device_online_schedule();
        break;

    /* MQTT with tuya cloud is disconnected, device offline */
    case TUYA_EVENT_MQTT_DISCONNECT:
        PR_INFO("Device MQTT DisConnected!");
        sg_mqtt_connected = FALSE;
        sg_dp_online_retry_index = 0;
        if (NULL != sg_dp_online_sync_timer &&
            tal_sw_timer_is_running(sg_dp_online_sync_timer)) {
            tal_sw_timer_stop(sg_dp_online_sync_timer);
        }
        break;

    /* RECV upgrade request */
    case TUYA_EVENT_UPGRADE_NOTIFY:
        user_upgrade_notify_on(client, event->value.asJSON);
        break;

    /* Sync time with tuya Cloud */
    case TUYA_EVENT_TIMESTAMP_SYNC:
        PR_INFO("Sync timestamp:%d", event->value.asInteger);
        tal_event_publish("app.time.sync", NULL);
        break;

    case TUYA_EVENT_RESET: {
        tuya_reset_type_t reset_type = (tuya_reset_type_t)event->value.asInteger;
        PR_INFO("Device Reset:%d", reset_type);

        // TUYA_RESET_TYPE_FACTORY, TUYA_RESET_TYPE_REMOTE_FACTORY, TUYA_RESET_TYPE_DATA_FACTORY
        // Need remove the device application data from the kv store
    } break;
    case TUYA_EVENT_RESET_COMPLETE: {
        PR_INFO("Device Reset Complete!");

        // Restart the device
        tal_system_reset();
    } break;

    /* RECV OBJ DP */
    case TUYA_EVENT_DP_RECEIVE_OBJ: {
        dp_obj_recv_t *dpobj = event->value.dpobj;
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d CNT:%u", dpobj->cmd_tp, dpobj->dtt_tp, dpobj->dpscnt);
        if (dpobj->devid != NULL) {
            PR_DEBUG("devid.%s", dpobj->devid);
        }

        audio_dp_obj_proc(dpobj);

        tuya_iot_dp_obj_report(client, dpobj->devid, dpobj->dps, dpobj->dpscnt, 0);

    } break;

    /* RECV RAW DP */
    case TUYA_EVENT_DP_RECEIVE_RAW: {
        dp_raw_recv_t *dpraw = event->value.dpraw;
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d", dpraw->cmd_tp, dpraw->dtt_tp);
        if (dpraw->devid != NULL) {
            PR_DEBUG("devid.%s", dpraw->devid);
        }

        uint32_t  index = 0;
        dp_raw_t *dp    = &dpraw->dp;
        PR_DEBUG("dpid:%d type:RAW len:%d data:", dp->id, dp->len);
        for (index = 0; index < dp->len; index++) {
            PR_DEBUG_RAW("%02x", dp->data[index]);
        }

        tuya_iot_dp_raw_report(client, dpraw->devid, &dpraw->dp, 3);

    } break;

    default:
        break;
    }
}

/**
 * @brief user defined network check callback, it will check the network every 1sec,
 *        in this demo it alwasy return ture due to it's a wired demo
 *
 * @return true
 * @return false
 */
bool user_network_check(void)
{
    netmgr_status_e status = NETMGR_LINK_DOWN;
    netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &status);
    return status == NETMGR_LINK_DOWN ? false : true;
}

void user_main(void)
{
    int ret = OPRT_OK;

    //! open iot development kit runtim init
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_psram_malloc, .free_fn = tal_psram_free});
#else
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_malloc, .free_fn = tal_free});
#endif

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("Application information:");
    PR_NOTICE("Project name:        %s", PROJECT_NAME);
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("TuyaOpen version:    %s", OPEN_VERSION);
    PR_NOTICE("TuyaOpen commit-id:  %s", OPEN_COMMIT);
    PR_NOTICE("Platform chip:       %s", PLATFORM_CHIP);
    PR_NOTICE("Platform board:      %s", PLATFORM_BOARD);
    PR_NOTICE("Platform commit-id:  %s", PLATFORM_COMMIT);

    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key  = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init();
    tal_time_service_init();
    tal_cli_init();

    ret = tal_sw_timer_create(ai_device_online_sync_timer_cb, NULL,
                              &sg_dp_online_sync_timer);
    if (OPRT_OK != ret) {
        PR_ERR("DP online sync timer create failed: ret=%d", ret);
    }

    reset_netconfig_start();

    ret = tuya_iot_license_read(&license);
    if (ret != OPRT_OK) {
        PR_ERR("Tuya device license unavailable; waiting for factory provisioning.");
        do {
            tal_system_sleep(LICENSE_RETRY_INTERVAL_MS);
            ret = tuya_iot_license_read(&license);
        } while (ret != OPRT_OK);
    }
    PR_NOTICE("Tuya device license loaded from protected runtime storage.");

    /* Initialize Tuya device configuration */
    ret = tuya_iot_init(&ai_client, &(const tuya_iot_config_t){
                                        .software_ver = PROJECT_VERSION,
                                        .productkey   = TUYA_PRODUCT_ID,
                                        .uuid         = license.uuid,
                                        .authkey      = license.authkey,
                                        // .firmware_key      = TUYA_DEVICE_FIRMWAREKEY,
                                        .event_handler = user_event_handler_on,
                                        .network_check = user_network_check,
                                    });
    assert(ret == OPRT_OK);

#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
    TUYA_LwIP_Init();
#endif

    // network init
    netmgr_type_e type = 0;
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    type |= NETCONN_WIFI;
#endif
#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
    type |= NETCONN_WIRED;
#endif
    netmgr_init(type);
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_NETCFG, &(netcfg_args_t){.type = NETCFG_TUYA_BLE | NETCFG_TUYA_WIFI_AP});
#endif

    PR_DEBUG("tuya_iot_init success");

    ret = board_register_hardware();
    if (ret != OPRT_OK) {
        PR_ERR("board_register_hardware failed");
    }

    ret = app_chat_bot_init();
    if (ret != OPRT_OK) {
        PR_ERR("app_chat_bot_init failed");
    }

#if defined(ENABLE_BATTERY) && (ENABLE_BATTERY == 1)
    ret = app_battery_init();
    if (ret != OPRT_OK) {
        PR_ERR("app_battery_init failed");
    }
#endif

    /* Start tuya iot task */
    tuya_iot_start(&ai_client);

    tkl_wifi_set_lp_mode(0, 0);

    reset_netconfig_check();

    for (;;) {
        /* Loop to receive packets, and handles client keepalive */
        tuya_iot_yield(&ai_client);
    }
}

/**
 * @brief main
 *
 * @param argc
 * @param argv
 * @return void
 */
#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#else

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief  task thread
 *
 * @param[in] arg:Parameters when creating a task
 * @return none
 */
static void tuya_app_thread(void *arg)
{
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth   = 4096;
    thrd_param.priority     = 4;
    thrd_param.thrdname     = "tuya_app_main";
    /* Stack must be internal DRAM: esp_partition_mmap / flash pause cache (ESP-SR) asserts
     * esp_task_stack_is_sane_cache_disabled() — PSRAM stacks fail that check (see IDF cache_utils.c). */
    thrd_param.psram_mode   = 0;
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
