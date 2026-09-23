/**
 * @file service_time.c
 * @brief RTC + SNTP time service.
 *
 * RTC is stored in UTC. Domestic products should set TZ=CST-8 for UI display.
 */

#include "service_time.h"
#include "log/app_log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TIME_MODULE              "time-service"
#define RTC_DEVICE_PATH          "/dev/rtc0"
#define NTP_SERVER_IPV4          "203.107.6.88"
#define NTP_PORT                 123
#define NTP_PACKET_SIZE          48
#define NTP_UNIX_EPOCH_DELTA     2208988800UL
#define TIME_SYNC_MIN_INTERVAL   300
#define TIME_SYNC_RETRY_INTERVAL 30
#define TIME_SYNC_NETWORK_DELAY  2
#define TIME_SYNC_TIMEOUT_SEC    5
#define TIME_RTC_SYNC_INTERVAL   30
#define TIME_RTC_SYNC_MIN_DELTA  2
#define TIME_CLOCK_STEP_MIN_DELTA 5

static pthread_mutex_t g_time_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_time_service_enabled = true;
static bool g_sync_in_progress;
static bool g_system_time_trusted;
static bool g_clock_baseline_valid;
static time_t g_last_sync_attempt;
static time_t g_last_rtc_sync_check;
static time_t g_last_realtime;
static time_t g_last_monotonic;

static bool env_disables_time_service(const char *value)
{
    if (!value || value[0] == '\0') return false;

    return strcmp(value, "0") == 0 ||
           strcmp(value, "false") == 0 ||
           strcmp(value, "FALSE") == 0 ||
           strcmp(value, "off") == 0 ||
           strcmp(value, "OFF") == 0 ||
           strcmp(value, "disabled") == 0 ||
           strcmp(value, "DISABLED") == 0;
}

static bool rtc_time_plausible(const struct rtc_time *tm)
{
    int year;

    if (!tm) return false;

    year = tm->tm_year + 1900;
    return year >= 2024 && year <= 2099 &&
           tm->tm_mon >= 0 && tm->tm_mon <= 11 &&
           tm->tm_mday >= 1 && tm->tm_mday <= 31 &&
           tm->tm_hour >= 0 && tm->tm_hour <= 23 &&
           tm->tm_min >= 0 && tm->tm_min <= 59 &&
           tm->tm_sec >= 0 && tm->tm_sec <= 60;
}

static bool unix_time_plausible(time_t epoch)
{
    struct tm utc_tm;
    int year;

    if (epoch <= 0) return false;
    if (!gmtime_r(&epoch, &utc_tm)) return false;

    year = utc_tm.tm_year + 1900;
    return year >= 2024 && year <= 2099;
}

static int rtc_time_to_epoch(const struct rtc_time *tm, time_t *epoch)
{
    struct tm utc_tm;
    time_t value;

    if (!tm || !epoch || !rtc_time_plausible(tm)) return -1;

    memset(&utc_tm, 0, sizeof(utc_tm));
    utc_tm.tm_year = tm->tm_year;
    utc_tm.tm_mon = tm->tm_mon;
    utc_tm.tm_mday = tm->tm_mday;
    utc_tm.tm_hour = tm->tm_hour;
    utc_tm.tm_min = tm->tm_min;
    utc_tm.tm_sec = tm->tm_sec;
    utc_tm.tm_isdst = -1;

    value = timegm(&utc_tm);
    if (value == (time_t)-1 || !unix_time_plausible(value)) {
        return -1;
    }

    *epoch = value;
    return 0;
}

static int rtc_time_from_epoch(time_t utc_epoch, struct rtc_time *tm)
{
    struct tm utc_tm;

    if (!tm || !unix_time_plausible(utc_epoch)) return -1;
    if (!gmtime_r(&utc_epoch, &utc_tm)) return -1;

    memset(tm, 0, sizeof(*tm));
    tm->tm_year = utc_tm.tm_year;
    tm->tm_mon = utc_tm.tm_mon;
    tm->tm_mday = utc_tm.tm_mday;
    tm->tm_hour = utc_tm.tm_hour;
    tm->tm_min = utc_tm.tm_min;
    tm->tm_sec = utc_tm.tm_sec;
    return 0;
}

static time_t monotonic_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (time_t)-1;
    }

    return ts.tv_sec;
}

static long time_abs_delta(time_t a, time_t b)
{
    long delta = (long)(a - b);

    return delta < 0 ? -delta : delta;
}

static void update_clock_baseline_locked(time_t now)
{
    time_t monotonic = monotonic_seconds();

    if (monotonic == (time_t)-1) {
        g_clock_baseline_valid = false;
        return;
    }

    g_last_realtime = now;
    g_last_monotonic = monotonic;
    g_clock_baseline_valid = true;
}

static bool detect_external_time_step_locked(time_t now)
{
    time_t monotonic;
    time_t expected;
    long delta;

    monotonic = monotonic_seconds();
    if (monotonic == (time_t)-1) {
        g_clock_baseline_valid = false;
        return false;
    }

    if (!g_clock_baseline_valid || monotonic < g_last_monotonic) {
        g_last_realtime = now;
        g_last_monotonic = monotonic;
        g_clock_baseline_valid = true;
        return false;
    }

    expected = g_last_realtime + (monotonic - g_last_monotonic);
    delta = time_abs_delta(now, expected);

    g_last_realtime = now;
    g_last_monotonic = monotonic;
    return delta >= TIME_CLOCK_STEP_MIN_DELTA;
}

static int read_rtc_time(struct rtc_time *tm)
{
    int fd;
    int ret;

    if (!tm) return -1;

    fd = open(RTC_DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        APP_LOGW(TIME_MODULE, "open %s failed: %s", RTC_DEVICE_PATH, strerror(errno));
        return -1;
    }

    ret = ioctl(fd, RTC_RD_TIME, tm);
    if (ret < 0) {
        APP_LOGW(TIME_MODULE, "RTC_RD_TIME failed: %s", strerror(errno));
    }

    close(fd);
    return ret;
}

static int write_rtc_time(const struct rtc_time *tm)
{
    int fd;
    int ret;

    if (!tm) return -1;

    fd = open(RTC_DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        APP_LOGW(TIME_MODULE, "open %s failed: %s", RTC_DEVICE_PATH, strerror(errno));
        return -1;
    }

    ret = ioctl(fd, RTC_SET_TIME, tm);
    if (ret < 0) {
        APP_LOGW(TIME_MODULE, "RTC_SET_TIME failed: %s", strerror(errno));
    }

    close(fd);
    return ret;
}

static int set_system_time_from_tm(const struct rtc_time *tm)
{
    struct timespec ts;
    time_t epoch;

    if (!tm) return -1;

    if (rtc_time_to_epoch(tm, &epoch) != 0) {
        APP_LOGW(TIME_MODULE, "failed to convert RTC time");
        return -1;
    }

    ts.tv_sec = epoch;
    ts.tv_nsec = 0;
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        APP_LOGW(TIME_MODULE, "clock_settime from RTC failed: %s", strerror(errno));
        return -1;
    }

    APP_LOGI(TIME_MODULE, "system time restored from RTC: %04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return 0;
}

static int set_system_time_epoch(time_t utc_epoch, const char *source)
{
    struct timespec ts;

    if (!unix_time_plausible(utc_epoch)) {
        APP_LOGW(TIME_MODULE, "reject implausible %s time: %ld",
                 source ? source : "external", (long)utc_epoch);
        return -1;
    }

    ts.tv_sec = utc_epoch;
    ts.tv_nsec = 0;

    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        APP_LOGW(TIME_MODULE, "clock_settime from %s failed: %s",
                 source ? source : "external", strerror(errno));
        return -1;
    }

    return 0;
}

static int write_rtc_from_epoch(time_t utc_epoch, const char *source)
{
    struct rtc_time rtc_tm;

    if (rtc_time_from_epoch(utc_epoch, &rtc_tm) != 0) {
        APP_LOGW(TIME_MODULE, "failed to convert %s time for RTC",
                 source ? source : "external");
        return -1;
    }

    if (write_rtc_time(&rtc_tm) != 0) {
        return -1;
    }

    APP_LOGI(TIME_MODULE, "RTC updated from %s as UTC: %04d-%02d-%02d %02d:%02d:%02d",
             source ? source : "external",
             rtc_tm.tm_year + 1900, rtc_tm.tm_mon + 1, rtc_tm.tm_mday,
             rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
    return 0;
}

static int sntp_query_utc(const char *server_ipv4, time_t *utc_epoch)
{
    int fd;
    uint8_t packet[NTP_PACKET_SIZE];
    struct sockaddr_in addr;
    struct timeval timeout;
    ssize_t nread;
    uint32_t seconds;

    if (!server_ipv4 || !utc_epoch) return -1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        APP_LOGW(TIME_MODULE, "socket failed: %s", strerror(errno));
        return -1;
    }

    timeout.tv_sec = TIME_SYNC_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NTP_PORT);
    if (inet_pton(AF_INET, server_ipv4, &addr.sin_addr) != 1) {
        APP_LOGW(TIME_MODULE, "invalid NTP IPv4 address: %s", server_ipv4);
        close(fd);
        return -1;
    }

    memset(packet, 0, sizeof(packet));
    packet[0] = 0x1b;

    if (sendto(fd, packet, sizeof(packet), 0,
               (const struct sockaddr *)&addr, sizeof(addr)) != (ssize_t)sizeof(packet)) {
        APP_LOGW(TIME_MODULE, "sendto NTP failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    nread = recvfrom(fd, packet, sizeof(packet), 0, NULL, NULL);
    close(fd);

    if (nread < NTP_PACKET_SIZE) {
        APP_LOGW(TIME_MODULE, "NTP response invalid: nread=%ld error=%s",
                 (long)nread, nread < 0 ? strerror(errno) : "short packet");
        return -1;
    }

    seconds = ((uint32_t)packet[40] << 24) |
              ((uint32_t)packet[41] << 16) |
              ((uint32_t)packet[42] << 8) |
              (uint32_t)packet[43];

    if (seconds < NTP_UNIX_EPOCH_DELTA) {
        APP_LOGW(TIME_MODULE, "NTP timestamp before Unix epoch");
        return -1;
    }

    *utc_epoch = (time_t)(seconds - NTP_UNIX_EPOCH_DELTA);
    return 0;
}

static int apply_network_time(time_t utc_epoch)
{
    return service_time_set_utc_epoch(utc_epoch, "NTP");
}

static void *sync_thread_main(void *arg)
{
    time_t utc_epoch = 0;
    bool success = false;
    (void)arg;

    sleep(TIME_SYNC_NETWORK_DELAY);

    if (sntp_query_utc(NTP_SERVER_IPV4, &utc_epoch) == 0 &&
        apply_network_time(utc_epoch) == 0) {
        success = true;
    }

    pthread_mutex_lock(&g_time_lock);
    g_sync_in_progress = false;
    if (success) {
        g_last_sync_attempt = time(NULL);
    } else {
        g_last_sync_attempt = time(NULL) - (TIME_SYNC_MIN_INTERVAL - TIME_SYNC_RETRY_INTERVAL);
    }
    pthread_mutex_unlock(&g_time_lock);

    if (!success) {
        APP_LOGW(TIME_MODULE, "network time sync failed");
    }
    return NULL;
}

void service_time_init(void)
{
    struct rtc_time tm;

    g_time_service_enabled = !env_disables_time_service(getenv("LV_BACKEND_TIME_SERVICE"));
    if (!g_time_service_enabled) {
        APP_LOGI(TIME_MODULE, "disabled by LV_BACKEND_TIME_SERVICE");
        return;
    }

    APP_LOGI(TIME_MODULE, "initializing");

    if (read_rtc_time(&tm) == 0 && rtc_time_plausible(&tm)) {
        if (set_system_time_from_tm(&tm) == 0) {
            g_system_time_trusted = true;
        }
    } else {
        APP_LOGW(TIME_MODULE, "RTC time is not plausible, waiting for network sync");
    }

    pthread_mutex_lock(&g_time_lock);
    g_last_rtc_sync_check = time(NULL);
    update_clock_baseline_locked(g_last_rtc_sync_check);
    pthread_mutex_unlock(&g_time_lock);
}

void service_time_deinit(void)
{
    if (!g_time_service_enabled) return;
    APP_LOGI(TIME_MODULE, "deinitialized");
}

int service_time_set_utc_epoch(time_t utc_epoch, const char *source)
{
    int ret = -1;

    if (!g_time_service_enabled) return -1;
    if (!unix_time_plausible(utc_epoch)) {
        APP_LOGW(TIME_MODULE, "reject implausible %s time: %ld",
                 source ? source : "external", (long)utc_epoch);
        return -1;
    }

    pthread_mutex_lock(&g_time_lock);
    if (set_system_time_epoch(utc_epoch, source) == 0) {
        ret = write_rtc_from_epoch(utc_epoch, source);
        g_system_time_trusted = true;
        g_last_rtc_sync_check = time(NULL);
        update_clock_baseline_locked(g_last_rtc_sync_check);
    }
    pthread_mutex_unlock(&g_time_lock);

    return ret;
}

void service_time_update(void)
{
    struct rtc_time rtc_tm;
    time_t now;
    time_t rtc_epoch = 0;
    long delta;
    bool should_check = false;
    bool should_write = false;
    bool external_step = false;

    if (!g_time_service_enabled) return;

    now = time(NULL);

    pthread_mutex_lock(&g_time_lock);
    if (!g_sync_in_progress &&
        (g_last_rtc_sync_check == 0 ||
         now < g_last_rtc_sync_check ||
         (now - g_last_rtc_sync_check) >= TIME_RTC_SYNC_INTERVAL)) {
        g_last_rtc_sync_check = now;
        external_step = detect_external_time_step_locked(now);
        if (external_step) {
            g_system_time_trusted = true;
            APP_LOGI(TIME_MODULE, "external system time step detected, RTC persistence enabled");
        }
        should_check = g_system_time_trusted && unix_time_plausible(now);
    }
    pthread_mutex_unlock(&g_time_lock);

    if (!should_check) return;

    if (read_rtc_time(&rtc_tm) != 0 || rtc_time_to_epoch(&rtc_tm, &rtc_epoch) != 0) {
        APP_LOGW(TIME_MODULE, "RTC read invalid, mirroring system time to RTC");
        should_write = true;
    } else {
        delta = (long)(now - rtc_epoch);
        if (delta < 0) delta = -delta;
        should_write = delta >= TIME_RTC_SYNC_MIN_DELTA;
    }

    if (!should_write) return;

    pthread_mutex_lock(&g_time_lock);
    (void)write_rtc_from_epoch(now, "system clock");
    pthread_mutex_unlock(&g_time_lock);
}

void service_time_on_network_ready(void)
{
    pthread_t tid;
    time_t now = time(NULL);

    if (!g_time_service_enabled) return;

    pthread_mutex_lock(&g_time_lock);
    if (g_sync_in_progress) {
        pthread_mutex_unlock(&g_time_lock);
        return;
    }
    if (g_last_sync_attempt != 0 &&
        now >= g_last_sync_attempt &&
        (now - g_last_sync_attempt) < TIME_SYNC_MIN_INTERVAL) {
        pthread_mutex_unlock(&g_time_lock);
        return;
    }
    g_last_sync_attempt = now;
    g_sync_in_progress = true;
    pthread_mutex_unlock(&g_time_lock);

    if (pthread_create(&tid, NULL, sync_thread_main, NULL) == 0) {
        pthread_detach(tid);
        APP_LOGI(TIME_MODULE, "network time sync scheduled");
    } else {
        pthread_mutex_lock(&g_time_lock);
        g_sync_in_progress = false;
        pthread_mutex_unlock(&g_time_lock);
        APP_LOGW(TIME_MODULE, "failed to create sync thread");
    }
}
