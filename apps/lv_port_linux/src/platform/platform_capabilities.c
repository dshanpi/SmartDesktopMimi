#include "platform_capabilities.h"

bool aitvbox_platform_has(const aitvbox_platform_descriptor_t *descriptor,
                          aitvbox_platform_capability_t capability)
{
    if (!descriptor || descriptor->abi_version != AITVBOX_PLATFORM_ABI_VERSION) {
        return false;
    }
    return (descriptor->capabilities & (aitvbox_capability_mask_t)capability) != 0;
}
