#include "led_strip_hal.h"
#include "log/app_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#ifndef LED_STRIP_DEV_PATH
#define LED_STRIP_DEV_PATH "/dev/ws2812-leds"
#endif

static struct {
    pthread_mutex_t lock;
    int fd;
    int count;
    int brightness; /* 0–100 */
    led_rgb_t pixels[LED_STRIP_MAX_COUNT];
    uint8_t last_frame[LED_STRIP_MAX_COUNT * 3];
    int last_frame_len;
    bool last_frame_valid;
    bool open_warned;
} g_hal = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .fd = -1,
    .count = LED_STRIP_DEFAULT_COUNT,
    .brightness = 22,
    .open_warned = false,
};

static int clamp_pct(int p)
{
    if (p < 0) return 0;
    if (p > 100) return 100;
    return p;
}

static uint8_t scale_ch(uint8_t v, int pct)
{
    return (uint8_t)((unsigned)v * (unsigned)pct / 100u);
}

int led_strip_hal_open(int led_count)
{
    int fd;
    int count = led_count > 0 ? led_count : LED_STRIP_DEFAULT_COUNT;

    if (count > LED_STRIP_MAX_COUNT)
        count = LED_STRIP_MAX_COUNT;

    pthread_mutex_lock(&g_hal.lock);

    if (g_hal.fd >= 0) {
        g_hal.count = count;
        pthread_mutex_unlock(&g_hal.lock);
        return 0;
    }

    fd = open(LED_STRIP_DEV_PATH, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        if (!g_hal.open_warned) {
            APP_LOGW("led-hal", "open %s failed: %s", LED_STRIP_DEV_PATH,
                     strerror(errno));
            g_hal.open_warned = true;
        }
        pthread_mutex_unlock(&g_hal.lock);
        return -errno;
    }

    /* Exclusive use: second process (e.g. debug app) fails to flock. */
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        APP_LOGW("led-hal", "flock exclusive failed (strip in use?): %s",
                 strerror(errno));
        close(fd);
        pthread_mutex_unlock(&g_hal.lock);
        return -EAGAIN;
    }

    g_hal.fd = fd;
    g_hal.count = count;
    memset(g_hal.pixels, 0, sizeof(g_hal.pixels));
    g_hal.last_frame_valid = false;
    g_hal.last_frame_len = 0;
    g_hal.open_warned = false;
    APP_LOGI("led-hal", "opened %s count=%d brightness=%d%%",
             LED_STRIP_DEV_PATH, g_hal.count, g_hal.brightness);

    pthread_mutex_unlock(&g_hal.lock);
    return 0;
}

void led_strip_hal_close(void)
{
    pthread_mutex_lock(&g_hal.lock);
    if (g_hal.fd >= 0) {
        /* Best-effort blank before release */
        memset(g_hal.pixels, 0, sizeof(g_hal.pixels));
        {
            uint8_t frame[LED_STRIP_MAX_COUNT * 3];
            int n = g_hal.count * 3;
            memset(frame, 0, (size_t)n);
            (void)write(g_hal.fd, frame, (size_t)n);
        }
        flock(g_hal.fd, LOCK_UN);
        close(g_hal.fd);
        g_hal.fd = -1;
        g_hal.last_frame_valid = false;
        g_hal.last_frame_len = 0;
        APP_LOGI("led-hal", "closed");
    }
    pthread_mutex_unlock(&g_hal.lock);
}

bool led_strip_hal_is_open(void)
{
    bool open;
    pthread_mutex_lock(&g_hal.lock);
    open = (g_hal.fd >= 0);
    pthread_mutex_unlock(&g_hal.lock);
    return open;
}

int led_strip_hal_count(void)
{
    int c;
    pthread_mutex_lock(&g_hal.lock);
    c = g_hal.count;
    pthread_mutex_unlock(&g_hal.lock);
    return c;
}

void led_strip_hal_set_brightness(int percent)
{
    pthread_mutex_lock(&g_hal.lock);
    percent = clamp_pct(percent);
    if (g_hal.brightness != percent) {
        g_hal.brightness = percent;
        g_hal.last_frame_valid = false;
    }
    pthread_mutex_unlock(&g_hal.lock);
}

int led_strip_hal_get_brightness(void)
{
    int b;
    pthread_mutex_lock(&g_hal.lock);
    b = g_hal.brightness;
    pthread_mutex_unlock(&g_hal.lock);
    return b;
}

void led_strip_hal_clear(void)
{
    pthread_mutex_lock(&g_hal.lock);
    memset(g_hal.pixels, 0, sizeof(g_hal.pixels));
    pthread_mutex_unlock(&g_hal.lock);
}

void led_strip_hal_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    int i;
    pthread_mutex_lock(&g_hal.lock);
    for (i = 0; i < g_hal.count; i++) {
        g_hal.pixels[i].r = r;
        g_hal.pixels[i].g = g;
        g_hal.pixels[i].b = b;
    }
    pthread_mutex_unlock(&g_hal.lock);
}

void led_strip_hal_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b)
{
    pthread_mutex_lock(&g_hal.lock);
    if (index >= 0 && index < g_hal.count) {
        g_hal.pixels[index].r = r;
        g_hal.pixels[index].g = g;
        g_hal.pixels[index].b = b;
    }
    pthread_mutex_unlock(&g_hal.lock);
}

int led_strip_hal_flush(void)
{
    uint8_t frame[LED_STRIP_MAX_COUNT * 3];
    int i, n, pct, fd;
    ssize_t wr;

    pthread_mutex_lock(&g_hal.lock);
    if (g_hal.fd < 0) {
        pthread_mutex_unlock(&g_hal.lock);
        return -ENODEV;
    }

    pct = g_hal.brightness;
    n = g_hal.count;
    for (i = 0; i < n; i++) {
        frame[i * 3 + 0] = scale_ch(g_hal.pixels[i].g, pct);
        frame[i * 3 + 1] = scale_ch(g_hal.pixels[i].r, pct);
        frame[i * 3 + 2] = scale_ch(g_hal.pixels[i].b, pct);
    }

    /* WS2812 retains its last frame. Avoid retransmitting an identical frame
     * dozens of times per second; redundant SPI traffic made marginal pixels
     * look like random high-rate flicker. */
    if (g_hal.last_frame_valid && g_hal.last_frame_len == n * 3 &&
        memcmp(frame, g_hal.last_frame, (size_t)(n * 3)) == 0) {
        pthread_mutex_unlock(&g_hal.lock);
        return 0;
    }
    fd = g_hal.fd;
    /* write while holding lock so no concurrent close */
    wr = write(fd, frame, (size_t)(n * 3));
    if (wr == n * 3) {
        memcpy(g_hal.last_frame, frame, (size_t)(n * 3));
        g_hal.last_frame_len = n * 3;
        g_hal.last_frame_valid = true;
    }
    pthread_mutex_unlock(&g_hal.lock);

    if (wr < 0) {
        APP_LOGW("led-hal", "write failed: %s", strerror(errno));
        return -errno;
    }
    if (wr != n * 3) {
        APP_LOGW("led-hal", "short write %zd / %d", wr, n * 3);
        return -EIO;
    }
    return 0;
}
