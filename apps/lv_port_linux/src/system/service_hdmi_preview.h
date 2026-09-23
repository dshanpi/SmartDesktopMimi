#ifndef SERVICE_HDMI_PREVIEW_H
#define SERVICE_HDMI_PREVIEW_H

#include <stdbool.h>

void service_hdmi_preview_init(void);
void service_hdmi_preview_deinit(void);

/* 打开/关闭 HDMI 持续检测。关闭即停止 monitor 线程与 hdmi_preview 子进程，
 * 不再探测/抓帧；打开则拉起 monitor。状态经 TOPIC_HDMI_PREVIEW_STATUS 上报。 */
void service_hdmi_preview_set_enabled(bool enable);

/* 重发一帧当前状态快照（响应 GET_STATUS）。 */
void service_hdmi_preview_send_status(void);

#endif /* SERVICE_HDMI_PREVIEW_H */
