#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

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

#endif /* SETTINGS_H */
