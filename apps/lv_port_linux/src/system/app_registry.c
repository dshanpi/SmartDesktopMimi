#include "app_registry.h"
#include "../ui/locale_en_us.h"
#include "../apps/ai/ai_app.h"
#include "../apps/bt/bt_app.h"
#include "../apps/ota/ota_app.h"
#include "../apps/setting/setting_app.h"
#include "../apps/video/video_app.h"
#include "../apps/wifi/wifi_app.h"
#include "../apps/user/user_app.h"
#include "../apps/hdmi_mcp/hdmi_mcp_app.h"

LV_IMAGE_DECLARE(video_app);
LV_IMAGE_DECLARE(wifi_app);
LV_IMAGE_DECLARE(setting_app);
LV_IMAGE_DECLARE(aibot_app);
LV_IMAGE_DECLARE(bt_speaker);

static const app_registry_entry_t app_registry[] = {
    {
        .id = APP_ID_VIDEO,
        .name = UI_APP_VIDEO,
        .icon_type = APP_ICON_BUILTIN,
        .icon_src = &video_app,
        .accent_color = 0xE85D75,
        .show_in_launcher = true,
        .descriptor = &app_video,
    },
    {
        .id = APP_ID_WIFI,
        .name = UI_APP_WIFI,
        .icon_type = APP_ICON_BUILTIN,
        .icon_src = &wifi_app,
        .accent_color = 0x16A6E8,
        .show_in_launcher = true,
        .descriptor = &app_wifi,
    },
    {
        .id = APP_ID_SETTING,
        .name = UI_APP_SETTINGS,
        .icon_type = APP_ICON_BUILTIN,
        .icon_src = &setting_app,
        .accent_color = 0x7C63FF,
        .show_in_launcher = true,
        .descriptor = &app_setting,
    },
    {
        .id = APP_ID_AI,
        .name = UI_APP_AI,
        .icon_type = APP_ICON_BUILTIN,
        .icon_src = &aibot_app,
        .accent_color = 0x30B27A,
        .show_in_launcher = true,
        .descriptor = &app_ai,
    },
    {
        .id = APP_ID_BT,
        .name = UI_APP_BLUETOOTH,
        .icon_type = APP_ICON_BUILTIN,
        .icon_src = &bt_speaker,
        .accent_color = 0x2E7DFF,
        .show_in_launcher = true,
        .descriptor = &app_bt,
    },
    {
        .id = APP_ID_OTA,
        .name = UI_APP_OTA,
        .icon_type = APP_ICON_BUILTIN,
        .icon_src = NULL,
        .accent_color = 0x2E7DFF,
        .show_in_launcher = true,
        .descriptor = &app_ota,
    },
    {
        .id = APP_ID_USER_APPS,
        .name = UI_APP_USER,
        .icon_type = APP_ICON_GENERATED,
        .icon_src = NULL,
        .accent_color = 0x00897B,
        .show_in_launcher = true,
        .descriptor = &app_user,
    },
    {
        .id = APP_ID_HDMI_MCP,
        .name = "HDMI MCP",
        .icon_type = APP_ICON_GENERATED,
        .icon_src = NULL,
        .accent_color = 0x4868F2,
        .show_in_launcher = true,
        .descriptor = &app_hdmi_mcp,
    },
};

#define APP_REGISTRY_COUNT (sizeof(app_registry) / sizeof(app_registry[0]))

const app_registry_entry_t * app_registry_get_entry(app_id_t id)
{
    for (size_t i = 0; i < APP_REGISTRY_COUNT; i++) {
        if (app_registry[i].id == id) {
            return &app_registry[i];
        }
    }
    return NULL;
}

AppDescriptor * app_registry_get_descriptor(app_id_t id)
{
    const app_registry_entry_t *entry = app_registry_get_entry(id);
    return entry ? entry->descriptor : NULL;
}

size_t app_registry_get_launcher_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < APP_REGISTRY_COUNT; i++) {
        if (app_registry[i].show_in_launcher) count++;
    }
    return count;
}

const app_registry_entry_t * app_registry_get_launcher_entry(size_t index)
{
    size_t launcher_index = 0;
    for (size_t i = 0; i < APP_REGISTRY_COUNT; i++) {
        if (!app_registry[i].show_in_launcher) continue;
        if (launcher_index == index) return &app_registry[i];
        launcher_index++;
    }
    return NULL;
}
