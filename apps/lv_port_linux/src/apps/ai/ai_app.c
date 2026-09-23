#include "ai_app.h"
#include "ai_free_chat_view.h"

#include "../../middleware/middleware.h"
#include "../../system/app_manager.h"
#include "../../system/backend_types.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"
#include "../../ui/ui_fonts.h"

#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(aibot_app)

#define FREE_CHAT_ENTER_TIMEOUT_MS 6000U

static lv_obj_t *label_state = NULL;
static lv_obj_t *label_hint = NULL;
static lv_obj_t *label_text = NULL;
static lv_obj_t *assistant_orb = NULL;
static lv_obj_t *wave_bars[5] = {0};
static lv_obj_t *btn_free_chat = NULL;
static lv_obj_t *btn_free_chat_label = NULL;
static lv_obj_t *free_chat_feedback_label = NULL;
static lv_obj_t *bind_modal = NULL;
static lv_obj_t *bind_qrcode = NULL;
static bool bind_modal_dismissed = false;
static char dismissed_bind_url[AI_BIND_URL_MAX] = {0};
static char modal_bind_url[AI_BIND_URL_MAX] = {0};
static bool wave_anim_running = false;
static bool orb_breathe_running = false;
static bool latest_tuya_bound = false;
static bool free_chat_enter_pending = false;
static bool free_chat_enter_failed = false;
static bool conversation_activity_seen = false;
static bool has_latest_status = false;
static ai_status_t latest_status;
static lv_timer_t *free_chat_enter_timer = NULL;

static void set_wave_active(bool active, lv_color_t accent);
static void set_orb_breathe(bool active);
static void free_chat_exit_requested(void *user_data);
static void maybe_show_bind_modal(const ai_status_t *status);
static void hide_bind_modal(void);
static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb);

static const char *ai_state_text(ai_state_t state)
{
    switch (state) {
        case AI_STATE_IDLE: return "Ready";
        case AI_STATE_CONNECTING: return "Starting assistant";
        case AI_STATE_LISTENING: return "Listening";
        case AI_STATE_THINKING: return "Thinking";
        case AI_STATE_SPEAKING: return "Speaking";
        case AI_STATE_NETWORK_UNAVAILABLE: return "Wi-Fi disconnected";
        case AI_STATE_ERROR: return "Service error";
        default: return "Unknown";
    }
}

static void send_ai_cmd(ai_cmd_action_t action)
{
    ai_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.action = action;
    mw_publish(TOPIC_AI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static void update_buttons(const ai_status_t *status)
{
    bool can_free_chat;
    const char *button_text = "Open Free Chat";
    const char *feedback_text = "";

    if (!status) return;

    if (free_chat_enter_pending) {
        button_text = "Opening...";
        feedback_text = "Waiting for the assistant";
        can_free_chat = false;
    } else {
        can_free_chat = status->control_center_running &&
                        status->tuya_bound &&
                        !status->free_chat_active &&
                        status->state == AI_STATE_IDLE;

        if (!status->tuya_bound) {
            button_text = "Pair Device First";
            feedback_text = "Pair this device to continue";
        } else if (status->state == AI_STATE_NETWORK_UNAVAILABLE) {
            button_text = "Connect to Wi-Fi";
            feedback_text = "Wi-Fi is required";
        } else if (status->state == AI_STATE_ERROR) {
            button_text = "Service Unavailable";
            feedback_text = "Tuya service unavailable. Try again later.";
        } else if (!status->control_center_running ||
                   status->state == AI_STATE_CONNECTING) {
            button_text = "Assistant Starting";
            feedback_text = "Connecting to Tuya Assistant";
        } else if (status->state == AI_STATE_LISTENING ||
                   status->state == AI_STATE_THINKING ||
                   status->state == AI_STATE_SPEAKING) {
            button_text = "Conversation in Progress";
            feedback_text = "Wait for the current conversation to finish";
        } else if (free_chat_enter_failed) {
            feedback_text = "Could not open Free Chat. Try again.";
        }

        if (status->free_chat_active ||
            status->chat_mode == AI_CHAT_MODE_FREE) {
            button_text = "Free Chat Active";
            feedback_text = "";
            can_free_chat = false;
        }
    }

    if (btn_free_chat_label) {
        lv_label_set_text(btn_free_chat_label, button_text);
    }
    if (free_chat_feedback_label) {
        lv_label_set_text(free_chat_feedback_label, feedback_text);
    }

    if (btn_free_chat) {
        if (can_free_chat) {
            lv_obj_clear_state(btn_free_chat, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_free_chat, LV_STATE_DISABLED);
        }
    }
}

static lv_color_t ai_state_color(ai_state_t state)
{
    switch (state) {
        case AI_STATE_IDLE:
        case AI_STATE_CONNECTING:
        case AI_STATE_LISTENING:
        case AI_STATE_THINKING:
        case AI_STATE_SPEAKING:
            return UI_COLOR_PRIMARY;
        case AI_STATE_NETWORK_UNAVAILABLE: return lv_color_hex(0x8A94A6);
        case AI_STATE_ERROR: return lv_color_hex(0xE04F5F);
        default: return UI_COLOR_PRIMARY;
    }
}

static const char *ai_hint_text(const ai_status_t *status)
{
    if (!status) return "Loading Tuya status";

    if (!status->tuya_bound) {
        return status->bind_url[0] ?
               "Scan with the Smart Life app to pair this device" :
               "Retrieving pairing status";
    }

    switch (status->state) {
        case AI_STATE_NETWORK_UNAVAILABLE:
            return "Connect to Wi-Fi";
        case AI_STATE_CONNECTING:
            return "Starting the Tuya voice service";
        case AI_STATE_IDLE:
            return status->control_center_running ?
                   "Say “Ni Hao Tuya” to wake me" :
                   "Tuya service is starting";
        case AI_STATE_LISTENING:
            return "I'm listening";
        case AI_STATE_THINKING:
            return "Thinking";
        case AI_STATE_SPEAKING:
            return "Replying";
        case AI_STATE_ERROR:
            return "Tuya service error";
        default:
            return "Syncing Tuya status";
    }
}

static void update_visual_state(const ai_status_t *status)
{
    lv_color_t accent;

    if (!status) return;
    accent = ai_state_color(status->state);

    if (label_state) {
        lv_obj_set_style_text_color(label_state, accent, 0);
    }

    if (assistant_orb) {
        lv_obj_set_style_bg_color(assistant_orb, lv_color_mix(accent, lv_color_white(), 72), 0);
        lv_obj_set_style_bg_grad_color(assistant_orb,
                                       lv_color_mix(accent,
                                                    lv_color_white(), 112), 0);
        lv_obj_set_style_border_color(assistant_orb, accent, 0);
        lv_obj_set_style_shadow_color(assistant_orb, accent, 0);
    }

    set_wave_active(status->state == AI_STATE_LISTENING ||
                    status->state == AI_STATE_SPEAKING,
                    accent);
    set_orb_breathe(status->state == AI_STATE_IDLE ||
                     status->state == AI_STATE_THINKING);
}

static void cancel_free_chat_enter_timer(void)
{
    if (free_chat_enter_timer) {
        lv_timer_del(free_chat_enter_timer);
        free_chat_enter_timer = NULL;
    }
}

static void free_chat_enter_timeout_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    free_chat_enter_timer = NULL;
    free_chat_enter_pending = false;
    free_chat_enter_failed = true;

    if (ai_free_chat_view_is_open()) {
        ai_free_chat_view_close();
    }

    if (has_latest_status) update_buttons(&latest_status);
    send_ai_cmd(AI_CMD_EXIT_FREE_CHAT);
    send_ai_cmd(AI_CMD_GET_STATUS);
}

static void on_ai_status(const mw_msg_t *msg)
{
    const ai_status_t *status;

    if (msg->topic != TOPIC_AI_STATUS || msg->data_len < sizeof(ai_status_t)) return;
    status = (const ai_status_t *)msg->data;
    latest_status = *status;
    has_latest_status = true;
    latest_tuya_bound = status->tuya_bound;

    if (status->state == AI_STATE_LISTENING ||
        status->state == AI_STATE_THINKING ||
        status->state == AI_STATE_SPEAKING) {
        conversation_activity_seen = true;
    }

    if (status->free_chat_active || status->chat_mode == AI_CHAT_MODE_FREE) {
        cancel_free_chat_enter_timer();
        free_chat_enter_pending = false;
        free_chat_enter_failed = false;
        if (!ai_free_chat_view_is_open()) {
            ai_free_chat_view_open(free_chat_exit_requested, NULL);
        }
    } else if (free_chat_enter_pending &&
               (status->state == AI_STATE_NETWORK_UNAVAILABLE ||
                status->state == AI_STATE_ERROR)) {
        cancel_free_chat_enter_timer();
        free_chat_enter_pending = false;
        free_chat_enter_failed = true;
    }

    if (label_state) {
        lv_label_set_text(label_state, ai_state_text(status->state));
    }

    if (label_hint) {
        lv_label_set_text(label_hint, ai_hint_text(status));
    }

    update_visual_state(status);
    update_buttons(status);
    maybe_show_bind_modal(status);

    if (ai_free_chat_view_is_open()) {
        ai_free_chat_view_set_status(status);
    }
}

static void on_ai_text(const mw_msg_t *msg)
{
    const ai_text_t *text;

    if (msg->topic != TOPIC_AI_TEXT || msg->data_len < sizeof(ai_text_t)) return;
    text = (const ai_text_t *)msg->data;

    if (label_text && text->text[0] && conversation_activity_seen) {
        lv_label_set_text(label_text, text->text);
        lv_obj_set_style_text_color(label_text, UI_TEXT_PRIMARY, 0);
    }

    if (ai_free_chat_view_is_open() && text->text[0]) {
        ai_free_chat_view_set_text(text->text);
    }
}

static void free_chat_exit_requested(void *user_data)
{
    LV_UNUSED(user_data);
    send_ai_cmd(AI_CMD_EXIT_FREE_CHAT);
}

static void back_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (free_chat_enter_pending) {
            cancel_free_chat_enter_timer();
            free_chat_enter_pending = false;
            send_ai_cmd(AI_CMD_EXIT_FREE_CHAT);
        }
        if (ai_free_chat_view_is_open()) {
            send_ai_cmd(AI_CMD_EXIT_FREE_CHAT);
            ai_free_chat_view_close();
        }
        send_ai_cmd(AI_CMD_STOP_LISTEN);
        app_manager_back_home();
    }
}

static void free_chat_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (free_chat_enter_pending) return;
        if (!latest_tuya_bound) {
            send_ai_cmd(AI_CMD_GET_STATUS);
            return;
        }

        free_chat_enter_pending = true;
        free_chat_enter_failed = false;
        if (has_latest_status) update_buttons(&latest_status);

        /* Tuya confirms the mode change asynchronously over UDP.  Open the
         * local view immediately so a valid tap never appears to do nothing;
         * the timeout closes it again if the mode switch actually fails. */
        if (!ai_free_chat_view_is_open()) {
            ai_free_chat_view_open(free_chat_exit_requested, NULL);
        }

        cancel_free_chat_enter_timer();
        free_chat_enter_timer = lv_timer_create(free_chat_enter_timeout_cb,
                                                 FREE_CHAT_ENTER_TIMEOUT_MS,
                                                 NULL);
        if (free_chat_enter_timer) {
            lv_timer_set_repeat_count(free_chat_enter_timer, 1);
        } else {
            free_chat_enter_pending = false;
            free_chat_enter_failed = true;
            if (has_latest_status) update_buttons(&latest_status);
        }
        send_ai_cmd(AI_CMD_ENTER_FREE_CHAT);
    }
}

static void bind_modal_close_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (bind_modal) {
            snprintf(dismissed_bind_url, sizeof(dismissed_bind_url), "%s",
                     modal_bind_url);
            bind_modal_dismissed = true;
            lv_obj_delete(bind_modal);
            bind_modal = NULL;
            bind_qrcode = NULL;
            modal_bind_url[0] = '\0';
        }

        if (ai_free_chat_view_is_open()) {
            send_ai_cmd(AI_CMD_EXIT_FREE_CHAT);
            ai_free_chat_view_close();
        }
        send_ai_cmd(AI_CMD_STOP_LISTEN);
        app_manager_back_home();
    }
}

static void hide_bind_modal(void)
{
    if (bind_modal) {
        lv_obj_delete(bind_modal);
        bind_modal = NULL;
        bind_qrcode = NULL;
    }
    bind_modal_dismissed = false;
    dismissed_bind_url[0] = '\0';
    modal_bind_url[0] = '\0';
}

static void maybe_show_bind_modal(const ai_status_t *status)
{
    lv_obj_t *card;
    lv_obj_t *content_panel;
    lv_obj_t *qr_panel;
    lv_obj_t *brand_label;
    lv_obj_t *title;
    lv_obj_t *tip;
    lv_obj_t *guide_group;
    lv_obj_t *guide_title;
    lv_obj_t *guide_text;
    lv_obj_t *spacer;
    lv_obj_t *later_note;
    lv_obj_t *close_btn;
    lv_obj_t *close_label;
    lv_obj_t *qr_title;
    lv_obj_t *qr_holder;
    lv_obj_t *qr_status;

    if (!status) return;

    if (status->tuya_bound || !status->tuya_bind_qr_pending || status->bind_url[0] == '\0') {
        hide_bind_modal();
        return;
    }

    if (bind_modal_dismissed &&
        strcmp(dismissed_bind_url, status->bind_url) == 0) {
        return;
    }

    if (bind_modal) {
        if (bind_qrcode) {
            lv_qrcode_update(bind_qrcode, status->bind_url, strlen(status->bind_url));
        }
        snprintf(modal_bind_url, sizeof(modal_bind_url), "%s", status->bind_url);
        return;
    }

    /* Full-screen scrim: clear default theme radius so corners are square. */
    bind_modal = lv_obj_create(lv_layer_top());
    snprintf(modal_bind_url, sizeof(modal_bind_url), "%s", status->bind_url);
    lv_obj_set_size(bind_modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bind_modal, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(bind_modal, LV_OPA_70, 0);
    lv_obj_set_style_radius(bind_modal, 0, 0);
    lv_obj_set_style_border_width(bind_modal, 0, 0);
    lv_obj_set_style_outline_width(bind_modal, 0, 0);
    lv_obj_set_style_shadow_width(bind_modal, 0, 0);
    lv_obj_set_style_pad_all(bind_modal, 0, 0);
    lv_obj_clear_flag(bind_modal, LV_OBJ_FLAG_SCROLLABLE);

    card = lv_obj_create(bind_modal);
    lv_obj_set_size(card, 840, 520);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xDCE5E0), 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x0B2419), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(card, 32, 0);
    lv_obj_set_style_shadow_offset_y(card, 10, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    content_panel = lv_obj_create(card);
    lv_obj_set_size(content_panel, 474, lv_pct(100));
    lv_obj_set_style_bg_opa(content_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_panel, 0, 0);
    lv_obj_set_style_pad_left(content_panel, 40, 0);
    lv_obj_set_style_pad_right(content_panel, 36, 0);
    lv_obj_set_style_pad_top(content_panel, 32, 0);
    lv_obj_set_style_pad_bottom(content_panel, 28, 0);
    lv_obj_set_style_pad_row(content_panel, 12, 0);
    lv_obj_set_flex_flow(content_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    /* 文案变长时允许轻量滚动，避免裁掉底部「稍后绑定」按钮 */
    lv_obj_set_scroll_dir(content_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content_panel, LV_SCROLLBAR_MODE_AUTO);

    brand_label = lv_label_create(content_panel);
    lv_label_set_text(brand_label, "TUYA ASSISTANT  ·  DEVICE PAIRING");
    lv_obj_set_style_text_font(brand_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(brand_label, lv_color_hex(0x24865D), 0);

    title = lv_label_create(content_panel);
    lv_label_set_text(title, "Pair Your Device");
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, UI_TEXT_H1, 0);
    lv_obj_set_style_text_color(title, UI_TEXT_PRIMARY, 0);

    tip = lv_label_create(content_panel);
    lv_label_set_text(tip,
                      "Scan the QR code with the Smart Life app. "
                      "The assistant opens automatically after pairing.");
    lv_obj_set_width(tip, lv_pct(100));
    lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(tip, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(tip, UI_TEXT_SECONDARY, 0);

    /*
     * 固定高度容易在长文案/大字号下裁切；用 CONTENT 并靠 spacer 把
     * 底部按钮顶住，避免左侧内容把「稍后绑定」顶出卡片。
     */
    guide_group = lv_obj_create(content_panel);
    lv_obj_set_width(guide_group, lv_pct(100));
    lv_obj_set_height(guide_group, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(guide_group, 14, 0);
    lv_obj_set_style_bg_color(guide_group, lv_color_hex(0xF3F7F5), 0);
    lv_obj_set_style_bg_opa(guide_group, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(guide_group, 0, 0);
    lv_obj_set_style_pad_left(guide_group, 16, 0);
    lv_obj_set_style_pad_right(guide_group, 16, 0);
    lv_obj_set_style_pad_top(guide_group, 12, 0);
    lv_obj_set_style_pad_bottom(guide_group, 12, 0);
    lv_obj_set_style_pad_row(guide_group, 4, 0);
    lv_obj_set_flex_flow(guide_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(guide_group, LV_OBJ_FLAG_SCROLLABLE);

    guide_title = lv_label_create(guide_group);
    lv_label_set_text(guide_title, "How to Pair");
    lv_obj_set_style_text_font(guide_title, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(guide_title, UI_TEXT_PRIMARY, 0);

    guide_text = lv_label_create(guide_group);
    lv_label_set_text(guide_text,
                      "1. Download Smart Life from your app store "
                      "(not Tuya Smart).\n"
                      "2. Open the app, scan the QR code, and confirm pairing.");
    lv_obj_set_width(guide_text, lv_pct(100));
    lv_label_set_long_mode(guide_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(guide_text, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(guide_text, UI_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_line_space(guide_text, 2, 0);

    spacer = lv_obj_create(content_panel);
    lv_obj_set_width(spacer, 1);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    later_note = lv_label_create(content_panel);
    lv_label_set_text(later_note, "You can return to the desktop and pair later.");
    lv_obj_set_width(later_note, lv_pct(100));
    lv_label_set_long_mode(later_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(later_note, UI_TEXT_CAPTION, 0);
    lv_obj_set_style_text_color(later_note, lv_color_hex(0x718078), 0);

    close_btn = lv_button_create(content_panel);
    lv_obj_set_size(close_btn, 210, 58);
    lv_obj_set_style_radius(close_btn, 14, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xF2F6F4), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xE5ECE8), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(0xCAD7D0), 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_set_style_translate_y(close_btn, 1, LV_STATE_PRESSED);
    lv_obj_add_event_cb(close_btn, bind_modal_close_event, LV_EVENT_CLICKED, NULL);

    close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Pair Later and Exit");
    lv_obj_set_style_text_font(close_label, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0x28483A), 0);
    lv_obj_center(close_label);

    qr_panel = lv_obj_create(card);
    lv_obj_set_size(qr_panel, 366, lv_pct(100));
    lv_obj_set_style_bg_color(qr_panel, lv_color_hex(0xEEF7F2), 0);
    lv_obj_set_style_bg_opa(qr_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_panel, 0, 0);
    lv_obj_set_style_pad_left(qr_panel, 24, 0);
    lv_obj_set_style_pad_right(qr_panel, 24, 0);
    lv_obj_set_style_pad_top(qr_panel, 24, 0);
    lv_obj_set_style_pad_bottom(qr_panel, 20, 0);
    lv_obj_set_style_pad_row(qr_panel, 12, 0);
    lv_obj_set_flex_flow(qr_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qr_panel,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(qr_panel, LV_OBJ_FLAG_SCROLLABLE);

    qr_title = lv_label_create(qr_panel);
    lv_label_set_text(qr_title, "Scan with Smart Life");
    lv_obj_set_width(qr_title, lv_pct(100));
    lv_label_set_long_mode(qr_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(qr_title, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(qr_title, UI_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(qr_title, LV_TEXT_ALIGN_CENTER, 0);

    qr_holder = lv_obj_create(qr_panel);
    lv_obj_set_size(qr_holder, 310, 310);
    lv_obj_set_style_radius(qr_holder, 22, 0);
    lv_obj_set_style_bg_color(qr_holder, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_holder, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_holder, 1, 0);
    lv_obj_set_style_border_color(qr_holder, lv_color_hex(0xD8E6DE), 0);
    lv_obj_set_style_shadow_color(qr_holder, lv_color_hex(0x35654E), 0);
    lv_obj_set_style_shadow_opa(qr_holder, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(qr_holder, 18, 0);
    lv_obj_set_style_shadow_offset_y(qr_holder, 5, 0);
    lv_obj_set_style_pad_all(qr_holder, 0, 0);
    lv_obj_clear_flag(qr_holder, LV_OBJ_FLAG_SCROLLABLE);

    bind_qrcode = lv_qrcode_create(qr_holder);
    lv_qrcode_set_size(bind_qrcode, 270);
    lv_qrcode_set_dark_color(bind_qrcode, lv_color_hex(0x111827));
    lv_qrcode_set_light_color(bind_qrcode, lv_color_white());
    lv_qrcode_set_quiet_zone(bind_qrcode, true);
    lv_qrcode_update(bind_qrcode, status->bind_url, strlen(status->bind_url));
    lv_obj_center(bind_qrcode);

    qr_status = lv_label_create(qr_panel);
    lv_label_set_text(qr_status, "Waiting for scan  ·  Continues after pairing");
    lv_obj_set_width(qr_status, lv_pct(100));
    lv_obj_set_style_text_font(qr_status, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(qr_status, lv_color_hex(0x39745A), 0);
    lv_obj_set_style_text_align(qr_status, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 260, 72);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_grad_color(btn, UI_COLOR_SECONDARY, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x8B96A8), LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_opa(btn, LV_OPA_TRANSP, LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(btn, 14, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_offset_y(btn, 5, 0);
    lv_obj_set_style_translate_y(btn, 1, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_x(btn, 250, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn, 250, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    btn_free_chat_label = label;
    lv_label_set_text(label, text);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);

    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFCFCFE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x6B7A99), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(panel, 20, 0);
    lv_obj_set_style_shadow_offset_y(panel, 7, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    return panel;
}

static lv_obj_t *create_wave_bar(lv_obj_t *parent, lv_coord_t height)
{
    lv_obj_t *bar = lv_obj_create(parent);

    lv_obj_set_size(bar, 8, height);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bar, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

static void start_wave_anim(lv_obj_t *bar, int32_t min_h, int32_t max_h, uint32_t delay_ms)
{
    lv_anim_t anim;

    if (!bar) return;

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, bar);
    lv_anim_set_values(&anim, min_h, max_h);
    lv_anim_set_duration(&anim, 780);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_set_playback_duration(&anim, 780);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_height);
    lv_anim_start(&anim);
}

static void set_wave_active(bool active, lv_color_t accent)
{
    static const int32_t idle_heights[5] = {10, 16, 24, 16, 10};

    for (int i = 0; i < 5; i++) {
        if (!wave_bars[i]) continue;
        lv_obj_set_style_bg_color(wave_bars[i], accent, 0);
    }

    if (active) {
        if (!wave_anim_running) {
            start_wave_anim(wave_bars[0], 10, 24, 0);
            start_wave_anim(wave_bars[1], 14, 36, 120);
            start_wave_anim(wave_bars[2], 20, 46, 240);
            start_wave_anim(wave_bars[3], 14, 36, 360);
            start_wave_anim(wave_bars[4], 10, 24, 480);
            wave_anim_running = true;
        }

        for (int i = 0; i < 5; i++) {
            if (wave_bars[i]) lv_obj_set_style_opa(wave_bars[i], LV_OPA_COVER, 0);
        }
    } else {
        if (wave_anim_running) {
            for (int i = 0; i < 5; i++) {
                if (wave_bars[i]) lv_anim_delete(wave_bars[i], (lv_anim_exec_xcb_t)lv_obj_set_height);
            }
            wave_anim_running = false;
        }

        for (int i = 0; i < 5; i++) {
            if (!wave_bars[i]) continue;
            lv_obj_set_height(wave_bars[i], idle_heights[i]);
            lv_obj_set_style_opa(wave_bars[i], LV_OPA_30, 0);
        }
    }
}

static void orb_scale_anim_cb(void *obj, int32_t value)
{
    if (!obj) return;
    lv_obj_set_style_transform_scale_x((lv_obj_t *)obj, value, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t *)obj, value, 0);
}

static void set_orb_breathe(bool active)
{
    if (!assistant_orb) return;

    if (active && !orb_breathe_running) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, assistant_orb);
        lv_anim_set_values(&anim, 253, 259);
        lv_anim_set_duration(&anim, 1900);
        lv_anim_set_playback_duration(&anim, 1900);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&anim, orb_scale_anim_cb);
        lv_anim_start(&anim);
        orb_breathe_running = true;
    } else if (!active && orb_breathe_running) {
        lv_anim_delete(assistant_orb, orb_scale_anim_cb);
        lv_obj_set_style_transform_scale_x(assistant_orb, 256, 0);
        lv_obj_set_style_transform_scale_y(assistant_orb, 256, 0);
        orb_breathe_running = false;
    }
}

void ai_app_init(void)
{
    lv_obj_t *scr;
    lv_obj_t *header;
    lv_obj_t *content;
    lv_obj_t *left_panel;
    lv_obj_t *right_panel;
    lv_obj_t *avatar_icon;
    lv_obj_t *hero_title;
    lv_obj_t *wave_row;
    lv_obj_t *mode_title;
    lv_obj_t *mode_description;
    lv_obj_t *section_divider;
    lv_obj_t *recent_title;
    lv_obj_t *right_spacer;
    lv_obj_t *exit_note;

    latest_tuya_bound = false;
    free_chat_enter_pending = false;
    free_chat_enter_failed = false;
    conversation_activity_seen = false;
    has_latest_status = false;
    memset(&latest_status, 0, sizeof(latest_status));
    cancel_free_chat_enter_timer();

    mw_subscribe(TOPIC_AI_STATUS, on_ai_status);
    mw_subscribe(TOPIC_AI_TEXT, on_ai_text);

    scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, ui_theme_get_desktop_bg_style(), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    header = ui_create_app_header(scr, "Tuya Assistant", back_event_handler, false, NULL);

    content = lv_obj_create(scr);
    lv_obj_set_size(content, lv_pct(100), LV_MAX(1, 768 - UI_APP_HEADER_H));
    lv_obj_set_pos(content, 0, UI_APP_HEADER_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_left(content, 34, 0);
    lv_obj_set_style_pad_right(content, 34, 0);
    lv_obj_set_style_pad_top(content, 24, 0);
    lv_obj_set_style_pad_bottom(content, 30, 0);
    lv_obj_set_style_pad_column(content, 22, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    left_panel = create_panel(content);
    lv_obj_set_size(left_panel, 592, lv_pct(100));
    lv_obj_set_style_pad_left(left_panel, 34, 0);
    lv_obj_set_style_pad_right(left_panel, 34, 0);
    lv_obj_set_style_pad_top(left_panel, 28, 0);
    lv_obj_set_style_pad_bottom(left_panel, 30, 0);
    lv_obj_set_style_pad_row(left_panel, 16, 0);
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_panel,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    assistant_orb = lv_obj_create(left_panel);
    lv_obj_set_size(assistant_orb, 228, 228);
    lv_obj_set_style_radius(assistant_orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(assistant_orb,
                              lv_color_mix(UI_COLOR_PRIMARY,
                                           lv_color_white(), 82), 0);
    lv_obj_set_style_bg_grad_color(assistant_orb,
                                   lv_color_mix(UI_COLOR_SECONDARY,
                                                lv_color_white(), 112), 0);
    lv_obj_set_style_bg_grad_dir(assistant_orb, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(assistant_orb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(assistant_orb, 3, 0);
    lv_obj_set_style_border_color(assistant_orb, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_width(assistant_orb, 30, 0);
    lv_obj_set_style_shadow_opa(assistant_orb, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(assistant_orb, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_offset_y(assistant_orb, 8, 0);
    lv_obj_clear_flag(assistant_orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(assistant_orb, LV_OBJ_FLAG_CLICKABLE);

    avatar_icon = lv_image_create(assistant_orb);
    lv_image_set_src(avatar_icon, &aibot_app);
    lv_obj_align(avatar_icon, LV_ALIGN_CENTER, 0, -20);

    wave_row = lv_obj_create(assistant_orb);
    lv_obj_set_size(wave_row, 156, 48);
    lv_obj_align(wave_row, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_opa(wave_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wave_row, 0, 0);
    lv_obj_set_style_pad_all(wave_row, 0, 0);
    lv_obj_set_style_pad_column(wave_row, 9, 0);
    lv_obj_set_flex_flow(wave_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wave_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(wave_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    wave_bars[0] = create_wave_bar(wave_row, 10);
    wave_bars[1] = create_wave_bar(wave_row, 16);
    wave_bars[2] = create_wave_bar(wave_row, 24);
    wave_bars[3] = create_wave_bar(wave_row, 16);
    wave_bars[4] = create_wave_bar(wave_row, 10);
    set_wave_active(false, UI_COLOR_PRIMARY);

    label_state = lv_label_create(left_panel);
    lv_label_set_text(label_state, "Starting assistant");
    lv_obj_set_style_text_font(label_state, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(label_state, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_align(label_state, LV_TEXT_ALIGN_CENTER, 0);

    hero_title = lv_label_create(left_panel);
    lv_label_set_text(hero_title, "Hello, I'm Tuya Assistant");
    lv_obj_set_width(hero_title, 520);
    lv_label_set_long_mode(hero_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(hero_title, UI_TEXT_H1, 0);
    lv_obj_set_style_text_color(hero_title, UI_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(hero_title, LV_TEXT_ALIGN_CENTER, 0);

    label_hint = lv_label_create(left_panel);
    lv_label_set_text(label_hint, "Say “Ni Hao Tuya” to wake me");
    lv_obj_set_width(label_hint, 520);
    lv_label_set_long_mode(label_hint, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label_hint, UI_TEXT_H2, 0);
    lv_obj_set_style_text_color(label_hint, UI_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(label_hint, LV_TEXT_ALIGN_CENTER, 0);

    right_panel = create_panel(content);
    lv_obj_set_size(right_panel, 342, lv_pct(100));
    lv_obj_set_style_pad_all(right_panel, 26, 0);
    lv_obj_set_style_pad_row(right_panel, 16, 0);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    mode_title = lv_label_create(right_panel);
    lv_label_set_text(mode_title, "Free Chat");
    lv_obj_set_width(mode_title, lv_pct(100));
    lv_obj_set_style_text_font(mode_title, UI_TEXT_H2, 0);
    lv_obj_set_style_text_color(mode_title, UI_TEXT_PRIMARY, 0);

    mode_description = lv_label_create(right_panel);
    lv_label_set_text(mode_description, "Open the full-screen assistant");
    lv_obj_set_width(mode_description, lv_pct(100));
    lv_label_set_long_mode(mode_description, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(mode_description, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(mode_description, UI_TEXT_SECONDARY, 0);

    btn_free_chat = create_button(right_panel, "Open Free Chat", free_chat_event_handler);
    lv_obj_set_size(btn_free_chat, lv_pct(100), 78);
    lv_obj_add_state(btn_free_chat, LV_STATE_DISABLED);

    free_chat_feedback_label = lv_label_create(right_panel);
    lv_label_set_text(free_chat_feedback_label, "Loading assistant status");
    lv_obj_set_size(free_chat_feedback_label, lv_pct(100), 24);
    lv_label_set_long_mode(free_chat_feedback_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(free_chat_feedback_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(free_chat_feedback_label, UI_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(free_chat_feedback_label, LV_TEXT_ALIGN_CENTER, 0);

    section_divider = lv_obj_create(right_panel);
    lv_obj_set_size(section_divider, lv_pct(100), 1);
    lv_obj_set_style_bg_color(section_divider, UI_DESKTOP_DIVIDER, 0);
    lv_obj_set_style_bg_opa(section_divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(section_divider, 0, 0);
    lv_obj_set_style_pad_all(section_divider, 0, 0);
    lv_obj_clear_flag(section_divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    recent_title = lv_label_create(right_panel);
    lv_label_set_text(recent_title, "CURRENT CONVERSATION");
    lv_obj_set_style_text_font(recent_title, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(recent_title, UI_TEXT_SECONDARY, 0);

    label_text = lv_label_create(right_panel);
    lv_obj_set_width(label_text, lv_pct(100));
    lv_obj_set_height(label_text, 156);
    lv_label_set_long_mode(label_text, LV_LABEL_LONG_DOT);
    lv_label_set_text(label_text, "No conversation yet");
    lv_obj_set_style_text_font(label_text, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(label_text, UI_TEXT_SECONDARY, 0);

    right_spacer = lv_obj_create(right_panel);
    lv_obj_set_size(right_spacer, 1, 1);
    lv_obj_set_flex_grow(right_spacer, 1);
    lv_obj_set_style_bg_opa(right_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_spacer, 0, 0);
    lv_obj_set_style_pad_all(right_spacer, 0, 0);
    lv_obj_clear_flag(right_spacer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    exit_note = lv_label_create(right_panel);
    lv_label_set_text(exit_note, "Double-tap the screen to exit Free Chat");
    lv_obj_set_width(exit_note, lv_pct(100));
    lv_label_set_long_mode(exit_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(exit_note, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(exit_note, UI_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(exit_note, LV_TEXT_ALIGN_CENTER, 0);

    set_orb_breathe(true);

    lv_obj_move_foreground(header);

    lv_scr_load(scr);
    send_ai_cmd(AI_CMD_GET_STATUS);
}

void ai_app_close(void)
{
    hide_bind_modal();
    cancel_free_chat_enter_timer();

    if (free_chat_enter_pending) {
        send_ai_cmd(AI_CMD_EXIT_FREE_CHAT);
        free_chat_enter_pending = false;
    }

    if (ai_free_chat_view_is_open()) {
        send_ai_cmd(AI_CMD_EXIT_FREE_CHAT);
        ai_free_chat_view_close();
    }

    mw_unsubscribe(TOPIC_AI_STATUS, on_ai_status);
    mw_unsubscribe(TOPIC_AI_TEXT, on_ai_text);
    set_orb_breathe(false);
    label_state = NULL;
    label_hint = NULL;
    label_text = NULL;
    assistant_orb = NULL;
    for (int i = 0; i < 5; i++) wave_bars[i] = NULL;
    btn_free_chat = NULL;
    btn_free_chat_label = NULL;
    free_chat_feedback_label = NULL;
    wave_anim_running = false;
    orb_breathe_running = false;
    latest_tuya_bound = false;
    free_chat_enter_failed = false;
    conversation_activity_seen = false;
    has_latest_status = false;
}

AppDescriptor app_ai = {
    .id = APP_ID_AI,
    .name = "Tuya Assistant",
    .init = ai_app_init,
    .close = ai_app_close,
};
