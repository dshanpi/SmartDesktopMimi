#ifndef MIMI_DESKTOP_CAPABILITIES_H
#define MIMI_DESKTOP_CAPABILITIES_H
#include <stdbool.h>
/* Build capability ceiling only, NOT a claim that the service is online.
 * Legacy board builds retain their provider path. Ubuntu defines this as 0
 * until an actual HDMI adapter is integrated and qualified. */
#ifndef MIMI_HDMI_SUPPORTED
#define MIMI_HDMI_SUPPORTED 1
#endif
static inline bool desktop_hdmi_supported(void) { return MIMI_HDMI_SUPPORTED != 0; }
#endif
