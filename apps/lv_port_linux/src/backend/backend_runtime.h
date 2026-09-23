#ifndef BACKEND_RUNTIME_H
#define BACKEND_RUNTIME_H

#include <stdbool.h>

typedef struct {
    bool wifi_enabled;
    bool bluetooth_enabled;
    bool hdmi_enabled;
} backend_runtime_options_t;

/* Start all system services in dependency order and apply persisted options. */
int backend_runtime_start(const backend_runtime_options_t *options);

/* Run one non-blocking service update cycle. */
void backend_runtime_tick(void);

/* Stop all started services in the established production order. */
void backend_runtime_stop(void);

#endif /* BACKEND_RUNTIME_H */
