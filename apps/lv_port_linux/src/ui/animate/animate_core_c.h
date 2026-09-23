#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Tick all registered animation widgets once (call from LVGL timer). */
void animate_update_all(void);

/* Destroy all registered animation widgets (optional for teardown). */
void animate_clear_all(void);

#ifdef __cplusplus
}
#endif
