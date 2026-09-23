#include "service_sensor.h"

#include "../middleware/middleware.h"
#include "backend_types.h"
#include "log/app_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define SENSOR_UPDATE_INTERVAL_MS 3000

static struct {
    bool initialized;
    bool found;
    bool missing_logged;
    char temp_path[96];
    char humi_path[96];
    bool ina_found;
    bool ina_missing_logged;
    char ina_bus_path[96];
    char ina_curr_path[96];
    char ina_power_path[96];
    int64_t last_update_ms;
    sensor_status_t last_status;
    void (*listener)(const sensor_status_t *st);
} g_sensor = {0};

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int read_int_from_file(const char *path, int *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (fscanf(fp, "%d", out) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static bool detect_aht20_paths(void)
{
    char name_path[96];
    char name_buf[64];

    for (int i = 0; i < 10; i++) {
        FILE *fp;
        snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/hwmon%d/name", i);
        fp = fopen(name_path, "r");
        if (!fp) continue;

        memset(name_buf, 0, sizeof(name_buf));
        if (fgets(name_buf, sizeof(name_buf), fp) && strstr(name_buf, "aht20")) {
            fclose(fp);
            snprintf(g_sensor.temp_path, sizeof(g_sensor.temp_path), "/sys/class/hwmon/hwmon%d/temp1_input", i);
            snprintf(g_sensor.humi_path, sizeof(g_sensor.humi_path), "/sys/class/hwmon/hwmon%d/humidity1_input", i);
            APP_LOGI("sensor-service", "AHT20 detected at hwmon%d", i);
            g_sensor.missing_logged = false;
            return true;
        }
        fclose(fp);
    }

    if (!g_sensor.missing_logged) {
        APP_LOGW("sensor-service", "AHT20 hwmon not found");
        g_sensor.missing_logged = true;
    }
    return false;
}

/* 探测 INA219 整机电流监测芯片（ti,ina219），填 in1/curr1/power1 sysfs 路径。
 * 与 AHT20 是不同 hwmon 实例，独立探测，互不影响。 */
static bool detect_ina219_paths(void)
{
    char name_path[96];
    char name_buf[64];

    for (int i = 0; i < 10; i++) {
        FILE *fp;
        snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/hwmon%d/name", i);
        fp = fopen(name_path, "r");
        if (!fp) continue;

        memset(name_buf, 0, sizeof(name_buf));
        if (fgets(name_buf, sizeof(name_buf), fp) && strstr(name_buf, "ina219")) {
            fclose(fp);
            snprintf(g_sensor.ina_bus_path, sizeof(g_sensor.ina_bus_path),
                     "/sys/class/hwmon/hwmon%d/in1_input", i);
            snprintf(g_sensor.ina_curr_path, sizeof(g_sensor.ina_curr_path),
                     "/sys/class/hwmon/hwmon%d/curr1_input", i);
            snprintf(g_sensor.ina_power_path, sizeof(g_sensor.ina_power_path),
                     "/sys/class/hwmon/hwmon%d/power1_input", i);
            APP_LOGI("sensor-service", "INA219 detected at hwmon%d", i);
            g_sensor.ina_missing_logged = false;
            return true;
        }
        fclose(fp);
    }

    if (!g_sensor.ina_missing_logged) {
        APP_LOGW("sensor-service", "INA219 hwmon not found");
        g_sensor.ina_missing_logged = true;
    }
    return false;
}

static void publish_sensor_status(const sensor_status_t *status)
{
    mw_publish(TOPIC_SENSOR_STATUS, status, sizeof(*status), MW_DIR_BACKEND_TO_UI);
}

void service_sensor_set_listener(void (*cb)(const sensor_status_t *st))
{
    g_sensor.listener = cb;
}

void service_sensor_init(void)
{
    memset(&g_sensor, 0, sizeof(g_sensor));
    g_sensor.initialized = true;
    g_sensor.found = detect_aht20_paths();
    g_sensor.ina_found = detect_ina219_paths();
    g_sensor.last_update_ms = 0;
    g_sensor.last_status.valid = false;
    g_sensor.last_status.err_code = g_sensor.found ? -2 : -1;
    publish_sensor_status(&g_sensor.last_status);
}

void service_sensor_update(void)
{
    sensor_status_t status;
    int temp = 0;
    int humi = 0;
    int64_t now;

    if (!g_sensor.initialized) return;

    now = monotonic_ms();
    if ((now - g_sensor.last_update_ms) < SENSOR_UPDATE_INTERVAL_MS) return;
    g_sensor.last_update_ms = now;

    if (!g_sensor.found) {
        g_sensor.found = detect_aht20_paths();
        if (!g_sensor.found) {
            status = g_sensor.last_status;
            status.valid = false;
            status.err_code = -1;
            g_sensor.last_status = status;
            publish_sensor_status(&status);
            return;
        }
    }

    memset(&status, 0, sizeof(status));
    if (read_int_from_file(g_sensor.temp_path, &temp) == 0 &&
        read_int_from_file(g_sensor.humi_path, &humi) == 0) {
        status.valid = true;
        status.temp_mC = temp;
        status.humi_mpermil = humi;
        status.err_code = 0;
    } else {
        status.valid = false;
        status.err_code = -2;
    }

    /* INA219 整机电流监测：独立于温湿度采集，缺失/失败不影响温湿度上报。
     * 仅本机 UI 顶栏显示，不上云（云监听器只读温湿度字段）。 */
    if (!g_sensor.ina_found) {
        g_sensor.ina_found = detect_ina219_paths();
    }
    if (g_sensor.ina_found) {
        int bus = 0, curr = 0, power = 0;
        if (read_int_from_file(g_sensor.ina_bus_path, &bus) == 0 &&
            read_int_from_file(g_sensor.ina_curr_path, &curr) == 0 &&
            read_int_from_file(g_sensor.ina_power_path, &power) == 0) {
            status.ina_valid = true;
            status.bus_mV = bus;
            status.current_mA = curr;
            status.power_uW = power;
        } else {
            status.ina_valid = false;
        }
    } else {
        status.ina_valid = false;
    }

    g_sensor.last_status = status;
    publish_sensor_status(&status);
    /* 通知进程内监听器（云上报）。只在读到有效数据时通知，无效不报。 */
    if (status.valid && g_sensor.listener) {
        g_sensor.listener(&status);
    }
}

void service_sensor_deinit(void)
{
    memset(&g_sensor, 0, sizeof(g_sensor));
}
