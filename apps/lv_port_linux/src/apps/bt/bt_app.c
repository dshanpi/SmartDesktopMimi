/**
 * @file bt_app.c
 * @brief Bluetooth Speaker App - A2DP Sink UI (蓝牙音箱界面)
 *
 * 界面功能:
 *   - 蓝牙开关
 *   - 本机设备信息（名称、MAC、可发现状态）
 *   - 已连接设备信息（设备名、音频状态、歌曲信息）
 *   - 音量滑块
 *   - 断开连接
 */

#include "bt_app.h"
#include "../../system/app_manager.h"
#include "../../system/backend_service.h"
#include "../../system/device_identity.h"
#include "../../system/settings.h"
#include "../../middleware/middleware.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"
#include "../../ui/ui_fonts.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* =========================================================================
 *                            图片资源
 * ========================================================================= */
LV_IMG_DECLARE(play_arrow_40dp_FFFFFF_FILL0_wght0_GRAD0_opsz40)
LV_IMG_DECLARE(pause_40dp_FFFFFF)
LV_IMG_DECLARE(fast_rewind_40dp_FFFFFF)
LV_IMG_DECLARE(fast_forward_40dp_FFFFFF)
LV_IMG_DECLARE(compactdisc)
LV_IMG_DECLARE(arrow_back_ios_28dp_FFFFFF)

/* =========================================================================
 *                            静态 UI 组件指针
 * ========================================================================= */

static lv_obj_t *sw_bluetooth = NULL;
static lv_obj_t *label_adapter_name = NULL;
static lv_obj_t *label_adapter_addr = NULL;

static lv_obj_t *section_connected = NULL;
static lv_obj_t *player_visual = NULL;
static lv_obj_t *cd_disc = NULL;
static lv_obj_t *label_conn_device = NULL;
static lv_obj_t *audio_status_badge = NULL;
static lv_obj_t *label_audio_status = NULL;
static lv_obj_t *label_cover_status = NULL;
static lv_obj_t *label_cover_title = NULL;
static lv_obj_t *label_track_title = NULL;
static lv_obj_t *label_track_artist = NULL;
static lv_obj_t *btn_prev = NULL;
static lv_obj_t *btn_play_pause = NULL;
static lv_obj_t *btn_next = NULL;
static lv_obj_t *icon_play_pause = NULL;
static lv_obj_t *section_playback_details = NULL;
static lv_obj_t *label_track_album = NULL;
static lv_obj_t *label_track_genre = NULL;
static lv_obj_t *label_track_time = NULL;
static lv_obj_t *label_track_time_end = NULL;
static lv_obj_t *bar_track_progress = NULL;

static lv_obj_t *slider_volume = NULL;
static lv_obj_t *label_volume = NULL;
static lv_obj_t *btn_disconnect = NULL;
static lv_obj_t *ambient_glow = NULL;
static lv_obj_t *label_source = NULL;
static lv_obj_t *label_connection_hint = NULL;
static lv_obj_t *device_details_overlay = NULL;
static lv_obj_t *device_details_sheet = NULL;
static lv_obj_t *name_edit_modal = NULL;
static lv_obj_t *name_edit_ta = NULL;
static lv_obj_t *name_edit_kb = NULL;
static lv_obj_t *label_name_edit_hint = NULL;

/* 缓存状态 */
static bt_runtime_t cached_runtime;
static bt_avrcp_info_t cached_avrcp;
static bool has_runtime = false;
static bool ignore_switch_event = false;
static bool ignore_slider_event = false;
static bool switch_toggle_pending = false;
static bool switch_toggle_target_enabled = false;
static int pending_volume = -1;
static int last_sent_volume = -1;
static bool has_avrcp = false;
static bool has_playback_state = false;
static bool avrcp_metadata_unavailable = false;
static bool cd_disc_active = false;

/* =========================================================================
 *                            辅助函数
 * ========================================================================= */

typedef struct {
    int32_t screen_w;
    int32_t screen_h;
    int32_t header_h;
    int32_t content_h;
    int32_t page_pad;
    int32_t card_pad;
    int32_t gap;
    int32_t left_col_w;
    bool compact;
} bt_layout_t;

static int32_t clamp_i32(int32_t value, int32_t min, int32_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void get_bt_layout(bt_layout_t *layout) {
    layout->screen_w = lv_display_get_horizontal_resolution(NULL);
    layout->screen_h = lv_display_get_vertical_resolution(NULL);
    if (layout->screen_w <= 0) layout->screen_w = 1024;
    if (layout->screen_h <= 0) layout->screen_h = 768;

    layout->compact = layout->screen_w < 900 || layout->screen_h < 620;
    layout->header_h = layout->compact ? UI_APP_HEADER_H_COMPACT : UI_APP_HEADER_H;
    layout->content_h = LV_MAX(1, layout->screen_h - layout->header_h);
    layout->page_pad = clamp_i32(layout->screen_w / 36, 16, 32);
    layout->card_pad = layout->compact ? 16 : 22;
    layout->gap = layout->compact ? 10 : 14;
    layout->left_col_w = clamp_i32((layout->screen_w - layout->page_pad * 2 - layout->gap) * 55 / 100,
                                   500, 560);
}

static void clear_panel_style(lv_obj_t *obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_cd_circle(lv_obj_t *parent,
                                  int32_t size,
                                  lv_color_t bg,
                                  lv_opa_t bg_opa,
                                  lv_color_t border,
                                  lv_opa_t border_opa,
                                  int32_t border_w) {
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, size, size);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, bg, 0);
    lv_obj_set_style_bg_opa(circle, bg_opa, 0);
    lv_obj_set_style_border_color(circle, border, 0);
    lv_obj_set_style_border_opa(circle, border_opa, 0);
    lv_obj_set_style_border_width(circle, border_w, 0);
    lv_obj_set_style_shadow_width(circle, 0, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    return circle;
}

static void cd_spin_anim_cb(void *var, int32_t value) {
    int32_t rotation = value % 3600;
    if (rotation < 0) rotation += 3600;
    lv_obj_set_style_transform_rotation((lv_obj_t *)var, rotation, 0);
}

static void set_cd_spinning(bool spinning) {
    if (!cd_disc) return;

    if (cd_disc_active == spinning) return;
    cd_disc_active = spinning;

    bool running = lv_anim_get(cd_disc, cd_spin_anim_cb) != NULL;
    if (!spinning) {
        if (running) lv_anim_delete(cd_disc, cd_spin_anim_cb);
        return;
    }
    if (running) lv_anim_delete(cd_disc, cd_spin_anim_cb);

    int32_t start = lv_obj_get_style_transform_rotation(cd_disc, 0) % 3600;
    if (start < 0) start += 3600;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, cd_disc);
    lv_anim_set_exec_cb(&anim, cd_spin_anim_cb);
    lv_anim_set_values(&anim, start, start + 3600);
    lv_anim_set_duration(&anim, 6500);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
}

static lv_obj_t *create_cd_deck_art(lv_obj_t *parent, int32_t disc_size) {
    enum { COMPACT_DISC_ASSET_SIZE = 300 };
    lv_obj_t *deck = lv_obj_create(parent);
    clear_panel_style(deck);
    lv_obj_set_size(deck, disc_size, disc_size);
    lv_obj_center(deck);
    lv_obj_add_flag(deck, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *disc = lv_image_create(deck);
    lv_image_set_src(disc, &compactdisc);
    lv_image_set_scale(disc, (uint32_t)(disc_size * LV_SCALE_NONE / COMPACT_DISC_ASSET_SIZE));
    lv_image_set_pivot(disc, COMPACT_DISC_ASSET_SIZE / 2, COMPACT_DISC_ASSET_SIZE / 2);
    lv_image_set_antialias(disc, true);
    lv_obj_set_style_transform_pivot_x(disc, COMPACT_DISC_ASSET_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(disc, COMPACT_DISC_ASSET_SIZE / 2, 0);
    lv_obj_center(disc);
    cd_disc = disc;
    return deck;
}

static void prepare_single_line_label(lv_obj_t *label) {
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

static void prepare_marquee_label(lv_obj_t *label, int32_t height) {
    lv_obj_set_size(label, lv_pct(100), height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(label, 9000, 0);
}

static bool metadata_text_is_placeholder(const char *text) {
    char normalized[32];
    size_t out = 0;
    bool has_visible = false;
    bool has_non_ascii = false;

    if (!text) return true;

    for (size_t i = 0; text[i] != '\0' && out + 1 < sizeof(normalized); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c > 0x20) has_visible = true;
        if (c >= 'A' && c <= 'Z') {
            normalized[out++] = (char)(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            normalized[out++] = (char)c;
        } else if (c >= 0x80) {
            has_non_ascii = true;
        }
    }
    normalized[out] = '\0';

    if (!has_visible) return true;
    if (has_non_ascii && out == 0) return false;

    return strcmp(normalized, "notprovided") == 0 ||
           strcmp(normalized, "notavailable") == 0 ||
           strcmp(normalized, "unavailable") == 0 ||
           strcmp(normalized, "unknown") == 0 ||
           strcmp(normalized, "null") == 0 ||
           strcmp(normalized, "none") == 0 ||
           strcmp(normalized, "na") == 0;
}

static void clear_metadata_placeholder(char *text, size_t text_size) {
    if (!text || text_size == 0) return;
    if (metadata_text_is_placeholder(text)) text[0] = '\0';
}

static bool sanitize_avrcp_info(bt_avrcp_info_t *info) {
    if (!info) return true;

    /* title 为 placeholder 的中间态事件已在 on_bt_avrcp_info 提前拦截、未覆盖缓存，
     * 故此处不再清空整条信息。仅清理 artist/album 等子字段的 placeholder 占位值。 */
    if (metadata_text_is_placeholder(info->title)) {
        return true;
    }

    clear_metadata_placeholder(info->artist, sizeof(info->artist));
    clear_metadata_placeholder(info->album, sizeof(info->album));
    clear_metadata_placeholder(info->genre, sizeof(info->genre));
    clear_metadata_placeholder(info->duration, sizeof(info->duration));
    return false;
}

static void format_time_ms(int32_t ms, char *buf, size_t buf_size) {
    if (ms < 0) ms = 0;
    int32_t total_sec = ms / 1000;
    int32_t min = total_sec / 60;
    int32_t sec = total_sec % 60;
    snprintf(buf, buf_size, "%d:%02d", (int)min, (int)sec);
}

static int32_t parse_ms_text(const char *text) {
    int32_t value = 0;
    if (!text) return 0;
    while (*text >= '0' && *text <= '9') {
        int32_t digit = *text - '0';
        if (value > (INT32_MAX - digit) / 10) return INT32_MAX;
        value = value * 10 + digit;
        text++;
    }
    return value;
}

static void sync_switch_checked(bool enabled) {
    if (!sw_bluetooth) return;

    bool checked = lv_obj_has_state(sw_bluetooth, LV_STATE_CHECKED);
    if (checked == enabled) return;

    ignore_switch_event = true;
    if (enabled)
        lv_obj_add_state(sw_bluetooth, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw_bluetooth, LV_STATE_CHECKED);
    ignore_switch_event = false;
}

static void clear_switch_pending(void) {
    switch_toggle_pending = false;
    if (sw_bluetooth) {
        lv_obj_clear_state(sw_bluetooth, LV_STATE_DISABLED);
    }
}

static const char *display_adapter_name(void)
{
    if (has_runtime && cached_runtime.adapter_name[0]) {
        return cached_runtime.adapter_name;
    }
    return APP_DEVICE_NAME;
}

static void close_name_edit_modal(void)
{
    if (name_edit_modal) {
        lv_obj_delete(name_edit_modal);
        name_edit_modal = NULL;
        name_edit_ta = NULL;
        name_edit_kb = NULL;
        label_name_edit_hint = NULL;
    }
}

static void send_set_adapter_name(const char *name)
{
    bt_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = BT_CMD_SET_ADAPTER_NAME;
    if (name && name[0]) {
        snprintf(cmd.adapter_name, sizeof(cmd.adapter_name), "%s", name);
    }
    mw_publish(TOPIC_BT_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static void name_edit_save_cb(lv_event_t *e)
{
    const char *text;
    LV_UNUSED(e);
    if (!name_edit_ta) {
        close_name_edit_modal();
        return;
    }
    text = lv_textarea_get_text(name_edit_ta);
    send_set_adapter_name(text);
    close_name_edit_modal();
}

static void name_edit_reset_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    /* Empty name restores auto name: product + MAC suffix. */
    send_set_adapter_name("");
    close_name_edit_modal();
}

static void name_edit_cancel_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    close_name_edit_modal();
}

static void name_edit_keyboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        name_edit_save_cb(e);
    } else if (code == LV_EVENT_CANCEL) {
        close_name_edit_modal();
    }
}

static void show_name_edit_modal(lv_event_t *e)
{
    lv_obj_t *dialog;
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *btn_row;
    lv_obj_t *btn;
    lv_obj_t *lbl;
    sys_settings_t *settings = sys_settings_get();
    const char *current;
    int32_t screen_h;
    int32_t dialog_w;
    int32_t btn_w;

    LV_UNUSED(e);
    if (name_edit_modal) return;

    /* 用 top layer，避免被设备详情抽屉/其它顶层控件压住。 */
    name_edit_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(name_edit_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(name_edit_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(name_edit_modal, LV_OPA_50, 0);
    lv_obj_set_style_radius(name_edit_modal, 0, 0);
    lv_obj_set_style_border_width(name_edit_modal, 0, 0);
    lv_obj_set_style_outline_width(name_edit_modal, 0, 0);
    lv_obj_set_style_shadow_width(name_edit_modal, 0, 0);
    lv_obj_set_style_pad_all(name_edit_modal, 0, 0);
    lv_obj_clear_flag(name_edit_modal, LV_OBJ_FLAG_SCROLLABLE);

    screen_h = lv_display_get_vertical_resolution(NULL);
    if (screen_h <= 0) screen_h = 768;
    dialog_w = screen_h < 620 ? 480 : 540;
    btn_w = screen_h < 620 ? 120 : 140;

    /*
     * 键盘约占半屏高度。对话框贴顶 + 内容高度自适应，
     * 避免矮屏上键盘盖住底部「取消/保存」按钮。
     */
    dialog = lv_obj_create(name_edit_modal);
    lv_obj_set_width(dialog, dialog_w);
    lv_obj_set_height(dialog, LV_SIZE_CONTENT);
    lv_obj_align(dialog, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_radius(dialog, 20, 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0x334155), 0);
    lv_obj_set_style_shadow_width(dialog, 24, 0);
    lv_obj_set_style_shadow_color(dialog, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(dialog, LV_OPA_40, 0);
    lv_obj_set_style_pad_left(dialog, 18, 0);
    lv_obj_set_style_pad_right(dialog, 18, 0);
    lv_obj_set_style_pad_top(dialog, 16, 0);
    lv_obj_set_style_pad_bottom(dialog, 16, 0);
    lv_obj_set_style_pad_row(dialog, 10, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(dialog);
    lv_label_set_text(title, "Rename Bluetooth Device");
    lv_obj_set_style_text_font(title, ui_font_h2(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);

    hint = lv_label_create(dialog);
    label_name_edit_hint = hint;
    lv_label_set_text(hint,
                      "This name appears in your phone's Bluetooth list. "
                      "Choose Auto Name to use the product name and last four MAC digits.");
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(hint, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_line_space(hint, 2, 0);

    name_edit_ta = lv_textarea_create(dialog);
    lv_textarea_set_one_line(name_edit_ta, true);
    lv_textarea_set_max_length(name_edit_ta, 32);
    lv_obj_set_width(name_edit_ta, lv_pct(100));
    lv_obj_set_height(name_edit_ta, 52);
    lv_obj_set_style_text_font(name_edit_ta, ui_font_h3(), 0);
    lv_obj_set_style_radius(name_edit_ta, 12, 0);
    lv_obj_set_style_bg_color(name_edit_ta, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_color(name_edit_ta, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_color(name_edit_ta, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(name_edit_ta, 1, 0);
    lv_obj_set_style_pad_all(name_edit_ta, 12, 0);
    /*
     * 默认主题光标是深色左边框，只在 FOCUSED 时生效；深色输入框上看不见。
     * 强制浅色光标 + 闪烁，并主动聚焦。
     */
    lv_obj_set_style_border_color(name_edit_ta, lv_color_hex(0xF8FAFC),
                                  LV_PART_CURSOR);
    lv_obj_set_style_border_color(name_edit_ta, lv_color_hex(0xF8FAFC),
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(name_edit_ta, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_width(name_edit_ta, 2,
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(name_edit_ta, LV_BORDER_SIDE_LEFT,
                                 LV_PART_CURSOR);
    lv_obj_set_style_border_side(name_edit_ta, LV_BORDER_SIDE_LEFT,
                                 LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(name_edit_ta, 400, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(name_edit_ta, 400,
                                   LV_PART_CURSOR | LV_STATE_FOCUSED);
    current = (settings && settings->bluetooth_name[0]) ?
              settings->bluetooth_name : display_adapter_name();
    lv_textarea_set_text(name_edit_ta, current ? current : "");
    lv_textarea_set_placeholder_text(name_edit_ta, "Example: Living Room Speaker");
    lv_textarea_set_cursor_pos(name_edit_ta, LV_TEXTAREA_CURSOR_LAST);

    btn_row = lv_obj_create(dialog);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 10, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    btn = lv_button_create(btn_row);
    lv_obj_set_size(btn, btn_w, 44);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x334155), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, name_edit_cancel_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Cancel");
    lv_obj_set_style_text_font(lbl, ui_font_body_lg(), 0);
    lv_obj_center(lbl);

    btn = lv_button_create(btn_row);
    lv_obj_set_size(btn, btn_w, 44);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x475569), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, name_edit_reset_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Auto Name");
    lv_obj_set_style_text_font(lbl, ui_font_body_lg(), 0);
    lv_obj_center(lbl);

    btn = lv_button_create(btn_row);
    lv_obj_set_size(btn, btn_w, 44);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, name_edit_save_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Save");
    lv_obj_set_style_text_font(lbl, ui_font_body_lg(), 0);
    lv_obj_center(lbl);

    name_edit_kb = lv_keyboard_create(name_edit_modal);
    lv_keyboard_set_textarea(name_edit_kb, name_edit_ta);
    /* 限制键盘高度，给顶部对话框留出可点按钮区域。 */
    lv_obj_set_size(name_edit_kb, LV_PCT(100),
                    screen_h < 620 ? LV_PCT(42) : LV_PCT(40));
    lv_obj_align(name_edit_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(name_edit_kb, name_edit_keyboard_cb, LV_EVENT_ALL, NULL);

    /* keyboard_set_textarea 会设 FOCUSED；再强调一次，确保光标动画启动。 */
    lv_obj_add_state(name_edit_ta, LV_STATE_FOCUSED);
    lv_obj_invalidate(name_edit_ta);
}

static void set_volume_controls_enabled(bool enabled) {
    if (slider_volume) {
        if (enabled)
            lv_obj_clear_state(slider_volume, LV_STATE_DISABLED);
        else
            lv_obj_add_state(slider_volume, LV_STATE_DISABLED);
    }

    if (label_volume) {
        lv_obj_set_style_text_opa(label_volume, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    }
}

static bool playback_is_active(void) {
    /* 后端播放状态到达后以其为准。audio_streaming 仅用于首个状态包
     * 到达前兜底，避免手机暂停时旧的 A2DP 流状态压住新 AVRCP 状态。 */
    return has_playback_state ? cached_avrcp.is_playing : cached_runtime.audio_streaming;
}

static bool player_controls_available(void) {
    return cached_runtime.a2dp_connected && cached_avrcp.player_available;
}

static void update_transport_icon(void) {
    if (!icon_play_pause) return;

    lv_image_set_src(icon_play_pause,
        playback_is_active() ? &pause_40dp_FFFFFF :
                               &play_arrow_40dp_FFFFFF_FILL0_wght0_GRAD0_opsz40);
}

static void set_transport_controls_enabled(bool enabled) {
    lv_obj_t *buttons[] = { btn_prev, btn_play_pause, btn_next };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (!buttons[i]) continue;
        if (enabled)
            lv_obj_clear_state(buttons[i], LV_STATE_DISABLED);
        else
            lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
    }
}

static void update_playback_details(void) {
    bool connected = player_controls_available();
    int32_t total = cached_avrcp.song_len_ms;
    int32_t pos = cached_avrcp.song_pos_ms;

    if (connected && total <= 0 && cached_avrcp.duration[0])
        total = parse_ms_text(cached_avrcp.duration);
    if (pos < 0) pos = 0;
    if (total > 0 && pos > total) pos = total;

    if (label_track_album) {
        const char *album = connected ? "Album  Not available" : "Album  Waiting for metadata";
        char album_buf[128];
        if (connected && cached_avrcp.album[0]) album = cached_avrcp.album;
        if (connected && cached_avrcp.album[0]) {
            snprintf(album_buf, sizeof(album_buf), "Album  %s", album);
            album = album_buf;
        }
        lv_label_set_text(label_track_album, album);
    }
    if (label_track_genre) {
        char meta_buf[192];
        const char *artist = connected && cached_avrcp.artist[0] ? cached_avrcp.artist : "Not available";
        const char *genre = connected && cached_avrcp.genre[0] ? cached_avrcp.genre : "Not available";
        if (!connected) {
            lv_label_set_text(label_track_genre, "Artist  Waiting for metadata");
        } else {
            snprintf(meta_buf, sizeof(meta_buf), "Artist  %s  Genre  %s", artist, genre);
            lv_label_set_text(label_track_genre, meta_buf);
        }
    }
    if (bar_track_progress) {
        int value = connected && total > 0 ? (int)((pos * 100) / total) : 0;
        lv_slider_set_value(bar_track_progress, value, LV_ANIM_OFF);
    }
    if (label_track_time) {
        char cur[16];
        char end[16];
        format_time_ms(connected ? pos : 0, cur, sizeof(cur));
        format_time_ms(connected ? total : 0, end, sizeof(end));
        lv_label_set_text(label_track_time, cur);
        if (label_track_time_end) lv_label_set_text(label_track_time_end, end);
    }

    if (section_playback_details) {
        lv_obj_set_style_opa(section_playback_details, connected ? LV_OPA_COVER : LV_OPA_70, 0);
    }
}

static void update_player_visual(lv_color_t accent) {
    if (!player_visual) return;

    bool connected = cached_runtime.a2dp_connected;
    bool active = playback_is_active();
    uint8_t mix = active ? 26 : (connected ? 18 : 8);
    lv_opa_t border_opa = active ? LV_OPA_50 : (connected ? LV_OPA_30 : LV_OPA_20);

    lv_obj_set_style_bg_color(player_visual, lv_color_mix(accent, lv_color_hex(0x111827), mix), 0);
    lv_obj_set_style_bg_grad_color(player_visual, lv_color_hex(0x080B12), 0);
    lv_obj_set_style_bg_grad_dir(player_visual, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(player_visual, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(player_visual, accent, 0);
    lv_obj_set_style_border_opa(player_visual, border_opa, 0);
    lv_obj_set_style_shadow_color(player_visual, accent, 0);
    lv_obj_set_style_shadow_opa(player_visual, active ? LV_OPA_20 : LV_OPA_10, 0);
    if (ambient_glow) {
        lv_obj_set_style_bg_color(ambient_glow, accent, 0);
        lv_obj_set_style_bg_opa(ambient_glow,
                                active ? LV_OPA_20 : (connected ? LV_OPA_10 : LV_OPA_0), 0);
        lv_obj_set_style_border_color(ambient_glow, accent, 0);
        lv_obj_set_style_border_opa(ambient_glow, active ? LV_OPA_30 : LV_OPA_10, 0);
    }
    set_cd_spinning(active);
}

static void update_cover_card_text(void) {
    bool active = playback_is_active();

    if (label_cover_status) {
        lv_label_set_text(label_cover_status, active ? "SPINNING" : "VINYL DECK");
        lv_obj_set_style_text_color(label_cover_status,
                                    active ? UI_COLOR_PRIMARY : lv_color_hex(0x94A3B8),
                                    0);
    }

    if (label_cover_title) {
        const char *line = "Start music on your phone";
        if (!cached_runtime.enabled) {
            line = "Bluetooth is off";
        } else if (active) {
            line = (has_avrcp && cached_avrcp.title[0]) ? cached_avrcp.title : "Bluetooth Audio";
        } else if (cached_runtime.a2dp_connected) {
            line = "Start music on your phone";
        }
        lv_label_set_text(label_cover_title, line);
    }
}

static void set_audio_status(const char *text, lv_color_t accent) {
    if (label_audio_status) {
        lv_label_set_text(label_audio_status, text);
        lv_obj_set_style_text_color(label_audio_status, lv_color_hex(0xF8FAFC), 0);
    }
    if (audio_status_badge) {
        lv_obj_set_style_bg_color(audio_status_badge, lv_color_mix(accent, lv_color_hex(0x111827), 56), 0);
        lv_obj_set_style_border_color(audio_status_badge, accent, 0);
    }
    update_cover_card_text();
    update_player_visual(accent);
}

static void start_switch_pending(bool target_enabled) {
    switch_toggle_pending = true;
    switch_toggle_target_enabled = target_enabled;

    if (sw_bluetooth) {
        lv_obj_add_state(sw_bluetooth, LV_STATE_DISABLED);
    }
}

static void update_ui_from_runtime(void) {
    if (!has_runtime) return;
    bool active = playback_is_active();

    /* 蓝牙开关 */
    sync_switch_checked(cached_runtime.enabled);
    if (switch_toggle_pending) {
        if (cached_runtime.enabled == switch_toggle_target_enabled) {
            sys_settings_t *settings = sys_settings_get();
            if (settings->bluetooth_enabled != cached_runtime.enabled) {
                settings->bluetooth_enabled = cached_runtime.enabled;
                sys_settings_save();
            }
            clear_switch_pending();
        } else {
            clear_switch_pending();
        }
    }

    /* 适配器信息 */
    if (label_adapter_name) {
        lv_label_set_text(label_adapter_name,
            cached_runtime.adapter_name[0] ? cached_runtime.adapter_name : "Not initialized");
    }
    if (label_adapter_addr) {
        lv_label_set_text(label_adapter_addr,
            cached_runtime.adapter_addr[0] ? cached_runtime.adapter_addr : "--:--:--:--:--:--");
    }
    /* 已连接设备区域 */
    if (cached_runtime.a2dp_connected) {
        bool player_available = cached_avrcp.player_available;
        if (section_connected) lv_obj_clear_flag(section_connected, LV_OBJ_FLAG_HIDDEN);
        if (btn_disconnect)   lv_obj_clear_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
        if (label_conn_device) {
            lv_label_set_text(label_conn_device,
                              cached_runtime.connected_device_name[0] ?
                              cached_runtime.connected_device_name : "Connected device");
        }
        if (label_source) {
            char source[128];
            snprintf(source, sizeof(source), "%.96s  ·  Bluetooth",
                     cached_runtime.connected_device_name[0] ?
                     cached_runtime.connected_device_name : "Connected device");
            lv_label_set_text(label_source, source);
        }
        if (label_connection_hint) {
            lv_label_set_text(label_connection_hint,
                              !player_available ? "Open a music player on your phone" :
                              (active ? "Audio from the connected phone" : "Paused"));
        }
        const char *title = !player_available ? "Phone player is not open" :
                            (avrcp_metadata_unavailable ? "Playback information unavailable" : "Connected · Ready to play");
        if (player_available && has_avrcp && cached_avrcp.title[0])
            title = cached_avrcp.title;
        else if (player_available && !avrcp_metadata_unavailable && active)
            title = "Playing Bluetooth audio";
        if (label_track_title) {
            lv_label_set_text(label_track_title, title);
        }
        if (label_cover_title) lv_label_set_text(label_cover_title, title);
        if (label_track_artist) {
            const char *artist = !player_available ? "Open music on your phone to enable controls" :
                                 (avrcp_metadata_unavailable ? "Artist unavailable" : "Bluetooth Audio");
            if (player_available && has_avrcp && cached_avrcp.artist[0])
                artist = cached_avrcp.artist;
            lv_label_set_text(label_track_artist, artist);
        }
        set_audio_status(active ? "Playing" : (player_available ? "Paused" : "Connected"),
                         active ? UI_COLOR_PRIMARY :
                         (player_available ? UI_TEXT_SECONDARY : lv_color_hex(0x16A34A)));
    } else {
        has_avrcp = false;
        has_playback_state = false;
        avrcp_metadata_unavailable = false;
        memset(&cached_avrcp, 0, sizeof(cached_avrcp));
        if (section_connected) lv_obj_clear_flag(section_connected, LV_OBJ_FLAG_HIDDEN);
        if (btn_disconnect)    lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
        if (label_conn_device) lv_label_set_text(label_conn_device, "Bluetooth audio input");
        if (label_source) {
            char source[96];
            snprintf(source, sizeof(source), "%s  ·  Waiting for connection", display_adapter_name());
            lv_label_set_text(label_source, source);
        }
        if (label_connection_hint) {
            if (cached_runtime.enabled) {
                char hint[128];
                snprintf(hint, sizeof(hint), "Select %s in your phone's Bluetooth settings",
                         display_adapter_name());
                lv_label_set_text(label_connection_hint, hint);
            } else {
                lv_label_set_text(label_connection_hint, "Turn on Bluetooth to connect a phone");
            }
        }
        set_audio_status(cached_runtime.enabled ? "Waiting for connection" : "Off",
                         cached_runtime.enabled ? UI_COLOR_PRIMARY : UI_TEXT_SECONDARY);
        if (label_track_title) {
            lv_label_set_text(label_track_title, "Bluetooth Audio");
        }
        if (label_track_artist) {
            lv_label_set_text(label_track_artist,
                cached_runtime.enabled ? "A2DP Sink" : "Bluetooth is off");
        }
    }
    update_playback_details();
    set_transport_controls_enabled(player_controls_available());
    update_transport_icon();

    /* 音量滑块 */
    if (slider_volume) {
        ignore_slider_event = true;
        lv_slider_set_value(slider_volume, cached_runtime.volume, LV_ANIM_OFF);
        ignore_slider_event = false;
    }
    if (label_volume) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", cached_runtime.volume);
        lv_label_set_text(label_volume, buf);
    }
    last_sent_volume = cached_runtime.volume;
    pending_volume = -1;
    set_volume_controls_enabled(cached_runtime.a2dp_connected);
}

static void update_ui_from_avrcp(void) {
    if (!cached_runtime.a2dp_connected) {
        if (label_conn_device) lv_label_set_text(label_conn_device, "Bluetooth audio input");
        if (label_track_title)  lv_label_set_text(label_track_title, "Bluetooth Audio");
        if (label_track_artist) lv_label_set_text(label_track_artist, "A2DP Sink");
        set_audio_status("Waiting for connection", UI_COLOR_PRIMARY);
        set_transport_controls_enabled(false);
        update_transport_icon();
        return;
    }

    if (!cached_avrcp.player_available) {
        has_avrcp = false;
        avrcp_metadata_unavailable = false;
        if (label_track_title) lv_label_set_text(label_track_title, "Phone player is not open");
        if (label_cover_title) lv_label_set_text(label_cover_title, "Phone player is not open");
        if (label_track_artist) lv_label_set_text(label_track_artist, "Open music on your phone to enable controls");
        if (label_connection_hint) lv_label_set_text(label_connection_hint, "Open a music player on your phone");
        update_transport_icon();
        set_audio_status("Connected", lv_color_hex(0x16A34A));
        update_playback_details();
        set_transport_controls_enabled(false);
        return;
    }

    const char *title = cached_avrcp.title[0] ? cached_avrcp.title :
        (avrcp_metadata_unavailable ? "Playback information unavailable" :
         (cached_avrcp.is_playing ? "Playing Bluetooth audio" : "Connected · Ready to play"));
    if (label_track_title) {
        lv_label_set_text(label_track_title, title);
    }
    if (label_cover_title) lv_label_set_text(label_cover_title, title);
    if (label_track_artist) {
        const char *artist = cached_avrcp.artist[0] ? cached_avrcp.artist :
            (avrcp_metadata_unavailable ? "Artist unavailable" : "From connected phone");
        lv_label_set_text(label_track_artist, artist);
    }
    if (label_connection_hint) {
        lv_label_set_text(label_connection_hint,
                          cached_avrcp.is_playing ? "Audio from the connected phone" : "Paused");
    }
    /* 图标是用户最先感知的反馈，优先于卡片色彩和唱片状态刷新。 */
    update_transport_icon();
    set_audio_status(cached_avrcp.is_playing ? "Playing" : "Paused",
                     cached_avrcp.is_playing ? UI_COLOR_PRIMARY : UI_TEXT_SECONDARY);
    update_playback_details();
    set_transport_controls_enabled(player_controls_available());
}

/* =========================================================================
 *                            IPC 回调
 * ========================================================================= */

static void on_bt_runtime(const mw_msg_t *msg) {
    if (msg->data_len != sizeof(bt_runtime_t)) return;
    memcpy(&cached_runtime, msg->data, sizeof(bt_runtime_t));
    has_runtime = true;
    update_ui_from_runtime();
}

static void on_bt_avrcp_info(const mw_msg_t *msg) {
    if (msg->data_len != sizeof(bt_avrcp_info_t)) return;

    const bt_avrcp_info_t *incoming = (const bt_avrcp_info_t *)msg->data;
    has_playback_state = true;

    /* AVRCP 切歌/切应用时常先发 title 为空或 "Not Provided" 的中间态事件。
     * 若用其覆盖缓存，会把上次已识别的元数据抹掉；后续真实标题若因 A2DP 流
     * 中断未到达，UI 就卡在"暂无信息"。故 placeholder 中间态只更新播放状态等
     * 非元数据字段，保留上次的 title/artist/album。 */
    if (metadata_text_is_placeholder(incoming->title) && incoming->player_available) {
        bool was_playing = cached_avrcp.is_playing;
        bool was_available = cached_avrcp.player_available;
        bt_playback_state_t was_state = cached_avrcp.playback_state;
        cached_avrcp.is_playing = incoming->is_playing;
        cached_avrcp.player_available = incoming->player_available;
        cached_avrcp.playback_state = incoming->playback_state;
        cached_avrcp.song_len_ms = incoming->song_len_ms;
        cached_avrcp.song_pos_ms = incoming->song_pos_ms;
        /* 播放状态或播放器可用性变化都必须刷新 UI。 */
        if (was_playing != incoming->is_playing ||
            was_available != incoming->player_available ||
            was_state != incoming->playback_state) {
            update_ui_from_avrcp();
        }
        return;
    }

    memcpy(&cached_avrcp, incoming, sizeof(bt_avrcp_info_t));
    avrcp_metadata_unavailable = sanitize_avrcp_info(&cached_avrcp);
    has_avrcp = cached_avrcp.title[0] || cached_avrcp.artist[0] ||
                cached_avrcp.album[0] || cached_avrcp.genre[0] ||
                cached_avrcp.song_len_ms > 0 || cached_avrcp.song_pos_ms > 0;
    update_ui_from_avrcp();
}

/* =========================================================================
 *                            事件处理
 * ========================================================================= */

static void on_back_click(lv_event_t *e) {
    LV_UNUSED(e);
    app_manager_back_home();
}

static void hide_device_details(void) {
    if (!device_details_overlay) return;
    lv_obj_add_flag(device_details_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void on_device_details_click(lv_event_t *e) {
    LV_UNUSED(e);
    if (!device_details_overlay) return;
    lv_obj_clear_flag(device_details_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(device_details_overlay);
}

static void on_device_details_close(lv_event_t *e) {
    LV_UNUSED(e);
    hide_device_details();
}

static void on_device_overlay_click(lv_event_t *e) {
    if (lv_event_get_target(e) == device_details_overlay) hide_device_details();
}

static void on_switch_changed(lv_event_t *e) {
    if (ignore_switch_event) return;
    if (switch_toggle_pending) {
        sync_switch_checked(switch_toggle_target_enabled);
        return;
    }

    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    bt_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = on ? BT_CMD_ENABLE : BT_CMD_DISABLE;
    start_switch_pending(on);
    mw_publish(TOPIC_BT_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static void on_slider_changed(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code != LV_EVENT_VALUE_CHANGED &&
        code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST) {
        return;
    }

    if (ignore_slider_event) return;
    if (!cached_runtime.a2dp_connected) {
        if (slider_volume) {
            ignore_slider_event = true;
            lv_slider_set_value(slider_volume, cached_runtime.volume, LV_ANIM_OFF);
            ignore_slider_event = false;
        }
        return;
    }

    lv_obj_t *slider = lv_event_get_target(e);
    int vol = (int)lv_slider_get_value(slider);
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    if (label_volume) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", vol);
        lv_label_set_text(label_volume, buf);
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        pending_volume = vol;
        return;
    }

    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }

    if (pending_volume >= 0) {
        vol = pending_volume;
    }
    if (vol == last_sent_volume) {
        pending_volume = -1;
        return;
    }

    bt_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = BT_CMD_SET_VOLUME;
    cmd.volume = vol;
    mw_publish(TOPIC_BT_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    last_sent_volume = vol;
    pending_volume = -1;
}

static void on_disconnect_click(lv_event_t *e) {
    LV_UNUSED(e);
    bt_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = BT_CMD_DISCONNECT;
    memcpy(cmd.bd_addr, cached_runtime.connected_device_addr, sizeof(cmd.bd_addr));
    mw_publish(TOPIC_BT_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    hide_device_details();
}

static void on_transport_click(lv_event_t *e) {
    if (!player_controls_available()) return;

    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    bt_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    if (action == BT_CMD_PLAY) {
        cmd.action = playback_is_active() ? BT_CMD_PAUSE : BT_CMD_PLAY;
    } else {
        cmd.action = action;
    }

    mw_publish(TOPIC_BT_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static lv_obj_t *create_transport_button(lv_obj_t *parent,
                                         int32_t size,
                                         const void *icon_src,
                                         bool primary,
                                         int action,
                                         lv_obj_t **icon_out) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(btn, primary ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(btn, primary ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn,
        primary ? UI_COLOR_PRIMARY : lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(btn, primary ? UI_COLOR_PRIMARY : lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_opa(btn, primary ? LV_OPA_TRANSP : LV_OPA_30, 0);
    lv_obj_set_style_bg_color(btn, lv_color_mix(UI_COLOR_PRIMARY, lv_color_black(), 210), LV_STATE_PRESSED);
    lv_obj_set_style_opa(btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, on_transport_click, LV_EVENT_CLICKED, (void *)(intptr_t)action);

    lv_obj_t *icon = lv_image_create(btn);
    lv_image_set_src(icon, icon_src);
    if (!primary) {
        lv_obj_set_style_image_recolor(icon, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    }
    lv_obj_center(icon);
    if (icon_out) *icon_out = icon;
    return btn;
}

/* =========================================================================
 *                            界面构建
 * ========================================================================= */

void bt_app_init(void) {
    printf("[BT App] Init\n");

    bt_layout_t layout;
    get_bt_layout(&layout);
    ui_fonts_init();

    memset(&cached_runtime, 0, sizeof(cached_runtime));
    memset(&cached_avrcp,   0, sizeof(cached_avrcp));
    has_runtime = false;
    has_avrcp = false;
    has_playback_state = false;
    avrcp_metadata_unavailable = false;
    cd_disc_active = false;

    /* 订阅 IPC */
    mw_subscribe(TOPIC_BT_RUNTIME, on_bt_runtime);
    mw_subscribe(TOPIC_BT_AVRCP_INFO, on_bt_avrcp_info);

    /* Midnight Hi-Fi：蓝牙页是一张播放场景，不是常驻设备设置表单。 */
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x070A10), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x121A28), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 静态环境光。避免实时模糊，在软件渲染下成本可控。 */
    lv_obj_t *bg_glow_left = lv_obj_create(scr);
    lv_obj_set_size(bg_glow_left, 420, 420);
    lv_obj_set_pos(bg_glow_left, -150, 310);
    lv_obj_set_style_radius(bg_glow_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bg_glow_left, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(bg_glow_left, LV_OPA_10, 0);
    lv_obj_set_style_border_width(bg_glow_left, 0, 0);
    lv_obj_clear_flag(bg_glow_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bg_glow_left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bg_glow_right = lv_obj_create(scr);
    lv_obj_set_size(bg_glow_right, 360, 360);
    lv_obj_set_pos(bg_glow_right, 820, -170);
    lv_obj_set_style_radius(bg_glow_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bg_glow_right, lv_color_hex(0xD9A441), 0);
    lv_obj_set_style_bg_opa(bg_glow_right, LV_OPA_10, 0);
    lv_obj_set_style_border_width(bg_glow_right, 0, 0);
    lv_obj_clear_flag(bg_glow_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bg_glow_right, LV_OBJ_FLAG_SCROLLABLE);

    /* ===== 透明顶栏 ===== */
    const int32_t header_h = layout.compact ? 68 : 76;
    lv_obj_t *header = lv_obj_create(scr);
    clear_panel_style(header);
    lv_obj_set_size(header, lv_pct(100), header_h);
    lv_obj_set_pos(header, 0, 0);

    lv_obj_t *back_btn = lv_button_create(header);
    lv_obj_set_size(back_btn, 52, 52);
    lv_obj_set_pos(back_btn, layout.page_pad, (header_h - 52) / 2);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x172033), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x26344D), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_border_opa(back_btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_image_create(back_btn);
    lv_image_set_src(back_icon, &arrow_back_ios_28dp_FFFFFF);
    lv_obj_center(back_icon);

    lv_obj_t *brand = lv_obj_create(header);
    clear_panel_style(brand);
    lv_obj_set_size(brand, 310, 52);
    lv_obj_set_pos(brand, layout.page_pad + 68, (header_h - 52) / 2);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(brand, 0, 0);

    lv_obj_t *brand_title = lv_label_create(brand);
    lv_label_set_text(brand_title, "BLUETOOTH HI-FI");
    lv_obj_set_style_text_font(brand_title, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(brand_title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_letter_space(brand_title, 2, 0);

    lv_obj_t *brand_subtitle = lv_label_create(brand);
    lv_label_set_text(brand_subtitle, "AITVBox Sound System");
    lv_obj_set_style_text_font(brand_subtitle, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(brand_subtitle, lv_color_hex(0x64748B), 0);

    audio_status_badge = lv_obj_create(header);
    lv_obj_set_size(audio_status_badge, LV_SIZE_CONTENT, 38);
    lv_obj_align(audio_status_badge, LV_ALIGN_RIGHT_MID, -layout.page_pad, 0);
    lv_obj_set_style_radius(audio_status_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(audio_status_badge, lv_color_hex(0x172033), 0);
    lv_obj_set_style_bg_opa(audio_status_badge, LV_OPA_80, 0);
    lv_obj_set_style_border_width(audio_status_badge, 1, 0);
    lv_obj_set_style_border_color(audio_status_badge, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_opa(audio_status_badge, LV_OPA_30, 0);
    lv_obj_set_style_pad_left(audio_status_badge, 16, 0);
    lv_obj_set_style_pad_right(audio_status_badge, 16, 0);
    lv_obj_set_style_pad_top(audio_status_badge, 0, 0);
    lv_obj_set_style_pad_bottom(audio_status_badge, 0, 0);
    lv_obj_clear_flag(audio_status_badge, LV_OBJ_FLAG_SCROLLABLE);

    label_audio_status = lv_label_create(audio_status_badge);
    lv_label_set_text(label_audio_status, "Waiting for connection");
    lv_obj_set_style_text_font(label_audio_status, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label_audio_status, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(label_audio_status);

    /* ===== 主内容区 ===== */
    lv_obj_t *content = lv_obj_create(scr);
    clear_panel_style(content);
    lv_obj_set_size(content, lv_pct(100), layout.screen_h - header_h);
    lv_obj_set_pos(content, 0, header_h);
    lv_obj_set_style_pad_left(content, layout.page_pad, 0);
    lv_obj_set_style_pad_right(content, layout.page_pad, 0);
    lv_obj_set_style_pad_top(content, layout.compact ? 12 : 14, 0);
    lv_obj_set_style_pad_bottom(content, layout.compact ? 16 : 24, 0);
    lv_obj_set_flex_flow(content, layout.compact ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(content, layout.compact ? 18 : 24, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t slider_track_h = 10;
    const int32_t stage_w = layout.compact ? layout.screen_w - layout.page_pad * 2 : 470;

    /* --- 左侧：单一 Hi-Fi 舞台 --- */
    section_connected = lv_obj_create(content);
    lv_obj_set_size(section_connected, stage_w,
                    layout.compact ? 500 : lv_pct(100));
    lv_obj_set_flex_flow(section_connected, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(section_connected, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(section_connected, layout.compact ? 18 : 26, 0);
    lv_obj_set_style_pad_gap(section_connected, layout.compact ? 10 : 14, 0);
    lv_obj_set_style_radius(section_connected, layout.compact ? 26 : 34, 0);
    lv_obj_set_style_bg_color(section_connected, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_grad_color(section_connected, lv_color_hex(0x080B12), 0);
    lv_obj_set_style_bg_grad_dir(section_connected, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(section_connected, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(section_connected, 1, 0);
    lv_obj_set_style_border_color(section_connected, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_opa(section_connected, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(section_connected, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(section_connected, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(section_connected, 24, 0);
    lv_obj_set_style_shadow_offset_y(section_connected, 10, 0);
    lv_obj_clear_flag(section_connected, LV_OBJ_FLAG_SCROLLABLE);

    player_visual = section_connected;

    label_cover_status = lv_label_create(section_connected);
    lv_label_set_text(label_cover_status, "VINYL DECK");
    lv_obj_set_style_text_font(label_cover_status, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(label_cover_status, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_letter_space(label_cover_status, 3, 0);
    lv_obj_set_width(label_cover_status, lv_pct(100));
    lv_obj_set_style_text_align(label_cover_status, LV_TEXT_ALIGN_CENTER, 0);

    const int32_t disc_stage_size = layout.compact ? 276 : 338;
    lv_obj_t *disc_stage = lv_obj_create(section_connected);
    clear_panel_style(disc_stage);
    lv_obj_set_size(disc_stage, disc_stage_size, disc_stage_size);
    lv_obj_add_flag(disc_stage, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    ambient_glow = create_cd_circle(disc_stage,
                                    disc_stage_size,
                                    UI_COLOR_PRIMARY,
                                    LV_OPA_10,
                                    UI_COLOR_PRIMARY,
                                    LV_OPA_20,
                                    2);
    lv_obj_center(ambient_glow);

    int32_t disc_size = layout.compact ? 236 : 320;
    create_cd_deck_art(disc_stage, disc_size);

    lv_obj_t *deck_footer = lv_obj_create(section_connected);
    clear_panel_style(deck_footer);
    lv_obj_set_size(deck_footer, lv_pct(76), 24);
    lv_obj_set_flex_flow(deck_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(deck_footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *deck_speed = lv_label_create(deck_footer);
    lv_label_set_text(deck_speed, "33 RPM");
    lv_obj_set_style_text_font(deck_speed, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(deck_speed, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_letter_space(deck_speed, 1, 0);

    lv_obj_t *deck_input = lv_label_create(deck_footer);
    lv_label_set_text(deck_input, "A2DP AUDIO");
    lv_obj_set_style_text_font(deck_input, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(deck_input, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_letter_space(deck_input, 1, 0);

    /* --- 右侧：Now Playing 信息与直接控制 --- */
    lv_obj_t *right_col = lv_obj_create(content);
    clear_panel_style(right_col);
    lv_obj_set_size(right_col, layout.compact ? lv_pct(100) : 1,
                    layout.compact ? 520 : lv_pct(100));
    if (!layout.compact) lv_obj_set_flex_grow(right_col, 1);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(right_col, layout.compact ? 10 : 18, 0);
    lv_obj_set_style_pad_left(right_col, layout.compact ? 0 : 10, 0);
    lv_obj_set_style_pad_right(right_col, layout.compact ? 0 : 4, 0);
    lv_obj_set_style_pad_gap(right_col, layout.compact ? 10 : 12, 0);
    section_playback_details = right_col;

    label_source = lv_label_create(right_col);
    {
        char source[96];
        snprintf(source, sizeof(source), "%s  ·  Waiting for connection", APP_DEVICE_NAME);
        lv_label_set_text(label_source, source);
    }
    lv_obj_set_style_text_font(label_source, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(label_source, UI_COLOR_PRIMARY, 0);
    prepare_single_line_label(label_source);

    label_track_title = lv_label_create(right_col);
    lv_label_set_text(label_track_title, "Bluetooth Audio");
    lv_obj_set_size(label_track_title, lv_pct(100), layout.compact ? 58 : 86);
    lv_label_set_long_mode(label_track_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_track_title, ui_font_clock_meta(), 0);
    lv_obj_set_style_text_color(label_track_title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_line_space(label_track_title, 4, 0);
    /* 舞台不再重复显示曲名；避免暂停时占位文案覆盖右侧真实 AVRCP 标题。 */
    label_cover_title = NULL;

    label_track_artist = lv_label_create(right_col);
    lv_label_set_text(label_track_artist, "Waiting for phone");
    lv_obj_set_style_text_font(label_track_artist, ui_font_h2(), 0);
    lv_obj_set_style_text_color(label_track_artist, lv_color_hex(0xCBD5E1), 0);
    prepare_single_line_label(label_track_artist);

    label_connection_hint = lv_label_create(right_col);
    {
        char hint[128];
        snprintf(hint, sizeof(hint), "Select %s in your phone's Bluetooth settings", APP_DEVICE_NAME);
        lv_label_set_text(label_connection_hint, hint);
    }
    lv_obj_set_style_text_font(label_connection_hint, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label_connection_hint, lv_color_hex(0x64748B), 0);
    prepare_single_line_label(label_connection_hint);

    label_track_album = lv_label_create(right_col);
    lv_label_set_text(label_track_album, "Album  Waiting for metadata");
    lv_obj_set_style_text_font(label_track_album, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label_track_album, lv_color_hex(0x94A3B8), 0);
    prepare_marquee_label(label_track_album, 24);

    lv_obj_t *progress_area = lv_obj_create(right_col);
    clear_panel_style(progress_area);
    lv_obj_set_size(progress_area, lv_pct(100), 58);
    lv_obj_set_flex_flow(progress_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(progress_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(progress_area, 8, 0);

    bar_track_progress = lv_slider_create(progress_area);
    lv_obj_set_size(bar_track_progress, lv_pct(100), 8);
    lv_slider_set_range(bar_track_progress, 0, 100);
    lv_slider_set_value(bar_track_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_track_progress, lv_color_hex(0x263247), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_track_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_track_progress, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_track_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_track_progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_track_progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_track_progress, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(bar_track_progress, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *time_row = lv_obj_create(progress_area);
    clear_panel_style(time_row);
    lv_obj_set_size(time_row, lv_pct(100), 24);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label_track_time = lv_label_create(time_row);
    lv_label_set_text(label_track_time, "0:00");
    lv_obj_set_style_text_font(label_track_time, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label_track_time, lv_color_hex(0x94A3B8), 0);

    label_track_time_end = lv_label_create(time_row);
    lv_label_set_text(label_track_time_end, "0:00");
    lv_obj_set_style_text_font(label_track_time_end, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label_track_time_end, lv_color_hex(0x94A3B8), 0);

    lv_obj_t *transport_buttons = lv_obj_create(right_col);
    clear_panel_style(transport_buttons);
    lv_obj_set_size(transport_buttons, lv_pct(100), layout.compact ? 76 : 92);
    lv_obj_set_flex_flow(transport_buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transport_buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(transport_buttons, layout.compact ? 24 : 30, 0);

    int32_t small_transport = layout.compact ? 52 : 58;
    int32_t main_transport = layout.compact ? 68 : 80;
    btn_prev = create_transport_button(transport_buttons,
                                       small_transport,
                                       &fast_rewind_40dp_FFFFFF,
                                       false,
                                       BT_CMD_PREV,
                                       NULL);
    btn_play_pause = create_transport_button(transport_buttons,
                                             main_transport,
                                             &play_arrow_40dp_FFFFFF_FILL0_wght0_GRAD0_opsz40,
                                             true,
                                             BT_CMD_PLAY,
                                             &icon_play_pause);
    btn_next = create_transport_button(transport_buttons,
                                       small_transport,
                                       &fast_forward_40dp_FFFFFF,
                                       false,
                                       BT_CMD_NEXT,
                                       NULL);
    set_transport_controls_enabled(false);
    update_transport_icon();

    lv_obj_t *volume_panel = lv_obj_create(right_col);
    lv_obj_set_size(volume_panel, lv_pct(100), layout.compact ? 104 : 116);
    lv_obj_set_style_radius(volume_panel, 22, 0);
    lv_obj_set_style_bg_color(volume_panel, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(volume_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(volume_panel, 1, 0);
    lv_obj_set_style_border_color(volume_panel, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_opa(volume_panel, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(volume_panel, 0, 0);
    lv_obj_set_style_pad_all(volume_panel, 16, 0);
    lv_obj_set_flex_flow(volume_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(volume_panel, 10, 0);
    lv_obj_clear_flag(volume_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *vol_header = lv_obj_create(volume_panel);
    clear_panel_style(vol_header);
    lv_obj_set_size(vol_header, lv_pct(100), 34);
    lv_obj_set_flex_flow(vol_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_vol_title = lv_label_create(vol_header);
    lv_label_set_text(label_vol_title, "OUTPUT VOLUME");
    lv_obj_set_style_text_font(label_vol_title, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label_vol_title, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_letter_space(label_vol_title, 1, 0);

    label_volume = lv_label_create(vol_header);
    lv_label_set_text(label_volume, "50%");
    lv_obj_set_style_text_font(label_volume, ui_font_h2(), 0);
    lv_obj_set_style_text_color(label_volume, lv_color_hex(0xF8FAFC), 0);

    slider_volume = lv_slider_create(volume_panel);
    lv_obj_set_size(slider_volume, lv_pct(100), slider_track_h);
    lv_slider_set_range(slider_volume, 0, 100);
    lv_slider_set_value(slider_volume, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_volume, lv_color_hex(0x263247), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider_volume, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_volume, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider_volume, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider_volume, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_volume, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider_volume, 0, LV_PART_KNOB);
    lv_obj_set_style_transform_width(slider_volume, 9, LV_PART_KNOB);
    lv_obj_set_style_transform_height(slider_volume, 9, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_volume, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider_volume, lv_color_hex(0xF8FAFC), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider_volume, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider_volume, UI_COLOR_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider_volume, 10, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider_volume, LV_OPA_30, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_volume, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider_volume, on_slider_changed, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider_volume, on_slider_changed, LV_EVENT_PRESS_LOST, NULL);
    set_volume_controls_enabled(false);

    lv_obj_t *details_btn = lv_button_create(right_col);
    lv_obj_set_size(details_btn, lv_pct(100), 48);
    lv_obj_set_style_radius(details_btn, 16, 0);
    lv_obj_set_style_bg_color(details_btn, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_color(details_btn, lv_color_hex(0x1E293B), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(details_btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(details_btn, 1, 0);
    lv_obj_set_style_border_color(details_btn, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_opa(details_btn, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(details_btn, 0, 0);
    lv_obj_add_event_cb(details_btn, on_device_details_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *details_label = lv_label_create(details_btn);
    lv_label_set_text(details_label, "Device Details & Connections  ›");
    lv_obj_set_style_text_font(details_label, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(details_label, lv_color_hex(0xCBD5E1), 0);
    lv_obj_center(details_label);

    /* ===== 设备详情底部抽屉 ===== */
    /* Full-screen scrim: clear default theme radius so corners are square. */
    device_details_overlay = lv_obj_create(scr);
    lv_obj_set_size(device_details_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(device_details_overlay, 0, 0);
    lv_obj_set_style_bg_color(device_details_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(device_details_overlay, LV_OPA_60, 0);
    lv_obj_set_style_radius(device_details_overlay, 0, 0);
    lv_obj_set_style_border_width(device_details_overlay, 0, 0);
    lv_obj_set_style_outline_width(device_details_overlay, 0, 0);
    lv_obj_set_style_shadow_width(device_details_overlay, 0, 0);
    lv_obj_set_style_pad_all(device_details_overlay, 0, 0);
    lv_obj_clear_flag(device_details_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(device_details_overlay, on_device_overlay_click, LV_EVENT_CLICKED, NULL);

    device_details_sheet = lv_obj_create(device_details_overlay);
    lv_obj_set_size(device_details_sheet, lv_pct(100), layout.compact ? 360 : 318);
    lv_obj_align(device_details_sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(device_details_sheet, 30, 0);
    lv_obj_set_style_bg_color(device_details_sheet, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_grad_color(device_details_sheet, lv_color_hex(0x182235), 0);
    lv_obj_set_style_bg_grad_dir(device_details_sheet, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(device_details_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(device_details_sheet, 1, 0);
    lv_obj_set_style_border_color(device_details_sheet, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_opa(device_details_sheet, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(device_details_sheet, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(device_details_sheet, 30, 0);
    lv_obj_set_style_shadow_opa(device_details_sheet, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(device_details_sheet, 28, 0);
    lv_obj_clear_flag(device_details_sheet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sheet_title = lv_label_create(device_details_sheet);
    lv_label_set_text(sheet_title, "Bluetooth Device Management");
    lv_obj_set_style_text_font(sheet_title, ui_font_h1(), 0);
    lv_obj_set_style_text_color(sheet_title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(sheet_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *sheet_close = lv_button_create(device_details_sheet);
    lv_obj_set_size(sheet_close, 90, 42);
    lv_obj_align(sheet_close, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_radius(sheet_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sheet_close, lv_color_hex(0x263247), 0);
    lv_obj_set_style_shadow_width(sheet_close, 0, 0);
    lv_obj_add_event_cb(sheet_close, on_device_details_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sheet_close_label = lv_label_create(sheet_close);
    lv_label_set_text(sheet_close_label, "Done");
    lv_obj_set_style_text_font(sheet_close_label, ui_font_body_lg(), 0);
    lv_obj_set_style_text_color(sheet_close_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(sheet_close_label);

    lv_obj_t *sheet_info = lv_obj_create(device_details_sheet);
    clear_panel_style(sheet_info);
    lv_obj_set_size(sheet_info, lv_pct(100), 116);
    lv_obj_set_pos(sheet_info, 0, 62);
    lv_obj_set_flex_flow(sheet_info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sheet_info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(sheet_info, 18, 0);

    lv_obj_t *info_name = lv_obj_create(sheet_info);
    lv_obj_set_size(info_name, 1, lv_pct(100));
    lv_obj_set_flex_grow(info_name, 1);
    lv_obj_set_style_radius(info_name, 18, 0);
    lv_obj_set_style_bg_color(info_name, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(info_name, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(info_name, 1, 0);
    lv_obj_set_style_border_color(info_name, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(info_name, 16, 0);
    lv_obj_set_flex_flow(info_name, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(info_name, 5, 0);
    lv_obj_add_flag(info_name, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(info_name, show_name_edit_modal, LV_EVENT_CLICKED, NULL);
    lv_obj_t *info_name_title = lv_label_create(info_name);
    lv_label_set_text(info_name_title, "Bluetooth name · Tap to edit");
    lv_obj_set_style_text_font(info_name_title, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(info_name_title, lv_color_hex(0x64748B), 0);
    prepare_single_line_label(info_name_title);
    label_adapter_name = lv_label_create(info_name);
    lv_label_set_text(label_adapter_name, "Not initialized");
    lv_obj_set_style_text_font(label_adapter_name, ui_font_h3(), 0);
    lv_obj_set_style_text_color(label_adapter_name, lv_color_hex(0xF8FAFC), 0);
    prepare_single_line_label(label_adapter_name);

    lv_obj_t *info_mac = lv_obj_create(sheet_info);
    lv_obj_set_size(info_mac, 1, lv_pct(100));
    lv_obj_set_flex_grow(info_mac, 1);
    lv_obj_set_style_radius(info_mac, 18, 0);
    lv_obj_set_style_bg_color(info_mac, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(info_mac, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(info_mac, 1, 0);
    lv_obj_set_style_border_color(info_mac, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(info_mac, 16, 0);
    lv_obj_set_flex_flow(info_mac, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(info_mac, 5, 0);
    lv_obj_t *info_mac_title = lv_label_create(info_mac);
    lv_label_set_text(info_mac_title, "MAC Address");
    lv_obj_set_style_text_font(info_mac_title, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(info_mac_title, lv_color_hex(0x64748B), 0);
    label_adapter_addr = lv_label_create(info_mac);
    lv_label_set_text(label_adapter_addr, "--:--:--:--:--:--");
    lv_obj_set_style_text_font(label_adapter_addr, ui_font_h3(), 0);
    lv_obj_set_style_text_color(label_adapter_addr, lv_color_hex(0xF8FAFC), 0);
    prepare_single_line_label(label_adapter_addr);

    lv_obj_t *sheet_actions = lv_obj_create(device_details_sheet);
    clear_panel_style(sheet_actions);
    lv_obj_set_size(sheet_actions, lv_pct(100), 64);
    lv_obj_align(sheet_actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(sheet_actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sheet_actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sheet_actions, 20, 0);

    lv_obj_t *switch_title = lv_label_create(sheet_actions);
    lv_label_set_text(switch_title, "Bluetooth");
    lv_obj_set_style_text_font(switch_title, ui_font_h3(), 0);
    lv_obj_set_style_text_color(switch_title, lv_color_hex(0xF8FAFC), 0);

    sw_bluetooth = lv_switch_create(sheet_actions);
    lv_obj_set_size(sw_bluetooth, 64, 32);
    lv_obj_set_style_bg_color(sw_bluetooth, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw_bluetooth, UI_COLOR_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw_bluetooth, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(sw_bluetooth, on_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *action_spacer = lv_obj_create(sheet_actions);
    clear_panel_style(action_spacer);
    lv_obj_set_size(action_spacer, 1, 1);
    lv_obj_set_flex_grow(action_spacer, 1);

    label_conn_device = lv_label_create(sheet_actions);
    lv_label_set_text(label_conn_device, "Bluetooth audio input");
    lv_obj_set_width(label_conn_device, 250);
    lv_label_set_long_mode(label_conn_device, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label_conn_device, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(label_conn_device, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label_conn_device, lv_color_hex(0x94A3B8), 0);

    btn_disconnect = lv_button_create(sheet_actions);
    lv_obj_set_size(btn_disconnect, 124, 44);
    lv_obj_set_style_radius(btn_disconnect, 14, 0);
    lv_obj_set_style_bg_color(btn_disconnect, lv_color_hex(0x3A151A), 0);
    lv_obj_set_style_bg_color(btn_disconnect, lv_color_hex(0x571C22), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_disconnect, 1, 0);
    lv_obj_set_style_border_color(btn_disconnect, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_border_opa(btn_disconnect, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(btn_disconnect, 0, 0);
    lv_obj_add_flag(btn_disconnect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(btn_disconnect, on_disconnect_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_label = lv_label_create(btn_disconnect);
    lv_label_set_text(disconnect_label, "Disconnect");
    lv_obj_set_style_text_font(disconnect_label, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(disconnect_label, lv_color_hex(0xFCA5A5), 0);
    lv_obj_center(disconnect_label);

    lv_obj_add_flag(device_details_overlay, LV_OBJ_FLAG_HIDDEN);

    update_playback_details();
    set_audio_status("Waiting for connection", UI_COLOR_PRIMARY);

    lv_scr_load(scr);

    /* 拉取初始状态 */
    bt_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = BT_CMD_GET_STATUS;
    mw_publish(TOPIC_BT_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

void bt_app_close(void) {
    printf("[BT App] Close\n");

    mw_unsubscribe(TOPIC_BT_RUNTIME, on_bt_runtime);
    mw_unsubscribe(TOPIC_BT_AVRCP_INFO, on_bt_avrcp_info);
    if (cd_disc) lv_anim_delete(cd_disc, cd_spin_anim_cb);
    close_name_edit_modal();

    sw_bluetooth = NULL;
    label_adapter_name = NULL;
    label_adapter_addr = NULL;
    section_connected = NULL;
    player_visual = NULL;
    cd_disc = NULL;
    label_conn_device = NULL;
    audio_status_badge = NULL;
    label_audio_status = NULL;
    label_cover_status = NULL;
    label_cover_title = NULL;
    label_track_title = NULL;
    label_track_artist = NULL;
    btn_prev = NULL;
    btn_play_pause = NULL;
    btn_next = NULL;
    icon_play_pause = NULL;
    section_playback_details = NULL;
    label_track_album = NULL;
    label_track_genre = NULL;
    label_track_time = NULL;
    label_track_time_end = NULL;
    bar_track_progress = NULL;
    slider_volume = NULL;
    label_volume = NULL;
    btn_disconnect = NULL;
    ambient_glow = NULL;
    label_source = NULL;
    label_connection_hint = NULL;
    device_details_overlay = NULL;
    device_details_sheet = NULL;
    has_runtime = false;
    ignore_switch_event = false;
    switch_toggle_pending = false;
    switch_toggle_target_enabled = false;
    pending_volume = -1;
    last_sent_volume = -1;
    has_avrcp = false;
    has_playback_state = false;
    avrcp_metadata_unavailable = false;
    cd_disc_active = false;
}

AppDescriptor app_bt = {
    .id    = APP_ID_BT,
    .name  = "Bluetooth Audio",
    .init  = bt_app_init,
    .close = bt_app_close
};
