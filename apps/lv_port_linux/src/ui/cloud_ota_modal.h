#ifndef CLOUD_OTA_MODAL_H
#define CLOUD_OTA_MODAL_H

#include "../../lvgl/lvgl.h"
#include <stdbool.h>

/* 云端 OTA 安装提示模态框（多态）。
 *
 * 两种态：
 *  - PROMPT     下载完成，问是否安装。mandatory=true 时无「稍后」按钮。
 *  - INSTALLING 安装中：spinner + 「请勿断电」警示，不关闭直到 reboot。
 *
 * 触发：cloud_ota_status_callback 检测到 ota_pending==true 且 OTA 下载完成
 *       （TOPIC_OTA_STATUS == DOWNLOAD_DONE）时调 cloud_ota_modal_show()。
 * 动作：立即安装 → OTA_CMD_APPLY + 切 INSTALLING 态（不关弹窗，给安装反馈）。
 *       稍后 → 关闭模态 + CLOUD_CMD_DISMISS_OTA（清 ota_pending + 写延后提醒）。
 * 防重复弹：已显示则不重复。 */

/* 显示安装模态框（PROMPT 态）。
 * version：云端推送的目标版本；mandatory：true 时隐藏「稍后」。已显示则忽略。 */
void cloud_ota_modal_show(const char *version, bool mandatory);

/* 切到 INSTALLING 态：清掉 PROMPT 内容，显示 spinner + 警示。
 * 点「立即安装」时立即调，给用户即时反馈（不等状态回调）。 */
void cloud_ota_modal_enter_installing(void);

/* 兼容旧调用；安装进度条已移除，空实现。 */
void cloud_ota_modal_set_install_progress(int32_t progress);

/* 隐藏模态框。 */
void cloud_ota_modal_hide(void);

/* 模态框当前是否已显示。 */
bool cloud_ota_modal_is_shown(void);

#endif
