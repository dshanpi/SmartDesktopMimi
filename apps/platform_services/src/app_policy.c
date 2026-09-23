#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_JSON_BYTES (1024 * 1024)
#define MAX_UI_DEPTH 8
#define MAX_UI_NODES 128

static const char *const permissions[] = {
    "capture.snapshot",
    "input.keyboard",
    "input.pointer",
    "hardware.gpio.read",
    "hardware.gpio.write",
    "hardware.i2c",
    "hardware.spi",
    "hardware.uart",
    "hardware.usb",
    "network.https",
    "storage.app",
};

struct manifest_policy {
    bool granted[sizeof(permissions) / sizeof(permissions[0])];
    int schema;
    const char *ui_entry;
    const char *runtime_entry;
};

static int fail(const char *message)
{
    fprintf(stderr, "aitvbox-app-policy: %s\n", message);
    return 1;
}

static bool exact_keys(json_object *object, const char *const *allowed,
                       size_t allowed_count)
{
    if (!json_object_is_type(object, json_type_object))
        return false;
    json_object_object_foreach(object, key, value) {
        (void)value;
        bool found = false;
        for (size_t i = 0; i < allowed_count; i++) {
            if (!strcmp(key, allowed[i])) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static bool get_value(json_object *object, const char *name,
                      enum json_type type, json_object **value)
{
    return json_object_object_get_ex(object, name, value) &&
           json_object_is_type(*value, type);
}

static bool bounded_string(json_object *object, const char *name,
                           size_t minimum, size_t maximum, const char **value)
{
    json_object *item = NULL;
    if (!get_value(object, name, json_type_string, &item))
        return false;
    size_t length = (size_t)json_object_get_string_len(item);
    const char *text = json_object_get_string(item);
    if (length < minimum || length > maximum || strlen(text) != length)
        return false;
    *value = text;
    return true;
}

static bool regular_file(const char *path, off_t maximum)
{
    struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode) &&
           st.st_size > 0 && st.st_size <= maximum;
}

static json_object *read_json(const char *path)
{
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > MAX_JSON_BYTES)
        return NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return NULL;
    size_t length = (size_t)st.st_size;
    char *data = malloc(length + 1);
    if (!data) {
        close(fd);
        return NULL;
    }
    size_t used = 0;
    while (used < length) {
        ssize_t count = read(fd, data + used, length - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
    }
    close(fd);
    data[used] = '\0';
    if (used != length) {
        free(data);
        return NULL;
    }

    struct json_tokener *tokener = json_tokener_new();
    if (!tokener) {
        free(data);
        return NULL;
    }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    json_object *result = json_tokener_parse_ex(tokener, data, (int)length);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    size_t end = (size_t)tokener->char_offset;
    while (end < length &&
           (data[end] == ' ' || data[end] == '\t' ||
            data[end] == '\r' || data[end] == '\n'))
        end++;
    if (error != json_tokener_success || end != length) {
        if (result)
            json_object_put(result);
        result = NULL;
    }
    json_tokener_free(tokener);
    free(data);
    return result;
}

static bool valid_app_id(const char *value)
{
    size_t length = strlen(value);
    if (length < 3 || length > 96 || value[0] < 'a' || value[0] > 'z')
        return false;
    bool dot = false;
    bool segment_start = true;
    for (size_t i = 0; i < length; i++) {
        char c = value[i];
        if (c == '.') {
            if (segment_start || i + 1 == length)
                return false;
            dot = true;
            segment_start = true;
        } else if (segment_start) {
            if ((c < 'a' || c > 'z') && (c < '0' || c > '9'))
                return false;
            segment_start = false;
        } else if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '-') {
            return false;
        }
    }
    return dot;
}

static bool parse_version(const char *value, uint32_t parts[3])
{
    const char *cursor = value;
    for (int i = 0; i < 3; i++) {
        if (*cursor < '0' || *cursor > '9')
            return false;
        errno = 0;
        char *end = NULL;
        unsigned long number = strtoul(cursor, &end, 10);
        if (errno || number > UINT32_MAX || end == cursor)
            return false;
        parts[i] = (uint32_t)number;
        if (i < 2) {
            if (*end != '.')
                return false;
            cursor = end + 1;
        } else if (*end) {
            return false;
        }
    }
    return true;
}

static bool valid_publisher(const char *value)
{
    size_t length = strlen(value);
    if (!length || length > 64)
        return false;
    for (size_t i = 0; i < length; i++) {
        char c = value[i];
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') &&
            (c < '0' || c > '9') && c != '.' && c != '_' && c != '-')
            return false;
    }
    return true;
}

static bool valid_ui_entry(const char *value)
{
    size_t length = strlen(value);
    if (length < 9 || length > 192 || strncmp(value, "ui/", 3) ||
        strcmp(value + length - 5, ".json"))
        return false;
    const char *segment = value;
    for (const char *cursor = value; ; cursor++) {
        char c = *cursor;
        if (c == '/' || c == '\0') {
            size_t size = (size_t)(cursor - segment);
            if (!size || (size == 1 && segment[0] == '.') ||
                (size == 2 && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (!c)
                break;
            segment = cursor + 1;
        } else if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') &&
                   (c < '0' || c > '9') && c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

static bool valid_runtime_entry(const char *value)
{
    size_t length = strlen(value);
    if (length < 5 || length > 68 || strncmp(value, "bin/", 4))
        return false;
    for (size_t i = 4; i < length; i++) {
        char c = value[i];
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') &&
            (c < '0' || c > '9') && c != '.' && c != '_' && c != '-')
            return false;
    }
    return value[4] != '.';
}

static int permission_index(const char *value)
{
    for (size_t i = 0; i < sizeof(permissions) / sizeof(permissions[0]); i++)
        if (!strcmp(value, permissions[i]))
            return (int)i;
    return -1;
}

static bool validate_manifest(json_object *root, struct manifest_policy *policy)
{
    static const char *const root_keys_v1[] = {
        "schema", "id", "name", "version", "publisher", "ui", "permissions"
    };
    static const char *const root_keys_v2[] = {
        "schema", "id", "name", "version", "publisher", "ui", "permissions",
        "runtime"
    };
    static const char *const ui_keys[] = {"entry", "presentation"};
    static const char *const runtime_keys[] = {"type", "entry", "api"};
    json_object *schema = NULL, *ui = NULL, *permission_array = NULL;
    json_object *runtime = NULL;
    const char *id = NULL, *name = NULL, *version = NULL, *publisher = NULL;
    if (!get_value(root, "schema", json_type_int, &schema))
        return false;
    int schema_number = json_object_get_int(schema);
    const char *const *root_keys = schema_number == 1 ?
        root_keys_v1 : root_keys_v2;
    size_t root_key_count = schema_number == 1 ?
        sizeof(root_keys_v1) / sizeof(root_keys_v1[0]) :
        sizeof(root_keys_v2) / sizeof(root_keys_v2[0]);
    if ((schema_number != 1 && schema_number != 2) ||
        !exact_keys(root, root_keys, root_key_count) ||
        json_object_object_length(root) != (int)root_key_count ||
        !bounded_string(root, "id", 3, 96, &id) || !valid_app_id(id) ||
        !bounded_string(root, "name", 1, 48, &name) ||
        !bounded_string(root, "version", 5, 32, &version) ||
        !bounded_string(root, "publisher", 1, 64, &publisher) ||
        !valid_publisher(publisher) ||
        !get_value(root, "ui", json_type_object, &ui) ||
        !get_value(root, "permissions", json_type_array, &permission_array))
        return false;
    uint32_t version_parts[3];
    if (!parse_version(version, version_parts) ||
        !exact_keys(ui, ui_keys, sizeof(ui_keys) / sizeof(ui_keys[0])) ||
        json_object_object_length(ui) != 2)
        return false;
    const char *entry = NULL, *presentation = NULL;
    if (!bounded_string(ui, "entry", 9, 192, &entry) ||
        !valid_ui_entry(entry) ||
        !bounded_string(ui, "presentation", 8, 8, &presentation) ||
        strcmp(presentation, "embedded"))
        return false;
    if (schema_number == 2) {
        json_object *api = NULL;
        const char *runtime_type = NULL, *runtime_entry = NULL;
        if (!get_value(root, "runtime", json_type_object, &runtime) ||
            !exact_keys(runtime, runtime_keys, 3) ||
            json_object_object_length(runtime) != 3 ||
            !bounded_string(runtime, "type", 10, 10, &runtime_type) ||
            strcmp(runtime_type, "native-rpc") ||
            !bounded_string(runtime, "entry", 5, 68, &runtime_entry) ||
            !valid_runtime_entry(runtime_entry) ||
            !get_value(runtime, "api", json_type_int, &api) ||
            json_object_get_int64(api) != 1)
            return false;
        policy->runtime_entry = runtime_entry;
    }

    size_t count = json_object_array_length(permission_array);
    if (count > sizeof(permissions) / sizeof(permissions[0]))
        return false;
    for (size_t i = 0; i < count; i++) {
        json_object *item = json_object_array_get_idx(permission_array, i);
        if (!json_object_is_type(item, json_type_string))
            return false;
        int index = permission_index(json_object_get_string(item));
        if (index < 0 || policy->granted[index])
            return false;
        policy->granted[index] = true;
    }
    policy->schema = schema_number;
    policy->ui_entry = entry;
    return true;
}

static bool validate_node(json_object *node, const struct manifest_policy *policy,
                          unsigned depth, unsigned *node_count)
{
    if (depth > MAX_UI_DEPTH || ++*node_count > MAX_UI_NODES)
        return false;
    const char *type = NULL;
    if (!bounded_string(node, "type", 1, 16, &type))
        return false;
    if (!strcmp(type, "column")) {
        static const char *const keys[] = {"type", "children"};
        json_object *children = NULL;
        if (!exact_keys(node, keys, 2) || json_object_object_length(node) != 2 ||
            !get_value(node, "children", json_type_array, &children) ||
            json_object_array_length(children) > 64)
            return false;
        for (size_t i = 0; i < json_object_array_length(children); i++)
            if (!validate_node(json_object_array_get_idx(children, i), policy,
                               depth + 1, node_count))
                return false;
        return true;
    }
    if (!strcmp(type, "text")) {
        static const char *const keys[] = {"type", "text"};
        const char *text = NULL;
        return exact_keys(node, keys, 2) &&
               json_object_object_length(node) == 2 &&
               bounded_string(node, "text", 0, 4096, &text);
    }
    if (!strcmp(type, "action")) {
        static const char *const keys[] = {
            "type", "label", "command", "arguments"
        };
        const char *label = NULL, *command = NULL;
        json_object *arguments = NULL;
        if (!exact_keys(node, keys, 4) ||
            (json_object_object_length(node) != 3 &&
             json_object_object_length(node) != 4) ||
            !bounded_string(node, "label", 1, 64, &label) ||
            !bounded_string(node, "command", 1, 64, &command))
            return false;
        if (!strcmp(command, "app.invoke")) {
            const char *name = NULL;
            static const char *const argument_keys[] = {"name"};
            return policy->schema == 2 &&
                   json_object_object_get_ex(node, "arguments", &arguments) &&
                   exact_keys(arguments, argument_keys, 1) &&
                   json_object_object_length(arguments) == 1 &&
                   bounded_string(arguments, "name", 1, 64, &name) &&
                   ((name[0] >= 'a' && name[0] <= 'z') ||
                    (name[0] >= 'A' && name[0] <= 'Z')) &&
                   strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                "abcdefghijklmnopqrstuvwxyz"
                                "0123456789._-") == strlen(name);
        }
        if (strcmp(command, "capture.snapshot"))
            return false;
        int index = permission_index(command);
        if (index < 0 || !policy->granted[index])
            return false;
        if (json_object_object_get_ex(node, "arguments", &arguments) &&
            (!json_object_is_type(arguments, json_type_object) ||
             json_object_object_length(arguments) != 0))
            return false;
        return true;
    }
    return false;
}

static bool validate_ui(json_object *root, const struct manifest_policy *policy)
{
    static const char *const keys[] = {"schema", "title", "layout"};
    json_object *schema = NULL, *layout = NULL;
    const char *title = NULL;
    if (!exact_keys(root, keys, 3) || json_object_object_length(root) != 3 ||
        !get_value(root, "schema", json_type_int, &schema) ||
        json_object_get_int64(schema) != 1 ||
        !bounded_string(root, "title", 1, 64, &title) ||
        !get_value(root, "layout", json_type_object, &layout))
        return false;
    unsigned node_count = 0;
    return validate_node(layout, policy, 1, &node_count);
}

static bool valid_native_elf(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    Elf64_Ehdr header;
    ssize_t count;
    do {
        count = read(fd, &header, sizeof(header));
    } while (count < 0 && errno == EINTR);
    close(fd);
    return count == (ssize_t)sizeof(header) &&
           !memcmp(header.e_ident, ELFMAG, SELFMAG) &&
           header.e_ident[EI_CLASS] == ELFCLASS64 &&
           header.e_ident[EI_DATA] == ELFDATA2LSB &&
           header.e_ident[EI_VERSION] == EV_CURRENT &&
           (header.e_type == ET_EXEC || header.e_type == ET_DYN) &&
           header.e_machine == EM_AARCH64 &&
           header.e_version == EV_CURRENT && header.e_entry != 0;
}

static int validate_directory(const char *directory)
{
    char manifest_path[PATH_MAX], ui_path[PATH_MAX], runtime_path[PATH_MAX];
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json",
                 directory) >= (int)sizeof(manifest_path) ||
        !regular_file(manifest_path, MAX_JSON_BYTES))
        return fail("manifest.json is missing, unsafe, or too large");
    json_object *manifest = read_json(manifest_path);
    struct manifest_policy policy = {0};
    if (!manifest || !validate_manifest(manifest, &policy)) {
        if (manifest)
            json_object_put(manifest);
        return fail("manifest policy validation failed");
    }
    if (snprintf(ui_path, sizeof(ui_path), "%s/%s", directory,
                 policy.ui_entry) >= (int)sizeof(ui_path) ||
        !regular_file(ui_path, MAX_JSON_BYTES)) {
        json_object_put(manifest);
        return fail("UI entry is missing, unsafe, or too large");
    }
    json_object *ui = read_json(ui_path);
    bool valid = ui && validate_ui(ui, &policy);
    if (ui)
        json_object_put(ui);
    if (valid && policy.schema == 2) {
        if (snprintf(runtime_path, sizeof(runtime_path), "%s/%s", directory,
                     policy.runtime_entry) >= (int)sizeof(runtime_path) ||
            !regular_file(runtime_path, 8 * 1024 * 1024) ||
            access(runtime_path, X_OK) || !valid_native_elf(runtime_path))
            valid = false;
    }
    json_object_put(manifest);
    if (!valid)
        return fail("declarative UI policy validation failed");
    puts("OK");
    return 0;
}

static int compare_versions(const char *left, const char *right)
{
    uint32_t a[3], b[3];
    if (!parse_version(left, a) || !parse_version(right, b))
        return fail("invalid semantic version");
    for (int i = 0; i < 3; i++) {
        if (a[i] < b[i]) {
            puts("-1");
            return 0;
        }
        if (a[i] > b[i]) {
            puts("1");
            return 0;
        }
    }
    puts("0");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "validate"))
        return validate_directory(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "compare-version"))
        return compare_versions(argv[2], argv[3]);
    fprintf(stderr,
            "usage: %s validate APP_DIR\n"
            "       %s compare-version LEFT RIGHT\n",
            argv[0], argv[0]);
    return 2;
}
