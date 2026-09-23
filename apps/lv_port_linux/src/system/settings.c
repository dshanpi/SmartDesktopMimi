#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define SETTINGS_FILENAME ".lv_port_linux_settings.bin"
#define SETTINGS_VERSION 6
#define DEFAULT_THEME_COLOR 0x5267B8U
#define DEFAULT_THEME_PRESET_ID 0U
#define THEME_PRESET_COUNT 5U
#define LEGACY_DEFAULT_THEME_COLOR 0x2196F3U
#define LEGACY_MOON_MIST_THEME_COLOR 0x3F7F73U
#define LEGACY_SILVER_SLATE_THEME_COLOR 0xB86F5CU
#define LEGACY_SUNNY_CLOUD_THEME_COLOR 0xF27D62U

static void get_settings_path(char *buffer, size_t size) {
    const char *override = getenv("LV_SETTINGS_FILE");
    const char *home = getenv("HOME");

    if (override && override[0]) {
        snprintf(buffer, size, "%s", override);
    } else if (home && home[0]) {
        snprintf(buffer, size, "%s/%s", home, SETTINGS_FILENAME);
    } else {
        snprintf(buffer, size, "/tmp/%s", SETTINGS_FILENAME);
    }
}

static sys_settings_t current_settings = {
    .version = SETTINGS_VERSION,
    .theme_color = DEFAULT_THEME_COLOR,
    /* A value below 20% is not reliably visible on the R818 panel. */
    .brightness = 50,
    .volume = 50,
    .wifi_enabled = true,
    .bluetooth_enabled = true,
    /* First boot enters the complete local HDMI video/audio application.
     * A later user toggle is still persisted in the settings file. */
    .hdmi_enabled = true,
    .theme_preset_id = DEFAULT_THEME_PRESET_ID,
    .theme_follow_time = true,
    .bluetooth_name = {0},
};

typedef struct {
    uint32_t theme_color;
    int32_t brightness;
    int32_t volume;
} legacy_sys_settings_t;

typedef struct {
    uint32_t theme_color;
    int32_t brightness;
    int32_t volume;
    bool wifi_enabled;
} settings_v1_t;

typedef struct {
    uint32_t version;
    uint32_t theme_color;
    int32_t brightness;
    int32_t volume;
    bool wifi_enabled;
    bool bluetooth_enabled;
} settings_v2_t;

typedef struct {
    uint32_t version;
    uint32_t theme_color;
    int32_t brightness;
    int32_t volume;
    bool wifi_enabled;
    bool bluetooth_enabled;
    bool hdmi_enabled;
} settings_v3_t;

/* v4: preset id, no follow-time flag */
typedef struct {
    uint32_t version;
    uint32_t theme_color;
    int32_t brightness;
    int32_t volume;
    bool wifi_enabled;
    bool bluetooth_enabled;
    bool hdmi_enabled;
    uint32_t theme_preset_id;
} settings_v4_t;

/* v5: follow-time flag, no custom bluetooth name */
typedef struct {
    uint32_t version;
    uint32_t theme_color;
    int32_t brightness;
    int32_t volume;
    bool wifi_enabled;
    bool bluetooth_enabled;
    bool hdmi_enabled;
    uint32_t theme_preset_id;
    bool theme_follow_time;
} settings_v5_t;

static uint32_t preset_from_legacy_color(uint32_t color)
{
    switch (color) {
        case 0x3974B8U:
            return 1U;
        case 0xA95568U:
            return 2U;
        case 0x62598DU:
        case 0xA746C6U:
            return 3U;
        case 0xC8787AU:
            return 4U;
        default:
            return DEFAULT_THEME_PRESET_ID;
    }
}

static uint32_t preset_primary_color(uint32_t preset_id)
{
    static const uint32_t colors[THEME_PRESET_COUNT] = {
        0x5267B8U, /* 下午 · 靛蓝 */
        0x3974B8U, /* 上午 · 天蓝 */
        0xA95568U, /* 傍晚 · 暖珊瑚 */
        0x62598DU, /* 夜晚 · 暮紫 */
        0xC8787AU, /* 清晨 · 柔玫瑰 */
    };
    return colors[preset_id < THEME_PRESET_COUNT ? preset_id : 0U];
}

static void normalize_bluetooth_name(void) {
    size_t i;
    size_t start = 0;
    size_t end;
    char cleaned[sizeof(current_settings.bluetooth_name)];

    current_settings.bluetooth_name[sizeof(current_settings.bluetooth_name) - 1] = '\0';
    for (i = 0; current_settings.bluetooth_name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)current_settings.bluetooth_name[i];
        /* Drop control chars; keep printable UTF-8 bytes. */
        if (c < 0x20 || c == 0x7F) {
            current_settings.bluetooth_name[i] = ' ';
        }
    }

    while (current_settings.bluetooth_name[start] == ' ') {
        start++;
    }
    end = strlen(current_settings.bluetooth_name);
    while (end > start && current_settings.bluetooth_name[end - 1] == ' ') {
        end--;
    }

    if (end <= start) {
        current_settings.bluetooth_name[0] = '\0';
        return;
    }

    if (start == 0 && current_settings.bluetooth_name[end] == '\0') {
        return;
    }

    memset(cleaned, 0, sizeof(cleaned));
    if (end - start >= sizeof(cleaned)) {
        end = start + sizeof(cleaned) - 1;
    }
    memcpy(cleaned, current_settings.bluetooth_name + start, end - start);
    memcpy(current_settings.bluetooth_name, cleaned, sizeof(cleaned));
}

static void normalize_settings(void) {
    current_settings.version = SETTINGS_VERSION;
    if (current_settings.theme_preset_id >= THEME_PRESET_COUNT) {
        current_settings.theme_preset_id =
            preset_from_legacy_color(current_settings.theme_color);
    }
    current_settings.theme_color =
        preset_primary_color(current_settings.theme_preset_id);
    /* Never restore a persisted zero/near-zero value during boot.  Both the
     * backend and UI apply this setting, so accepting zero here can turn off
     * an otherwise healthy display immediately after the kernel lights it. */
    if (current_settings.brightness < 20) current_settings.brightness = 20;
    if (current_settings.brightness > 100) current_settings.brightness = 100;
    if (current_settings.volume < 0) current_settings.volume = 0;
    if (current_settings.volume > 100) current_settings.volume = 100;
    normalize_bluetooth_name();
}

void sys_settings_init(void) {
    char path[256];
    bool need_save = false;
    get_settings_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (f) {
        long file_size = 0;
        fseek(f, 0, SEEK_END);
        file_size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (file_size == (long)sizeof(sys_settings_t)) {
            sys_settings_t loaded = {0};
            if (fread(&loaded, sizeof(loaded), 1, f) != 1) {
                printf("Settings file read failed, using defaults.\n");
            } else if (loaded.version == SETTINGS_VERSION) {
                current_settings = loaded;
            } else {
                printf("Unknown settings version, using defaults.\n");
                need_save = true;
            }
        } else if (file_size == (long)sizeof(settings_v5_t)) {
            settings_v5_t legacy = {0};
            if (fread(&legacy, sizeof(legacy), 1, f) == 1 &&
                legacy.version == 5U) {
                current_settings.theme_color = legacy.theme_color;
                current_settings.brightness = legacy.brightness;
                current_settings.volume = legacy.volume;
                current_settings.wifi_enabled = legacy.wifi_enabled;
                current_settings.bluetooth_enabled = legacy.bluetooth_enabled;
                current_settings.hdmi_enabled = legacy.hdmi_enabled;
                current_settings.theme_preset_id = legacy.theme_preset_id;
                current_settings.theme_follow_time = legacy.theme_follow_time;
                memset(current_settings.bluetooth_name, 0,
                       sizeof(current_settings.bluetooth_name));
                printf("Settings v5 loaded and migrated.\n");
                need_save = true;
            } else {
                printf("Unknown settings v5 payload, using defaults.\n");
                need_save = true;
            }
        } else if (file_size == (long)sizeof(settings_v4_t)) {
            settings_v4_t legacy = {0};
            if (fread(&legacy, sizeof(legacy), 1, f) == 1 &&
                legacy.version == 4U) {
                current_settings.theme_color = legacy.theme_color;
                current_settings.brightness = legacy.brightness;
                current_settings.volume = legacy.volume;
                current_settings.wifi_enabled = legacy.wifi_enabled;
                current_settings.bluetooth_enabled = legacy.bluetooth_enabled;
                current_settings.hdmi_enabled = legacy.hdmi_enabled;
                current_settings.theme_preset_id = legacy.theme_preset_id;
                current_settings.theme_follow_time = true;
                printf("Settings v4 loaded and migrated.\n");
                need_save = true;
            } else {
                printf("Unknown settings v4 payload, using defaults.\n");
                need_save = true;
            }
        } else if (file_size == (long)sizeof(settings_v3_t)) {
            settings_v3_t legacy = {0};
            if (fread(&legacy, sizeof(legacy), 1, f) == 1) {
                if (legacy.version == 3U) {
                    current_settings.theme_color = legacy.theme_color;
                    current_settings.brightness = legacy.brightness;
                    current_settings.volume = legacy.volume;
                    current_settings.wifi_enabled = legacy.wifi_enabled;
                    current_settings.bluetooth_enabled = legacy.bluetooth_enabled;
                    current_settings.hdmi_enabled = legacy.hdmi_enabled;
                    current_settings.theme_preset_id =
                        preset_from_legacy_color(legacy.theme_color);
                    current_settings.theme_follow_time = true;
                    printf("Settings v3 loaded and migrated.\n");
                    need_save = true;
                } else if (legacy.version == 2U) {
                    current_settings.theme_color = legacy.theme_color;
                    current_settings.brightness = legacy.brightness;
                    current_settings.volume = legacy.volume;
                    current_settings.wifi_enabled = legacy.wifi_enabled;
                    current_settings.bluetooth_enabled = legacy.bluetooth_enabled;
                    current_settings.hdmi_enabled = false;
                    current_settings.theme_preset_id =
                        preset_from_legacy_color(legacy.theme_color);
                    current_settings.theme_follow_time = true;
                    printf("Settings v2 loaded and migrated.\n");
                    need_save = true;
                } else {
                    printf("Unknown settings version, using defaults.\n");
                    need_save = true;
                }
            }
        } else if (file_size == (long)sizeof(settings_v1_t)) {
            settings_v1_t legacy = {0};
            if (fread(&legacy, sizeof(legacy), 1, f) == 1) {
                current_settings.theme_color = legacy.theme_color;
                current_settings.brightness = legacy.brightness;
                current_settings.volume = legacy.volume;
                current_settings.wifi_enabled = legacy.wifi_enabled;
                current_settings.bluetooth_enabled = true;
                current_settings.hdmi_enabled = false;
                current_settings.theme_preset_id =
                    preset_from_legacy_color(legacy.theme_color);
                current_settings.theme_follow_time = true;
                printf("Settings v1 loaded and migrated.\n");
                need_save = true;
            }
        } else if (file_size == (long)sizeof(legacy_sys_settings_t)) {
            legacy_sys_settings_t legacy = {0};
            if (fread(&legacy, sizeof(legacy), 1, f) == 1) {
                current_settings.theme_color = legacy.theme_color;
                current_settings.brightness = legacy.brightness;
                current_settings.volume = legacy.volume;
                current_settings.wifi_enabled = true;
                current_settings.bluetooth_enabled = true;
                current_settings.hdmi_enabled = false;
                current_settings.theme_preset_id =
                    preset_from_legacy_color(legacy.theme_color);
                current_settings.theme_follow_time = true;
                printf("Legacy settings loaded and migrated.\n");
                need_save = true;
            }
        } else {
            printf("Unknown settings file format, using defaults.\n");
            need_save = true;
        }
        fclose(f);
        sys_settings_t before_normalize = current_settings;
        normalize_settings();
        if (memcmp(&before_normalize, &current_settings, sizeof(current_settings)) != 0) {
            need_save = true;
        }
        printf("Settings loaded from %s\n", path);
        if (need_save) {
            sys_settings_save();
        }
    } else {
        printf("No settings file found at %s, using defaults.\n", path);
        sys_settings_save(); // Create initial file
    }
}

sys_settings_t* sys_settings_get(void) {
    return &current_settings;
}

void sys_settings_save(void) {
    char path[256];
    get_settings_path(path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (f) {
        size_t n = fwrite(&current_settings, sizeof(sys_settings_t), 1, f);
        /* Flush through libc and kernel so power-loss/reboot keeps the write. */
        if (n == 1) {
            fflush(f);
            fsync(fileno(f));
        }
        fclose(f);
        if (n == 1) {
            printf("Settings saved to %s\n", path);
        } else {
            printf("Error writing settings to %s!\n", path);
        }
    } else {
        printf("Error saving settings to %s!\n", path);
    }
}
