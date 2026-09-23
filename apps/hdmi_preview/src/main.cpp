/*
 * HDMI input preview
 * V4L2 capture + Allwinner videoOutPort display
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <stdint.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <alsa/asoundlib.h>
#include <samplerate.h>
#include <sched.h>
#include <math.h>

// #include "AWVideoEncoder.h" // Removed
// #include "sunxiMemInterface.h" // Removed

#define VIDEO_VERSION     "1.0.1"
#define VIDEO_DEV         "/dev/video0"
#define WIDTH            1920
#define HEIGHT           1080
#define FPS              60
#define BITRATE          20000
#define BUFFER_COUNT     3
#define MAX_PLANES       3
#define HDMI_TIMEOUT_LIMIT 3
#define HDMI_SERVICE_TIMEOUT_LIMIT 2
#define HDMI_CAPTURE_SELECT_TIMEOUT_SEC 2
#define HDMI_SERVICE_SELECT_TIMEOUT_SEC 1
#define HDMI_INVALID_LIMIT 2
#define HDMI_PROBE_TIMEOUT_SEC 4
#define HDMI_SIGNAL_UNSUPPORTED (-2)
#define HDMI_STATUS_SOCKET "/tmp/hdmi_preview_status.sock"
#define HDMI_SERVICE_RETRY_SEC 3
#define CAPTURE_SOCKET "/var/run/aitvbox/capture.sock"
#define CAPTURE_SOCKET_LOCK "/var/run/aitvbox/capture-socket.lock"
#define SNAPSHOT_PATH "/var/run/aitvbox/screen.jpg"
#define SNAPSHOT_WIDTH 960
#define SNAPSHOT_HEIGHT 540
#define SNAPSHOT_QUALITY 80
#define INVALID_BOTTOM_ROWS 8
#define HDMI_AUDIO_CAPTURE_DEFAULT "HDMIIn"
#define HDMI_AUDIO_PLAYBACK_DEFAULT "PlaybackDmix"
#define HDMI_AUDIO_CAPTURE_RATE 44100
#define HDMI_AUDIO_PLAYBACK_RATE 44100
#define HDMI_AUDIO_CHANNELS 2
#define HDMI_AUDIO_CHUNK_FRAMES 1024
#define HDMI_AUDIO_OUTPUT_FRAMES 2048
#define HDMI_AUDIO_LATENCY_US 200000
#define HDMI_AUDIO_CAPTURE_RING_FRAMES 32768
#define HDMI_AUDIO_CAPTURE_PREFILL_FRAMES 4096
#define HDMI_AUDIO_PREFILL_TIMEOUT_MS 2000
#define HDMI_AUDIO_CAPTURE_PRIORITY 21
#define HDMI_AUDIO_PLAYBACK_PRIORITY 20
#define HDMI_AUDIO_TARGET_DELAY_SECONDS 0.050

// Unix Socket for video data
// #define CTRL_SOCKET      "/var/run/kvm_ctrl.sock" // Removed, no longer needed

#define LOG_FILE         "/tmp/hdmi_preview.log"
#define LOG_FILE_MAX_BYTES (512 * 1024)

static void rotate_log_if_needed(void)
{
    struct stat st;

    if (stat(LOG_FILE, &st) == 0 && st.st_size > LOG_FILE_MAX_BYTES) {
        FILE* f = fopen(LOG_FILE, "w");
        if (f) {
            fclose(f);
        }
    }
}

// Helper for writing logs to file
static void log_to_file(const char* format, ...)
{
    rotate_log_if_needed();

    FILE* f = fopen(LOG_FILE, "a");
    if (!f) return;
    
    // Add timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);
    char time_buf[26];
    strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(f, "[%s.%03d] ", time_buf, (int)(tv.tv_usec / 1000));
    
    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    
    fclose(f);
}

// Custom printf that writes to both stdout and file
#define kvm_log(fmt, args...) do { \
    fprintf(stdout, fmt, ##args); \
    log_to_file(fmt, ##args); \
} while(0)

// #define ALIGN_16B(x) (((x) + (15)) & ~(15)) // Removed

// using namespace awvideoencoder; // Removed

// Global variables
static int g_video_fd = -1;
static bool g_running = false;
static bool g_capture_started = false;
static bool g_service_mode = false;
static bool g_display_mode = false;
static pthread_t g_cap_thread = 0;
static pthread_t g_snapshot_thread = 0;
static pthread_t g_audio_thread = 0;
static volatile sig_atomic_t g_audio_running = 0;
static pthread_mutex_t g_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_snapshot_cond = PTHREAD_COND_INITIALIZER;
static int g_snapshot_state = 0;
static int g_snapshot_result = -1;
static long g_snapshot_size = 0;
// static AWVideoEncoder* g_encoder = NULL; // Removed

// Ion memory for encoder input
// static dma_mem_des_t gIonMem; // Removed
// static bool g_ion_inited = false; // Removed

// Socket fds
// static int g_ctrl_fd = -1; // Removed, no longer needed

// V4L2 buffer structure
typedef struct {
    void* start[MAX_PLANES];
    int length[MAX_PLANES];
    void* phy_addr[MAX_PLANES];
    unsigned int fd[MAX_PLANES];
} buffer_t;

static buffer_t* g_buffers = NULL;
static int g_buffer_count = 0;

// Socket file descriptor for video output
// Socket file descriptor for video output (REMOVED)
// static int g_video_socket_fd = -1;

// Timestamp for frames
// static uint64_t g_encoder_start_time = 0; // Removed

#include <linux/fb.h>

#include <ion_mem_alloc.h>
extern "C" {
#include <videoOutPort.h>
}

// Framebuffer variables (REMOVED, now using videoOutPort)
// static int g_fb_fd = -1;
// static char *g_fb_ptr = NULL;
// static long int g_fb_size = 0;
// static struct fb_var_screeninfo g_vinfo;
// static struct fb_fix_screeninfo g_finfo;

// VideoOutPort related global variables
static videoParam g_vop_vparam;
static renderBuf g_vop_rBuf;
static dispOutPort *g_disp_out_port = NULL;
static struct SunxiMemOpsS *g_sunxi_mem_ops = NULL;
static char *g_vop_vir_buf = NULL;
static char *g_vop_phy_buf = NULL;

static int open_video_device(void);
static uint64_t get_timestamp_ms(void);

static const char* env_or_default(const char* name, const char* fallback)
{
    const char* value = getenv(name);
    return (value && value[0]) ? value : fallback;
}

static int configure_audio_pcm(snd_pcm_t* pcm, unsigned int rate)
{
    return snd_pcm_set_params(pcm,
                              SND_PCM_FORMAT_S16_LE,
                              SND_PCM_ACCESS_RW_INTERLEAVED,
                              HDMI_AUDIO_CHANNELS,
                              rate,
                              1,
                              HDMI_AUDIO_LATENCY_US);
}

typedef struct {
    snd_pcm_t* pcm;
    short* samples;
    size_t capacity_frames;
    size_t read_frame;
    size_t write_frame;
    size_t queued_frames;
    unsigned long long captured_frames;
    unsigned long long dropped_frames;
    unsigned long long measured_samples;
    unsigned long long sample_square_sum;
    unsigned int peak_sample;
    unsigned int xruns;
    int fatal_error;
    volatile sig_atomic_t running;
    pthread_mutex_t lock;
    pthread_cond_t ready;
} audio_capture_ring_t;

static size_t audio_capture_ring_level(audio_capture_ring_t* ring)
{
    size_t level;
    pthread_mutex_lock(&ring->lock);
    level = ring->queued_frames;
    pthread_mutex_unlock(&ring->lock);
    return level;
}

static size_t audio_capture_ring_pop(audio_capture_ring_t* ring,
                                     short* output,
                                     size_t maximum_frames)
{
    pthread_mutex_lock(&ring->lock);
    size_t frames = ring->queued_frames;
    if (frames > maximum_frames) frames = maximum_frames;
    size_t first = frames;
    if (first > ring->capacity_frames - ring->read_frame) {
        first = ring->capacity_frames - ring->read_frame;
    }
    memcpy(output,
           ring->samples + ring->read_frame * HDMI_AUDIO_CHANNELS,
           first * HDMI_AUDIO_CHANNELS * sizeof(short));
    if (frames > first) {
        memcpy(output + first * HDMI_AUDIO_CHANNELS,
               ring->samples,
               (frames - first) * HDMI_AUDIO_CHANNELS * sizeof(short));
    }
    ring->read_frame = (ring->read_frame + frames) % ring->capacity_frames;
    ring->queued_frames -= frames;
    pthread_mutex_unlock(&ring->lock);
    return frames;
}

static void audio_capture_ring_push(audio_capture_ring_t* ring,
                                    const short* input,
                                    size_t frames)
{
    unsigned long long square_sum = 0;
    unsigned int peak = 0;
    size_t sample_count = frames * HDMI_AUDIO_CHANNELS;
    for (size_t i = 0; i < sample_count; ++i) {
        int sample = input[i];
        unsigned int magnitude = (unsigned int)(sample < 0 ? -sample : sample);
        if (magnitude > 32768U) magnitude = 32768U;
        if (magnitude > peak) peak = magnitude;
        square_sum += (unsigned long long)magnitude * magnitude;
    }

    pthread_mutex_lock(&ring->lock);
    size_t available = ring->capacity_frames - ring->queued_frames;
    if (frames > available) {
        size_t drop = frames - available;
        ring->read_frame = (ring->read_frame + drop) % ring->capacity_frames;
        ring->queued_frames -= drop;
        ring->dropped_frames += drop;
    }
    size_t first = frames;
    if (first > ring->capacity_frames - ring->write_frame) {
        first = ring->capacity_frames - ring->write_frame;
    }
    memcpy(ring->samples + ring->write_frame * HDMI_AUDIO_CHANNELS,
           input,
           first * HDMI_AUDIO_CHANNELS * sizeof(short));
    if (frames > first) {
        memcpy(ring->samples,
               input + first * HDMI_AUDIO_CHANNELS,
               (frames - first) * HDMI_AUDIO_CHANNELS * sizeof(short));
    }
    ring->write_frame = (ring->write_frame + frames) % ring->capacity_frames;
    ring->queued_frames += frames;
    ring->captured_frames += frames;
    ring->measured_samples += sample_count;
    ring->sample_square_sum += square_sum;
    if (peak > ring->peak_sample) ring->peak_sample = peak;
    pthread_cond_signal(&ring->ready);
    pthread_mutex_unlock(&ring->lock);
}

static void* audio_capture_worker(void* opaque)
{
    audio_capture_ring_t* ring = (audio_capture_ring_t*)opaque;
    short input[HDMI_AUDIO_CHUNK_FRAMES * HDMI_AUDIO_CHANNELS];
    struct sched_param sched = {};
    sched.sched_priority = HDMI_AUDIO_CAPTURE_PRIORITY;
    pthread_setschedparam(pthread_self(), SCHED_RR, &sched);

    while (ring->running && g_audio_running && g_running && g_capture_started) {
        snd_pcm_sframes_t frames = snd_pcm_readi(ring->pcm, input,
                                                 HDMI_AUDIO_CHUNK_FRAMES);
        if (frames == -EAGAIN) {
            usleep(500);
            continue;
        }
        if (frames == -EPIPE || frames == -ESTRPIPE) {
            pthread_mutex_lock(&ring->lock);
            ring->xruns++;
            pthread_mutex_unlock(&ring->lock);
            snd_pcm_recover(ring->pcm, (int)frames, 1);
            snd_pcm_start(ring->pcm);
            continue;
        }
        if (frames < 0) {
            pthread_mutex_lock(&ring->lock);
            ring->fatal_error = (int)frames;
            pthread_mutex_unlock(&ring->lock);
            break;
        }
        if (frames == 0) {
            usleep(500);
            continue;
        }
        audio_capture_ring_push(ring, input, (size_t)frames);
    }
    return NULL;
}

/*
 * HDMI and the internal codec run from independent clock domains.  A plain
 * arecord|aplay pipe eventually overruns, while alsaloop's "simple" sync
 * mode periodically inserts/removes whole samples and produces audible
 * clicks.  Keep both PCMs non-blocking and continuously vary libsamplerate's
 * ratio by a few ppm according to the HDMI capture FIFO fill level.
 */
static void* audio_passthrough_thread(void*)
{
    const char* capture_name = env_or_default("HDMI_AUDIO_CAPTURE_PCM",
                                               HDMI_AUDIO_CAPTURE_DEFAULT);
    const char* playback_name = env_or_default("HDMI_AUDIO_PLAYBACK_PCM",
                                                HDMI_AUDIO_PLAYBACK_DEFAULT);
    short input[HDMI_AUDIO_CHUNK_FRAMES * HDMI_AUDIO_CHANNELS];
    short output[HDMI_AUDIO_OUTPUT_FRAMES * HDMI_AUDIO_CHANNELS];
    float input_float[HDMI_AUDIO_CHUNK_FRAMES * HDMI_AUDIO_CHANNELS];
    float output_float[HDMI_AUDIO_OUTPUT_FRAMES * HDMI_AUDIO_CHANNELS];
    const double nominal_ratio = (double)HDMI_AUDIO_PLAYBACK_RATE /
                                 (double)HDMI_AUDIO_CAPTURE_RATE;
    struct sched_param sched = {};

    sched.sched_priority = HDMI_AUDIO_PLAYBACK_PRIORITY;
    if (pthread_setschedparam(pthread_self(), SCHED_RR, &sched) != 0) {
        kvm_log("[WARN] HDMI audio could not enable SCHED_RR priority %d\n",
                HDMI_AUDIO_PLAYBACK_PRIORITY);
    }

    while (g_audio_running && g_running && g_capture_started) {
        snd_pcm_t* capture = NULL;
        snd_pcm_t* playback = NULL;
        SRC_STATE* resampler = NULL;
        int src_error = 0;
        double ratio = nominal_ratio;
        unsigned long long input_total = 0;
        unsigned long long output_total = 0;
        unsigned int playback_xruns = 0;
        uint64_t report_start_ms = get_timestamp_ms();
        bool opened = false;
        audio_capture_ring_t capture_ring = {};
        pthread_t capture_worker = 0;
        bool ring_initialized = false;
        uint64_t prefill_start_ms = 0;

        int error = snd_pcm_open(&capture, capture_name, SND_PCM_STREAM_CAPTURE,
                                 SND_PCM_NONBLOCK);
        if (error < 0) {
            kvm_log("[WARN] HDMI audio capture open %s failed: %s\n",
                    capture_name, snd_strerror(error));
            goto audio_retry;
        }
        error = snd_pcm_open(&playback, playback_name, SND_PCM_STREAM_PLAYBACK,
                             SND_PCM_NONBLOCK);
        if (error < 0) {
            kvm_log("[WARN] HDMI audio playback open %s failed: %s\n",
                    playback_name, snd_strerror(error));
            goto audio_retry;
        }
        error = configure_audio_pcm(capture, HDMI_AUDIO_CAPTURE_RATE);
        if (error < 0) {
            kvm_log("[WARN] HDMI audio capture configure failed: %s\n",
                    snd_strerror(error));
            goto audio_retry;
        }
        error = configure_audio_pcm(playback, HDMI_AUDIO_PLAYBACK_RATE);
        if (error < 0) {
            kvm_log("[WARN] HDMI audio playback configure failed: %s\n",
                    snd_strerror(error));
            goto audio_retry;
        }

        resampler = src_new(SRC_SINC_FASTEST, HDMI_AUDIO_CHANNELS, &src_error);
        if (!resampler) {
            kvm_log("[WARN] HDMI audio resampler init failed: %s\n",
                    src_strerror(src_error));
            goto audio_retry;
        }

        capture_ring.pcm = capture;
        capture_ring.capacity_frames = HDMI_AUDIO_CAPTURE_RING_FRAMES;
        capture_ring.samples = (short*)calloc(
            capture_ring.capacity_frames * HDMI_AUDIO_CHANNELS,
            sizeof(short));
        if (!capture_ring.samples) {
            kvm_log("[WARN] HDMI audio capture ring allocation failed\n");
            goto audio_retry;
        }
        pthread_mutex_init(&capture_ring.lock, NULL);
        pthread_cond_init(&capture_ring.ready, NULL);
        ring_initialized = true;
        capture_ring.running = 1;

        snd_pcm_prepare(capture);
        snd_pcm_prepare(playback);
        error = snd_pcm_start(capture);
        if (error < 0) {
            kvm_log("[WARN] HDMI audio capture start failed: %s\n",
                    snd_strerror(error));
            goto audio_retry;
        }
        if (pthread_create(&capture_worker, NULL, audio_capture_worker,
                           &capture_ring) != 0) {
            kvm_log("[WARN] HDMI audio capture worker start failed\n");
            goto audio_retry;
        }

        prefill_start_ms = get_timestamp_ms();
        while (g_audio_running && g_running && g_capture_started &&
               audio_capture_ring_level(&capture_ring) <
                   HDMI_AUDIO_CAPTURE_PREFILL_FRAMES) {
            pthread_mutex_lock(&capture_ring.lock);
            int capture_error = capture_ring.fatal_error;
            pthread_mutex_unlock(&capture_ring.lock);
            if (capture_error) break;
            if (get_timestamp_ms() - prefill_start_ms >=
                    HDMI_AUDIO_PREFILL_TIMEOUT_MS) {
                break;
            }
            usleep(1000);
        }

        if (!g_audio_running || !g_running || !g_capture_started) {
            goto audio_retry;
        }
        pthread_mutex_lock(&capture_ring.lock);
        error = capture_ring.fatal_error;
        pthread_mutex_unlock(&capture_ring.lock);
        if (error) {
            kvm_log("[WARN] HDMI audio capture failed before prefill: %s\n",
                    snd_strerror(error));
            goto audio_retry;
        }
        if (audio_capture_ring_level(&capture_ring) <
                HDMI_AUDIO_CAPTURE_PREFILL_FRAMES) {
            kvm_log("[WARN] HDMI audio input clock/data unavailable; no frames received within %u ms, retrying\n",
                    HDMI_AUDIO_PREFILL_TIMEOUT_MS);
            goto audio_retry;
        }
        opened = true;
        kvm_log("[INFO] HDMI audio passthrough started: %s@%u -> %s@%u, capture ring + continuous SRC\n",
                capture_name, HDMI_AUDIO_CAPTURE_RATE,
                playback_name, HDMI_AUDIO_PLAYBACK_RATE);

        while (g_audio_running && g_running && g_capture_started) {
            snd_pcm_sframes_t playback_delay = 0;
            size_t ring_level = audio_capture_ring_level(&capture_ring);
            snd_pcm_sframes_t frames = (snd_pcm_sframes_t)
                audio_capture_ring_pop(&capture_ring, input,
                                       HDMI_AUDIO_CHUNK_FRAMES);
            if (frames == 0) {
                pthread_mutex_lock(&capture_ring.lock);
                int capture_error = capture_ring.fatal_error;
                pthread_mutex_unlock(&capture_ring.lock);
                if (capture_error) {
                    kvm_log("[WARN] HDMI audio capture failed: %s\n",
                            snd_strerror(capture_error));
                    break;
                }
                usleep(500);
                continue;
            }
            if (ring_level < (size_t)frames) {
                ring_level = (size_t)frames;
            }

            snd_pcm_delay(playback, &playback_delay);
            if (playback_delay < 0) playback_delay = 0;

            /* The capture worker is never blocked by playback.  Control the
             * small clock mismatch from both sides of the software bridge:
             * playback delay catches codec-clock drift, while ring level
             * catches scheduling stalls before they become HDMI overruns.
             */
            const double target_delay = HDMI_AUDIO_PLAYBACK_RATE *
                                        HDMI_AUDIO_TARGET_DELAY_SECONDS;
            const double target_ring = HDMI_AUDIO_CAPTURE_PREFILL_FRAMES;
            double playback_error =
                ((double)playback_delay - target_delay) / target_delay;
            double ring_error = ((double)ring_level - target_ring) /
                                target_ring;
            double correction = playback_error * 0.0005 +
                                ring_error * 0.0015;
            if (correction > 0.003) correction = 0.003;
            if (correction < -0.003) correction = -0.003;
            double target_ratio = nominal_ratio * (1.0 - correction);
            ratio += (target_ratio - ratio) * 0.02;

            src_short_to_float_array(input, input_float,
                                     (int)frames * HDMI_AUDIO_CHANNELS);
            SRC_DATA data = {};
            data.data_in = input_float;
            data.data_out = output_float;
            data.input_frames = frames;
            data.output_frames = HDMI_AUDIO_OUTPUT_FRAMES;
            data.src_ratio = ratio;
            data.end_of_input = 0;
            src_error = src_process(resampler, &data);
            if (src_error != 0) {
                kvm_log("[WARN] HDMI audio resampler failed: %s\n",
                        src_strerror(src_error));
                break;
            }
            if (data.input_frames_used != frames) {
                kvm_log("[WARN] HDMI audio SRC input short consume: %ld/%ld\n",
                        data.input_frames_used, (long)frames);
            }

            src_float_to_short_array(output_float, output,
                                     (int)data.output_frames_gen *
                                     HDMI_AUDIO_CHANNELS);
            snd_pcm_sframes_t written = 0;
            while (written < data.output_frames_gen && g_audio_running &&
                   g_running && g_capture_started) {
                snd_pcm_sframes_t count = snd_pcm_writei(
                    playback,
                    output + written * HDMI_AUDIO_CHANNELS,
                    data.output_frames_gen - written);
                if (count == -EAGAIN) {
                    usleep(1000);
                    continue;
                }
                if (count == -EPIPE || count == -ESTRPIPE) {
                    playback_xruns++;
                    snd_pcm_recover(playback, (int)count, 1);
                    continue;
                }
                if (count < 0) {
                    kvm_log("[WARN] HDMI audio playback failed: %s\n",
                            snd_strerror((int)count));
                    break;
                }
                written += count;
            }

            input_total += (unsigned long long)data.input_frames_used;
            output_total += (unsigned long long)written;
            uint64_t now_ms = get_timestamp_ms();
            if (now_ms - report_start_ms >= 5000) {
                double ppm = (ratio / nominal_ratio - 1.0) * 1000000.0;
                unsigned long long captured = 0;
                unsigned long long dropped = 0;
                unsigned long long measured_samples = 0;
                unsigned long long sample_square_sum = 0;
                unsigned int capture_xruns = 0;
                unsigned int peak_sample = 0;
                pthread_mutex_lock(&capture_ring.lock);
                captured = capture_ring.captured_frames;
                dropped = capture_ring.dropped_frames;
                capture_xruns = capture_ring.xruns;
                ring_level = capture_ring.queued_frames;
                measured_samples = capture_ring.measured_samples;
                sample_square_sum = capture_ring.sample_square_sum;
                peak_sample = capture_ring.peak_sample;
                capture_ring.measured_samples = 0;
                capture_ring.sample_square_sum = 0;
                capture_ring.peak_sample = 0;
                pthread_mutex_unlock(&capture_ring.lock);
                double rms = measured_samples
                    ? sqrt((double)sample_square_sum / (double)measured_samples)
                    : 0.0;
                double rms_dbfs = rms > 0.0 ? 20.0 * log10(rms / 32768.0) : -120.0;
                double peak_dbfs = peak_sample > 0
                    ? 20.0 * log10((double)peak_sample / 32768.0) : -120.0;
                kvm_log("[INFO] HDMI audio stable: captured=%llu in=%llu out=%llu ring=%zu delay=%ld ratio_ppm=%+.1f rms_dbfs=%.1f peak_dbfs=%.1f capture_xruns=%u playback_xruns=%u dropped=%llu\n",
                        captured, input_total, output_total, ring_level,
                        (long)playback_delay, ppm, rms_dbfs, peak_dbfs, capture_xruns,
                        playback_xruns, dropped);
                report_start_ms = now_ms;
            }
        }

audio_retry:
        if (ring_initialized) capture_ring.running = 0;
        if (capture_worker) {
            pthread_join(capture_worker, NULL);
            capture_worker = 0;
        }
        if (opened) {
            kvm_log("[INFO] HDMI audio passthrough stopping (in=%llu out=%llu capture_xruns=%u playback_xruns=%u dropped=%llu)\n",
                    input_total, output_total,
                    ring_initialized ? capture_ring.xruns : 0,
                    playback_xruns,
                    ring_initialized ? capture_ring.dropped_frames : 0);
        }
        if (capture) {
            snd_pcm_drop(capture);
            snd_pcm_close(capture);
        }
        if (playback) {
            snd_pcm_drop(playback);
            snd_pcm_close(playback);
        }
        if (resampler) src_delete(resampler);
        if (ring_initialized) {
            pthread_cond_destroy(&capture_ring.ready);
            pthread_mutex_destroy(&capture_ring.lock);
        }
        free(capture_ring.samples);

        if (g_audio_running && g_running && g_capture_started) sleep(1);
    }

    return NULL;
}

static void start_audio_passthrough(void)
{
    if (g_audio_thread || !g_display_mode) return;

    g_audio_running = 1;
    if (pthread_create(&g_audio_thread, NULL, audio_passthrough_thread, NULL) != 0) {
        g_audio_running = 0;
        g_audio_thread = 0;
        kvm_log("[WARN] Cannot start HDMI audio passthrough thread\n");
    }
}

static void stop_audio_passthrough(void)
{
    if (!g_audio_thread) return;
    g_audio_running = 0;
    pthread_join(g_audio_thread, NULL);
    g_audio_thread = 0;
}

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jump;
} snapshot_jpeg_error_t;

static void snapshot_jpeg_error_exit(j_common_ptr cinfo)
{
    snapshot_jpeg_error_t* error = (snapshot_jpeg_error_t*)cinfo->err;
    longjmp(error->jump, 1);
}

static unsigned char clamp_rgb(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (unsigned char)value;
}

/* A dequeued, completely zeroed NV12 buffer renders as a solid green frame.
 * It is a transport failure, not valid black video (limited-range black is
 * Y=16, UV=128). Sample the planes before publishing a frame or enabling the
 * display layer so a stalled VIN path can never be presented as HDMI video.
 */
static bool nv12_frame_has_signal(const unsigned char* y_plane,
                                  size_t y_length,
                                  const unsigned char* uv_plane,
                                  size_t uv_length,
                                  int width,
                                  int height)
{
    if (!y_plane || !uv_plane || width <= 0 || height <= INVALID_BOTTOM_ROWS) {
        return false;
    }

    const size_t y_required = (size_t)width * (height - INVALID_BOTTOM_ROWS);
    const size_t uv_required = (size_t)width * ((height - INVALID_BOTTOM_ROWS) / 2);
    if (y_length < y_required || uv_length < uv_required) {
        return false;
    }

    const size_t sample_count = 1024;
    for (size_t i = 0; i < sample_count; i++) {
        size_t y_pos = i * (y_required - 1) / (sample_count - 1);
        size_t uv_pos = i * (uv_required - 1) / (sample_count - 1);
        if (y_plane[y_pos] != 0 || uv_plane[uv_pos] != 0) {
            return true;
        }
    }
    return false;
}

static int encode_nv12_snapshot(const unsigned char* y_plane,
                                const unsigned char* uv_plane,
                                int src_width,
                                int src_height)
{
    struct jpeg_compress_struct cinfo;
    snapshot_jpeg_error_t jerr;
    unsigned char* row = NULL;
    FILE* output = NULL;
    const char* temporary_path = SNAPSHOT_PATH ".tmp";
    int result = -1;

    if (!y_plane || !uv_plane || src_width <= 0 || src_height <= 0) return -1;

    row = (unsigned char*)malloc((size_t)SNAPSHOT_WIDTH * 3);
    output = fopen(temporary_path, "wb");
    if (!row || !output) goto done;
    if (chmod(temporary_path, 0600) != 0) goto done;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = snapshot_jpeg_error_exit;
    if (setjmp(jerr.jump)) {
        jpeg_destroy_compress(&cinfo);
        goto done;
    }

    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, output);
    cinfo.image_width = SNAPSHOT_WIDTH;
    cinfo.image_height = SNAPSHOT_HEIGHT;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, SNAPSHOT_QUALITY, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        int dst_y = (int)cinfo.next_scanline;
        int valid_src_height = src_height - INVALID_BOTTOM_ROWS;
        int src_y = dst_y * valid_src_height / SNAPSHOT_HEIGHT;
        for (int dst_x = 0; dst_x < SNAPSHOT_WIDTH; dst_x++) {
            int src_x = dst_x * src_width / SNAPSHOT_WIDTH;
            int y = y_plane[src_y * src_width + src_x];
            int uv_index = (src_y / 2) * src_width + (src_x & ~1);
            int u = (int)uv_plane[uv_index] - 128;
            int v = (int)uv_plane[uv_index + 1] - 128;
            int c = y - 16;
            int offset = dst_x * 3;
            if (c < 0) c = 0;
            row[offset] = clamp_rgb((298 * c + 409 * v + 128) >> 8);
            row[offset + 1] = clamp_rgb((298 * c - 100 * u - 208 * v + 128) >> 8);
            row[offset + 2] = clamp_rgb((298 * c + 516 * u + 128) >> 8);
        }
        JSAMPROW rows[1] = { row };
        jpeg_write_scanlines(&cinfo, rows, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    if (fflush(output) == 0 && fsync(fileno(output)) == 0) {
        fclose(output);
        output = NULL;
        if (rename(temporary_path, SNAPSHOT_PATH) == 0) result = 0;
    }

done:
    if (output) fclose(output);
    if (result != 0) unlink(temporary_path);
    free(row);
    return result;
}

static void complete_snapshot(const unsigned char* y_plane,
                              const unsigned char* uv_plane,
                              int width,
                              int height)
{
    int should_encode = 0;

    pthread_mutex_lock(&g_snapshot_lock);
    if (g_snapshot_state == 1) {
        g_snapshot_state = 2;
        should_encode = 1;
    }
    pthread_mutex_unlock(&g_snapshot_lock);
    if (!should_encode) return;

    int result = encode_nv12_snapshot(y_plane, uv_plane, width, height);
    struct stat st;
    long size = result == 0 && stat(SNAPSHOT_PATH, &st) == 0 ? (long)st.st_size : 0;

    pthread_mutex_lock(&g_snapshot_lock);
    if (g_snapshot_state == 2) {
        g_snapshot_result = result;
        g_snapshot_size = size;
        g_snapshot_state = 3;
        pthread_cond_broadcast(&g_snapshot_cond);
    }
    pthread_mutex_unlock(&g_snapshot_lock);
}

static void* snapshot_server_thread(void*)
{
    int server_fd;
    int lock_fd;
    struct sockaddr_un addr;

    mkdir("/var/run/aitvbox", 0751);
    chmod("/var/run/aitvbox", 0751);

    /* aitvbox-kvm-video normally owns the product-wide MCP snapshot socket.
     * Never unlink a live provider's pathname when local Display Mode starts.
     * The advisory lock also makes cleanup ownership explicit. */
    lock_fd = open(CAPTURE_SOCKET_LOCK, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (lock_fd >= 0) close(lock_fd);
        kvm_log("[SNAPSHOT] Shared capture socket already has a provider\n");
        return NULL;
    }
    unlink(CAPTURE_SOCKET);
    server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        close(lock_fd);
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CAPTURE_SOCKET);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(server_fd, 4) < 0) {
        close(server_fd);
        unlink(CAPTURE_SOCKET);
        close(lock_fd);
        return NULL;
    }
    chmod(CAPTURE_SOCKET, 0660);
    fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK);

    while (g_running) {
        int client = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            usleep(50000);
            continue;
        }

        char command[32] = {0};
        struct timeval command_timeout = {.tv_sec = 2, .tv_usec = 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   &command_timeout, sizeof(command_timeout));
        size_t length = 0;
        while (length < sizeof(command) - 1) {
            ssize_t count = read(client, command + length,
                                 sizeof(command) - 1 - length);
            if (count <= 0) break;
            length += (size_t)count;
            if (memchr(command, '\n', length)) break;
        }
        if (length == 9 && memcmp(command, "SNAPSHOT\n", 9) == 0) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 5;

            pthread_mutex_lock(&g_snapshot_lock);
            if (g_snapshot_state == 1 || g_snapshot_state == 2) {
                dprintf(client, "ERROR busy\n");
            } else {
                g_snapshot_state = 1;
                g_snapshot_result = -1;
                while (g_snapshot_state != 3) {
                    if (pthread_cond_timedwait(&g_snapshot_cond,
                                               &g_snapshot_lock,
                                               &deadline) == ETIMEDOUT) {
                        g_snapshot_state = 0;
                        break;
                    }
                }
                if (g_snapshot_state == 3 && g_snapshot_result == 0) {
                    dprintf(client, "OK %s %ld\n", SNAPSHOT_PATH, g_snapshot_size);
                } else {
                    dprintf(client, "ERROR no-frame\n");
                }
                g_snapshot_state = 0;
            }
            pthread_mutex_unlock(&g_snapshot_lock);
        } else {
            dprintf(client, "ERROR unsupported-command\n");
        }
        close(client);
    }

    close(server_fd);
    unlink(CAPTURE_SOCKET);
    close(lock_fd);
    return NULL;
}

static int request_capture_command(const char* command,
                                   const char* success_prefix)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr;
    char response[256] = {0};

    if (fd < 0) return 1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CAPTURE_SOCKET);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "capture service unavailable: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    if (write(fd, command, strlen(command)) != (ssize_t)strlen(command)) {
        close(fd);
        return 1;
    }
    ssize_t length = read(fd, response, sizeof(response) - 1);
    close(fd);
    if (length <= 0) return 1;
    fputs(response, stdout);
    return !success_prefix || strncmp(response, success_prefix,
                                      strlen(success_prefix)) == 0 ? 0 : 1;
}

static int request_snapshot(void)
{
    return request_capture_command("SNAPSHOT\n", "OK ");
}

// Removed send_video_frame_to_socket and write_full functions as they are no longer needed.

// init_framebuffer function removed, replaced by init_display_output

// deinit_framebuffer function removed, replaced by deinit_display_output

static void deinit_display_output(void);

// Initialize display output using videoOutPort
static int init_display_output(void)
{
    VoutRect rect;
    int enable = 0; // Start disabled, enable later
    int rotate = 0; // No rotation

    g_sunxi_mem_ops = GetMemAdapterOpsS();
    if (!g_sunxi_mem_ops) {
        kvm_log("[ERROR] Failed to get SunxiMemOpsS\n");
        return -1;
    }
    SunxiMemOpen(g_sunxi_mem_ops);

    // Create video output port 0 (usually the main display)
    g_disp_out_port = CreateVideoOutport(0);
    if (g_disp_out_port == NULL) {
        kvm_log("[ERROR] CreateVideoOutport ERR\n");
        SunxiMemClose(g_sunxi_mem_ops);
        g_sunxi_mem_ops = NULL;
        return -1;
    }

    // Initialize the display port with content size (follows yuview pattern)
    rect.x = 0;
    rect.y = 0;
    rect.width = WIDTH;
    rect.height = HEIGHT;
    g_disp_out_port->init(g_disp_out_port, enable, rotate, &rect);

    // Get screen resolution after init
    int screen_w = g_disp_out_port->getScreenWidth(g_disp_out_port);
    int screen_h = g_disp_out_port->getScreenHeight(g_disp_out_port);
    kvm_log("[INFO] Screen resolution: %dx%d, video: %dx%d\n", screen_w, screen_h, WIDTH, HEIGHT);

    // Scale to fit screen preserving aspect ratio (letterbox)
    float scale_w = (float)screen_w / (float)WIDTH;
    float scale_h = (float)screen_h / (float)HEIGHT;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;
    int disp_w = (int)(WIDTH * scale);
    int disp_h = (int)(HEIGHT * scale);

    rect.x = (screen_w - disp_w) / 2;
    rect.y = (screen_h - disp_h) / 2;
    rect.width = disp_w;
    rect.height = disp_h;
    kvm_log("[INFO] Display rect: %dx%d at (%d,%d)\n", disp_w, disp_h, rect.x, rect.y);
    g_disp_out_port->setRect(g_disp_out_port, &rect);
    g_disp_out_port->SetZorder(g_disp_out_port, VIDEO_ZORDER_MIDDLE);

    // Allocate ION memory for the video frame (NV12: Y + UV planes)
    // Assuming a single buffer for simplicity, consider buffer_num for actual usage
    int frame_size = WIDTH * HEIGHT * 3 / 2; // NV12 format
    g_vop_vir_buf = (char*)SunxiMemPalloc(g_sunxi_mem_ops, frame_size);
    if (!g_vop_vir_buf) {
        kvm_log("[ERROR] SunxiMemPalloc for video frame failed\n");
        g_disp_out_port->deinit(g_disp_out_port);
        DestroyVideoOutport(g_disp_out_port);
        g_disp_out_port = NULL;
        SunxiMemClose(g_sunxi_mem_ops);
        g_sunxi_mem_ops = NULL;
        return -1;
    }
    g_vop_phy_buf = (char*)SunxiMemGetPhysicAddressCpu(g_sunxi_mem_ops, g_vop_vir_buf);
    if (!g_vop_phy_buf) {
        kvm_log("[ERROR] SunxiMemGetPhysicAddressCpu failed\n");
        SunxiMemPfree(g_sunxi_mem_ops, g_vop_vir_buf);
        g_vop_vir_buf = NULL;
        g_disp_out_port->deinit(g_disp_out_port);
        DestroyVideoOutport(g_disp_out_port);
        g_disp_out_port = NULL;
        SunxiMemClose(g_sunxi_mem_ops);
        g_sunxi_mem_ops = NULL;
        return -1;
    }

    kvm_log("[INFO] VideoOutPort initialized. ION buffer vir=%p, phy=%p, size=%d\n",
            g_vop_vir_buf, g_vop_phy_buf, frame_size);

    // Set up video parameters for queueToDisplay
    memset(&g_vop_vparam, 0, sizeof(videoParam));
    g_vop_vparam.srcInfo.crop_x = 0;
    g_vop_vparam.srcInfo.crop_y = 0;
    g_vop_vparam.srcInfo.crop_w = WIDTH;
    g_vop_vparam.srcInfo.crop_h = HEIGHT;
    g_vop_vparam.srcInfo.w = WIDTH;
    g_vop_vparam.srcInfo.h = HEIGHT;
    g_vop_vparam.srcInfo.format = VIDEO_PIXEL_FORMAT_NV12;
    g_vop_vparam.srcInfo.color_space = VIDEO_BT601;

    memset(&g_vop_rBuf, 0, sizeof(renderBuf));
    g_vop_rBuf.isExtPhy = VIDEO_USE_EXTERN_ION_BUF;
    g_vop_rBuf.y_phaddr = (unsigned long)g_vop_phy_buf;
    g_vop_rBuf.u_phaddr = (unsigned long)(g_vop_phy_buf + WIDTH * HEIGHT); // For NV12, U and V share the same plane, V is after U
    g_vop_rBuf.v_phaddr = (unsigned long)(g_vop_phy_buf + WIDTH * HEIGHT); // For NV12, U and V share the same plane
#ifdef CONF_KERNEL_IOMMU
    g_vop_rBuf.fd = SunxiMemGetBufferFd(g_sunxi_mem_ops, (void*)g_vop_vir_buf);
    kvm_log("[INFO] VideoOutPort IOMMU buffer fd=%d\n", g_vop_rBuf.fd);
#endif

    return 0;
}

// Deinitialize display output
static void deinit_display_output(void)
{
    if (g_disp_out_port) {
        g_disp_out_port->setEnable(g_disp_out_port, 0);
        g_disp_out_port->deinit(g_disp_out_port);
        DestroyVideoOutport(g_disp_out_port);
        g_disp_out_port = NULL;
    }

    if (g_vop_vir_buf) {
        SunxiMemPfree(g_sunxi_mem_ops, g_vop_vir_buf);
        g_vop_vir_buf = NULL;
        g_vop_phy_buf = NULL;
    }

    if (g_sunxi_mem_ops) {
        SunxiMemClose(g_sunxi_mem_ops);
        g_sunxi_mem_ops = NULL;
    }
    kvm_log("[INFO] VideoOutPort deinitialized and ION memory freed.\n");
}

static uint64_t get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void send_service_status(const char* status)
{
    const char* sock_path;
    int fd;
    struct sockaddr_un addr;

    if (!g_service_mode || !status) {
        return;
    }

    sock_path = getenv("HDMI_PREVIEW_STATUS_SOCK");
    if (!sock_path || sock_path[0] == '\0') {
        sock_path = HDMI_STATUS_SOCKET;
    }

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);
    sendto(fd, status, strlen(status), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(fd);
}

static void stop_current_capture_no_signal(void)
{
    send_service_status("NO_SIGNAL");
    if (g_service_mode) {
        g_capture_started = false;
    } else {
        g_running = false;
    }
}

typedef struct {
    bool valid;
    unsigned int width;
    unsigned int height;
    unsigned int fps;
    unsigned long long pixelclock;
} hdmi_signal_info_t;

static unsigned int calc_dv_fps(const struct v4l2_bt_timings* bt)
{
    unsigned long long total_w;
    unsigned long long total_h;
    unsigned long long denom;

    if (!bt || bt->width == 0 || bt->height == 0 || bt->pixelclock == 0) {
        return 0;
    }

    total_w = (unsigned long long)bt->width + bt->hfrontporch + bt->hsync + bt->hbackporch;
    total_h = (unsigned long long)bt->height + bt->vfrontporch + bt->vsync + bt->vbackporch;
    denom = total_w * total_h;
    if (denom == 0) {
        return 0;
    }

    return (unsigned int)((bt->pixelclock + denom / 2) / denom);
}

static bool validate_hdmi_timings(const struct v4l2_dv_timings* timings,
                                  hdmi_signal_info_t* info)
{
    const struct v4l2_bt_timings* bt;
    unsigned int fps;

    if (info) {
        memset(info, 0, sizeof(*info));
    }
    if (!timings || timings->type != V4L2_DV_BT_656_1120) {
        return false;
    }

    bt = &timings->bt;
    fps = calc_dv_fps(bt);
    if (info) {
        info->width = bt->width;
        info->height = bt->height;
        info->fps = fps;
        info->pixelclock = bt->pixelclock;
    }

    if (bt->width < 640 || bt->height < 480 || bt->pixelclock == 0) {
        return false;
    }
    if (fps < 15 || fps > 240) {
        return false;
    }

    if (info) {
        info->valid = true;
    }
    return true;
}

static int query_hdmi_signal_fd(int fd, const char* dev_name, hdmi_signal_info_t* info, bool verbose)
{
    struct v4l2_dv_timings timings;
    int ret;
    int saved_errno;

    if (fd < 0) {
        return -1;
    }

    memset(&timings, 0, sizeof(timings));
    ret = ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &timings);
    if (ret < 0) {
        saved_errno = errno;
        memset(&timings, 0, sizeof(timings));
        ret = ioctl(fd, VIDIOC_G_DV_TIMINGS, &timings);
        if (ret < 0) {
            if (saved_errno == ENOTTY || saved_errno == EINVAL || errno == ENOTTY || errno == EINVAL) {
                if (verbose) {
                    kvm_log("[INFO] HDMI timings ioctl is not supported on %s\n",
                            dev_name ? dev_name : "video device");
                }
                return HDMI_SIGNAL_UNSUPPORTED;
            }
            if (verbose) {
                kvm_log("[WARN] Cannot query HDMI timings on %s: %s\n",
                        dev_name ? dev_name : "video device", strerror(saved_errno));
            }
            return -1;
        }
    }

    if (!validate_hdmi_timings(&timings, info)) {
        if (verbose) {
        kvm_log("[INFO] HDMI signal not valid: type=%u width=%u height=%u pclk=%llu fps=%u\n",
                timings.type,
                timings.bt.width,
                    timings.bt.height,
                    (unsigned long long)timings.bt.pixelclock,
                    calc_dv_fps(&timings.bt));
        }
        return 0;
    }

    if (verbose && info) {
        kvm_log("[INFO] HDMI signal locked: %ux%u@%u pclk=%llu\n",
                info->width, info->height, info->fps, info->pixelclock);
    }
    return 1;
}

static void unmap_probe_buffers(buffer_t* buffers, int count)
{
    if (!buffers) {
        return;
    }

    for (int i = 0; i < count; i++) {
        for (int p = 0; p < MAX_PLANES; p++) {
            if (buffers[i].start[p]) {
                munmap(buffers[i].start[p], buffers[i].length[p]);
                buffers[i].start[p] = NULL;
            }
        }
    }
    free(buffers);
}

static int probe_hdmi_frame_signal(bool verbose)
{
    int fd;
    struct v4l2_format fmt;
    struct v4l2_streamparm parms;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[MAX_PLANES];
    enum v4l2_buf_type type;
    buffer_t* buffers = NULL;
    int buffer_count = 0;
    unsigned int nplanes = 1;
    fd_set fds;
    struct timeval tv;
    int ret = 2;

    fd = open_video_device();
    if (fd < 0) {
        return 2;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = WIDTH;
    fmt.fmt.pix_mp.height = HEIGHT;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0 && verbose) {
        kvm_log("[WARN] probe VIDIOC_S_FMT failed: %s\n", strerror(errno));
    }

    memset(&parms, 0, sizeof(parms));
    parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parms.parm.capture.timeperframe.numerator = 1;
    parms.parm.capture.timeperframe.denominator = FPS;
    parms.parm.capture.capturemode = 0x0002;
    if (ioctl(fd, VIDIOC_S_PARM, &parms) < 0 && verbose) {
        kvm_log("[WARN] probe VIDIOC_S_PARM failed: %s\n", strerror(errno));
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        if (verbose) kvm_log("[WARN] probe VIDIOC_G_FMT failed: %s\n", strerror(errno));
        goto out_close;
    }
    nplanes = fmt.fmt.pix_mp.num_planes;
    if (nplanes == 0 || nplanes > MAX_PLANES) {
        if (verbose) kvm_log("[WARN] probe invalid nplanes=%u\n", nplanes);
        goto out_close;
    }

    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        if (verbose) kvm_log("[WARN] probe VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        goto out_close;
    }
    if (req.count <= 0) {
        if (verbose) kvm_log("[WARN] probe no V4L2 buffers allocated\n");
        goto out_release_buffers;
    }

    buffer_count = req.count;
    buffers = (buffer_t*)calloc(buffer_count, sizeof(buffer_t));
    if (!buffers) {
        goto out_release_buffers;
    }

    for (int i = 0; i < buffer_count; i++) {
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = nplanes;
        buf.m.planes = planes;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            if (verbose) kvm_log("[WARN] probe VIDIOC_QUERYBUF %d failed\n", i);
            goto out_unmap;
        }

        for (unsigned int p = 0; p < nplanes; p++) {
            buffers[i].start[p] = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd, planes[p].m.mem_offset);
            if (buffers[i].start[p] == MAP_FAILED) {
                buffers[i].start[p] = NULL;
                if (verbose) kvm_log("[WARN] probe mmap buffer %d plane %u failed\n", i, p);
                goto out_unmap;
            }
            buffers[i].length[p] = planes[p].length;
        }

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = nplanes;
        buf.m.planes = planes;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            if (verbose) kvm_log("[WARN] probe VIDIOC_QBUF %d failed\n", i);
            goto out_unmap;
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        if (verbose) kvm_log("[WARN] probe VIDIOC_STREAMON failed: %s\n", strerror(errno));
        goto out_unmap;
    }

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = HDMI_PROBE_TIMEOUT_SEC;
    tv.tv_usec = 0;
    ret = select(fd + 1, &fds, NULL, NULL, &tv);
    if (ret > 0) {
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = nplanes;
        buf.m.planes = planes;
        if (ioctl(fd, VIDIOC_DQBUF, &buf) == 0) {
            bool valid_frame = false;
            if (buf.index < (unsigned int)buffer_count) {
                unsigned char* y_plane =
                    (unsigned char*)buffers[buf.index].start[0];
                size_t y_length = buffers[buf.index].length[0];
                unsigned char* uv_plane;
                size_t uv_length;
                size_t y_size = (size_t)WIDTH * HEIGHT;

                if (nplanes >= 2) {
                    uv_plane = (unsigned char*)buffers[buf.index].start[1];
                    uv_length = buffers[buf.index].length[1];
                } else if (y_length >= y_size) {
                    uv_plane = y_plane + y_size;
                    uv_length = y_length - y_size;
                } else {
                    uv_plane = NULL;
                    uv_length = 0;
                }
                valid_frame = nv12_frame_has_signal(y_plane, y_length,
                                                    uv_plane, uv_length,
                                                    WIDTH, HEIGHT);
            }
            if (valid_frame) {
                if (verbose) kvm_log("[INFO] HDMI frame probe locked: got valid first frame\n");
                ret = 0;
            } else {
                if (verbose) kvm_log("[WARN] HDMI frame probe rejected zero/invalid NV12 frame\n");
                ret = 1;
            }
        } else {
            if (verbose) kvm_log("[WARN] probe VIDIOC_DQBUF failed: %s\n", strerror(errno));
            ret = 1;
        }
    } else if (ret == 0) {
        if (verbose) kvm_log("[INFO] HDMI frame probe timeout: no signal/frame\n");
        ret = 1;
    } else {
        if (verbose) kvm_log("[WARN] HDMI frame probe select failed: %s\n", strerror(errno));
        ret = 2;
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);

out_unmap:
    unmap_probe_buffers(buffers, buffer_count);
    buffers = NULL;
out_release_buffers:
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_REQBUFS, &req);
out_close:
    close(fd);
    return ret;
}

static int open_video_device(void)
{
    int fd;
    struct v4l2_input input;

    fd = open(VIDEO_DEV, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        kvm_log("[ERROR] Cannot open %s: %s\n", VIDEO_DEV, strerror(errno));
        return -1;
    }

    memset(&input, 0, sizeof(input));
    input.index = 0;
    if (ioctl(fd, VIDIOC_S_INPUT, &input) < 0) {
        kvm_log("[WARN] Cannot set input 0: %s\n", strerror(errno));
    }

    return fd;
}

static int queue_capture_buffers(int fd, int count, unsigned int nplanes)
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[MAX_PLANES];

    for (int i = 0; i < count; i++) {
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = nplanes;
        buf.m.planes = planes;

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            kvm_log("[ERROR] Cannot queue buffer %d: %s\n", i, strerror(errno));
            return -1;
        }
    }

    return 0;
}

static void release_capture_buffers(int fd)
{
    struct v4l2_requestbuffers req;

    if (g_buffers) {
        for (int i = 0; i < g_buffer_count; i++) {
            for (int p = 0; p < MAX_PLANES; p++) {
                if (g_buffers[i].start[p]) {
                    if (munmap(g_buffers[i].start[p], g_buffers[i].length[p]) < 0) {
                        kvm_log("[WARN] munmap failed for buffer %d plane %d\n", i, p);
                    }
                    g_buffers[i].start[p] = NULL;
                    g_buffers[i].length[p] = 0;
                }
            }
        }
        free(g_buffers);
        g_buffers = NULL;
        g_buffer_count = 0;
    }

    if (fd >= 0) {
        memset(&req, 0, sizeof(req));
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_MMAP;
        ioctl(fd, VIDIOC_REQBUFS, &req);
    }
}

static int probe_hdmi_signal(bool verbose)
{
    int fd;
    int state;
    int frame_state;
    hdmi_signal_info_t info;

    fd = open_video_device();
    if (fd < 0) {
        return -1;
    }

    state = query_hdmi_signal_fd(fd, VIDEO_DEV, &info, verbose);
    close(fd);

    if (state == HDMI_SIGNAL_UNSUPPORTED) {
        if (verbose) {
            kvm_log("[INFO] Falling back to HDMI frame probe\n");
        }
        frame_state = probe_hdmi_frame_signal(verbose);
        if (frame_state == 0) {
            return 1;
        }
        if (frame_state == 1) {
            return 0;
        }
        return -1;
    }
    return state;
}

static void print_usage(const char* prog)
{
    fprintf(stdout, "Usage: %s [--probe|--service [--display]|--audio-only|--snapshot|--capture-status|--display-enable|--display-disable]\n", prog);
    fprintf(stdout, "  --probe    Check HDMI signal and exit: 0=locked, 1=no signal, 2=error\n");
    fprintf(stdout, "  --service  Run continuously and publish status to %s\n", HDMI_STATUS_SOCKET);
    fprintf(stdout, "  --display  With --service, render locally and pass HDMI audio through\n");
    fprintf(stdout, "  --snapshot Capture the next service frame to %s\n", SNAPSHOT_PATH);
    fprintf(stdout, "  --capture-status Query the shared Capture Daemon\n");
    fprintf(stdout, "  --display-enable Enable the shared local HDMI output\n");
    fprintf(stdout, "  --display-disable Disable the shared local HDMI output\n");
    fprintf(stdout, "  --audio-only Run HDMI audio passthrough without opening video capture\n");
}

// encode_uint64 function removed as it's no longer needed.

// encode_uint32 function removed as it's no longer needed.

// findAllNALUnits function removed as it's no longer needed.

// EncoderCallback class removed as it's no longer needed.

// Signal handler
static void signal_handler(int sig)
{
    kvm_log("\n[INFO] Received signal %d, stopping...\n", sig);
    g_running = false;

    // Removed control socket shutdown as it's no longer needed.
}

// connect_sockets function removed as it's no longer needed.

// Send video state to ctrl socket
// send_video_state function removed as it's no longer needed.

// init_ion_memory function removed as it's no longer needed.

// free_ion_memory function removed as it's no longer needed.

// Forward declaration
static void* capture_thread(void* arg);

// connect_ctrl_socket function removed as it's no longer needed.

// Simple JSON parsing for control commands
// parse_action function removed as it's no longer needed.

// control_thread function removed as it's no longer needed.

// init_encoder function removed as it's no longer needed.

// Video capture thread
static void* capture_thread(void* arg)
{
    (void)arg;
    kvm_log("[INFO] Capture thread started\n");

    struct v4l2_format fmt;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;
    struct v4l2_plane planes[MAX_PLANES];
    enum v4l2_buf_type type;
    unsigned int nplanes = 1;
    bool map_failed = false;
    bool display_initialized = false;
    bool display_enabled = false;
    bool timings_supported = true;
    hdmi_signal_info_t signal_info;
    int signal_state;

    // Open device
    g_video_fd = open_video_device();
    if (g_video_fd < 0) {
        // Reset capture state so it can be retried later
        g_capture_started = false;
        return NULL;
    }

    signal_state = query_hdmi_signal_fd(g_video_fd, VIDEO_DEV, &signal_info, true);
    if (signal_state == HDMI_SIGNAL_UNSUPPORTED || signal_state < 0) {
        timings_supported = false;
        kvm_log("[INFO] HDMI timings unavailable, probing frame before opening display layer\n");
        close(g_video_fd);
        g_video_fd = -1;

        if (probe_hdmi_frame_signal(true) != 0) {
            kvm_log("[WARN] No HDMI frame available, capture not started\n");
            send_service_status("NO_SIGNAL");
            g_capture_started = false;
            return NULL;
        }

        g_video_fd = open_video_device();
        if (g_video_fd < 0) {
            g_capture_started = false;
            return NULL;
        }
        kvm_log("[INFO] HDMI frame probe passed, starting capture with frame watchdog\n");
    } else if (signal_state <= 0) {
        kvm_log("[WARN] No valid HDMI signal, capture not started\n");
        send_service_status("NO_SIGNAL");
        close(g_video_fd);
        g_video_fd = -1;
        g_capture_started = false;
        return NULL;
    }

    // Plain --service remains a headless snapshot provider for the browser
    // Agent. The desktop's Display Mode uses --service --display so this
    // process also owns the local video layer and HDMI audio passthrough.
    if (!g_service_mode || g_display_mode) {
        if (init_display_output() < 0) {
            send_service_status("ERROR display_init");
            close(g_video_fd);
            g_video_fd = -1;
            g_capture_started = false;
            return NULL;
        }
        display_initialized = true;
    }

    // Set format
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = WIDTH;
    fmt.fmt.pix_mp.height = HEIGHT;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    ioctl(g_video_fd, VIDIOC_S_FMT, &fmt);

    // Set framerate
    struct v4l2_streamparm parms;
    memset(&parms, 0, sizeof(parms));
    parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parms.parm.capture.timeperframe.numerator = 1;
    parms.parm.capture.timeperframe.denominator = FPS;
    parms.parm.capture.capturemode = 0x0002;
    ioctl(g_video_fd, VIDIOC_S_PARM, &parms);

    // Verify format
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(g_video_fd, VIDIOC_G_FMT, &fmt) < 0) {
        kvm_log("[ERROR] VIDIOC_G_FMT failed: %s\n", strerror(errno));
        send_service_status("ERROR g_fmt");
        close(g_video_fd);
        g_video_fd = -1;
        if (display_initialized) deinit_display_output();
        return NULL;
    }
    nplanes = fmt.fmt.pix_mp.num_planes;
    if (nplanes == 0 || nplanes > MAX_PLANES) {
        kvm_log("[ERROR] invalid nplanes=%u\n", nplanes);
        send_service_status("ERROR planes");
        close(g_video_fd);
        g_video_fd = -1;
        if (display_initialized) deinit_display_output();
        return NULL;
    }
    kvm_log("[INFO] Format set: %dx%d nplanes=%u pixfmt=0x%x\n",
            fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, nplanes, fmt.fmt.pix_mp.pixelformat);

// send_video_state(fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, (float)FPS); // Removed, no longer needed.

    // Request MMAP buffers
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(g_video_fd, VIDIOC_REQBUFS, &req) < 0) {
        kvm_log("[ERROR] Cannot request buffers: %s\n", strerror(errno));
        send_service_status("ERROR reqbufs");
        close(g_video_fd);
        g_video_fd = -1;
        if (display_initialized) deinit_display_output();
        return NULL;
    }

    if (g_buffers) {
        release_capture_buffers(g_video_fd);
    }
    g_buffer_count = req.count;
    g_buffers = (buffer_t*)calloc(g_buffer_count, sizeof(buffer_t));
    if (!g_buffers) {
        kvm_log("[ERROR] Cannot allocate buffer metadata\n");
        send_service_status("ERROR alloc");
        release_capture_buffers(g_video_fd);
        close(g_video_fd);
        g_video_fd = -1;
        if (display_initialized) deinit_display_output();
        return NULL;
    }

    kvm_log("[INFO] Allocated %d buffers (MMAP mode)\n", g_buffer_count);

    // Map buffers
    for (int i = 0; i < g_buffer_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = nplanes;
        memset(planes, 0, sizeof(planes));
        buf.m.planes = planes;

        if (ioctl(g_video_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            kvm_log("[ERROR] Cannot query buffer %d\n", i);
            map_failed = true;
            break;
        }

        for (unsigned int p = 0; p < nplanes; p++) {
            g_buffers[i].start[p] = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE,
                                         MAP_SHARED, g_video_fd, planes[p].m.mem_offset);
            if (g_buffers[i].start[p] == MAP_FAILED) {
                kvm_log("[ERROR] mmap failed for buffer %d plane %u\n", i, p);
                g_buffers[i].start[p] = NULL;
                map_failed = true;
                break;
            }
            g_buffers[i].length[p] = planes[p].length;
        }

        if (map_failed) {
            break;
        }

        kvm_log("[INFO] Buffer %d mapped: p0=%d p1=%d p2=%d bytes\n",
                i, g_buffers[i].length[0], g_buffers[i].length[1], g_buffers[i].length[2]);

    }

    if (map_failed) {
        send_service_status("ERROR mmap");
        release_capture_buffers(g_video_fd);
        close(g_video_fd);
        g_video_fd = -1;
        if (display_initialized) deinit_display_output();
        return NULL;
    }

    if (queue_capture_buffers(g_video_fd, g_buffer_count, nplanes) < 0) {
        send_service_status("ERROR qbuf");
        release_capture_buffers(g_video_fd);
        close(g_video_fd);
        g_video_fd = -1;
        if (display_initialized) deinit_display_output();
        return NULL;
    }

    // Start streaming
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(g_video_fd, VIDIOC_STREAMON, &type) < 0) {
        kvm_log("[ERROR] Cannot start streaming: %s\n", strerror(errno));
        send_service_status("ERROR streamon");
        release_capture_buffers(g_video_fd);
        close(g_video_fd);
        g_video_fd = -1;
        if (display_initialized) deinit_display_output();
        return NULL;
    }

    kvm_log("[INFO] Streaming started\n");

    // Send SPS/PPS header at each capture start to improve first-frame decode on reconnect. (REMOVED)
    // if (g_encoder && g_video_socket_fd >= 0) {
    //     unsigned char header[1024] = {0};
    //     int headerLen = g_encoder->getHeader(header);
    //     if (headerLen > 0) {
    //         if (send_video_frame_to_socket(header, headerLen, NULL, 0) == 0) {
    //             kvm_log("[INFO] Sent codec header, len=%d\n", headerLen);
    //         } else {
    //             kvm_log("[WARN] Failed to send codec header: errno=%d\n", errno);
    //         }
    //     } else {
    //         kvm_log("[WARN] getHeader returned %d\n", headerLen);
    //     }
    // }

    kvm_log("[INFO] Starting direct FB display loop\n");

    // Capture loop
    fd_set fds;
    struct timeval tv;
    int frame_count = 0;
    int fps_window_frames = 0;
    int timeout_count = 0;
    int timeout_limit = g_service_mode ? HDMI_SERVICE_TIMEOUT_LIMIT : HDMI_TIMEOUT_LIMIT;
    int invalid_signal_count = 0;
    unsigned int invalid_frame_count = 0;
    uint64_t fps_window_start_ms = get_timestamp_ms();

    while (g_running && g_capture_started) {
        FD_ZERO(&fds);
        FD_SET(g_video_fd, &fds);
        tv.tv_sec = g_service_mode ? HDMI_SERVICE_SELECT_TIMEOUT_SEC : HDMI_CAPTURE_SELECT_TIMEOUT_SEC;
        tv.tv_usec = 0;

        int r = select(g_video_fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) {
            timeout_count++;
            kvm_log("[WARN] Capture timeout %d/%d\n", timeout_count, timeout_limit);
            if (timeout_count >= timeout_limit) {
                if (timings_supported) {
                    signal_state = query_hdmi_signal_fd(g_video_fd, VIDEO_DEV, &signal_info, true);
                    if (signal_state != 1) {
                        kvm_log("[WARN] HDMI signal lost after capture timeouts\n");
                        stop_current_capture_no_signal();
                        break;
                    }
                    timeout_count = 0;
                } else {
                    kvm_log("[WARN] HDMI frame watchdog timed out, stopping preview\n");
                    stop_current_capture_no_signal();
                    break;
                }
            }
            continue;
        }
        timeout_count = 0;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = nplanes;
        memset(planes, 0, sizeof(planes));
        buf.m.planes = planes;

        if (ioctl(g_video_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            kvm_log("[ERROR] DQBUF failed\n");
            break;
        }

        if (buf.index < (unsigned int)g_buffer_count) {
            char* src_y;
            char* src_uv;
            unsigned int y_size = WIDTH * HEIGHT;
            unsigned int uv_size = y_size / 2;
            size_t y_length = g_buffers[buf.index].length[0];
            size_t uv_length;

            if (nplanes >= 2) {
                src_y = (char*)g_buffers[buf.index].start[0];
                src_uv = (char*)g_buffers[buf.index].start[1];
                uv_length = g_buffers[buf.index].length[1];
            } else {
                src_y = (char*)g_buffers[buf.index].start[0];
                src_uv = src_y + y_size;
                uv_length = y_length > y_size ? y_length - y_size : 0;
            }

            if (!nv12_frame_has_signal((const unsigned char*)src_y, y_length,
                                       (const unsigned char*)src_uv, uv_length,
                                       WIDTH, HEIGHT)) {
                invalid_frame_count++;
                if (invalid_frame_count == 1 || invalid_frame_count % 30 == 0) {
                    kvm_log("[WARN] Rejected zero/invalid HDMI NV12 frame (count=%u)\n",
                            invalid_frame_count);
                }
            } else {
                invalid_frame_count = 0;
                complete_snapshot((const unsigned char*)src_y,
                                  (const unsigned char*)src_uv,
                                  WIDTH,
                                  HEIGHT);

                if (g_vop_vir_buf) {
                    memcpy(g_vop_vir_buf, src_y, y_size);
                    memcpy(g_vop_vir_buf + y_size, src_uv, uv_size);
                    SunxiMemFlushCache(g_sunxi_mem_ops, (void*)g_vop_vir_buf,
                                       y_size + uv_size);
                    g_disp_out_port->queueToDisplay(g_disp_out_port,
                                                    y_size + uv_size,
                                                    &g_vop_vparam,
                                                    &g_vop_rBuf);
                }
                if (!display_enabled) {
                    send_service_status("LOCKED");
                    if (g_disp_out_port)
                        g_disp_out_port->setEnable(g_disp_out_port, 1);
                    display_enabled = true;
                    start_audio_passthrough();
                    send_service_status("RUNNING");
                    kvm_log(g_service_mode
                                ? (g_display_mode
                                       ? "[INFO] Service display and audio ready after first valid HDMI frame\n"
                                       : "[INFO] Headless capture ready after first valid HDMI frame\n")
                                : "[INFO] Display layer enabled after first valid HDMI frame\n");
                }
            }
        }

        // Re-queue buffer
        if (ioctl(g_video_fd, VIDIOC_QBUF, &buf) < 0) {
            kvm_log("[ERROR] QBUF failed\n");
        }

        frame_count++;
        fps_window_frames++;

        uint64_t now_ms = get_timestamp_ms();
        uint64_t elapsed_ms = now_ms - fps_window_start_ms;
        if (elapsed_ms >= 2000) {
            double real_fps = (double)fps_window_frames * 1000.0 / (double)elapsed_ms;
            kvm_log("[INFO] Capture FPS: %.2f (window=%llu ms, frames=%d)\n",
                    real_fps, (unsigned long long)elapsed_ms, fps_window_frames);
            if (timings_supported) {
                signal_state = query_hdmi_signal_fd(g_video_fd, VIDEO_DEV, &signal_info, false);
                if (signal_state == 1) {
                    invalid_signal_count = 0;
                } else {
                    invalid_signal_count++;
                    kvm_log("[WARN] HDMI timings invalid %d/%d\n",
                            invalid_signal_count, HDMI_INVALID_LIMIT);
                    if (invalid_signal_count >= HDMI_INVALID_LIMIT) {
                        kvm_log("[WARN] HDMI signal lost, stopping preview\n");
                        stop_current_capture_no_signal();
                        break;
                    }
                }
            }
            fps_window_start_ms = now_ms;
            fps_window_frames = 0;
        }
    }

    kvm_log("[INFO] Capture thread stopped, captured %d frames\n", frame_count);

    stop_audio_passthrough();

    // --- MUST CLEANUP HARDWARE RESOURCES HERE ---
    // Stop streaming
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (g_video_fd >= 0 && ioctl(g_video_fd, VIDIOC_STREAMOFF, &type) < 0) {
        kvm_log("[WARN] VIDIOC_STREAMOFF failed: %s\n", strerror(errno));
    }

    // Unmap buffers
    release_capture_buffers(g_video_fd);

    // Close device
    if (g_video_fd >= 0) {
        kvm_log("[INFO] Closing video device fd=%d\n", g_video_fd);
        close(g_video_fd);
        g_video_fd = -1;
    }
    // Deinitialize display output
    deinit_display_output();
    // ---------------------------------------------

    return NULL;
}
int main(int argc, char* argv[])
{
    bool probe_only = false;
    bool service_mode = false;
    bool snapshot_only = false;
    bool audio_only = false;
    const char* capture_command = NULL;
    const char* capture_success = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--probe") == 0 || strcmp(argv[i], "-p") == 0) {
            probe_only = true;
        } else if (strcmp(argv[i], "--service") == 0 || strcmp(argv[i], "-s") == 0) {
            service_mode = true;
        } else if (strcmp(argv[i], "--display") == 0) {
            g_display_mode = true;
        } else if (strcmp(argv[i], "--snapshot") == 0) {
            snapshot_only = true;
        } else if (strcmp(argv[i], "--capture-status") == 0) {
            capture_command = "STATUS\n";
            capture_success = "{\"state\":";
        } else if (strcmp(argv[i], "--display-enable") == 0) {
            capture_command = "DISPLAY 1\n";
            capture_success = "OK ";
        } else if (strcmp(argv[i], "--display-disable") == 0) {
            capture_command = "DISPLAY 0\n";
            capture_success = "OK ";
        } else if (strcmp(argv[i], "--audio-only") == 0) {
            audio_only = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    // Disable stdout buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (snapshot_only) return request_snapshot();
    if (capture_command) {
        return request_capture_command(capture_command, capture_success);
    }

    if (audio_only) {
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        g_running = true;
        g_capture_started = true;
        g_display_mode = true;
        start_audio_passthrough();
        while (g_running) pause();
        g_capture_started = false;
        stop_audio_passthrough();
        return 0;
    }

    if (probe_only && service_mode) {
        print_usage(argv[0]);
        return 2;
    }

    if (g_display_mode && !service_mode) {
        fprintf(stderr, "--display requires --service\n");
        return 2;
    }

    if (probe_only) {
        int state = probe_hdmi_signal(true);
        if (state == 1) {
            kvm_log("[INFO] HDMI probe result: locked\n");
            return 0;
        }
        if (state == 0) {
            kvm_log("[INFO] HDMI probe result: no signal\n");
            return 1;
        }
        kvm_log("[ERROR] HDMI probe result: error\n");
        return 2;
    }

    if (service_mode) {
        g_service_mode = true;
    }

    kvm_log("===========================================\n");
    kvm_log("  HDMI Preview v%s\n", VIDEO_VERSION);
    kvm_log("  Resolution: %dx%d @ %dfps\n", WIDTH, HEIGHT, FPS);
    kvm_log("  Bitrate: %dkbps\n", BITRATE);
    kvm_log("===========================================\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    // Snapshot clients enforce their own deadline. A client disconnecting
    // while JPEG encoding finishes must not terminate the capture service.
    signal(SIGPIPE, SIG_IGN);

    // Removed connection to luckfox_kvm control socket and waiting logic.

    // Removed Init Ion memory
    // if (init_ion_memory() < 0) {
    //     return -1;
    // }

    // Removed encoder initialization logic
    // kvm_log("[MAIN] Initializing encoder...\n");
    // if (init_encoder() < 0) {
    //     free_ion_memory();
    //     return -1;
    // }

    kvm_log("[MAIN] Starting capture %s...\n", service_mode ? "service" : "directly");

    g_running = true;
    if (service_mode) {
        int error = pthread_create(&g_snapshot_thread, NULL,
                                   snapshot_server_thread, NULL);
        if (error != 0) {
            g_running = false;
            kvm_log("[ERROR] Cannot start snapshot service: %s\n",
                    strerror(error));
            return 1;
        }
    }

    do {
        g_capture_started = true;
        pthread_create(&g_cap_thread, NULL, capture_thread, NULL);
        kvm_log("[MAIN] Capture thread started\n");

        pthread_join(g_cap_thread, NULL);
        g_cap_thread = 0;
        g_capture_started = false;

        if (service_mode && g_running) {
            send_service_status("NO_SIGNAL");
            kvm_log("[MAIN] Service waiting %d seconds before next HDMI capture attempt\n",
                    HDMI_SERVICE_RETRY_SEC);
            sleep(HDMI_SERVICE_RETRY_SEC);
        }
    } while (service_mode && g_running);

    if (g_snapshot_thread) {
        pthread_join(g_snapshot_thread, NULL);
        g_snapshot_thread = 0;
    }

    // Cleanup (removed encoder and ion memory cleanup)
    // if (g_encoder) {
    //     AWVideoEncoder::destroy(g_encoder);
    //     g_encoder = NULL;
    // }

    // if (g_callback) {
    //     delete g_callback;
    //     g_callback = NULL;
    // }

    // Free Ion memory
    // free_ion_memory();

    // Free buffers
    if (g_buffers) {
        for (int i = 0; i < g_buffer_count; i++) {
            for (int p = 0; p < MAX_PLANES; p++) {
                if (g_buffers[i].start[p]) {
                    munmap(g_buffers[i].start[p], g_buffers[i].length[p]);
                }
            }
        }
        free(g_buffers);
        g_buffers = NULL;
        g_buffer_count = 0;
    }

    // Close sockets
    // Removed control socket close as it's no longer needed.

    kvm_log("[INFO] Demo finished\n");
    return 0;
}
