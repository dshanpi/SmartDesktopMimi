#include "lockscreen_3d.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h"
#include "../../../cat3d/frame_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static pid_t child;
static struct cat3d_frames *frames;
static lv_obj_t *image, *static_cat;
static lv_image_dsc_t descriptor;
static lv_timer_t *poll_timer, *reap_timer;
static uint32_t last_frame, stop_time, last_sequence;

static void reap(lv_timer_t *timer)
{
    int status;
    pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child || (result < 0 && errno == ECHILD)) {
        child = 0; reap_timer = NULL; lv_timer_delete(timer);
    } else if (lv_tick_elaps(stop_time) > 1500) kill(child, SIGKILL);
}

void ui_lockscreen_3d_stop(void)
{
    if (poll_timer) lv_timer_delete(poll_timer);
    poll_timer = NULL;
    if (image) { lv_obj_delete(image); image = NULL; }
    if (static_cat) lv_obj_remove_flag(static_cat, LV_OBJ_FLAG_HIDDEN);
    static_cat = NULL;
    if (descriptor.data) {
        lv_image_cache_drop(&descriptor);
        lv_free((void *)descriptor.data); descriptor.data = NULL;
    }
    if (frames) {
        __atomic_store_n(&frames->stop, 1, __ATOMIC_RELEASE);
        munmap(frames, sizeof(*frames)); frames = NULL;
    }
    if (child > 0 && !reap_timer) {
        kill(child, SIGTERM); stop_time = lv_tick_get();
        reap_timer = lv_timer_create(reap, 50, NULL);
    }
}

static void poll_frame(lv_timer_t *timer)
{
    (void)timer;
    int status;
    if (child > 0 && waitpid(child, &status, WNOHANG) == child) {
        fprintf(stderr, "[Cat3D] renderer exited status=%d; using static cat\n", status);
        child = 0; ui_lockscreen_3d_stop(); return;
    }
    /* Never block the UI on rendering, startup, driver teardown or waitpid. */
    for (int i = 0; i < 3; ++i) {
        uint32_t expected = CAT3D_READY;
        if (!__atomic_compare_exchange_n(&frames->slots[i].state, &expected,
                CAT3D_READING, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) continue;
        if (frames->slots[i].sequence > last_sequence) {
            memcpy((void *)descriptor.data, frames->slots[i].bgra, CAT3D_BYTES);
            last_sequence = frames->slots[i].sequence;
            lv_image_cache_drop(&descriptor);
            lv_image_set_src(image, &descriptor); lv_obj_invalidate(image);
            lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(static_cat, LV_OBJ_FLAG_HIDDEN);
            last_frame = lv_tick_get();
        }
        __atomic_store_n(&frames->slots[i].state, CAT3D_FREE, __ATOMIC_RELEASE);
    }
    if (lv_tick_elaps(last_frame) > (last_sequence ? 5000U : 30000U)) {
        fprintf(stderr, "[Cat3D] renderer timed out; using static cat\n");
        ui_lockscreen_3d_stop();
    }
}

void ui_lockscreen_3d_start(lv_obj_t *parent, lv_obj_t *fallback)
{
    if (child || frames || access("/usr/bin/mimi-cat3d-start", X_OK)) return;
    /* Trial activation is lost on reboot. Persistent activation is installed
     * only after device validation, so recovery boots keep the original UI. */
    if (access("/etc/mimi-cat3d.enabled", F_OK) &&
        access("/tmp/mimi-cat3d.enabled", F_OK)) return;
    char name[] = "/tmp/mimi-cat3d-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) return;
    unlink(name);
    if (ftruncate(fd, sizeof(*frames))) { close(fd); return; }
    frames = mmap(NULL, sizeof(*frames), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (frames == MAP_FAILED) { frames = NULL; close(fd); return; }
    memset(frames, 0, sizeof(*frames)); frames->magic = CAT3D_MAGIC;
    descriptor = (lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,
        .cf=LV_COLOR_FORMAT_XRGB8888,.w=CAT3D_WIDTH,.h=CAT3D_HEIGHT,
        .stride=CAT3D_WIDTH*4},.data_size=CAT3D_BYTES,.data=lv_malloc(CAT3D_BYTES)};
    if (!descriptor.data) { close(fd); ui_lockscreen_3d_stop(); return; }
    memset((void *)descriptor.data, 0, CAT3D_BYTES);
    static_cat = fallback;
    image = lv_image_create(parent);
    lv_obj_set_pos(image, (1024-CAT3D_WIDTH)/2, 232);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fd, 3);
    if (fd != 3) posix_spawn_file_actions_addclose(&actions, fd);
    posix_spawn_file_actions_addopen(&actions, 1, "/tmp/mimi-cat3d.log", O_WRONLY|O_CREAT|O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    char *args[] = {"/usr/bin/mimi-cat3d-start", "--shared-fd", "3", NULL};
    pid_t pid = 0;
    int result = posix_spawn(&pid, args[0], &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions); close(fd);
    if (result) { fprintf(stderr, "[Cat3D] spawn: %s\n", strerror(result)); ui_lockscreen_3d_stop(); return; }
    child = pid; last_sequence = 0; last_frame = lv_tick_get();
    poll_timer = lv_timer_create(poll_frame, 40, NULL);
    fprintf(stderr, "[Cat3D] started pid=%ld\n", (long)child);
}
