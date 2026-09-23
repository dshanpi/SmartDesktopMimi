#ifndef SERVICE_AI_H
#define SERVICE_AI_H

#include "backend_types.h"

void service_ai_init(void);
void service_ai_deinit(void);
void service_ai_start_listen(void);
void service_ai_stop_listen(void);
void service_ai_enter_free_chat(void);
void service_ai_exit_free_chat(void);
void service_ai_send_status(void);
void service_ai_update(void);
bool service_ai_blocks_bt_playback(void);

/*
 * 量产验收使用：只有涂鸦运行时已用当前 License 走到“生成有效绑定二维码”
 * 或“MQTT 已连接”后才返回 true。单纯进程存活不代表凭据有效。
 */
bool service_ai_is_tuya_license_ready(void);

#endif /* SERVICE_AI_H */
