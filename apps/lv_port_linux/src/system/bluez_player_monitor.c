#include "bluez_player_monitor.h"
#include "log/app_log.h"
#include <dbus/dbus.h>
#include <string.h>

#define BLUEZ_MEDIA_PLAYER_IFACE "org.bluez.MediaPlayer1"

static DBusConnection *monitor_connection;
static bluez_player_state_cb_t state_callback;

static bool playback_state_from_text(const char *text,
                                     bt_playback_state_t *state) {
    if (!text || !state) return false;
    if (strcmp(text, "playing") == 0) {
        *state = BT_PLAYBACK_PLAYING;
    } else if (strcmp(text, "paused") == 0) {
        *state = BT_PLAYBACK_PAUSED;
    } else if (strcmp(text, "stopped") == 0) {
        *state = BT_PLAYBACK_STOPPED;
    } else {
        return false;
    }
    return true;
}

static void handle_properties_changed(DBusMessage *message) {
    DBusMessageIter args;
    DBusMessageIter changed;
    const char *interface_name = NULL;
    const char *object_path = dbus_message_get_path(message);

    if (!dbus_message_iter_init(message, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return;
    dbus_message_iter_get_basic(&args, &interface_name);
    if (!interface_name || strcmp(interface_name, BLUEZ_MEDIA_PLAYER_IFACE) != 0) return;

    if (!dbus_message_iter_next(&args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) return;
    dbus_message_iter_recurse(&args, &changed);

    while (dbus_message_iter_get_arg_type(&changed) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        DBusMessageIter variant;
        const char *key = NULL;

        dbus_message_iter_recurse(&changed, &entry);
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&entry, &key);
        }
        if (key && strcmp(key, "Status") == 0 &&
            dbus_message_iter_next(&entry) &&
            dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
            const char *status_text = NULL;
            bt_playback_state_t state;
            dbus_message_iter_recurse(&entry, &variant);
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&variant, &status_text);
                if (playback_state_from_text(status_text, &state) && state_callback) {
                    state_callback(true, state, object_path);
                }
            }
        }
        dbus_message_iter_next(&changed);
    }
}

static bool interfaces_removed_contains_player(DBusMessage *message,
                                               const char **object_path) {
    DBusMessageIter args;
    DBusMessageIter interfaces;

    if (!dbus_message_iter_init(message, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH) return false;
    dbus_message_iter_get_basic(&args, object_path);
    if (!dbus_message_iter_next(&args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) return false;
    dbus_message_iter_recurse(&args, &interfaces);
    while (dbus_message_iter_get_arg_type(&interfaces) == DBUS_TYPE_STRING) {
        const char *interface_name = NULL;
        dbus_message_iter_get_basic(&interfaces, &interface_name);
        if (interface_name && strcmp(interface_name, BLUEZ_MEDIA_PLAYER_IFACE) == 0) {
            return true;
        }
        dbus_message_iter_next(&interfaces);
    }
    return false;
}

bool bluez_player_monitor_init(bluez_player_state_cb_t callback) {
    DBusError error;
    const char *properties_rule =
        "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',arg0='org.bluez.MediaPlayer1'";
    const char *removed_rule =
        "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager',"
        "member='InterfacesRemoved'";

    if (monitor_connection) return true;
    dbus_error_init(&error);
    monitor_connection = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
    if (!monitor_connection) {
        APP_LOGW("bt", "BlueZ player monitor unavailable: %s",
                 error.message ? error.message : "system bus connection failed");
        dbus_error_free(&error);
        return false;
    }

    dbus_connection_set_exit_on_disconnect(monitor_connection, false);
    state_callback = callback;
    dbus_bus_add_match(monitor_connection, properties_rule, &error);
    if (!dbus_error_is_set(&error)) {
        dbus_bus_add_match(monitor_connection, removed_rule, &error);
    }
    if (dbus_error_is_set(&error)) {
        APP_LOGW("bt", "BlueZ player monitor match failed: %s", error.message);
        dbus_error_free(&error);
        bluez_player_monitor_deinit();
        return false;
    }
    dbus_connection_flush(monitor_connection);
    APP_LOGI("bt", "BlueZ MediaPlayer1 monitor initialized");
    return true;
}

void bluez_player_monitor_update(void) {
    DBusMessage *message;

    if (!monitor_connection) return;
    dbus_connection_read_write(monitor_connection, 0);
    while ((message = dbus_connection_pop_message(monitor_connection)) != NULL) {
        if (dbus_message_is_signal(message,
                                   "org.freedesktop.DBus.Properties",
                                   "PropertiesChanged")) {
            handle_properties_changed(message);
        } else if (dbus_message_is_signal(message,
                                          "org.freedesktop.DBus.ObjectManager",
                                          "InterfacesRemoved")) {
            const char *object_path = NULL;
            if (interfaces_removed_contains_player(message, &object_path) && state_callback) {
                state_callback(false, BT_PLAYBACK_UNKNOWN, object_path);
            }
        }
        dbus_message_unref(message);
    }
}

void bluez_player_monitor_deinit(void) {
    if (monitor_connection) {
        dbus_connection_close(monitor_connection);
        dbus_connection_unref(monitor_connection);
        monitor_connection = NULL;
    }
    state_callback = NULL;
}
