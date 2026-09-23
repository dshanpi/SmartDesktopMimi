/**
 *
 * @file evdev.c
 *
 * The lib evdev driver
 *
 * Based on the original file from the repository
 *
 * - Move the driver to a separate file to avoid excessive conditional
 *   compilation
 *   Author: EDGEMTech Ltd, Erik Tagirov (erik.tagirov@edgemtech.ch)
 *
 * Copyright (c) 2025 EDGEMTech Ltd.
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#if LV_USE_EVDEV
#include "lvgl/src/core/lv_global.h"
#include "../backends.h"

/* Product-level navigation hook implemented by src/system/app_manager.c. */
void app_manager_back_home(void);

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void discovery_cb(lv_indev_t * indev, lv_evdev_type_t type, void * user_data);
static lv_indev_t * init_pointer_evdev(lv_display_t * display);
static void navigation_key_cb(lv_event_t * event);

/**********************
 *  STATIC VARIABLES
 **********************/

static char * backend_name = "EVDEV";

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/*
 * Initialize the evdev driver
 *
 * @param backend the backend descriptor
 */
int backend_init_evdev(backend_t * backend)
{
    LV_ASSERT_NULL(backend);
    backend->handle->indev = malloc(sizeof(indev_backend_t));
    LV_ASSERT_NULL(backend->handle->indev);

    backend->handle->indev->init_indev = init_pointer_evdev;

    backend->name = backend_name;
    backend->type = BACKEND_INDEV;
    return 0;
}


/**********************
 *   STATIC FUNCTIONS
 **********************/

/*
 * Remove cursor icon
 *
 * @description When the indev is deleted remove the mouse cursor icon
 * @note called by the LVGL evdev driver
 * @param e the deletion event
 */
#if 0
static void indev_deleted_cb(lv_event_t * e)
{
    if(LV_GLOBAL_DEFAULT()->deinit_in_progress) return;
    lv_obj_t * cursor_obj = lv_event_get_user_data(e);
    lv_obj_delete(cursor_obj);
}
#endif


/*
 * Set cursor icon
 *
 * @description Once the input device is discovered set the mouse cursor icon
 * @note called by the LVGL evdev driver
 * @param indev the input device
 * @param type the type of the input device
 * @param user_data the user data
 */
static void discovery_cb(lv_indev_t * indev, lv_evdev_type_t type, void * user_data)
{
    LV_LOG_USER("new '%s' device discovered", type == LV_EVDEV_TYPE_REL ? "REL" :
                type == LV_EVDEV_TYPE_ABS ? "ABS" :
                type == LV_EVDEV_TYPE_KEY ? "KEY" :
                "unknown");

    lv_display_t * disp = user_data;
    lv_indev_set_display(indev, disp);

    if(type == LV_EVDEV_TYPE_REL) {
        // set_mouse_cursor_icon(indev, disp);
    }
}

/*
 * Set cursor icon
 *
 * @description Enables a pointer (touchscreen/mouse) input device
 * @param display the display on which to create
 * @param indev the input device to set the cursor on
 */
#if 0
static void set_mouse_cursor_icon(lv_indev_t * indev, lv_display_t * display)
{
    /* Set the cursor icon */
    LV_IMAGE_DECLARE(mouse_cursor_icon);
    lv_obj_t * cursor_obj = lv_image_create(lv_display_get_screen_active(display));
    lv_image_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(indev, cursor_obj);

    /* delete the mouse cursor icon if the device is removed */
    lv_indev_add_event_cb(indev, indev_deleted_cb, LV_EVENT_DELETE, cursor_obj);

}
#endif

/*
 * Initialize a mouse pointer device
 *
 * Enables a pointer (touchscreen/mouse) input device
 * Use 'evtest' to find the correct input device. /dev/input/by-id/ is recommended if possible
 * Use /dev/input/by-id/my-mouse-or-touchscreen or /dev/input/eventX
 *
 * If LV_LINUX_EVDEV_POINTER_DEVICE is not set, automatic evdev discovery will start
 *
 * @param display the LVGL display
 *
 * @return input device
 */
static lv_indev_t * init_pointer_evdev(lv_display_t * display)
{
    const char * input_device = getenv("LV_LINUX_EVDEV_POINTER_DEVICE");
    const char * keypad_device = getenv("LV_LINUX_EVDEV_KEYPAD_DEVICE");
    lv_indev_t * pointer = NULL;
    lv_indev_t * keypad;

    if(input_device == NULL) {
        LV_LOG_USER("Using evdev automatic discovery.");
        lv_evdev_discovery_start(discovery_cb, display);
    }
    else {
        pointer = lv_evdev_create(LV_INDEV_TYPE_POINTER, input_device);
        if(pointer != NULL) {
            lv_indev_set_display(pointer, display);
        }
    }

    if(keypad_device != NULL && keypad_device[0] != '\0') {
        lv_group_t * group = lv_group_get_default();
        if(group == NULL) {
            group = lv_group_create();
            lv_group_set_default(group);
        }

        keypad = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, keypad_device);
        if(keypad != NULL) {
            lv_indev_set_display(keypad, display);
            lv_indev_set_group(keypad, group);
            lv_indev_add_event_cb(keypad, navigation_key_cb, LV_EVENT_KEY, NULL);
            LV_LOG_USER("Using '%s' as evdev keypad.", keypad_device);
        }
        else {
            LV_LOG_WARN("Failed to open '%s' as evdev keypad.", keypad_device);
        }
    }

    // set_mouse_cursor_icon(indev, display);
    return pointer;
}

/* Back/Menu/Test on the R818 NEC remote are global navigation keys.  Handle
 * them at the input device so every app has a reliable escape route even if
 * its currently focused widget does not implement LV_EVENT_CANCEL. */
static void navigation_key_cb(lv_event_t * event)
{
    lv_indev_t * indev = lv_event_get_target(event);
    uint32_t key;

    if(lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) return;
    key = lv_indev_get_key(indev);
    if(key == LV_KEY_ESC || key == LV_KEY_HOME) {
        app_manager_back_home();
    }
}
#endif /*#if LV_USE_EVDEV*/
