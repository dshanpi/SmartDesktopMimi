#ifndef SERVICE_CLOUD_H
#define SERVICE_CLOUD_H

#include <stdbool.h>

/* 可选 Cloud provider 服务。公开构建使用无网络 stub，内部构建可显式启用
 * 100ask provider：MQTT 连云 + 接收云端 OTA 推送。
 * - 通用固件读取 CPUID + 出厂助手写入的 device_sig，首次联网 provision 获取 secret。
 * - 云端推 OTA → on_ota 静默下载（复用 service_ota 的 OTA_CMD_DOWNLOAD）→
 *   下载完成经 TOPIC_CLOUD_STATUS(ota_pending) 通知 UI 弹模态框提示安装。
 * - 连接状态经 TOPIC_CLOUD_STATUS 上报 UI（顶栏云图标）。 */

void service_cloud_init(void);
void service_cloud_update(void);   /* 10ms 主循环调：iot_loop + 重连 */
void service_cloud_deinit(void);

/* UI 侧清本次推送的待安装标记（用户点稍后/已安装后调）。 */
void service_cloud_clear_ota_pending(void);

/* 重发一帧当前云状态快照（响应 GET_STATUS）。 */
void service_cloud_send_status(void);

/* 用户准备绑定时按需刷新短期 bindToken；不会返回或暴露 device_secret。 */
void service_cloud_refresh_bind_token(void);

/*
 * 量产验收状态：由 AI 服务提供涂鸦 License 是否已被运行时真实接受。
 * 状态只通过设备已认证的 100ask MQTT 连接上报，不包含 UUID/AuthKey 明文。
 */
void service_cloud_set_tuya_license_ready(bool ready);

#endif /* SERVICE_CLOUD_H */
