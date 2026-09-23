/**
 * @file hal.cpp
 * @brief 硬件抽象层默认实现
 *
 * Tick must be monotonic and independent of wall-clock / NTP jumps.
 * Using gettimeofday() breaks NumberFlow after cold boot: system starts at
 * 1970, then NTP/RTC jumps to real time, producing multi-year dt so easing
 * finishes in a single frame (seconds reel looks like it "has no animation").
 */
#include "animate_types.hpp"
#include <chrono>
#include <thread>
#include <time.h>

namespace lv_ui {

static uint32_t _start_time_ms = 0;
static bool _initialized = false;

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        /* Fallback: steady_clock is also monotonic. */
        using clock = std::chrono::steady_clock;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now().time_since_epoch());
        return static_cast<uint32_t>(ms.count());
    }
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void ensure_init()
{
    if (!_initialized) {
        _start_time_ms = monotonic_ms();
        _initialized = true;
    }
}

uint32_t hal_get_tick()
{
    ensure_init();
    return monotonic_ms() - _start_time_ms;
}

void hal_delay(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace lv_ui
