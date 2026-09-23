#include "bt_pcm_player.h"

#include "log/app_log.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BT_PCM_DEVICE_DEFAULT "PlaybackDmix"
#define BT_PCM_RING_BYTES (48000U * 2U * sizeof(int16_t) / 2U)
#define BT_PCM_CHUNK_BYTES (2048U * 2U * sizeof(int16_t))
#define BT_PCM_PREFILL_MS 40U
#define BT_PCM_LATENCY_US 100000U
#define BT_PCM_REPORT_MS 5000
#define BT_PCM_OPEN_RETRY_MS 500U

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    pthread_t thread;
    bool thread_started;
    bool stopping;
    bool active;
    uint8_t *ring;
    size_t read_pos;
    size_t write_pos;
    size_t queued;
    unsigned int channels;
    unsigned int sampling;
    uint64_t format_generation;
    uint64_t received_bytes;
    uint64_t written_frames;
    uint64_t dropped_bytes;
    unsigned int xruns;
} bt_pcm_player_t;

static bt_pcm_player_t g_player = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .ready = PTHREAD_COND_INITIALIZER,
};

static int64_t monotonic_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *playback_device(void)
{
    const char *name = getenv("BT_AUDIO_PLAYBACK_PCM");
    return name && name[0] ? name : BT_PCM_DEVICE_DEFAULT;
}

static size_t frame_bytes(unsigned int channels)
{
    return (size_t)channels * sizeof(int16_t);
}

static size_t prefill_bytes(unsigned int sampling, unsigned int channels)
{
    size_t bytes = (size_t)sampling * frame_bytes(channels) *
                   BT_PCM_PREFILL_MS / 1000U;
    size_t unit = frame_bytes(channels);

    if (bytes < unit) bytes = unit;
    if (bytes > BT_PCM_RING_BYTES) bytes = BT_PCM_RING_BYTES;
    return bytes - bytes % unit;
}

static void reset_ring_locked(bool count_as_dropped)
{
    if (count_as_dropped) g_player.dropped_bytes += g_player.queued;
    g_player.read_pos = 0;
    g_player.write_pos = 0;
    g_player.queued = 0;
}

static void ring_push_locked(const uint8_t *data, size_t len, size_t unit)
{
    size_t available;
    size_t first;

    if (len > BT_PCM_RING_BYTES) {
        size_t skip = len - BT_PCM_RING_BYTES;
        skip += (unit - skip % unit) % unit;
        if (skip > len) skip = len;
        data += skip;
        len -= skip;
        g_player.dropped_bytes += skip;
    }

    available = BT_PCM_RING_BYTES - g_player.queued;
    if (len > available) {
        size_t drop = len - available;
        drop += (unit - drop % unit) % unit;
        if (drop > g_player.queued) drop = g_player.queued;
        g_player.read_pos = (g_player.read_pos + drop) % BT_PCM_RING_BYTES;
        g_player.queued -= drop;
        g_player.dropped_bytes += drop;
    }

    first = len;
    if (first > BT_PCM_RING_BYTES - g_player.write_pos) {
        first = BT_PCM_RING_BYTES - g_player.write_pos;
    }
    memcpy(g_player.ring + g_player.write_pos, data, first);
    if (len > first) memcpy(g_player.ring, data + first, len - first);
    g_player.write_pos = (g_player.write_pos + len) % BT_PCM_RING_BYTES;
    g_player.queued += len;
}

static size_t ring_pop_locked(uint8_t *output, size_t maximum, size_t unit)
{
    size_t len = g_player.queued;
    size_t first;

    if (len > maximum) len = maximum;
    len -= len % unit;
    if (len == 0) return 0;

    first = len;
    if (first > BT_PCM_RING_BYTES - g_player.read_pos) {
        first = BT_PCM_RING_BYTES - g_player.read_pos;
    }
    memcpy(output, g_player.ring + g_player.read_pos, first);
    if (len > first) memcpy(output + first, g_player.ring, len - first);
    g_player.read_pos = (g_player.read_pos + len) % BT_PCM_RING_BYTES;
    g_player.queued -= len;
    return len;
}

static void wait_for_retry_locked(unsigned int delay_ms)
{
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += delay_ms / 1000U;
    deadline.tv_nsec += (long)(delay_ms % 1000U) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&g_player.ready, &g_player.lock, &deadline);
}

static snd_pcm_t *open_playback_pcm(const char *name, unsigned int sampling,
                                    unsigned int channels)
{
    snd_pcm_t *pcm = NULL;
    int rc;

    rc = snd_pcm_open(&pcm, name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (rc < 0) {
        APP_LOGW("bt_pcm", "open %s failed: %s", name, snd_strerror(rc));
        return NULL;
    }

    rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                            SND_PCM_ACCESS_RW_INTERLEAVED, channels, sampling,
                            1, BT_PCM_LATENCY_US);
    if (rc < 0) {
        APP_LOGW("bt_pcm", "configure %s %u Hz/%u ch failed: %s",
                 name, sampling, channels, snd_strerror(rc));
        snd_pcm_close(pcm);
        return NULL;
    }

    rc = snd_pcm_prepare(pcm);
    if (rc < 0) {
        APP_LOGW("bt_pcm", "prepare %s failed: %s", name, snd_strerror(rc));
        snd_pcm_close(pcm);
        return NULL;
    }
    return pcm;
}

static bool player_format_current(uint64_t generation)
{
    bool current;

    pthread_mutex_lock(&g_player.lock);
    current = !g_player.stopping && g_player.active &&
              g_player.format_generation == generation;
    pthread_mutex_unlock(&g_player.lock);
    return current;
}

static void close_playback_pcm(snd_pcm_t **pcm)
{
    if (!*pcm) return;
    snd_pcm_drop(*pcm);
    snd_pcm_close(*pcm);
    *pcm = NULL;
}

static void report_stats_if_due(int64_t *last_report_ms)
{
    int64_t now = monotonic_ms();
    uint64_t received;
    uint64_t written;
    uint64_t dropped;
    unsigned int xruns;
    size_t queued;

    if (now - *last_report_ms < BT_PCM_REPORT_MS) return;
    pthread_mutex_lock(&g_player.lock);
    received = g_player.received_bytes;
    written = g_player.written_frames;
    dropped = g_player.dropped_bytes;
    xruns = g_player.xruns;
    queued = g_player.queued;
    pthread_mutex_unlock(&g_player.lock);
    APP_LOGI("bt_pcm",
             "stable: received=%llu bytes written=%llu frames queued=%zu bytes dropped=%llu bytes xruns=%u",
             (unsigned long long)received, (unsigned long long)written,
             queued, (unsigned long long)dropped, xruns);
    *last_report_ms = now;
}

static void *playback_worker(void *unused)
{
    uint8_t chunk[BT_PCM_CHUNK_BYTES];
    snd_pcm_t *pcm = NULL;
    unsigned int pcm_channels = 0;
    unsigned int pcm_sampling = 0;
    uint64_t pcm_generation = 0;
    int64_t last_report_ms = monotonic_ms();
    const char *device = playback_device();

    (void)unused;
    for (;;) {
        unsigned int channels;
        unsigned int sampling;
        uint64_t generation;
        bool active;
        bool stopping;
        size_t queued;
        size_t unit;
        size_t wanted_prefill;
        bool should_wait;

        pthread_mutex_lock(&g_player.lock);
        stopping = g_player.stopping;
        active = g_player.active;
        channels = g_player.channels;
        sampling = g_player.sampling;
        generation = g_player.format_generation;
        queued = g_player.queued;
        unit = frame_bytes(channels);
        wanted_prefill = (channels && sampling) ?
            prefill_bytes(sampling, channels) : 0;
        should_wait = !stopping &&
            ((!pcm && (!active || !wanted_prefill || queued < wanted_prefill)) ||
             (pcm && active && generation == pcm_generation && queued == 0));
        if (should_wait) {
            pthread_cond_wait(&g_player.ready, &g_player.lock);
            pthread_mutex_unlock(&g_player.lock);
            continue;
        }
        pthread_mutex_unlock(&g_player.lock);

        if (stopping) break;

        if (pcm && (!active || generation != pcm_generation ||
                    channels != pcm_channels || sampling != pcm_sampling)) {
            close_playback_pcm(&pcm);
            pcm_channels = 0;
            pcm_sampling = 0;
            pcm_generation = 0;
            continue;
        }
        if (!active || channels == 0 || channels > 2 || sampling == 0) continue;

        if (!pcm) {
            pcm = open_playback_pcm(device, sampling, channels);
            if (!pcm) {
                pthread_mutex_lock(&g_player.lock);
                if (!g_player.stopping) wait_for_retry_locked(BT_PCM_OPEN_RETRY_MS);
                pthread_mutex_unlock(&g_player.lock);
                continue;
            }
            pcm_channels = channels;
            pcm_sampling = sampling;
            pcm_generation = generation;
            last_report_ms = monotonic_ms();
            APP_LOGI("bt_pcm", "playback started: %s S16_LE %u Hz %u ch, prefill=%zu bytes",
                     device, sampling, channels, wanted_prefill);
            continue;
        }

        pthread_mutex_lock(&g_player.lock);
        if (!g_player.active || g_player.format_generation != pcm_generation) {
            pthread_mutex_unlock(&g_player.lock);
            continue;
        }
        size_t bytes = ring_pop_locked(chunk, sizeof(chunk), unit);
        pthread_mutex_unlock(&g_player.lock);
        if (bytes == 0) continue;

        snd_pcm_sframes_t total_frames = (snd_pcm_sframes_t)(bytes / unit);
        snd_pcm_sframes_t written = 0;
        bool fatal = false;
        while (written < total_frames && player_format_current(pcm_generation)) {
            snd_pcm_sframes_t rc = snd_pcm_writei(
                pcm, chunk + (size_t)written * unit, total_frames - written);
            if (rc == -EAGAIN) {
                struct timespec pause = { .tv_sec = 0, .tv_nsec = 1000000L };
                nanosleep(&pause, NULL);
                continue;
            }
            if (rc == -EPIPE || rc == -ESTRPIPE) {
                pthread_mutex_lock(&g_player.lock);
                g_player.xruns++;
                pthread_mutex_unlock(&g_player.lock);
                if (snd_pcm_recover(pcm, (int)rc, 1) < 0) {
                    fatal = true;
                    break;
                }
                continue;
            }
            if (rc < 0) {
                APP_LOGW("bt_pcm", "write failed: %s", snd_strerror((int)rc));
                fatal = true;
                break;
            }
            if (rc == 0) continue;
            written += rc;
            pthread_mutex_lock(&g_player.lock);
            g_player.written_frames += (uint64_t)rc;
            pthread_mutex_unlock(&g_player.lock);
        }
        if (written < total_frames) {
            pthread_mutex_lock(&g_player.lock);
            g_player.dropped_bytes +=
                (uint64_t)(total_frames - written) * unit;
            pthread_mutex_unlock(&g_player.lock);
        }
        if (fatal) close_playback_pcm(&pcm);
        report_stats_if_due(&last_report_ms);
    }

    close_playback_pcm(&pcm);
    APP_LOGI("bt_pcm", "playback worker stopped");
    return NULL;
}

bool bt_pcm_player_start(void)
{
    int rc;

    pthread_mutex_lock(&g_player.lock);
    if (g_player.thread_started) {
        pthread_mutex_unlock(&g_player.lock);
        return true;
    }
    g_player.ring = malloc(BT_PCM_RING_BYTES);
    if (!g_player.ring) {
        pthread_mutex_unlock(&g_player.lock);
        APP_LOGE("bt_pcm", "cannot allocate %u-byte ring", BT_PCM_RING_BYTES);
        return false;
    }
    g_player.stopping = false;
    g_player.active = false;
    g_player.channels = 0;
    g_player.sampling = 0;
    g_player.format_generation++;
    g_player.received_bytes = 0;
    g_player.written_frames = 0;
    g_player.dropped_bytes = 0;
    g_player.xruns = 0;
    reset_ring_locked(false);
    rc = pthread_create(&g_player.thread, NULL, playback_worker, NULL);
    if (rc != 0) {
        free(g_player.ring);
        g_player.ring = NULL;
        pthread_mutex_unlock(&g_player.lock);
        APP_LOGE("bt_pcm", "cannot start playback worker: %s", strerror(rc));
        return false;
    }
    g_player.thread_started = true;
    pthread_mutex_unlock(&g_player.lock);
    return true;
}

void bt_pcm_player_stop(void)
{
    pthread_t thread;

    pthread_mutex_lock(&g_player.lock);
    if (!g_player.thread_started) {
        pthread_mutex_unlock(&g_player.lock);
        return;
    }
    g_player.stopping = true;
    g_player.active = false;
    reset_ring_locked(true);
    thread = g_player.thread;
    pthread_cond_broadcast(&g_player.ready);
    pthread_mutex_unlock(&g_player.lock);

    pthread_join(thread, NULL);

    pthread_mutex_lock(&g_player.lock);
    free(g_player.ring);
    g_player.ring = NULL;
    g_player.thread_started = false;
    g_player.stopping = false;
    g_player.channels = 0;
    g_player.sampling = 0;
    reset_ring_locked(false);
    pthread_mutex_unlock(&g_player.lock);
}

void bt_pcm_player_set_active(bool active)
{
    pthread_mutex_lock(&g_player.lock);
    if (!g_player.thread_started || g_player.stopping) {
        pthread_mutex_unlock(&g_player.lock);
        return;
    }
    if (g_player.active != active) {
        g_player.active = active;
        if (!active) {
            reset_ring_locked(true);
            g_player.channels = 0;
            g_player.sampling = 0;
            g_player.format_generation++;
        }
        pthread_cond_broadcast(&g_player.ready);
    }
    pthread_mutex_unlock(&g_player.lock);
}

void bt_pcm_player_push(uint16_t channels, uint16_t sampling,
                        const uint8_t *data, uint32_t len)
{
    size_t unit;
    size_t bytes;

    if (!data || len == 0 || channels == 0 || channels > 2 || sampling == 0) {
        return;
    }
    unit = frame_bytes(channels);
    bytes = len - len % unit;
    if (bytes == 0) return;

    pthread_mutex_lock(&g_player.lock);
    if (!g_player.thread_started || g_player.stopping || !g_player.active) {
        pthread_mutex_unlock(&g_player.lock);
        return;
    }
    if (g_player.channels != channels || g_player.sampling != sampling) {
        reset_ring_locked(true);
        g_player.channels = channels;
        g_player.sampling = sampling;
        g_player.format_generation++;
    }
    g_player.received_bytes += bytes;
    ring_push_locked(data, bytes, unit);
    pthread_cond_signal(&g_player.ready);
    pthread_mutex_unlock(&g_player.lock);
}
