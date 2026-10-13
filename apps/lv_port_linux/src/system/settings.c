#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

#define SETTINGS_FILENAME ".lv_port_linux_settings.bin"
#define SETTINGS_VERSION 8
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
    .tuya_ai_enabled = true,
    .led_enabled = true,
    .led_brightness = 22,
    .locale_mode = SYS_LOCALE_AUTO,
    .appearance_mode = SYS_APPEARANCE_AUTO,
};
static sys_settings_t persisted_settings_snapshot;
static bool persisted_settings_snapshot_valid;

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

/* v6: custom bluetooth name, before Tuya IoT persisted controls. */
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
    char bluetooth_name[48];
} settings_v6_t;

/* v7: Tuya IoT controls, before locale and appearance preferences. */
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
    char bluetooth_name[48];
    bool tuya_ai_enabled;
    bool led_enabled;
    int32_t led_brightness;
} settings_v7_t;

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
    if (current_settings.led_brightness < 0) current_settings.led_brightness = 0;
    if (current_settings.led_brightness > 100) current_settings.led_brightness = 100;
    if (current_settings.locale_mode > SYS_LOCALE_EN_US) {
        current_settings.locale_mode = SYS_LOCALE_AUTO;
    }
    if (current_settings.appearance_mode > SYS_APPEARANCE_DARK) {
        current_settings.appearance_mode = SYS_APPEARANCE_AUTO;
    }
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
        } else if (file_size == (long)sizeof(settings_v7_t)) {
            settings_v7_t legacy = {0};
            if (fread(&legacy, sizeof(legacy), 1, f) == 1 &&
                legacy.version == 7U) {
                current_settings.theme_color = legacy.theme_color;
                current_settings.brightness = legacy.brightness;
                current_settings.volume = legacy.volume;
                current_settings.wifi_enabled = legacy.wifi_enabled;
                current_settings.bluetooth_enabled = legacy.bluetooth_enabled;
                current_settings.hdmi_enabled = legacy.hdmi_enabled;
                current_settings.theme_preset_id = legacy.theme_preset_id;
                current_settings.theme_follow_time = legacy.theme_follow_time;
                memcpy(current_settings.bluetooth_name, legacy.bluetooth_name,
                       sizeof(current_settings.bluetooth_name));
                current_settings.tuya_ai_enabled = legacy.tuya_ai_enabled;
                current_settings.led_enabled = legacy.led_enabled;
                current_settings.led_brightness = legacy.led_brightness;
                current_settings.locale_mode = SYS_LOCALE_AUTO;
                current_settings.appearance_mode = SYS_APPEARANCE_AUTO;
                printf("Settings v7 loaded and migrated.\n");
                need_save = true;
            } else {
                printf("Unknown settings v7 payload, using defaults.\n");
                need_save = true;
            }
        } else if (file_size == (long)sizeof(settings_v6_t)) {
            settings_v6_t legacy = {0};
            if (fread(&legacy, sizeof(legacy), 1, f) == 1 &&
                legacy.version == 6U) {
                current_settings.theme_color = legacy.theme_color;
                current_settings.brightness = legacy.brightness;
                current_settings.volume = legacy.volume;
                current_settings.wifi_enabled = legacy.wifi_enabled;
                current_settings.bluetooth_enabled = legacy.bluetooth_enabled;
                current_settings.hdmi_enabled = legacy.hdmi_enabled;
                current_settings.theme_preset_id = legacy.theme_preset_id;
                current_settings.theme_follow_time = legacy.theme_follow_time;
                memcpy(current_settings.bluetooth_name, legacy.bluetooth_name,
                       sizeof(current_settings.bluetooth_name));
                /* New IoT controls intentionally start from safe product defaults. */
                current_settings.tuya_ai_enabled = true;
                current_settings.led_enabled = true;
                current_settings.led_brightness = 22;
                printf("Settings v6 loaded and migrated.\n");
                need_save = true;
            } else {
                printf("Unknown settings v6 payload, using defaults.\n");
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
        persisted_settings_snapshot = current_settings;
        persisted_settings_snapshot_valid = true;
        if (need_save) {
            sys_settings_save();
        }
    } else {
        printf("No settings file found at %s, using defaults.\n", path);
        persisted_settings_snapshot = current_settings;
        persisted_settings_snapshot_valid = true;
        sys_settings_save(); // Create initial file
    }
}

sys_settings_t* sys_settings_get(void) {
    return &current_settings;
}

static void merge_local_settings_changes(sys_settings_t *target,
                                         const sys_settings_t *desired,
                                         const sys_settings_t *base)
{
#define MERGE_CHANGED_FIELD(field) \
    do { \
        if (desired->field != base->field) target->field = desired->field; \
    } while (0)

    target->version = SETTINGS_VERSION;
    MERGE_CHANGED_FIELD(theme_color);
    MERGE_CHANGED_FIELD(brightness);
    MERGE_CHANGED_FIELD(volume);
    MERGE_CHANGED_FIELD(wifi_enabled);
    MERGE_CHANGED_FIELD(bluetooth_enabled);
    MERGE_CHANGED_FIELD(hdmi_enabled);
    MERGE_CHANGED_FIELD(theme_preset_id);
    MERGE_CHANGED_FIELD(theme_follow_time);
    if (memcmp(desired->bluetooth_name, base->bluetooth_name,
               sizeof(desired->bluetooth_name)) != 0) {
        memcpy(target->bluetooth_name, desired->bluetooth_name,
               sizeof(target->bluetooth_name));
    }
    MERGE_CHANGED_FIELD(tuya_ai_enabled);
    MERGE_CHANGED_FIELD(led_enabled);
    MERGE_CHANGED_FIELD(led_brightness);
    MERGE_CHANGED_FIELD(locale_mode);
    MERGE_CHANGED_FIELD(appearance_mode);

#undef MERGE_CHANGED_FIELD
}

void sys_settings_save(void) {
    char path[256];
    sys_settings_t desired;
    sys_settings_t merged;
    sys_settings_t disk;
    ssize_t count;
    size_t written = 0;
    int fd;

    get_settings_path(path, sizeof(path));

    desired = current_settings;
    merged = desired;
    fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        printf("Error saving settings to %s!\n", path);
        return;
    }
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        printf("Error locking settings at %s!\n", path);
        return;
    }
    (void)fchmod(fd, 0600);

    /* lv_backend and lvglsim are separate processes. Merge only fields changed
     * since this process last loaded/saved the file, so a Tuya cloud write of
     * HDMI/BT does not get reverted when the UI later saves brightness. */
    count = pread(fd, &disk, sizeof(disk), 0);
    if (persisted_settings_snapshot_valid && count == (ssize_t)sizeof(disk) &&
        disk.version == SETTINGS_VERSION) {
        merge_local_settings_changes(&disk, &desired,
                                     &persisted_settings_snapshot);
        merged = disk;
    }

    if (ftruncate(fd, 0) != 0) {
        flock(fd, LOCK_UN);
        close(fd);
        printf("Error truncating settings at %s!\n", path);
        return;
    }
    while (written < sizeof(merged)) {
        count = pwrite(fd, (const char *)&merged + written,
                       sizeof(merged) - written, (off_t)written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        written += (size_t)count;
    }
    if (written == sizeof(merged) && fsync(fd) == 0) {
        current_settings = merged;
        persisted_settings_snapshot = merged;
        persisted_settings_snapshot_valid = true;
        printf("Settings saved to %s\n", path);
    } else {
        printf("Error writing settings to %s!\n", path);
    }
    flock(fd, LOCK_UN);
    close(fd);
}
