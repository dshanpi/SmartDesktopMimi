#include "desktop_health.h"
#include "../middleware/middleware.h"
#include "../system/backend_types.h"
#include "lvgl/lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static uint64_t backend_reply;
static uint64_t activity_seen[4];
static bool activity_busy[4];
static uint64_t monotonic_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void runtime_received(const mw_msg_t *message)
{
    if (message->data_len >= sizeof(wifi_runtime_status_t)) backend_reply = monotonic_ms();
}
static void activity_received(const mw_msg_t *message)
{
    unsigned index;
    bool busy;
    if (message->topic == TOPIC_AI_STATUS && message->data_len >= sizeof(ai_status_t)) {
        const ai_status_t *s = (const ai_status_t *)message->data; index = 0;
        busy = s->free_chat_active || (s->state >= AI_STATE_CONNECTING && s->state <= AI_STATE_SPEAKING);
    } else if (message->topic == TOPIC_BT_RUNTIME && message->data_len >= sizeof(bt_runtime_t)) {
        const bt_runtime_t *s = (const bt_runtime_t *)message->data; index = 1; busy = s->audio_streaming;
    } else if (message->topic == TOPIC_VIDEO_STATUS && message->data_len >= sizeof(video_status_t)) {
        const video_status_t *s = (const video_status_t *)message->data; index = 2; busy = s->is_playing || s->is_paused;
    } else if (message->topic == TOPIC_HDMI_PREVIEW_STATUS && message->data_len >= sizeof(hdmi_preview_status_t)) {
        const hdmi_preview_status_t *s = (const hdmi_preview_status_t *)message->data; index = 3; busy = s->active;
    } else return;
    activity_seen[index] = monotonic_ms(); activity_busy[index] = busy;
}
static void heartbeat(lv_timer_t *timer)
{
    (void)timer;
    /* Re-request a read-only state to prove the real V1 UI/backend round trip. */
    wifi_cmd_t cmd = {.action = CMD_GET_RUNTIME};
    mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    ai_cmd_t ai = {.action = AI_CMD_GET_STATUS};
    bt_cmd_t bt = {.action = BT_CMD_GET_STATUS};
    video_cmd_t video = {.action = VIDEO_CMD_GET_STATUS};
    hdmi_cmd_t hdmi = {.action = HDMI_CMD_GET_STATUS};
    mw_publish(TOPIC_AI_COMMAND, &ai, sizeof(ai), MW_DIR_UI_TO_BACKEND);
    mw_publish(TOPIC_BT_COMMAND, &bt, sizeof(bt), MW_DIR_UI_TO_BACKEND);
    mw_publish(TOPIC_VIDEO_CONTROL, &video, sizeof(video), MW_DIR_UI_TO_BACKEND);
    mw_publish(TOPIC_HDMI_COMMAND, &hdmi, sizeof(hdmi), MW_DIR_UI_TO_BACKEND);
    uint64_t now = monotonic_ms();
    bool ready = true, busy = false;
    for (unsigned i = 0; i < 4; i++) {
        if (!activity_seen[i] || now - activity_seen[i] > 5000) ready = false;
        busy |= activity_busy[i];
    }
    FILE *file = fopen("/run/smartdesktopmimi/ui-health.tmp", "w");
    if (!file) return;
    fprintf(file, "{\"pid\":%ld,\"monotonic_ms\":%llu,\"backend_reply_ms\":%llu,\"activity_ready\":%s,\"busy\":%s}\n",
        (long)getpid(), (unsigned long long)monotonic_ms(), (unsigned long long)backend_reply, ready ? "true" : "false", busy ? "true" : "false");
    if (fclose(file) == 0) rename("/run/smartdesktopmimi/ui-health.tmp", "/run/smartdesktopmimi/ui-health.json");
}
void desktop_health_start(void)
{
    /* Legacy systems and host fixtures do not create platform health receipts. */
    if (!getenv("MIMI_RELEASE_DIR")) return;
    mw_subscribe(TOPIC_WIFI_RUNTIME, runtime_received);
    mw_subscribe(TOPIC_AI_STATUS, activity_received);
    mw_subscribe(TOPIC_BT_RUNTIME, activity_received);
    mw_subscribe(TOPIC_VIDEO_STATUS, activity_received);
    mw_subscribe(TOPIC_HDMI_PREVIEW_STATUS, activity_received);
    lv_timer_create(heartbeat, 1000, NULL);
}
