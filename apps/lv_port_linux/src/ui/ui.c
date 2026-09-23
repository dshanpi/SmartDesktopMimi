#include "ui.h"
#include "ui_helpers.h"
#include "ui_fonts.h"
#include <stdio.h>
#include <string.h>
#include "../system/app_manager.h"
#include "../system/service_wifi.h"
#include "../system/settings.h"
#include "../system/backend_types.h"
#include "../middleware/middleware.h"
#include "theme/theme.h"
#include "desktop_clock.h"
#include "desktop_all_apps.h"
#include "desktop_dock.h"
#include "desktop_status_bar.h"
#include "desktop_home_tokens.h"
#include "../apps/hdmi_mcp/hdmi_mcp_app.h"

/* 布局调试开关：1=显示区域边框，0=关闭 */
#define UI_LAYOUT_DEBUG 0

/* =======================
 * 字体声明
 * ======================= */
LV_IMAGE_DECLARE(TemperatureAndHumidity);
LV_IMAGE_DECLARE(wifi_off_28dp_666666); // Updated
LV_IMAGE_DECLARE(wifi_28dp_666666); // Available for future use
LV_IMAGE_DECLARE(sun);


/* TODO: User to provide these images */
// LV_IMAGE_DECLARE(ic_battery);




/* =======================
 * 前置声明
 * ======================= */
static lv_obj_t *create_main_card(lv_obj_t *parent);
static void create_desktop_background_layers(lv_obj_t *parent);

/* =======================
 * UI 入口
 * ======================= */
void ui_init(void)
{
    /* 初始化主题 */
    ui_theme_init();
    ui_fonts_init();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_add_style(scr, ui_theme_get_desktop_bg_style(), 0);
    lv_obj_set_style_bg_color(scr, HOME_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_desktop_background_layers(scr);

    /* 初始化应用管理器 (注册当前屏幕为主页) */
    app_manager_init();
    hdmi_mcp_app_monitor_start();

    lv_obj_t *main_card = create_main_card(scr);

    ui_statusbar_create(main_card);
    ui_clock_create(main_card);
    ui_dock_create(main_card);
    ui_all_apps_create(scr);

    /* 订阅WiFi状态消息 */
    mw_subscribe(TOPIC_WIFI_STATUS, wifi_status_callback);
    mw_subscribe(TOPIC_WIFI_RUNTIME, wifi_runtime_callback);
    mw_subscribe(TOPIC_SENSOR_STATUS, sensor_status_callback);
    mw_subscribe(TOPIC_NETWORK_INFO, network_info_callback);
    mw_subscribe(TOPIC_HDMI_PREVIEW_STATUS, hdmi_preview_status_callback);
    mw_subscribe(TOPIC_CLOUD_STATUS, cloud_status_callback);
    mw_subscribe(TOPIC_OTA_STATUS, cloud_ota_status_callback);
    printf("[UI] Subscribed to WiFi status messages\n");

    /* 请求当前WiFi状态（由 runtime 回调决定是否做首次扫描） */
    printf("[UI] Requesting current WiFi status\n");
    wifi_cmd_t cmd = { .action = CMD_GET_RUNTIME };
    mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);

    /* 请求当前 HDMI 预览状态，同步顶栏「显示模式」开关 */
    hdmi_cmd_t hcmd = { .action = HDMI_CMD_GET_STATUS };
    mw_publish(TOPIC_HDMI_COMMAND, &hcmd, sizeof(hcmd), MW_DIR_UI_TO_BACKEND);

    /* 请求当前 100ask 云状态，同步顶栏云图标 */
    cloud_cmd_t ccmd = { .action = CLOUD_CMD_GET_STATUS };
    mw_publish(TOPIC_CLOUD_COMMAND, &ccmd, sizeof(ccmd), MW_DIR_UI_TO_BACKEND);

    /* 同步 OTA 阶段（下载中/待安装时顶栏进度条与圆点颜色） */
    ota_cmd_t ocmd;
    memset(&ocmd, 0, sizeof(ocmd));
    ocmd.action = OTA_CMD_GET_STATUS;
    mw_publish(TOPIC_OTA_COMMAND, &ocmd, sizeof(ocmd), MW_DIR_UI_TO_BACKEND);
}

/* =======================
 * 桌面背景分层
 * ======================= */
static void create_desktop_background_layers(lv_obj_t *parent)
{
    /* A restrained cool halo gives the AI surface depth without using a
     * decorative wallpaper that competes with HDMI content. */
    lv_obj_t *upper_field = lv_obj_create(parent);
    lv_obj_set_size(upper_field, lv_pct(100), 310);
    lv_obj_align(upper_field, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(upper_field, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_grad_color(upper_field, HOME_BG, 0);
    lv_obj_set_style_bg_grad_dir(upper_field, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(upper_field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(upper_field, 0, 0);
    lv_obj_set_style_radius(upper_field, 0, 0);
    lv_obj_set_style_pad_all(upper_field, 0, 0);
    lv_obj_set_style_shadow_width(upper_field, 0, 0);
    lv_obj_clear_flag(upper_field, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(upper_field, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

/* =======================
 * 主卡片容器
 * ======================= */
static lv_obj_t *create_main_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_size(card, lv_pct(100), lv_pct(100));
    lv_obj_center(card);

    // lv_obj_set_style_bg_color(card, UI_DESKTOP_CARD_BG, 0); // Removed direct color setting
    // lv_obj_set_style_radius(card, 0, 0);
    
    /* Make main card transparent to show desktop background */
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0); // No border for the main container

    lv_obj_set_style_shadow_width(card, 0, 0);
    /* Let child tray/card shadows paint into the 12px flex gaps. */
    lv_obj_add_flag(card, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* 使用纵向 Flex 布局 */
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
                           LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER);

    /* 10 + 64 + 10 + 550 + 10 + 114 + 10 = 768. */
    lv_obj_set_style_pad_hor(card, 0, 0);
    lv_obj_set_style_pad_top(card, 10, 0);
    lv_obj_set_style_pad_bottom(card, 10, 0);
    lv_obj_set_style_pad_row(card, HOME_GAP, 0);

    apply_debug_outline(card, lv_color_hex(0x6A4CFF));

    return card;
}
