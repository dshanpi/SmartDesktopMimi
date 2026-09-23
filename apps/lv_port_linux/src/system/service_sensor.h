#ifndef SERVICE_SENSOR_H
#define SERVICE_SENSOR_H

#include "backend_types.h"

void service_sensor_init(void);
void service_sensor_update(void);
void service_sensor_deinit(void);

/* 注册进程内温湿度监听器：每次读到有效 AHT20 数据时回调（service_cloud 上云用）。
 * mw_publish(TOPIC_SENSOR_STATUS) 只走后端→UI IPC，不回环后端进程内，故单独通知监听器。 */
void service_sensor_set_listener(void (*cb)(const sensor_status_t *st));

#endif /* SERVICE_SENSOR_H */
