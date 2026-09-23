#ifndef AITVBOX_PLATFORM_CAPABILITIES_H
#define AITVBOX_PLATFORM_CAPABILITIES_H

#include "../../../../core/ports/aitvbox_ports.h"

#if !defined(AITVBOX_PLATFORM_ABI_VERSION)
#error "core platform ABI contract is unavailable"
#endif

/* Implemented by exactly one board adapter selected by the build profile. */
const aitvbox_platform_descriptor_t *aitvbox_platform_get_descriptor(void);

bool aitvbox_platform_has(const aitvbox_platform_descriptor_t *descriptor,
                          aitvbox_platform_capability_t capability);

#endif /* AITVBOX_PLATFORM_CAPABILITIES_H */
