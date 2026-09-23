/**
 * @file app_chat_bot.c
 * @brief app_chat_bot module is used to
 * @version 0.1
 * @date 2025-03-25
 */

#include "tal_api.h"
#include "tal_thread.h"

#include "netmgr.h"

#include "ai_manage_mode.h"
#include "ai_mode_free.h"
#include "ai_chat_main.h"
#include "app_chat_bot.h"
#include "tuya_ai_agent.h"
#include "tuya_ai_protocol.h"

#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
#include "ai_audio_player.h"
#include "tuya_ai_input.h"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "tkl_wifi.h"
#endif

#if defined(ENABLE_PRINTER) && (ENABLE_PRINTER == 1)
#include "app_printer.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define PRINTF_FREE_HEAP_TTIME (10 * 1000)
#define DISP_NET_STATUS_TIME   (1 * 1000)
#define TUYA_MAIN_UI_PORT      5679
#define TUYA_MAIN_UI_CMD_PORT  5678
#define MAIN_UI_REPLY_TEXT_MAX 1024
#define TTS_STREAM_WATCHDOG_MS (60 * 1000)

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
***********************const declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static TIMER_ID sg_printf_heap_tm;
static TIMER_ID sg_tts_watchdog_tm;
static BOOL_T sg_tts_stream_active = FALSE;
static int sg_main_ui_udp_fd = -1;
static int sg_main_ui_cmd_fd = -1;
static THREAD_HANDLE sg_main_ui_cmd_thread = NULL;
static BOOL_T sg_main_ui_udp_close_registered = FALSE;
static char sg_main_ui_reply_text[MAIN_UI_REPLY_TEXT_MAX];
static size_t sg_main_ui_reply_len = 0;

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
static AI_UI_WIFI_STATUS_E sg_wifi_status = AI_UI_WIFI_STATUS_DISCONNECTED;
static TIMER_ID            sg_disp_status_tm;
#endif

/***********************************************************
***********************function define**********************
***********************************************************/
#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
extern void app_ui_action_register(void);
#endif

static void __main_ui_bridge_close(void);
static void __json_escape_copy(char *dst, size_t dst_size,
                               const char *src, size_t src_len);

static int __main_ui_bridge_send(const char *json)
{
    struct sockaddr_in addr;
    ssize_t sent;

    if (NULL == json) {
        return -1;
    }

    if (FALSE == sg_main_ui_udp_close_registered) {
        atexit(__main_ui_bridge_close);
        sg_main_ui_udp_close_registered = TRUE;
    }

    if (sg_main_ui_udp_fd < 0) {
        sg_main_ui_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sg_main_ui_udp_fd < 0) {
            PR_WARN("main ui bridge socket create failed");
            return -1;
        }
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TUYA_MAIN_UI_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    sent = sendto(sg_main_ui_udp_fd, json, strlen(json), 0,
                  (const struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        PR_WARN("main ui bridge send failed");
        return -1;
    }

    return 0;
}

static void __main_ui_bridge_close(void)
{
    if (sg_main_ui_udp_fd >= 0) {
        close(sg_main_ui_udp_fd);
        sg_main_ui_udp_fd = -1;
    }

    if (sg_main_ui_cmd_fd >= 0) {
        close(sg_main_ui_cmd_fd);
        sg_main_ui_cmd_fd = -1;
    }
}

int app_chat_bot_main_ui_send_bind_url(const char *url)
{
    char escaped[300];
    char payload[384];

    if (NULL == url || url[0] == '\0') {
        return -1;
    }

    __json_escape_copy(escaped, sizeof(escaped), url, strlen(url));
    snprintf(payload, sizeof(payload),
             "{\"runtime\":\"tuya\",\"bound\":false,\"bind_url\":\"%s\"}",
             escaped);
    return __main_ui_bridge_send(payload);
}

int app_chat_bot_main_ui_send_bound(BOOL_T bound)
{
    char payload[64];

    snprintf(payload, sizeof(payload),
             "{\"runtime\":\"tuya\",\"bound\":%s}",
             bound ? "true" : "false");
    return __main_ui_bridge_send(payload);
}

static const char *__main_ui_state_from_mode(AI_MODE_STATE_E state)
{
    switch (state) {
    case AI_MODE_STATE_IDLE:
    case AI_MODE_STATE_INIT:
        return "idle";
    case AI_MODE_STATE_LISTEN:
        return "listening";
    case AI_MODE_STATE_UPLOAD:
    case AI_MODE_STATE_THINK:
        return "thinking";
    case AI_MODE_STATE_SPEAK:
        return "speaking";
    default:
        return "error";
    }
}

static const char *__main_ui_chat_mode_name(AI_CHAT_MODE_E mode)
{
    switch (mode) {
    case AI_CHAT_MODE_FREE:
        return "free";
    case AI_CHAT_MODE_WAKEUP:
        return "wakeup";
    case AI_CHAT_MODE_HOLD:
        return "hold";
    case AI_CHAT_MODE_ONE_SHOT:
        return "one_shot";
    default:
        return "unknown";
    }
}

static void __main_ui_bridge_send_state(const char *state)
{
    char payload[128];

    snprintf(payload, sizeof(payload),
             "{\"runtime\":\"tuya\",\"state\":\"%s\"}",
             state ? state : "error");
    __main_ui_bridge_send(payload);
}

static void __main_ui_bridge_send_mode(AI_CHAT_MODE_E mode)
{
    char payload[160];
    AI_MODE_STATE_E state = ai_mode_get_state();

    snprintf(payload, sizeof(payload),
             "{\"runtime\":\"tuya\",\"mode\":\"%s\",\"state\":\"%s\"}",
             __main_ui_chat_mode_name(mode),
             __main_ui_state_from_mode(state));
    __main_ui_bridge_send(payload);
}

static void __main_ui_break_chat(void)
{
#if defined(ENABLE_COMP_AI_AUDIO) && (ENABLE_COMP_AI_AUDIO == 1)
    ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
    tuya_ai_input_stop();
#endif
    tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
}

static void __main_ui_handle_enter_free_chat(BOOL_T wake)
{
    OPERATE_RET rt;

    rt = ai_mode_switch(AI_CHAT_MODE_FREE);
    if (rt != OPRT_OK) {
        PR_WARN("main ui enter free chat failed: %d", rt);
        __main_ui_bridge_send_state("error");
        return;
    }

    if (wake) {
        TUYA_CALL_ERR_LOG(ai_mode_free_wakeup());
    }

    __main_ui_bridge_send_mode(AI_CHAT_MODE_FREE);
}

static void __main_ui_handle_exit_free_chat(void)
{
    OPERATE_RET rt;

    __main_ui_break_chat();
    rt = ai_mode_switch(AI_CHAT_MODE_WAKEUP);
    if (rt != OPRT_OK) {
        PR_WARN("main ui exit free chat failed: %d", rt);
        __main_ui_bridge_send_state("error");
        return;
    }

    __main_ui_bridge_send_mode(AI_CHAT_MODE_WAKEUP);
}

static BOOL_T __json_has_cmd(const char *json, const char *cmd)
{
    char pattern[96];

    if (NULL == json || NULL == cmd) {
        return FALSE;
    }

    snprintf(pattern, sizeof(pattern), "\"cmd\":\"%s\"", cmd);
    if (strstr(json, pattern)) {
        return TRUE;
    }

    snprintf(pattern, sizeof(pattern), "\"cmd\" : \"%s\"", cmd);
    return strstr(json, pattern) ? TRUE : FALSE;
}

static void __main_ui_cmd_handle(const char *json)
{
    if (__json_has_cmd(json, "enter_free_chat")) {
        BOOL_T wake = strstr(json, "\"wake\":false") ? FALSE : TRUE;

        PR_NOTICE("main ui command: enter_free_chat wake=%d", wake);
        __main_ui_handle_enter_free_chat(wake);
        return;
    }

    if (__json_has_cmd(json, "exit_free_chat")) {
        PR_NOTICE("main ui command: exit_free_chat");
        __main_ui_handle_exit_free_chat();
        return;
    }

    PR_WARN("main ui unknown command: %s", json ? json : "(null)");
}

static void __main_ui_cmd_thread(void *arg)
{
    struct sockaddr_in addr;
    char buf[256];
    int opt = 1;
    ssize_t n;

    (void)arg;

    sg_main_ui_cmd_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sg_main_ui_cmd_fd < 0) {
        PR_WARN("main ui cmd socket create failed");
        return;
    }

    setsockopt(sg_main_ui_cmd_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TUYA_MAIN_UI_CMD_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(sg_main_ui_cmd_fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        PR_WARN("main ui cmd bind %d failed, errno=%d", TUYA_MAIN_UI_CMD_PORT, errno);
        close(sg_main_ui_cmd_fd);
        sg_main_ui_cmd_fd = -1;
        return;
    }

    PR_NOTICE("main ui command bridge listening on 127.0.0.1:%d", TUYA_MAIN_UI_CMD_PORT);

    while (1) {
        n = recv(sg_main_ui_cmd_fd, buf, sizeof(buf) - 1, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            PR_WARN("main ui cmd recv failed, errno=%d", errno);
            break;
        }

        buf[n] = '\0';
        __main_ui_cmd_handle(buf);
    }
}

static void __main_ui_cmd_start(void)
{
    OPERATE_RET rt = OPRT_OK;
    THREAD_CFG_T cfg = {
        .thrdname = "ui_cmd",
        .priority = THREAD_PRIO_3,
        .stackDepth = 4096,
    };

    if (sg_main_ui_cmd_thread) {
        return;
    }

    if (FALSE == sg_main_ui_udp_close_registered) {
        atexit(__main_ui_bridge_close);
        sg_main_ui_udp_close_registered = TRUE;
    }

    TUYA_CALL_ERR_LOG(tal_thread_create_and_start(&sg_main_ui_cmd_thread,
                                                  NULL,
                                                  NULL,
                                                  __main_ui_cmd_thread,
                                                  NULL,
                                                  &cfg));
}

static void __main_ui_bridge_send_emotion(const char *emotion)
{
    char escaped[64];
    char payload[128];

    if (NULL == emotion || emotion[0] == '\0') {
        return;
    }

    __json_escape_copy(escaped, sizeof(escaped), emotion, strlen(emotion));
    snprintf(payload, sizeof(payload),
             "{\"runtime\":\"tuya\",\"emotion\":\"%s\"}",
             escaped);
    __main_ui_bridge_send(payload);
}

static void __json_escape_copy(char *dst, size_t dst_size,
                               const char *src, size_t src_len)
{
    size_t i;
    size_t n = 0;

    if (!dst || dst_size == 0) {
        return;
    }

    for (i = 0; src && i < src_len && src[i] != '\0' && n + 1 < dst_size; i++) {
        if ((src[i] == '"' || src[i] == '\\') && n + 2 < dst_size) {
            dst[n++] = '\\';
            dst[n++] = src[i];
        } else if ((unsigned char)src[i] >= 0x20) {
            dst[n++] = src[i];
        }
    }
    dst[n] = '\0';
}

static void __main_ui_bridge_send_text_data(const char *prefix,
                                            const char *data, size_t len)
{
    char escaped[330];
    char payload[384];

    if (!data || len == 0) {
        return;
    }

    __json_escape_copy(escaped, sizeof(escaped), data, len);
    snprintf(payload, sizeof(payload),
             "{\"runtime\":\"tuya\",\"text\":\"%s%s\"}",
             prefix ? prefix : "", escaped);
    __main_ui_bridge_send(payload);
}

static void __main_ui_bridge_send_text(const char *prefix, AI_NOTIFY_TEXT_T *text)
{
    if (!text || !text->data || text->datalen == 0) {
        return;
    }

    __main_ui_bridge_send_text_data(prefix, text->data, text->datalen);
}

static BOOL_T __main_ui_text_valid(AI_NOTIFY_TEXT_T *text)
{
    return (text && text->data && text->datalen > 0) ? TRUE : FALSE;
}

static void __main_ui_reply_reset(void)
{
    sg_main_ui_reply_len = 0;
    sg_main_ui_reply_text[0] = '\0';
}

static void __main_ui_reply_append(AI_NOTIFY_TEXT_T *text)
{
    size_t copy_len;

    if (!text || !text->data || text->datalen == 0) {
        return;
    }

    if (sg_main_ui_reply_len >= sizeof(sg_main_ui_reply_text) - 1) {
        return;
    }

    copy_len = text->datalen;
    if (copy_len > sizeof(sg_main_ui_reply_text) - 1 - sg_main_ui_reply_len) {
        copy_len = sizeof(sg_main_ui_reply_text) - 1 - sg_main_ui_reply_len;
    }

    memcpy(&sg_main_ui_reply_text[sg_main_ui_reply_len], text->data, copy_len);
    sg_main_ui_reply_len += copy_len;
    sg_main_ui_reply_text[sg_main_ui_reply_len] = '\0';
}

static void __main_ui_reply_publish(void)
{
    if (sg_main_ui_reply_len == 0) {
        return;
    }

    __main_ui_bridge_send_text_data("Assistant: ",
                                    sg_main_ui_reply_text,
                                    sg_main_ui_reply_len);
}

static void __tts_stream_watchdog_cb(TIMER_ID timer_id, void *arg)
{
    OPERATE_RET rt;

    (void)timer_id;
    (void)arg;

    if (FALSE == sg_tts_stream_active) {
        return;
    }

    sg_tts_stream_active = FALSE;
    PR_ERR("TTS stream stalled for %d ms; recovering foreground player",
           TTS_STREAM_WATCHDOG_MS);

    rt = ai_audio_player_stop(AI_AUDIO_PLAYER_FG);
    if (rt != OPRT_OK) {
        /* The backend supervises this process and will restart it.  Exiting is
         * safer than leaving Free Chat permanently wedged in SPEAK state. */
        PR_ERR("TTS player recovery failed: %d; restarting runtime", rt);
        _exit(124);
    }

    ai_user_event_notify(AI_USER_EVT_TTS_ERROR, NULL);
}

static void __tts_stream_watchdog_start(void)
{
    sg_tts_stream_active = TRUE;
    if (sg_tts_watchdog_tm) {
        tal_sw_timer_start(sg_tts_watchdog_tm,
                           TTS_STREAM_WATCHDOG_MS,
                           TAL_TIMER_ONCE);
    }
}

static void __tts_stream_watchdog_stop(void)
{
    sg_tts_stream_active = FALSE;
    if (sg_tts_watchdog_tm) {
        tal_sw_timer_stop(sg_tts_watchdog_tm);
    }
}

static void __printf_free_heap_tm_cb(TIMER_ID timer_id, void *arg)
{
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    uint32_t free_heap       = tal_system_get_free_heap_size();
    uint32_t free_psram_heap = tal_psram_get_free_heap_size();
    PR_INFO("Free heap size:%d, Free psram heap size:%d", free_heap, free_psram_heap);
#else
    uint32_t free_heap = tal_system_get_free_heap_size();
    PR_INFO("Free heap size:%d", free_heap);
#endif
}

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
static void __display_net_status_update(void)
{
    AI_UI_WIFI_STATUS_E wifi_status = AI_UI_WIFI_STATUS_DISCONNECTED;
    netmgr_status_e     net_status  = NETMGR_LINK_DOWN;

    netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &net_status);
    if (net_status == NETMGR_LINK_UP) {
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
        // get rssi
        int8_t rssi = 0;
#ifndef PLATFORM_T5
        // BUG: Getting RSSI causes a crash on T5 platform
        tkl_wifi_station_get_conn_ap_rssi(&rssi);
#endif
        if (rssi >= -60) {
            wifi_status = AI_UI_WIFI_STATUS_GOOD;
        } else if (rssi >= -70) {
            wifi_status = AI_UI_WIFI_STATUS_FAIR;
        } else {
            wifi_status = AI_UI_WIFI_STATUS_WEAK;
        }
#else
        wifi_status = AI_UI_WIFI_STATUS_GOOD;
#endif
    } else {
        wifi_status = AI_UI_WIFI_STATUS_DISCONNECTED;
    }

    if (wifi_status != sg_wifi_status) {
        sg_wifi_status = wifi_status;
        ai_ui_disp_msg(AI_UI_DISP_NETWORK, (uint8_t *)&wifi_status, sizeof(AI_UI_WIFI_STATUS_E));
    }
}

static void __display_status_tm_cb(TIMER_ID timer_id, void *arg)
{
    __display_net_status_update();
}

#endif

static void __ai_chat_handle_event(AI_NOTIFY_EVENT_T *event)
{
    AI_NOTIFY_TEXT_T *text = NULL;

    if (NULL == event) {
        return;
    }

    switch(event->type) {
        case AI_USER_EVT_MODE_STATE_UPDATE: {
            AI_MODE_STATE_E state = (AI_MODE_STATE_E)(event->data);
            __main_ui_bridge_send_state(__main_ui_state_from_mode(state));
        } break;
        case AI_USER_EVT_ASR_OK: {
            text = (AI_NOTIFY_TEXT_T *)event->data;
            if (!__main_ui_text_valid(text)) {
                PR_DEBUG("main ui ignore empty ASR");
                break;
            }
            __main_ui_bridge_send_text("You: ", text);
            __main_ui_bridge_send_state("thinking");
        } break;
        case AI_USER_EVT_TEXT_STREAM_START: {
            text = (AI_NOTIFY_TEXT_T *)event->data;
            __main_ui_reply_reset();
            __main_ui_reply_append(text);
            __main_ui_reply_publish();
        } break;
        case AI_USER_EVT_TEXT_STREAM_DATA: {
            text = (AI_NOTIFY_TEXT_T *)event->data;
            __main_ui_reply_append(text);
            __main_ui_reply_publish();
        } break;
        case AI_USER_EVT_TEXT_STREAM_STOP: {
            text = (AI_NOTIFY_TEXT_T *)event->data;
            __main_ui_reply_append(text);
            __main_ui_reply_publish();
        } break;
        case AI_USER_EVT_TEXT_STREAM_ABORT:
        case AI_USER_EVT_CHAT_BREAK: {
            __main_ui_reply_reset();
            __tts_stream_watchdog_stop();
        } break;
        case AI_USER_EVT_TTS_PRE:
        case AI_USER_EVT_TTS_START: {
            __tts_stream_watchdog_start();
            __main_ui_bridge_send_state("speaking");
        } break;
        case AI_USER_EVT_TTS_DATA: {
            __tts_stream_watchdog_start();
        } break;
        case AI_USER_EVT_TTS_STOP: {
            /* TTS_STOP only means that the cloud finished delivering the
             * compressed stream.  The player may still have several seconds
             * buffered, and this is exactly where a decoder/ALSA stall used
             * to leave Free Chat stuck in SPEAK forever.  Keep the watchdog
             * armed until the foreground player reports PLAY_END. */
            __tts_stream_watchdog_start();
        } break;
        case AI_USER_EVT_TTS_ABORT:
        case AI_USER_EVT_TTS_ERROR: {
            __tts_stream_watchdog_stop();
            __main_ui_bridge_send_state("idle");
        } break;
        case AI_USER_EVT_MODE_SWITCH: {
            AI_CHAT_MODE_E mode = (AI_CHAT_MODE_E)(uintptr_t)(event->data);
            __main_ui_bridge_send_mode(mode);
        } break;
        case AI_USER_EVT_EMOTION:
        case AI_USER_EVT_LLM_EMOTION: {
            AI_NOTIFY_EMO_T *emo = (AI_NOTIFY_EMO_T *)event->data;
            if (emo && emo->name) {
                PR_NOTICE("main ui emotion: %s", emo->name);
                __main_ui_bridge_send_emotion(emo->name);
            }
        } break;
        case AI_USER_EVT_PLAY_END:
        case AI_USER_EVT_PLAY_CTL_END:
        {
            if (sg_tts_stream_active) {
                __tts_stream_watchdog_stop();
            }
        } break;
        #if defined(ENABLE_PRINTER) && (ENABLE_PRINTER == 1)
        case AI_USER_EVT_GENERATE_PICTURE:
        case AI_USER_EVT_GET_PICTURE_FROM_APP: {
            #if defined(ENABLE_COMP_AI_PICTURE) && (ENABLE_COMP_AI_PICTURE == 1)
            app_print_img_from_album((const char *)event->data);
            #endif
        } break;
        #endif
        default:
        break;
    }

}

OPERATE_RET app_chat_bot_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    AI_CHAT_MODE_CFG_T ai_chat_cfg = {
        .default_mode = AI_CHAT_MODE_WAKEUP,
        .default_vol  = 50,
        .evt_cb       = __ai_chat_handle_event,
    };
    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__tts_stream_watchdog_cb,
                                              NULL,
                                              &sg_tts_watchdog_tm));
    TUYA_CALL_ERR_RETURN(ai_chat_init(&ai_chat_cfg));

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    app_ui_action_register();
#endif

#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
    TUYA_CALL_ERR_LOG(ai_video_init());
#endif

#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
    TUYA_CALL_ERR_RETURN(ai_mcp_init());
#endif

#if defined(ENABLE_COMP_AI_PICTURE) && (ENABLE_COMP_AI_PICTURE == 1)
    TUYA_CALL_ERR_RETURN(ai_picture_init());
#endif

    // Free heap size
    tal_sw_timer_create(__printf_free_heap_tm_cb, NULL, &sg_printf_heap_tm);
    tal_sw_timer_start(sg_printf_heap_tm, PRINTF_FREE_HEAP_TTIME, TAL_TIMER_CYCLE);

    #if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    ai_ui_disp_msg(AI_UI_DISP_NETWORK, (uint8_t *)&sg_wifi_status, sizeof(AI_UI_WIFI_STATUS_E));

    ai_ui_disp_msg(AI_UI_DISP_STATUS, (uint8_t *)INITIALIZING, strlen(INITIALIZING));
    ai_ui_disp_msg(AI_UI_DISP_EMOTION, (uint8_t *)EMOJI_NEUTRAL, strlen(EMOJI_NEUTRAL));

    // display status update
    tal_sw_timer_create(__display_status_tm_cb, NULL, &sg_disp_status_tm);
    tal_sw_timer_start(sg_disp_status_tm, DISP_NET_STATUS_TIME, TAL_TIMER_CYCLE);
#endif

    __main_ui_bridge_send_state("connecting");
    __main_ui_cmd_start();

    return OPRT_OK;
}
