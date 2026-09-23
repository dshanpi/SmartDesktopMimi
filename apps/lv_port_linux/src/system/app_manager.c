#include "app_manager.h"
#include "app_registry.h"
#include "../lib/backends.h"
#include <stdio.h>

static lv_obj_t *home_screen = NULL;
static lv_obj_t *hdmi_preview_screen = NULL;
static lv_obj_t *hdmi_return_screen = NULL;
static app_id_t hdmi_return_app_id = APP_ID_NONE;
static app_id_t current_app_id = APP_ID_NONE;

typedef enum {
    APP_MANAGER_ACTION_NONE = 0,
    APP_MANAGER_ACTION_OPEN,
    APP_MANAGER_ACTION_BACK_HOME,
} app_manager_action_t;

typedef struct {
    app_manager_action_t type;
    app_id_t app_id;
} app_manager_pending_action_t;

static app_manager_pending_action_t pending_action = {
    .type = APP_MANAGER_ACTION_NONE,
    .app_id = APP_ID_NONE,
};

static void app_manager_open_now(app_id_t id);
static void app_manager_back_home_now(void);
static void app_manager_process_pending_action(void *user_data);
static void app_manager_schedule_action(app_manager_action_t type, app_id_t id);

void app_manager_init(void)
{
    home_screen = lv_scr_act();
    printf("[AppManager] Initialized. Home Screen: %p\n", (void *)home_screen);
}

static void app_manager_open_now(app_id_t id)
{
    if (id == current_app_id) return;

    AppDescriptor *app = app_registry_get_descriptor(id);
    if (!app) {
        printf("[AppManager] App ID %d not found!\n", id);
        return;
    }

    printf("[AppManager] Opening App: %s\n", app->name);

    lv_obj_t *old_scr = NULL;
    if (current_app_id != APP_ID_NONE) {
        AppDescriptor *old_app = app_registry_get_descriptor(current_app_id);
        old_scr = lv_scr_act();
        if (old_app && old_app->close) old_app->close();
        current_app_id = APP_ID_NONE;
    }

    if (app->init) app->init();

    if (old_scr &&
        old_scr != home_screen &&
        old_scr != hdmi_preview_screen &&
        old_scr != lv_scr_act()) {
        lv_obj_delete_async(old_scr);
    }
    current_app_id = id;
}

static void app_manager_back_home_now(void)
{
    if (current_app_id == APP_ID_NONE) return;

    printf("[AppManager] Returning to Home...\n");
    AppDescriptor *app = app_registry_get_descriptor(current_app_id);
    if (app && app->close) app->close();

    if (home_screen) {
        lv_obj_t *app_scr = lv_scr_act();
        lv_scr_load(home_screen);
        if (app_scr && app_scr != home_screen) lv_obj_delete_async(app_scr);
    }
    current_app_id = APP_ID_NONE;
}

static void app_manager_process_pending_action(void *user_data)
{
    app_manager_pending_action_t action;
    (void)user_data;

    action = pending_action;
    pending_action.type = APP_MANAGER_ACTION_NONE;
    pending_action.app_id = APP_ID_NONE;

    switch (action.type) {
        case APP_MANAGER_ACTION_OPEN:
            app_manager_open_now(action.app_id);
            break;
        case APP_MANAGER_ACTION_BACK_HOME:
            app_manager_back_home_now();
            break;
        default:
            break;
    }
}

static void app_manager_schedule_action(app_manager_action_t type, app_id_t id)
{
    if (pending_action.type != APP_MANAGER_ACTION_NONE) {
        lv_async_call_cancel(app_manager_process_pending_action, &pending_action);
    }

    pending_action.type = type;
    pending_action.app_id = id;
    lv_async_call(app_manager_process_pending_action, &pending_action);
}

void app_manager_open(app_id_t id)
{
    if (id == APP_ID_NONE) return;
    if (id == current_app_id && pending_action.type == APP_MANAGER_ACTION_NONE) return;
    app_manager_schedule_action(APP_MANAGER_ACTION_OPEN, id);
}

void app_manager_back_home(void)
{
    if (current_app_id == APP_ID_NONE && pending_action.type == APP_MANAGER_ACTION_NONE) return;
    app_manager_schedule_action(APP_MANAGER_ACTION_BACK_HOME, APP_ID_NONE);
}

void app_manager_show_hdmi_preview_screen(void)
{
    if (hdmi_preview_screen && lv_scr_act() == hdmi_preview_screen) return;

    hdmi_return_screen = lv_scr_act();
    hdmi_return_app_id = current_app_id;

    if (!hdmi_preview_screen) {
        hdmi_preview_screen = lv_obj_create(NULL);
        lv_obj_remove_style_all(hdmi_preview_screen);
        lv_obj_set_style_bg_opa(hdmi_preview_screen, LV_OPA_TRANSP, 0);
    }

    printf("[AppManager] Showing HDMI transparent screen\n");
    lv_scr_load(hdmi_preview_screen);
    if (!sunxifb_set_transparent_overlay(true)) {
        printf("[AppManager] SunxiFB transparency unavailable\n");
    }
    lv_obj_invalidate(hdmi_preview_screen);
}

void app_manager_hide_hdmi_preview_screen(void)
{
    if (!hdmi_preview_screen) return;

    printf("[AppManager] Hiding HDMI transparent screen\n");
    if (home_screen && lv_scr_act() == hdmi_preview_screen) {
        lv_scr_load(hdmi_return_screen ? hdmi_return_screen : home_screen);
    }
    sunxifb_set_transparent_overlay(false);
    if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());

    lv_obj_delete(hdmi_preview_screen);
    hdmi_preview_screen = NULL;
    hdmi_return_screen = NULL;
    current_app_id = hdmi_return_app_id;
    hdmi_return_app_id = APP_ID_NONE;
}
