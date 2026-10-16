#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SYS_LOCALE_AUTO = 0,
    SYS_LOCALE_ZH_CN = 1,
    SYS_LOCALE_EN_US = 2,
} sys_locale_mode_t;

typedef enum {
    SYS_APPEARANCE_AUTO = 0,
    SYS_APPEARANCE_LIGHT = 1,
    SYS_APPEARANCE_DARK = 2,
} sys_appearance_mode_t;

typedef struct {
    uint32_t version;
    uint32_t theme_color; // 0xRRGGBB format
    int32_t brightness;   // 0-100
    int32_t volume;       // 0-100
    bool wifi_enabled;    // 是否默认开启 WiFi
    bool bluetooth_enabled; // 是否默认开启蓝牙音箱
    bool hdmi_enabled;    // 是否默认开启 HDMI 显示模式检测
    uint32_t theme_preset_id; // Curated card-gradient preset
    bool theme_follow_time;   // 主题色跟随一天中的时段自动切换
    /* 本机蓝牙广播名；全 0 表示使用自动名（产品名 + MAC 后缀） */
    char bluetooth_name[48];
    /* Tuya IoT panel controlled preferences.  Keep these after the existing
     * v6 payload so old settings files can be migrated without ambiguity. */
    bool tuya_ai_enabled;
    bool led_enabled;
    int32_t led_brightness; /* 0-100 */
    /* v8: local UI preferences. AUTO locale follows an explicit zh/en process
     * locale and otherwise uses the product's Chinese shipping language;
     * AUTO appearance uses daytime (07:00-19:00) light surfaces. */
    uint32_t locale_mode;
    uint32_t appearance_mode;
} sys_settings_t;

/**
 * @brief Initialize settings module and load from file
 */
void sys_settings_init(void);

/**
 * @brief Get pointer to current settings
 * @return Pointer to global settings struct
 */
sys_settings_t* sys_settings_get(void);

/**
 * @brief Save current settings to file
 */
void sys_settings_save(void);
/* 0 only when the complete settings record was written and synced. */
int sys_settings_save_checked(void);

#endif /* SETTINGS_H */
