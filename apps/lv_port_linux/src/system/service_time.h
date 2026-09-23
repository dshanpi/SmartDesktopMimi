#ifndef SERVICE_TIME_H
#define SERVICE_TIME_H

#include <time.h>

/**
 * @brief Initialize time service.
 *
 * The product is domestic-only, so UI processes display time with UTC+8. The
 * RTC itself is kept in UTC to stay compatible with AI-TVBox and Linux time
 * semantics.
 */
void service_time_init(void);

/**
 * @brief Deinitialize time service.
 */
void service_time_deinit(void);

/**
 * @brief Apply a trusted UTC Unix timestamp to system time and RTC.
 *
 * Use this for UI/manual time setting and network time sync. RTC is written in
 * UTC, while UI display should remain controlled by TZ.
 */
int service_time_set_utc_epoch(time_t utc_epoch, const char *source);

/**
 * @brief Low frequency RTC persistence guard.
 *
 * This catches time changes made through other system paths by mirroring a
 * plausible system clock back to RTC only when a meaningful drift is detected.
 */
void service_time_update(void);

/**
 * @brief Notify time service that network is usable.
 *
 * This schedules an asynchronous SNTP sync with throttling.
 */
void service_time_on_network_ready(void);

#endif /* SERVICE_TIME_H */
