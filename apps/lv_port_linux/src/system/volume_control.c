#include "volume_control.h"

#include "settings.h"
#include "log/app_log.h"

#include <alsa/asoundlib.h>
#include <stdbool.h>

#define ALSA_CTL_DEVICE "hw:0"
#define ALSA_CTL_NUMID 10U
#define ALSA_VOLUME_MIN 0L
#define ALSA_VOLUME_MAX 7L

static bool g_volume_open_warned = false;

static int clamp_percent(int percent)
{
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return percent;
}

static long percent_to_raw(int percent)
{
    percent = clamp_percent(percent);
    return (long)((percent * ALSA_VOLUME_MAX + 50) / 100);
}

static int raw_to_percent(long raw)
{
    if (raw < ALSA_VOLUME_MIN) raw = ALSA_VOLUME_MIN;
    if (raw > ALSA_VOLUME_MAX) raw = ALSA_VOLUME_MAX;
    return (int)((raw * 100 + (ALSA_VOLUME_MAX / 2)) / ALSA_VOLUME_MAX);
}

static int open_ctl(snd_ctl_t **ctl_out)
{
    int rc;

    if (!ctl_out) return -1;

    rc = snd_ctl_open(ctl_out, ALSA_CTL_DEVICE, 0);
    if (rc < 0) {
        if (!g_volume_open_warned) {
            APP_LOGW("volume", "cannot open ALSA ctl %s: %s",
                     ALSA_CTL_DEVICE, snd_strerror(rc));
            g_volume_open_warned = true;
        }
        return -1;
    }

    return 0;
}

int sys_volume_set_percent(int percent)
{
    snd_ctl_t *ctl = NULL;
    snd_ctl_elem_value_t *control = NULL;
    int rc;
    long raw;

    if (open_ctl(&ctl) != 0) return -1;

    raw = percent_to_raw(percent);
    snd_ctl_elem_value_alloca(&control);
    snd_ctl_elem_value_set_numid(control, ALSA_CTL_NUMID);
    snd_ctl_elem_value_set_integer(control, 0, raw);

    rc = snd_ctl_elem_write(ctl, control);
    snd_ctl_close(ctl);

    if (rc < 0) {
        APP_LOGE("volume", "ALSA write numid=%u failed: %s",
                 ALSA_CTL_NUMID, snd_strerror(rc));
        return -1;
    }

    return 0;
}

int sys_volume_get_percent(int *percent_out)
{
    snd_ctl_t *ctl = NULL;
    snd_ctl_elem_value_t *control = NULL;
    int rc;
    long raw;

    if (!percent_out) return -1;
    if (open_ctl(&ctl) != 0) return -1;

    snd_ctl_elem_value_alloca(&control);
    snd_ctl_elem_value_set_numid(control, ALSA_CTL_NUMID);

    rc = snd_ctl_elem_read(ctl, control);
    snd_ctl_close(ctl);
    if (rc < 0) {
        APP_LOGE("volume", "ALSA read numid=%u failed: %s",
                 ALSA_CTL_NUMID, snd_strerror(rc));
        return -1;
    }

    raw = snd_ctl_elem_value_get_integer(control, 0);
    *percent_out = raw_to_percent(raw);
    return 0;
}

void sys_volume_apply_saved_setting(void)
{
    sys_settings_t *settings = sys_settings_get();

    if (sys_volume_set_percent(settings->volume) == 0) {
        APP_LOGI("volume", "applied saved volume: %d%%", settings->volume);
    }
}
