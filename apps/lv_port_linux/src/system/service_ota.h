#ifndef SERVICE_OTA_H
#define SERVICE_OTA_H

#include "backend_types.h"

/* OTA 升级服务（lv_backend 内）。
 *
 * 由 backend_main 注册 TOPIC_OTA_COMMAND handler 调用 service_ota_handle_command()。
 * 状态经 TOPIC_OTA_STATUS 上报给 UI（lvglsim）。
 *
 * 流程：CHECK（比对版本）→ DOWNLOAD（libcurl 下载 .swu 到 UDISK）
 *       → APPLY（swupdate -i 写非活动槽 + 切 env + 重启）
 *       → 新槽启动后 COMMIT（health check 通过则清 upgrade_available）。
 * 失败回滚由 U-Boot bootcount + altbootcmd 处理（Phase 0 地基）。
 */

void service_ota_init(void);
void service_ota_update(void);
void service_ota_deinit(void);

/* 处理 UI 发来的 OTA 指令（在 backend_main 的 handler 里调用）。 */
void service_ota_handle_command(const ota_cmd_t *cmd);

/* 当前启动槽：读 /proc/cmdline 的 root=，返回 'A'/'B'/0(未知)。供云上报用。 */
char service_ota_get_boot_slot(void);

/* 本地升级包是否就绪：/mnt/UDISK/upgrade.swu 存在且 sha256 匹配 expect_sha256。
 * expect_sha256 为空时只校验文件存在。供云端重下载守卫 / 开机兜底判断用。 */
bool service_ota_is_package_ready(const char *expect_sha256);

/* 标记包已下载完成：把状态推到 DOWNLOAD_DONE 并 publish 给 UI。
 * 用于云端「包已就绪跳过下载」场景——没走下载流程也要让 UI 收到 DOWNLOAD_DONE
 * 才能弹安装提示。仅当包确实就绪时生效。 */
void service_ota_mark_package_downloaded(void);

/* 注册进程内 OTA 状态监听器：每次状态变化（publish_status）时回调，供云上报联动用。 */
void service_ota_set_status_listener(void (*cb)(const ota_status_t *st));

#endif /* SERVICE_OTA_H */
