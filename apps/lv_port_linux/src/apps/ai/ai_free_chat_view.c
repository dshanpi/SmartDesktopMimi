#include "ai_free_chat_view.h"

#include "../../ui/theme/theme.h"
#include "../../ui/ui_fonts.h"

#include <stdio.h>
#include <string.h>

#define EMOTE_DIR "/usr/share/aitvbox/emotions"
#define EMOTE_IDLE EMOTE_DIR "/static.json"
#define EMOTE_LISTEN EMOTE_DIR "/investigate.json"
#define EMOTE_THINK EMOTE_DIR "/ponder.json"
#define EMOTE_SPEAK EMOTE_DIR "/smile.json"
#define EMOTE_ERROR EMOTE_DIR "/panic.json"
#define EMOTE_HAPPY EMOTE_DIR "/smile.json"
#define EMOTE_SAD EMOTE_DIR "/sad.json"
#define EMOTE_ANGRY EMOTE_DIR "/angry.json"
#define EMOTE_SURPRISE EMOTE_DIR "/shocked.json"
#define EMOTE_CONFUSED EMOTE_DIR "/question.json"
#define EMOTE_SLEEP EMOTE_DIR "/asleep.json"

#define FREE_CHAT_LOTTIE_SIZE 680
#define EMOTE_SWITCH_DEBOUNCE_MS 300
#define EMOTE_MIN_HOLD_MS 800
#define SPEECH_FADE_IN_MS 150
#define SPEECH_FADE_OUT_MS 220
#define SPEECH_ROLL_OUT_MS 120
#define SPEECH_ROLL_OFFSET 8
#define SPEECH_MAX_CODEPOINTS 56U
#define SPEECH_ROLL_STEP_CODEPOINTS 24U
#define SPEECH_CLEAR_DELAY_MIN_MS 1800U
#define SPEECH_CLEAR_DELAY_MAX_MS 3800U
#define SPEECH_TEXT_OPA LV_OPA_90
#define SPEECH_TYPE_INTERVAL_MS 55U
#define SPEECH_TYPE_PUNCT_MS 130U

static lv_obj_t *view_root = NULL;
static lv_obj_t *speech_label = NULL;
static lv_obj_t *speech_label_alt = NULL;
static lv_obj_t *lottie_view = NULL;
static ai_free_chat_exit_cb_t exit_requested_cb = NULL;
static void *exit_requested_user_data = NULL;
static bool exit_requested = false;
static const char *current_emote = NULL;
static const char *pending_emote = NULL;
static uint32_t last_click_tick = 0;
static uint32_t last_emote_switch_tick = 0;
static lv_timer_t *emote_switch_timer = NULL;
static lv_timer_t *clear_speech_timer = NULL;
static lv_timer_t *speech_type_timer = NULL;
static char speech_text_buf[384];
static char speech_raw_buf[384];
static char speech_target_buf[384];
static size_t speech_visible_bytes = 0U;
static uint32_t speech_last_roll_codepoints = 0U;
static ai_state_t speech_state = AI_STATE_IDLE;

#if LV_USE_LOTTIE
static uint8_t lottie_buf[FREE_CHAT_LOTTIE_SIZE * FREE_CHAT_LOTTIE_SIZE * 4];
#endif

static uint16_t emote_speed_pct(const char *src)
{
    if (!src) return 100;
    if (strcmp(src, EMOTE_IDLE) == 0) return 125;
    if (strcmp(src, EMOTE_LISTEN) == 0) return 135;
    if (strcmp(src, EMOTE_THINK) == 0) return 115;
    if (strcmp(src, EMOTE_SPEAK) == 0 || strcmp(src, EMOTE_HAPPY) == 0) return 110;
    if (strcmp(src, EMOTE_SAD) == 0) return 105;
    if (strcmp(src, EMOTE_ANGRY) == 0) return 125;
    if (strcmp(src, EMOTE_SURPRISE) == 0) return 130;
    if (strcmp(src, EMOTE_CONFUSED) == 0) return 130;
    if (strcmp(src, EMOTE_SLEEP) == 0) return 90;
    if (strcmp(src, EMOTE_ERROR) == 0) return 120;
    return 100;
}

static void apply_lottie_speed(const char *src)
{
#if LV_USE_LOTTIE
    lv_anim_t *anim;
    uint32_t duration;
    uint16_t speed_pct;

    if (!lottie_view || !src) return;

    anim = lv_lottie_get_anim(lottie_view);
    if (!anim) return;

    duration = lv_anim_get_time(anim);
    speed_pct = emote_speed_pct(src);
    if (duration == 0 || speed_pct == 100) return;

    duration = (duration * 100U) / speed_pct;
    if (duration < 100U) duration = 100U;
    lv_anim_set_duration(anim, duration);
#else
    LV_UNUSED(src);
#endif
}

static const char *emote_for_emotion(const char *emotion)
{
    if (!emotion || !emotion[0]) return NULL;

    if (strcmp(emotion, "NEUTRAL") == 0) return EMOTE_IDLE;
    if (strcmp(emotion, "HAPPY") == 0 ||
        strcmp(emotion, "LAUGHING") == 0 ||
        strcmp(emotion, "FUNNY") == 0 ||
        strcmp(emotion, "LOVING") == 0 ||
        strcmp(emotion, "RELAXED") == 0 ||
        strcmp(emotion, "DELICIOUS") == 0 ||
        strcmp(emotion, "KISSY") == 0 ||
        strcmp(emotion, "CONFIDENT") == 0 ||
        strcmp(emotion, "SILLY") == 0 ||
        strcmp(emotion, "WINK") == 0) {
        return EMOTE_HAPPY;
    }
    if (strcmp(emotion, "SAD") == 0 ||
        strcmp(emotion, "FEARFUL") == 0 ||
        strcmp(emotion, "DISAPPOINTED") == 0) {
        return EMOTE_SAD;
    }
    if (strcmp(emotion, "ANGRY") == 0 ||
        strcmp(emotion, "ANNOYED") == 0) {
        return EMOTE_ANGRY;
    }
    if (strcmp(emotion, "SURPRISE") == 0 ||
        strcmp(emotion, "SHOCKED") == 0 ||
        strcmp(emotion, "EMBARRASSED") == 0 ||
        strcmp(emotion, "TOUCH") == 0) {
        return EMOTE_SURPRISE;
    }
    if (strcmp(emotion, "THINKING") == 0) return EMOTE_THINK;
    if (strcmp(emotion, "CONFUSED") == 0) return EMOTE_CONFUSED;
    if (strcmp(emotion, "SLEEP") == 0) return EMOTE_SLEEP;
    if (strcmp(emotion, "WAKEUP") == 0) return EMOTE_LISTEN;
    if (strcmp(emotion, "LEFT") == 0 ||
        strcmp(emotion, "RIGHT") == 0) {
        return EMOTE_CONFUSED;
    }

    return NULL;
}

static const char *emote_for_status(const ai_status_t *status)
{
    const char *emotion_src;

    if (!status) return EMOTE_IDLE;

    emotion_src = emote_for_emotion(status->emotion);
    if (emotion_src) return emotion_src;

    switch (status->state) {
        case AI_STATE_LISTENING:
            return EMOTE_LISTEN;
        case AI_STATE_THINKING:
            return EMOTE_THINK;
        case AI_STATE_SPEAKING:
            return EMOTE_SPEAK;
        case AI_STATE_NETWORK_UNAVAILABLE:
        case AI_STATE_ERROR:
            return EMOTE_ERROR;
        case AI_STATE_IDLE:
        case AI_STATE_CONNECTING:
        default:
            return EMOTE_IDLE;
    }
}

static void request_exit(void)
{
    if (exit_requested) return;

    exit_requested = true;
    if (speech_label) lv_label_set_text(speech_label, "Exiting");

    if (exit_requested_cb) {
        exit_requested_cb(exit_requested_user_data);
    }
}

static void root_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DOUBLE_CLICKED) {
        request_exit();
    } else if (code == LV_EVENT_CLICKED) {
        uint32_t now = lv_tick_get();

        if (last_click_tick != 0 && lv_tick_elaps(last_click_tick) <= 650) {
            last_click_tick = 0;
            request_exit();
        } else {
            last_click_tick = now;
        }
    }
}

static void add_bubble(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void cancel_pending_emote(void)
{
    if (emote_switch_timer) {
        lv_timer_del(emote_switch_timer);
        emote_switch_timer = NULL;
    }
    pending_emote = NULL;
}

static void load_emote_src(const char *src)
{
#if LV_USE_LOTTIE
    if (!lottie_view || !src) return;
    if (current_emote == src) return;

    current_emote = src;
    last_emote_switch_tick = lv_tick_get();
    /* ThorVG's tvg_picture_load doesn't replace an already-loaded paint at
     * runtime, so a second lv_lottie_set_src_file never shows the new src.
     * Recreate the lottie widget so the new src loads on a fresh paint --
     * the same path the initial load (in view_open) takes. */
    lv_obj_t *parent = lv_obj_get_parent(lottie_view);
    lv_obj_delete(lottie_view);
    lottie_view = lv_lottie_create(parent);
    lv_obj_set_size(lottie_view, FREE_CHAT_LOTTIE_SIZE, FREE_CHAT_LOTTIE_SIZE);
    lv_lottie_set_buffer(lottie_view, FREE_CHAT_LOTTIE_SIZE, FREE_CHAT_LOTTIE_SIZE, lottie_buf);
    lv_obj_align(lottie_view, LV_ALIGN_CENTER, 0, -28);
    add_bubble(lottie_view);
    lv_lottie_set_src_file(lottie_view, src);
    apply_lottie_speed(src);
#else
    LV_UNUSED(src);
#endif
}

static void emote_switch_timer_cb(lv_timer_t *t)
{
    const char *src = pending_emote;

    LV_UNUSED(t);
    emote_switch_timer = NULL;
    pending_emote = NULL;
    load_emote_src(src);
}

static void schedule_emote_switch(const char *src, uint32_t delay_ms)
{
    if (!src) return;

    if (emote_switch_timer) {
        lv_timer_del(emote_switch_timer);
        emote_switch_timer = NULL;
    }

    pending_emote = src;
    emote_switch_timer = lv_timer_create(emote_switch_timer_cb, delay_ms, NULL);
    if (emote_switch_timer) {
        lv_timer_set_repeat_count(emote_switch_timer, 1);
    } else {
        pending_emote = NULL;
        load_emote_src(src);
    }
}

static void set_emote_src(const char *src)
{
    uint32_t elapsed;
    uint32_t delay_ms;

    if (!src) return;

    if (current_emote == src) {
        cancel_pending_emote();
        return;
    }

    if (!current_emote) {
        cancel_pending_emote();
        load_emote_src(src);
        return;
    }

    if (pending_emote == src && emote_switch_timer) return;

    elapsed = last_emote_switch_tick ? lv_tick_elaps(last_emote_switch_tick) : EMOTE_MIN_HOLD_MS;
    delay_ms = EMOTE_SWITCH_DEBOUNCE_MS;
    if (elapsed < EMOTE_MIN_HOLD_MS &&
        EMOTE_MIN_HOLD_MS - elapsed > delay_ms) {
        delay_ms = EMOTE_MIN_HOLD_MS - elapsed;
    }

    schedule_emote_switch(src, delay_ms);
}

static void speech_opa_anim_cb(void *obj, int32_t value)
{
    if (obj) lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void speech_translate_anim_cb(void *obj, int32_t value)
{
    if (obj) lv_obj_set_style_translate_y((lv_obj_t *)obj, value, 0);
}

static void speech_fade_out_completed_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    if (!speech_label) return;

    lv_label_set_text(speech_label, "");
    lv_obj_set_style_text_opa(speech_label, SPEECH_TEXT_OPA, 0);
    lv_obj_set_style_translate_y(speech_label, 0, 0);
    speech_raw_buf[0] = '\0';
    speech_text_buf[0] = '\0';
    speech_last_roll_codepoints = 0U;
}

static void cancel_speech_fade(void)
{
    if (speech_label) {
        lv_anim_delete(speech_label, speech_opa_anim_cb);
        lv_anim_delete(speech_label, speech_translate_anim_cb);
    }
    if (speech_label_alt) {
        lv_anim_delete(speech_label_alt, speech_opa_anim_cb);
        lv_anim_delete(speech_label_alt, speech_translate_anim_cb);
    }
}

static void start_speech_anim(lv_obj_t *label, lv_anim_exec_xcb_t exec_cb,
                              int32_t start, int32_t end, uint32_t duration)
{
    lv_anim_t anim;

    if (!label || !exec_cb) return;

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_exec_cb(&anim, exec_cb);
    lv_anim_set_values(&anim, start, end);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void fade_speech_to(lv_opa_t end_opa, uint32_t duration,
                           lv_anim_completed_cb_t completed_cb)
{
    lv_anim_t anim;

    if (!speech_label) return;

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, speech_label);
    lv_anim_set_exec_cb(&anim, speech_opa_anim_cb);
    lv_anim_set_values(&anim,
                       lv_obj_get_style_text_opa(speech_label, LV_PART_MAIN),
                       end_opa);
    lv_anim_set_duration(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim, completed_cb);
    lv_anim_start(&anim);
}

static const char *skip_speech_prefix(const char *text)
{
    if (!text) return "";

    while (*text == ' ' || *text == '\t') text++;

    if (strncmp(text, "我：", strlen("我：")) == 0) {
        text += strlen("我：");
    } else if (strncmp(text, "我:", strlen("我:")) == 0) {
        text += strlen("我:");
    } else if (strncmp(text, "助手：", strlen("助手：")) == 0) {
        text += strlen("助手：");
    } else if (strncmp(text, "助手:", strlen("助手:")) == 0) {
        text += strlen("助手:");
    } else if (strncmp(text, "AI：", strlen("AI：")) == 0) {
        text += strlen("AI：");
    } else if (strncmp(text, "AI:", strlen("AI:")) == 0) {
        text += strlen("AI:");
    }

    while (*text == ' ' || *text == '\t') text++;
    return text;
}

static size_t speech_utf8_char_size(const char *text)
{
    const unsigned char c = text ? (unsigned char)text[0] : 0U;
    size_t bytes;

    if (c == 0U) return 0U;
    if ((c & 0x80U) == 0U) return 1U;
    if ((c & 0xE0U) == 0xC0U) bytes = 2U;
    else if ((c & 0xF0U) == 0xE0U) bytes = 3U;
    else if ((c & 0xF8U) == 0xF0U) bytes = 4U;
    else return 1U;

    for (size_t i = 1U; i < bytes; i++) {
        if (text[i] == '\0' ||
            (((unsigned char)text[i] & 0xC0U) != 0x80U)) {
            return 1U;
        }
    }
    return bytes;
}

static uint32_t speech_utf8_count(const char *text)
{
    uint32_t count = 0U;

    while (text && *text) {
        size_t bytes = speech_utf8_char_size(text);
        if (bytes == 0U) break;
        text += bytes;
        count++;
    }
    return count;
}

static void normalize_speech_text(const char *text, char *out, size_t out_size)
{
    const char *src = skip_speech_prefix(text);
    size_t used = 0U;
    bool pending_space = false;

    if (!out || out_size == 0U) return;

    while (src && *src && used + 1U < out_size) {
        size_t bytes = speech_utf8_char_size(src);

        if (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
            pending_space = used > 0U;
            src += bytes ? bytes : 1U;
            continue;
        }

        if (pending_space && used + 1U < out_size) {
            out[used++] = ' ';
            pending_space = false;
        }
        if (bytes == 0U || used + bytes >= out_size) break;
        memcpy(out + used, src, bytes);
        used += bytes;
        src += bytes;
    }

    while (used > 0U && out[used - 1U] == ' ') used--;
    out[used] = '\0';
}

static uint32_t build_recent_speech(const char *text, char *out, size_t out_size)
{
    const char *start = text;
    uint32_t total = speech_utf8_count(text);
    uint32_t skip = total > SPEECH_MAX_CODEPOINTS
                        ? total - SPEECH_MAX_CODEPOINTS
                        : 0U;

    if (!out || out_size == 0U) return total;

    while (start && *start && skip > 0U) {
        size_t bytes = speech_utf8_char_size(start);
        if (bytes == 0U) break;
        start += bytes;
        skip--;
    }
    while (start && *start == ' ') start++;

    snprintf(out, out_size, "%s", start ? start : "");
    return total;
}

static void show_speech_transition(const char *text)
{
    const char *current_text;
    lv_opa_t current_opa;
    bool had_visible_text;

    if (!speech_label || !speech_label_alt) return;

    current_text = lv_label_get_text(speech_label);
    current_opa = lv_obj_get_style_text_opa(speech_label, LV_PART_MAIN);
    had_visible_text = current_text && current_text[0] && current_opa > LV_OPA_20;

    cancel_speech_fade();

    if (!had_visible_text) {
        lv_label_set_text(speech_label, text);
        lv_obj_set_style_text_opa(speech_label, 0, 0);
        lv_obj_set_style_translate_y(speech_label, SPEECH_ROLL_OFFSET, 0);
        start_speech_anim(speech_label, speech_opa_anim_cb,
                          0, SPEECH_TEXT_OPA, SPEECH_FADE_IN_MS);
        start_speech_anim(speech_label, speech_translate_anim_cb,
                          SPEECH_ROLL_OFFSET, 0, SPEECH_FADE_IN_MS);
        return;
    }

    lv_obj_t *outgoing = speech_label;
    lv_obj_t *incoming = speech_label_alt;

    lv_label_set_text(incoming, text);
    lv_obj_set_style_text_opa(incoming, 0, 0);
    lv_obj_set_style_translate_y(incoming, SPEECH_ROLL_OFFSET, 0);

    start_speech_anim(outgoing, speech_opa_anim_cb,
                      current_opa, 0, SPEECH_ROLL_OUT_MS);
    start_speech_anim(outgoing, speech_translate_anim_cb,
                      lv_obj_get_style_translate_y(outgoing, LV_PART_MAIN),
                      -SPEECH_ROLL_OFFSET, SPEECH_ROLL_OUT_MS);
    start_speech_anim(incoming, speech_opa_anim_cb,
                      0, SPEECH_TEXT_OPA, SPEECH_FADE_IN_MS);
    start_speech_anim(incoming, speech_translate_anim_cb,
                      SPEECH_ROLL_OFFSET, 0, SPEECH_FADE_IN_MS);

    speech_label = incoming;
    speech_label_alt = outgoing;
}

static void set_speech(const char *text)
{
    char normalized[sizeof(speech_raw_buf)];
    uint32_t total_codepoints;
    size_t previous_len;
    bool cumulative;
    bool should_roll;

    if (!speech_label) return;

    normalize_speech_text(text, normalized, sizeof(normalized));
    if (!normalized[0]) {
        cancel_speech_fade();
        lv_label_set_text(speech_label, "");
        if (speech_label_alt) lv_label_set_text(speech_label_alt, "");
        lv_obj_set_style_text_opa(speech_label, SPEECH_TEXT_OPA, 0);
        lv_obj_set_style_translate_y(speech_label, 0, 0);
        if (speech_label_alt) {
            lv_obj_set_style_text_opa(speech_label_alt, 0, 0);
            lv_obj_set_style_translate_y(speech_label_alt, 0, 0);
        }
        speech_raw_buf[0] = '\0';
        speech_text_buf[0] = '\0';
        speech_last_roll_codepoints = 0U;
        return;
    }

    if (strcmp(normalized, speech_raw_buf) == 0) return;

    previous_len = strlen(speech_raw_buf);
    cumulative = previous_len > 0U &&
                 strncmp(normalized, speech_raw_buf, previous_len) == 0;
    total_codepoints = build_recent_speech(normalized,
                                           speech_text_buf,
                                           sizeof(speech_text_buf));

    should_roll = !lv_label_get_text(speech_label)[0] || !cumulative;
    if (cumulative && total_codepoints > SPEECH_MAX_CODEPOINTS &&
        total_codepoints >= speech_last_roll_codepoints +
                            SPEECH_ROLL_STEP_CODEPOINTS) {
        should_roll = true;
    }

    snprintf(speech_raw_buf, sizeof(speech_raw_buf), "%s", normalized);

    if (should_roll) {
        show_speech_transition(speech_text_buf);
        speech_last_roll_codepoints = total_codepoints;
    } else {
        lv_label_set_text(speech_label, speech_text_buf);
    }
}

static void cancel_speech_typing(bool clear_target)
{
    if (speech_type_timer) {
        lv_timer_del(speech_type_timer);
        speech_type_timer = NULL;
    }
    if (clear_target) {
        speech_target_buf[0] = '\0';
        speech_visible_bytes = 0U;
    }
}

static void finish_speech_typing(void)
{
    if (speech_target_buf[0]) {
        set_speech(speech_target_buf);
    }
    cancel_speech_typing(true);
}

static void speech_type_timer_cb(lv_timer_t *timer)
{
    char visible[sizeof(speech_target_buf)];
    size_t target_len = strlen(speech_target_buf);
    size_t char_bytes;
    unsigned char last_char;
    uint32_t next_period = SPEECH_TYPE_INTERVAL_MS;

    if (!speech_label || speech_visible_bytes >= target_len) {
        speech_type_timer = NULL;
        lv_timer_del(timer);
        return;
    }

    char_bytes = speech_utf8_char_size(speech_target_buf + speech_visible_bytes);
    if (char_bytes == 0U || speech_visible_bytes + char_bytes > target_len) {
        speech_visible_bytes = target_len;
    } else {
        speech_visible_bytes += char_bytes;
    }

    memcpy(visible, speech_target_buf, speech_visible_bytes);
    visible[speech_visible_bytes] = '\0';
    set_speech(visible);

    last_char = speech_visible_bytes ?
                (unsigned char)speech_target_buf[speech_visible_bytes - 1U] : 0U;
    if (last_char == '.' || last_char == ',' || last_char == '!' ||
        last_char == '?' || last_char == ';' || last_char == ':') {
        next_period = SPEECH_TYPE_PUNCT_MS;
    }
    lv_timer_set_period(timer, next_period);
}

static void start_speech_typing(void)
{
    if (!speech_label || !speech_target_buf[0] || speech_type_timer) return;

    if (speech_visible_bytes == 0U) {
        set_speech("");
    }
    speech_type_timer = lv_timer_create(speech_type_timer_cb,
                                        SPEECH_TYPE_INTERVAL_MS,
                                        NULL);
    if (speech_type_timer) lv_timer_ready(speech_type_timer);
}

static void clear_speech_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    clear_speech_timer = NULL;
    cancel_speech_fade();
    fade_speech_to(0, SPEECH_FADE_OUT_MS, speech_fade_out_completed_cb);
}

static void cancel_clear_speech(void)
{
    if (clear_speech_timer) {
        lv_timer_del(clear_speech_timer);
        clear_speech_timer = NULL;
    }
}

static void schedule_clear_speech(void)
{
    uint32_t text_codepoints;
    uint32_t delay_ms;

    if (clear_speech_timer) return;

    text_codepoints = speech_utf8_count(speech_text_buf);
    delay_ms = SPEECH_CLEAR_DELAY_MIN_MS + text_codepoints * 35U;
    if (delay_ms > SPEECH_CLEAR_DELAY_MAX_MS) {
        delay_ms = SPEECH_CLEAR_DELAY_MAX_MS;
    }

    clear_speech_timer = lv_timer_create(clear_speech_timer_cb, delay_ms, NULL);
    if (clear_speech_timer) lv_timer_set_repeat_count(clear_speech_timer, 1);
}

/* Subtitle follows the conversation state:
 *   listening/thinking -> prompt text
 *   speaking/connecting -> keep current text (reply / enter prompt)
 *   idle -> clear after a short delay so the user can read the last reply */
static void apply_speech_for_state(ai_state_t state)
{
    ai_state_t previous_state = speech_state;
    speech_state = state;

    switch (state) {
        case AI_STATE_LISTENING:
            cancel_clear_speech();
            if (previous_state == AI_STATE_SPEAKING && speech_target_buf[0]) {
                finish_speech_typing();
                schedule_clear_speech();
            } else if (!speech_raw_buf[0]) {
                set_speech("I'm listening");
            }
            break;
        case AI_STATE_THINKING:
            cancel_clear_speech();
            cancel_speech_typing(true);
            if (!speech_raw_buf[0]) set_speech("...");
            break;
        case AI_STATE_SPEAKING:
            cancel_clear_speech();
            start_speech_typing();
            break;
        case AI_STATE_CONNECTING:
            cancel_clear_speech();
            break;
        case AI_STATE_IDLE:
            finish_speech_typing();
            schedule_clear_speech();
            break;
        default:
            cancel_clear_speech();
            cancel_speech_typing(true);
            cancel_speech_fade();
            set_speech("");
            break;
    }
}

static void apply_status_visuals(const ai_status_t *status)
{
    set_emote_src(emote_for_status(status));
}

void ai_free_chat_view_open(ai_free_chat_exit_cb_t exit_cb, void *user_data)
{
    lv_obj_t *layer;
    lv_obj_t *body;
    lv_obj_t *body_alt;
    lv_coord_t hor;
    lv_coord_t ver;

    if (view_root) return;

    exit_requested_cb = exit_cb;
    exit_requested_user_data = user_data;
    exit_requested = false;
    current_emote = NULL;
    pending_emote = NULL;
    last_click_tick = 0;
    last_emote_switch_tick = 0;
    speech_text_buf[0] = '\0';
    speech_raw_buf[0] = '\0';
    speech_target_buf[0] = '\0';
    speech_visible_bytes = 0U;
    speech_last_roll_codepoints = 0U;
    speech_state = AI_STATE_IDLE;

    hor = lv_display_get_horizontal_resolution(NULL);
    ver = lv_display_get_vertical_resolution(NULL);
    layer = lv_layer_top();

    view_root = lv_obj_create(layer);
    lv_obj_set_size(view_root, hor, ver);
    lv_obj_set_pos(view_root, 0, 0);
    lv_obj_set_style_bg_color(view_root, lv_color_hex(0x000303), 0);
    lv_obj_set_style_bg_opa(view_root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(view_root, 0, 0);
    lv_obj_set_style_border_width(view_root, 0, 0);
    lv_obj_set_style_outline_width(view_root, 0, 0);
    lv_obj_set_style_shadow_width(view_root, 0, 0);
    lv_obj_set_style_pad_all(view_root, 0, 0);
    lv_obj_clear_flag(view_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(view_root, root_event_cb, LV_EVENT_DOUBLE_CLICKED, NULL);
    lv_obj_add_event_cb(view_root, root_event_cb, LV_EVENT_CLICKED, NULL);

    for (int y = 18; y < ver; y += 32) {
        lv_obj_t *line = lv_obj_create(view_root);
        lv_obj_set_size(line, hor, 1);
        lv_obj_set_pos(line, 0, y);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x0A442A), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_10, 0);
        lv_obj_set_style_radius(line, 0, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_outline_width(line, 0, 0);
        lv_obj_set_style_shadow_width(line, 0, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }

#if LV_USE_LOTTIE
    lottie_view = lv_lottie_create(view_root);
    lv_obj_set_size(lottie_view, FREE_CHAT_LOTTIE_SIZE, FREE_CHAT_LOTTIE_SIZE);
    lv_lottie_set_buffer(lottie_view, FREE_CHAT_LOTTIE_SIZE, FREE_CHAT_LOTTIE_SIZE, lottie_buf);
    lv_obj_align(lottie_view, LV_ALIGN_CENTER, 0, -28);
    add_bubble(lottie_view);
#else
    lottie_view = NULL;
#endif

    body = lv_label_create(view_root);
    speech_label = body;
    lv_label_set_text(body, "");
    lv_obj_set_size(body, LV_MIN(hor - 180, 760), 56);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD0D4DA), 0);
    lv_obj_set_style_text_opa(body, SPEECH_TEXT_OPA, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, -30);
    add_bubble(body);

    body_alt = lv_label_create(view_root);
    speech_label_alt = body_alt;
    lv_label_set_text(body_alt, "");
    lv_obj_set_size(body_alt, LV_MIN(hor - 180, 760), 56);
    lv_label_set_long_mode(body_alt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body_alt, UI_TEXT_H3, 0);
    lv_obj_set_style_text_color(body_alt, lv_color_hex(0xD0D4DA), 0);
    lv_obj_set_style_text_opa(body_alt, 0, 0);
    lv_obj_set_style_text_align(body_alt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body_alt, LV_ALIGN_BOTTOM_MID, 0, -30);
    add_bubble(body_alt);

    apply_status_visuals(NULL);
}

void ai_free_chat_view_close(void)
{
    if (!view_root) return;

    cancel_pending_emote();
    cancel_clear_speech();
    cancel_speech_typing(true);
    cancel_speech_fade();
    lv_obj_delete(view_root);
    view_root = NULL;
    speech_label = NULL;
    speech_label_alt = NULL;
    lottie_view = NULL;
    exit_requested_cb = NULL;
    exit_requested_user_data = NULL;
    exit_requested = false;
    current_emote = NULL;
    pending_emote = NULL;
    last_click_tick = 0;
    last_emote_switch_tick = 0;
    speech_text_buf[0] = '\0';
    speech_raw_buf[0] = '\0';
    speech_target_buf[0] = '\0';
    speech_visible_bytes = 0U;
    speech_last_roll_codepoints = 0U;
    speech_state = AI_STATE_IDLE;
}

bool ai_free_chat_view_is_open(void)
{
    return view_root != NULL;
}

void ai_free_chat_view_set_status(const ai_status_t *status)
{
    if (!view_root || !status) return;

    if (exit_requested && !status->free_chat_active && status->chat_mode != AI_CHAT_MODE_FREE) {
        ai_free_chat_view_close();
        return;
    }

    apply_status_visuals(status);
    apply_speech_for_state(status->state);
}

void ai_free_chat_view_set_text(const char *text)
{
    size_t old_len;
    bool cumulative;

    if (!speech_label || !text || !text[0]) return;

    cancel_clear_speech();
    if (strncmp(text, "Assistant: ", strlen("Assistant: ")) != 0) {
        cancel_speech_typing(true);
        set_speech(text);
        return;
    }

    old_len = strlen(speech_target_buf);
    cumulative = old_len > 0U && strncmp(text, speech_target_buf, old_len) == 0;
    if (!cumulative) {
        cancel_speech_typing(true);
    }
    snprintf(speech_target_buf, sizeof(speech_target_buf), "%s", text);
    if (speech_state == AI_STATE_SPEAKING) {
        start_speech_typing();
    }
}
