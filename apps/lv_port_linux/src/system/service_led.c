#include "service_led.h"
#include "led_strip_hal.h"
#include "log/app_log.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 25 Hz is enough for the short transient animations and reduces SPI traffic. */
#define LED_RENDER_HZ          25
#define LED_RENDER_PERIOD_US   (1000000 / LED_RENDER_HZ)
/* Event-first UX: default OFF when idle; short, high-contrast feedback on events. */
#define LED_BOOT_TTL_MS        3500
#define LED_WIFI_OK_TTL_MS     1800
#define LED_WIFI_FAIL_TTL_MS   1800
#define LED_NOTIFY_TTL_MS      1200
#define LED_AI_ERROR_TTL_MS    2000
#define LED_UI_SWIPE_TTL_MS    380
#define LED_HAL_RETRY_MS       2000
#define LED_PI2                6.283185307179586f

typedef struct {
    bool active;
    led_scene_t scene;
    int param;
    int priority;
    int64_t expire_ms; /* 0 = no expiry */
} led_slot_t;

static struct {
    pthread_mutex_t lock;
    pthread_t thread;
    atomic_bool running;
    bool thread_started;
    bool enabled;
    int brightness;
    led_slot_t slots[LED_CLIENT_MAX];
    led_client_t last_winner;
    led_scene_t last_scene;
    int64_t anim_t0_ms;
    int64_t last_open_try_ms;
    bool hal_ok;
} g_led = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .running = false,
    .thread_started = false,
    .enabled = true,
    /* Global dim; scenes use strong colors so short events stay readable. */
    .brightness = 28,
    .last_winner = LED_CLIENT_SYSTEM,
    .last_scene = LED_SCENE_OFF,
    .anim_t0_ms = 0,
    .last_open_try_ms = 0,
    .hal_ok = false,
};

static int client_priority(led_client_t who, led_scene_t scene)
{
    /* Scene can boost SYSTEM error above ambient. */
    if (scene == LED_SCENE_ERROR)
        return 90;
    if (scene == LED_SCENE_OTA_PROGRESS || scene == LED_SCENE_OTA_APPLYING)
        return 100;

    switch (who) {
    case LED_CLIENT_OTA:      return 100;
    case LED_CLIENT_SYSTEM:   return (scene == LED_SCENE_BOOT) ? 50 : 10;
    case LED_CLIENT_AI:       return 80;
    case LED_CLIENT_NETWORK:  return 60;
    case LED_CLIENT_BT:       return 55;
    case LED_CLIENT_NOTIFY:   return 40;
    /* UI dock swipe: below AI/OTA, above idle network flashes if overlapping */
    case LED_CLIENT_SETTINGS: return 45;
    default:                  return 0;
    }
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *client_name(led_client_t c)
{
    static const char *names[] = {
        "SYSTEM", "NETWORK", "BT", "AI", "OTA", "NOTIFY", "SETTINGS"
    };
    if (c >= 0 && c < LED_CLIENT_MAX)
        return names[c];
    return "?";
}

static const char *scene_name(led_scene_t s)
{
    switch (s) {
    case LED_SCENE_OFF: return "OFF";
    case LED_SCENE_BOOT: return "BOOT";
    case LED_SCENE_IDLE: return "IDLE";
    case LED_SCENE_WIFI_CONNECTING: return "WIFI_CONNECTING";
    case LED_SCENE_WIFI_OK: return "WIFI_OK";
    case LED_SCENE_WIFI_FAIL: return "WIFI_FAIL";
    case LED_SCENE_AI_CONNECTING: return "AI_CONNECTING";
    case LED_SCENE_AI_LISTENING: return "AI_LISTENING";
    case LED_SCENE_AI_THINKING: return "AI_THINKING";
    case LED_SCENE_AI_SPEAKING: return "AI_SPEAKING";
    case LED_SCENE_AI_ERROR: return "AI_ERROR";
    case LED_SCENE_OTA_PROGRESS: return "OTA_PROGRESS";
    case LED_SCENE_OTA_APPLYING: return "OTA_APPLYING";
    case LED_SCENE_ERROR: return "ERROR";
    case LED_SCENE_NOTIFY: return "NOTIFY";
    case LED_SCENE_SWIPE_LEFT: return "SWIPE_LEFT";
    case LED_SCENE_SWIPE_RIGHT: return "SWIPE_RIGHT";
    default: return "?";
    }
}

static void expire_slots_locked(int64_t now)
{
    int i;
    for (i = 0; i < LED_CLIENT_MAX; i++) {
        if (!g_led.slots[i].active)
            continue;
        if (g_led.slots[i].expire_ms > 0 && now >= g_led.slots[i].expire_ms) {
            APP_LOGI("led", "ttl expire client=%s scene=%s",
                     client_name((led_client_t)i),
                     scene_name(g_led.slots[i].scene));
            g_led.slots[i].active = false;
        }
    }
}

/* Pick highest priority active slot. Tie-break: higher client enum wins if equal prio. */
static bool pick_winner_locked(led_client_t *out_who, led_slot_t *out_slot)
{
    int best_prio = -1;
    int best_i = -1;
    int i;

    for (i = 0; i < LED_CLIENT_MAX; i++) {
        if (!g_led.slots[i].active)
            continue;
        if (g_led.slots[i].priority > best_prio ||
            (g_led.slots[i].priority == best_prio && i > best_i)) {
            best_prio = g_led.slots[i].priority;
            best_i = i;
        }
    }

    if (best_i < 0)
        return false;

    *out_who = (led_client_t)best_i;
    *out_slot = g_led.slots[best_i];
    return true;
}

/* Smooth 0..1 envelope: raised-cosine (never a hard triangle edge). */
static float envelope_cos(int64_t t0, int64_t now, int period_ms, float phase01)
{
    float t;
    float e;

    if (period_ms < 400)
        period_ms = 400;
    t = (float)((now - t0) % period_ms) / (float)period_ms + phase01;
    t -= floorf(t);
    /* 0.5 - 0.5*cos → smooth in/out; ease again for softer valley */
    e = 0.5f - 0.5f * cosf(t * LED_PI2);
    e = e * e * (3.0f - 2.0f * e); /* smoothstep */
    return e;
}

/* Map envelope to [lo, hi] channel levels. Keep lo > 0 so strip never "blinks off". */
static uint8_t level_range(float env, uint8_t lo, uint8_t hi)
{
    float v;
    int iv;

    if (hi < lo) {
        uint8_t tmp = lo;
        lo = hi;
        hi = tmp;
    }
    v = (float)lo + ((float)hi - (float)lo) * env;
    iv = (int)(v + 0.5f);
    if (iv < 0) iv = 0;
    if (iv > 255) iv = 255;
    return (uint8_t)iv;
}

static void set_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_hal_set_all(r, g, b);
}

/*
 * Soft comet. dir > 0: head moves toward higher index (right if LED0=left);
 * dir < 0: toward lower index.
 */
static void set_comet(int64_t t0, int64_t now, int period_ms, int dir,
                      uint8_t r, uint8_t g, uint8_t b, int tail)
{
    int n = led_strip_hal_count();
    int i;
    float pos;
    int head;

    if (n <= 0)
        n = LED_STRIP_DEFAULT_COUNT;
    if (period_ms < 300)
        period_ms = 300;
    if (tail < 2)
        tail = 2;

    pos = (float)((now - t0) % period_ms) / (float)period_ms * (float)n;
    if (dir >= 0)
        head = (int)pos % n;
    else
        head = (n - 1 - (int)pos % n + n) % n;

    led_strip_hal_clear();
    for (i = 0; i < tail; i++) {
        int idx;
        float fall;

        if (dir >= 0)
            idx = (head - i + n * 8) % n;
        else
            idx = (head + i) % n;
        fall = 1.0f - (float)i / (float)tail;
        fall = fall * fall;
        led_strip_hal_set_pixel(idx,
                                (uint8_t)((float)r * fall),
                                (uint8_t)((float)g * fall),
                                (uint8_t)((float)b * fall));
    }
}

static void render_scene(led_scene_t scene, int param, int64_t t0, int64_t now)
{
    int n = led_strip_hal_count();
    int i;
    float e;

    if (n <= 0)
        n = LED_STRIP_DEFAULT_COUNT;

    switch (scene) {
    case LED_SCENE_OFF:
        led_strip_hal_clear();
        break;

    case LED_SCENE_BOOT: {
        /*
         * Startup signature (cool-white):
         *  0–1.2s  fill progress (physical left→right; LED0 is on the right
         *          of the bar, so we light high indices first)
         *  1.2–2.4s full on hold
         *  2.4–3.5s fade out → OFF
         */
        int64_t dt = now - t0;
        led_strip_hal_clear();
        if (dt < 1200) {
            int lit = (int)((dt * n) / 1200) + 1;
            if (lit > n) lit = n;
            /* index n-1 first … down to n-lit → appears L→R on device */
            for (i = 0; i < lit; i++)
                led_strip_hal_set_pixel(n - 1 - i, 200, 200, 220);
        } else if (dt < 2400) {
            set_all_rgb(200, 200, 220);
        } else {
            float fade = 1.0f - (float)(dt - 2400) / 1100.f;
            if (fade < 0.f) fade = 0.f;
            if (fade > 1.f) fade = 1.f;
            set_all_rgb((uint8_t)(200.f * fade),
                        (uint8_t)(200.f * fade),
                        (uint8_t)(220.f * fade));
        }
        break;
    }

    case LED_SCENE_IDLE:
        /* Reserved: product default is OFF when idle (no always-on glow). */
        led_strip_hal_clear();
        break;

    case LED_SCENE_WIFI_CONNECTING:
        /* Pure blue chase — “connecting”, readable at a glance */
        set_comet(t0, now, 900, 1, 0, 0, 255, 4);
        break;

    case LED_SCENE_SWIPE_LEFT:
        /* Cool white comet left — matches dock select toward lower index */
        set_comet(t0, now, 320, -1, 220, 230, 255, 4);
        break;

    case LED_SCENE_SWIPE_RIGHT:
        set_comet(t0, now, 320, 1, 220, 230, 255, 4);
        break;

    case LED_SCENE_WIFI_OK:
        /* Two solid green flashes then hold */
        {
            int64_t dt = now - t0;
            bool on = (dt < 250) || (dt >= 400 && dt < 650) || (dt >= 800);
            if (on)
                set_all_rgb(0, 220, 40);
            else
                led_strip_hal_clear();
        }
        break;

    case LED_SCENE_WIFI_FAIL:
        {
            int64_t dt = now - t0;
            bool on = ((dt / 200) % 2) == 0;
            if (on)
                set_all_rgb(255, 40, 0);
            else
                led_strip_hal_clear();
        }
        break;

    case LED_SCENE_AI_CONNECTING:
        /* Pure purple whole-strip breath (clear “AI starting”) */
        e = envelope_cos(t0, now, 1400, 0.f);
        set_all_rgb(level_range(e, 40, 180), 0, level_range(e, 80, 255));
        break;

    case LED_SCENE_AI_LISTENING:
        /* Stable colors avoid distracting/fault-amplifying continuous writes. */
        set_all_rgb(0, 150, 210);
        break;

    case LED_SCENE_AI_THINKING:
        set_all_rgb(125, 0, 190);
        break;

    case LED_SCENE_AI_SPEAKING:
        set_all_rgb(0, 180, 25);
        break;

    case LED_SCENE_AI_ERROR:
    case LED_SCENE_ERROR:
        /* Distinct red double-blink feel via envelope + floor off */
        e = envelope_cos(t0, now, 700, 0.f);
        if (e < 0.15f)
            led_strip_hal_clear();
        else
            set_all_rgb(level_range(e, 80, 255), 0, 0);
        break;

    case LED_SCENE_OTA_PROGRESS: {
        int lit;
        float frac;
        if (param < 0) param = 0;
        if (param > 100) param = 100;
        frac = (float)param / 100.f;
        lit = (int)(frac * (float)n + 0.001f);
        if (param > 0 && lit < 1)
            lit = 1;
        led_strip_hal_clear();
        /* Same physical mapping as BOOT: fill from visual left (high index). */
        for (i = 0; i < lit && i < n; i++)
            led_strip_hal_set_pixel(n - 1 - i, 0, 40, 255);
        break;
    }

    case LED_SCENE_OTA_APPLYING:
        /* Fast pure-blue blink — “writing, don’t power off” */
        {
            bool on = (((now - t0) / 250) % 2) == 0;
            if (on)
                set_all_rgb(0, 60, 255);
            else
                led_strip_hal_clear();
        }
        break;

    case LED_SCENE_NOTIFY:
        e = envelope_cos(t0, now, 500, 0.f);
        set_all_rgb(level_range(e, 0, 255),
                    level_range(e, 0, 200),
                    0);
        break;

    default:
        led_strip_hal_clear();
        break;
    }

    (void)led_strip_hal_flush();
}

static void ensure_hal_locked(int64_t now)
{
    if (g_led.hal_ok && led_strip_hal_is_open())
        return;
    if (now - g_led.last_open_try_ms < LED_HAL_RETRY_MS)
        return;
    g_led.last_open_try_ms = now;
    if (led_strip_hal_open(LED_STRIP_DEFAULT_COUNT) == 0) {
        led_strip_hal_set_brightness(g_led.brightness);
        g_led.hal_ok = true;
    } else {
        g_led.hal_ok = false;
    }
}

static void *led_render_thread(void *arg)
{
    (void)arg;

    while (atomic_load(&g_led.running)) {
        int64_t now = now_ms();
        int64_t anim_t0;
        led_client_t who = LED_CLIENT_SYSTEM;
        led_slot_t slot;
        bool have;
        led_scene_t scene = LED_SCENE_OFF;
        int param = 0;
        bool can_render;

        pthread_mutex_lock(&g_led.lock);
        expire_slots_locked(now);
        ensure_hal_locked(now);

        if (!g_led.enabled) {
            scene = LED_SCENE_OFF;
        } else {
            have = pick_winner_locked(&who, &slot);
            if (have) {
                scene = slot.scene;
                param = slot.param;
            } else {
                /* No active claim → fully off (not always-on ambient). */
                scene = LED_SCENE_OFF;
                who = LED_CLIENT_SYSTEM;
            }
        }

        if (who != g_led.last_winner || scene != g_led.last_scene) {
            APP_LOGI("led", "grant client=%s scene=%s param=%d",
                     client_name(who), scene_name(scene), param);
            g_led.last_winner = who;
            g_led.last_scene = scene;
            g_led.anim_t0_ms = now;
        }
        anim_t0 = g_led.anim_t0_ms;
        can_render = g_led.hal_ok;
        pthread_mutex_unlock(&g_led.lock);

        if (can_render || led_strip_hal_is_open()) {
            render_scene(scene, param, anim_t0, now);
        }

        usleep(LED_RENDER_PERIOD_US);
    }

    /* Final blank */
    if (led_strip_hal_is_open()) {
        led_strip_hal_clear();
        led_strip_hal_flush();
    }
    return NULL;
}

void service_led_request(led_client_t who, led_scene_t scene, int param, int ttl_ms)
{
    int64_t now;

    if (who < 0 || who >= LED_CLIENT_MAX)
        return;

    now = now_ms();
    pthread_mutex_lock(&g_led.lock);
    g_led.slots[who].active = true;
    g_led.slots[who].scene = scene;
    g_led.slots[who].param = param;
    g_led.slots[who].priority = client_priority(who, scene);
    g_led.slots[who].expire_ms = (ttl_ms > 0) ? (now + ttl_ms) : 0;
    APP_LOGD("led", "request client=%s scene=%s param=%d ttl=%d prio=%d",
             client_name(who), scene_name(scene), param, ttl_ms,
             g_led.slots[who].priority);
    pthread_mutex_unlock(&g_led.lock);
}

void service_led_release(led_client_t who)
{
    if (who < 0 || who >= LED_CLIENT_MAX)
        return;

    pthread_mutex_lock(&g_led.lock);
    if (g_led.slots[who].active) {
        APP_LOGD("led", "release client=%s (was scene=%s)",
                 client_name(who), scene_name(g_led.slots[who].scene));
        g_led.slots[who].active = false;
    }
    pthread_mutex_unlock(&g_led.lock);
}

void service_led_set_enabled(bool on)
{
    pthread_mutex_lock(&g_led.lock);
    g_led.enabled = on;
    pthread_mutex_unlock(&g_led.lock);
    APP_LOGI("led", "enabled=%d", on ? 1 : 0);
}

bool service_led_get_enabled(void)
{
    bool on;
    pthread_mutex_lock(&g_led.lock);
    on = g_led.enabled;
    pthread_mutex_unlock(&g_led.lock);
    return on;
}

void service_led_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    pthread_mutex_lock(&g_led.lock);
    g_led.brightness = percent;
    pthread_mutex_unlock(&g_led.lock);
    led_strip_hal_set_brightness(percent);
}

int service_led_get_brightness(void)
{
    int b;
    pthread_mutex_lock(&g_led.lock);
    b = g_led.brightness;
    pthread_mutex_unlock(&g_led.lock);
    return b;
}

void service_led_on_ai_status(const ai_status_t *st)
{
    if (!st)
        return;

    switch (st->state) {
    case AI_STATE_CONNECTING:
        /*
         * Quiet on purpose: AI bring-up is background (especially after reboot
         * when WiFi already flashed green). Showing purple here leaves a
         * one-frame "wrong color" tail. Lights only for dialog/error states.
         */
        service_led_release(LED_CLIENT_AI);
        break;
    case AI_STATE_LISTENING:
        service_led_request(LED_CLIENT_AI, LED_SCENE_AI_LISTENING, 0, 0);
        break;
    case AI_STATE_THINKING:
        service_led_request(LED_CLIENT_AI, LED_SCENE_AI_THINKING, 0, 0);
        break;
    case AI_STATE_SPEAKING:
        service_led_request(LED_CLIENT_AI, LED_SCENE_AI_SPEAKING, 0, 0);
        break;
    case AI_STATE_ERROR:
        /* Short red cue, then off — do not stick “always red”. */
        service_led_request(LED_CLIENT_AI, LED_SCENE_AI_ERROR, 0,
                            LED_AI_ERROR_TTL_MS);
        break;
    case AI_STATE_NETWORK_UNAVAILABLE:
        /* Quiet: no continuous glow while waiting for network. */
        service_led_release(LED_CLIENT_AI);
        break;
    case AI_STATE_IDLE:
    default:
        service_led_release(LED_CLIENT_AI);
        break;
    }
}

void service_led_on_ota_status(const ota_status_t *st)
{
    if (!st)
        return;

    switch (st->state) {
    case OTA_STATE_CHECKING:
    case OTA_STATE_DOWNLOADING:
        service_led_request(LED_CLIENT_OTA, LED_SCENE_OTA_PROGRESS,
                            st->progress, 0);
        break;
    case OTA_STATE_DOWNLOAD_DONE:
        service_led_request(LED_CLIENT_OTA, LED_SCENE_OTA_PROGRESS, 100, 0);
        break;
    case OTA_STATE_APPLYING:
    case OTA_STATE_REBOOTING:
    case OTA_STATE_COMMITTING:
        service_led_request(LED_CLIENT_OTA, LED_SCENE_OTA_APPLYING, 0, 0);
        break;
    case OTA_STATE_ERROR:
        service_led_request(LED_CLIENT_OTA, LED_SCENE_ERROR, 0, 4000);
        break;
    case OTA_STATE_IDLE:
    case OTA_STATE_UP_TO_DATE:
    case OTA_STATE_UPDATE_AVAILABLE:
    default:
        service_led_release(LED_CLIENT_OTA);
        break;
    }
}

void service_led_on_wifi_connecting(void)
{
    service_led_request(LED_CLIENT_NETWORK, LED_SCENE_WIFI_CONNECTING, 0, 0);
}

void service_led_on_wifi_connected(void)
{
    service_led_request(LED_CLIENT_NETWORK, LED_SCENE_WIFI_OK, 0,
                        LED_WIFI_OK_TTL_MS);
}

void service_led_on_wifi_disconnected(void)
{
    service_led_release(LED_CLIENT_NETWORK);
}

void service_led_on_wifi_error(void)
{
    service_led_request(LED_CLIENT_NETWORK, LED_SCENE_WIFI_FAIL, 0,
                        LED_WIFI_FAIL_TTL_MS);
}

void service_led_on_ui_swipe(int dir)
{
    if (dir == 0)
        return;
    if (dir < 0) {
        service_led_request(LED_CLIENT_SETTINGS, LED_SCENE_SWIPE_LEFT, 0,
                            LED_UI_SWIPE_TTL_MS);
    } else {
        service_led_request(LED_CLIENT_SETTINGS, LED_SCENE_SWIPE_RIGHT, 0,
                            LED_UI_SWIPE_TTL_MS);
    }
}

void service_led_handle_command(const led_cmd_t *cmd)
{
    if (!cmd)
        return;
    switch (cmd->action) {
    case LED_CMD_SWIPE_LEFT:
        service_led_on_ui_swipe(-1);
        break;
    case LED_CMD_SWIPE_RIGHT:
        service_led_on_ui_swipe(1);
        break;
    default:
        break;
    }
}

void service_led_init(void)
{
    if (g_led.thread_started)
        return;

    memset(g_led.slots, 0, sizeof(g_led.slots));
    atomic_store(&g_led.running, true);

    if (led_strip_hal_open(LED_STRIP_DEFAULT_COUNT) == 0) {
        led_strip_hal_set_brightness(g_led.brightness);
        led_strip_hal_clear();
        led_strip_hal_flush();
        g_led.hal_ok = true;
    } else {
        g_led.hal_ok = false;
        APP_LOGW("led", "device not ready at init; will retry");
    }

    /* Explicit boot signature, then OFF until next event. */
    service_led_request(LED_CLIENT_SYSTEM, LED_SCENE_BOOT, 0, LED_BOOT_TTL_MS);

    if (pthread_create(&g_led.thread, NULL, led_render_thread, NULL) != 0) {
        APP_LOGE("led", "failed to start render thread");
        atomic_store(&g_led.running, false);
        led_strip_hal_close();
        return;
    }
    g_led.thread_started = true;
    APP_LOGI("led", "service ready brightness=%d%% enabled=%d",
             g_led.brightness, g_led.enabled ? 1 : 0);
}

void service_led_deinit(void)
{
    if (!g_led.thread_started)
        return;

    atomic_store(&g_led.running, false);
    pthread_join(g_led.thread, NULL);
    g_led.thread_started = false;

    led_strip_hal_close();
    memset(g_led.slots, 0, sizeof(g_led.slots));
    APP_LOGI("led", "service stopped");
}
