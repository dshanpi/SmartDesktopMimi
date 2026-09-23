#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdarg.h>

typedef enum {
    APP_LOG_LEVEL_DEBUG = 0,
    APP_LOG_LEVEL_INFO,
    APP_LOG_LEVEL_WARN,
    APP_LOG_LEVEL_ERROR
} app_log_level_t;

void app_log_set_level(app_log_level_t level);
app_log_level_t app_log_get_level(void);
void app_log_write(app_log_level_t level, const char *module, const char *fmt, ...);

#define APP_LOGD(module, fmt, ...) app_log_write(APP_LOG_LEVEL_DEBUG, module, fmt, ##__VA_ARGS__)
#define APP_LOGI(module, fmt, ...) app_log_write(APP_LOG_LEVEL_INFO, module, fmt, ##__VA_ARGS__)
#define APP_LOGW(module, fmt, ...) app_log_write(APP_LOG_LEVEL_WARN, module, fmt, ##__VA_ARGS__)
#define APP_LOGE(module, fmt, ...) app_log_write(APP_LOG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)

#endif /* APP_LOG_H */
