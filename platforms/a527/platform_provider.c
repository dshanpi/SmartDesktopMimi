#include "../../core/ports/aitvbox_ports.h"

const aitvbox_platform_descriptor_t *aitvbox_platform_get_descriptor(void)
{
    static const aitvbox_platform_descriptor_t descriptor = {
        .abi_version = AITVBOX_PLATFORM_ABI_VERSION,
        .platform_id = "a527",
        .soc = "allwinner-a527",
        .provider_version = "0.0.0-skeleton",
        .capabilities = 0,
    };
    return &descriptor;
}
