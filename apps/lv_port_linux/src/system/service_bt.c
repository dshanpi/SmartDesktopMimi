/**
 * @file service_bt.c
 * @brief Bluetooth Backend Service - A2DP Sink (蓝牙音箱模式)
 *
 * 封装 btmanager API，将 btmanager 回调转为 IPC 消息发送到 UI 进程。
 * 蓝牙音箱模式：设备被动可发现，手机扫描连接，音频经 bluez-alsa 路由到 ALSA 播放。
 */

#include "service_bt.h"
#include "service_ai.h"
#include "bt_pcm_player.h"
#include "bluez_player_monitor.h"
#include "device_identity.h"
#include "settings.h"
#include "log/app_log.h"
#include "../middleware/middleware.h"
#include "backend_types.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bt_manager.h>

/* libbtmg 4.0 导出但未写入公共头文件。打开后才会投递已经解码的
 * S16_LE PCM；A133 预编译版的内部播放线程会打开 codec 却不写帧，
 * 因此由 bt_pcm_player 明确消费该回调并写入共享 PlaybackDmix。 */
extern void bt_a2dp_sink_stream_cb_enable(bool enable);

#define BT_PROGRESS_UPDATE_INTERVAL_MS 500
#define BT_INIT_RETRY_INTERVAL_MS 1000
#define BT_INIT_RETRY_MAX_ATTEMPTS 90
#define BT_STREAM_IDLE_PAUSE_MS 280
#define BT_METADATA_CLEAR_DELAY_MS 1200

/* =========================================================================
 *                            内部状态
 * ========================================================================= */

static struct {
    bool    initialized;

    /* 适配器状态 */
    bool    enabled;
    bool    adapter_powered;
    bool    discoverable;
    char    adapter_name[64];
    char    adapter_addr[18];

    /* A2DP Sink 连接状态 */
    bool    a2dp_connected;
    bool    audio_streaming;
    char    connected_device_name[128];
    char    connected_device_addr[18];

    /* 音量 */
    int     volume;        /* 0-100 */

    /* AVRCP */
    char    track_title[256];
    char    track_artist[256];
    char    track_album[256];
    char    track_genre[128];
    char    track_duration[64];
    int     track_len_ms;
    int     track_pos_ms;
    bool    track_is_playing;
    bool    player_available;
    bool    pause_inferred_from_stream;
    bt_playback_state_t playback_state;
    int64_t metadata_clear_deadline_ms;
    int64_t track_progress_clock_ms;
    int64_t track_progress_publish_ms;
} bt_state;

static pthread_mutex_t bt_state_lock = PTHREAD_MUTEX_INITIALIZER;
static btmg_callback_t *bt_callback = NULL;
static bool bt_init_deferred = false;
static int bt_init_retry_count = 0;
static int64_t bt_init_retry_next_ms = 0;
static atomic_llong bt_last_stream_packet_ms;

static void copy_text(char *dst, size_t dst_size, const char *src);

static int64_t monotonic_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool advance_track_position_locked(int64_t now_ms) {
    int64_t elapsed_ms;
    int64_t next_pos_ms;
    bool playing = bt_state.track_is_playing;

    if (!playing || bt_state.track_len_ms <= 0) {
        bt_state.track_progress_clock_ms = 0;
        return false;
    }

    if (bt_state.track_progress_clock_ms <= 0 || now_ms <= bt_state.track_progress_clock_ms) {
        bt_state.track_progress_clock_ms = now_ms;
        return false;
    }

    elapsed_ms = now_ms - bt_state.track_progress_clock_ms;
    bt_state.track_progress_clock_ms = now_ms;
    next_pos_ms = (int64_t)bt_state.track_pos_ms + elapsed_ms;
    if (next_pos_ms > bt_state.track_len_ms) {
        next_pos_ms = bt_state.track_len_ms;
    }
    if (next_pos_ms == bt_state.track_pos_ms) return false;

    bt_state.track_pos_ms = (int)next_pos_ms;
    return true;
}

/* Build default broadcast name: product name + last 2 MAC bytes, e.g. AI-DeskTopBox-A1B2 */
static void build_auto_adapter_name(char *out, size_t out_size, const char *adapter_addr)
{
    char hex[13] = {0};
    char suffix[8] = {0};
    const char *p;
    int hi = 0;

    if (!out || out_size == 0) return;

    if (adapter_addr && adapter_addr[0]) {
        for (p = adapter_addr; *p && hi < 12; p++) {
            if (isxdigit((unsigned char)*p)) {
                hex[hi++] = (char)toupper((unsigned char)*p);
            }
        }
        if (hi >= 4) {
            suffix[0] = hex[hi - 4];
            suffix[1] = hex[hi - 3];
            suffix[2] = hex[hi - 2];
            suffix[3] = hex[hi - 1];
            suffix[4] = '\0';
        }
    }

    if (suffix[0]) {
        snprintf(out, out_size, "%s-%s", APP_DEVICE_NAME, suffix);
    } else {
        snprintf(out, out_size, "%s", APP_DEVICE_NAME);
    }
}

static void desired_adapter_name(char *out, size_t out_size, const char *adapter_addr)
{
    sys_settings_t *settings = sys_settings_get();

    if (!out || out_size == 0) return;

    if (settings && settings->bluetooth_name[0]) {
        snprintf(out, out_size, "%s", settings->bluetooth_name);
        return;
    }

    build_auto_adapter_name(out, out_size, adapter_addr);
}

static void refresh_adapter_identity(void) {
    char adapter_addr[sizeof(bt_state.adapter_addr)] = {0};
    char adapter_name[sizeof(bt_state.adapter_name)] = {0};
    char desired[64] = {0};

    bt_manager_get_adapter_address(adapter_addr);
    desired_adapter_name(desired, sizeof(desired), adapter_addr);
    if (desired[0]) {
        bt_manager_set_adapter_name(desired);
    }
    bt_manager_get_adapter_name(adapter_name);
    if (adapter_name[0] == '\0' && desired[0]) {
        copy_text(adapter_name, sizeof(adapter_name), desired);
    }

    pthread_mutex_lock(&bt_state_lock);
    copy_text(bt_state.adapter_addr, sizeof(bt_state.adapter_addr), adapter_addr);
    copy_text(bt_state.adapter_name, sizeof(bt_state.adapter_name), adapter_name);
    pthread_mutex_unlock(&bt_state_lock);
}

static bool hci0_is_up(void) {
    struct hci_dev_info di;
    int dev_id = hci_devid("hci0");

    if (dev_id < 0) return false;
    memset(&di, 0, sizeof(di));
    if (hci_devinfo(dev_id, &di) < 0) return false;

    return hci_test_bit(HCI_UP, &di.flags) != 0;
}

static size_t utf8_char_len(const char *s) {
    unsigned char c = (unsigned char)s[0];

    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF &&
        ((unsigned char)s[1] & 0xC0) == 0x80) {
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF &&
        ((unsigned char)s[1] & 0xC0) == 0x80 &&
        ((unsigned char)s[2] & 0xC0) == 0x80) {
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4 &&
        ((unsigned char)s[1] & 0xC0) == 0x80 &&
        ((unsigned char)s[2] & 0xC0) == 0x80 &&
        ((unsigned char)s[3] & 0xC0) == 0x80) {
        return 4;
    }
    return 1;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
    size_t in = 0;
    size_t out = 0;

    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[in] != '\0') {
        size_t len = utf8_char_len(&src[in]);
        if (out + len >= dst_size) break;
        memcpy(&dst[out], &src[in], len);
        in += len;
        out += len;
    }
    dst[out] = '\0';
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

static void copy_metadata_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (metadata_text_is_placeholder(src)) {
        dst[0] = '\0';
        return;
    }
    copy_text(dst, dst_size, src);
}

/* =========================================================================
 *                         IPC 发布辅助函数
 * ========================================================================= */

static void send_bt_runtime(void) {
    bt_runtime_t rt;
    memset(&rt, 0, sizeof(rt));
    pthread_mutex_lock(&bt_state_lock);
    rt.enabled      = bt_state.enabled;
    rt.discoverable = bt_state.discoverable;
    memcpy(rt.adapter_name, bt_state.adapter_name, sizeof(rt.adapter_name));
    memcpy(rt.adapter_addr, bt_state.adapter_addr, sizeof(rt.adapter_addr));
    rt.a2dp_connected = bt_state.a2dp_connected;
    rt.audio_streaming = bt_state.audio_streaming;
    memcpy(rt.connected_device_name, bt_state.connected_device_name, sizeof(rt.connected_device_name));
    memcpy(rt.connected_device_addr, bt_state.connected_device_addr, sizeof(rt.connected_device_addr));
    rt.volume = bt_state.volume;
    pthread_mutex_unlock(&bt_state_lock);

    mw_publish(TOPIC_BT_RUNTIME, &rt, sizeof(rt), MW_DIR_BACKEND_TO_UI);
}

static void send_bt_avrcp_info(void) {
    bt_avrcp_info_t info;
    memset(&info, 0, sizeof(info));
    pthread_mutex_lock(&bt_state_lock);
    copy_text(info.title,    sizeof(info.title),    bt_state.track_title);
    copy_text(info.artist,   sizeof(info.artist),   bt_state.track_artist);
    copy_text(info.album,    sizeof(info.album),    bt_state.track_album);
    copy_text(info.duration, sizeof(info.duration), bt_state.track_duration);
    copy_text(info.genre,    sizeof(info.genre),    bt_state.track_genre);
    info.song_len_ms = bt_state.track_len_ms;
    info.song_pos_ms = bt_state.track_pos_ms;
    info.is_playing = bt_state.track_is_playing;
    info.player_available = bt_state.player_available;
    info.playback_state = bt_state.playback_state;
    pthread_mutex_unlock(&bt_state_lock);

    mw_publish(TOPIC_BT_AVRCP_INFO, &info, sizeof(info), MW_DIR_BACKEND_TO_UI);
}

static void clear_track_metadata_locked(void) {
    memset(bt_state.track_title, 0, sizeof(bt_state.track_title));
    memset(bt_state.track_artist, 0, sizeof(bt_state.track_artist));
    memset(bt_state.track_album, 0, sizeof(bt_state.track_album));
    memset(bt_state.track_genre, 0, sizeof(bt_state.track_genre));
    memset(bt_state.track_duration, 0, sizeof(bt_state.track_duration));
    bt_state.track_len_ms = 0;
    bt_state.track_pos_ms = 0;
}

static void clear_track_info(void) {
    clear_track_metadata_locked();
    bt_state.track_is_playing = false;
    bt_state.player_available = false;
    bt_state.pause_inferred_from_stream = false;
    bt_state.playback_state = BT_PLAYBACK_UNKNOWN;
    bt_state.metadata_clear_deadline_ms = 0;
    bt_state.track_progress_clock_ms = 0;
    bt_state.track_progress_publish_ms = 0;
    atomic_store_explicit(&bt_last_stream_packet_ms, 0, memory_order_relaxed);
}

static void clear_connection_info(void) {
    bt_state.a2dp_connected = false;
    bt_state.audio_streaming = false;
    memset(bt_state.connected_device_name, 0, sizeof(bt_state.connected_device_name));
    memset(bt_state.connected_device_addr, 0, sizeof(bt_state.connected_device_addr));
    clear_track_info();
}

static void on_bluez_player_state(bool available,
                                  bt_playback_state_t state,
                                  const char *object_path) {
    int64_t now = monotonic_ms();
    bool changed;

    pthread_mutex_lock(&bt_state_lock);
    bool old_available = bt_state.player_available;
    bool old_playing = bt_state.track_is_playing;
    bt_playback_state_t old_state = bt_state.playback_state;

    if (!available) {
        advance_track_position_locked(now);
        clear_track_metadata_locked();
        bt_state.player_available = false;
        bt_state.track_is_playing = false;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_UNKNOWN;
        bt_state.metadata_clear_deadline_ms = 0;
        bt_state.track_progress_clock_ms = 0;
    } else if (state == BT_PLAYBACK_PLAYING) {
        bt_state.player_available = true;
        bt_state.track_is_playing = true;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_PLAYING;
        bt_state.metadata_clear_deadline_ms = 0;
        bt_state.track_progress_clock_ms = now;
        bt_state.track_progress_publish_ms = 0;
    } else if (state == BT_PLAYBACK_PAUSED) {
        advance_track_position_locked(now);
        bt_state.player_available = true;
        bt_state.track_is_playing = false;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_PAUSED;
        bt_state.track_progress_clock_ms = 0;
    } else if (state == BT_PLAYBACK_STOPPED) {
        advance_track_position_locked(now);
        bt_state.player_available = true;
        bt_state.track_is_playing = false;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_STOPPED;
        bt_state.metadata_clear_deadline_ms = now + BT_METADATA_CLEAR_DELAY_MS;
        bt_state.track_progress_clock_ms = 0;
    }

    changed = old_available != bt_state.player_available ||
              old_playing != bt_state.track_is_playing ||
              old_state != bt_state.playback_state;
    pthread_mutex_unlock(&bt_state_lock);

    if (changed) {
        APP_LOGI("bt", "BlueZ player state: available=%d state=%d path=%s",
                 available, state, object_path ? object_path : "unknown");
        send_bt_avrcp_info();
    }
}

static void make_discoverable(void) {
    bool adapter_powered;

    pthread_mutex_lock(&bt_state_lock);
    adapter_powered = bt_state.adapter_powered;
    pthread_mutex_unlock(&bt_state_lock);
    if (!adapter_powered) return;

    bt_manager_agent_set_io_capability(BTMG_IO_CAP_NOINPUTNOOUTPUT);
    bt_manager_set_scan_mode(BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
    pthread_mutex_lock(&bt_state_lock);
    bt_state.discoverable = true;
    pthread_mutex_unlock(&bt_state_lock);
}

static void make_unavailable(void) {
    bool adapter_powered;

    pthread_mutex_lock(&bt_state_lock);
    adapter_powered = bt_state.adapter_powered;
    pthread_mutex_unlock(&bt_state_lock);
    if (!adapter_powered) return;

    bt_manager_set_scan_mode(BTMG_SCAN_MODE_NONE);
    pthread_mutex_lock(&bt_state_lock);
    bt_state.discoverable = false;
    pthread_mutex_unlock(&bt_state_lock);
}

static void soft_disable_bt_service(void) {
    char connected_addr[sizeof(bt_state.connected_device_addr)];
    bool should_disconnect;
    bool adapter_powered;

    pthread_mutex_lock(&bt_state_lock);
    should_disconnect = bt_state.a2dp_connected && bt_state.connected_device_addr[0];
    adapter_powered = bt_state.adapter_powered;
    snprintf(connected_addr, sizeof(connected_addr), "%s", bt_state.connected_device_addr);
    pthread_mutex_unlock(&bt_state_lock);

    bt_pcm_player_set_active(false);
    if (should_disconnect) {
        bt_manager_disconnect(connected_addr);
    }
    if (adapter_powered) {
        make_unavailable();
    }

    pthread_mutex_lock(&bt_state_lock);
    bt_state.enabled = false;
    clear_connection_info();
    pthread_mutex_unlock(&bt_state_lock);
    send_bt_runtime();
    send_bt_avrcp_info();
}

/* =========================================================================
 *                         btmanager 回调
 * ========================================================================= */

static void on_adapter_state(btmg_adapter_state_t status) {
    switch (status) {
    case BTMG_ADAPTER_OFF:
        bt_pcm_player_set_active(false);
        pthread_mutex_lock(&bt_state_lock);
        bt_state.adapter_powered = false;
        bt_state.enabled = false;
        bt_state.discoverable = false;
        clear_connection_info();
        pthread_mutex_unlock(&bt_state_lock);
        APP_LOGI("bt", "adapter off");
        break;
    case BTMG_ADAPTER_ON:
    {
        bool enabled;
        char adapter_addr[sizeof(bt_state.adapter_addr)] = {0};
        char adapter_name[sizeof(bt_state.adapter_name)] = {0};
        refresh_adapter_identity();
        pthread_mutex_lock(&bt_state_lock);
        bt_state.adapter_powered = true;
        copy_text(adapter_addr, sizeof(adapter_addr), bt_state.adapter_addr);
        copy_text(adapter_name, sizeof(adapter_name), bt_state.adapter_name);
        enabled = bt_state.enabled;
        pthread_mutex_unlock(&bt_state_lock);
        if (enabled)
            make_discoverable();
        else
            make_unavailable();
        APP_LOGI("bt", "adapter on, name:%s, addr:%s",
                 adapter_name, adapter_addr);
        break;
    }
    case BTMG_ADAPTER_TURNING_ON:
        APP_LOGI("bt", "adapter turning on");
        break;
    case BTMG_ADAPTER_TURNING_OFF:
        APP_LOGI("bt", "adapter turning off");
        break;
    }
    send_bt_runtime();
    if (status == BTMG_ADAPTER_OFF) {
        send_bt_avrcp_info();
    }
}

static void on_bond_state(btmg_bond_state_t state, const char *bd_addr) {
    APP_LOGI("bt", "bond state:%d, addr:%s", state, bd_addr);
    /* 配对完成后保持可发现 */
    send_bt_runtime();
}

static void on_a2dp_sink_connection_state(const char *bd_addr,
                                          btmg_a2dp_sink_connection_state_t state) {
    bool a2dp_connected;
    switch (state) {
    case BTMG_A2DP_SINK_CONNECTED:
    {
        char remote_name[128] = { 0 };
        char connected_name[sizeof(bt_state.connected_device_name)] = {0};
        if (bt_manager_get_device_name((char *)bd_addr, remote_name) == 0) {
            copy_text(connected_name, sizeof(connected_name), remote_name);
        } else {
            copy_text(connected_name, sizeof(connected_name), bd_addr);
        }
        pthread_mutex_lock(&bt_state_lock);
        bt_state.a2dp_connected = true;
        copy_text(bt_state.connected_device_addr, sizeof(bt_state.connected_device_addr), bd_addr);
        copy_text(bt_state.connected_device_name, sizeof(bt_state.connected_device_name), connected_name);
        pthread_mutex_unlock(&bt_state_lock);
        APP_LOGI("bt", "A2DP sink connected: %s (%s)", connected_name, bd_addr);
        break;
    }
    case BTMG_A2DP_SINK_DISCONNECTED:
    {
        bool should_discoverable;
        bt_pcm_player_set_active(false);
        pthread_mutex_lock(&bt_state_lock);
        clear_connection_info();
        should_discoverable = bt_state.enabled && bt_state.adapter_powered;
        pthread_mutex_unlock(&bt_state_lock);
        /* 断开后恢复可发现 */
        if (should_discoverable) {
            make_discoverable();
        }
        APP_LOGI("bt", "A2DP sink disconnected: %s", bd_addr);
        break;
    }
    case BTMG_A2DP_SINK_CONNECTING:
        APP_LOGI("bt", "A2DP sink connecting: %s", bd_addr);
        break;
    case BTMG_A2DP_SINK_DISCONNECTING:
        APP_LOGI("bt", "A2DP sink disconnecting: %s", bd_addr);
        break;
    default:
        break;
    }
    send_bt_runtime();
    pthread_mutex_lock(&bt_state_lock);
    a2dp_connected = bt_state.a2dp_connected;
    pthread_mutex_unlock(&bt_state_lock);
    if (!a2dp_connected) {
        send_bt_avrcp_info();
    }
}

static void on_a2dp_sink_stream(const char *bd_addr,
                                uint16_t channels,
                                uint16_t sampling,
                                uint8_t *data,
                                uint32_t len) {
    (void)bd_addr;
    if (len > 0) {
        atomic_store_explicit(&bt_last_stream_packet_ms, monotonic_ms(),
                              memory_order_relaxed);
        bt_pcm_player_push(channels, sampling, data, len);
    }
}

static void on_a2dp_sink_audio_state(const char *bd_addr,
                                     btmg_a2dp_sink_audio_state_t state) {
    int64_t now = monotonic_ms();

    switch (state) {
    case BTMG_A2DP_SINK_AUDIO_STARTED:
        bt_pcm_player_set_active(true);
        pthread_mutex_lock(&bt_state_lock);
        bt_state.audio_streaming = true;
        bt_state.track_is_playing = true;
        bt_state.player_available = true;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_PLAYING;
        bt_state.metadata_clear_deadline_ms = 0;
        bt_state.track_progress_clock_ms = now;
        bt_state.track_progress_publish_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        APP_LOGI("bt", "A2DP audio started: %s", bd_addr);
        break;
    case BTMG_A2DP_SINK_AUDIO_SUSPENDED:
        bt_pcm_player_set_active(false);
        pthread_mutex_lock(&bt_state_lock);
        advance_track_position_locked(now);
        bt_state.audio_streaming = false;
        bt_state.track_is_playing = false;
        bt_state.pause_inferred_from_stream = false;
        if (bt_state.player_available) {
            bt_state.playback_state = BT_PLAYBACK_PAUSED;
        }
        bt_state.track_progress_clock_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        APP_LOGI("bt", "A2DP audio suspended: %s", bd_addr);
        break;
    case BTMG_A2DP_SINK_AUDIO_STOPPED:
        bt_pcm_player_set_active(false);
        pthread_mutex_lock(&bt_state_lock);
        advance_track_position_locked(now);
        bt_state.audio_streaming = false;
        bt_state.track_is_playing = false;
        bt_state.pause_inferred_from_stream = false;
        if (bt_state.player_available) {
            bt_state.playback_state = BT_PLAYBACK_STOPPED;
            bt_state.metadata_clear_deadline_ms = now + BT_METADATA_CLEAR_DELAY_MS;
        }
        bt_state.track_progress_clock_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        APP_LOGI("bt", "A2DP audio stopped: %s", bd_addr);
        break;
    }
    if (state != BTMG_A2DP_SINK_AUDIO_STARTED) {
        atomic_store_explicit(&bt_last_stream_packet_ms, 0, memory_order_relaxed);
    }
    send_bt_runtime();
    send_bt_avrcp_info();
}

static void on_avrcp_play_state(const char *bd_addr, btmg_avrcp_play_state_t state) {
    int64_t now = monotonic_ms();
    const char *state_name = "other";

    switch (state) {
    case BTMG_AVRCP_PLAYSTATE_PLAYING:
        state_name = "playing";
        pthread_mutex_lock(&bt_state_lock);
        bt_state.track_is_playing = true;
        bt_state.player_available = true;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_PLAYING;
        bt_state.metadata_clear_deadline_ms = 0;
        bt_state.track_progress_clock_ms = now;
        bt_state.track_progress_publish_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        break;
    case BTMG_AVRCP_PLAYSTATE_PAUSED:
        state_name = "paused";
        pthread_mutex_lock(&bt_state_lock);
        advance_track_position_locked(now);
        bt_state.track_is_playing = false;
        bt_state.player_available = true;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_PAUSED;
        if (!bt_state.audio_streaming) {
            bt_state.track_progress_clock_ms = 0;
        }
        pthread_mutex_unlock(&bt_state_lock);
        break;
    case BTMG_AVRCP_PLAYSTATE_STOPPED:
        state_name = "stopped";
        pthread_mutex_lock(&bt_state_lock);
        advance_track_position_locked(now);
        bt_state.track_is_playing = false;
        bt_state.pause_inferred_from_stream = false;
        bt_state.playback_state = BT_PLAYBACK_STOPPED;
        bt_state.metadata_clear_deadline_ms = now + BT_METADATA_CLEAR_DELAY_MS;
        bt_state.track_progress_clock_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        break;
    default:
        break;
    }
    APP_LOGI("bt", "AVRCP play state: %s (%d), device=%s",
             state_name, state, bd_addr ? bd_addr : "unknown");
    send_bt_avrcp_info();
}

static void on_avrcp_track_changed(const char *bd_addr, btmg_track_info_t track_info) {
    (void)bd_addr;
    /* 切歌时 placeholder 后通常很快跟随真实元数据；关闭播放器时则不会。
     * 延迟清理可以兼顾切歌稳定性和播放器关闭后的过期信息回收。 */
    if (metadata_text_is_placeholder(track_info.title)) {
        pthread_mutex_lock(&bt_state_lock);
        bt_state.metadata_clear_deadline_ms =
            monotonic_ms() + BT_METADATA_CLEAR_DELAY_MS;
        pthread_mutex_unlock(&bt_state_lock);
        return;
    }

    pthread_mutex_lock(&bt_state_lock);
    /* 去重：BTMG 在元数据分段到达时（title/artist/album 各就绪）会多次回调，
     * 标题未变则不重复打日志/发 IPC，避免日志噪音与冗余刷新。 */
    bool unchanged = strcmp(bt_state.track_title, track_info.title) == 0;
    copy_metadata_text(bt_state.track_title,    sizeof(bt_state.track_title),    track_info.title);
    copy_metadata_text(bt_state.track_artist,   sizeof(bt_state.track_artist),   track_info.artist);
    copy_metadata_text(bt_state.track_album,    sizeof(bt_state.track_album),    track_info.album);
    copy_metadata_text(bt_state.track_genre,    sizeof(bt_state.track_genre),    track_info.genre);
    copy_metadata_text(bt_state.track_duration, sizeof(bt_state.track_duration), track_info.duration);
    bt_state.player_available = true;
    bt_state.metadata_clear_deadline_ms = 0;
    bt_state.track_len_ms = bt_state.track_duration[0] ? atoi(bt_state.track_duration) : 0;
    bt_state.track_pos_ms = 0;
    bt_state.track_progress_clock_ms =
        (bt_state.audio_streaming || bt_state.track_is_playing) ? monotonic_ms() : 0;
    bt_state.track_progress_publish_ms = 0;
    if (!unchanged) {
        APP_LOGI("bt", "track: %s - %s", bt_state.track_title, bt_state.track_artist);
    }
    pthread_mutex_unlock(&bt_state_lock);
    send_bt_avrcp_info();
}

static void on_avrcp_play_position(const char *bd_addr, int song_len, int song_pos) {
    int64_t now = monotonic_ms();

    (void)bd_addr;
    pthread_mutex_lock(&bt_state_lock);
    bt_state.track_len_ms = song_len;
    bt_state.track_pos_ms = song_pos;
    if (song_len > 0) {
        bt_state.player_available = true;
        bt_state.metadata_clear_deadline_ms = 0;
    }
    bt_state.track_progress_clock_ms =
        (bt_state.audio_streaming || bt_state.track_is_playing) ? now : 0;
    bt_state.track_progress_publish_ms = now;
    pthread_mutex_unlock(&bt_state_lock);
    send_bt_avrcp_info();
}

static void on_avrcp_volume(const char *bd_addr, unsigned int volume) {
    (void)bd_addr;
    /* AVRCP volume range is 0-127, map to 0-100 */
    pthread_mutex_lock(&bt_state_lock);
    bt_state.volume = volume * 100 / 127;
    APP_LOGI("bt", "volume changed: %d", bt_state.volume);
    pthread_mutex_unlock(&bt_state_lock);
    send_bt_runtime();
}

/* Agent callbacks - auto-accept pairing (NOINPUTNOOUTPUT mode) */
static void on_agent_request_pincode(void *handle, char *device) {
    APP_LOGI("bt", "agent: request pincode from %s", device);
    bt_manager_agent_pair_send_empty_response(handle);
}

static void on_agent_request_passkey(void *handle, char *device) {
    unsigned int passkey = (unsigned int)rand() % 1000000;
    APP_LOGI("bt", "agent: request passkey from %s, sending %06u", device, passkey);
    bt_manager_agent_send_passkey(handle, passkey);
}

static void on_agent_request_confirm_passkey(void *handle, char *device, unsigned int passkey) {
    APP_LOGI("bt", "agent: confirm passkey %06u from %s", passkey, device);
    bt_manager_agent_pair_send_empty_response(handle);
}

static void on_agent_request_authorize(void *handle, char *device) {
    APP_LOGI("bt", "agent: authorize request from %s", device);
    bt_manager_agent_pair_send_empty_response(handle);
}

static void on_agent_authorize_service(void *handle, char *device, char *uuid) {
    APP_LOGI("bt", "agent: authorize service %s from %s", uuid, device);
    bt_manager_agent_pair_send_empty_response(handle);
}

/* =========================================================================
 *                           公开 API
 * ========================================================================= */

static void defer_bt_init(const char *reason) {
    int64_t now = monotonic_ms();
    bool first;

    pthread_mutex_lock(&bt_state_lock);
    first = !bt_init_deferred;
    bt_init_deferred = true;
    if (first) {
        bt_init_retry_count = 0;
        bt_init_retry_next_ms = now + BT_INIT_RETRY_INTERVAL_MS;
    }
    pthread_mutex_unlock(&bt_state_lock);

    if (first) {
        APP_LOGW("bt", "bt init deferred: %s", reason);
    }
}

static bool service_bt_init_actual(void) {
    bool initialized;
    bool desired_enabled;

    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    desired_enabled = bt_state.enabled;
    pthread_mutex_unlock(&bt_state_lock);

    if (initialized) {
        APP_LOGW("bt", "already initialized");
        return true;
    }

    pthread_mutex_lock(&bt_state_lock);
    memset(&bt_state, 0, sizeof(bt_state));
    bt_state.volume = 50; /* 默认音量 50% */
    bt_state.enabled = desired_enabled;
    pthread_mutex_unlock(&bt_state_lock);
    atomic_store_explicit(&bt_last_stream_packet_ms, 0, memory_order_relaxed);

    /* 1. Pre-init */
    if (bt_manager_preinit(&bt_callback) != 0) {
        APP_LOGE("bt", "preinit failed");
        bt_callback = NULL;
        return false;
    }

    /* 2. 蓝牙音箱只需要媒体音频输入和媒体控制，避免手机连成 HFP 通话设备 */
    bt_manager_enable_profile(BTMG_A2DP_SINK_ENABLE | BTMG_AVRCP_ENABLE);

    /* 3. 注册适配器回调 */
    bt_callback->btmg_adapter_cb.adapter_state_cb = on_adapter_state;

    /* 4. 注册 gap 回调 */
    bt_callback->btmg_gap_cb.gap_bond_state_cb = on_bond_state;

    /* 5. 注册 agent 回调 (免 PIN 配对) */
    bt_callback->btmg_agent_cb.agent_request_pincode        = on_agent_request_pincode;
    bt_callback->btmg_agent_cb.agent_display_pincode        = NULL;
    bt_callback->btmg_agent_cb.agent_request_passkey        = on_agent_request_passkey;
    bt_callback->btmg_agent_cb.agent_display_passkey        = NULL;
    bt_callback->btmg_agent_cb.agent_request_confirm_passkey = on_agent_request_confirm_passkey;
    bt_callback->btmg_agent_cb.agent_request_authorize      = on_agent_request_authorize;
    bt_callback->btmg_agent_cb.agent_authorize_service      = on_agent_authorize_service;

    /* 6. 注册 A2DP Sink 回调 */
    bt_callback->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb = on_a2dp_sink_connection_state;
    bt_callback->btmg_a2dp_sink_cb.a2dp_sink_audio_state_cb      = on_a2dp_sink_audio_state;
    bt_callback->btmg_a2dp_sink_cb.a2dp_sink_stream_cb           = on_a2dp_sink_stream;

    /* 7. 注册 AVRCP 回调 */
    bt_callback->btmg_avrcp_cb.avrcp_play_state_cb    = on_avrcp_play_state;
    bt_callback->btmg_avrcp_cb.avrcp_track_changed_cb  = on_avrcp_track_changed;
    bt_callback->btmg_avrcp_cb.avrcp_play_position_cb  = on_avrcp_play_position;
    bt_callback->btmg_avrcp_cb.avrcp_audio_volume_cb   = on_avrcp_volume;

    /* 8. 播放消费者必须早于 btmanager 回调线程就绪。 */
    if (!bt_pcm_player_start()) {
        APP_LOGE("bt", "explicit A2DP PCM player init failed");
        bt_manager_deinit(bt_callback);
        bt_callback = NULL;
        return false;
    }

    /* 9. Init */
    if (bt_manager_init(bt_callback) != 0) {
        APP_LOGE("bt", "init failed");
        bt_pcm_player_stop();
        bt_manager_deinit(bt_callback);
        bt_callback = NULL;
        return false;
    }
    /* 只在消费者线程就绪后开启原始 PCM 回调，避免任何无人消费窗口。 */
    bt_a2dp_sink_stream_cb_enable(true);
    bluez_player_monitor_init(on_bluez_player_state);

    pthread_mutex_lock(&bt_state_lock);
    bt_state.initialized = true;
    bt_init_deferred = false;
    bt_init_retry_count = 0;
    bt_init_retry_next_ms = 0;
    pthread_mutex_unlock(&bt_state_lock);
    if (bt_manager_get_adapter_state() != BTMG_ADAPTER_ON) {
        /*
         * This enables btmanager/BlueZ profile registration. The board startup
         * script should already have prepared hci0, bluetoothd, and bluealsa.
         * UI toggles stay soft and do not call bt_manager_enable().
         */
        bt_manager_enable(true);
    }
    if (bt_manager_get_adapter_state() == BTMG_ADAPTER_ON) {
        bool enabled;
        refresh_adapter_identity();
        pthread_mutex_lock(&bt_state_lock);
        bt_state.adapter_powered = true;
        enabled = bt_state.enabled;
        pthread_mutex_unlock(&bt_state_lock);
        if (enabled)
            make_discoverable();
        else
            make_unavailable();
    }
    APP_LOGI("bt", "service initialized");
    return true;
}

void service_bt_init(void) {
    bool initialized;

    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    pthread_mutex_unlock(&bt_state_lock);

    if (initialized) {
        APP_LOGW("bt", "already initialized");
        return;
    }

    if (!hci0_is_up()) {
        defer_bt_init("hci0 is not UP");
        return;
    }

    if (!service_bt_init_actual()) {
        defer_bt_init("bt manager init failed");
    }
}

void service_bt_deinit(void) {
    bool initialized;
    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    bt_init_deferred = false;
    bt_init_retry_count = 0;
    bt_init_retry_next_ms = 0;
    pthread_mutex_unlock(&bt_state_lock);
    if (!initialized) return;

    soft_disable_bt_service();
    bluez_player_monitor_deinit();
    bt_a2dp_sink_stream_cb_enable(false);
    bt_pcm_player_stop();
    bt_manager_deinit(bt_callback);
    bt_callback = NULL;

    pthread_mutex_lock(&bt_state_lock);
    memset(&bt_state, 0, sizeof(bt_state));
    pthread_mutex_unlock(&bt_state_lock);
    APP_LOGI("bt", "service deinitialized");
}

void service_bt_enable(bool enable) {
    bool initialized;
    bool enabled;
    bool adapter_powered;

    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    enabled = bt_state.enabled;
    adapter_powered = bt_state.adapter_powered;
    if (!initialized) {
        bt_state.enabled = enable;
        pthread_mutex_unlock(&bt_state_lock);
        APP_LOGI("bt", "save desired enabled=%d until adapter is ready", enable ? 1 : 0);
        if (enable) {
            service_bt_init();
        }
        return;
    }
    pthread_mutex_unlock(&bt_state_lock);

    if (enable) {
        if (enabled) {
            send_bt_runtime();
            return;
        }

        if (!adapter_powered && bt_manager_get_adapter_state() == BTMG_ADAPTER_ON) {
            refresh_adapter_identity();
            pthread_mutex_lock(&bt_state_lock);
            bt_state.adapter_powered = true;
            adapter_powered = true;
            pthread_mutex_unlock(&bt_state_lock);
        }
        if (!adapter_powered) {
            APP_LOGE("bt", "adapter is not powered; run bt_init.sh before enabling BT speaker");
            send_bt_runtime();
            return;
        }

        pthread_mutex_lock(&bt_state_lock);
        bt_state.enabled = true;
        pthread_mutex_unlock(&bt_state_lock);
        make_discoverable();
        send_bt_runtime();
    } else {
        if (!enabled) {
            send_bt_runtime();
            return;
        }

        soft_disable_bt_service();
    }
}

void service_bt_disconnect_device(const char *bd_addr) {
    bool initialized;
    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    pthread_mutex_unlock(&bt_state_lock);
    if (!initialized) return;
    bt_manager_disconnect((char *)bd_addr);
}

void service_bt_set_adapter_name(const char *name)
{
    sys_settings_t *settings = sys_settings_get();
    char cleaned[sizeof(settings->bluetooth_name)];
    size_t i;
    size_t start = 0;
    size_t end;
    bool initialized;

    if (!settings) return;

    memset(cleaned, 0, sizeof(cleaned));
    if (name && name[0]) {
        snprintf(cleaned, sizeof(cleaned), "%s", name);
        for (i = 0; cleaned[i] != '\0'; i++) {
            unsigned char c = (unsigned char)cleaned[i];
            if (c < 0x20 || c == 0x7F) cleaned[i] = ' ';
        }
        while (cleaned[start] == ' ') start++;
        end = strlen(cleaned);
        while (end > start && cleaned[end - 1] == ' ') end--;
        if (end > start) {
            if (start > 0 || cleaned[end] != '\0') {
                char tmp[sizeof(cleaned)];
                memset(tmp, 0, sizeof(tmp));
                if (end - start >= sizeof(tmp)) end = start + sizeof(tmp) - 1;
                memcpy(tmp, cleaned + start, end - start);
                memcpy(cleaned, tmp, sizeof(tmp));
            }
        } else {
            cleaned[0] = '\0';
        }
    }

    if (strncmp(settings->bluetooth_name, cleaned, sizeof(settings->bluetooth_name)) != 0) {
        memset(settings->bluetooth_name, 0, sizeof(settings->bluetooth_name));
        if (cleaned[0]) {
            snprintf(settings->bluetooth_name, sizeof(settings->bluetooth_name),
                     "%s", cleaned);
        }
        sys_settings_save();
        APP_LOGI("bt", "adapter name saved: '%s'",
                 settings->bluetooth_name[0] ? settings->bluetooth_name : "(auto)");
    }

    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    pthread_mutex_unlock(&bt_state_lock);

    if (initialized) {
        bool reassert_discoverable = false;

        refresh_adapter_identity();
        pthread_mutex_lock(&bt_state_lock);
        reassert_discoverable = bt_state.enabled && bt_state.adapter_powered;
        pthread_mutex_unlock(&bt_state_lock);
        /* Re-assert discoverable so phones re-scan the new local name. */
        if (reassert_discoverable) {
            make_discoverable();
        }
        service_bt_send_runtime_status();
    }
}

void service_bt_set_volume(int vol) {
    bool initialized;
    bool a2dp_connected;
    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    a2dp_connected = bt_state.a2dp_connected;
    pthread_mutex_unlock(&bt_state_lock);
    if (!initialized) return;
    if (!a2dp_connected) {
        send_bt_runtime();
        return;
    }
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    pthread_mutex_lock(&bt_state_lock);
    bt_state.volume = vol;
    pthread_mutex_unlock(&bt_state_lock);
    bt_manager_a2dp_set_vol(vol);
    send_bt_runtime();
}

static bool service_bt_avrcp_command(btmg_avrcp_command_t command) {
    char connected_addr[sizeof(bt_state.connected_device_addr)];
    bool initialized;
    bool a2dp_connected;
    bool player_available;

    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    a2dp_connected = bt_state.a2dp_connected;
    player_available = bt_state.player_available;
    snprintf(connected_addr, sizeof(connected_addr), "%s", bt_state.connected_device_addr);
    pthread_mutex_unlock(&bt_state_lock);

    if (!initialized) return false;
    if (!a2dp_connected || !connected_addr[0]) {
        send_bt_runtime();
        return false;
    }
    if (!player_available) {
        APP_LOGI("bt", "ignore AVRCP command %d: phone player unavailable", command);
        send_bt_avrcp_info();
        return false;
    }

    if (bt_manager_avrcp_command(connected_addr, command) != 0) {
        APP_LOGW("bt", "avrcp command failed: %d", command);
        return false;
    }

    return true;
}

void service_bt_avrcp_play(void) {
    if (service_ai_blocks_bt_playback()) {
        APP_LOGI("bt", "ignore play while AI owns audio focus");
        send_bt_runtime();
        send_bt_avrcp_info();
        return;
    }

    if (service_bt_avrcp_command(BTMG_AVRCP_PLAY)) {
        pthread_mutex_lock(&bt_state_lock);
        bt_state.track_is_playing = true;
        bt_state.playback_state = BT_PLAYBACK_PLAYING;
        bt_state.pause_inferred_from_stream = false;
        bt_state.track_progress_clock_ms = monotonic_ms();
        bt_state.track_progress_publish_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        send_bt_avrcp_info();
    }
}

void service_bt_avrcp_pause(void) {
    if (service_bt_avrcp_command(BTMG_AVRCP_PAUSE)) {
        int64_t now = monotonic_ms();

        pthread_mutex_lock(&bt_state_lock);
        advance_track_position_locked(now);
        bt_state.audio_streaming = false;
        bt_state.track_is_playing = false;
        bt_state.playback_state = BT_PLAYBACK_PAUSED;
        bt_state.pause_inferred_from_stream = false;
        bt_state.track_progress_clock_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        send_bt_runtime();
        send_bt_avrcp_info();
    }
}

void service_bt_avrcp_next(void) {
    service_bt_avrcp_command(BTMG_AVRCP_FORWARD);
}

void service_bt_avrcp_prev(void) {
    service_bt_avrcp_command(BTMG_AVRCP_BACKWARD);
}

void service_bt_send_runtime_status(void) {
    bool initialized;
    pthread_mutex_lock(&bt_state_lock);
    initialized = bt_state.initialized;
    pthread_mutex_unlock(&bt_state_lock);
    if (!initialized) return;
    send_bt_runtime();
    send_bt_avrcp_info();
}

static void service_bt_retry_deferred_init(void) {
    int64_t now = monotonic_ms();
    int attempt;
    bool due;

    pthread_mutex_lock(&bt_state_lock);
    due = bt_init_deferred && !bt_state.initialized &&
          bt_init_retry_count < BT_INIT_RETRY_MAX_ATTEMPTS &&
          now >= bt_init_retry_next_ms;
    if (!due) {
        pthread_mutex_unlock(&bt_state_lock);
        return;
    }

    bt_init_retry_count++;
    attempt = bt_init_retry_count;
    bt_init_retry_next_ms = now + BT_INIT_RETRY_INTERVAL_MS;
    pthread_mutex_unlock(&bt_state_lock);

    if (!hci0_is_up()) {
        if (attempt == 1 || attempt % 5 == 0) {
            APP_LOGW("bt", "deferred init waiting for hci0 UP (%d/%d)",
                     attempt, BT_INIT_RETRY_MAX_ATTEMPTS);
        }
    } else {
        APP_LOGI("bt", "hci0 is UP, retry bt service init (%d/%d)",
                 attempt, BT_INIT_RETRY_MAX_ATTEMPTS);
        if (service_bt_init_actual()) {
            return;
        }
        APP_LOGW("bt", "deferred bt manager init failed (%d/%d)",
                 attempt, BT_INIT_RETRY_MAX_ATTEMPTS);
    }

    if (attempt >= BT_INIT_RETRY_MAX_ATTEMPTS) {
        pthread_mutex_lock(&bt_state_lock);
        bt_init_deferred = false;
        bt_init_retry_next_ms = 0;
        pthread_mutex_unlock(&bt_state_lock);
        APP_LOGE("bt", "deferred init timeout; bluetooth service stays offline");
    }
}

void service_bt_update(void) {
    bool publish_progress = false;
    bool publish_state = false;
    bool inferred_pause = false;
    bool inferred_resume = false;
    bool metadata_expired = false;
    bool player_disabled = false;
    int64_t now = monotonic_ms();
    int64_t last_stream_packet =
        atomic_load_explicit(&bt_last_stream_packet_ms, memory_order_relaxed);

    service_bt_retry_deferred_init();
    bluez_player_monitor_update();

    if (service_ai_blocks_bt_playback() && service_bt_is_audio_streaming()) {
        APP_LOGI("bt", "pause playback while AI owns audio focus");
        service_bt_avrcp_pause();
        return;
    }

    pthread_mutex_lock(&bt_state_lock);

    if (bt_state.metadata_clear_deadline_ms > 0 &&
        now >= bt_state.metadata_clear_deadline_ms) {
        bt_state.metadata_clear_deadline_ms = 0;
        clear_track_metadata_locked();
        if (!bt_state.track_is_playing) {
            bt_state.player_available = false;
            bt_state.pause_inferred_from_stream = false;
            bt_state.playback_state = BT_PLAYBACK_STOPPED;
            player_disabled = true;
        }
        metadata_expired = true;
        publish_state = true;
    }

    /* 部分手机只在 A2DP 真正 suspend 时才上报 paused。PCM 数据回调已出现后，
     * 连续短时间没有新数据即可先更新 UI；正式 AVRCP/A2DP 回调仍是最终校正。 */
    if (bt_state.initialized && bt_state.a2dp_connected &&
        bt_state.audio_streaming && bt_state.player_available &&
        last_stream_packet > 0) {
        int64_t stream_idle_ms = now - last_stream_packet;
        if (bt_state.track_is_playing && stream_idle_ms >= BT_STREAM_IDLE_PAUSE_MS) {
            advance_track_position_locked(now);
            bt_state.track_is_playing = false;
            bt_state.pause_inferred_from_stream = true;
            bt_state.playback_state = BT_PLAYBACK_PAUSED;
            bt_state.track_progress_clock_ms = 0;
            inferred_pause = true;
            publish_state = true;
        } else if (bt_state.pause_inferred_from_stream &&
                   stream_idle_ms < BT_STREAM_IDLE_PAUSE_MS) {
            bt_state.track_is_playing = true;
            bt_state.pause_inferred_from_stream = false;
            bt_state.playback_state = BT_PLAYBACK_PLAYING;
            bt_state.track_progress_clock_ms = now;
            bt_state.track_progress_publish_ms = 0;
            inferred_resume = true;
            publish_state = true;
        }
    }

    if (bt_state.initialized && bt_state.a2dp_connected &&
        bt_state.track_len_ms > 0 &&
        bt_state.track_is_playing) {
        advance_track_position_locked(now);
        if (bt_state.track_progress_publish_ms <= 0 ||
            now - bt_state.track_progress_publish_ms >= BT_PROGRESS_UPDATE_INTERVAL_MS) {
            bt_state.track_progress_publish_ms = now;
            publish_progress = true;
        }
    }
    pthread_mutex_unlock(&bt_state_lock);

    if (metadata_expired) {
        APP_LOGI("bt", "phone player metadata expired; controls %s",
                 player_disabled ? "disabled" : "kept available");
    }
    if (inferred_pause) {
        APP_LOGI("bt", "infer pause after %d ms without A2DP data",
                 BT_STREAM_IDLE_PAUSE_MS);
    } else if (inferred_resume) {
        APP_LOGI("bt", "infer resume from A2DP data");
    }
    if (publish_progress || publish_state) {
        send_bt_avrcp_info();
    }
}

bool service_bt_is_a2dp_connected(void) {
    bool ret;
    pthread_mutex_lock(&bt_state_lock);
    ret = bt_state.initialized && bt_state.a2dp_connected;
    pthread_mutex_unlock(&bt_state_lock);
    return ret;
}

bool service_bt_is_audio_streaming(void) {
    bool ret;
    pthread_mutex_lock(&bt_state_lock);
    ret = bt_state.initialized && bt_state.audio_streaming;
    pthread_mutex_unlock(&bt_state_lock);
    return ret;
}

bool service_bt_is_playing(void) {
    bool ret;
    pthread_mutex_lock(&bt_state_lock);
    ret = bt_state.initialized && (bt_state.audio_streaming || bt_state.track_is_playing);
    pthread_mutex_unlock(&bt_state_lock);
    return ret;
}
