#include "tkl_gpio.h"
#include "tuya_error_code.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

typedef struct {
    TUYA_GPIO_NUM_E pin_id;
    TUYA_GPIO_BASE_CFG_T base_cfg;

    int handle_fd;

    TUYA_GPIO_IRQ_T irq_cfg;
    int event_fd;
    pthread_t irq_thread;
    bool irq_thread_started;
    bool irq_enabled;

    struct gpioevent_request event_req;

    struct gpiohandle_request hreq;
    bool hreq_valid;
} gpio_pin_ctx_t;

static pthread_mutex_t g_gpio_lock = PTHREAD_MUTEX_INITIALIZER;
static gpio_pin_ctx_t g_gpio_pins[64];
static bool g_gpio_inited = false;

static void gpio_global_init_once(void)
{
    if (g_gpio_inited) {
        return;
    }

    memset(g_gpio_pins, 0, sizeof(g_gpio_pins));
    for (size_t i = 0; i < (sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0])); i++) {
        g_gpio_pins[i].pin_id = (TUYA_GPIO_NUM_E)i;
        g_gpio_pins[i].handle_fd = -1;
        g_gpio_pins[i].event_fd = -1;
    }
    g_gpio_inited = true;
}

static int gpiochip_open_best_effort(void)
{
    const char *candidates[] = { "/dev/gpiochip0", "/dev/gpiochip1", "/dev/gpiochip2", "/dev/gpiochip3" };
    for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); i++) {
        int fd = open(candidates[i], O_RDONLY);
        if (fd >= 0) {
            return fd;
        }
    }
    return -1;
}

static uint32_t tuya_level_to_u32(TUYA_GPIO_LEVEL_E level)
{
    return (level == TUYA_GPIO_LEVEL_HIGH) ? 1U : 0U;
}

static TUYA_GPIO_LEVEL_E u32_to_tuya_level(uint8_t v)
{
    return (v != 0U) ? TUYA_GPIO_LEVEL_HIGH : TUYA_GPIO_LEVEL_LOW;
}

static uint32_t mode_to_handle_flags(TUYA_GPIO_MODE_E mode)
{
    uint32_t flags = 0;

    switch (mode) {
    case TUYA_GPIO_OPENDRAIN:
    case TUYA_GPIO_OPENDRAIN_PULLUP:
#ifdef GPIOHANDLE_REQUEST_OPEN_DRAIN
        flags |= GPIOHANDLE_REQUEST_OPEN_DRAIN;
#endif
        break;
    default:
        break;
    }

#ifdef GPIOHANDLE_REQUEST_BIAS_PULL_UP
    if (mode == TUYA_GPIO_PULLUP || mode == TUYA_GPIO_OPENDRAIN_PULLUP) {
        flags |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
    }
#endif

#ifdef GPIOHANDLE_REQUEST_BIAS_PULL_DOWN
    if (mode == TUYA_GPIO_PULLDOWN) {
        flags |= GPIOHANDLE_REQUEST_BIAS_PULL_DOWN;
    }
#endif

    return flags;
}

static int ensure_line_handle_locked(gpio_pin_ctx_t *pin)
{
    if (pin == NULL) {
        return -1;
    }
    if (pin->handle_fd >= 0 && pin->hreq_valid) {
        return 0;
    }

    int chip_fd = gpiochip_open_best_effort();
    if (chip_fd < 0) {
        return -1;
    }

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = (uint32_t)pin->pin_id;
    req.lines = 1;

    uint32_t flags = mode_to_handle_flags(pin->base_cfg.mode);
    if (pin->base_cfg.direct == TUYA_GPIO_OUTPUT) {
        flags |= GPIOHANDLE_REQUEST_OUTPUT;
        req.default_values[0] = tuya_level_to_u32(pin->base_cfg.level);
    } else {
        flags |= GPIOHANDLE_REQUEST_INPUT;
    }
    req.flags = flags;
    (void)snprintf(req.consumer_label, sizeof(req.consumer_label), "tuya_tkl_gpio");

    int rc = ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
    close(chip_fd);
    if (rc < 0) {
        return -1;
    }

    pin->handle_fd = req.fd;
    pin->hreq = req;
    pin->hreq_valid = true;
    return 0;
}

static void close_line_handle_locked(gpio_pin_ctx_t *pin)
{
    if (pin->handle_fd >= 0) {
        close(pin->handle_fd);
        pin->handle_fd = -1;
    }
    pin->hreq_valid = false;
    memset(&pin->hreq, 0, sizeof(pin->hreq));
}

static void close_event_fd_locked(gpio_pin_ctx_t *pin)
{
    if (pin->event_fd >= 0) {
        close(pin->event_fd);
        pin->event_fd = -1;
    }
    memset(&pin->event_req, 0, sizeof(pin->event_req));
}

static int request_event_fd_locked(gpio_pin_ctx_t *pin)
{
    if (pin->event_fd >= 0) {
        return 0;
    }

    int chip_fd = gpiochip_open_best_effort();
    if (chip_fd < 0) {
        return -1;
    }

    struct gpioevent_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffset = (uint32_t)pin->pin_id;
    (void)snprintf(req.consumer_label, sizeof(req.consumer_label), "tuya_tkl_gpio_irq");

    switch (pin->irq_cfg.mode) {
    case TUYA_GPIO_IRQ_RISE:
        req.eventflags = GPIOEVENT_REQUEST_RISING_EDGE;
        break;
    case TUYA_GPIO_IRQ_FALL:
        req.eventflags = GPIOEVENT_REQUEST_FALLING_EDGE;
        break;
    case TUYA_GPIO_IRQ_RISE_FALL:
    case TUYA_GPIO_IRQ_LOW:
    case TUYA_GPIO_IRQ_HIGH:
    default:
        req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
        break;
    }

    int rc = ioctl(chip_fd, GPIO_GET_LINEEVENT_IOCTL, &req);
    close(chip_fd);
    if (rc < 0) {
        return -1;
    }

    pin->event_fd = req.fd;
    pin->event_req = req;
    return 0;
}

static bool irq_mode_match_level(TUYA_GPIO_IRQ_E mode, TUYA_GPIO_LEVEL_E level)
{
    if (mode == TUYA_GPIO_IRQ_LOW) {
        return level == TUYA_GPIO_LEVEL_LOW;
    }
    if (mode == TUYA_GPIO_IRQ_HIGH) {
        return level == TUYA_GPIO_LEVEL_HIGH;
    }
    return true;
}

static void *gpio_irq_thread(void *arg)
{
    gpio_pin_ctx_t *pin = (gpio_pin_ctx_t *)arg;
    if (pin == NULL) {
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&g_gpio_lock);
        int efd = pin->event_fd;
        bool enabled = pin->irq_enabled;
        TUYA_GPIO_IRQ_T irq_cfg = pin->irq_cfg;
        pthread_mutex_unlock(&g_gpio_lock);

        if (efd < 0) {
            return NULL;
        }

        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = efd;
        pfd.events = POLLIN;

        int prc = poll(&pfd, 1, -1);
        if (prc <= 0) {
            continue;
        }
        if ((pfd.revents & POLLIN) == 0) {
            continue;
        }

        struct gpioevent_data ev;
        ssize_t n = read(efd, &ev, sizeof(ev));
        if (n != (ssize_t)sizeof(ev)) {
            return NULL;
        }

        if (!enabled || irq_cfg.cb == NULL) {
            continue;
        }

        if (irq_cfg.mode == TUYA_GPIO_IRQ_LOW || irq_cfg.mode == TUYA_GPIO_IRQ_HIGH) {
            TUYA_GPIO_LEVEL_E level;
            if (tkl_gpio_read(pin->pin_id, &level) == OPRT_OK) {
                if (!irq_mode_match_level(irq_cfg.mode, level)) {
                    continue;
                }
            }
        }

        irq_cfg.cb(irq_cfg.arg);
    }

    return NULL;
}

OPERATE_RET tkl_gpio_init(TUYA_GPIO_NUM_E pin_id, const TUYA_GPIO_BASE_CFG_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (pin_id >= (TUYA_GPIO_NUM_E)(sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_gpio_lock);
    gpio_global_init_once();

    gpio_pin_ctx_t *pin = &g_gpio_pins[(size_t)pin_id];
    pin->pin_id = pin_id;
    pin->base_cfg = *cfg;

    close_line_handle_locked(pin);

    // For Linux gpio-cdev, an IRQ event request and a line handle request are mutually exclusive.
    // Keep input pins lazy-opened (handle opened on first read), so that calling
    // tkl_gpio_init(pin, INPUT) then tkl_gpio_irq_init(pin, ...) will not fail with EBUSY.
    int rc = 0;
    if (pin->base_cfg.direct == TUYA_GPIO_OUTPUT) {
        rc = ensure_line_handle_locked(pin);
    }
    pthread_mutex_unlock(&g_gpio_lock);

    return (rc == 0) ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET tkl_gpio_deinit(TUYA_GPIO_NUM_E pin_id)
{
    if (pin_id >= (TUYA_GPIO_NUM_E)(sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_gpio_lock);
    gpio_global_init_once();
    gpio_pin_ctx_t *pin = &g_gpio_pins[(size_t)pin_id];

    pin->irq_enabled = false;
    close_event_fd_locked(pin);
    close_line_handle_locked(pin);

    pthread_mutex_unlock(&g_gpio_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_gpio_write(TUYA_GPIO_NUM_E pin_id, TUYA_GPIO_LEVEL_E level)
{
    if (pin_id >= (TUYA_GPIO_NUM_E)(sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_gpio_lock);
    gpio_global_init_once();
    gpio_pin_ctx_t *pin = &g_gpio_pins[(size_t)pin_id];
    pin->pin_id = pin_id;
    pin->base_cfg.level = level;

    if (ensure_line_handle_locked(pin) != 0) {
        pthread_mutex_unlock(&g_gpio_lock);
        return OPRT_COM_ERROR;
    }

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = (uint8_t)tuya_level_to_u32(level);

    int rc = ioctl(pin->handle_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    pthread_mutex_unlock(&g_gpio_lock);
    return (rc == 0) ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET tkl_gpio_read(TUYA_GPIO_NUM_E pin_id, TUYA_GPIO_LEVEL_E *level)
{
    if (level == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (pin_id >= (TUYA_GPIO_NUM_E)(sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_gpio_lock);
    gpio_global_init_once();
    gpio_pin_ctx_t *pin = &g_gpio_pins[(size_t)pin_id];
    pin->pin_id = pin_id;

    if (ensure_line_handle_locked(pin) != 0) {
        pthread_mutex_unlock(&g_gpio_lock);
        return OPRT_COM_ERROR;
    }

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    int rc = ioctl(pin->handle_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
    if (rc == 0) {
        *level = u32_to_tuya_level(data.values[0]);
        pin->base_cfg.level = *level;
    }

    pthread_mutex_unlock(&g_gpio_lock);
    return (rc == 0) ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET tkl_gpio_irq_init(TUYA_GPIO_NUM_E pin_id, const TUYA_GPIO_IRQ_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (pin_id >= (TUYA_GPIO_NUM_E)(sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_gpio_lock);
    gpio_global_init_once();

    gpio_pin_ctx_t *pin = &g_gpio_pins[(size_t)pin_id];
    pin->pin_id = pin_id;
    pin->irq_cfg = *cfg;

    // If someone previously initialized this pin and acquired a line handle,
    // close it before requesting an event fd; otherwise GPIO_GET_LINEEVENT_IOCTL may fail (EBUSY).
    close_line_handle_locked(pin);

    if (request_event_fd_locked(pin) != 0) {
        pthread_mutex_unlock(&g_gpio_lock);
        return OPRT_COM_ERROR;
    }

    if (!pin->irq_thread_started) {
        pin->irq_thread_started = (pthread_create(&pin->irq_thread, NULL, gpio_irq_thread, pin) == 0);
    }

    pthread_mutex_unlock(&g_gpio_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_gpio_irq_enable(TUYA_GPIO_NUM_E pin_id)
{
    if (pin_id >= (TUYA_GPIO_NUM_E)(sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_gpio_lock);
    gpio_global_init_once();
    gpio_pin_ctx_t *pin = &g_gpio_pins[(size_t)pin_id];
    pin->pin_id = pin_id;
    pin->irq_enabled = true;
    pthread_mutex_unlock(&g_gpio_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_gpio_irq_disable(TUYA_GPIO_NUM_E pin_id)
{
    if (pin_id >= (TUYA_GPIO_NUM_E)(sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]))) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_gpio_lock);
    gpio_global_init_once();
    gpio_pin_ctx_t *pin = &g_gpio_pins[(size_t)pin_id];
    pin->pin_id = pin_id;
    pin->irq_enabled = false;
    pthread_mutex_unlock(&g_gpio_lock);
    return OPRT_OK;
}
