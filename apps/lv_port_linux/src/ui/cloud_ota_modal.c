/* cloud_ota_modal.c — 云端 OTA 安装提示全屏模态框（多态）。
 *
 * 两种态：
 *  - PROMPT     下载完成，问是否安装（图标盘子 + 版本 + 按钮）。
 *  - INSTALLING 安装中（spinner + 进度条 + 「请勿断电」警示），不关闭直到 reboot。
 *
 * 触发：cloud_ota_status_callback 检测到 ota_pending==true 且 OTA 下载完成
 *       （TOPIC_OTA_STATUS == DOWNLOAD_DONE）时调 cloud_ota_modal_show()。
 * 动作：立即安装 → OTA_CMD_APPLY + 切 INSTALLING 态（不关弹窗，给安装反馈）。
 *       稍后 → 关闭模态 + CLOUD_CMD_DISMISS_OTA（清 ota_pending + 写延后提醒）。
 * 在 LVGL 线程上下文执行（由 cloud_status_callback 经 mw_process_ui_messages 调度）。
 */

#include "cloud_ota_modal.h"
#include "theme/theme.h"
#include "../middleware/middleware.h"
#include "../system/backend_types.h"
#include "../system/app_manager.h"
#include <stdio.h>
#include <string.h>

/* 占位图标：暂用 refresh（更新语义）。后续替换为专属升级图标
 * system_update_48dp_1F1F1F（用户提供后接入）。 */
LV_IMG_DECLARE(refresh_28dp_1F1F1F)

typedef enum {
    MODAL_STATE_NONE = 0,
    MODAL_STATE_PROMPT,
    MODAL_STATE_INSTALLING,
} modal_state_t;

static lv_obj_t *modal_bg = NULL;       /* 全屏遮罩 */
static lv_obj_t *modal_card = NULL;
static modal_state_t modal_state = MODAL_STATE_NONE;

/* PROMPT 态控件（切 INSTALLING 态时删除） */
static lv_obj_t *prompt_body = NULL;    /* 容器：图标+标题+版本+提示+按钮 */
/* INSTALLING 态控件 */
static lv_obj_t *install_body = NULL;   /* 容器：spinner+文案 */

/* ---- 工具：图标盘子（复用 setting_app create_icon_plate 模式） ---- */
static lv_obj_t *create_icon_plate(lv_obj_t *parent, const void *icon_src)
{
    lv_obj_t *plate = lv_obj_create(parent);
    lv_obj_set_size(plate, 64, 64);
    lv_obj_set_style_bg_color(plate, lv_color_mix(UI_COLOR_PRIMARY, UI_BG_CARD, 52), 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(plate, 1, 0);
    lv_obj_set_style_border_color(plate, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_opa(plate, LV_OPA_20, 0);
    lv_obj_set_style_radius(plate, UI_RADIUS_LG, 0);
    lv_obj_set_style_pad_all(plate, 0, 0);
    lv_obj_clear_flag(plate, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_img_create(plate);
    lv_img_set_src(icon, icon_src);
    lv_obj_set_style_image_recolor(icon, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_center(icon);
    return plate;
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text,
                               lv_color_t bg, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 160, 56);
    lv_obj_set_style_radius(btn, UI_RADIUS_LG, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return btn;
}

/* 立即安装：发 APPLY + 立即切 INSTALLING 态（即时反馈，不等状态回调）。 */
static void on_install_clicked(lv_event_t *e)
{
    (void)e;
    ota_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = OTA_CMD_APPLY;
    mw_publish(TOPIC_OTA_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    cloud_ota_modal_enter_installing();
}

/* 稍后：关闭模态 + 清本次推送待安装标记（service_cloud 记延后提醒）。 */
static void on_later_clicked(lv_event_t *e)
{
    (void)e;
    cloud_ota_modal_hide();
    cloud_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = CLOUD_CMD_DISMISS_OTA;
    mw_publish(TOPIC_CLOUD_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

/* 构建卡片外壳（遮罩 + 卡片）。挂到 lv_layer_top()——跨屏全局可见，
 * 不被 app 的 lv_scr_load 切走，无论用户在哪个 app 都能弹出并置顶。
 * top layer 默认不接收点击，需置 CLICKABLE 拦截底层点击（防点穿）。 */
static lv_obj_t *build_shell(void)
{
    lv_obj_t *layer = lv_layer_top();
    modal_bg = lv_obj_create(layer);
    lv_obj_remove_style_all(modal_bg);
    lv_obj_set_size(modal_bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modal_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_bg, LV_OPA_60, 0);
    lv_obj_clear_flag(modal_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(modal_bg, LV_OBJ_FLAG_CLICKABLE);  /* 拦截点击，防点穿到底层 app */
    lv_obj_center(modal_bg);

    modal_card = lv_obj_create(modal_bg);
    lv_obj_set_size(modal_card, 520, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(modal_card, UI_BG_CARD, 0);
    lv_obj_set_style_bg_opa(modal_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(modal_card, UI_RADIUS_LG, 0);
    lv_obj_set_style_border_width(modal_card, 0, 0);
    lv_obj_set_style_shadow_width(modal_card, 24, 0);
    lv_obj_set_style_shadow_color(modal_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(modal_card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(modal_card, 32, 0);
    lv_obj_set_style_pad_row(modal_card, 18, 0);
    lv_obj_set_flex_flow(modal_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal_card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(modal_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(modal_card);
    return modal_card;
}

/* 构建 PROMPT 态内容。 */
static void build_prompt(lv_obj_t *card, const char *version, bool mandatory)
{
    prompt_body = lv_obj_create(card);
    lv_obj_remove_style_all(prompt_body);
    lv_obj_set_size(prompt_body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(prompt_body, 0, 0);
    lv_obj_set_style_pad_row(prompt_body, 16, 0);
    lv_obj_set_flex_flow(prompt_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(prompt_body, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(prompt_body, LV_OBJ_FLAG_SCROLLABLE);

    /* 图标盘子 */
    create_icon_plate(prompt_body, &refresh_28dp_1F1F1F);

    /* 标题 */
    lv_obj_t *title = lv_label_create(prompt_body);
    lv_label_set_text(title, "Update Available");
    lv_obj_set_style_text_font(title, UI_TEXT_H2, 0);
    lv_obj_set_style_text_color(title, UI_TEXT_PRIMARY, 0);

    /* 版本号 */
    lv_obj_t *ver = lv_label_create(prompt_body);
    char ver_buf[96];
    snprintf(ver_buf, sizeof(ver_buf), "New version v%s",
             (version && version[0]) ? version : "Unknown");
    lv_label_set_text(ver, ver_buf);
    lv_obj_set_style_text_font(ver, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(ver, UI_COLOR_PRIMARY, 0);

    /* 提示 */
    lv_obj_t *hint = lv_label_create(prompt_body);
    lv_label_set_text(hint, mandatory
        ? "The firmware is ready and must be installed now.\nInstallation takes 1–2 minutes. Keep power connected."
        : "The firmware is ready. Install it now?\nInstallation takes 1–2 minutes. Keep power connected.");
    lv_obj_set_style_text_font(hint, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(hint, UI_TEXT_SECONDARY, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, 420);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    /* 按钮区 */
    lv_obj_t *actions = lv_obj_create(prompt_body);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 20, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    if (!mandatory) {
        create_button(actions, "Later", lv_color_hex(0xA8B2C3), on_later_clicked);
    }
    create_button(actions, "Install Now", UI_COLOR_PRIMARY, on_install_clicked);
}

/* 清掉 PROMPT 态内容（切 INSTALLING 前调）。 */
static void clear_prompt(void)
{
    if (prompt_body) {
        lv_obj_delete(prompt_body);
        prompt_body = NULL;
    }
}

/* 构建 INSTALLING 态内容。 */
static void build_installing(lv_obj_t *card)
{
    install_body = lv_obj_create(card);
    lv_obj_remove_style_all(install_body);
    lv_obj_set_size(install_body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(install_body, 0, 0);
    lv_obj_set_style_pad_row(install_body, 20, 0);
    lv_obj_set_flex_flow(install_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(install_body, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(install_body, LV_OBJ_FLAG_SCROLLABLE);

    /* spinner（照搬 wifi_app 模式） */
    lv_obj_t *spinner = lv_spinner_create(install_body);
    lv_obj_set_size(spinner, 72, 72);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, UI_DESKTOP_BORDER, LV_PART_MAIN);

    /* 主文案 */
    lv_obj_t *title = lv_label_create(install_body);
    lv_label_set_text(title, "Installing... Keep Power Connected");
    lv_obj_set_style_text_font(title, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(title, UI_TEXT_PRIMARY, 0);

    /* 副文案 */
    lv_obj_t *sub = lv_label_create(install_body);
    lv_label_set_text(sub, "The device will restart automatically.");
    lv_obj_set_style_text_font(sub, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(sub, UI_TEXT_SECONDARY, 0);
}

void cloud_ota_modal_show(const char *version, bool mandatory)
{
    if (modal_bg) {
        /* 已显示：若正在 INSTALLING 不打断；PROMPT 态可刷新版本/mandatory。 */
        if (modal_state == MODAL_STATE_INSTALLING) return;
        return;
    }

    build_shell();
    build_prompt(modal_card, version, mandatory);
    modal_state = MODAL_STATE_PROMPT;
}

void cloud_ota_modal_enter_installing(void)
{
    if (!modal_bg) return;  /* 未显示，无需切 */
    if (modal_state == MODAL_STATE_INSTALLING) return;

    clear_prompt();
    build_installing(modal_card);
    modal_state = MODAL_STATE_INSTALLING;
}

/* 安装进度回调：进度条已移除（swupdate 刷写期间进度无法实时联动，卡住反而焦虑），
 * 现仅靠 spinner 表达"进行中"。此接口保留为空实现，供 ui_helpers 无脑调用。 */
void cloud_ota_modal_set_install_progress(int32_t progress)
{
    (void)progress;
}

void cloud_ota_modal_hide(void)
{
    if (modal_bg) {
        lv_obj_delete(modal_bg);
        modal_bg = NULL;
        modal_card = NULL;
        prompt_body = NULL;
        install_body = NULL;
        modal_state = MODAL_STATE_NONE;
    }
}

bool cloud_ota_modal_is_shown(void)
{
    return modal_bg != NULL;
}
