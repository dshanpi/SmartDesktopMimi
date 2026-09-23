/* ota_app.c — 系统升级（OTA）UI 页面。
 *
 * 仿 ai_app.c 模式：init 时订阅 TOPIC_OTA_STATUS 并拉取初始状态，close 时取消订阅
 * 并置空 widget 指针。按钮经 mw_publish(TOPIC_OTA_COMMAND, ...) 下发指令到 backend
 * 的 service_ota。状态回调在 LVGL 线程（mw_process_ui_messages 的 10ms 定时器）执行，
 * 可直接更新 widget。
 */

#include "ota_app.h"

#include "../../middleware/middleware.h"
#include "../../system/app_manager.h"
#include "../../system/backend_types.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"

#include <stdio.h>
#include <string.h>


static lv_obj_t *label_state = NULL;
static lv_obj_t *label_hint = NULL;
static lv_obj_t *label_ver_local = NULL;
static lv_obj_t *label_ver_remote = NULL;
static lv_obj_t *bar_progress = NULL;
static lv_obj_t *label_progress = NULL;
static lv_obj_t *btn_check = NULL;
static lv_obj_t *btn_download = NULL;
static lv_obj_t *btn_apply = NULL;

static const char *ota_state_text(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_IDLE:             return "Idle";
    case OTA_STATE_CHECKING:         return "Checking for updates";
    case OTA_STATE_UPDATE_AVAILABLE: return "Update available";
    case OTA_STATE_UP_TO_DATE:       return "Up to date";
    case OTA_STATE_DOWNLOADING:      return "Downloading";
    case OTA_STATE_DOWNLOAD_DONE:    return "Ready to install";
    case OTA_STATE_APPLYING:         return "Installing";
    case OTA_STATE_REBOOTING:        return "Restarting soon";
    case OTA_STATE_COMMITTING:       return "Finalizing update";
    case OTA_STATE_ERROR:            return "Update failed";
    default:                         return "Unknown";
    }
}

/* action 取 OTA_CMD_* 常量（匿名枚举，故用 int）。 */
static void send_ota_cmd(int action)
{
    ota_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = action;
    mw_publish(TOPIC_OTA_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static void update_buttons(const ota_status_t *status)
{
    if (!btn_check || !btn_download || !btn_apply) return;

    bool can_check = status->state == OTA_STATE_IDLE ||
                     status->state == OTA_STATE_UP_TO_DATE ||
                     status->state == OTA_STATE_UPDATE_AVAILABLE ||
                     status->state == OTA_STATE_ERROR;
    bool can_download = status->state == OTA_STATE_UPDATE_AVAILABLE;
    bool can_apply = status->state == OTA_STATE_DOWNLOAD_DONE;

    if (can_check) lv_obj_clear_state(btn_check, LV_STATE_DISABLED);
    else           lv_obj_add_state(btn_check, LV_STATE_DISABLED);

    if (can_download) lv_obj_clear_state(btn_download, LV_STATE_DISABLED);
    else              lv_obj_add_state(btn_download, LV_STATE_DISABLED);

    if (can_apply) lv_obj_clear_state(btn_apply, LV_STATE_DISABLED);
    else           lv_obj_add_state(btn_apply, LV_STATE_DISABLED);
}

static void on_ota_status(const mw_msg_t *msg)
{
    if (msg->topic != TOPIC_OTA_STATUS || msg->data_len < sizeof(ota_status_t)) return;
    const ota_status_t *status = (const ota_status_t *)msg->data;

    if (label_state) {
        lv_label_set_text(label_state, ota_state_text(status->state));
    }

    if (label_hint) {
        if (status->state == OTA_STATE_ERROR) {
            lv_label_set_text_fmt(label_hint, "Error code: %d", status->error_code);
        } else if (status->state == OTA_STATE_UPDATE_AVAILABLE) {
            lv_label_set_text(label_hint, "A new version is ready to download.");
        } else if (status->state == OTA_STATE_UP_TO_DATE) {
            lv_label_set_text(label_hint, "Your firmware is up to date.");
        } else {
            lv_label_set_text(label_hint, "");
        }
    }

    if (label_ver_local && status->version_local[0]) {
        lv_label_set_text(label_ver_local, status->version_local);
    }
    if (label_ver_remote) {
        lv_label_set_text(label_ver_remote,
                          status->version_remote[0] ? status->version_remote : "—");
    }

    if (bar_progress) {
        lv_bar_set_value(bar_progress, status->progress, LV_ANIM_OFF);
    }
    if (label_progress) {
        lv_label_set_text_fmt(label_progress, "%d%%", status->progress);
    }

    update_buttons(status);
}

static void back_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        /* 返回设置页：OTA 是独立 app，open SETTING 会 close 本页并重建设置屏幕。 */
        app_manager_open(APP_ID_SETTING);
    }
}

static void check_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        send_ota_cmd(OTA_CMD_CHECK);
    }
}

static void download_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        send_ota_cmd(OTA_CMD_DOWNLOAD);
    }
}

static void apply_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        send_ota_cmd(OTA_CMD_APPLY);
    }
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 180, 60);
    lv_obj_set_style_radius(btn, UI_RADIUS_LG, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xA8B2C3), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return btn;
}

/* 单行“标题 + 值”信息行。 */
static lv_obj_t *create_info_row(lv_obj_t *parent, const char *title, lv_obj_t **out_value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *key = lv_label_create(row);
    lv_label_set_text(key, title);
    lv_obj_set_style_text_font(key, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(key, UI_TEXT_SECONDARY, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, "—");
    lv_obj_set_style_text_font(val, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(val, UI_TEXT_PRIMARY, 0);

    if (out_value) *out_value = val;
    return row;
}

void ota_app_init(void)
{
    lv_obj_t *scr;
    lv_obj_t *content, *card, *card_title, *state_row, *hint_row;
    lv_obj_t *actions;

    mw_subscribe(TOPIC_OTA_STATUS, on_ota_status);

    int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    int32_t screen_h = lv_display_get_vertical_resolution(NULL);
    if (screen_w <= 0) screen_w = 1024;
    if (screen_h <= 0) screen_h = 768;

    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_BG_DESKTOP, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶栏 */
    ui_create_app_header(scr, "System Update", back_event_handler, false, NULL);

    /* 内容区 */
    content = lv_obj_create(scr);
    lv_obj_set_size(content, lv_pct(100), LV_MAX(1, screen_h - UI_APP_HEADER_H));
    lv_obj_set_pos(content, 0, UI_APP_HEADER_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, UI_SPACE_XL, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    card = lv_obj_create(content);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, UI_BG_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UI_DESKTOP_BORDER, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_LG, 0);
    lv_obj_set_style_pad_all(card, UI_SPACE_LG, 0);
    lv_obj_set_style_pad_row(card, UI_SPACE_MD, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    card_title = lv_label_create(card);
    lv_label_set_text(card_title, "Firmware Update");
    lv_obj_set_style_text_font(card_title, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(card_title, UI_TEXT_PRIMARY, 0);

    /* 当前状态 */
    state_row = lv_obj_create(card);
    lv_obj_set_size(state_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(state_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state_row, 0, 0);
    lv_obj_set_style_pad_all(state_row, 0, 0);
    lv_obj_set_style_pad_column(state_row, UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(state_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(state_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *state_key = lv_label_create(state_row);
    lv_label_set_text(state_key, "Status:");
    lv_obj_set_style_text_font(state_key, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(state_key, UI_TEXT_SECONDARY, 0);

    label_state = lv_label_create(state_row);
    lv_label_set_text(label_state, "—");
    lv_obj_set_style_text_font(label_state, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(label_state, UI_TEXT_PRIMARY, 0);

    /* 提示行 */
    hint_row = lv_obj_create(card);
    lv_obj_set_size(hint_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hint_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hint_row, 0, 0);
    lv_obj_set_style_pad_all(hint_row, 0, 0);
    lv_obj_clear_flag(hint_row, LV_OBJ_FLAG_SCROLLABLE);
    label_hint = lv_label_create(hint_row);
    lv_label_set_text(label_hint, "");
    lv_obj_set_style_text_font(label_hint, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(label_hint, UI_TEXT_SECONDARY, 0);
    lv_label_set_long_mode(label_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_hint, lv_pct(100));

    /* 版本信息 */
    create_info_row(card, "Current version:", &label_ver_local);
    create_info_row(card, "Latest version:", &label_ver_remote);

    /* 进度条 */
    lv_obj_t *prog_row = lv_obj_create(card);
    lv_obj_set_size(prog_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(prog_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prog_row, 0, 0);
    lv_obj_set_style_pad_all(prog_row, 0, 0);
    lv_obj_set_style_pad_column(prog_row, UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(prog_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prog_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(prog_row, LV_OBJ_FLAG_SCROLLABLE);

    bar_progress = lv_bar_create(prog_row);
    lv_obj_set_size(bar_progress, 1, 10);
    lv_obj_set_flex_grow(bar_progress, 1);
    lv_bar_set_range(bar_progress, 0, 100);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_progress, UI_DESKTOP_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_progress, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_progress, UI_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_progress, UI_RADIUS_SM, LV_PART_INDICATOR);

    label_progress = lv_label_create(prog_row);
    lv_label_set_text(label_progress, "0%");
    lv_obj_set_style_text_font(label_progress, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(label_progress, UI_TEXT_SECONDARY, 0);

    /* 操作按钮 */
    actions = lv_obj_create(card);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, UI_SPACE_MD, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    btn_check = create_button(actions, "Check for Updates", check_event_handler);
    btn_download = create_button(actions, "Download", download_event_handler);
    btn_apply = create_button(actions, "Install Now", apply_event_handler);

    /* 初始禁用下载/安装，等待状态回调按实际状态启用。 */
    lv_obj_add_state(btn_download, LV_STATE_DISABLED);
    lv_obj_add_state(btn_apply, LV_STATE_DISABLED);

    lv_scr_load(scr);

    /* 拉取当前 OTA 状态（backend 会回传 TOPIC_OTA_STATUS）。 */
    send_ota_cmd(OTA_CMD_GET_STATUS);
}

void ota_app_close(void)
{
    mw_unsubscribe(TOPIC_OTA_STATUS, on_ota_status);
    label_state = NULL;
    label_hint = NULL;
    label_ver_local = NULL;
    label_ver_remote = NULL;
    bar_progress = NULL;
    label_progress = NULL;
    btn_check = NULL;
    btn_download = NULL;
    btn_apply = NULL;
}

AppDescriptor app_ota = {
    .id = APP_ID_OTA,
    .name = "System Update",
    .init = ota_app_init,
    .close = ota_app_close,
};
