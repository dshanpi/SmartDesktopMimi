/**
 * @file video_app.c
 * @brief Video Player App - Refactored for Fullscreen Overlay Design
 */

#include "video_app.h"
#include "../../ui/theme/theme.h"
#include "../../system/app_manager.h"
#include "../../middleware/middleware.h"
#include "../../system/backend_types.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*****************************************************************************
 *                                 Resources
 *****************************************************************************/
LV_IMAGE_DECLARE(video);
LV_IMAGE_DECLARE(arrow_back_ios_28dp_FFFFFF);
LV_IMAGE_DECLARE(play_arrow_40dp_FFFFFF_FILL0_wght0_GRAD0_opsz40);
LV_IMAGE_DECLARE(pause_40dp_FFFFFF);
LV_IMAGE_DECLARE(fast_rewind_40dp_FFFFFF);
LV_IMAGE_DECLARE(fast_forward_40dp_FFFFFF);
LV_IMAGE_DECLARE(playlist_play_40dp_FFFFFF);

LV_FONT_DECLARE(font_inter_semibold_32)
LV_FONT_DECLARE(font_inter_medium_24)
LV_FONT_DECLARE(font_inter_regular_20)

/*****************************************************************************
 *                                 Constants
 *****************************************************************************/

/* Layout dimensions */
#define SCREEN_WIDTH        1024
#define SCREEN_HEIGHT       768
#define HEADER_HEIGHT       UI_APP_HEADER_H
#define CONTROL_BAR_HEIGHT  120

/* Styling */
#define COLOR_BG            lv_color_hex(0x121212)
#define COLOR_OVERLAY_BG    lv_color_hex(0x000000)
#define OPA_OVERLAY         LV_OPA_80
#define COLOR_INACTIVE      lv_color_hex(0x444444)
#define COLOR_TEXT_PRIMARY  lv_color_white()
#define COLOR_TEXT_MUTED    lv_color_hex(0xB9C0CC)
#define FONT_TITLE          UI_TEXT_H2
#define FONT_TIME           UI_TEXT_BODY_LG
#define FONT_TEXT           UI_TEXT_BODY_LG

/*****************************************************************************
 *                                 UI Components
 *****************************************************************************/

/* Main container */
static lv_obj_t * video_screen = NULL;
static lv_obj_t * main_cont = NULL;

/* Layers */
static lv_obj_t * video_layer = NULL;
static lv_obj_t * letterbox_top = NULL;
static lv_obj_t * letterbox_bottom = NULL;
static lv_obj_t * letterbox_left = NULL;
static lv_obj_t * letterbox_right = NULL;
static lv_obj_t * overlay_layer = NULL;
static lv_obj_t * header_cont = NULL;
static lv_obj_t * controls_cont = NULL;
static lv_obj_t * video_hint_card = NULL;
static lv_obj_t * current_title_label = NULL;
static lv_obj_t * current_status_label = NULL;

/* Controls */
static lv_obj_t * progress_bar = NULL;
static lv_obj_t * time_label_curr = NULL;
static lv_obj_t * time_label_total = NULL;
static lv_obj_t * play_control_btn = NULL;
static lv_obj_t * play_control_icon = NULL;

/* Playlist */
static lv_obj_t * playlist_mask = NULL;
static lv_obj_t * playlist_panel = NULL;
static lv_obj_t * playlist_content = NULL;
static lv_obj_t * playlist_count_label = NULL;
static lv_obj_t * playlist_empty_label = NULL;
static lv_obj_t * playlist_refresh_btn = NULL;
static bool playlist_open = false;

/* Toast / error banner (unsupported media etc.) */
static lv_obj_t * toast_card = NULL;
static lv_obj_t * toast_title = NULL;
static lv_obj_t * toast_body = NULL;
static lv_timer_t * toast_timer = NULL;

/* Playlist Data */
#define PLAYLIST_CACHE_MAX 300
static video_playlist_item_t playlist_cache[PLAYLIST_CACHE_MAX];
static int playlist_cache_count = 0;
static int video_count = 0;
static int current_video_index = -1;
static bool playlist_scan_in_progress = false;
static bool playlist_has_snapshot = false;

/* Timer for auto-hide */
static lv_timer_t * auto_hide_timer = NULL;

/*****************************************************************************
 *                                 State Variables
 *****************************************************************************/

static bool is_playing = false;
static bool is_paused = false;   /* paused keeps hardware frame; do not black out */
static bool controls_visible = true;
static bool user_seeking = false;
static int32_t pending_seek_sec = -1;
static int32_t last_total_sec = 0;
static int32_t last_current_sec = 0;

/*****************************************************************************
 *                                 Callbacks
 *****************************************************************************/

/* Forward Declarations */
static void scan_video_files(void);
static void toggle_playlist(void);
static void send_video_cmd(int action, int val, const char * path);
static void create_playlist_header(const char *status_text);
static void show_toast(const char *title, const char *body, uint32_t hold_ms);
static void hide_toast(void);
static void update_progress_ui(int32_t cur, int32_t total, bool force);
static void rebuild_playlist_rows(void);
static void scroll_playlist_to_current(void);
static lv_obj_t *create_playlist_item(lv_obj_t *parent, const video_playlist_item_t *it,
                                      bool active);

static void playlist_close_cb(lv_event_t * e);

static void toggle_playlist(void) {
    playlist_open = !playlist_open;
    
    if(playlist_open) {
        lv_obj_clear_flag(playlist_mask, LV_OBJ_FLAG_HIDDEN);
        
        if (!playlist_has_snapshot && !playlist_scan_in_progress) {
            scan_video_files();
        } else if (playlist_has_snapshot) {
            /* Re-draw so "now playing" highlight matches current index */
            rebuild_playlist_rows();
            scroll_playlist_to_current();
        }
        
        /* Slide in: 1024 -> 624 (panel width 400) */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, playlist_panel);
        lv_anim_set_values(&a, SCREEN_WIDTH, SCREEN_WIDTH - 400);
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_start(&a);
    } else {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, playlist_panel);
        lv_anim_set_values(&a, SCREEN_WIDTH - 400, SCREEN_WIDTH);
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_set_completed_cb(&a, (lv_anim_completed_cb_t)playlist_close_cb);
        lv_anim_start(&a);
    }
}

static void playlist_close_cb(lv_event_t * e) {
    (void)e; // Unused
    lv_obj_add_flag(playlist_mask, LV_OBJ_FLAG_HIDDEN);
}

static void playlist_mask_click_cb(lv_event_t * e) {
    (void)e;
    if(playlist_open) {
        toggle_playlist();
    }
}

static void reset_auto_hide_timer(void) {
    if(auto_hide_timer) {
        lv_timer_reset(auto_hide_timer);
        if(!controls_visible) {
            controls_visible = true;
            lv_obj_clear_flag(header_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(controls_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void set_current_video_text(const char *title, const char *status, bool is_error) {
    if (current_title_label) {
        lv_label_set_text(current_title_label, title && title[0] ? title : "Choose a video");
    }
    if (current_status_label) {
        lv_label_set_text(current_status_label, status && status[0] ? status : "Ready");
        lv_obj_set_style_text_color(current_status_label,
                                    is_error ? lv_color_hex(0xFF8A80) : COLOR_TEXT_MUTED, 0);
    }
}

static void hide_toast(void)
{
    if (toast_timer) {
        lv_timer_del(toast_timer);
        toast_timer = NULL;
    }
    if (toast_card)
        lv_obj_add_flag(toast_card, LV_OBJ_FLAG_HIDDEN);
}

static void toast_timeout_cb(lv_timer_t * t)
{
    (void)t;
    hide_toast();
}

static void show_toast(const char *title, const char *body, uint32_t hold_ms)
{
    if (!toast_card)
        return;
    if (toast_title)
        lv_label_set_text(toast_title, title && title[0] ? title : "Notice");
    if (toast_body)
        lv_label_set_text(toast_body, body && body[0] ? body : "");
    lv_obj_clear_flag(toast_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_card);
    reset_auto_hide_timer();
    /* Keep chrome visible while toast is up */
    controls_visible = true;
    if (header_cont) lv_obj_clear_flag(header_cont, LV_OBJ_FLAG_HIDDEN);
    if (controls_cont) lv_obj_clear_flag(controls_cont, LV_OBJ_FLAG_HIDDEN);

    if (toast_timer) {
        lv_timer_del(toast_timer);
        toast_timer = NULL;
    }
    if (hold_ms > 0)
        toast_timer = lv_timer_create(toast_timeout_cb, hold_ms, NULL);
}

static void format_time_mmss(int32_t sec, char *buf, size_t buflen)
{
    if (sec < 0) sec = 0;
    snprintf(buf, buflen, "%d:%02d", sec / 60, sec % 60);
}

static void update_progress_ui(int32_t cur, int32_t total, bool force)
{
    char buf[16];

    if (!progress_bar)
        return;

    if (user_seeking && !force)
        return;

    if (total > 0) {
        if (force || total != last_total_sec)
            lv_slider_set_range(progress_bar, 0, total);
        if (cur < 0) cur = 0;
        if (cur > total) cur = total;
        if (force || cur != last_current_sec || total != last_total_sec)
            lv_slider_set_value(progress_bar, cur, LV_ANIM_OFF);
        last_total_sec = total;
        last_current_sec = cur;
        if (time_label_curr) {
            format_time_mmss(cur, buf, sizeof(buf));
            lv_label_set_text(time_label_curr, buf);
        }
        if (time_label_total) {
            format_time_mmss(total, buf, sizeof(buf));
            lv_label_set_text(time_label_total, buf);
        }
    } else {
        /* Duration unknown: keep slider at 0, show elapsed only. */
        last_total_sec = 0;
        last_current_sec = cur > 0 ? cur : 0;
        lv_slider_set_range(progress_bar, 0, 1);
        lv_slider_set_value(progress_bar, 0, LV_ANIM_OFF);
        if (time_label_curr) {
            format_time_mmss(cur > 0 ? cur : 0, buf, sizeof(buf));
            lv_label_set_text(time_label_curr, buf);
        }
        if (time_label_total)
            lv_label_set_text(time_label_total, "--:--");
    }
}

static void auto_hide_cb(lv_timer_t * timer) {
    (void)timer;
    /* Keep chrome if toast or playlist is open */
    if (toast_card && !lv_obj_has_flag(toast_card, LV_OBJ_FLAG_HIDDEN))
        return;
    if (playlist_open)
        return;
    if (controls_visible) {
        controls_visible = false;
        lv_obj_add_flag(header_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(controls_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

static void screen_click_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reset_auto_hide_timer();
    }
}

static void back_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reset_auto_hide_timer();
        printf("[Video] Back button clicked\n");
        app_manager_back_home();
    }
}

static void send_video_cmd(int action, int val, const char * path) {
    printf("[VideoApp] Sending CMD: action=%d, val=%d, path=%s\n", action, val, path ? path : "NULL");
    video_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = action;
    cmd.val = val;
    if (path) {
        strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    }
    mw_publish(TOPIC_VIDEO_CONTROL, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static void update_play_state(bool playing) {
    if (playing) {
        if (!is_playing) {
             send_video_cmd(VIDEO_CMD_PLAY, -1, NULL); // Resume
        }
    } else {
        if (is_playing) {
            send_video_cmd(VIDEO_CMD_PAUSE, 0, NULL);
        }
    }
    // is_playing state will be updated by backend status msg
}

static void play_button_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        hide_toast();
        update_play_state(!is_playing);
    }
}

static void prev_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reset_auto_hide_timer();
        hide_toast(); /* don't leave "无法播放" over the next file */
        printf("[Video] Previous\n");
        send_video_cmd(VIDEO_CMD_PREV, 0, NULL);
    }
}

static void next_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reset_auto_hide_timer();
        hide_toast();
        printf("[Video] Next\n");
        send_video_cmd(VIDEO_CMD_NEXT, 0, NULL);
    }
}

static void list_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reset_auto_hide_timer();
        printf("[Video] Playlist\n");
        toggle_playlist();
    }
}

/*****************************************************************************
 *                                 Helper Functions
 *****************************************************************************/

/**
 * @brief Create an image button with auto-scaling
 */
static lv_obj_t * create_image_btn(lv_obj_t * parent, const void * img_src, int32_t size, lv_event_cb_t cb) {
    lv_obj_t * btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    if(cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    
    lv_obj_t * icon = lv_image_create(btn);
    lv_image_set_src(icon, img_src);
    lv_obj_center(icon);
    
    return btn;
}

static void playlist_refresh_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    scan_video_files();
}

static void create_playlist_header(const char *status_text) {
    if (!playlist_content) return;

    lv_obj_t * title_box = lv_obj_create(playlist_content);
    lv_obj_remove_style_all(title_box);
    lv_obj_set_width(title_box, lv_pct(100));
    lv_obj_set_height(title_box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(title_box, 4, 0);
    lv_obj_set_style_pad_right(title_box, 4, 0);
    lv_obj_set_style_pad_top(title_box, 8, 0);
    lv_obj_set_style_pad_bottom(title_box, 10, 0);
    lv_obj_set_flex_flow(title_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(title_box, 6, 0);
    lv_obj_clear_flag(title_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * row = lv_obj_create(title_box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 32);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * pl_title = lv_label_create(row);
    lv_label_set_text(pl_title, "Video Library");
    lv_obj_set_style_text_font(pl_title, FONT_TITLE, 0);
    lv_obj_set_style_text_color(pl_title, COLOR_TEXT_PRIMARY, 0);

    playlist_refresh_btn = lv_obj_create(row);
    lv_obj_remove_style_all(playlist_refresh_btn);
    lv_obj_set_size(playlist_refresh_btn, 72, 28);
    lv_obj_set_style_radius(playlist_refresh_btn, 6, 0);
    lv_obj_set_style_bg_color(playlist_refresh_btn, lv_color_hex(0x243044), 0);
    lv_obj_set_style_bg_opa(playlist_refresh_btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(playlist_refresh_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(playlist_refresh_btn, playlist_refresh_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t * rl = lv_label_create(playlist_refresh_btn);
        lv_label_set_text(rl, "Refresh");
        lv_obj_set_style_text_font(rl, UI_TEXT_BODY_MD, 0);
        lv_obj_set_style_text_color(rl, COLOR_TEXT_PRIMARY, 0);
        lv_obj_center(rl);
    }

    playlist_count_label = lv_label_create(title_box);
    lv_label_set_text(playlist_count_label, status_text ? status_text : "Scanning the microSD card...");
    lv_obj_set_style_text_font(playlist_count_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(playlist_count_label, COLOR_TEXT_MUTED, 0);

    lv_obj_t * hint = lv_label_create(title_box);
    lv_label_set_text(hint, "Up to 4K@30 · Compatible videos first · Unsupported files skipped");
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(hint, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x7A8494), 0);
}

static void format_file_type(const char *filename, char *buf, size_t buf_size) {
    const char *ext = filename ? strrchr(filename, '.') : NULL;
    size_t out = 0;

    if (!buf || buf_size == 0) return;
    if (!ext || !ext[1]) {
        snprintf(buf, buf_size, "VID");
        return;
    }

    ext++;
    while (ext[out] && out + 1 < buf_size && out < 4) {
        buf[out] = (char)toupper((unsigned char)ext[out]);
        out++;
    }
    buf[out] = '\0';
}

static void scan_video_files(void) {
    if (playlist_scan_in_progress) {
        return;
    }

    playlist_scan_in_progress = true;
    playlist_has_snapshot = false;
    video_count = 0;
    playlist_cache_count = 0;
    memset(playlist_cache, 0, sizeof(playlist_cache));

    /* Clear current UI list */
    if (playlist_content) {
        lv_obj_clean(playlist_content);
        playlist_count_label = NULL;
        playlist_empty_label = NULL;
        playlist_refresh_btn = NULL;
        create_playlist_header("Scanning for videos...");
    }

    /* Request backend to scan */
    send_video_cmd(VIDEO_CMD_SCAN_FILES, 0, NULL);
}

typedef struct {
    int index;
    uint8_t unsupported;
    char reason[80];
} playlist_item_ud_t;

static void playlist_item_delete_cb(lv_event_t * e)
{
    void *p = lv_event_get_user_data(e);
    if (p)
        lv_free(p);
}

static void playlist_item_click_cb(lv_event_t * e) {
    playlist_item_ud_t *ud = (playlist_item_ud_t *)lv_event_get_user_data(e);
    if (!ud)
        return;
    printf("[Video] Playlist item clicked: %d unsupported=%u\n", ud->index, ud->unsupported);
    reset_auto_hide_timer();
    if (ud->unsupported) {
        show_toast("Unable to Play This Video",
                   ud->reason[0] ? ud->reason : "Unsupported format or resolution (maximum 4K@30)",
                   4500);
        /* Keep list open so user can pick another */
        return;
    }
    hide_toast();
    send_video_cmd(VIDEO_CMD_PLAY, ud->index, NULL);
    toggle_playlist();
}

static const char * basename_only(const char *path)
{
    const char *s = path ? strrchr(path, '/') : NULL;
    return s ? s + 1 : (path ? path : "");
}

static void format_meta_line(const video_playlist_item_t *it, bool now_playing,
                             char *buf, size_t buflen)
{
    char spec[40];

    /* Keep short — list column is narrow; full reason goes to toast. */
    if (it->unsupported) {
        if (it->width > 0 && it->height > 0 && it->fps > 0)
            snprintf(buf, buflen, "Unsupported · %dx%d@%d", it->width, it->height, it->fps);
        else if (it->width > 0 && it->height > 0)
            snprintf(buf, buflen, "Unsupported · %dx%d", it->width, it->height);
        else
            snprintf(buf, buflen, "Unsupported · Above 4K@30");
        return;
    }

    if (it->width > 0 && it->height > 0 && it->fps > 0)
        snprintf(spec, sizeof(spec), "%dx%d · %dfps", it->width, it->height, it->fps);
    else if (it->width > 0 && it->height > 0)
        snprintf(spec, sizeof(spec), "%dx%d", it->width, it->height);
    else
        snprintf(spec, sizeof(spec), "Unknown format");

    if (now_playing && (is_playing || is_paused))
        snprintf(buf, buflen, "%s · %s", is_paused ? "Paused" : "Playing", spec);
    else
        snprintf(buf, buflen, "%s", spec);
}

/* Rebuild list body from cache so current playing row stays highlighted. */
static void rebuild_playlist_rows(void)
{
    char header_status[48];
    int i;

    if (!playlist_content)
        return;

    if (video_count > 0)
        snprintf(header_status, sizeof(header_status), "%d video%s", video_count, video_count == 1 ? "" : "s");
    else
        snprintf(header_status, sizeof(header_status), "No videos found");

    lv_obj_clean(playlist_content);
    playlist_count_label = NULL;
    playlist_empty_label = NULL;
    playlist_refresh_btn = NULL;
    create_playlist_header(header_status);

    for (i = 0; i < playlist_cache_count; i++) {
        bool active = (playlist_cache[i].index == current_video_index) &&
                      (is_playing || is_paused);
        create_playlist_item(playlist_content, &playlist_cache[i], active);
    }

    if (playlist_cache_count == 0 && !playlist_empty_label) {
        playlist_empty_label = lv_label_create(playlist_content);
        lv_label_set_text(playlist_empty_label,
                          "No videos found\nAdd MP4 or MKV files to the microSD card.\nIt mounts automatically at startup.");
        lv_obj_set_width(playlist_empty_label, lv_pct(100));
        lv_obj_set_style_text_align(playlist_empty_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(playlist_empty_label, UI_TEXT_BODY_LG, 0);
        lv_obj_set_style_text_color(playlist_empty_label, COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_margin_top(playlist_empty_label, 48, 0);
    }
}

static void scroll_playlist_to_current(void)
{
    uint32_t i, cnt;

    if (!playlist_content || current_video_index < 0)
        return;
    cnt = lv_obj_get_child_count(playlist_content);
    /* child 0 is header */
    for (i = 1; i < cnt; i++) {
        lv_obj_t *row = lv_obj_get_child(playlist_content, i);
        playlist_item_ud_t *ud;
        if (!row)
            continue;
        /* user_data is on CLICKED event; store index on obj via child walk is hard.
         * Rows are created in cache order after header → index i-1 maps cache. */
        if ((int)(i - 1) < playlist_cache_count &&
            playlist_cache[i - 1].index == current_video_index) {
            lv_obj_scroll_to_view(row, LV_ANIM_ON);
            break;
        }
        (void)ud;
    }
}

/*
 * Row layout (flex, no absolute overlap):
 *   [4px bar][badge 40][ title+meta grow ][ #nn 36 ]
 * Panel ~380, content pad 10 → ~360 usable.
 */
static lv_obj_t *create_playlist_item(lv_obj_t * parent, const video_playlist_item_t * it, bool active) {
    playlist_item_ud_t *ud;
    const char * filename = basename_only(it->path);
    char meta[72];
    char type_text[8];
    char index_text[16];
    lv_color_t title_col;
    lv_color_t bg;
    bool now = active && !it->unsupported;

    ud = (playlist_item_ud_t *)lv_malloc(sizeof(*ud));
    if (ud) {
        memset(ud, 0, sizeof(*ud));
        ud->index = it->index;
        ud->unsupported = it->unsupported;
        snprintf(ud->reason, sizeof(ud->reason), "%s", it->reason);
    }

    format_meta_line(it, now, meta, sizeof(meta));
    format_file_type(filename, type_text, sizeof(type_text));
    snprintf(index_text, sizeof(index_text), "%02d", it->index + 1);

    /* Current playing/paused: stronger highlight so user sees which file is on. */
    if (now)
        bg = lv_color_mix(UI_COLOR_PRIMARY, lv_color_hex(0x0B1220), 100);
    else
        bg = (it->index % 2) ? lv_color_hex(0x151D29) : lv_color_hex(0x101722);

    title_col = it->unsupported ? COLOR_TEXT_MUTED
                : (now ? UI_COLOR_PRIMARY : COLOR_TEXT_PRIMARY);

    lv_obj_t * item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, lv_pct(100));
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(item, 72, 0);
    lv_obj_set_style_bg_color(item, bg, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(0x263244), 0);
    lv_obj_set_style_border_opa(item, LV_OPA_70, 0);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(item, 6, 0);
    lv_obj_set_style_pad_left(item, 8, 0);
    lv_obj_set_style_pad_right(item, 8, 0);
    lv_obj_set_style_pad_ver(item, 10, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 8, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    if (ud) {
        lv_obj_add_event_cb(item, playlist_item_click_cb, LV_EVENT_CLICKED, ud);
        lv_obj_add_event_cb(item, playlist_item_delete_cb, LV_EVENT_DELETE, ud);
    }

    /* Left marker: primary when now-playing, soft cue when unsupported */
    {
        lv_obj_t * bar = lv_obj_create(item);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, now ? 4 : 3, 44);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
        if (it->unsupported) {
            lv_obj_set_style_bg_color(bar, lv_color_hex(0xE57373), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        } else if (now) {
            lv_obj_set_style_bg_color(bar, UI_COLOR_PRIMARY, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        }
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Type badge */
    {
        lv_obj_t * type_badge = lv_obj_create(item);
        lv_obj_remove_style_all(type_badge);
        lv_obj_set_size(type_badge, now ? 48 : 40, 24);
        lv_obj_set_style_radius(type_badge, 4, 0);
        lv_obj_set_style_bg_color(type_badge,
                                  it->unsupported ? lv_color_hex(0x4A5568)
                                  : (now ? UI_COLOR_PRIMARY : lv_color_hex(0x5B42B8)), 0);
        lv_obj_set_style_bg_opa(type_badge, LV_OPA_COVER, 0);
        lv_obj_clear_flag(type_badge, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t * type_label = lv_label_create(type_badge);
        lv_label_set_text(type_label,
                          it->unsupported ? "Skip" : (now ? "Play" : type_text));
        lv_obj_set_style_text_font(type_label, UI_TEXT_BODY_MD, 0);
        lv_obj_set_style_text_color(type_label, lv_color_white(), 0);
        lv_obj_center(type_label);
    }

    /* Title + meta: takes remaining width (flex grow) */
    {
        lv_obj_t * text_col = lv_obj_create(item);
        lv_obj_remove_style_all(text_col);
        lv_obj_set_flex_grow(text_col, 1);
        lv_obj_set_height(text_col, LV_SIZE_CONTENT);
        lv_obj_set_style_min_width(text_col, 0, 0); /* allow shrink for LONG_DOT */
        lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(text_col, 4, 0);
        lv_obj_clear_flag(text_col, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * lbl_title = lv_label_create(text_col);
        lv_label_set_text(lbl_title, filename);
        lv_obj_set_width(lbl_title, lv_pct(100));
        lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(lbl_title, FONT_TEXT, 0);
        lv_obj_set_style_text_color(lbl_title, title_col, 0);

        lv_obj_t * lbl_meta = lv_label_create(text_col);
        lv_label_set_text(lbl_meta, meta);
        lv_obj_set_width(lbl_meta, lv_pct(100));
        lv_label_set_long_mode(lbl_meta, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(lbl_meta, UI_TEXT_BODY_MD, 0);
        lv_obj_set_style_text_color(lbl_meta, COLOR_TEXT_MUTED, 0);
    }

    /* Index — fixed, never overlaps title */
    {
        lv_obj_t * index_label = lv_label_create(item);
        lv_label_set_text(index_label, index_text);
        lv_obj_set_style_text_font(index_label, UI_TEXT_BODY_MD, 0);
        lv_obj_set_style_text_color(index_label,
                                    now ? UI_COLOR_PRIMARY : COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_min_width(index_label, 28, 0);
        lv_obj_set_style_text_align(index_label, LV_TEXT_ALIGN_RIGHT, 0);
    }

    return item;
}

static void progress_bar_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * slider = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        user_seeking = true;
        reset_auto_hide_timer();
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t value = lv_slider_get_value(slider);
        pending_seek_sec = value;
        if (time_label_curr) {
            char buf[16];
            format_time_mmss(value, buf, sizeof(buf));
            lv_label_set_text(time_label_curr, buf);
        }
        reset_auto_hide_timer();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int32_t value = pending_seek_sec >= 0 ? pending_seek_sec : lv_slider_get_value(slider);
        printf("[Video] Seek to %d\n", value);
        send_video_cmd(VIDEO_CMD_SEEK, value, NULL);
        pending_seek_sec = -1;
        user_seeking = false;
        reset_auto_hide_timer();
    }
}

/*
 * Video is under the UI FB: transparent pixels show the HW plane (or the
 * desktop if the plane does not cover that pixel). Dawn theme is reddish —
 * any transparent gap without video looks like a "淡淡红" wash.
 *
 * Strategy: full-screen black cover when idle; when playing/paused only the
 * letterboxed video rect is transparent, and solid black bars fill the rest.
 */
static void layout_letterbox(int src_w, int src_h, bool show_video)
{
    int disp_x = 0, disp_y = 0, disp_w = SCREEN_WIDTH, disp_h = SCREEN_HEIGHT;

    if (!video_layer)
        return;

    if (!show_video) {
        lv_obj_set_style_bg_color(video_layer, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(video_layer, LV_OPA_COVER, 0);
        if (letterbox_top) lv_obj_add_flag(letterbox_top, LV_OBJ_FLAG_HIDDEN);
        if (letterbox_bottom) lv_obj_add_flag(letterbox_bottom, LV_OBJ_FLAG_HIDDEN);
        if (letterbox_left) lv_obj_add_flag(letterbox_left, LV_OBJ_FLAG_HIDDEN);
        if (letterbox_right) lv_obj_add_flag(letterbox_right, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Transparent hole for HW video */
    lv_obj_set_style_bg_opa(video_layer, LV_OPA_TRANSP, 0);

    if (src_w > 0 && src_h > 0) {
        float rw = (float)SCREEN_WIDTH / (float)src_w;
        float rh = (float)SCREEN_HEIGHT / (float)src_h;
        float sc = (rw < rh) ? rw : rh;
        disp_w = (int)(src_w * sc);
        disp_h = (int)(src_h * sc);
        if (disp_w < 1) disp_w = 1;
        if (disp_h < 1) disp_h = 1;
        disp_x = (SCREEN_WIDTH - disp_w) / 2;
        disp_y = (SCREEN_HEIGHT - disp_h) / 2;
    }

    /* Top / bottom bars */
    if (letterbox_top) {
        lv_obj_set_pos(letterbox_top, 0, 0);
        lv_obj_set_size(letterbox_top, SCREEN_WIDTH, disp_y > 0 ? disp_y : 0);
        if (disp_y > 0) lv_obj_clear_flag(letterbox_top, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(letterbox_top, LV_OBJ_FLAG_HIDDEN);
    }
    if (letterbox_bottom) {
        int y = disp_y + disp_h;
        int h = SCREEN_HEIGHT - y;
        lv_obj_set_pos(letterbox_bottom, 0, y);
        lv_obj_set_size(letterbox_bottom, SCREEN_WIDTH, h > 0 ? h : 0);
        if (h > 0) lv_obj_clear_flag(letterbox_bottom, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(letterbox_bottom, LV_OBJ_FLAG_HIDDEN);
    }
    /* Left / right bars (portrait) */
    if (letterbox_left) {
        lv_obj_set_pos(letterbox_left, 0, disp_y);
        lv_obj_set_size(letterbox_left, disp_x > 0 ? disp_x : 0, disp_h);
        if (disp_x > 0) lv_obj_clear_flag(letterbox_left, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(letterbox_left, LV_OBJ_FLAG_HIDDEN);
    }
    if (letterbox_right) {
        int x = disp_x + disp_w;
        int w = SCREEN_WIDTH - x;
        lv_obj_set_pos(letterbox_right, x, disp_y);
        lv_obj_set_size(letterbox_right, w > 0 ? w : 0, disp_h);
        if (w > 0) lv_obj_clear_flag(letterbox_right, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(letterbox_right, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_video_plane_visible(bool visible, int src_w, int src_h)
{
    layout_letterbox(src_w, src_h, visible);
}

static void on_video_status(const mw_msg_t * msg) {
    if (msg->data_len != sizeof(video_status_t)) {
        printf("[VideoApp] status size mismatch got %u expect %zu\n",
               msg->data_len, sizeof(video_status_t));
        return;
    }
    video_status_t * status = (video_status_t *)msg->data;
    bool is_error = (status->status_text[0] != '\0' &&
                     !status->is_playing && !status->is_paused);
    bool has_frame; /* hardware layer still has a valid picture */

    is_playing = status->is_playing;
    is_paused = status->is_paused;
    has_frame = is_playing || is_paused;

    {
        const void * icon_src = is_playing ? &pause_40dp_FFFFFF
                                           : &play_arrow_40dp_FFFFFF_FILL0_wght0_GRAD0_opsz40;
        if (play_control_icon)
            lv_image_set_src(play_control_icon, icon_src);
    }

    /* Pause keeps frame; letterbox blacks block desktop theme bleed (dawn red). */
    set_video_plane_visible(has_frame && !is_error, status->source_w, status->source_h);

    if (video_hint_card) {
        if (status->current_file[0] || status->total_sec > 0 || has_frame || is_error)
            lv_obj_add_flag(video_hint_card, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(video_hint_card, LV_OBJ_FLAG_HIDDEN);
    }

    if (is_error) {
        const char *title = status->current_file[0] ? status->current_file : "Unable to play";
        set_current_video_text(title, "Unable to play", true);
        show_toast("Unable to Play This Video", status->status_text, 2800);
        set_video_plane_visible(false, 0, 0);
        update_progress_ui(0, 0, true);
    } else if (status->current_file[0] || has_frame) {
        const char *st;
        hide_toast();
        if (is_playing)
            st = "Playing";
        else if (is_paused)
            st = "Paused";
        else
            st = "Stopped";
        set_current_video_text(status->current_file[0] ? status->current_file : "Video",
                               st, false);
        update_progress_ui(status->current_sec, status->total_sec, false);
    } else {
        hide_toast();
        set_current_video_text("Choose a video", "Ready", false);
        set_video_plane_visible(false, 0, 0);
        update_progress_ui(0, 0, true);
    }

    if (status->current_index >= 0) {
        static bool list_was_playing = false;
        static bool list_was_paused = false;
        int prev_idx = current_video_index;
        bool need_list_refresh;

        current_video_index = status->current_index;
        need_list_refresh = (prev_idx != current_video_index) ||
                            (list_was_playing != is_playing) ||
                            (list_was_paused != is_paused);
        list_was_playing = is_playing;
        list_was_paused = is_paused;

        /* Only rebuild when selection/play state changes (not every progress tick). */
        if (need_list_refresh && playlist_open && playlist_has_snapshot)
            rebuild_playlist_rows();
    }
}

static void on_video_playlist(const mw_msg_t * msg) {
    if (msg->data_len != sizeof(video_playlist_item_t)) {
        printf("[VideoApp] Invalid playlist msg len: %d expect %zu\n",
               msg->data_len, sizeof(video_playlist_item_t));
        return;
    }
    video_playlist_item_t * item = (video_playlist_item_t *)msg->data;

    if (item->total_count >= 0) {
        video_count = item->total_count;
        playlist_scan_in_progress = false;
        playlist_has_snapshot = true;
        printf("[VideoApp] Playlist updated, count: %d cache=%d\n",
               video_count, playlist_cache_count);
        /* Final pass: rebuild with correct now-playing highlight */
        rebuild_playlist_rows();
        if (playlist_open)
            scroll_playlist_to_current();
    } else {
        printf("[VideoApp] New item: %s idx=%d unsup=%u %dx%d@%d\n",
               item->path, item->index, item->unsupported,
               item->width, item->height, item->fps);
        if (item->index >= 0 && item->index < PLAYLIST_CACHE_MAX) {
            /* Cache by index; publish order may be 0..n-1 after sort */
            if (item->index >= playlist_cache_count)
                playlist_cache_count = item->index + 1;
            playlist_cache[item->index] = *item;
        }
        /* Defer row create to total_count rebuild for consistent highlight */
    }
}

void video_app_init(void) {
    printf("[Video] App Init\n");
    
    /* Subscribe to Backend */
    mw_subscribe(TOPIC_VIDEO_STATUS, on_video_status);
    mw_subscribe(TOPIC_VIDEO_PLAYLIST, on_video_playlist);
    
    /* Initialize UI */
    
    /*
     * Screen/main must stay transparent where HW video shows (underlay).
     * Idle cover + letterbox bars are pure black to block desktop theme bleed
     * (dawn primary is pink/red — was the "淡淡红").
     */
    video_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(video_screen);
    lv_obj_set_style_bg_opa(video_screen, LV_OPA_TRANSP, 0);

    /* 1. Main Container (Fullscreen, Grid Layout) */
    main_cont = lv_obj_create(video_screen);
    lv_obj_remove_style_all(main_cont);
    lv_obj_set_size(main_cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(main_cont, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Grid Definitions */
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(main_cont, col_dsc, row_dsc);

    /* 2. Video Layer: black when idle; transparent hole + letterbox when playing */
    video_layer = lv_obj_create(main_cont);
    lv_obj_remove_style_all(video_layer);
    lv_obj_set_grid_cell(video_layer, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_bg_color(video_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(video_layer, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(video_layer, screen_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(video_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(video_layer, LV_OBJ_FLAG_SCROLLABLE);

    /* Letterbox bars sit above video_layer, below controls */
    {
        int bi;
        lv_obj_t **bars[4];
        bars[0] = &letterbox_top;
        bars[1] = &letterbox_bottom;
        bars[2] = &letterbox_left;
        bars[3] = &letterbox_right;
        for (bi = 0; bi < 4; bi++) {
            lv_obj_t *b = lv_obj_create(main_cont);
            lv_obj_remove_style_all(b);
            lv_obj_set_style_bg_color(b, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
            *bars[bi] = b;
        }
    }

    /* 3. Overlay Layer (Top Layer, Grid 0,0) - Transparent container for controls */
    overlay_layer = lv_obj_create(main_cont);
    lv_obj_remove_style_all(overlay_layer);
    lv_obj_set_grid_cell(overlay_layer, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_bg_opa(overlay_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(overlay_layer, LV_OBJ_FLAG_CLICKABLE); // Let clicks pass through to video layer

    video_hint_card = lv_obj_create(overlay_layer);
    lv_obj_remove_style_all(video_hint_card);
    lv_obj_set_size(video_hint_card, 420, 248);
    lv_obj_set_style_bg_color(video_hint_card, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(video_hint_card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(video_hint_card, 1, 0);
    lv_obj_set_style_border_color(video_hint_card, lv_color_mix(UI_COLOR_PRIMARY, lv_color_white(), 90), 0);
    lv_obj_set_style_border_opa(video_hint_card, LV_OPA_40, 0);
    lv_obj_set_style_radius(video_hint_card, 18, 0);
    lv_obj_set_style_shadow_width(video_hint_card, 26, 0);
    lv_obj_set_style_shadow_color(video_hint_card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(video_hint_card, LV_OPA_40, 0);
    lv_obj_set_flex_flow(video_hint_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(video_hint_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(video_hint_card, 24, 0);
    lv_obj_set_style_pad_gap(video_hint_card, 12, 0);
    lv_obj_align(video_hint_card, LV_ALIGN_CENTER, 0, -22);

    lv_obj_t * hint_icon = lv_image_create(video_hint_card);
    lv_image_set_src(hint_icon, &video);

    lv_obj_t * hint_title = lv_label_create(video_hint_card);
    lv_label_set_text(hint_title, "Choose a Video");
    lv_obj_set_style_text_font(hint_title, UI_TEXT_H2, 0);
    lv_obj_set_style_text_color(hint_title, COLOR_TEXT_PRIMARY, 0);

    lv_obj_t * hint_text = lv_label_create(video_hint_card);
    lv_label_set_text(hint_text, "Choose a video from the microSD card list.\nPlayback supports up to 4K@30.");
    lv_obj_set_width(hint_text, lv_pct(100));
    lv_obj_set_style_text_align(hint_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint_text, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(hint_text, COLOR_TEXT_MUTED, 0);

    /* Center toast for unsupported / errors */
    toast_card = lv_obj_create(overlay_layer);
    lv_obj_remove_style_all(toast_card);
    lv_obj_set_size(toast_card, 520, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast_card, lv_color_hex(0x1A0F12), 0);
    lv_obj_set_style_bg_opa(toast_card, LV_OPA_90, 0);
    lv_obj_set_style_border_width(toast_card, 2, 0);
    lv_obj_set_style_border_color(toast_card, lv_color_hex(0xE57373), 0);
    lv_obj_set_style_border_opa(toast_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast_card, 14, 0);
    lv_obj_set_style_pad_all(toast_card, 22, 0);
    lv_obj_set_style_pad_gap(toast_card, 10, 0);
    lv_obj_set_flex_flow(toast_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(toast_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_shadow_width(toast_card, 28, 0);
    lv_obj_set_style_shadow_color(toast_card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(toast_card, LV_OPA_50, 0);
    lv_obj_align(toast_card, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_flag(toast_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(toast_card, LV_OBJ_FLAG_CLICKABLE);

    toast_title = lv_label_create(toast_card);
    lv_label_set_text(toast_title, "Unable to Play This Video");
    lv_obj_set_style_text_font(toast_title, UI_TEXT_H2, 0);
    lv_obj_set_style_text_color(toast_title, lv_color_hex(0xFFCDD2), 0);

    toast_body = lv_label_create(toast_card);
    lv_label_set_text(toast_body, "");
    lv_obj_set_width(toast_body, 460);
    lv_label_set_long_mode(toast_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(toast_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(toast_body, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(toast_body, lv_color_hex(0xFFCCBC), 0);

    {
        lv_obj_t * toast_hint = lv_label_create(toast_card);
        lv_label_set_text(toast_hint, "Choose a video at 4K@30 or below");
        lv_obj_set_style_text_font(toast_hint, UI_TEXT_BODY_MD, 0);
        lv_obj_set_style_text_color(toast_hint, COLOR_TEXT_MUTED, 0);
    }

    /* ===== HEADER (Top, Floating) ===== */
    header_cont = lv_obj_create(overlay_layer);
    lv_obj_remove_style_all(header_cont);
    lv_obj_set_size(header_cont, lv_pct(100), HEADER_HEIGHT);
    lv_obj_set_style_bg_color(header_cont, COLOR_OVERLAY_BG, 0);
    lv_obj_set_style_bg_opa(header_cont, OPA_OVERLAY, 0);
    lv_obj_set_style_border_width(header_cont, 1, 0);
    lv_obj_set_style_border_color(header_cont, lv_color_white(), 0);
    lv_obj_set_style_border_opa(header_cont, LV_OPA_10, 0);
    lv_obj_set_style_border_side(header_cont, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_align(header_cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(header_cont, LV_OBJ_FLAG_CLICKABLE); // Catch clicks

    /* Back Button (Absolute Left) */
    lv_obj_t * back_btn = create_image_btn(header_cont, &arrow_back_ios_28dp_FFFFFF, 50, back_event_handler);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 18, 0);

    /* Title (Centered) */
    lv_obj_t * title = lv_label_create(header_cont);
    lv_label_set_text(title, "Video Player");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, FONT_TITLE, 0);
    lv_obj_center(title);

    current_title_label = lv_label_create(header_cont);
    lv_label_set_text(current_title_label, "Choose a video");
    lv_obj_set_width(current_title_label, 320);
    lv_label_set_long_mode(current_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(current_title_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(current_title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(current_title_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(current_title_label, LV_ALIGN_RIGHT_MID, -24, -10);

    current_status_label = lv_label_create(header_cont);
    lv_label_set_text(current_status_label, "Ready");
    lv_obj_set_width(current_status_label, 320);
    lv_label_set_long_mode(current_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(current_status_label, UI_TEXT_BODY_MD, 0);
    lv_obj_set_style_text_color(current_status_label, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(current_status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(current_status_label, LV_ALIGN_RIGHT_MID, -24, 14);

    /* ===== CONTROLS (Bottom, Floating) ===== */
    controls_cont = lv_obj_create(overlay_layer);
    lv_obj_remove_style_all(controls_cont);
    lv_obj_set_size(controls_cont, lv_pct(100), CONTROL_BAR_HEIGHT);
    lv_obj_set_style_bg_color(controls_cont, COLOR_OVERLAY_BG, 0);
    lv_obj_set_style_bg_opa(controls_cont, OPA_OVERLAY, 0);
    lv_obj_set_style_border_width(controls_cont, 1, 0);
    lv_obj_set_style_border_color(controls_cont, lv_color_white(), 0);
    lv_obj_set_style_border_opa(controls_cont, LV_OPA_10, 0);
    lv_obj_set_style_border_side(controls_cont, LV_BORDER_SIDE_TOP, 0);
    lv_obj_align(controls_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(controls_cont, LV_OBJ_FLAG_CLICKABLE); // Catch clicks

    /* Progress Bar Container (Top part of controls) */
    lv_obj_t * progress_row = lv_obj_create(controls_cont);
    lv_obj_remove_style_all(progress_row);
    lv_obj_set_size(progress_row, lv_pct(100), 40);
    lv_obj_align(progress_row, LV_ALIGN_TOP_MID, 0, 10);

    /* Time Current */
    time_label_curr = lv_label_create(progress_row);
    lv_label_set_text(time_label_curr, "0:00");
    lv_obj_set_style_text_color(time_label_curr, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(time_label_curr, FONT_TIME, 0);
    lv_obj_align(time_label_curr, LV_ALIGN_LEFT_MID, 32, 0);

    /* Time Total */
    time_label_total = lv_label_create(progress_row);
    lv_label_set_text(time_label_total, "0:00");
    lv_obj_set_style_text_color(time_label_total, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(time_label_total, FONT_TIME, 0);
    lv_obj_align(time_label_total, LV_ALIGN_RIGHT_MID, -32, 0);

    /* Slider — taller track + larger knob for touch / visibility */
    progress_bar = lv_slider_create(progress_row);
    lv_obj_set_height(progress_bar, 10);
    lv_obj_set_width(progress_bar, 700);
    lv_obj_center(progress_bar);
    lv_slider_set_range(progress_bar, 0, 1);
    lv_slider_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar, COLOR_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_pad_all(progress_bar, 10, LV_PART_KNOB);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_add_event_cb(progress_bar, progress_bar_event_cb, LV_EVENT_ALL, NULL);

    /* Buttons Row (Bottom part of controls) */
    lv_obj_t * buttons_row = lv_obj_create(controls_cont);
    lv_obj_remove_style_all(buttons_row);
    lv_obj_set_size(buttons_row, lv_pct(100), 70);
    lv_obj_align(buttons_row, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Playlist Button (Right) */
    lv_obj_t * list_btn = create_image_btn(buttons_row, &playlist_play_40dp_FFFFFF, 56, list_event_cb);
    lv_obj_align(list_btn, LV_ALIGN_RIGHT_MID, -32, 0);

    /* Center Controls Group */
    lv_obj_t * center_group = lv_obj_create(buttons_row);
    lv_obj_remove_style_all(center_group);
    lv_obj_set_size(center_group, 300, lv_pct(100));
    lv_obj_center(center_group);
    lv_obj_set_flex_flow(center_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(center_group, 40, 0);

    /* Prev */
    create_image_btn(center_group, &fast_rewind_40dp_FFFFFF, 56, prev_event_cb);

    /* Play/Pause (Primary) */
    play_control_btn = create_image_btn(center_group, &play_arrow_40dp_FFFFFF_FILL0_wght0_GRAD0_opsz40, 56, play_button_event_cb);
    lv_obj_set_style_bg_color(play_control_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(play_control_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(play_control_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_width(play_control_btn, 20, 0);
    lv_obj_set_style_shadow_opa(play_control_btn, LV_OPA_30, 0);
    play_control_icon = lv_obj_get_child(play_control_btn, 0);

    /* Next */
    create_image_btn(center_group, &fast_forward_40dp_FFFFFF, 56, next_event_cb);

    /* Initialize state */
    update_play_state(false);
    
    /* Start auto-hide timer */
    auto_hide_timer = lv_timer_create(auto_hide_cb, 3000, NULL);
    reset_auto_hide_timer();

    // progress_timer is no longer needed, we use IPC status updates
    /* ===== PLAYLIST (Slide-out Panel) ===== */
    /* 1. Mask (Click to close) */
    playlist_mask = lv_obj_create(main_cont); // Child of main_cont to cover video
    lv_obj_remove_style_all(playlist_mask);
    lv_obj_set_grid_cell(playlist_mask, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1); // Cover screen using Grid
    lv_obj_set_style_bg_color(playlist_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(playlist_mask, LV_OPA_40, 0);
    lv_obj_add_flag(playlist_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(playlist_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(playlist_mask, playlist_mask_click_cb, LV_EVENT_CLICKED, NULL);

    /* 2. Panel */
    playlist_panel = lv_obj_create(main_cont);
    lv_obj_remove_style_all(playlist_panel);
    lv_obj_set_size(playlist_panel, 400, SCREEN_HEIGHT);
    lv_obj_add_flag(playlist_panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(playlist_panel, SCREEN_WIDTH, 0);
    lv_obj_set_style_bg_color(playlist_panel, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(playlist_panel, 248, 0);
    lv_obj_set_style_border_color(playlist_panel, lv_color_mix(UI_COLOR_PRIMARY, lv_color_white(), 80), 0);
    lv_obj_set_style_border_opa(playlist_panel, LV_OPA_30, 0);
    lv_obj_set_style_border_width(playlist_panel, 1, 0);
    lv_obj_set_style_border_side(playlist_panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_shadow_width(playlist_panel, 40, 0);
    lv_obj_set_style_shadow_color(playlist_panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(playlist_panel, LV_OPA_50, 0);
    lv_obj_clear_flag(playlist_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Panel Content (Scrollable) */
    playlist_content = lv_obj_create(playlist_panel);
    lv_obj_remove_style_all(playlist_content);
    lv_obj_set_size(playlist_content, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(playlist_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(playlist_content, 10, 0);
    lv_obj_set_style_pad_right(playlist_content, 10, 0);
    lv_obj_set_style_pad_top(playlist_content, 8, 0);
    lv_obj_set_style_pad_bottom(playlist_content, 16, 0);
    lv_obj_set_style_pad_row(playlist_content, 6, 0);
    lv_obj_add_flag(playlist_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(playlist_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(playlist_content, LV_SCROLLBAR_MODE_AUTO);
    
    create_playlist_header("Scanning for videos...");

    /* Populate Playlist */
    scan_video_files();

    /* Load Screen */
    lv_scr_load(video_screen);
}

void video_app_close(void) {
    /* Unsubscribe from Backend events to prevent callbacks after UI destruction */
    mw_unsubscribe(TOPIC_VIDEO_STATUS, on_video_status);
    mw_unsubscribe(TOPIC_VIDEO_PLAYLIST, on_video_playlist);

    if (auto_hide_timer) {
        lv_timer_del(auto_hide_timer);
        auto_hide_timer = NULL;
    }
    hide_toast();

    /* Full teardown: stop decoder and cover plane so frame does not leak to desktop. */
    send_video_cmd(VIDEO_CMD_STOP, 0, NULL);
    set_video_plane_visible(false, 0, 0);
    is_playing = false;
    is_paused = false;

    /* Reset static variables */
    video_screen = NULL;
    main_cont = NULL;
    video_layer = NULL;
    letterbox_top = NULL;
    letterbox_bottom = NULL;
    letterbox_left = NULL;
    letterbox_right = NULL;
    overlay_layer = NULL;
    header_cont = NULL;
    controls_cont = NULL;
    video_hint_card = NULL;
    toast_card = NULL;
    toast_title = NULL;
    toast_body = NULL;
    current_title_label = NULL;
    current_status_label = NULL;
    progress_bar = NULL;
    time_label_curr = NULL;
    time_label_total = NULL;
    play_control_btn = NULL;
    playlist_refresh_btn = NULL;
    user_seeking = false;
    last_total_sec = 0;
    last_current_sec = 0;
    play_control_icon = NULL;
    playlist_mask = NULL;
    playlist_panel = NULL;
    playlist_content = NULL;
    playlist_count_label = NULL;
    playlist_empty_label = NULL;
    playlist_open = false;
    playlist_scan_in_progress = false;
    playlist_has_snapshot = false;
    pending_seek_sec = -1;
    
    /* Clean up UI if needed, but AppManager handles screen deletion */
    printf("[Video] App Closed\n");
}

AppDescriptor app_video = {
    .id = APP_ID_VIDEO,
    .name = "Video Player",
    .init = video_app_init,
    .close = video_app_close,
};
