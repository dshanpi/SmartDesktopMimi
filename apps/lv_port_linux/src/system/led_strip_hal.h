/**
 * @file led_strip_hal.h
 * @brief WS2812 strip HAL — sole opener of /dev/ws2812-leds.
 *
 * Only service_led (Backend) should use this. Other modules must go through
 * service_led request/release so the strip remains a single-owner resource.
 */
#ifndef LED_STRIP_HAL_H
#define LED_STRIP_HAL_H

#include <stdbool.h>
#include <stdint.h>

#ifndef LED_STRIP_DEFAULT_COUNT
#define LED_STRIP_DEFAULT_COUNT 10
#endif

#define LED_STRIP_MAX_COUNT 64

/* GRB channel order matches the kernel driver / WS2812 wire format. */
typedef struct {
    uint8_t g;
    uint8_t r;
    uint8_t b;
} led_rgb_t;

/**
 * Open device, take exclusive flock, allocate frame buffer.
 * @param led_count 0 → LED_STRIP_DEFAULT_COUNT
 * @return 0 on success, negative errno-style on failure (device missing is ok to retry)
 */
int led_strip_hal_open(int led_count);

void led_strip_hal_close(void);

bool led_strip_hal_is_open(void);

int led_strip_hal_count(void);

/** Brightness 0–100 applied on write (not stored in pixel buffer). */
void led_strip_hal_set_brightness(int percent);
int led_strip_hal_get_brightness(void);

void led_strip_hal_clear(void);
void led_strip_hal_set_all(uint8_t r, uint8_t g, uint8_t b);
void led_strip_hal_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b);

/** Push current buffer to the device (GRB × N). Thread-safe. */
int led_strip_hal_flush(void);

#endif /* LED_STRIP_HAL_H */
