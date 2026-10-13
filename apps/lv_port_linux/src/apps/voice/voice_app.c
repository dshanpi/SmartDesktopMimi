#include "voice_app.h"
#if MIMI_ENABLE_VOICE_CONTROL
#include "voice_command.h"
#include "../../system/app_manager.h"
#include "../../system/backend_types.h"
#include "../../middleware/middleware.h"
#include "../../ui/mimi_ui.h"
#include "../../ui/ui_fonts.h"
#include "../../ui/desktop_all_apps.h"
#include <string.h>
static lv_obj_t *entry, *message, *confirm_button, *listen_button, *enable_button, *keyboard, *body;
static int32_t body_height;
static lv_timer_t * timer;
static bool active, enabled, armed, owns_session, listening_seen, received;
static ai_status_t status;
static mimi_command_t proposal;
static uint32_t armed_at, proposed_at;
/* The frozen legacy topic has no utterance ID. Retain the last observed user
 * text only for this page, including the GET_STATUS cache replay while idle. */
static char last_user_text[sizeof(((ai_text_t *)0)->text)];
static const char * tr(const char * z, const char * e)
{
    return ui_i18n_is_zh() ? z : e;
}
static void hide_keyboard(void)
{
    if(!keyboard || !body) return;
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(body, body_height);
    lv_obj_update_layout(body);
}
static void send(ai_cmd_action_t action)
{
    ai_cmd_t cmd = {.action = action};
    mw_publish(TOPIC_AI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}
static void stop_listen(void)
{
    armed          = false;
    listening_seen = false;
    if(owns_session) {
        owns_session = false;
        send(AI_CMD_EXIT_FREE_CHAT);
    }
}
static void clear_proposal(void)
{
    proposal = MIMI_COMMAND_NONE;
    if(confirm_button) lv_obj_add_state(confirm_button, LV_STATE_DISABLED);
}
static void prepare(lv_event_t * event)
{
    (void)event;
    hide_keyboard();
    stop_listen();
    clear_proposal();
    if(mimi_command_parse(lv_textarea_get_text(entry), &proposal)) {
        proposed_at = lv_tick_get();
        lv_label_set_text(message, tr("已识别导航指令，请核对文字后确认执行（30 秒内有效）",
                                      "Navigation command recognized. Review the text and confirm within 30 seconds."));
        lv_obj_remove_state(confirm_button, LV_STATE_DISABLED);
    } else
        lv_label_set_text(message, tr("暂不支持这条指令。可以说：打开设置、返回首页、打开涂鸦智家。",
                                      "Unsupported command. Try: open settings, go home, or open smart home."));
}
static void edited(lv_event_t * event)
{
    (void)event;
    bool was_listening = armed;
    stop_listen();
    clear_proposal();
    if(was_listening && message)
        lv_label_set_text(message, tr("已停止语音输入，请预览文字指令",
                                      "Voice input stopped. Preview your typed command."));
}
static void execute(lv_event_t * event)
{
    (void)event;
    if(proposal == MIMI_COMMAND_NONE || lv_tick_elaps(proposed_at) > 30000) {
        clear_proposal();
        return;
    }
    mimi_command_t chosen = proposal;
    clear_proposal();
    stop_listen();
    app_id_t target = APP_ID_NONE;
    switch(chosen) {
        case MIMI_COMMAND_SETTINGS: target = APP_ID_SETTING; break;
        case MIMI_COMMAND_WIFI: target = APP_ID_WIFI; break;
        case MIMI_COMMAND_BLUETOOTH: target = APP_ID_BT; break;
        case MIMI_COMMAND_VIDEO: target = APP_ID_VIDEO; break;
        case MIMI_COMMAND_PAIRING: target = APP_ID_PAIRING; break;
        case MIMI_COMMAND_IOT: target = APP_ID_IOT; break;
        default: break;
    }
    if(target != APP_ID_NONE)
        app_manager_open(target);
    else {
        app_manager_back_home();
        if(chosen == MIMI_COMMAND_DRAWER) ui_all_apps_show();
    }
}
static void start_listen(lv_event_t * event)
{
    (void)event;
    if(!enabled || !received || !status.tuya_bound || !status.control_center_running || status.free_chat_active ||
       status.state != AI_STATE_IDLE || owns_session)
        return;
    hide_keyboard();
    clear_proposal();
    lv_textarea_set_text(entry, "");
    armed          = true;
    owns_session   = true;
    listening_seen = false;
    armed_at       = lv_tick_get();
    lv_label_set_text(message, tr("正在启动识别，请等候聆听状态后说出导航指令",
                                  "Starting recognition. Wait for listening, then speak a navigation command."));
    send(AI_CMD_ENTER_FREE_CHAT);
}
static void toggle(lv_event_t * event)
{
    (void)event;
    enabled = !enabled;
    stop_listen();
    clear_proposal();
    lv_label_set_text(lv_obj_get_child(enable_button, 0),
                      enabled ? tr("关闭语音控制", "Disable voice control")
                              : tr("启用本次语音控制", "Enable voice for this session"));
    lv_label_set_text(message, enabled
                                   ? tr("已启用；点击开始识别后才接受本次转写",
                                        "Enabled. Select Start recognition to accept a transcript.")
                                   : tr("语音控制已关闭，仍可输入文字预览导航指令",
                                        "Voice control is off. You can still type and preview navigation commands."));
}
static void on_status(const mw_msg_t * msg)
{
    if(!active || !msg || !msg->data || msg->data_len != sizeof(status)) return;
    memcpy(&status, msg->data, sizeof(status));
    received = true;
    if(armed && status.free_chat_active && status.state == AI_STATE_LISTENING) {
        listening_seen = true;
        lv_label_set_text(message, tr("正在聆听，请说出导航指令…", "Listening. Speak a navigation command…"));
    }
    if(armed && listening_seen && !status.free_chat_active) {
        stop_listen();
        lv_label_set_text(message, tr("识别会话已结束，请重新开始", "Recognition session ended. Start again."));
    }
    if(armed &&
       (status.last_error_code || status.state == AI_STATE_NETWORK_UNAVAILABLE || status.state == AI_STATE_ERROR)) {
        stop_listen();
        lv_label_set_text(message, tr("语音服务不可用，请检查网络与涂鸦助手绑定",
                                      "Voice service unavailable. Check the network and Tuya Assistant binding."));
    }
}
static void on_text(const mw_msg_t * msg)
{
    if(!active || !msg || !msg->data || msg->data_len != sizeof(ai_text_t))
        return;
    const ai_text_t * t = (const ai_text_t *)msg->data;
    size_t n = strnlen(t->text, sizeof(t->text)); /* Existing Tuya adapter prefixes genuine ASR with You:, replies with
                                                     Assistant:. All proposals still require a local confirmation. */
    if(n >= sizeof(t->text) || n <= 5 || strncmp(t->text, "You: ", 5)) return;
    bool repeated = !strcmp(last_user_text, t->text);
    memcpy(last_user_text, t->text, n + 1);
    if(!enabled || !armed || !listening_seen) return;
    if(lv_tick_elaps(armed_at) > 30000 || !status.free_chat_active ||
       !status.tuya_bound || !status.control_center_running) {
        stop_listen();
        clear_proposal();
        lv_label_set_text(message, tr("识别会话已结束或超时，请重新开始",
                                      "Recognition session ended or timed out. Start again."));
        return;
    }
    if(repeated) {
        lv_label_set_text(message, tr("已忽略重复或缓存的转写，请输入文字或换一种说法",
                                      "Repeated or cached transcript ignored. Type the command or rephrase it."));
        return;
    }
    /* Never truncate a long transcript into a shorter whitelisted command. */
    if(n - 5 > 127) {
        stop_listen();
        clear_proposal();
        lv_label_set_text(message, tr("转写过长，请使用简短导航指令",
                                      "Transcript too long. Use a short navigation command."));
        return;
    }
    stop_listen();
    lv_textarea_set_text(entry, t->text + 5);
    prepare(NULL);
}
static void tick(lv_timer_t * t)
{
    (void)t;
    if(armed && lv_tick_elaps(armed_at) > 30000) {
        stop_listen();
        lv_label_set_text(message, tr("本次识别已超时，请重新开始", "Recognition timed out. Start again."));
    }
    if(proposal != MIMI_COMMAND_NONE && lv_tick_elaps(proposed_at) > 30000) {
        clear_proposal();
        lv_label_set_text(message,
                          tr("确认已过期，请重新预览指令", "Confirmation expired. Preview the command again."));
    }
    bool available = enabled && received && status.tuya_bound && status.control_center_running &&
                     !status.free_chat_active && status.state == AI_STATE_IDLE && !owns_session;
    if(available)
        lv_obj_remove_state(listen_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(listen_button, LV_STATE_DISABLED);
}
static lv_obj_t * button(lv_obj_t * p, const char * text, lv_event_cb_t cb)
{
    lv_obj_t * b = lv_button_create(p);
    mimi_ui_button_style(b, true);
    lv_obj_set_size(b, lv_pct(100), 52);
    lv_obj_t * l = mimi_ui_text(b, text, ui_font_body_md(), MIMI_TEXT);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}
static void cancel(lv_event_t * e)
{
    (void)e;
    hide_keyboard();
    stop_listen();
    clear_proposal();
    lv_textarea_set_text(entry, "");
    lv_label_set_text(message, tr("已取消，没有执行操作", "Cancelled. No action was executed."));
}
static void back(lv_event_t * e)
{
    (void)e;
    app_manager_back_home();
}
static void close_app(void)
{
    active = false;
    stop_listen();
    mw_unsubscribe(TOPIC_AI_STATUS, on_status);
    mw_unsubscribe(TOPIC_AI_TEXT, on_text);
    if(timer) {
        lv_timer_delete(timer);
        timer = NULL;
    }
    entry = message = confirm_button = listen_button = enable_button = keyboard = body = NULL;
    proposal                                                                    = MIMI_COMMAND_NONE;
    enabled = received = false;
    memset(&status, 0, sizeof(status));
    memset(last_user_text, 0, sizeof(last_user_text));
}
static void focus(lv_event_t * e)
{
    (void)e;
    if(keyboard) {
        lv_keyboard_set_textarea(keyboard, entry);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        /* Reserve space for the keyboard, then reveal the complete input. */
        lv_obj_set_height(body, LV_MAX(96, body_height - lv_obj_get_height(keyboard)));
        lv_obj_update_layout(body);
        lv_obj_scroll_to_view_recursive(entry, LV_ANIM_OFF);
    }
}
static void keyboard_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) hide_keyboard();
}
static void init_app(void)
{
    close_app();
    active            = true;
    lv_obj_t * screen = lv_obj_create(NULL);
    mimi_ui_header(screen, tr("语音操作", "Voice actions"), back);
    body = mimi_ui_body(screen);
    lv_obj_update_layout(body);
    body_height = lv_obj_get_height(body);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * note = mimi_ui_text(
        body,
        tr("语音为可选功能，默认关闭。启用后使用已绑定的涂鸦云识别；转写只用于本页导航预览，确认后执行。退出本页即关闭"
           "。",
           "Voice is optional and off by default. It uses your linked Tuya cloud speech service. Transcripts preview "
           "navigation only; confirm to execute. Voice control turns off when you leave."),
        ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    enable_button = button(body, tr("启用本次语音控制", "Enable voice for this session"), toggle);
    listen_button = button(body, tr("开始识别", "Start recognition"), start_listen);
    lv_obj_add_state(listen_button, LV_STATE_DISABLED);
    entry = lv_textarea_create(body);
    lv_obj_set_size(entry, lv_pct(100), 72);
    lv_obj_set_style_text_font(entry, ui_font_body_lg(), 0);
    lv_textarea_set_max_length(entry, 127);
    lv_textarea_set_placeholder_text(entry, tr("例如：打开设置", "Example: open settings"));
    lv_obj_add_event_cb(entry, edited, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(entry, focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(entry, focus, LV_EVENT_CLICKED, NULL);
    button(body, tr("预览文字指令", "Preview command"), prepare);
    message = mimi_ui_text(body, tr("语音控制已关闭", "Voice control is off"), ui_font_body_md(), MIMI_TEXT);
    lv_obj_set_width(message, lv_pct(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    confirm_button = button(body, tr("确认执行", "Confirm action"), execute);
    lv_obj_add_state(confirm_button, LV_STATE_DISABLED);
    button(body, tr("取消", "Cancel"), cancel);
    keyboard = lv_keyboard_create(screen);
    mimi_ui_keyboard_style(keyboard);
    lv_obj_set_size(keyboard, lv_pct(100), LV_MIN(280, lv_display_get_vertical_resolution(NULL) / 2));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_ALL, NULL);
    mw_subscribe(TOPIC_AI_STATUS, on_status);
    mw_subscribe(TOPIC_AI_TEXT, on_text);
    timer = lv_timer_create(tick, 200, NULL);
    lv_scr_load(screen);
    send(AI_CMD_GET_STATUS);
}
AppDescriptor app_voice = {.id = APP_ID_VOICE, .name = "Voice actions", .init = init_app, .close = close_app};
#endif
