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
#include "mimi_device_pairing.h"
#include "tuya_iot_dp.h"
#include "dp_schema.h"
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

#define DPID_POWER             1
#define DPID_VOLUME            3
#define DPID_SCREEN_BRIGHTNESS 101
#define DPID_HDMI_PREVIEW      102
#define DPID_BLUETOOTH         103
#define DPID_LED_SWITCH        104
#define DPID_LED_BRIGHTNESS    105
#define DPID_TEMPERATURE       106
#define DPID_HUMIDITY          107
#define DPID_BUS_VOLTAGE       108
#define DPID_BUS_CURRENT       109
#define DPID_BUS_POWER         110
#define DPID_ASSISTANT_STATE   111
#define DPID_FREE_CHAT         112

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
static uint16_t      sg_last_schema_mask = UINT16_MAX;

typedef struct {
    uint8_t id;
    dp_prop_tp_t type;
    dp_mode_t mode;
} ai_dp_contract_t;

static const ai_dp_contract_t AI_DP_CONTRACT[] = {
    {DPID_POWER, PROP_BOOL, M_RW},
    {DPID_VOLUME, PROP_VALUE, M_RW},
    {DPID_SCREEN_BRIGHTNESS, PROP_VALUE, M_RW},
    {DPID_HDMI_PREVIEW, PROP_BOOL, M_RW},
    {DPID_BLUETOOTH, PROP_BOOL, M_RW},
    {DPID_LED_SWITCH, PROP_BOOL, M_RW},
    {DPID_LED_BRIGHTNESS, PROP_VALUE, M_RW},
    {DPID_TEMPERATURE, PROP_VALUE, M_RO},
    {DPID_HUMIDITY, PROP_VALUE, M_RO},
    {DPID_BUS_VOLTAGE, PROP_VALUE, M_RO},
    {DPID_BUS_CURRENT, PROP_VALUE, M_RO},
    {DPID_BUS_POWER, PROP_VALUE, M_RO},
    {DPID_ASSISTANT_STATE, PROP_ENUM, M_RO},
    {DPID_FREE_CHAT, PROP_BOOL, M_RW},
};

static OPERATE_RET ai_dp_report_result(OPERATE_RET ret,
                                       uint16_t schema_valid_count);

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
    PR_INFO("HMAC: %s", cJSON_IsString(hmac_item) ? "present" : "N/A");
    PR_INFO("URL: %s", cJSON_IsString(url_item) ? "present" : "N/A");
    PR_INFO("HTTPS URL: %s", cJSON_IsString(https_item) ? "present" : "N/A");
}

static BOOL_T ai_dp_write_is_supported(tuya_iot_client_t *client,
                                       const dp_obj_recv_t *request,
                                       const dp_obj_t *dp)
{
    char *devid;
    dp_node_t *node;

    if (NULL == client || NULL == request || NULL == dp) return FALSE;
    devid = request->devid && request->devid[0] ? request->devid :
                                                     client->activate.devid;
    if (NULL == devid || devid[0] == '\0') return FALSE;
    node = dp_node_find_by_devid(devid, dp->id);
    return node && node->desc.type == T_OBJ &&
           node->desc.prop_tp == dp->type && node->desc.mode != M_RO;
}

OPERATE_RET audio_dp_obj_proc(dp_obj_recv_t *dpobj)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    OPERATE_RET result = OPRT_OK;
    uint32_t index = 0;

    if (NULL == dpobj || NULL == client) return OPRT_INVALID_PARM;

    for (index = 0; index < dpobj->dpscnt; index++) {
        dp_obj_t *dp = dpobj->dps + index;
        OPERATE_RET ret = OPRT_OK;

        PR_DEBUG("idx:%d dpid:%d type:%d ts:%u", index, dp->id, dp->type,
                 dp->time_stamp);
        if (FALSE == ai_dp_write_is_supported(client, dpobj, dp)) {
            PR_WARN("reject DP %u absent or incompatible with product schema",
                    dp->id);
            result = OPRT_NOT_SUPPORTED;
            continue;
        }

        switch (dp->id) {
        case DPID_POWER:
            if (dp->type != PROP_BOOL) {
                ret = OPRT_INVALID_PARM;
                break;
            }
            ret = app_chat_bot_set_assistant_enabled(dp->value.dp_bool);
            if (OPRT_OK == ret) {
                ret = app_chat_bot_main_ui_send_iot_dp(
                          DPID_POWER, "bool", dp->value.dp_bool ? 1 : 0) == 0 ?
                      OPRT_OK : OPRT_COM_ERROR;
                if (OPRT_OK == ret) {
                    dp->value.dp_bool = app_chat_bot_is_assistant_enabled();
                    ret = ai_dp_report_result(
                        tuya_iot_dp_obj_report(client, dpobj->devid, dp, 1, 0),
                        1);
                }
            }
            break;
        case DPID_VOLUME: {
            int volume;

            if (dp->type != PROP_VALUE || dp->value.dp_value < 0 ||
                dp->value.dp_value > 100) {
                ret = OPRT_INVALID_PARM;
                break;
            }
            volume = dp->value.dp_value;
            ret = ai_chat_set_volume(volume);
            if (OPRT_OK != ret) break;
            ret = app_chat_bot_main_ui_send_iot_dp(
                      DPID_VOLUME, "value", volume) == 0 ?
                  OPRT_OK : OPRT_COM_ERROR;
            if (OPRT_OK == ret) {
                dp->value.dp_value = ai_chat_get_volume();
                ret = ai_dp_report_result(
                    tuya_iot_dp_obj_report(client, dpobj->devid, dp, 1, 0),
                    1);
            }
#if defined(ENABLE_CHAT_DISPLAY) && (ENABLE_CHAT_DISPLAY == 1)
            char volume_str[20] = {0};
            snprintf(volume_str, sizeof(volume_str), "%s%d", VOLUME, volume);
            ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)volume_str, strlen(volume_str));
#endif
            break;
        }
        case DPID_SCREEN_BRIGHTNESS:
        case DPID_LED_BRIGHTNESS:
            if (dp->type != PROP_VALUE || dp->value.dp_value < 0 ||
                dp->value.dp_value > 100 ||
                (dp->id == DPID_SCREEN_BRIGHTNESS && dp->value.dp_value < 20)) {
                ret = OPRT_INVALID_PARM;
                break;
            }
            ret = app_chat_bot_main_ui_send_iot_dp(dp->id, "value",
                                                    dp->value.dp_value) == 0 ?
                  OPRT_OK : OPRT_COM_ERROR;
            break;
        case DPID_HDMI_PREVIEW:
        case DPID_BLUETOOTH:
        case DPID_LED_SWITCH:
            if (dp->type != PROP_BOOL) {
                ret = OPRT_INVALID_PARM;
                break;
            }
            ret = app_chat_bot_main_ui_send_iot_dp(dp->id, "bool",
                                                    dp->value.dp_bool ? 1 : 0) == 0 ?
                  OPRT_OK : OPRT_COM_ERROR;
            break;
        case DPID_FREE_CHAT:
            if (dp->type != PROP_BOOL) {
                ret = OPRT_INVALID_PARM;
                break;
            }
            ret = app_chat_bot_set_free_chat_enabled(dp->value.dp_bool);
            if (OPRT_OK == ret) {
                ret = app_chat_bot_main_ui_send_iot_dp(
                          DPID_FREE_CHAT, "bool", dp->value.dp_bool ? 1 : 0) == 0 ?
                      OPRT_OK : OPRT_COM_ERROR;
                if (OPRT_OK == ret) {
                    ret = ai_dp_report_result(
                        tuya_iot_dp_obj_report(client, dpobj->devid, dp, 1, 0),
                        1);
                }
            }
            break;
        case DPID_TEMPERATURE:
        case DPID_HUMIDITY:
        case DPID_BUS_VOLTAGE:
        case DPID_BUS_CURRENT:
        case DPID_BUS_POWER:
        case DPID_ASSISTANT_STATE:
            PR_WARN("reject write to read-only DP %u", dp->id);
            ret = OPRT_NOT_SUPPORTED;
            break;
        default:
            PR_WARN("ignore unsupported object DP %u", dp->id);
            ret = OPRT_NOT_SUPPORTED;
            break;
        }

        if (OPRT_OK != ret) {
            PR_WARN("DP %u command rejected: %d", dp->id, ret);
            result = ret;
        }
    }

    return result;
}

static BOOL_T ai_dp_append_if_supported(dp_obj_t *out, uint16_t *count,
                                        uint8_t id, dp_prop_tp_t type, int value)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    dp_node_t *node;
    dp_obj_t *dp;

    if (NULL == out || NULL == count || NULL == client ||
        client->activate.devid[0] == '\0') {
        return FALSE;
    }
    node = dp_node_find_by_devid(client->activate.devid, id);
    if (NULL == node || node->desc.type != T_OBJ || node->desc.prop_tp != type ||
        node->desc.mode == M_WR) {
        return FALSE;
    }

    dp = &out[*count];
    memset(dp, 0, sizeof(*dp));
    dp->id = id;
    dp->type = type;
    if (type == PROP_BOOL) {
        dp->value.dp_bool = value ? true : false;
    } else if (type == PROP_ENUM) {
        dp->value.dp_enum = (uint32_t)value;
    } else {
        dp->value.dp_value = value;
    }
    (*count)++;
    return TRUE;
}

static OPERATE_RET ai_dp_report_result(OPERATE_RET ret, uint16_t schema_valid_count)
{
    /* TuyaOpen filters unchanged values before publishing and currently
     * returns OPRT_SVC_DP_ID_NOT_FOUND when that leaves no payload.  The DPs
     * have already passed the live schema check above, so this is a successful
     * no-op rather than a missing-schema failure. */
    if (schema_valid_count > 0 && ret == OPRT_SVC_DP_ID_NOT_FOUND) {
        PR_DEBUG("DP report skipped because schema-valid values are unchanged");
        return OPRT_OK;
    }
    return ret;
}

static BOOL_T ai_dp_contract_is_supported(tuya_iot_client_t *client,
                                           const ai_dp_contract_t *contract)
{
    dp_node_t *node;

    if (NULL == client || NULL == contract || client->activate.devid[0] == '\0') {
        return FALSE;
    }
    node = dp_node_find_by_devid(client->activate.devid, contract->id);
    return node && node->desc.type == T_OBJ &&
           node->desc.prop_tp == contract->type &&
           node->desc.mode == contract->mode;
}

static void ai_dp_send_schema_status(BOOL_T force)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    uint16_t mask = 0;
    uint8_t supported = 0;
    char missing[96] = {0};
    size_t used = 0;
    size_t i;

    if (FALSE == sg_mqtt_connected || NULL == client ||
        client->activate.devid[0] == '\0') {
        return;
    }

    for (i = 0; i < CNTSOF(AI_DP_CONTRACT); i++) {
        if (ai_dp_contract_is_supported(client, &AI_DP_CONTRACT[i])) {
            mask |= (uint16_t)(1U << i);
            supported++;
        } else if (used < sizeof(missing)) {
            int written = snprintf(missing + used, sizeof(missing) - used,
                                   "%s%u", used ? "," : "",
                                   (unsigned int)AI_DP_CONTRACT[i].id);
            if (written > 0 && (size_t)written < sizeof(missing) - used) {
                used += (size_t)written;
            }
        }
    }

    if (!force && mask == sg_last_schema_mask) return;
    sg_last_schema_mask = mask;
    app_chat_bot_main_ui_send_iot_schema(supported,
                                         (uint8_t)CNTSOF(AI_DP_CONTRACT));
    if (supported == CNTSOF(AI_DP_CONTRACT)) {
        PR_INFO("Tuya product schema complete: supported=%u required=%u",
                supported, (unsigned int)CNTSOF(AI_DP_CONTRACT));
    } else {
        PR_WARN("Tuya product schema incomplete: supported=%u required=%u missing=%s",
                supported, (unsigned int)CNTSOF(AI_DP_CONTRACT),
                missing[0] ? missing : "unknown");
    }
}

static OPERATE_RET ai_dp_report_one(uint8_t id, dp_prop_tp_t type, int value)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    dp_obj_t dp = {0};
    uint16_t count = 0;

    if (FALSE == sg_mqtt_connected || NULL == client) return OPRT_COM_ERROR;
    if (FALSE == ai_dp_append_if_supported(&dp, &count, id, type, value)) {
        return OPRT_NOT_SUPPORTED;
    }
    return ai_dp_report_result(
        tuya_iot_dp_obj_report(client, client->activate.devid, &dp, 1, 0),
        count);
}

static BOOL_T json_number(cJSON *root, const char *name, int *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || NULL == value) return FALSE;
    *value = item->valueint;
    return TRUE;
}

static BOOL_T json_boolean(cJSON *root, const char *name, int *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if ((!cJSON_IsBool(item) && !cJSON_IsNumber(item)) || NULL == value) return FALSE;
    *value = cJSON_IsBool(item) ? cJSON_IsTrue(item) : (item->valueint != 0);
    return TRUE;
}

OPERATE_RET tuya_device_iot_state_update(const char *json)
{
    tuya_iot_client_t *client = tuya_iot_client_get();
    dp_obj_t dps[CNTSOF(AI_DP_CONTRACT)];
    uint16_t count = 0;
    cJSON *root;
    int value;
    OPERATE_RET ret;

    if (NULL == json || NULL == client) return OPRT_INVALID_PARM;
    root = cJSON_Parse(json);
    if (NULL == root) return OPRT_INVALID_PARM;

    if (json_boolean(root, "assistant_enabled", &value)) {
        ret = app_chat_bot_set_assistant_enabled(value ? TRUE : FALSE);
        if (OPRT_OK == ret) {
            ai_dp_append_if_supported(dps, &count, DPID_POWER, PROP_BOOL,
                                      app_chat_bot_is_assistant_enabled());
        }
    }
    if (json_number(root, "volume", &value) && value >= 0 && value <= 100) {
        ai_chat_set_volume(value);
        ai_dp_append_if_supported(dps, &count, DPID_VOLUME, PROP_VALUE,
                                  ai_chat_get_volume());
    }
    if (json_number(root, "brightness", &value) && value >= 20 && value <= 100)
        ai_dp_append_if_supported(dps, &count, DPID_SCREEN_BRIGHTNESS, PROP_VALUE, value);
    if (json_boolean(root, "hdmi_enabled", &value))
        ai_dp_append_if_supported(dps, &count, DPID_HDMI_PREVIEW, PROP_BOOL, value);
    if (json_boolean(root, "bluetooth_enabled", &value))
        ai_dp_append_if_supported(dps, &count, DPID_BLUETOOTH, PROP_BOOL, value);
    if (json_boolean(root, "led_enabled", &value))
        ai_dp_append_if_supported(dps, &count, DPID_LED_SWITCH, PROP_BOOL, value);
    if (json_number(root, "led_brightness", &value) && value >= 0 && value <= 100)
        ai_dp_append_if_supported(dps, &count, DPID_LED_BRIGHTNESS, PROP_VALUE, value);
    if (json_number(root, "temperature_tenths", &value))
        ai_dp_append_if_supported(dps, &count, DPID_TEMPERATURE, PROP_VALUE, value);
    if (json_number(root, "humidity_tenths", &value))
        ai_dp_append_if_supported(dps, &count, DPID_HUMIDITY, PROP_VALUE, value);
    if (json_number(root, "bus_voltage_mv", &value))
        ai_dp_append_if_supported(dps, &count, DPID_BUS_VOLTAGE, PROP_VALUE, value);
    if (json_number(root, "bus_current_ma", &value))
        ai_dp_append_if_supported(dps, &count, DPID_BUS_CURRENT, PROP_VALUE, value);
    if (json_number(root, "bus_power_mw", &value))
        ai_dp_append_if_supported(dps, &count, DPID_BUS_POWER, PROP_VALUE, value);
    if (json_number(root, "assistant_state", &value) && value >= 0 && value <= 6)
        ai_dp_append_if_supported(dps, &count, DPID_ASSISTANT_STATE, PROP_ENUM, value);
    if (json_boolean(root, "free_chat", &value))
        ai_dp_append_if_supported(dps, &count, DPID_FREE_CHAT, PROP_BOOL, value);

    cJSON_Delete(root);
    ai_dp_send_schema_status(FALSE);
    if (count == 0) return OPRT_NOT_SUPPORTED;
    if (FALSE == sg_mqtt_connected) return OPRT_COM_ERROR;

    ret = ai_dp_report_result(
        tuya_iot_dp_obj_report(client, client->activate.devid, dps, count, 0),
        count);
    PR_INFO("IoT state sync result=%d supported_dps=%u", ret, count);
    return ret;
}

OPERATE_RET ai_audio_volume_upload(void)
{
    uint8_t volume = ai_chat_get_volume();

    PR_DEBUG("DP upload volume:%d", volume);
    return ai_dp_report_one(DPID_VOLUME, PROP_VALUE, volume);
}

static OPERATE_RET ai_device_power_upload(void)
{
    /* DP 1 is the product's master switch.  Leaving it at its schema default
     * false makes some Smart Life panels disable voice/persona controls even
     * though MQTT and the conversational channel are connected. */
    return ai_dp_report_one(DPID_POWER, PROP_BOOL,
                            app_chat_bot_is_assistant_enabled());
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

    ai_dp_send_schema_status(FALSE);

    /* Report separately: a product-schema mismatch on DP1 must not suppress
     * the valid volume state, and vice versa. */
    power_ret = ai_device_power_upload();
    volume_ret = ai_audio_volume_upload();
    app_chat_bot_main_ui_request_iot_state();

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
        sg_mqtt_connected = TRUE;
        sg_dp_online_retry_index = 0;
        sg_last_schema_mask = UINT16_MAX;
        app_chat_bot_main_ui_send_bound(TRUE);
        ai_dp_send_schema_status(TRUE);

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
        sg_last_schema_mask = UINT16_MAX;
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

        /* The SDK clears its own binding/KV data. Ask the product backend to
         * clear the device preferences controlled by the Tuya panel too. */
        app_chat_bot_main_ui_send_bound(FALSE);
        app_chat_bot_main_ui_send_iot_reset();
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

    } break;

    /* RECV RAW DP */
    case TUYA_EVENT_DP_RECEIVE_RAW: {
        dp_raw_recv_t *dpraw = event->value.dpraw;
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d", dpraw->cmd_tp, dpraw->dtt_tp);
        if (dpraw->devid != NULL) {
            PR_DEBUG("devid.%s", dpraw->devid);
        }

        dp_raw_t *dp    = &dpraw->dp;
        /* This product defines no RAW DP. Do not dump or echo opaque cloud
         * payloads: they cannot be type/range validated and may be private. */
        PR_WARN("reject unsupported RAW DP id=%u len=%u", dp->id, dp->len);

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
    /* Public edition must stop before SDK, CLI, timers or network startup. */
    if (!mimi_tuya_pairing_enabled()) return;
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

    /* Explicit product adapter owns runtime one-device authorization. */
    ret = mimi_tuya_pairing_init(&ai_client, &license, &(const tuya_iot_config_t){
                                        .software_ver = PROJECT_VERSION,
                                        .productkey   = TUYA_PRODUCT_ID,
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
