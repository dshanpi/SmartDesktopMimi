#include "app_log.h"

#include <stdio.h>
#include <time.h>

static app_log_level_t g_log_level = APP_LOG_LEVEL_INFO;

static const char *level_to_text(app_log_level_t level)
{
    switch (level) {
        case APP_LOG_LEVEL_DEBUG: return "DEBUG";
        case APP_LOG_LEVEL_INFO: return "INFO";
        case APP_LOG_LEVEL_WARN: return "WARN";
        case APP_LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void app_log_set_level(app_log_level_t level)
{
    g_log_level = level;
}

app_log_level_t app_log_get_level(void)
{
    return g_log_level;
}

void app_log_write(app_log_level_t level, const char *module, const char *fmt, ...)
{
    va_list args;
    time_t now;
    struct tm tm_now;
    char ts[32];

    if (level < g_log_level) return;

    now = time(NULL);
    localtime_r(&now, &tm_now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(stderr, "[%s] [%s] [%s] ", ts, level_to_text(level), module ? module : "app");

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
