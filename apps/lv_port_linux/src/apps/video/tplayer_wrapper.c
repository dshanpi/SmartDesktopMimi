#include "tplayer_wrapper.h"
#include <tplayer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>

/* Global TPlayer instance */
static TPlayer *g_tplayer = NULL;
static tplayer_status_t g_status = TPLAYER_STATUS_STOPPED;
static bool g_loop = false;
static sem_t g_prepared_sem;
static char g_last_error[128];
static int g_prepared_w = 0;
static int g_prepared_h = 0;

static int CallbackForTPlayer(void *pUserData, int msg, int param0, void *param1);

/*
 * Product decode budget: refuse to play above 4K@30 (full hard decode max).
 * Over-budget content previously hard-locked cedar (SBM full / drop frames).
 */
#define AITVBOX_MAX_DECODE_W   3840
#define AITVBOX_MAX_DECODE_H   2160
#define AITVBOX_MAX_DECODE_FPS 30

void tplayer_wrapper_clear_error(void)
{
    g_last_error[0] = '\0';
}

const char *tplayer_wrapper_get_last_error(void)
{
    return g_last_error;
}

static void set_error(const char *msg)
{
    if (!msg) {
        g_last_error[0] = '\0';
        return;
    }
    snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

static int stream_fps_approx(int n_frame_rate, int n_frame_duration)
{
    if (n_frame_rate > 1000)
        return (n_frame_rate + 500) / 1000;
    if (n_frame_rate > 0)
        return n_frame_rate;
    if (n_frame_duration >= 1000)
        return (int)((1000000 + n_frame_duration / 2) / n_frame_duration);
    return 0;
}

/*
 * True UHD/4K class — do NOT use height>=1800 alone (portrait 1080x1920
 * would be mis-tagged and painted red even though it is only FHD).
 */
static bool is_uhd_4k_class(int vid_w, int vid_h)
{
    long pixels;
    int max_side, min_side;

    if (vid_w <= 0 || vid_h <= 0)
        return false;
    pixels = (long)vid_w * (long)vid_h;
    max_side = (vid_w > vid_h) ? vid_w : vid_h;
    min_side = (vid_w < vid_h) ? vid_w : vid_h;
    /* Classic 4K: ~3840x2160 either orientation, or near-4K pixel count. */
    if (max_side >= 3200 && min_side >= 1800)
        return true;
    if (pixels >= (long)AITVBOX_MAX_DECODE_W * AITVBOX_MAX_DECODE_H * 9 / 10)
        return true;
    return false;
}

/* true = exceeds 4K@30 product limit.
 * fps==0 means unknown: only reject when resolution alone is over 4K. */
static bool exceeds_decode_budget(int vid_w, int vid_h, int fps)
{
    long pixels;
    int over_4k;

    if (vid_w <= 0 || vid_h <= 0)
        return false;

    pixels = (long)vid_w * (long)vid_h;
    over_4k = (vid_w > AITVBOX_MAX_DECODE_W || vid_h > AITVBOX_MAX_DECODE_H ||
               pixels > (long)AITVBOX_MAX_DECODE_W * AITVBOX_MAX_DECODE_H);
    if (over_4k)
        return true;

    /* 4K@>30 only when we really have UHD + known high fps. */
    if (is_uhd_4k_class(vid_w, vid_h) && fps > AITVBOX_MAX_DECODE_FPS)
        return true;

    return false;
}

static unsigned be32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static unsigned be16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}

/* Audio sample rates — do not use these mdhd timescales for video fps. */
static bool is_audio_timescale(unsigned ts)
{
    return ts == 8000 || ts == 11025 || ts == 12000 || ts == 16000 ||
           ts == 22050 || ts == 24000 || ts == 32000 || ts == 44100 ||
           ts == 48000 || ts == 96000;
}

/*
 * Probe MP4/MOV video track:
 *  - VisualSampleEntry width/height at type+28/+30 (ISO BMFF)
 *  - fps from video-track mdhd+stts only (skip audio 48kHz tracks)
 */
/* Scan one buffer blob for video dims + fps; update best_* in/out. */
static void probe_mp4_scan_buf(const unsigned char *buf, size_t n,
                               int *w, int *h, int *fps,
                               size_t *best_avc_pos, long *best_pixels,
                               unsigned long long *best_weight)
{
    size_t i;

    if (!buf || n < 64)
        return;

    for (i = 8; i + 32 < n; i++) {
        int is_v =
            (buf[i] == 'a' && buf[i + 1] == 'v' && buf[i + 2] == 'c' && buf[i + 3] == '1') ||
            (buf[i] == 'h' && buf[i + 1] == 'v' && buf[i + 2] == 'c' && buf[i + 3] == '1') ||
            (buf[i] == 'h' && buf[i + 1] == 'e' && buf[i + 2] == 'v' && buf[i + 3] == '1');
        unsigned box_size, tw, th;
        long pix;

        if (!is_v)
            continue;
        box_size = be32(&buf[i - 4]);
        if (box_size < 0x56 || box_size > 2 * 1024 * 1024)
            continue;

        tw = be16(&buf[i + 28]);
        th = be16(&buf[i + 30]);
        if (tw < 128 || tw > 7680 || th < 128 || th > 4320)
            continue;
        if ((tw & 1) || (th & 1))
            continue;
        pix = (long)tw * (long)th;
        if (pix >= *best_pixels) {
            *best_pixels = pix;
            *w = (int)tw;
            *h = (int)th;
            *best_avc_pos = i;
        }
    }

    if (*w <= 0 || *h <= 0) {
        for (i = 8; i + 8 < n; i++) {
            unsigned char ver;
            unsigned tw, th;
            size_t base;

            if (!(buf[i] == 't' && buf[i + 1] == 'k' && buf[i + 2] == 'h' &&
                  buf[i + 3] == 'd'))
                continue;
            ver = buf[i + 4];
            base = i - 4;
            if (ver == 0 && base + 84 <= n) {
                tw = be32(&buf[base + 76]) >> 16;
                th = be32(&buf[base + 80]) >> 16;
            } else if (ver == 1 && base + 96 <= n) {
                tw = be32(&buf[base + 88]) >> 16;
                th = be32(&buf[base + 92]) >> 16;
            } else {
                continue;
            }
            if (tw < 128 || tw > 7680 || th < 128 || th > 4320)
                continue;
            if ((long)tw * (long)th > *best_pixels) {
                *best_pixels = (long)tw * (long)th;
                *w = (int)tw;
                *h = (int)th;
                *best_avc_pos = i;
            }
        }
    }

    if (*w <= 0 || *h <= 0)
        return;

    for (i = 0; i + 20 < n; i++) {
        unsigned entry_count, e, sc, delta;
        unsigned ts = 0;
        size_t md, off;
        unsigned long long weight, sum_delta;
        int fps_i;

        if (!(buf[i] == 's' && buf[i + 1] == 't' && buf[i + 2] == 't' && buf[i + 3] == 's'))
            continue;

        entry_count = be32(&buf[i + 8]);
        if (entry_count == 0 || entry_count > 100000)
            continue;
        if (i + 12 + (size_t)entry_count * 8 > n)
            continue;

        md = i;
        while (md > 8) {
            md--;
            if (buf[md] == 'm' && buf[md + 1] == 'd' && buf[md + 2] == 'h' &&
                buf[md + 3] == 'd') {
                unsigned char ver = buf[md + 4];
                if (ver == 0 && md + 20 < n)
                    ts = be32(&buf[md + 16]);
                else if (ver == 1 && md + 28 < n)
                    ts = be32(&buf[md + 24]);
                break;
            }
            if (i - md > 4096)
                break;
        }
        if (ts == 0 || is_audio_timescale(ts))
            continue;

        weight = 0;
        sum_delta = 0;
        off = i + 12;
        for (e = 0; e < entry_count; e++) {
            sc = be32(&buf[off]);
            delta = be32(&buf[off + 4]);
            off += 8;
            if (delta == 0)
                continue;
            sum_delta += (unsigned long long)sc * (unsigned long long)delta;
            weight += sc;
        }
        if (weight == 0)
            continue;
        delta = (unsigned)((sum_delta + weight / 2) / weight);
        if (delta == 0)
            continue;
        fps_i = (int)((ts + delta / 2) / delta);
        if (fps_i < 1 || fps_i > 120)
            continue;
        if (weight >= *best_weight) {
            *best_weight = weight;
            *fps = fps_i;
        }
    }
}

static int probe_mp4_video(const char *path, int *out_w, int *out_h, int *out_fps)
{
    unsigned char *buf = NULL;
    const size_t max_rd = 2 * 1024 * 1024;
    int fd;
    ssize_t n;
    off_t fsize;
    int w = 0, h = 0, fps = 0;
    size_t best_avc_pos = 0;
    long best_pixels = 0;
    unsigned long long best_weight = 0;

    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (out_fps) *out_fps = 0;
    if (!path)
        return -1;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;

    buf = (unsigned char *)malloc(max_rd);
    if (!buf) {
        close(fd);
        return -1;
    }

    /* Head: many files keep moov near start */
    n = read(fd, buf, max_rd);
    if (n >= 64)
        probe_mp4_scan_buf(buf, (size_t)n, &w, &h, &fps,
                           &best_avc_pos, &best_pixels, &best_weight);

    /* Tail: progressive MP4 often has moov at end */
    fsize = lseek(fd, 0, SEEK_END);
    if (fsize > (off_t)max_rd) {
        if (lseek(fd, fsize - (off_t)max_rd, SEEK_SET) >= 0) {
            n = read(fd, buf, max_rd);
            if (n >= 64)
                probe_mp4_scan_buf(buf, (size_t)n, &w, &h, &fps,
                                   &best_avc_pos, &best_pixels, &best_weight);
        }
    }

    close(fd);
    free(buf);

    if (w <= 0 || h <= 0)
        return -1;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_fps) *out_fps = fps;
    printf("[TPlayerWrapper] probe_mp4 %s -> %dx%d fps~%d\n", path, w, h, fps);
    return 0;
}

static int recreate_tplayer(void)
{
    if (g_tplayer) {
        TPlayerStop(g_tplayer);
        TPlayerReset(g_tplayer);
        TPlayerDestroy(g_tplayer);
        g_tplayer = NULL;
    }
    g_tplayer = TPlayerCreate(CEDARX_PLAYER);
    if (!g_tplayer) {
        printf("[TPlayerWrapper] TPlayerCreate failed\n");
        set_error("Player initialization failed");
        return -1;
    }
    TPlayerSetNotifyCallback(g_tplayer, CallbackForTPlayer, NULL);
    /* Keep last decoded picture when paused (and until Stop clears it). */
    TPlayerSetHoldLastPicture(g_tplayer, 1);
    g_status = TPLAYER_STATUS_STOPPED;
    return 0;
}

static void format_unsupported_reason(char *buf, size_t buflen, int w, int h, int fps)
{
    if (!buf || buflen == 0)
        return;
    if (w > AITVBOX_MAX_DECODE_W || h > AITVBOX_MAX_DECODE_H)
        snprintf(buf, buflen, "Resolution too high (%dx%d); maximum is 4K@30", w, h);
    else if (fps > AITVBOX_MAX_DECODE_FPS)
        snprintf(buf, buflen, "Frame rate too high (%dx%d@%dfps); maximum is 4K@30", w, h, fps);
    else
        snprintf(buf, buflen, "Unsupported video; maximum is 4K@30");
}

static int reject_unsupported(int w, int h, int fps)
{
    char msg[128];
    printf("[TPlayerWrapper] REJECT %dx%d@~%dfps (max full-decode %dx%d@%d)\n",
           w, h, fps, AITVBOX_MAX_DECODE_W, AITVBOX_MAX_DECODE_H,
           AITVBOX_MAX_DECODE_FPS);
    format_unsupported_reason(msg, sizeof(msg), w, h, fps);
    set_error(msg);
    g_status = TPLAYER_STATUS_ERROR;
    return -1;
}

int tplayer_wrapper_probe_media(const char *path, int *out_w, int *out_h, int *out_fps)
{
    return probe_mp4_video(path, out_w, out_h, out_fps);
}

void tplayer_wrapper_get_prepared_size(int *out_w, int *out_h)
{
    if (out_w) *out_w = g_prepared_w;
    if (out_h) *out_h = g_prepared_h;
}

bool tplayer_wrapper_is_supported(const char *path, char *reason, size_t reason_len)
{
    int w = 0, h = 0, fps = 0;

    if (reason && reason_len)
        reason[0] = '\0';
    if (!path || !path[0]) {
        if (reason && reason_len)
            snprintf(reason, reason_len, "Invalid path");
        return false;
    }
    if (probe_mp4_video(path, &w, &h, &fps) != 0) {
        /* Unknown container/meta — allow try at play time. */
        return true;
    }
    if (exceeds_decode_budget(w, h, fps)) {
        if (reason && reason_len)
            format_unsupported_reason(reason, reason_len, w, h, fps);
        return false;
    }
    return true;
}

int tplayer_wrapper_init(void)
{
    if (g_tplayer != NULL) {
        printf("[TPlayerWrapper] Already initialized\n");
        return 0;
    }

    g_tplayer = TPlayerCreate(CEDARX_PLAYER);
    if (g_tplayer == NULL) {
        printf("[TPlayerWrapper] Failed to create TPlayer\n");
        return -1;
    }

    TPlayerSetNotifyCallback(g_tplayer, CallbackForTPlayer, NULL);
    TPlayerSetHoldLastPicture(g_tplayer, 1);
    sem_init(&g_prepared_sem, 0, 0);
    g_status = TPLAYER_STATUS_STOPPED;
    g_last_error[0] = '\0';

    printf("[TPlayerWrapper] Initialized (max full-decode 4K@30)\n");
    return 0;
}

void tplayer_wrapper_destroy(void)
{
    if (g_tplayer) {
        TPlayerDestroy(g_tplayer);
        g_tplayer = NULL;
    }
    sem_destroy(&g_prepared_sem);
    g_status = TPLAYER_STATUS_STOPPED;
    printf("[TPlayerWrapper] Destroyed\n");
}

int tplayer_wrapper_set_display_rect(int x, int y, int w, int h)
{
    if (!g_tplayer)
        return -1;
    TPlayerSetDisplayRect(g_tplayer, x, y, w, h);
    return 0;
}

int tplayer_wrapper_play_url(const char *url)
{
    int probed_w = 0, probed_h = 0, probed_fps = 0;
    int vid_w = 0, vid_h = 0, fps = 0;
    MediaInfo *mi;

    if (!g_tplayer || !url)
        return -1;

    set_error(NULL);
    printf("[TPlayerWrapper] Playing URL: %s\n", url);

    /* Gate before touching cedar when possible (avoid SBM thrash on 4K@60). */
    if (probe_mp4_video(url, &probed_w, &probed_h, &probed_fps) == 0) {
        printf("[TPlayerWrapper] probe %dx%d fps~%d\n", probed_w, probed_h, probed_fps);
        if (exceeds_decode_budget(probed_w, probed_h, probed_fps))
            return reject_unsupported(probed_w, probed_h, probed_fps);
    }

    if (recreate_tplayer() != 0)
        return -1;

    if (TPlayerSetDataSource(g_tplayer, url, NULL) != 0) {
        printf("[TPlayerWrapper] SetDataSource failed\n");
        set_error("Could not open the video file");
        g_status = TPLAYER_STATUS_ERROR;
        return -1;
    }

    g_status = TPLAYER_STATUS_PREPARING;
    if (TPlayerPrepare(g_tplayer) != 0) {
        printf("[TPlayerWrapper] Prepare failed\n");
        set_error("Could not prepare the video");
        g_status = TPLAYER_STATUS_ERROR;
        return -1;
    }

    mi = TPlayerGetMediaInfo(g_tplayer);
    if (mi && mi->pVideoStreamInfo) {
        vid_w = mi->pVideoStreamInfo->nWidth;
        vid_h = mi->pVideoStreamInfo->nHeight;
        fps = stream_fps_approx(mi->pVideoStreamInfo->nFrameRate,
                                mi->pVideoStreamInfo->nFrameDuration);
    } else {
        vid_w = probed_w;
        vid_h = probed_h;
        fps = probed_fps;
    }

    /* Second gate after demux MediaInfo (probe may miss fps). */
    if (exceeds_decode_budget(vid_w, vid_h, fps)) {
        TPlayerStop(g_tplayer);
        TPlayerReset(g_tplayer);
        return reject_unsupported(vid_w, vid_h, fps);
    }

    /* Prefer demux MediaInfo for UI (probe can still miss odd containers). */
    g_prepared_w = vid_w;
    g_prepared_h = vid_h;
    printf("[TPlayerWrapper] Prepared %dx%d fps~%d (within 4K@30)\n",
           vid_w, vid_h, fps);

    /* Letterbox into 1024x768 using *player* dims, not raw probe. */
    {
        int screen_w = 1024;
        int screen_h = 768;
        int disp_x = 0, disp_y = 0, disp_w = screen_w, disp_h = screen_h;

        if (vid_w > 0 && vid_h > 0) {
            float ratio_w = (float)screen_w / (float)vid_w;
            float ratio_h = (float)screen_h / (float)vid_h;
            float sc = (ratio_w < ratio_h) ? ratio_w : ratio_h;
            disp_w = (int)(vid_w * sc);
            disp_h = (int)(vid_h * sc);
            disp_x = (screen_w - disp_w) / 2;
            disp_y = (screen_h - disp_h) / 2;
            printf("[TPlayerWrapper] Display: %d,%d %dx%d (src %dx%d)\n",
                   disp_x, disp_y, disp_w, disp_h, vid_w, vid_h);
        }
        TPlayerSetDisplayRect(g_tplayer, disp_x, disp_y, disp_w, disp_h);
    }

    if (TPlayerStart(g_tplayer) == 0) {
        g_status = TPLAYER_STATUS_PLAYING;
        printf("[TPlayerWrapper] Started playback\n");
        return 0;
    }

    g_status = TPLAYER_STATUS_ERROR;
    set_error("Could not start playback");
    printf("[TPlayerWrapper] Start failed\n");
    return -1;
}

int tplayer_wrapper_pause(void)
{
    if (!g_tplayer)
        return -1;
    if (g_status == TPLAYER_STATUS_PLAYING) {
        if (TPlayerPause(g_tplayer) == 0) {
            g_status = TPLAYER_STATUS_PAUSED;
            return 0;
        }
    }
    return -1;
}

int tplayer_wrapper_resume(void)
{
    if (!g_tplayer)
        return -1;
    if (g_status == TPLAYER_STATUS_PAUSED) {
        if (TPlayerStart(g_tplayer) == 0) {
            g_status = TPLAYER_STATUS_PLAYING;
            return 0;
        }
    }
    return -1;
}

int tplayer_wrapper_stop(void)
{
    if (!g_tplayer)
        return -1;

    if (g_status == TPLAYER_STATUS_STOPPED)
        return 0;

    /* Drop held frame so plane does not stick after leaving the app. */
    TPlayerSetHoldLastPicture(g_tplayer, 0);
    if (TPlayerStop(g_tplayer) == 0) {
        g_status = TPLAYER_STATUS_STOPPED;
        TPlayerSetHoldLastPicture(g_tplayer, 1); /* restore default for next play */
        return 0;
    }
    TPlayerSetHoldLastPicture(g_tplayer, 1);
    return -1;
}

int tplayer_wrapper_seek(int seconds)
{
    if (!g_tplayer)
        return -1;
    return TPlayerSeekTo(g_tplayer, seconds * 1000);
}

int tplayer_wrapper_get_duration(void)
{
    if (!g_tplayer)
        return 0;
    {
        int msec = 0;
        if (TPlayerGetDuration(g_tplayer, &msec) == 0)
            return msec / 1000;
    }
    return 0;
}

int tplayer_wrapper_get_position(void)
{
    if (!g_tplayer)
        return 0;
    {
        int msec = 0;
        if (TPlayerGetCurrentPosition(g_tplayer, &msec) == 0)
            return msec / 1000;
    }
    return 0;
}

tplayer_status_t tplayer_wrapper_get_status(void)
{
    return g_status;
}

void tplayer_wrapper_set_loop(bool loop)
{
    g_loop = loop;
    if (g_tplayer)
        TPlayerSetLooping(g_tplayer, loop);
}

bool tplayer_wrapper_is_playing(void)
{
    return g_status == TPLAYER_STATUS_PLAYING;
}

static int CallbackForTPlayer(void *pUserData, int msg, int param0, void *param1)
{
    (void)pUserData;
    (void)param1;

    switch (msg) {
    case TPLAYER_NOTIFY_PREPARED:
        printf("[TPlayerWrapper] Prepared (Callback)\n");
        sem_post(&g_prepared_sem);
        break;

    case TPLAYER_NOTIFY_PLAYBACK_COMPLETE:
        printf("[TPlayerWrapper] Playback Complete\n");
        g_status = TPLAYER_STATUS_STOPPED;
        break;

    case TPLAYER_NOTIFY_SEEK_COMPLETE:
        printf("[TPlayerWrapper] Seek Complete\n");
        break;

    case TPLAYER_NOTIFY_MEDIA_ERROR:
        printf("[TPlayerWrapper] Media Error: %d\n", param0);
        set_error("Playback error");
        g_status = TPLAYER_STATUS_ERROR;
        break;

    case TPLAYER_NOTIFY_NOT_SEEKABLE:
        printf("[TPlayerWrapper] Media not seekable\n");
        break;

    default:
        break;
    }
    return 0;
}
