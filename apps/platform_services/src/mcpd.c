#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CAPTURE_SOCKET
#define CAPTURE_SOCKET "/var/run/aitvbox/capture.sock"
#endif
#ifndef CAPTURE_PATH
#define CAPTURE_PATH "/var/run/aitvbox/screen.jpg"
#endif
#ifndef HIDCTL_PATH
#define HIDCTL_PATH "/usr/bin/aitvbox-hidctl"
#endif
#ifndef HID_KEYBOARD_PATH
#define HID_KEYBOARD_PATH "/dev/hidg0"
#endif
#ifndef HID_POINTER_PATH
#define HID_POINTER_PATH "/dev/hidg1"
#endif
#ifndef GPIO_PATH
#define GPIO_PATH "/var/run/aitvbox/capabilities/gpio"
#endif
#ifndef I2C_PATH
#define I2C_PATH "/var/run/aitvbox/capabilities/i2c"
#endif
#ifndef SPI_PATH
#define SPI_PATH "/var/run/aitvbox/capabilities/spi"
#endif
#ifndef UART_PATH
#define UART_PATH "/var/run/aitvbox/capabilities/uart"
#endif
#ifndef USB_HOST_PATH
#define USB_HOST_PATH "/sys/bus/usb/devices"
#endif
#ifndef AGENT_LOCK_FILE
#define AGENT_LOCK_FILE "/var/run/aitvbox/agent.lock"
#endif
#ifndef AGENT_STOP_FILE
#define AGENT_STOP_FILE "/var/run/aitvbox/agent.stop"
#endif
#define MAX_REQUEST (1024 * 1024)
#define MAX_IMAGE (8 * 1024 * 1024)

enum server_state {
    SERVER_UNINITIALIZED,
    SERVER_INITIALIZED,
    SERVER_READY,
};

static bool process_active(long pid)
{
    if (pid <= 1)
        return false;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
    FILE *file = fopen(path, "r");
    if (file) {
        char line[512];
        if (fgets(line, sizeof(line), file)) {
            char *name_end = strrchr(line, ')');
            if (name_end && name_end[1] == ' ' && name_end[2] == 'Z') {
                fclose(file);
                return false;
            }
        }
        fclose(file);
    }
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

static void emit(json_object *value)
{
    fputs(json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void emit_error(json_object *id, int code, const char *message)
{
    json_object *response = json_object_new_object();
    json_object_object_add(response, "jsonrpc", json_object_new_string("2.0"));
    if (id) {
        json_object_get(id);
        json_object_object_add(response, "id", id);
    } else {
        json_object_object_add(response, "id", NULL);
    }
    json_object *error = json_object_new_object();
    json_object_object_add(error, "code", json_object_new_int(code));
    json_object_object_add(error, "message", json_object_new_string(message));
    json_object_object_add(response, "error", error);
    emit(response);
    json_object_put(response);
}

static json_object *content_text(const char *text, bool error)
{
    json_object *result = json_object_new_object();
    json_object *content = json_object_new_array();
    json_object *item = json_object_new_object();
    json_object_object_add(item, "type", json_object_new_string("text"));
    json_object_object_add(item, "text", json_object_new_string(text));
    json_object_array_add(content, item);
    json_object_object_add(result, "content", content);
    if (error)
        json_object_object_add(result, "isError", json_object_new_boolean(true));
    return result;
}

static char *base64_file(const char *path)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    struct stat st;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > MAX_IMAGE) {
        if (fd >= 0)
            close(fd);
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    unsigned char *input = malloc(size);
    char *output = malloc(((size + 2) / 3) * 4 + 1);
    if (!input || !output) {
        close(fd);
        free(input);
        free(output);
        return NULL;
    }
    size_t used = 0;
    while (used < size) {
        ssize_t n = read(fd, input + used, size - used);
        if (n <= 0) break;
        used += (size_t)n;
    }
    close(fd);
    if (used != size) {
        free(input);
        free(output);
        return NULL;
    }
    size_t out = 0;
    for (size_t i = 0; i < size; i += 3) {
        uint32_t value = (uint32_t)input[i] << 16;
        if (i + 1 < size) value |= (uint32_t)input[i + 1] << 8;
        if (i + 2 < size) value |= input[i + 2];
        output[out++] = table[(value >> 18) & 63];
        output[out++] = table[(value >> 12) & 63];
        output[out++] = i + 1 < size ? table[(value >> 6) & 63] : '=';
        output[out++] = i + 2 < size ? table[value & 63] : '=';
    }
    output[out] = '\0';
    free(input);
    return output;
}

static int request_capture(char *reply, size_t reply_size)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 6, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CAPTURE_SOCKET);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) ||
        write(fd, "SNAPSHOT\n", 9) != 9) {
        close(fd);
        return -1;
    }
    ssize_t n = read(fd, reply, reply_size - 1);
    close(fd);
    if (n <= 0) return -1;
    reply[n] = '\0';
    return strncmp(reply, "OK ", 3) == 0 ? 0 : -1;
}

static json_object *capture_tool(void)
{
    char reply[256];
    if (request_capture(reply, sizeof(reply)))
        return content_text("capture service unavailable or no HDMI frame", true);
    char *data = base64_file(CAPTURE_PATH);
    if (!data) return content_text("snapshot file could not be read", true);
    json_object *result = json_object_new_object();
    json_object *content = json_object_new_array();
    json_object *image = json_object_new_object();
    json_object_object_add(image, "type", json_object_new_string("image"));
    json_object_object_add(image, "data", json_object_new_string(data));
    json_object_object_add(image, "mimeType", json_object_new_string("image/jpeg"));
    json_object_array_add(content, image);
    json_object_object_add(result, "content", content);
    free(data);
    return result;
}

static int run_hid(const char *mode, int a, int b, int c, int d)
{
    char sa[16], sb[16], sc[16], sd[16];
    snprintf(sa, sizeof(sa), "%d", a);
    snprintf(sb, sizeof(sb), "%d", b);
    snprintf(sc, sizeof(sc), "%d", c);
    snprintf(sd, sizeof(sd), "%d", d);
    pid_t pid = fork();
    if (pid == 0) {
        if (!strcmp(mode, "key"))
            execl(HIDCTL_PATH, "aitvbox-hidctl", "key", sa, sb, NULL);
        else
            execl(HIDCTL_PATH, "aitvbox-hidctl", "mouse",
                  sa, sb, sc, sd, NULL);
        _exit(127);
    }
    if (pid < 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int integer_arg(json_object *args, const char *name, int min, int max,
                       int fallback, bool required, bool *ok)
{
    json_object *value = NULL;
    if (!args || !json_object_object_get_ex(args, name, &value)) {
        if (required) *ok = false;
        return fallback;
    }
    if (!json_object_is_type(value, json_type_int)) {
        *ok = false;
        return fallback;
    }
    int result = json_object_get_int(value);
    if (result < min || result > max) *ok = false;
    return result;
}

static bool autonomous_agent_active(void)
{
    int fd = open(AGENT_LOCK_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    char text[32] = {0};
    ssize_t length = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (length <= 0)
        return false;
    char *end = NULL;
    long pid = strtol(text, &end, 10);
    bool active = pid > 1 && end && (*end == '\0' || *end == '\n') &&
                  process_active(pid);
    if (!active)
        unlink(AGENT_LOCK_FILE);
    return active;
}

static void request_agent_stop(void)
{
    int fd = open(AGENT_STOP_FILE,
                  O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0)
        close(fd);
}

static bool object_has_only(json_object *object, const char *const *keys,
                            size_t key_count)
{
    if (!json_object_is_type(object, json_type_object))
        return false;
    json_object_object_foreach(object, key, value) {
        (void)value;
        bool allowed = false;
        for (size_t i = 0; i < key_count; i++) {
            if (!strcmp(key, keys[i])) {
                allowed = true;
                break;
            }
        }
        if (!allowed)
            return false;
    }
    return true;
}

static json_object *call_tool(const char *name, json_object *args)
{
    bool ok = true;
    if (!strcmp(name, "computer.capture")) {
        if (!object_has_only(args, NULL, 0))
            return content_text("computer.capture does not accept arguments", true);
        return capture_tool();
    }
    if (!strcmp(name, "computer.key")) {
        static const char *const keys[] = {"keycode", "modifier"};
        if (!object_has_only(args, keys, 2))
            return content_text("unexpected keyboard argument", true);
        if (autonomous_agent_active())
            return content_text("autonomous agent owns USB input", true);
        int key = integer_arg(args, "keycode", 0, 101, 0, true, &ok);
        int modifier = integer_arg(args, "modifier", 0, 255, 0, false, &ok);
        if (!ok) return content_text("invalid HID keycode/modifier", true);
        int rc = run_hid("key", key, modifier, 0, 0);
        return content_text(rc ? "USB HID keyboard unavailable" : "OK", rc != 0);
    }
    if (!strcmp(name, "computer.mouse")) {
        static const char *const keys[] = {"dx", "dy", "buttons", "wheel"};
        if (!object_has_only(args, keys, 4))
            return content_text("unexpected pointer argument", true);
        if (autonomous_agent_active())
            return content_text("autonomous agent owns USB input", true);
        int dx = integer_arg(args, "dx", -127, 127, 0, true, &ok);
        int dy = integer_arg(args, "dy", -127, 127, 0, true, &ok);
        int buttons = integer_arg(args, "buttons", 0, 7, 0, false, &ok);
        int wheel = integer_arg(args, "wheel", -127, 127, 0, false, &ok);
        if (!ok) return content_text("invalid relative mouse values", true);
        int rc = run_hid("mouse", dx, dy, buttons, wheel);
        return content_text(rc ? "USB HID pointer unavailable" : "OK", rc != 0);
    }
    if (!strcmp(name, "hardware.capabilities")) {
        if (!object_has_only(args, NULL, 0))
            return content_text("hardware.capabilities does not accept arguments",
                                true);
        char text[512];
        snprintf(text, sizeof(text),
                 "{\"capture\":%s,\"keyboard\":%s,\"pointer\":%s,"
                 "\"gpio\":%s,\"i2c\":%s,\"spi\":%s,\"uart\":%s,"
                 "\"usbHost\":%s}",
                 access(CAPTURE_SOCKET, F_OK) ? "false" : "true",
                 access(HID_KEYBOARD_PATH, W_OK) ? "false" : "true",
                 access(HID_POINTER_PATH, W_OK) ? "false" : "true",
                 access(GPIO_PATH, F_OK) ? "false" : "true",
                 access(I2C_PATH, F_OK) ? "false" : "true",
                 access(SPI_PATH, F_OK) ? "false" : "true",
                 access(UART_PATH, F_OK) ? "false" : "true",
                 access(USB_HOST_PATH, F_OK) ? "false" : "true");
        return content_text(text, false);
    }
    if (!strcmp(name, "safety.stop")) {
        if (!object_has_only(args, NULL, 0))
            return content_text("safety.stop does not accept arguments", true);
        request_agent_stop();
        run_hid("key", 0, 0, 0, 0);
        run_hid("mouse", 0, 0, 0, 0);
        return content_text("OK", false);
    }
    return content_text("unknown tool", true);
}

static json_object *schema(const char *properties)
{
    json_object *value = json_tokener_parse(properties);
    return value ? value : json_object_new_object();
}

static void add_tool(json_object *tools, const char *name, const char *description,
                     const char *input_schema)
{
    json_object *tool = json_object_new_object();
    json_object_object_add(tool, "name", json_object_new_string(name));
    json_object_object_add(tool, "description", json_object_new_string(description));
    json_object_object_add(tool, "inputSchema", schema(input_schema));
    json_object_array_add(tools, tool);
}

static json_object *list_tools(void)
{
    json_object *result = json_object_new_object();
    json_object *tools = json_object_new_array();
    add_tool(tools, "computer.capture", "Capture the HDMI computer display",
             "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}");
    add_tool(tools, "computer.key", "Send one USB HID keyboard key and release it",
             "{\"type\":\"object\",\"properties\":{\"keycode\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":101},\"modifier\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255}},\"required\":[\"keycode\"],\"additionalProperties\":false}");
    add_tool(tools, "computer.mouse", "Move the relative USB HID pointer",
             "{\"type\":\"object\",\"properties\":{\"dx\":{\"type\":\"integer\",\"minimum\":-127,\"maximum\":127},\"dy\":{\"type\":\"integer\",\"minimum\":-127,\"maximum\":127},\"buttons\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":7},\"wheel\":{\"type\":\"integer\",\"minimum\":-127,\"maximum\":127}},\"required\":[\"dx\",\"dy\"],\"additionalProperties\":false}");
    add_tool(tools, "hardware.capabilities", "Report currently available hardware interfaces",
             "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}");
    add_tool(tools, "safety.stop", "Release all USB HID keys and pointer buttons",
             "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}");
    json_object_object_add(result, "tools", tools);
    return result;
}

static void handle_request(json_object *request, enum server_state *state)
{
    json_object *id = NULL, *method_obj = NULL, *params = NULL;
    bool has_id = json_object_is_type(request, json_type_object) &&
                  json_object_object_get_ex(request, "id", &id);
    bool valid_id = !has_id || json_object_is_type(id, json_type_int) ||
                    json_object_is_type(id, json_type_string);
    json_object *jsonrpc = NULL;
    if (!json_object_is_type(request, json_type_object) ||
        !json_object_object_get_ex(request, "jsonrpc", &jsonrpc) ||
        !json_object_is_type(jsonrpc, json_type_string) ||
        strcmp(json_object_get_string(jsonrpc), "2.0") ||
        !json_object_object_get_ex(request, "method", &method_obj) ||
        !json_object_is_type(method_obj, json_type_string) || !valid_id) {
        if (has_id) emit_error(valid_id ? id : NULL, -32600, "Invalid Request");
        return;
    }
    const char *method = json_object_get_string(method_obj);
    if (!has_id) {
        if (!strcmp(method, "notifications/initialized") &&
            *state == SERVER_INITIALIZED)
            *state = SERVER_READY;
        return;
    }
    if (strcmp(method, "initialize") && *state != SERVER_READY) {
        emit_error(id, -32002, "Server not initialized");
        return;
    }
    json_object *response = json_object_new_object();
    json_object_object_add(response, "jsonrpc", json_object_new_string("2.0"));
    json_object_get(id);
    json_object_object_add(response, "id", id);
    json_object *result = NULL;
    if (!strcmp(method, "initialize")) {
        if (*state != SERVER_UNINITIALIZED) {
            json_object_put(response);
            emit_error(id, -32600, "Server already initialized");
            return;
        }
        const char *protocol = "2025-11-25";
        json_object_object_get_ex(request, "params", &params);
        if (!json_object_is_type(params, json_type_object)) {
            json_object_put(response);
            emit_error(id, -32602, "Invalid initialize parameters");
            return;
        }
        json_object *requested = NULL;
        if (params && json_object_object_get_ex(params, "protocolVersion", &requested) &&
            json_object_is_type(requested, json_type_string)) {
            const char *version = json_object_get_string(requested);
            if (!strcmp(version, "2025-11-25") ||
                !strcmp(version, "2025-06-18") ||
                !strcmp(version, "2025-03-26") ||
                !strcmp(version, "2024-11-05"))
                protocol = version;
        }
        result = json_object_new_object();
        json_object_object_add(result, "protocolVersion",
                               json_object_new_string(protocol));
        json_object *caps = json_object_new_object();
        json_object_object_add(caps, "tools", json_object_new_object());
        json_object_object_add(result, "capabilities", caps);
        json_object *info = json_object_new_object();
        json_object_object_add(info, "name", json_object_new_string("aitvbox-mcpd"));
        json_object_object_add(info, "version", json_object_new_string("1.0.0"));
        json_object_object_add(result, "serverInfo", info);
        *state = SERVER_INITIALIZED;
    } else if (!strcmp(method, "tools/list")) {
        result = list_tools();
    } else if (!strcmp(method, "tools/call")) {
        json_object_object_get_ex(request, "params", &params);
        json_object *name_obj = NULL, *args = NULL;
        static const char *const param_keys[] = {"name", "arguments"};
        if (object_has_only(params, param_keys, 2)) {
            json_object_object_get_ex(params, "name", &name_obj);
            json_object_object_get_ex(params, "arguments", &args);
        }
        result = name_obj && json_object_is_type(name_obj, json_type_string) &&
                         json_object_is_type(args, json_type_object) ?
                     call_tool(json_object_get_string(name_obj), args) :
                     content_text("invalid tool call parameters", true);
    } else {
        json_object_put(response);
        emit_error(id, -32601, "Method not found");
        return;
    }
    json_object_object_add(response, "result", result);
    emit(response);
    json_object_put(response);
}

static json_object *parse_request_line(const char *line)
{
    size_t length = strlen(line);
    json_tokener *tokener = json_tokener_new();
    if (!tokener)
        return NULL;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    json_object *request = json_tokener_parse_ex(tokener, line, (int)length);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    size_t consumed = (size_t)tokener->char_offset;
    while (consumed < length &&
           (line[consumed] == ' ' || line[consumed] == '\t' ||
            line[consumed] == '\r' || line[consumed] == '\n'))
        consumed++;
    json_tokener_free(tokener);
    if (error != json_tokener_success || consumed != length) {
        if (request)
            json_object_put(request);
        return NULL;
    }
    return request;
}

int main(void)
{
    char *line = malloc(MAX_REQUEST + 1);
    if (!line) return 1;
    enum server_state state = SERVER_UNINITIALIZED;
    while (fgets(line, MAX_REQUEST + 1, stdin)) {
        size_t length = strlen(line);
        if (length == MAX_REQUEST && line[length - 1] != '\n') {
            int byte;
            do {
                byte = fgetc(stdin);
            } while (byte != '\n' && byte != EOF);
            emit_error(NULL, -32700, "Request exceeds 1 MiB");
            continue;
        }
        json_object *request = parse_request_line(line);
        if (request) {
            handle_request(request, &state);
            json_object_put(request);
        } else {
            emit_error(NULL, -32700, "Parse error");
        }
    }
    free(line);
    return ferror(stdin) ? 1 : 0;
}
