#include "../middleware/middleware.h"
#include "backend_types.h"
#include "../apps/video/tplayer_wrapper.h"
#include "config/app_config.h"
#include "log/app_log.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

/*****************************************************************************
 *                                 Globals
 *****************************************************************************/

#define MAX_VIDEO_FILES   300
#define MAX_SCAN_DEPTH    8
#define MOUNT_HELPER      "/usr/bin/aitvbox-mount-sdcard"

static char video_files[MAX_VIDEO_FILES][512];
static char video_display[MAX_VIDEO_FILES][256];
static uint8_t video_unsupported[MAX_VIDEO_FILES];
static int video_w[MAX_VIDEO_FILES];
static int video_h[MAX_VIDEO_FILES];
static int video_fps[MAX_VIDEO_FILES];
static char video_reason[MAX_VIDEO_FILES][80];
static int video_count = 0;
static int current_index = -1;

static pthread_mutex_t video_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t scan_thread;
static bool scan_thread_running = false;
static bool scan_thread_joinable = false;
static const app_config_t *cfg = NULL;

/*****************************************************************************
 *                                 Helpers
 *****************************************************************************/

static bool is_video_ext(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext || !ext[1])
        return false;
    return strcasecmp(ext, ".mp4") == 0 ||
           strcasecmp(ext, ".mkv") == 0 ||
           strcasecmp(ext, ".avi") == 0 ||
           strcasecmp(ext, ".mov") == 0 ||
           strcasecmp(ext, ".ts") == 0 ||
           strcasecmp(ext, ".flv") == 0 ||
           strcasecmp(ext, ".webm") == 0;
}

static bool skip_dir_name(const char *name)
{
    if (!name || !name[0])
        return true;
    if (name[0] == '.')
        return true; /* ., .., .Trash-*, hidden */
    if (strcasecmp(name, "System Volume Information") == 0)
        return true;
    if (strcasecmp(name, "LOST.FOUND") == 0 || strcasecmp(name, "lost+found") == 0)
        return true;
    if (strcasecmp(name, "$RECYCLE.BIN") == 0)
        return true;
    if (strcasecmp(name, "RECYCLER") == 0)
        return true;
    return false;
}

static void ensure_sdcard_mounted(void)
{
    if (access(MOUNT_HELPER, X_OK) != 0)
        return;
    /* Idempotent; no-op when card missing. Ignore exit status. */
    int rc = system(MOUNT_HELPER " start");
    if (rc != 0)
        APP_LOGD("video-service", "mount helper rc=%d", rc);
}

static void add_video_file(const char *full_path, const char *display_path)
{
    int item_index = -1;
    int w = 0, h = 0, fps = 0;
    char reason[80];
    uint8_t unsup = 0;

    reason[0] = '\0';
    if (tplayer_wrapper_probe_media(full_path, &w, &h, &fps) == 0) {
        if (!tplayer_wrapper_is_supported(full_path, reason, sizeof(reason)))
            unsup = 1;
    }

    pthread_mutex_lock(&video_lock);
    if (video_count < MAX_VIDEO_FILES) {
        item_index = video_count;
        snprintf(video_files[item_index], sizeof(video_files[0]), "%s", full_path);
        snprintf(video_display[item_index], sizeof(video_display[0]), "%s", display_path);
        video_unsupported[item_index] = unsup;
        video_w[item_index] = w;
        video_h[item_index] = h;
        video_fps[item_index] = fps;
        snprintf(video_reason[item_index], sizeof(video_reason[0]), "%s", reason);
        video_count++;
        APP_LOGI("video-service", "found[%d]: %s meta %dx%d@%d unsup=%u %s",
                 item_index, display_path, w, h, fps, unsup, unsup ? reason : "ok");
    }
    pthread_mutex_unlock(&video_lock);
}

/* Playable first, unsupported last — red items no longer bury good ones at top. */
static void sort_playlist_playable_first(void)
{
    int i, j;

    pthread_mutex_lock(&video_lock);
    for (i = 0; i < video_count; i++) {
        for (j = i + 1; j < video_count; j++) {
            if (video_unsupported[i] && !video_unsupported[j]) {
                char path_tmp[512], disp_tmp[256], reason_tmp[80];
                uint8_t u;
                int tw, th, tf;

                memcpy(path_tmp, video_files[i], sizeof(path_tmp));
                memcpy(video_files[i], video_files[j], sizeof(video_files[0]));
                memcpy(video_files[j], path_tmp, sizeof(video_files[0]));

                memcpy(disp_tmp, video_display[i], sizeof(disp_tmp));
                memcpy(video_display[i], video_display[j], sizeof(video_display[0]));
                memcpy(video_display[j], disp_tmp, sizeof(video_display[0]));

                u = video_unsupported[i];
                video_unsupported[i] = video_unsupported[j];
                video_unsupported[j] = u;

                tw = video_w[i]; video_w[i] = video_w[j]; video_w[j] = tw;
                th = video_h[i]; video_h[i] = video_h[j]; video_h[j] = th;
                tf = video_fps[i]; video_fps[i] = video_fps[j]; video_fps[j] = tf;

                memcpy(reason_tmp, video_reason[i], sizeof(reason_tmp));
                memcpy(video_reason[i], video_reason[j], sizeof(video_reason[0]));
                memcpy(video_reason[j], reason_tmp, sizeof(video_reason[0]));
            }
        }
    }
    pthread_mutex_unlock(&video_lock);
}

static void publish_full_playlist(void)
{
    int i, n;
    video_playlist_item_t item;

    pthread_mutex_lock(&video_lock);
    n = video_count;
    pthread_mutex_unlock(&video_lock);

    for (i = 0; i < n; i++) {
        memset(&item, 0, sizeof(item));
        item.total_count = -1;
        item.index = i;
        pthread_mutex_lock(&video_lock);
        snprintf(item.path, sizeof(item.path), "%s", video_display[i]);
        item.width = video_w[i];
        item.height = video_h[i];
        item.fps = video_fps[i];
        item.unsupported = video_unsupported[i];
        snprintf(item.reason, sizeof(item.reason), "%s", video_reason[i]);
        pthread_mutex_unlock(&video_lock);
        mw_publish(TOPIC_VIDEO_PLAYLIST, &item, sizeof(item), MW_DIR_BACKEND_TO_UI);
        usleep(5000);
    }

    memset(&item, 0, sizeof(item));
    item.total_count = n;
    item.index = -1;
    mw_publish(TOPIC_VIDEO_PLAYLIST, &item, sizeof(item), MW_DIR_BACKEND_TO_UI);
    APP_LOGI("video-service", "playlist published count=%d (playable first)", n);
}

/* Recursive walk: full absolute path in dir_path; display is relative to video_dir. */
static void scan_dir_recursive(const char *dir_path, const char *display_prefix, int depth)
{
    DIR *dir;
    struct dirent *ent;
    bool full = false;

    if (depth > MAX_SCAN_DEPTH)
        return;

    pthread_mutex_lock(&video_lock);
    full = (video_count >= MAX_VIDEO_FILES);
    pthread_mutex_unlock(&video_lock);
    if (full)
        return;

    dir = opendir(dir_path);
    if (!dir) {
        if (depth == 0)
            APP_LOGW("video-service", "cannot open %s: %s", dir_path, strerror(errno));
        return;
    }

    while ((ent = readdir(dir)) != NULL) {
        char child_path[512];
        char child_display[256];
        struct stat st;
        int dtype;

        pthread_mutex_lock(&video_lock);
        full = (video_count >= MAX_VIDEO_FILES);
        pthread_mutex_unlock(&video_lock);
        if (full)
            break;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        if ((size_t)snprintf(child_path, sizeof(child_path), "%s/%s",
                             dir_path, ent->d_name) >= sizeof(child_path))
            continue;

        if (display_prefix && display_prefix[0]) {
            if ((size_t)snprintf(child_display, sizeof(child_display), "%s/%s",
                                 display_prefix, ent->d_name) >= sizeof(child_display))
                continue;
        } else {
            if ((size_t)snprintf(child_display, sizeof(child_display), "%s",
                                 ent->d_name) >= sizeof(child_display))
                continue;
        }

        dtype = ent->d_type;
        if (dtype == DT_UNKNOWN || dtype == DT_LNK) {
            if (stat(child_path, &st) != 0)
                continue;
            if (S_ISDIR(st.st_mode))
                dtype = DT_DIR;
            else if (S_ISREG(st.st_mode))
                dtype = DT_REG;
            else
                continue;
        }

        if (dtype == DT_DIR) {
            if (skip_dir_name(ent->d_name))
                continue;
            scan_dir_recursive(child_path, child_display, depth + 1);
            continue;
        }

        if (dtype != DT_REG)
            continue;
        if (!is_video_ext(ent->d_name))
            continue;

        add_video_file(child_path, child_display);
    }

    closedir(dir);
}

static void *scan_video_files_task(void *arg)
{
    const char *root;
    (void)arg;

    ensure_sdcard_mounted();

    root = (cfg && cfg->video_dir[0]) ? cfg->video_dir : "/mnt/SDCARD";
    APP_LOGI("video-service", "scanning (recursive) %s", root);

    pthread_mutex_lock(&video_lock);
    video_count = 0;
    current_index = -1;
    memset(video_unsupported, 0, sizeof(video_unsupported));
    pthread_mutex_unlock(&video_lock);

    scan_dir_recursive(root, "", 0);
    sort_playlist_playable_first();
    publish_full_playlist();
    APP_LOGI("video-service", "scan done root=%s", root);

    pthread_mutex_lock(&video_lock);
    scan_thread_running = false;
    pthread_mutex_unlock(&video_lock);
    return NULL;
}

static void start_scan(void)
{
    bool should_join = false;
    pthread_t old_thread;

    pthread_mutex_lock(&video_lock);
    if (scan_thread_joinable && !scan_thread_running) {
        old_thread = scan_thread;
        scan_thread_joinable = false;
        should_join = true;
    }
    pthread_mutex_unlock(&video_lock);

    if (should_join)
        pthread_join(old_thread, NULL);

    pthread_mutex_lock(&video_lock);
    if (scan_thread_running) {
        pthread_mutex_unlock(&video_lock);
        return;
    }
    scan_thread_running = true;
    pthread_mutex_unlock(&video_lock);

    if (pthread_create(&scan_thread, NULL, scan_video_files_task, NULL) == 0) {
        pthread_mutex_lock(&video_lock);
        scan_thread_joinable = true;
        pthread_mutex_unlock(&video_lock);
    } else {
        pthread_mutex_lock(&video_lock);
        scan_thread_running = false;
        pthread_mutex_unlock(&video_lock);
        APP_LOGE("video-service", "failed to create scan thread");
    }
}

static void fill_status_common(video_status_t *status)
{
    tplayer_status_t st = tplayer_wrapper_get_status();
    int index;

    memset(status, 0, sizeof(*status));
    status->is_playing = (st == TPLAYER_STATUS_PLAYING);
    status->is_paused = (st == TPLAYER_STATUS_PAUSED);
    if (status->is_playing || status->is_paused) {
        status->current_sec = tplayer_wrapper_get_position();
        status->total_sec = tplayer_wrapper_get_duration();
        if (status->total_sec < 0)
            status->total_sec = 0;
    }

    pthread_mutex_lock(&video_lock);
    index = current_index;
    status->current_index = index;
    if (index >= 0 && index < video_count) {
        const char *name = strrchr(video_files[index], '/');
        if (name)
            name++;
        else
            name = video_files[index];
        snprintf(status->current_file, sizeof(status->current_file), "%s", name);
        status->source_w = video_w[index];
        status->source_h = video_h[index];
    }
    pthread_mutex_unlock(&video_lock);

    /* Prefer live player MediaInfo for letterbox (probe can be wrong). */
    if (status->is_playing || status->is_paused) {
        int pw = 0, ph = 0;
        tplayer_wrapper_get_prepared_size(&pw, &ph);
        if (pw > 0 && ph > 0) {
            status->source_w = pw;
            status->source_h = ph;
            /* Keep list meta in sync for next open */
            pthread_mutex_lock(&video_lock);
            if (index >= 0 && index < video_count) {
                video_w[index] = pw;
                video_h[index] = ph;
            }
            pthread_mutex_unlock(&video_lock);
        }
    }
}

static void publish_play_status_now(const char *status_override)
{
    video_status_t status;
    const char *err;

    fill_status_common(&status);

    if (status_override && status_override[0]) {
        snprintf(status.status_text, sizeof(status.status_text), "%s", status_override);
    } else {
        err = tplayer_wrapper_get_last_error();
        if (err && err[0] && !status.is_playing && !status.is_paused)
            snprintf(status.status_text, sizeof(status.status_text), "%s", err);
    }

    mw_publish(TOPIC_VIDEO_STATUS, &status, sizeof(status), MW_DIR_BACKEND_TO_UI);
}

static void play_index(int index)
{
    char path[sizeof(video_files[0])];
    bool valid = false;
    int rc;

    pthread_mutex_lock(&video_lock);
    if (index >= 0 && index < video_count) {
        current_index = index;
        snprintf(path, sizeof(path), "%s", video_files[index]);
        valid = true;
    }
    pthread_mutex_unlock(&video_lock);

    if (!valid)
        return;

    APP_LOGI("video-service", "play index %d: %s", index, path);
    tplayer_wrapper_clear_error();
    rc = tplayer_wrapper_play_url(path);
    if (rc != 0) {
        const char *err = tplayer_wrapper_get_last_error();
        APP_LOGW("video-service", "play rejected/failed: %s",
                 err && err[0] ? err : "unknown");
        publish_play_status_now(err && err[0] ? err : "Unable to play");
    } else {
        /* Duration often becomes valid shortly after Start — publish twice. */
        publish_play_status_now(NULL);
        usleep(80000);
        publish_play_status_now(NULL);
    }
}

/*****************************************************************************
 *                                 Callbacks
 *****************************************************************************/

static void on_video_control(const mw_msg_t *msg)
{
    if (msg->data_len != sizeof(video_cmd_t)) {
        APP_LOGW("video-service", "cmd size mismatch expected %zu got %u",
                 sizeof(video_cmd_t), msg->data_len);
        return;
    }

    video_cmd_t *cmd = (video_cmd_t *)msg->data;
    APP_LOGD("video-service", "recv cmd action=%d", cmd->action);

    switch (cmd->action) {
    case VIDEO_CMD_PLAY:
        if (cmd->val >= 0) {
            play_index(cmd->val);
        } else if (strlen(cmd->path) > 0) {
            tplayer_wrapper_clear_error();
            if (tplayer_wrapper_play_url(cmd->path) != 0) {
                const char *err = tplayer_wrapper_get_last_error();
                publish_play_status_now(err && err[0] ? err : "Unable to play");
            } else {
                publish_play_status_now(NULL);
            }
        } else {
            tplayer_wrapper_resume();
        }
        break;

    case VIDEO_CMD_PAUSE:
        tplayer_wrapper_pause();
        publish_play_status_now(NULL);
        break;

    case VIDEO_CMD_RESUME:
        tplayer_wrapper_resume();
        publish_play_status_now(NULL);
        break;

    case VIDEO_CMD_STOP:
        tplayer_wrapper_stop();
        publish_play_status_now(NULL);
        break;

    case VIDEO_CMD_SEEK:
        tplayer_wrapper_seek(cmd->val);
        publish_play_status_now(NULL);
        break;

    case VIDEO_CMD_NEXT:
    case VIDEO_CMD_PREV: {
        /* Skip entries marked unsupported so red items don't block browsing. */
        int step = (cmd->action == VIDEO_CMD_NEXT) ? 1 : -1;
        int start, cand, n;

        pthread_mutex_lock(&video_lock);
        n = video_count;
        start = current_index;
        pthread_mutex_unlock(&video_lock);
        if (n <= 0)
            break;

        cand = start;
        for (int tries = 0; tries < n; tries++) {
            uint8_t bad;
            cand += step;
            if (cand >= n)
                cand = 0;
            if (cand < 0)
                cand = n - 1;
            pthread_mutex_lock(&video_lock);
            bad = video_unsupported[cand];
            pthread_mutex_unlock(&video_lock);
            if (!bad) {
                play_index(cand);
                break;
            }
            /* If only unsupported remain, land on next and let play reject. */
            if (tries == n - 1)
                play_index(cand);
        }
        break;
    }

    case VIDEO_CMD_SCAN_FILES:
        start_scan();
        break;

    case VIDEO_CMD_SET_RECT:
        tplayer_wrapper_set_display_rect(cmd->rect.x, cmd->rect.y,
                                         cmd->rect.w, cmd->rect.h);
        break;
    }
}

/*****************************************************************************
 *                                 Public API
 *****************************************************************************/

void service_video_init(void)
{
    cfg = app_config_get();
    APP_LOGI("video-service", "init video_dir=%s",
             cfg && cfg->video_dir[0] ? cfg->video_dir : "(null)");
    tplayer_wrapper_init();

    tplayer_wrapper_set_display_rect(0, 0, cfg->sim_window_width, cfg->sim_window_height);

    /* Best-effort early mount + background scan so playlist is warm. */
    ensure_sdcard_mounted();
    mw_subscribe(TOPIC_VIDEO_CONTROL, on_video_control);
    start_scan();
}

void service_video_deinit(void)
{
    bool should_join;
    pthread_t thread;

    APP_LOGI("video-service", "deinit");
    pthread_mutex_lock(&video_lock);
    should_join = scan_thread_joinable;
    thread = scan_thread;
    scan_thread_joinable = false;
    pthread_mutex_unlock(&video_lock);

    if (should_join)
        pthread_join(thread, NULL);

    tplayer_wrapper_stop();
    tplayer_wrapper_destroy();
}

void service_video_update(void)
{
    /* ~200ms status tick so progress bar moves smoothly (loop is 10ms). */
    static int counter = 0;
    if (++counter < 20)
        return;
    counter = 0;

    {
        tplayer_status_t st = tplayer_wrapper_get_status();

        if (st == TPLAYER_STATUS_PLAYING || st == TPLAYER_STATUS_PAUSED)
            publish_play_status_now(NULL);
    }
}
