#include "user_app.h"

#include "../../system/app_manager.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"

#include <dirent.h>
#include <errno.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define USER_APP_ROOT "/overlay/aitvbox/apps"
#define USER_APP_SOCKET "/var/run/aitvbox/apps.sock"
#define USER_APP_MAX 16
#define USER_APP_JSON_MAX (1024 * 1024)

typedef struct {
    char id[97];
    char name[49];
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
static uint32_t current_permissions;

static int write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static json_object *app_request(json_object *request)
{
    int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client < 0) return NULL;
    struct timeval timeout = {.tv_sec = 5};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", USER_APP_SOCKET);
    const char *text = json_object_to_json_string_ext(
        request, JSON_C_TO_STRING_PLAIN);
    if (connect(client, (struct sockaddr *)&address, sizeof(address)) ||
        write_all(client, text, strlen(text)) || write_all(client, "\n", 1)) {
        close(client);
        return NULL;
    }
    char response[4097];
    size_t used = 0;
    while (used < sizeof(response) - 1) {
        ssize_t count = read(client, response + used,
                             sizeof(response) - 1 - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
        if (memchr(response, '\n', used)) break;
    }
    close(client);
    response[used] = '\0';
    char *newline = memchr(response, '\n', used);
    if (!newline) return NULL;
    *newline = '\0';
    return json_tokener_parse(response);
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

static int compare_entries(const void *left, const void *right)
{
    const user_app_entry_t *a = left;
    const user_app_entry_t *b = right;
    int result = strcmp(a->name, b->name);
    return result ? result : strcmp(a->id, b->id);
}

static void scan_apps(void)
{
    DIR *root = opendir(USER_APP_ROOT);
    struct dirent *item;
    entry_count = 0;
    if (!root) return;

    while ((item = readdir(root)) != NULL && entry_count < USER_APP_MAX) {
        if (item->d_name[0] == '.') continue;
        char manifest_path[384];
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.json",
                 USER_APP_ROOT, item->d_name);
        if (!regular_file(manifest_path)) continue;
        json_object *manifest = json_object_from_file(manifest_path);
        if (!manifest) continue;
        json_object *id = NULL, *name = NULL, *ui = NULL, *permissions = NULL;
        json_object *entry = NULL, *presentation = NULL;
        bool valid = json_object_object_get_ex(manifest, "id", &id) &&
                     json_object_is_type(id, json_type_string) &&
                     !strcmp(json_object_get_string(id), item->d_name) &&
                     json_object_object_get_ex(manifest, "name", &name) &&
                     json_object_is_type(name, json_type_string) &&
                     json_object_object_get_ex(manifest, "ui", &ui) &&
                     json_object_is_type(ui, json_type_object) &&
                     json_object_object_get_ex(manifest, "permissions", &permissions) &&
                     json_object_is_type(permissions, json_type_array) &&
                     json_object_object_get_ex(ui, "entry", &entry) &&
                     json_object_is_type(entry, json_type_string) &&
                     json_object_object_get_ex(ui, "presentation", &presentation) &&
                     json_object_is_type(presentation, json_type_string) &&
                     !strcmp(json_object_get_string(presentation), "embedded");
        if (valid) {
            const char *entry_text = json_object_get_string(entry);
            if (!strncmp(entry_text, "ui/", 3) && !strstr(entry_text, "..")) {
                user_app_entry_t *target = &entries[entry_count];
                target->permissions = 0;
                size_t permission_count = json_object_array_length(permissions);
                for (size_t i = 0; i < permission_count; i++) {
                    json_object *permission = json_object_array_get_idx(permissions, i);
                    if (json_object_is_type(permission, json_type_string))
                        target->permissions |= permission_bit(
                            json_object_get_string(permission));
                }
                snprintf(target->id, sizeof(target->id), "%s",
                         json_object_get_string(id));
                snprintf(target->name, sizeof(target->name), "%s",
                         json_object_get_string(name));
                size_t root_len = strlen(USER_APP_ROOT);
                size_t dir_len = strlen(item->d_name);
                size_t file_len = strlen(entry_text);
                if (root_len + dir_len + file_len + 3 > sizeof(target->entry)) {
                    json_object_put(manifest);
                    continue;
                }
                char *out = target->entry;
                memcpy(out, USER_APP_ROOT, root_len);
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

static void action_event(lv_event_t *event)
{
    user_action_t *action = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !status_label) return;
    if (!strcmp(action->command, "app.invoke")) {
        json_object *request = json_object_new_object();
        json_object_object_add(request, "command", json_object_new_string("invoke"));
        json_object_object_add(request, "appId",
                               json_object_new_string(action->app_id));
        json_object_object_add(request, "action",
                               json_object_new_string(action->action));
        json_object *response = app_request(request);
        json_object_put(request);
        json_object *ok = NULL, *message = NULL;
        if (!response ||
            !json_object_object_get_ex(response, "ok", &ok) ||
            !json_object_get_boolean(ok) ||
            !json_object_object_get_ex(response, "message", &message) ||
            !json_object_is_type(message, json_type_string)) {
            lv_label_set_text(status_label, "Could not launch the app");
        } else {
            lv_label_set_text(status_label, json_object_get_string(message));
        }
        if (response) json_object_put(response);
        return;
    }
    uint32_t required = permission_bit(action->command);
    if (!required || !(current_permissions & required)) {
        lv_label_set_text(status_label, "Action not authorized");
        return;
    }
    json_object *request = json_object_new_object();
    json_object_object_add(request, "command",
                           json_object_new_string("platformAction"));
    json_object_object_add(request, "appId",
                           json_object_new_string(action->app_id));
    json_object_object_add(request, "capability",
                           json_object_new_string(action->command));
    json_object_object_add(request, "arguments", json_object_new_object());
    json_object *response = app_request(request);
    json_object_put(request);
    json_object *ok = NULL, *message = NULL;
    if (!response || !json_object_object_get_ex(response, "ok", &ok) ||
        !json_object_get_boolean(ok)) {
        if (response &&
            json_object_object_get_ex(response, "message", &message) &&
            json_object_is_type(message, json_type_string))
            lv_label_set_text(status_label, json_object_get_string(message));
        else
            lv_label_set_text(status_label, "Hardware request failed");
    } else {
        lv_label_set_text(status_label, "Request completed");
    }
    if (response) json_object_put(response);
}

static void action_delete_event(lv_event_t *event)
{
    lv_free(lv_event_get_user_data(event));
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
        lv_obj_set_style_text_color(label, UI_TEXT_INVERSE, 0);
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
    if (page_content) lv_obj_clean(page_content);
    status_label = NULL;
}

static void open_entry(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= entry_count) return;
    if (!regular_file(entries[index].entry)) return;
    json_object *page = json_object_from_file(entries[index].entry);
    clear_content();
    current_permissions = entries[index].permissions;
    if (!page) return;
    json_object *title = NULL, *layout = NULL;
    if (json_object_object_get_ex(page, "title", &title)) {
        lv_obj_t *heading = lv_label_create(page_content);
        lv_label_set_text(heading, json_object_get_string(title));
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
    ui_create_app_header(screen, "User Apps", back_event, false, NULL);

    lv_obj_t *body = lv_obj_create(screen);
    int32_t display_width = lv_display_get_horizontal_resolution(NULL);
    int32_t display_height = lv_display_get_vertical_resolution(NULL);
    int32_t body_height = LV_MAX(1, display_height - UI_APP_HEADER_H);
    lv_obj_set_size(body, display_width, body_height);
    lv_obj_set_pos(body, 0, UI_APP_HEADER_H);
    set_plain_container(body);
    lv_obj_set_layout(body, LV_LAYOUT_NONE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                      LV_OBJ_FLAG_SCROLL_CHAIN);

    app_list = lv_obj_create(body);
    int32_t list_width = display_width / 4;
    if (list_width < 160) list_width = 160;
    if (list_width > 280) list_width = 280;
    int32_t outer_pad = UI_SPACE_LG;
    int32_t column_gap = UI_SPACE_LG;
    int32_t content_height = LV_MAX(1, body_height - 2 * outer_pad);
    int32_t content_x = outer_pad + list_width + column_gap;
    int32_t content_width = LV_MAX(1, display_width - content_x - outer_pad);
    lv_obj_set_pos(app_list, outer_pad, outer_pad);
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
    lv_obj_set_style_pad_all(page_content, UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(page_content, UI_SPACE_MD, 0);
    lv_obj_set_flex_flow(page_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(page_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(page_content, LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                      LV_OBJ_FLAG_SCROLL_CHAIN);

    for (size_t i = 0; i < entry_count; i++) {
        lv_obj_t *button = lv_button_create(app_list);
        lv_obj_set_size(button, lv_pct(100), 52);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_OVERFLOW_VISIBLE |
                          LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(button, open_entry, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, entries[i].name);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_font(label, UI_TEXT_BODY_LG, 0);
        lv_obj_set_style_text_color(label, UI_TEXT_INVERSE, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
    }
    if (entry_count == 0) {
        lv_obj_t *label = lv_label_create(page_content);
        lv_label_set_text(label, "No apps installed");
        lv_obj_set_style_text_font(label, UI_TEXT_BODY_LG, 0);
        lv_obj_set_style_text_color(label, UI_TEXT_SECONDARY, 0);
    } else {
        lv_obj_send_event(lv_obj_get_child(app_list, 0), LV_EVENT_CLICKED, NULL);
    }
    lv_scr_load(screen);
}

void user_app_close(void)
{
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
