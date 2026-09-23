#include "backlight_control.h"

#include "settings.h"
#include "log/app_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DISP_LCD_SET_BRIGHTNESS 0x102
#define DISP_LCD_GET_BRIGHTNESS 0x103
#define DISP_DEVICE "/dev/disp"
#define DISP_SCREEN_ID 0U
#define DISP_BRIGHTNESS_MAX 255

static bool g_open_warned = false;

static int clamp_percent(int percent)
{
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return percent;
}

static int percent_to_raw(int percent)
{
    percent = clamp_percent(percent);
    return (percent * DISP_BRIGHTNESS_MAX + 50) / 100;
}

static int raw_to_percent(int raw)
{
    if (raw < 0) raw = 0;
    if (raw > DISP_BRIGHTNESS_MAX) raw = DISP_BRIGHTNESS_MAX;
    return (raw * 100 + (DISP_BRIGHTNESS_MAX / 2)) / DISP_BRIGHTNESS_MAX;
}

static int open_disp_device(void)
{
    int fd = open(DISP_DEVICE, O_RDWR);

    if (fd < 0 && !g_open_warned) {
        APP_LOGW("backlight", "cannot open %s: %s", DISP_DEVICE, strerror(errno));
        g_open_warned = true;
    }

    return fd;
}

static int disp_set_brightness_raw(int fd, unsigned int screen_id, unsigned int brightness)
{
    unsigned long param[4] = { 0UL, 0UL, 0UL, 0UL };

    param[0] = screen_id;
    param[1] = brightness;

    if (ioctl(fd, DISP_LCD_SET_BRIGHTNESS, param) < 0) {
        APP_LOGE("backlight", "ioctl SET_BRIGHTNESS(%u) failed: %s",
                 brightness, strerror(errno));
        return -1;
    }

    return 0;
}

static int disp_get_brightness_raw(int fd, unsigned int screen_id)
{
    unsigned long param[4] = { 0UL, 0UL, 0UL, 0UL };
    int ret;

    param[0] = screen_id;
    ret = ioctl(fd, DISP_LCD_GET_BRIGHTNESS, param);
    if (ret < 0) {
        APP_LOGE("backlight", "ioctl GET_BRIGHTNESS failed: %s", strerror(errno));
    }

    return ret;
}

int sys_backlight_set_percent(int percent)
{
    int fd;
    int rc;

    fd = open_disp_device();
    if (fd < 0) return -1;

    rc = disp_set_brightness_raw(fd, DISP_SCREEN_ID, (unsigned int)percent_to_raw(percent));
    close(fd);
    return rc;
}

int sys_backlight_get_percent(int *percent_out)
{
    int fd;
    int raw;

    if (!percent_out) return -1;

    fd = open_disp_device();
    if (fd < 0) return -1;

    raw = disp_get_brightness_raw(fd, DISP_SCREEN_ID);
    close(fd);
    if (raw < 0) return -1;

    *percent_out = raw_to_percent(raw);
    return 0;
}

void sys_backlight_apply_saved_setting(void)
{
    sys_settings_t *settings = sys_settings_get();

    if (sys_backlight_set_percent(settings->brightness) == 0) {
        APP_LOGI("backlight", "applied saved brightness: %d%%", settings->brightness);
    }
}
