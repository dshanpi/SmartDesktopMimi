#include "backend_command_router.h"

#include "../middleware/middleware.h"
#include "../system/backend_types.h"
#include "../system/service_ai.h"
#include "../system/service_bt.h"
#include "../system/service_cloud.h"
#include "../system/service_hdmi_preview.h"
#include "../system/service_led.h"
#include "../system/service_ota.h"
#include "../system/service_wifi.h"

#include <stdbool.h>
#include <stddef.h>

static void handle_wifi_command(const mw_msg_t *msg)
{
    if (msg->data_len < sizeof(wifi_cmd_t)) return;

    const wifi_cmd_t *cmd = (const wifi_cmd_t *)msg->data;
    switch (cmd->action) {
    case CMD_SCAN:
        service_wifi_scan();
        break;
    case CMD_CONNECT:
        service_wifi_connect(cmd->ssid, cmd->password);
        break;
    case CMD_DISCONNECT:
        service_wifi_disconnect();
        break;
    case CMD_ENABLE:
        service_wifi_set_enabled(true);
        break;
    case CMD_DISABLE:
        service_wifi_set_enabled(false);
        break;
    case CMD_GET_RUNTIME:
        service_wifi_send_runtime_status();
        break;
    default:
        break;
    }
}

static void handle_bt_command(const mw_msg_t *msg)
{
    if (msg->data_len < sizeof(bt_cmd_t)) return;

    const bt_cmd_t *cmd = (const bt_cmd_t *)msg->data;
    switch (cmd->action) {
    case BT_CMD_ENABLE:
        service_bt_enable(true);
        break;
    case BT_CMD_DISABLE:
        service_bt_enable(false);
        break;
    case BT_CMD_DISCONNECT:
        service_bt_disconnect_device(cmd->bd_addr);
        break;
    case BT_CMD_SET_VOLUME:
        service_bt_set_volume(cmd->volume);
        break;
    case BT_CMD_PLAY:
        service_bt_avrcp_play();
        break;
    case BT_CMD_PAUSE:
        service_bt_avrcp_pause();
        break;
    case BT_CMD_NEXT:
        service_bt_avrcp_next();
        break;
    case BT_CMD_PREV:
        service_bt_avrcp_prev();
        break;
    case BT_CMD_GET_STATUS:
        service_bt_send_runtime_status();
        break;
    case BT_CMD_SET_ADAPTER_NAME:
        service_bt_set_adapter_name(cmd->adapter_name);
        break;
    default:
        break;
    }
}

static void handle_ai_command(const mw_msg_t *msg)
{
    if (msg->data_len < sizeof(ai_cmd_t)) return;

    const ai_cmd_t *cmd = (const ai_cmd_t *)msg->data;
    switch (cmd->action) {
    case AI_CMD_START_LISTEN:
        service_ai_start_listen();
        break;
    case AI_CMD_STOP_LISTEN:
        service_ai_stop_listen();
        break;
    case AI_CMD_GET_STATUS:
        service_ai_send_status();
        break;
    case AI_CMD_ENTER_FREE_CHAT:
        service_ai_enter_free_chat();
        break;
    case AI_CMD_EXIT_FREE_CHAT:
        service_ai_exit_free_chat();
        break;
    default:
        break;
    }
}

static void handle_ota_command(const mw_msg_t *msg)
{
    if (msg->data_len < sizeof(ota_cmd_t)) return;
    service_ota_handle_command((const ota_cmd_t *)msg->data);
}

static void handle_hdmi_command(const mw_msg_t *msg)
{
    if (msg->data_len < sizeof(hdmi_cmd_t)) return;

    const hdmi_cmd_t *cmd = (const hdmi_cmd_t *)msg->data;
    switch (cmd->action) {
    case HDMI_CMD_ENABLE:
        service_hdmi_preview_set_enabled(true);
        break;
    case HDMI_CMD_DISABLE:
        service_hdmi_preview_set_enabled(false);
        break;
    case HDMI_CMD_GET_STATUS:
        service_hdmi_preview_send_status();
        break;
    default:
        break;
    }
}

static void handle_cloud_command(const mw_msg_t *msg)
{
    if (msg->data_len < sizeof(cloud_cmd_t)) return;

    const cloud_cmd_t *cmd = (const cloud_cmd_t *)msg->data;
    switch (cmd->action) {
    case CLOUD_CMD_GET_STATUS:
        service_cloud_send_status();
        break;
    case CLOUD_CMD_REFRESH_BIND_TOKEN:
        service_cloud_refresh_bind_token();
        break;
    case CLOUD_CMD_DISMISS_OTA:
        service_cloud_clear_ota_pending();
        break;
    default:
        break;
    }
}

static void handle_led_command(const mw_msg_t *msg)
{
    if (msg->data_len < sizeof(led_cmd_t)) return;
    service_led_handle_command((const led_cmd_t *)msg->data);
}

int backend_command_router_register(void)
{
    static const struct {
        mw_topic_t topic;
        mw_subscribe_cb_t handler;
    } routes[] = {
        {TOPIC_WIFI_COMMAND, handle_wifi_command},
        {TOPIC_BT_COMMAND, handle_bt_command},
        {TOPIC_AI_COMMAND, handle_ai_command},
        {TOPIC_OTA_COMMAND, handle_ota_command},
        {TOPIC_HDMI_COMMAND, handle_hdmi_command},
        {TOPIC_CLOUD_COMMAND, handle_cloud_command},
        {TOPIC_LED_COMMAND, handle_led_command},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        if (mw_backend_register_handler(routes[i].topic, routes[i].handler) != 0) {
            return -1;
        }
    }
    return 0;
}
