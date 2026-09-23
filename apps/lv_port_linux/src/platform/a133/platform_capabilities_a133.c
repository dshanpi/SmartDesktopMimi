#include "../platform_capabilities.h"

const aitvbox_platform_descriptor_t *aitvbox_platform_get_descriptor(void)
{
    static const aitvbox_platform_descriptor_t descriptor = {
        .abi_version = AITVBOX_PLATFORM_ABI_VERSION,
        .platform_id = "a133-b6",
        .soc = "allwinner-a133",
        .provider_version = "1.0.0",
        .capabilities = AITVBOX_CAP_LOCAL_DISPLAY |
                        AITVBOX_CAP_HDMI_CAPTURE |
                        AITVBOX_CAP_HDMI_AUDIO |
                        AITVBOX_CAP_HARDWARE_H264 |
                        AITVBOX_CAP_USB_HID_GADGET |
                        AITVBOX_CAP_WIFI |
                        AITVBOX_CAP_BLUETOOTH_AUDIO |
                        AITVBOX_CAP_ENV_SENSOR |
                        AITVBOX_CAP_ADDRESSABLE_LED |
                        AITVBOX_CAP_AB_UPDATE |
                        AITVBOX_CAP_SECURE_IDENTITY,
    };
    return &descriptor;
}
