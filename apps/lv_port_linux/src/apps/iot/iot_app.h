#ifndef MIMI_IOT_APP_H
#define MIMI_IOT_APP_H
#include "../app.h"
#include <stdint.h>
extern AppDescriptor app_iot;
/* LVGL thread only; passive aggregate, no requests, secrets or device IDs. */
typedef enum {
    MIMI_IOT_SUMMARY_UNKNOWN, MIMI_IOT_SUMMARY_READY, MIMI_IOT_SUMMARY_STALE,
    MIMI_IOT_SUMMARY_NOT_CONFIGURED, MIMI_IOT_SUMMARY_UNAUTHORIZED, MIMI_IOT_SUMMARY_UNAVAILABLE
} mimi_iot_summary_state;
typedef struct {
    mimi_iot_summary_state state;
    unsigned total, online;
    uint32_t received_at;
} mimi_iot_summary;
mimi_iot_summary mimi_iot_summary_get(void);
#endif
