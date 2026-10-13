/**
 * @file app_chat_bot.h
 * @brief app_chat_bot module is used to
 * @version 0.1
 * @date 2025-03-25
 */

#ifndef __APP_CHAT_BOT_H__
#define __APP_CHAT_BOT_H__

#include "tuya_cloud_types.h"
#include "ai_chat_main.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/
OPERATE_RET app_chat_bot_init(void);
int app_chat_bot_main_ui_send_bind_url(const char *url);
int app_chat_bot_main_ui_send_bound(BOOL_T bound);
int app_chat_bot_main_ui_send_iot_dp(uint8_t dpid, const char *type, int value);
int app_chat_bot_main_ui_send_iot_schema(uint8_t supported, uint8_t required);
int app_chat_bot_main_ui_request_iot_state(void);
int app_chat_bot_main_ui_send_iot_reset(void);
OPERATE_RET app_chat_bot_set_assistant_enabled(BOOL_T enabled);
BOOL_T app_chat_bot_is_assistant_enabled(void);
OPERATE_RET app_chat_bot_set_free_chat_enabled(BOOL_T enabled);

/* Implemented by tuya_main.c. The local backend sends one sanitized state
 * snapshot; this function reports only DPs present in the cloud schema. */
OPERATE_RET tuya_device_iot_state_update(const char *json);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CHAT_BOT_H__ */
