#ifndef SERVICE_WIFI_H
#define SERVICE_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "backend_types.h"

/**
 * @brief 初始化 Wi-Fi 服务 (包括任务队列、事件监听线程)
 */
void service_wifi_init(void);

/**
 * @brief 销毁 Wi-Fi 服务 (停止线程、关闭连接)
 */
void service_wifi_deinit(void);

/**
 * @brief 触发 Wi-Fi 扫描
 * @note 异步操作，结果通过 IPC 发送到 UI
 */
void service_wifi_scan(void);

/**
 * @brief 连接到指定 Wi-Fi
 * @param ssid SSID
 * @param password 密码
 */
void service_wifi_connect(const char * ssid, const char * password);

/**
 * @brief 断开当前 Wi-Fi 连接
 */
void service_wifi_disconnect(void);

/**
 * @brief 开启或关闭 Wi-Fi 射频与管理器
 * @param enabled true 开启，false 关闭
 */
void service_wifi_set_enabled(bool enabled);

/**
 * @brief 立即向 UI 发送一份 Wi-Fi 运行时状态快照
 */
void service_wifi_send_runtime_status(void);

/**
 * @brief 当前 Wi-Fi 是否已经连接并拿到可用 IP。
 *
 * AI、时间同步等依赖互联网的服务应使用这个状态，而不是只看 Wi-Fi
 * 关联状态；刚关联 AP 但 DHCP 还没拿到 IP 时仍返回 false。
 */
bool service_wifi_is_network_ready(void);

/**
 * @brief 轮询 IP 地址变化（在主循环中调用，自适应间隔）
 *
 * 连接后密集检查（500ms×6次），稳定后低频检查（10s），断开后无操作。
 */
void service_ip_monitor_poll(void);

/**
 * @brief 强制下一次轮询重新推送网络信息（用于 UI 重连场景）
 */
void service_ip_monitor_sync(void);

#endif /* SERVICE_WIFI_H */
