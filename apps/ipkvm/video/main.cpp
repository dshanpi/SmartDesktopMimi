/*
 * A133 HDMI Capture + Hardware H.264 Encode
 * AITVBox IPKVM Unix socket sender
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
#include <time.h>
#include <atomic>

#include "AWVideoEncoder.h"
#include "sunxiMemInterface.h"
extern "C" {
#include <videoOutPort.h>
}

#define VIDEO_VERSION     "1.0.0"
#define VIDEO_DEV         "/dev/video0"
#define WIDTH            1920
#define HEIGHT           1080
#define FPS              60
#define BITRATE          6000
#define BUFFER_COUNT     3
#define CAPTURE_SOCKET   "/var/run/aitvbox/capture.sock"
#define CAPTURE_SOCKET_LOCK "/var/run/aitvbox/capture-socket.lock"
#define SNAPSHOT_PATH    "/var/run/aitvbox/screen.jpg"
#define SNAPSHOT_WIDTH   960
#define SNAPSHOT_HEIGHT  540
#define SNAPSHOT_QUALITY 80
#define VIDEO_FRAME_MAGIC "AIV1"

// Unix Socket for video data
#define VIDEO_SOCKET_PATH  "/tmp/kvm_video_stream.sock"
#define CTRL_SOCKET      "/var/run/kvm_ctrl.sock"

#define LOG_FILE         "/tmp/kvm_video.log"

// Helper for writing logs to file
static void log_to_file(const char* format, ...)
{
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

#define ALIGN_16B(x) (((x) + (15)) & ~(15))
#define VISIBLE_Y_PLANE_SIZE ((size_t)WIDTH * (size_t)HEIGHT)
#define VISIBLE_UV_PLANE_SIZE (VISIBLE_Y_PLANE_SIZE / 2)
#define INVALID_BOTTOM_ROWS 8
// VIN reports a macroblock-aligned total buffer length, but its NV12 planes
// are tightly packed: UV starts immediately after the 1080 visible Y rows.
// The extra bytes are tail padding rather than eight additional Y rows.
#define CAPTURE_Y_PLANE_SIZE VISIBLE_Y_PLANE_SIZE
#define SNAPSHOT_FRAME_SIZE (VISIBLE_Y_PLANE_SIZE + VISIBLE_UV_PLANE_SIZE)

using namespace awvideoencoder;

// Global variables
static int g_video_fd = -1;
static bool g_running = false;
static std::atomic<bool> g_capture_started(false);
static std::atomic<bool> g_capture_stopping(false);
static time_t g_last_capture_stop_time = 0;
static pthread_t g_cap_thread = 0;
static AWVideoEncoder* g_encoder = NULL;
static std::atomic<bool> g_display_requested(true);
static std::atomic<bool> g_display_enabled(false);
static std::atomic<bool> g_stream_requested(false);
static std::atomic<bool> g_force_keyframe(false);
static std::atomic<uint64_t> g_frame_count(0);
static std::atomic<uint64_t> g_last_frame_ms(0);
static std::atomic<uint64_t> g_last_capture_attempt_ms(0);
static std::atomic<uint64_t> g_recovery_count(0);

// Physically contiguous input shared with the CedarX video engine.
static dma_mem_des_t gIonMem;
static bool g_ion_inited = false;

// Socket fds
static int g_ctrl_fd = -1;

// Mutex for thread safety
static pthread_mutex_t g_cap_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_display_mutex = PTHREAD_MUTEX_INITIALIZER;

static videoParam g_display_video_param;
static renderBuf g_display_render_buf;
static dispOutPort* g_display_port = NULL;

// V4L2 buffer structure
typedef struct {
    void* start[3];
    int length[3];
    void* phy_addr[3];
    unsigned int fd[3];
} buffer_t;

static buffer_t* g_buffers = NULL;
static int g_buffer_count = 0;

// Socket file descriptor for video output
static int g_video_socket_fd = -1;

// Timestamp for frames
static uint64_t g_encoder_start_time = 0;
static pthread_t g_snapshot_server_thread = 0;
static pthread_t g_snapshot_encode_thread = 0;
static pthread_mutex_t g_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_snapshot_cond = PTHREAD_COND_INITIALIZER;
static unsigned char* g_snapshot_frame = NULL;
static int g_snapshot_state = 0;
static int g_snapshot_result = -1;
static long g_snapshot_size = 0;

static void* capture_thread(void* arg);
static int ensure_capture_started(void);
static int init_display_output(void);
static void deinit_display_output(void);
static void set_display_requested(bool enabled);

// Get current timestamp in milliseconds
static uint64_t get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static uint64_t get_timestamp_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

// Encode uint64 to big-endian bytes
static void encode_uint64(uint8_t* buf, uint64_t val)
{
    buf[0] = (uint8_t)((val >> 56) & 0xFF);
    buf[1] = (uint8_t)((val >> 48) & 0xFF);
    buf[2] = (uint8_t)((val >> 40) & 0xFF);
    buf[3] = (uint8_t)((val >> 32) & 0xFF);
    buf[4] = (uint8_t)((val >> 24) & 0xFF);
    buf[5] = (uint8_t)((val >> 16) & 0xFF);
    buf[6] = (uint8_t)((val >> 8) & 0xFF);
    buf[7] = (uint8_t)(val & 0xFF);
}

// Encode uint32 to big-endian bytes
static void encode_uint32(uint8_t* buf, uint32_t val)
{
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

// Helper: Find all NAL units in encoder output buffer
// Returns total size of all NAL units found
static int findAllNALUnits(const uint8_t* data, int maxLen, int* nalStarts, int* nalEnds, int maxNals)
{
    int nalCount = 0;
    int i = 0;

    while (i < maxLen - 4 && nalCount < maxNals) {
        // Check for 4-byte start code (0x00 0x00 0x00 0x01)
        if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x01) {
            nalStarts[nalCount] = i + 4;  // Skip start code, point to NAL header
            if (nalCount > 0) {
                nalEnds[nalCount - 1] = i;  // Previous NAL ends here
            }
            nalCount++;
            i += 4;
            continue;
        }
        // Check for 3-byte start code (0x00 0x00 0x01)
        if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x01) {
            nalStarts[nalCount] = i + 3;  // Skip start code, point to NAL header
            if (nalCount > 0) {
                nalEnds[nalCount - 1] = i;  // Previous NAL ends here
            }
            nalCount++;
            i += 3;
            continue;
        }
        i++;
    }

    // Set end of last NAL
    if (nalCount > 0) {
        nalEnds[nalCount - 1] = maxLen;
    }

    return nalCount;
}

// Encoder callback - directly output Annex-B H.264 data
// Simplified version: send raw buffer directly to backend without NAL parsing
static int send_all(int fd, const void* data, size_t length)
{
    const uint8_t* cursor = (const uint8_t*)data;
    while (length > 0) {
        ssize_t written = send(fd, cursor, length, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) {
            errno = EPIPE;
            return -1;
        }
        cursor += written;
        length -= (size_t)written;
    }
    return 0;
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

static int encode_nv12_snapshot(const unsigned char* frame)
{
    struct jpeg_compress_struct cinfo;
    snapshot_jpeg_error_t jerr;
    unsigned char* row = NULL;
    FILE* output = NULL;
    const char* temporary_path = SNAPSHOT_PATH ".tmp";
    const unsigned char* y_plane = NULL;
    const unsigned char* uv_plane = NULL;
    int result = -1;

    if (!frame) return -1;
    y_plane = frame;
    uv_plane = frame + CAPTURE_Y_PLANE_SIZE;
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
        int src_y = dst_y * HEIGHT / SNAPSHOT_HEIGHT;
        for (int dst_x = 0; dst_x < SNAPSHOT_WIDTH; dst_x++) {
            int src_x = dst_x * WIDTH / SNAPSHOT_WIDTH;
            int y = y_plane[src_y * WIDTH + src_x];
            int uv_index = (src_y / 2) * WIDTH + (src_x & ~1);
            int u = (int)uv_plane[uv_index] - 128;
            int v = (int)uv_plane[uv_index + 1] - 128;
            int c = y - 16;
            int offset = dst_x * 3;
            if (c < 0) c = 0;
            // HDMI 1080p capture is limited-range BT.709.
            row[offset] = clamp_rgb((298 * c + 459 * v + 128) >> 8);
            row[offset + 1] =
                clamp_rgb((298 * c - 55 * u - 136 * v + 128) >> 8);
            row[offset + 2] = clamp_rgb((298 * c + 541 * u + 128) >> 8);
        }
        JSAMPROW rows[1] = {row};
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

static void queue_snapshot_frame(const unsigned char* frame)
{
    pthread_mutex_lock(&g_snapshot_lock);
    if (g_snapshot_state == 1 && g_snapshot_frame && frame) {
        memcpy(g_snapshot_frame, frame, SNAPSHOT_FRAME_SIZE);
        g_snapshot_state = 2;
        pthread_cond_broadcast(&g_snapshot_cond);
    }
    pthread_mutex_unlock(&g_snapshot_lock);
}

static void* snapshot_encode_thread(void*)
{
    while (g_running) {
        pthread_mutex_lock(&g_snapshot_lock);
        while (g_running && g_snapshot_state != 2) {
            pthread_cond_wait(&g_snapshot_cond, &g_snapshot_lock);
        }
        if (!g_running) {
            pthread_mutex_unlock(&g_snapshot_lock);
            break;
        }
        pthread_mutex_unlock(&g_snapshot_lock);

        int result = encode_nv12_snapshot(g_snapshot_frame);
        struct stat st;
        long size =
            result == 0 && stat(SNAPSHOT_PATH, &st) == 0 ? (long)st.st_size : 0;

        pthread_mutex_lock(&g_snapshot_lock);
        if (g_snapshot_state == 2) {
            g_snapshot_result = result;
            g_snapshot_size = size;
            g_snapshot_state = 3;
            pthread_cond_broadcast(&g_snapshot_cond);
        }
        pthread_mutex_unlock(&g_snapshot_lock);
    }
    return NULL;
}

static void* snapshot_server_thread(void*)
{
    mkdir("/var/run/aitvbox", 0751);
    int lock_fd = open(CAPTURE_SOCKET_LOCK,
                       O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (lock_fd >= 0) close(lock_fd);
        kvm_log("[ERROR] MCP snapshot socket already has a provider\n");
        return NULL;
    }
    unlink(CAPTURE_SOCKET);
    int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        close(lock_fd);
        return NULL;
    }

    struct sockaddr_un addr;
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
            // This thread is also the lightweight capture supervisor. A lost
            // HDMI signal or V4L2 timeout must recover without waiting for a
            // browser, UI action or operator intervention.
            if (g_display_requested.load() || g_stream_requested.load()) {
                (void)ensure_capture_started();
            }
            usleep(50000);
            continue;
        }
        char command[32] = {0};
        struct timeval command_timeout = {.tv_sec = 2, .tv_usec = 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   &command_timeout, sizeof(command_timeout));
        ssize_t length = read(client, command, sizeof(command) - 1);
        if (length == 9 && memcmp(command, "SNAPSHOT\n", 9) == 0) {
            /*
             * MCP control must not depend on a browser already consuming
             * WebRTC video. After a reboot there is normally no peer session,
             * so start the shared capture pipeline on the first snapshot
             * request and let a later WebRTC start/stop command adopt it.
             * Keeping the pipeline alive also avoids a two-second hardware
             * encoder warm-up before every agent step.
             */
            if (ensure_capture_started() != 0) {
                dprintf(client, "ERROR capture-start\n");
                close(client);
                continue;
            }

            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 8;
            pthread_mutex_lock(&g_snapshot_lock);
            if (g_snapshot_state != 0) {
                dprintf(client, "ERROR busy\n");
            } else {
                g_snapshot_state = 1;
                g_snapshot_result = -1;
                pthread_cond_broadcast(&g_snapshot_cond);
                while (g_running && g_snapshot_state != 3) {
                    if (pthread_cond_timedwait(&g_snapshot_cond,
                                              &g_snapshot_lock,
                                              &deadline) == ETIMEDOUT) {
                        g_snapshot_state = 0;
                        break;
                    }
                }
                if (g_snapshot_state == 3 && g_snapshot_result == 0) {
                    dprintf(client, "OK %s %ld\n",
                            SNAPSHOT_PATH, g_snapshot_size);
                } else {
                    dprintf(client, "ERROR no-frame\n");
                }
                g_snapshot_state = 0;
                pthread_cond_broadcast(&g_snapshot_cond);
            }
            pthread_mutex_unlock(&g_snapshot_lock);
        } else if (length == 10 && memcmp(command, "DISPLAY 1\n", 10) == 0) {
            set_display_requested(true);
            if (init_display_output() == 0 && ensure_capture_started() == 0) {
                dprintf(client, "OK display=enabled\n");
            } else {
                dprintf(client, "ERROR display-start\n");
            }
        } else if (length == 10 && memcmp(command, "DISPLAY 0\n", 10) == 0) {
            set_display_requested(false);
            dprintf(client, "OK display=disabled\n");
        } else if (length == 7 && memcmp(command, "STATUS\n", 7) == 0) {
            if (g_display_requested.load()) {
                (void)ensure_capture_started();
            }
            const uint64_t now_ms = get_timestamp_ms();
            const uint64_t last_frame_ms = g_last_frame_ms.load();
            const uint64_t frame_age_ms = last_frame_ms == 0
                ? 0 : now_ms - last_frame_ms;
            const bool live = g_capture_started && last_frame_ms != 0 &&
                              frame_age_ms < 3000;
            const char* state = live ? "live" :
                (g_capture_started ? "starting" :
                 (g_display_requested.load() || g_stream_requested.load()
                      ? "no_signal" : "idle"));
            dprintf(client,
                    "{\"state\":\"%s\",\"capture\":%s,"
                    "\"displayRequested\":%s,\"display\":%s,"
                    "\"streamRequested\":%s,\"frames\":%llu,"
                    "\"frameAgeMs\":%llu,\"recoveries\":%llu,"
                    "\"width\":%d,\"height\":%d,\"fps\":%d}\n",
                    state,
                    g_capture_started ? "true" : "false",
                    g_display_requested.load() ? "true" : "false",
                    g_display_enabled.load() ? "true" : "false",
                    g_stream_requested.load() ? "true" : "false",
                    (unsigned long long)g_frame_count.load(),
                    (unsigned long long)frame_age_ms,
                    (unsigned long long)g_recovery_count.load(),
                    WIDTH, HEIGHT, FPS);
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

class EncoderCallback : public AWVideoEncoderDataCallback {
public:
    EncoderCallback() {}
    virtual ~EncoderCallback() {}

    virtual int encoderDataReady(AVPacket* packet) override
    {
        static int encode_count = 0;

        if (!packet || packet->dataLen0 <= 0 || !packet->pAddrVir0) {
            kvm_log("[ENCODER] ERROR: invalid packet (packet=%p, dataLen0=%d, pAddrVir0=%p)\n",
                   packet, packet ? packet->dataLen0 : -1, packet ? packet->pAddrVir0 : NULL);
            return -1;
        }

        // Initialize start time on first frame
        if (g_encoder_start_time == 0) {
            g_encoder_start_time = get_timestamp_ms();
            kvm_log("[ENCODER] START: encoder started at ts=%llu\n", (unsigned long long)g_encoder_start_time);
        }

        // Raw buffer from encoder
        const uint8_t* data0 = (const uint8_t*)packet->pAddrVir0;
        int dataLen0 = packet->dataLen0;

        const uint8_t* data1 = (const uint8_t*)packet->pAddrVir1;
        int dataLen1 = packet->dataLen1;

        int totalLen = dataLen0 + dataLen1;

        // Debug log for first 5 frames
        if (encode_count < 5) {
            kvm_log("[ENCODER] frame %d: dataLen0=%d, dataLen1=%d, totalLen=%d\n",
                   encode_count, dataLen0, dataLen1, totalLen);
        }

        // Send RAW buffer to Unix Socket
        // Format: [4B LE length][H.264 data]
        static int socket_was_closed = 0;
        static int socket_error_shown = 0;
        if (g_video_socket_fd >= 0) {
            if (socket_was_closed) {
                kvm_log("[ENCODER] Socket connected again, resuming video transmission\n");
                socket_was_closed = 0;
                socket_error_shown = 0;
            }

            // Versioned frame envelope:
            // [4B "AIV1"][4B LE payload length][8B LE capture timestamp us].
            uint64_t timestamp_us =
                packet->pts > 0 ? (uint64_t)packet->pts : get_timestamp_us();
            uint8_t frame_header[16] = {
                'A', 'I', 'V', '1',
                (uint8_t)(totalLen & 0xFF),
                (uint8_t)((totalLen >> 8) & 0xFF),
                (uint8_t)((totalLen >> 16) & 0xFF),
                (uint8_t)((totalLen >> 24) & 0xFF),
                (uint8_t)(timestamp_us & 0xFF),
                (uint8_t)((timestamp_us >> 8) & 0xFF),
                (uint8_t)((timestamp_us >> 16) & 0xFF),
                (uint8_t)((timestamp_us >> 24) & 0xFF),
                (uint8_t)((timestamp_us >> 32) & 0xFF),
                (uint8_t)((timestamp_us >> 40) & 0xFF),
                (uint8_t)((timestamp_us >> 48) & 0xFF),
                (uint8_t)((timestamp_us >> 56) & 0xFF),
            };

            if (send_all(g_video_socket_fd,
                         frame_header, sizeof(frame_header)) < 0) {
                kvm_log("[ENCODER] Socket write error: errno=%d\n", errno);
                close(g_video_socket_fd);
                g_video_socket_fd = -1;
                socket_was_closed = 1;
                return 0;
            }

            // Write raw H.264 data part 0
            if (dataLen0 > 0 && data0 != NULL) {
                if (send_all(g_video_socket_fd, data0, (size_t)dataLen0) < 0) {
                    kvm_log("[ENCODER] Socket data write error: errno=%d\n", errno);
                    close(g_video_socket_fd);
                    g_video_socket_fd = -1;
                    socket_was_closed = 1;
                    return 0;
                }
            }

            // Write raw H.264 data part 1
            if (dataLen1 > 0 && data1 != NULL) {
                if (send_all(g_video_socket_fd, data1, (size_t)dataLen1) < 0) {
                    kvm_log("[ENCODER] Socket data1 write error: errno=%d\n", errno);
                    close(g_video_socket_fd);
                    g_video_socket_fd = -1;
                    socket_was_closed = 1;
                    return 0;
                }
            }

            if (encode_count < 5) {
                kvm_log("[ENCODER] Socket OK: sent %d bytes\n", totalLen);
            }
        } else {
            socket_was_closed = 1;
            if (socket_error_shown == 0) {
                kvm_log("[ENCODER] Socket not open, dropping frame\n");
                socket_error_shown = 1;
            }
        }

        encode_count++;
        return 0;
    }
};

static EncoderCallback* g_callback = NULL;

// Signal handler
static void signal_handler(int sig)
{
    kvm_log("\n[INFO] Received signal %d, stopping...\n", sig);
    g_running = false;
    pthread_cond_broadcast(&g_snapshot_cond);

    if (g_ctrl_fd >= 0) {
        shutdown(g_ctrl_fd, SHUT_RDWR);
    }
}

// Connect to luckfox_kvm ctrl socket with retry, connect video Unix Socket
// Note: Socket (video data) is critical - encoder cannot start without it
static int connect_sockets(void)
{
    int retry_count = 0;
    while (retry_count < 120 && g_running) {
        // Connect to ctrl socket
        g_ctrl_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_ctrl_fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, CTRL_SOCKET, sizeof(addr.sun_path) - 1);
            if (connect(g_ctrl_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(g_ctrl_fd);
                g_ctrl_fd = -1;
            }
        }

        // Connect to video Unix Socket - CRITICAL for video transmission
        if (g_video_socket_fd < 0) {
            g_video_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (g_video_socket_fd >= 0) {
                struct sockaddr_un addr;
                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, VIDEO_SOCKET_PATH, sizeof(addr.sun_path) - 1);
                if (connect(g_video_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    close(g_video_socket_fd);
                    g_video_socket_fd = -1;
                }
            }
            if (g_video_socket_fd >= 0) {
                kvm_log("[INFO] Connected video socket: %s (fd=%d)\n", VIDEO_SOCKET_PATH, g_video_socket_fd);
            } else {
                kvm_log("[WARN] Failed to connect video socket: %s (errno=%d), retrying...\n", VIDEO_SOCKET_PATH, errno);
            }
        }

        if (g_ctrl_fd >= 0 && g_video_socket_fd >= 0) {
            kvm_log("[INFO] CONNECTED: ctrl_fd=%d, video_socket_fd=%d\n", g_ctrl_fd, g_video_socket_fd);
            return 0;
        }

        // Close and retry
        if (g_ctrl_fd >= 0) { close(g_ctrl_fd); g_ctrl_fd = -1; }
        if (g_video_socket_fd >= 0) { close(g_video_socket_fd); g_video_socket_fd = -1; }

        kvm_log("[INFO] Waiting for luckfox_kvm... retry %d/120\n", retry_count + 1);
        sleep(2);
        retry_count++;
    }

    kvm_log("[INFO] Socket connections: ctrl=%d, video_socket=%d\n", g_ctrl_fd, g_video_socket_fd);
    return (g_ctrl_fd >= 0 && g_video_socket_fd >= 0) ? 0 : -1;
}

// Send video state to ctrl socket
static void send_video_state(int width, int height, float fps)
{
    if (g_ctrl_fd < 0) return;

    char json[256];
    snprintf(json, sizeof(json),
        "{\"event\":\"video_input_state\",\"data\":{\"ready\":true,\"width\":%d,\"height\":%d,\"fps\":%.1f}}\n",
        width, height, fps);

    send(g_ctrl_fd, json, strlen(json), 0);
    kvm_log("[INFO] Sent video state: %dx%d @ %.1ffps\n", width, height, fps);
}

static void send_video_unavailable(const char* error)
{
    if (g_ctrl_fd < 0) return;
    char json[256];
    snprintf(json, sizeof(json),
             "{\"event\":\"video_input_state\",\"data\":{"
             "\"ready\":false,\"error\":\"%s\","
             "\"width\":0,\"height\":0,\"fps\":0}}\n",
             error ? error : "no_frame");
    send(g_ctrl_fd, json, strlen(json), MSG_NOSIGNAL);
}

static int init_ion_memory(void)
{
    int ret = allocOpen(MEM_TYPE_CDX_NEW, &gIonMem, NULL);
    if (ret < 0) {
        kvm_log("[ERROR] allocOpen failed\n");
        return -1;
    }

    gIonMem.size = CAPTURE_Y_PLANE_SIZE +
                   ALIGN_16B(WIDTH) * ALIGN_16B(HEIGHT) / 2;
    ret = allocAlloc(MEM_TYPE_CDX_NEW, &gIonMem, NULL);
    if (ret < 0) {
        kvm_log("[ERROR] allocAlloc failed\n");
        allocClose(MEM_TYPE_CDX_NEW, &gIonMem, NULL);
        return -1;
    }

    kvm_log("[INFO] Ion memory allocated: vir=0x%lx, phy=0x%lx, "
            "size=%d, chroma_offset=%zu\n",
            gIonMem.vir, gIonMem.phy, gIonMem.size,
            CAPTURE_Y_PLANE_SIZE);
    g_ion_inited = true;
    return 0;
}

static void free_ion_memory(void)
{
    if (!g_ion_inited) return;
    allocFree(MEM_TYPE_CDX_NEW, &gIonMem, NULL);
    allocClose(MEM_TYPE_CDX_NEW, &gIonMem, NULL);
    g_ion_inited = false;
    kvm_log("[INFO] Ion memory freed\n");
}

static int init_display_output(void)
{
    pthread_mutex_lock(&g_display_mutex);
    if (g_display_port) {
        pthread_mutex_unlock(&g_display_mutex);
        return 0;
    }
    if (!g_ion_inited) {
        pthread_mutex_unlock(&g_display_mutex);
        return -1;
    }

    VoutRect rect;
    memset(&rect, 0, sizeof(rect));
    rect.width = WIDTH;
    rect.height = HEIGHT;

    g_display_port = CreateVideoOutport(0);
    if (!g_display_port) {
        pthread_mutex_unlock(&g_display_mutex);
        kvm_log("[DISPLAY] CreateVideoOutport failed\n");
        return -1;
    }
    if (g_display_port->init(g_display_port, 0, 0, &rect) != 0) {
        DestroyVideoOutport(g_display_port);
        g_display_port = NULL;
        pthread_mutex_unlock(&g_display_mutex);
        kvm_log("[DISPLAY] output init failed\n");
        return -1;
    }

    const int screen_width = g_display_port->getScreenWidth(g_display_port);
    const int screen_height = g_display_port->getScreenHeight(g_display_port);
    const float scale_width = (float)screen_width / (float)WIDTH;
    const float scale_height = (float)screen_height / (float)HEIGHT;
    const float scale = scale_width < scale_height ? scale_width : scale_height;
    rect.width = (int)((float)WIDTH * scale);
    rect.height = (int)((float)HEIGHT * scale);
    rect.x = (screen_width - rect.width) / 2;
    rect.y = (screen_height - rect.height) / 2;
    g_display_port->setRect(g_display_port, &rect);
    g_display_port->SetZorder(g_display_port, VIDEO_ZORDER_MIDDLE);

    memset(&g_display_video_param, 0, sizeof(g_display_video_param));
    g_display_video_param.srcInfo.crop_w = WIDTH;
    g_display_video_param.srcInfo.crop_h = HEIGHT;
    g_display_video_param.srcInfo.w = WIDTH;
    g_display_video_param.srcInfo.h = HEIGHT;
    g_display_video_param.srcInfo.format = VIDEO_PIXEL_FORMAT_NV12;
    g_display_video_param.srcInfo.color_space = VIDEO_BT709;

    memset(&g_display_render_buf, 0, sizeof(g_display_render_buf));
    g_display_render_buf.isExtPhy = VIDEO_USE_EXTERN_ION_BUF;
    g_display_render_buf.y_phaddr = gIonMem.phy;
    g_display_render_buf.u_phaddr = gIonMem.phy + VISIBLE_Y_PLANE_SIZE;
    g_display_render_buf.v_phaddr = gIonMem.phy + VISIBLE_Y_PLANE_SIZE;
    g_display_render_buf.fd = gIonMem.ion_buffer.fd_data.aw_fd;
    g_display_enabled.store(false);
    pthread_mutex_unlock(&g_display_mutex);

    kvm_log("[DISPLAY] initialized screen=%dx%d rect=%dx%d+%d+%d\n",
            screen_width, screen_height, rect.width, rect.height, rect.x, rect.y);
    return 0;
}

static void deinit_display_output(void)
{
    pthread_mutex_lock(&g_display_mutex);
    if (g_display_port) {
        g_display_port->setEnable(g_display_port, 0);
        g_display_port->deinit(g_display_port);
        DestroyVideoOutport(g_display_port);
        g_display_port = NULL;
    }
    g_display_enabled.store(false);
    pthread_mutex_unlock(&g_display_mutex);
}

static void set_display_requested(bool enabled)
{
    g_display_requested.store(enabled);
    pthread_mutex_lock(&g_display_mutex);
    if (!enabled && g_display_port && g_display_enabled.load()) {
        g_display_port->setEnable(g_display_port, 0);
        g_display_enabled.store(false);
    }
    pthread_mutex_unlock(&g_display_mutex);
    kvm_log("[DISPLAY] requested=%d\n", enabled ? 1 : 0);
}

static void present_local_frame(void)
{
    if (!g_display_requested.load() || !g_ion_inited) return;
    pthread_mutex_lock(&g_display_mutex);
    if (g_display_port) {
        flushCache(MEM_TYPE_CDX_NEW, &gIonMem, NULL);
        const int queued = g_display_port->queueToDisplay(
            g_display_port,
            VISIBLE_Y_PLANE_SIZE + VISIBLE_UV_PLANE_SIZE,
            &g_display_video_param,
            &g_display_render_buf);
        if (queued == 0 && !g_display_enabled.load()) {
            g_display_port->setEnable(g_display_port, 1);
            g_display_enabled.store(true);
            kvm_log("[DISPLAY] first repaired HDMI frame presented\n");
        } else if (queued != 0) {
            g_display_enabled.store(false);
            kvm_log("[DISPLAY] queue failed: %d\n", queued);
        }
    }
    pthread_mutex_unlock(&g_display_mutex);
}

static void copy_repaired_nv12(unsigned char* destination,
                               const unsigned char* source)
{
    if (!destination || !source) return;

    unsigned char* destination_uv = destination + VISIBLE_Y_PLANE_SIZE;
    const unsigned char* source_uv = source + CAPTURE_Y_PLANE_SIZE;

    // LT6911/VIN marks the frame as 1080 lines but the final eight delivered
    // rows are invalid. Repeating the last valid row hid the green tail, but
    // it also stretched Windows taskbar indicator colours into vertical bars.
    // Resample the 1072 valid rows over the full visible height instead. The
    // 0.75% vertical correction is imperceptible and preserves real content
    // all the way to the bottom edge.
    const int valid_y_height = HEIGHT - INVALID_BOTTOM_ROWS;
    for (int dst_row = 0; dst_row < HEIGHT; dst_row++) {
        const int src_row = dst_row * valid_y_height / HEIGHT;
        memcpy(destination + (size_t)dst_row * WIDTH,
               source + (size_t)src_row * WIDTH, WIDTH);
    }

    const int uv_height = HEIGHT / 2;
    const int valid_uv_height = valid_y_height / 2;
    for (int dst_row = 0; dst_row < uv_height; dst_row++) {
        const int src_row = dst_row * valid_uv_height / uv_height;
        memcpy(destination_uv + (size_t)dst_row * WIDTH,
               source_uv + (size_t)src_row * WIDTH, WIDTH);
    }
}

static int ensure_capture_started(void)
{
    int result = 0;
    const uint64_t now_ms = get_timestamp_ms();
    pthread_t completed_thread = 0;

    pthread_mutex_lock(&g_cap_mutex);
    if (g_capture_started || g_capture_stopping) {
        pthread_mutex_unlock(&g_cap_mutex);
        return 0;
    }
    const uint64_t previous_attempt = g_last_capture_attempt_ms.load();
    if (previous_attempt != 0 && now_ms - previous_attempt < 5000) {
        pthread_mutex_unlock(&g_cap_mutex);
        return 0;
    }
    if (g_cap_thread) {
        completed_thread = g_cap_thread;
        g_cap_thread = 0;
    }
    pthread_mutex_unlock(&g_cap_mutex);

    if (completed_thread) {
        pthread_join(completed_thread, NULL);
    }

    pthread_mutex_lock(&g_cap_mutex);
    if (g_capture_started || g_capture_stopping) {
        pthread_mutex_unlock(&g_cap_mutex);
        return 0;
    }
    g_last_capture_attempt_ms.store(now_ms);
    g_capture_started = true;
    result = pthread_create(&g_cap_thread, NULL, capture_thread, NULL);
    if (result != 0) {
        g_capture_started = false;
        g_cap_thread = 0;
    } else if (previous_attempt != 0) {
        g_recovery_count.fetch_add(1);
    }
    pthread_mutex_unlock(&g_cap_mutex);

    if (result != 0) {
        kvm_log("[CAPTURE] cannot start thread: %s\n", strerror(result));
        return -1;
    }
    kvm_log("[CAPTURE] shared pipeline started\n");
    return 0;
}

// Try to connect to ctrl socket only (for reconnection)
static int connect_ctrl_socket(void)
{
    if (g_ctrl_fd >= 0) {
        return 0;
    }

    g_ctrl_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_ctrl_fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CTRL_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(g_ctrl_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_ctrl_fd);
        g_ctrl_fd = -1;
        return -1;
    }

    kvm_log("[CTRL] Reconnected to ctrl socket\n");
    return 0;
}

// Simple JSON parsing for control commands
static int parse_action(const char* json_str, char* action, int max_len)
{
    const char* p = strstr(json_str, "\"action\"");
    if (!p) return -1;

    p = strchr(p, ':');
    if (!p) return -1;
    p++;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\"') p++;

    int i = 0;
    while (*p && *p != '\"' && i < max_len - 1) {
        action[i++] = *p++;
    }
    action[i] = '\0';

    return 0;
}

// Control command processing thread
static void* control_thread(void* arg)
{
    (void)arg;
    char buffer[512];

    kvm_log("[CTRL] Control thread started, waiting for commands...\n");

    while (g_running) {
        // Try to reconnect ctrl socket if disconnected
        if (g_ctrl_fd < 0) {
            if (connect_ctrl_socket() < 0) {
                sleep(1);
                continue;
            }
        }

        // Try to reconnect socket if disconnected (important!)
        if (g_video_socket_fd < 0) {
            g_video_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (g_video_socket_fd >= 0) {
                struct sockaddr_un addr;
                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, VIDEO_SOCKET_PATH, sizeof(addr.sun_path) - 1);
                if (connect(g_video_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    close(g_video_socket_fd);
                    g_video_socket_fd = -1;
                }
            }
            if (g_video_socket_fd >= 0) {
                kvm_log("[CTRL] Socket reopened: fd=%d\n", g_video_socket_fd);
            }
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_ctrl_fd, &fds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(g_ctrl_fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        int n = read(g_ctrl_fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            kvm_log("[CTRL] Ctrl socket closed, reconnecting...\n");
            close(g_ctrl_fd);
            g_ctrl_fd = -1;
            continue;
        }

        buffer[n] = '\0';
        kvm_log("[CTRL] Received command: %s\n", buffer);

        char action[64];
        if (parse_action(buffer, action, sizeof(action)) == 0) {
            bool ensure_after_unlock = false;
            pthread_mutex_lock(&g_cap_mutex);

            if (strcmp(action, "start_video") == 0) {
                kvm_log("[CTRL] CMD: start_video received\n");
                g_stream_requested.store(true);
                g_force_keyframe.store(true);

                // If we are currently stopping, we must wait for it to finish completely
                // before trying to start again. We check this while holding the mutex.
                while (g_capture_stopping) {
                    kvm_log("[CTRL] Capture is still stopping, waiting for it to finish...\n");
                    // Release mutex, wait a bit, then re-acquire and check again
                    pthread_mutex_unlock(&g_cap_mutex);
                    usleep(200000); // 200ms
                    pthread_mutex_lock(&g_cap_mutex);
                }

                // Now we are guaranteed that any previous stop_video has completely finished
                // (including the 1-second V4L2 release delay).

                // Re-connect socket if it was closed
                if (g_video_socket_fd < 0) {
                    kvm_log("[CTRL] Re-connecting video socket...\n");
                    g_video_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                    if (g_video_socket_fd >= 0) {
                        struct sockaddr_un addr;
                        memset(&addr, 0, sizeof(addr));
                        addr.sun_family = AF_UNIX;
                        strncpy(addr.sun_path, VIDEO_SOCKET_PATH, sizeof(addr.sun_path) - 1);
                        if (connect(g_video_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                            close(g_video_socket_fd);
                            g_video_socket_fd = -1;
                            kvm_log("[WARN] Failed to reconnect video socket: %s (errno=%d)\n", VIDEO_SOCKET_PATH, errno);
                        }
                    }
                }

                if (!g_capture_started.load()) {
                    ensure_after_unlock = true;
                } else {
                    kvm_log("[CTRL] Capture already running\n");
                }
            } else if (strcmp(action, "stop_video") == 0) {
                kvm_log("[CTRL] CMD: stop_video received\n");
                g_stream_requested.store(false);
                if (g_capture_started && !g_display_requested.load()) {
                    kvm_log("[CTRL] Stopping capture...\n");
                    g_capture_stopping = true;
                    g_capture_started = false;

                    if (g_cap_thread) {
                        kvm_log("[CTRL] Waiting for capture thread to join...\n");
                        pthread_mutex_unlock(&g_cap_mutex);
                        pthread_join(g_cap_thread, NULL);
                        pthread_mutex_lock(&g_cap_mutex);
                        g_cap_thread = 0;
                        kvm_log("[CTRL] Capture thread joined\n");
                    }
                    // Wait a bit to ensure V4L2 device is completely released by kernel
                    // Moving sleep OUTSIDE the mutex to avoid blocking start_video
                    kvm_log("[CTRL] Sleeping 1s outside mutex to release V4L2...\n");
                    pthread_mutex_unlock(&g_cap_mutex);
                    sleep(1);
                    pthread_mutex_lock(&g_cap_mutex);

                    kvm_log("[CTRL] Sleep 1s done, V4L2 should be released now\n");
                    g_last_capture_stop_time = time(NULL);
                    g_capture_stopping = false;
                    kvm_log("[CTRL] Capture stopped\n");
                } else if (g_capture_started) {
                    kvm_log("[CTRL] Keeping shared capture for local display\n");
                } else {
                    kvm_log("[CTRL] Capture not running\n");
                }

                // Keep the media socket connected. A later WebRTC session
                // requests a fresh IDR without disturbing local display.
            } else {
                kvm_log("[CTRL] Unknown action: %s\n", action);
            }

            pthread_mutex_unlock(&g_cap_mutex);
            if (ensure_after_unlock) {
                (void)ensure_capture_started();
            }
        }
    }

    kvm_log("[CTRL] Control thread exiting\n");
    return NULL;
}

// Init encoder
static int init_encoder(void)
{
    EncodeParam encParam;
    memset(&encParam, 0, sizeof(EncodeParam));

    encParam.codecType = CODEC_H264;
    encParam.pixelFormat = PIXEL_YUV420SP;
    encParam.srcW = WIDTH;
    encParam.srcH = HEIGHT;
    encParam.dstW = WIDTH;
    encParam.dstH = HEIGHT;
    encParam.rotation = Angle_0;
    encParam.bitRate = BITRATE * 1000;
    encParam.frameRate = FPS;
    encParam.maxKeyFrame = FPS;  // IDR every 1 second
    encParam.frameCount = MULTI_FRAMES;
    encParam.rcMode = VBR;
    encParam.minQp = 20;
    encParam.maxQp = 28;

    g_encoder = AWVideoEncoder::create();
    if (!g_encoder) {
        kvm_log("[ERROR] Failed to create encoder\n");
        return -1;
    }

    g_callback = new EncoderCallback();

    int ret = g_encoder->init(&encParam, g_callback);
    if (ret != 0) {
        kvm_log("[ERROR] Failed to init encoder: %d\n", ret);
        AWVideoEncoder::destroy(g_encoder);
        g_encoder = NULL;
        return -1;
    }

    kvm_log("[INFO] Encoder initialized: %dx%d @ %dfps, bitrate=%dkbps\n",
           WIDTH, HEIGHT, FPS, BITRATE);
    kvm_log("[INFO] Encoder config: maxKeyFrame=%d, frameCount=%d, rcMode=%d\n",
           encParam.maxKeyFrame, MULTI_FRAMES, VBR);

    return 0;
}

// Video capture thread
static void* capture_thread(void* arg)
{
    (void)arg;
    kvm_log("[INFO] Capture thread started\n");

    struct v4l2_format fmt;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;
    struct v4l2_plane planes[BUFFER_COUNT];
    enum v4l2_buf_type type;

    // Open device
    g_video_fd = open(VIDEO_DEV, O_RDWR | O_NONBLOCK);
    if (g_video_fd < 0) {
        kvm_log("[ERROR] Cannot open %s: %s\n", VIDEO_DEV, strerror(errno));
        send_video_unavailable(errno == EBUSY ? "busy" : "open_failed");
        // Reset capture state so it can be retried later
        g_capture_started = false;
        return NULL;
    }

    // Set input
    struct v4l2_input input;
    input.index = 0;
    if (ioctl(g_video_fd, VIDIOC_S_INPUT, &input) < 0) {
        kvm_log("[ERROR] Cannot set input: %s\n", strerror(errno));
        send_video_unavailable("input_failed");
        close(g_video_fd);
        g_video_fd = -1;
        g_capture_started = false;
        return NULL;
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
    ioctl(g_video_fd, VIDIOC_G_FMT, &fmt);
    kvm_log("[INFO] Format set: %dx%d\n", fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);

    // Request MMAP buffers
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(g_video_fd, VIDIOC_REQBUFS, &req) < 0) {
        kvm_log("[ERROR] Cannot request buffers: %s\n", strerror(errno));
        send_video_unavailable("buffer_failed");
        close(g_video_fd);
        g_video_fd = -1;
        g_capture_started = false;
        return NULL;
    }

    g_buffer_count = req.count;
    g_buffers = (buffer_t*)calloc(g_buffer_count, sizeof(buffer_t));

    kvm_log("[INFO] Allocated %d buffers (MMAP mode)\n", g_buffer_count);

    // Map buffers
    for (int i = 0; i < g_buffer_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        memset(&planes[0], 0, sizeof(planes[0]));
        buf.m.planes = &planes[0];

        if (ioctl(g_video_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            kvm_log("[ERROR] Cannot query buffer %d\n", i);
            continue;
        }

        g_buffers[i].start[0] = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, g_video_fd, planes[0].m.mem_offset);
        if (g_buffers[i].start[0] == MAP_FAILED) {
            kvm_log("[ERROR] mmap failed for buffer %d\n", i);
            g_buffers[i].start[0] = NULL;
            continue;
        }

        g_buffers[i].length[0] = planes[0].length;
        kvm_log("[INFO] Buffer %d mapped: %d bytes\n", i, planes[0].length);
        if ((size_t)planes[0].length < SNAPSHOT_FRAME_SIZE) {
            kvm_log("[ERROR] Buffer %d is too small for padded NV12: "
                    "%d < %zu bytes\n",
                    i, planes[0].length, SNAPSHOT_FRAME_SIZE);
            munmap(g_buffers[i].start[0], g_buffers[i].length[0]);
            g_buffers[i].start[0] = NULL;
            continue;
        }

        // QBUF
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = &planes[0];

        if (ioctl(g_video_fd, VIDIOC_QBUF, &buf) < 0) {
            kvm_log("[ERROR] Cannot queue buffer %d\n", i);
        }
    }

    // Start streaming
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(g_video_fd, VIDIOC_STREAMON, &type) < 0) {
        kvm_log("[ERROR] Cannot start streaming: %s\n", strerror(errno));
        send_video_unavailable("stream_failed");
        for (int i = 0; i < g_buffer_count; i++) {
            if (g_buffers[i].start[0]) {
                munmap(g_buffers[i].start[0], g_buffers[i].length[0]);
            }
        }
        free(g_buffers);
        g_buffers = NULL;
        g_buffer_count = 0;
        close(g_video_fd);
        g_video_fd = -1;
        g_capture_started = false;
        return NULL;
    }

    kvm_log("[INFO] Streaming started\n");

    // Wait for encoder to produce first IDR frame
    kvm_log("[INFO] Waiting for encoder to initialize...\n");
    sleep(2);
    kvm_log("[INFO] Starting capture loop\n");

    // Capture loop
    fd_set fds;
    struct timeval tv;
    int frame_count = 0;
    int timeout_count = 0;
    // Removed max_frames limit to allow continuous streaming
    // int max_frames = 10000;

    while (g_running && g_capture_started) {
        FD_ZERO(&fds);
        FD_SET(g_video_fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(g_video_fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) {
            timeout_count++;
            if (timeout_count >= 3) {
                kvm_log("[WARN] Capture stalled for %d consecutive timeouts\n",
                        timeout_count);
                break;
            }
            continue;
        }
        timeout_count = 0;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = 1;
        memset(&planes[0], 0, sizeof(planes[0]));
        buf.m.planes = &planes[0];

        if (ioctl(g_video_fd, VIDIOC_DQBUF, &buf) < 0) {
            kvm_log("[ERROR] DQBUF failed\n");
            break;
        }

        if (buf.index < (unsigned int)g_buffer_count &&
            g_buffers[buf.index].start[0] && g_ion_inited) {
            copy_repaired_nv12(
                (unsigned char*)gIonMem.vir,
                (const unsigned char*)g_buffers[buf.index].start[0]);

            g_frame_count.fetch_add(1);
            g_last_frame_ms.store(get_timestamp_ms());
            if (frame_count == 0) {
                send_video_state(WIDTH, HEIGHT, (float)FPS);
            }

            // WebRTC is the latency-sensitive branch. Submit the repaired
            // frame to the hardware encoder before cache maintenance and
            // display ioctls for the local preview. The encoder wrapper
            // copies the planes synchronously, so the shared source buffer
            // remains safe for the other consumers below.
            if (g_encoder && g_stream_requested.load()) {
                AVPacket packet;
                memset(&packet, 0, sizeof(AVPacket));

                // The patched SDK wrapper copies these planes into its own
                // macroblock-aligned, cache-flushed VE buffers.
                packet.pAddrVir0 = (unsigned char*)gIonMem.vir;
                packet.dataLen0 = VISIBLE_Y_PLANE_SIZE;

                packet.pAddrVir1 =
                    (unsigned char*)gIonMem.vir + VISIBLE_Y_PLANE_SIZE;
                packet.dataLen1 = VISIBLE_UV_PLANE_SIZE;

                packet.pts = (int64_t)get_timestamp_us();
                packet.id = frame_count;
                packet.isKeyframe = frame_count == 0 ||
                                    g_force_keyframe.exchange(false);

                int ret = g_encoder->encode(&packet);
                if (ret < 0) {
                    g_force_keyframe.store(true);
                    kvm_log("[WARN] Encode failed: %d\n", ret);
                }
            }

            // Local display and MCP snapshots consume the same repaired
            // frame after encoder submission. No second process opens
            // /dev/video0, and local presentation can no longer delay this
            // frame's entry into the IPKVM network path.
            queue_snapshot_frame((const unsigned char*)gIonMem.vir);
            present_local_frame();
        }

        // Re-queue buffer
        if (ioctl(g_video_fd, VIDIOC_QBUF, &buf) < 0) {
            kvm_log("[ERROR] QBUF failed\n");
        }

        frame_count++;
    }

    kvm_log("[INFO] Capture thread stopped, captured %d frames\n", frame_count);
    if (g_running) {
        send_video_unavailable(frame_count == 0 ? "no_frame" : "stalled");
    }

    // --- MUST CLEANUP HARDWARE RESOURCES HERE ---
    // Stop streaming
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(g_video_fd, VIDIOC_STREAMOFF, &type) < 0) {
        kvm_log("[WARN] VIDIOC_STREAMOFF failed: %s\n", strerror(errno));
    }

    // Unmap buffers
    for (int i = 0; i < g_buffer_count; i++) {
        if (g_buffers[i].start[0]) {
            if (munmap(g_buffers[i].start[0], g_buffers[i].length[0]) < 0) {
                kvm_log("[WARN] munmap failed for buffer %d\n", i);
            }
            g_buffers[i].start[0] = NULL;
        }
    }

    // Close device
    if (g_video_fd >= 0) {
        kvm_log("[INFO] Closing video device fd=%d\n", g_video_fd);
        close(g_video_fd);
        g_video_fd = -1;
    }
    if (g_buffers) {
        free(g_buffers);
        g_buffers = NULL;
        g_buffer_count = 0;
    }
    pthread_mutex_lock(&g_cap_mutex);
    g_capture_started = false;
    pthread_mutex_unlock(&g_cap_mutex);
    pthread_mutex_lock(&g_display_mutex);
    if (g_display_port && g_display_enabled.load()) {
        g_display_port->setEnable(g_display_port, 0);
        g_display_enabled.store(false);
    }
    pthread_mutex_unlock(&g_display_mutex);
    // ---------------------------------------------

    return NULL;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // Disable stdout buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    kvm_log("===========================================\n");
    kvm_log("  AITVBox A133 IPKVM Video v%s\n", VIDEO_VERSION);
    kvm_log("  Resolution: %dx%d @ %dfps\n", WIDTH, HEIGHT, FPS);
    kvm_log("  Bitrate: %dkbps\n", BITRATE);
    kvm_log("  Ctrl Socket: %s\n", CTRL_SOCKET);
    kvm_log("  Video Socket: %s\n", VIDEO_SOCKET_PATH);
    kvm_log("===========================================\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    kvm_log("[MAIN] Connecting to aitvbox-ipkvmd...\n");

    // CRITICAL: Must connect to luckfox_kvm BEFORE initializing encoder
    // because encoder init produces frames immediately
    int connect_retry = 0;
    while (connect_retry < 120) {
        // Connect to ctrl socket
        g_ctrl_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_ctrl_fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, CTRL_SOCKET, sizeof(addr.sun_path) - 1);
            if (connect(g_ctrl_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(g_ctrl_fd);
                g_ctrl_fd = -1;
            }
        }

        // Connect to video Unix Socket - MUST succeed before encoder init
        if (g_video_socket_fd < 0) {
            g_video_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (g_video_socket_fd >= 0) {
                struct sockaddr_un addr;
                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, VIDEO_SOCKET_PATH, sizeof(addr.sun_path) - 1);
                if (connect(g_video_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    close(g_video_socket_fd);
                    g_video_socket_fd = -1;
                }
            }
            if (g_video_socket_fd >= 0) {
                kvm_log("[INFO] Connected video socket: %s (fd=%d)\n", VIDEO_SOCKET_PATH, g_video_socket_fd);
            } else {
                kvm_log("[WARN] Failed to connect video socket: %s (errno=%d)\n", VIDEO_SOCKET_PATH, errno);
            }
        }

        if (g_ctrl_fd >= 0 && g_video_socket_fd >= 0) {
            kvm_log("[INFO] CONNECTED: ctrl_fd=%d, video_socket_fd=%d\n", g_ctrl_fd, g_video_socket_fd);
            break;
        }

        // Close and retry
        if (g_ctrl_fd >= 0) { close(g_ctrl_fd); g_ctrl_fd = -1; }
        if (g_video_socket_fd >= 0) { close(g_video_socket_fd); g_video_socket_fd = -1; }

        kvm_log("[MAIN] Waiting for luckfox_kvm... retry %d/120\n", connect_retry + 1);
        sleep(2);
        connect_retry++;
    }

    if (g_ctrl_fd < 0 || g_video_socket_fd < 0) {
        kvm_log("[ERROR] Failed to connect to luckfox_kvm after 120 retries\n");
        kvm_log("[ERROR] ctrl_fd=%d, video_socket_fd=%d\n", g_ctrl_fd, g_video_socket_fd);
        // Continue anyway - encoder will produce frames, we just can't send them
    }

    if (init_ion_memory() < 0) {
        return -1;
    }
    if (init_display_output() < 0) {
        free_ion_memory();
        return -1;
    }

    // Init encoder - only after socket is ready
    kvm_log("[MAIN] Initializing encoder...\n");
    if (init_encoder() < 0) {
        deinit_display_output();
        free_ion_memory();
        return -1;
    }

    kvm_log("[MAIN] Ready, waiting for control commands...\n");

    // Wait for ctrl socket (should already be connected)
    int wait_count = 0;
    while (g_ctrl_fd < 0 && wait_count < 30) {
        kvm_log("[MAIN] Waiting for ctrl socket... %d/30\n", wait_count + 1);
        sleep(1);
        wait_count++;
    }

    if (g_ctrl_fd < 0) {
        kvm_log("[MAIN] Ctrl socket not ready, socket not connected...\n");
    } else {
        kvm_log("[MAIN] Ctrl socket ready, waiting for start_video command...\n");
    }

    // Start running state
    g_running = true;

    g_snapshot_frame =
        (unsigned char*)malloc(SNAPSHOT_FRAME_SIZE);
    if (!g_snapshot_frame) {
        kvm_log("[ERROR] Cannot allocate MCP snapshot frame buffer\n");
        g_running = false;
    } else {
        int snapshot_error = pthread_create(
            &g_snapshot_encode_thread, NULL, snapshot_encode_thread, NULL);
        if (snapshot_error == 0) {
            snapshot_error = pthread_create(
                &g_snapshot_server_thread, NULL, snapshot_server_thread, NULL);
        }
        if (snapshot_error != 0) {
            kvm_log("[ERROR] Cannot start MCP snapshot service: %s\n",
                    strerror(snapshot_error));
            g_running = false;
            pthread_cond_broadcast(&g_snapshot_cond);
        } else {
            kvm_log("[MAIN] MCP snapshot side channel ready at %s\n",
                    CAPTURE_SOCKET);
        }
    }

    // Start control command processing thread
    pthread_t ctrl_thread;
    if (g_running) {
        pthread_create(&ctrl_thread, NULL, control_thread, NULL);

        // Local display is a first-class consumer. Start the single capture
        // pipeline without waiting for a browser session.
        (void)ensure_capture_started();

        // Wait for control thread
        pthread_join(ctrl_thread, NULL);
    }

    g_running = false;
    pthread_cond_broadcast(&g_snapshot_cond);
    if (g_snapshot_server_thread) {
        pthread_join(g_snapshot_server_thread, NULL);
        g_snapshot_server_thread = 0;
    }
    if (g_snapshot_encode_thread) {
        pthread_join(g_snapshot_encode_thread, NULL);
        g_snapshot_encode_thread = 0;
    }
    unlink(CAPTURE_SOCKET);

    // Stop capture if still running
    pthread_mutex_lock(&g_cap_mutex);
    g_capture_started = false;
    pthread_t final_capture_thread = g_cap_thread;
    g_cap_thread = 0;
    pthread_mutex_unlock(&g_cap_mutex);
    if (final_capture_thread) {
        pthread_join(final_capture_thread, NULL);
    }

    // Cleanup
    if (g_encoder) {
        AWVideoEncoder::destroy(g_encoder);
        g_encoder = NULL;
    }

    if (g_callback) {
        delete g_callback;
        g_callback = NULL;
    }

    deinit_display_output();
    free_ion_memory();

    // Free buffers
    if (g_buffers) {
        for (int i = 0; i < g_buffer_count; i++) {
            if (g_buffers[i].start[0]) {
                munmap(g_buffers[i].start[0], g_buffers[i].length[0]);
            }
        }
        free(g_buffers);
    }

    // Close sockets
    if (g_ctrl_fd >= 0) close(g_ctrl_fd);
    if (g_video_socket_fd >= 0) close(g_video_socket_fd);
    free(g_snapshot_frame);
    g_snapshot_frame = NULL;

    kvm_log("[INFO] Demo finished\n");
    return 0;
}
