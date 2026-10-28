#include "user_app.h"

#include "../../system/app_manager.h"
#include "user_app_config.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"
#include "../../ui/ui_i18n.h"
#include "../../ui/mimi_ui.h"

#include <dirent.h>
#include <errno.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define USER_APP_MAX 16
#define USER_APP_JSON_MAX (1024 * 1024)

typedef struct {
    char id[97];
    /* Manifest names allow 48 Unicode characters, up to four UTF-8 bytes each. */
    char name[48 * 4 + 1];
    char entry[256];
    uint32_t permissions;
} user_app_entry_t;

typedef struct {
    char app_id[97];
    char command[65];
    char action[65];
} user_action_t;

static lv_obj_t *screen;
static lv_obj_t *app_list;
static lv_obj_t *page_content;
static lv_obj_t *status_label;
static user_app_entry_t entries[USER_APP_MAX];
static size_t entry_count;
static bool refresh_pending;
static char selected_app_id[97];
static void refresh_apps_async(void *unused);
static void refresh_apps_event(lv_event_t *event)
{
    (void)event;
    if (!screen || refresh_pending) return;
    refresh_pending = true;
    lv_async_call(refresh_apps_async, NULL);
}
static uint32_t current_permissions;

static int64_t app_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int app_wait(int fd, short events, int64_t deadline)
{
    for (;;) {
        int64_t now = app_now_ms();
        if (now < 0 || now >= deadline) return -1;
        struct pollfd descriptor = {.fd = fd, .events = events};
        int ready = poll(&descriptor, 1, (int)(deadline - now));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || (descriptor.revents & (POLLERR | POLLNVAL))) return -1;
        return 0;
    }
}

static int app_write_all(int fd, const void *buffer, size_t length, int64_t deadline)
{
    const unsigned char *cursor = buffer;
    while (length > 0) {
        if (app_wait(fd, POLLOUT, deadline)) return -1;
        ssize_t count = send(fd, cursor, length, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

/* Strict JSON syntax is checked separately. Count root members before json-c
 * collapses duplicate decoded keys, including escaped aliases. Nested extension
 * fields are not interpreted by this client. NUL strings cannot be shown safely
 * through the C-string UI API, so reject them instead of hiding their suffix. */
static bool app_reply_members_valid(const char *data, size_t length, json_object *root)
{
    unsigned depth = 0, members = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char c = data[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') {
                if (length - i >= 6 && !memcmp(data + i, "\\u0000", 6)) return false;
                escaped = true;
            } else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            if (!depth) return false;
            --depth;
        } else if (c == ':' && depth == 1) ++members;
    }
    return members == (unsigned)json_object_object_length(root);
}

static json_object *app_request(json_object *request)
{
    const char *endpoint = mimi_user_app_config_get()->user_apps_socket_path;
    if(!*endpoint) return NULL;
    int64_t now = app_now_ms();
    if (now < 0) return NULL;
    int64_t deadline = now + 5000;
    int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (client < 0) return NULL;
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", endpoint);
    const char *text = json_object_to_json_string_ext(
        request, JSON_C_TO_STRING_PLAIN);
    if (connect(client, (struct sockaddr *)&address, sizeof(address))) {
        if (errno != EINPROGRESS || app_wait(client, POLLOUT, deadline)) {
            close(client); return NULL;
        }
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(client, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
            close(client); return NULL;
        }
    }
    if (app_write_all(client, text, strlen(text), deadline) ||
        app_write_all(client, "\n", 1, deadline)) {
        close(client); return NULL;
    }
    char response[4097];
    size_t used = 0;
    while (used < sizeof(response) - 1) {
        if (app_wait(client, POLLIN, deadline)) break;
        ssize_t count = recv(client, response + used,
                             sizeof(response) - 1 - used, MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) break;
        used += (size_t)count;
        if (memchr(response, '\n', used)) break;
    }
    close(client);
    response[used] = '\0';
    char *newline = memchr(response, '\n', used);
    if (!newline) return NULL;
    size_t length = (size_t)(newline - response);
    if (memchr(response, '\0', length)) return NULL;
    *newline = '\0';
    json_tokener *parser = json_tokener_new();
    if (!parser) return NULL;
    json_tokener_set_flags(parser, JSON_TOKENER_STRICT);
    json_object *result = json_tokener_parse_ex(parser, response, (int)length + 1);
    if (json_tokener_get_error(parser) != json_tokener_success ||
        (size_t)parser->char_offset < length ||
        !json_object_is_type(result, json_type_object) ||
        !app_reply_members_valid(response, length, result)) {
        if (result) json_object_put(result);
        result = NULL;
    }
    json_tokener_free(parser);
    return result;
}

enum {
    PERMISSION_CAPTURE = 1u << 0,
    PERMISSION_INPUT_KEYBOARD = 1u << 1,
    PERMISSION_INPUT_POINTER = 1u << 2,
    PERMISSION_GPIO_READ = 1u << 3,
    PERMISSION_GPIO_WRITE = 1u << 4,
    PERMISSION_I2C = 1u << 5,
    PERMISSION_SPI = 1u << 6,
    PERMISSION_UART = 1u << 7,
    PERMISSION_USB = 1u << 8,
    PERMISSION_NETWORK_HTTPS = 1u << 9,
    PERMISSION_STORAGE_APP = 1u << 10,
};

static uint32_t permission_bit(const char *permission)
{
    static const struct {
        const char *name;
        uint32_t bit;
    } permissions[] = {
        {"capture.snapshot", PERMISSION_CAPTURE},
        {"input.keyboard", PERMISSION_INPUT_KEYBOARD},
        {"input.pointer", PERMISSION_INPUT_POINTER},
        {"hardware.gpio.read", PERMISSION_GPIO_READ},
        {"hardware.gpio.write", PERMISSION_GPIO_WRITE},
        {"hardware.i2c", PERMISSION_I2C},
        {"hardware.spi", PERMISSION_SPI},
        {"hardware.uart", PERMISSION_UART},
        {"hardware.usb", PERMISSION_USB},
        {"network.https", PERMISSION_NETWORK_HTTPS},
        {"storage.app", PERMISSION_STORAGE_APP},
    };
    if (!permission) return 0;
    for (size_t i = 0; i < sizeof(permissions) / sizeof(permissions[0]); i++) {
        if (!strcmp(permission, permissions[i].name)) return permissions[i].bit;
    }
    return 0;
}

static bool regular_file(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_size > 0 && st.st_size <= USER_APP_JSON_MAX;
}

static bool page_string(json_object *value)
{
    return json_object_is_type(value, json_type_string) &&
           (size_t)json_object_get_string_len(value) == strlen(json_object_get_string(value));
}

static int compare_entries(const void *left, const void *right)
{
    const user_app_entry_t *a = left;
    const user_app_entry_t *b = right;
    int result = strcmp(a->name, b->name);
    return result ? result : strcmp(a->id, b->id);
}

static void scan_apps(void)
{
    const char *app_root = mimi_user_app_config_get()->user_apps_root;
    entry_count = 0;
    if(!*app_root) return;
    DIR *root = opendir(app_root);
    struct dirent *item;
    entry_count = 0;
    if (!root) return;

    while ((item = readdir(root)) != NULL && entry_count < USER_APP_MAX) {
        if (item->d_name[0] == '.') continue;
        char manifest_path[384];
        int path_size = snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.json",
                                 app_root, item->d_name);
        if(path_size < 0 || (size_t)path_size >= sizeof(manifest_path)) continue;
        if (!regular_file(manifest_path)) continue;
        json_object *manifest = json_object_from_file(manifest_path);
        if (!manifest) continue;
        json_object *id = NULL, *name = NULL, *ui = NULL, *permissions = NULL;
        json_object *entry = NULL, *presentation = NULL, *localizations = NULL;
        bool valid = json_object_object_get_ex(manifest, "id", &id) &&
                     page_string(id) &&
                     json_object_get_string_len(id) <= 96 &&
                     !strcmp(json_object_get_string(id), item->d_name) &&
                     json_object_object_get_ex(manifest, "name", &name) &&
                     page_string(name) &&
                     json_object_object_get_ex(manifest, "ui", &ui) &&
                     json_object_is_type(ui, json_type_object) &&
                     json_object_object_get_ex(manifest, "permissions", &permissions) &&
                     json_object_is_type(permissions, json_type_array) &&
                     json_object_object_get_ex(ui, "entry", &entry) &&
                     page_string(entry) &&
                     json_object_object_get_ex(ui, "presentation", &presentation) &&
                     page_string(presentation) &&
                     !strcmp(json_object_get_string(presentation), "embedded");
        if (valid) {
            const char *entry_text = json_object_get_string(entry);
            const char *localized_name_text = json_object_get_string(name);
            const char *locale_key = ui_i18n_is_zh() ? "zh-CN" : "en-US";
            if (json_object_object_get_ex(manifest, "localizations", &localizations) &&
                json_object_is_type(localizations, json_type_object)) {
                json_object *localized = NULL, *localized_name = NULL;
                json_object *localized_entry = NULL;
                if (json_object_object_get_ex(localizations, locale_key, &localized) &&
                    json_object_is_type(localized, json_type_object)) {
                    if (json_object_object_get_ex(localized, "name", &localized_name) &&
                        page_string(localized_name))
                        localized_name_text = json_object_get_string(localized_name);
                    if (json_object_object_get_ex(localized, "uiEntry", &localized_entry) &&
                        page_string(localized_entry))
                        entry_text = json_object_get_string(localized_entry);
                }
            }
            if (strlen(localized_name_text) >= sizeof(entries[0].name)) {
                json_object_put(manifest);
                continue;
            }
            if (!strncmp(entry_text, "ui/", 3) && !strstr(entry_text, "..")) {
                user_app_entry_t *target = &entries[entry_count];
                target->permissions = 0;
                size_t permission_count = json_object_array_length(permissions);
                for (size_t i = 0; i < permission_count; i++) {
                    json_object *permission = json_object_array_get_idx(permissions, i);
                    if (page_string(permission))
                        target->permissions |= permission_bit(
                            json_object_get_string(permission));
                }
                snprintf(target->id, sizeof(target->id), "%s",
                         json_object_get_string(id));
                snprintf(target->name, sizeof(target->name), "%s",
                         localized_name_text);
                size_t root_len = strlen(app_root);
                size_t dir_len = strlen(item->d_name);
                size_t file_len = strlen(entry_text);
                if (root_len + dir_len + file_len + 3 > sizeof(target->entry)) {
                    json_object_put(manifest);
                    continue;
                }
                char *out = target->entry;
                memcpy(out, app_root, root_len);
                out += root_len;
                *out++ = '/';
                memcpy(out, item->d_name, dir_len);
                out += dir_len;
                *out++ = '/';
                memcpy(out, entry_text, file_len + 1);
                if (regular_file(target->entry)) entry_count++;
            }
        }
        json_object_put(manifest);
    }
    closedir(root);
    qsort(entries, entry_count, sizeof(entries[0]), compare_entries);
}

static void set_plain_container(lv_obj_t *object)
{
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                      LV_OBJ_FLAG_SCROLL_CHAIN);
}

/* Worker owns only JSON; all page pointers and generation are UI-thread owned. */
static pthread_mutex_t action_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool action_active, action_done;
static json_object *action_reply;
static uint64_t page_generation, action_generation;
static bool action_invoke;

static void show_action_result(json_object *response, bool invoke)
{
    json_object *ok = NULL, *message = NULL;
    bool success = response && json_object_object_get_ex(response, "ok", &ok) &&
        json_object_is_type(ok, json_type_boolean) && json_object_get_boolean(ok);
    bool has_message = response && json_object_object_get_ex(response, "message", &message) &&
        json_object_is_type(message, json_type_string);
    if (invoke)
        lv_label_set_text(status_label, success && has_message ? json_object_get_string(message) :
                          ui_tr(UI_STR_USER_LAUNCH_FAILED));
    else if (success)
        lv_label_set_text(status_label, ui_tr(UI_STR_USER_REQUEST_COMPLETE));
    else
        lv_label_set_text(status_label, has_message ? json_object_get_string(message) :
                          ui_tr(UI_STR_USER_HARDWARE_FAILED));
}

static void *action_worker(void *argument)
{
    json_object *request = argument;
    json_object *response = app_request(request);
    json_object_put(request);
    pthread_mutex_lock(&action_mutex);
    action_reply = response;
    action_done = true;
    pthread_mutex_unlock(&action_mutex);
    return NULL;
}

static void action_completed(lv_timer_t *timer)
{
    pthread_mutex_lock(&action_mutex);
    bool done = action_done;
    json_object *response = done ? action_reply : NULL;
    if (done) { action_reply = NULL; action_done = false; }
    pthread_mutex_unlock(&action_mutex);
    if (!done) return;
    action_active = false;
    lv_timer_delete(timer);
    if (status_label && page_generation == action_generation)
        show_action_result(response, action_invoke);
    if (response) json_object_put(response);
}

static void action_event(lv_event_t *event)
{
    user_action_t *action = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !status_label) return;
    /* Never replay a potentially side-effecting command while its outcome is pending. */
    if (action_active) return;
    bool invoke = !strcmp(action->command, "app.invoke");
    uint32_t required = permission_bit(action->command);
    if (!invoke && (!required || !(current_permissions & required))) {
        lv_label_set_text(status_label, ui_tr(UI_STR_USER_NOT_AUTHORIZED));
        return;
    }
    json_object *request = json_object_new_object();
    if (!request) { show_action_result(NULL, invoke); return; }
    json_object_object_add(request, "command", json_object_new_string(invoke ? "invoke" : "platformAction"));
    json_object_object_add(request, "appId", json_object_new_string(action->app_id));
    if (invoke)
        json_object_object_add(request, "action", json_object_new_string(action->action));
    else {
        json_object_object_add(request, "capability", json_object_new_string(action->command));
        json_object_object_add(request, "arguments", json_object_new_object());
    }
    /* Resolve configuration on the UI thread before the worker reads it. */
    (void)mimi_user_app_config_get();
    lv_timer_t *timer = lv_timer_create(action_completed, 20, NULL);
    pthread_t thread;
    action_generation = page_generation;
    action_invoke = invoke;
    action_active = true;
    if (!timer || pthread_create(&thread, NULL, action_worker, request)) {
        if (timer) lv_timer_delete(timer);
        action_active = false;
        json_object_put(request);
        show_action_result(NULL, invoke);
        return;
    }
    pthread_detach(thread);
    lv_label_set_text(status_label, ui_i18n_is_zh() ? "正在处理，请稍候…" : "Working, please wait...");
}

static void action_delete_event(lv_event_t *event)
{
    lv_free(lv_event_get_user_data(event));
}

static bool page_node_valid(json_object *node, unsigned depth, size_t *count)
{
    json_object *type = NULL, *value = NULL;
    if (depth > 8 || ++(*count) > 128 || !json_object_is_type(node, json_type_object) ||
        !json_object_object_get_ex(node, "type", &type) || !page_string(type)) return false;
    const char *name = json_object_get_string(type);
    if (!strcmp(name, "text"))
        return json_object_object_get_ex(node, "text", &value) && page_string(value);
    if (!strcmp(name, "action")) {
        json_object *command = NULL;
        if (!json_object_object_get_ex(node, "label", &value) || !page_string(value) ||
            !json_object_object_get_ex(node, "command", &command) || !page_string(command) ||
            json_object_get_string_len(command) > 64) return false;
        if (!strcmp(json_object_get_string(command), "app.invoke")) {
            json_object *arguments = NULL;
            if (!json_object_object_get_ex(node, "arguments", &arguments) ||
                !json_object_is_type(arguments, json_type_object) ||
                !json_object_object_get_ex(arguments, "name", &value) || !page_string(value) ||
                json_object_get_string_len(value) > 64) return false;
        }
        return true;
    }
    if (!strcmp(name, "column")) {
        if (!json_object_object_get_ex(node, "children", &value) ||
            !json_object_is_type(value, json_type_array)) return false;
        size_t children = json_object_array_length(value);
        if (children > 64) return false;
        for (size_t i = 0; i < children; ++i)
            if (!page_node_valid(json_object_array_get_idx(value, i), depth + 1, count)) return false;
        return true;
    }
    return false;
}

static bool page_valid(json_object *page)
{
    json_object *title = NULL, *layout = NULL;
    size_t count = 0;
    if (!json_object_is_type(page, json_type_object)) return false;
    if (json_object_object_get_ex(page, "title", &title) && !page_string(title)) return false;
    return json_object_object_get_ex(page, "layout", &layout) && page_node_valid(layout, 1, &count);
}

static void render_node(lv_obj_t *parent, json_object *node, unsigned depth,
                        size_t *node_count, const char *app_id)
{
    json_object *type_obj = NULL;
    if (depth > 8 || ++(*node_count) > 128) return;
    if (!node || !json_object_object_get_ex(node, "type", &type_obj)) return;
    const char *type = json_object_get_string(type_obj);
    if (!strcmp(type, "text")) {
        json_object *text_obj = NULL;
        if (!json_object_object_get_ex(node, "text", &text_obj)) return;
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, json_object_get_string(text_obj));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_font(label, UI_TEXT_BODY_LG, 0);
        lv_obj_set_style_text_color(label, UI_TEXT_PRIMARY, 0);
    } else if (!strcmp(type, "action")) {
        json_object *label_obj = NULL, *command_obj = NULL;
        if (!json_object_object_get_ex(node, "label", &label_obj) ||
            !json_object_object_get_ex(node, "command", &command_obj) ||
            !json_object_is_type(label_obj, json_type_string) ||
            !json_object_is_type(command_obj, json_type_string)) return;
        const char *command = json_object_get_string(command_obj);
        user_action_t *action = lv_malloc(sizeof(*action));
        if (!action) return;
        memset(action, 0, sizeof(*action));
        snprintf(action->app_id, sizeof(action->app_id), "%s", app_id);
        snprintf(action->command, sizeof(action->command), "%s", command);
        if (!strcmp(command, "app.invoke")) {
            json_object *arguments = NULL, *name = NULL;
            if (!json_object_object_get_ex(node, "arguments", &arguments) ||
                !json_object_is_type(arguments, json_type_object) ||
                !json_object_object_get_ex(arguments, "name", &name) ||
                !json_object_is_type(name, json_type_string)) {
                lv_free(action);
                return;
            }
            snprintf(action->action, sizeof(action->action), "%s",
                     json_object_get_string(name));
        }
        lv_obj_t *button = lv_button_create(parent);
        mimi_ui_button_style(button, true);
        lv_obj_set_size(button, lv_pct(100), 48);
        lv_obj_set_style_max_width(button, 520, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                          LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(button, action_event, LV_EVENT_CLICKED, action);
        lv_obj_add_event_cb(button, action_delete_event, LV_EVENT_DELETE, action);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, json_object_get_string(label_obj));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, lv_pct(100));
        /* User-provided labels may contain CJK text. The LVGL default button
         * font only covers a small Latin set on this image. */
        lv_obj_set_style_text_font(label, UI_TEXT_BODY_LG, 0);

        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    } else if (!strcmp(type, "column")) {
        json_object *children = NULL;
        if (!json_object_object_get_ex(node, "children", &children) ||
            !json_object_is_type(children, json_type_array)) return;
        size_t count = json_object_array_length(children);
        if (count > 64) return;
        for (size_t i = 0; i < count; i++)
            render_node(parent, json_object_array_get_idx(children, i),
                        depth + 1, node_count, app_id);
    }
}

static void clear_content(void)
{
    ++page_generation;
    if (page_content) lv_obj_clean(page_content);
    status_label = NULL;
}

static void open_entry(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= entry_count) return;
    for (size_t i = 0; i < entry_count; ++i) {
        lv_obj_t *row = lv_obj_get_child(app_list, (int32_t)i);
        if (i == index) lv_obj_add_state(row, LV_STATE_CHECKED);
        else lv_obj_clear_state(row, LV_STATE_CHECKED);
    }
    snprintf(selected_app_id, sizeof(selected_app_id), "%s", entries[index].id);
    bool available = regular_file(entries[index].entry);
    json_object *page = available ? json_object_from_file(entries[index].entry) : NULL;
    clear_content();
    current_permissions = entries[index].permissions;
    if (!page_valid(page)) {
        status_label = lv_label_create(page_content);
        lv_label_set_text(status_label, !available ?
            (ui_i18n_is_zh() ? "应用页面不可用，请重新安装应用。" : "App page unavailable. Please reinstall the app.") :
            (ui_i18n_is_zh() ? "应用页面格式无效，请重新安装应用。" : "Invalid app page. Please reinstall the app."));
        lv_obj_set_width(status_label, lv_pct(100));
        lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(status_label, UI_TEXT_BODY_LG, 0);
        lv_obj_set_style_text_color(status_label, UI_TEXT_PRIMARY, 0);
        if (page) json_object_put(page);
        return;
    }
    json_object *title = NULL, *layout = NULL;
    if (json_object_object_get_ex(page, "title", &title)) {
        lv_obj_t *heading = lv_label_create(page_content);
        lv_label_set_text(heading, json_object_get_string(title));
        lv_obj_set_width(heading, lv_pct(100));
        lv_label_set_long_mode(heading, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(heading, UI_TEXT_H2, 0);
        lv_obj_set_style_text_color(heading, UI_TEXT_PRIMARY, 0);
    }
    if (json_object_object_get_ex(page, "layout", &layout)) {
        size_t node_count = 0;
        render_node(page_content, layout, 1, &node_count, entries[index].id);
    }
    status_label = lv_label_create(page_content);
    lv_label_set_text(status_label, "");
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_font(status_label, UI_TEXT_BODY_LG, 0);
    lv_obj_set_style_text_color(status_label, UI_TEXT_SECONDARY, 0);
    json_object_put(page);
}

static void back_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) app_manager_back_home();
}

void user_app_init(void)
{
    scan_apps();
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, UI_BG_DESKTOP, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                      LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_t *header = mimi_ui_header(screen, ui_tr(UI_STR_APP_USER), back_event);
    lv_obj_t *refresh = lv_button_create(header);
    mimi_ui_button_style(refresh, false);
    lv_obj_set_size(refresh, 116, 48);
    lv_obj_align(refresh, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(refresh, refresh_apps_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, ui_i18n_is_zh() ? "刷新列表" : "Refresh");
    lv_obj_set_style_text_font(refresh_label, UI_TEXT_BODY_MD, 0);
    lv_obj_center(refresh_label);

    lv_obj_t *body = lv_obj_create(screen);
    int32_t display_width = lv_display_get_horizontal_resolution(NULL);
    int32_t display_height = lv_display_get_vertical_resolution(NULL);
    int32_t body_height = LV_MAX(1, display_height - MIMI_HEADER_H);
    lv_obj_set_size(body, display_width, body_height);
    lv_obj_set_pos(body, 0, MIMI_HEADER_H);
    set_plain_container(body);
    lv_obj_set_layout(body, LV_LAYOUT_NONE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                      LV_OBJ_FLAG_SCROLL_CHAIN);

    app_list = lv_obj_create(body);
    int32_t body_width = LV_MIN(912, display_width - 48);
    int32_t side_margin = (display_width - body_width) / 2;
    int32_t list_width = body_width / 4;
    if (list_width < 160) list_width = 160;
    if (list_width > 280) list_width = 280;
    int32_t outer_pad = UI_SPACE_LG;
    int32_t column_gap = UI_SPACE_LG;
    int32_t content_height = LV_MAX(1, body_height - 2 * outer_pad);
    int32_t content_x = side_margin + list_width + column_gap;
    int32_t content_width = LV_MAX(1, display_width - content_x - side_margin);
    lv_obj_set_pos(app_list, side_margin, outer_pad);
    lv_obj_set_style_bg_color(app_list, UI_DESKTOP_CARD_BG, 0);
    lv_obj_set_style_border_color(app_list, UI_DESKTOP_BORDER, 0);
    lv_obj_set_size(app_list, list_width, content_height);
    lv_obj_set_style_radius(app_list, UI_RADIUS_MD, 0);
    lv_obj_set_style_clip_corner(app_list, true, 0);
    lv_obj_set_style_pad_all(app_list, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(app_list, UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(app_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(app_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(app_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(app_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                      LV_OBJ_FLAG_SCROLL_CHAIN);

    page_content = lv_obj_create(body);
    lv_obj_set_pos(page_content, content_x, outer_pad);
    lv_obj_set_size(page_content, content_width, content_height);
    set_plain_container(page_content);
    lv_obj_set_style_bg_color(page_content, UI_DESKTOP_CARD_BG, 0);
    lv_obj_set_style_bg_opa(page_content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_content, 1, 0);
    lv_obj_set_style_border_color(page_content, UI_DESKTOP_BORDER, 0);
    lv_obj_set_style_radius(page_content, UI_RADIUS_MD, 0);
    lv_obj_set_style_clip_corner(page_content, true, 0);
    lv_obj_set_style_pad_all(page_content, UI_SPACE_LG, 0);
    lv_obj_set_style_pad_row(page_content, UI_SPACE_MD, 0);
    lv_obj_set_flex_flow(page_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(page_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(page_content, LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                      LV_OBJ_FLAG_SCROLL_CHAIN);

    for (size_t i = 0; i < entry_count; i++) {
        lv_obj_t *button = lv_button_create(app_list);
        mimi_ui_button_style(button, false);
        lv_obj_set_style_bg_color(button, mimi_ui_color(MIMI_CORAL), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_CHECKED);
        lv_obj_set_size(button, lv_pct(100), 52);
        lv_obj_set_style_pad_ver(button, UI_SPACE_SM, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_border_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(button, mimi_ui_color(MIMI_CORAL), LV_STATE_CHECKED);
        lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                          LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(button, open_entry, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, entries[i].name);
        lv_obj_set_height(label, lv_font_get_line_height(UI_TEXT_BODY_LG));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_font(label, UI_TEXT_BODY_LG, 0);

        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    }
    if (entry_count == 0) {
        lv_obj_add_flag(app_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(page_content, side_margin, outer_pad);
        lv_obj_set_width(page_content, body_width);
        lv_obj_set_flex_align(page_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *label = lv_label_create(page_content);
        lv_label_set_text(label, ui_tr(UI_STR_USER_NO_APPS));
        lv_obj_set_style_text_font(label, UI_TEXT_H2, 0);
        lv_obj_set_style_text_color(label, UI_TEXT_PRIMARY, 0);
        lv_obj_t *hint = lv_label_create(page_content);
        lv_label_set_text(hint, ui_i18n_is_zh()
                          ? "安装应用后，点击右上角“刷新列表”即可查看。"
                          : "After installing an app, choose Refresh to load it here.");
        lv_obj_set_width(hint, lv_pct(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(hint, UI_TEXT_BODY_LG, 0);
        lv_obj_set_style_text_color(hint, UI_TEXT_SECONDARY, 0);
    } else {
        lv_obj_send_event(lv_obj_get_child(app_list, 0), LV_EVENT_CLICKED, NULL);
    }
    lv_scr_load(screen);
}

static void refresh_apps_async(void *unused)
{
    (void)unused;
    refresh_pending = false;
    if (!screen) return;
    char previous[sizeof(selected_app_id)];
    snprintf(previous, sizeof(previous), "%s", selected_app_id);
    lv_obj_t *old = screen;
    user_app_close();
    user_app_init();
    for (size_t i = 0; i < entry_count; ++i) {
        if (!strcmp(entries[i].id, previous)) {
            lv_obj_t *row = lv_obj_get_child(app_list, (int32_t)i);
            lv_obj_send_event(row, LV_EVENT_CLICKED, NULL);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
            break;
        }
    }
    lv_obj_del_async(old);
}

void user_app_close(void)
{
    if (refresh_pending) lv_async_call_cancel(refresh_apps_async, NULL);
    refresh_pending = false;
    selected_app_id[0] = '\0';
    ++page_generation;
    screen = NULL;
    app_list = NULL;
    page_content = NULL;
    status_label = NULL;
    current_permissions = 0;
    entry_count = 0;
}

AppDescriptor app_user = {
    .id = APP_ID_USER_APPS,
    .name = "User Apps",
    .init = user_app_init,
    .close = user_app_close,
};
