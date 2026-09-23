#include <curl/curl.h>
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef KEY_FILE
#define KEY_FILE "/overlay/aitvbox/secrets/model-api-key"
#endif
#ifndef CONFIG_FILE
#define CONFIG_FILE "/overlay/aitvbox/secrets/model.conf"
#endif
#ifndef SCREEN_FILE
#define SCREEN_FILE "/var/run/aitvbox/screen.jpg"
#endif
#ifndef STOP_FILE
#define STOP_FILE "/var/run/aitvbox/agent.stop"
#endif
#ifndef AGENT_LOCK_FILE
#define AGENT_LOCK_FILE "/var/run/aitvbox/agent.lock"
#endif
#ifndef HDMI_PREVIEW_PATH
#define HDMI_PREVIEW_PATH "/usr/bin/hdmi_preview"
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
#ifndef HID_LOCK_DIR
#define HID_LOCK_DIR "/var/run/aitvbox/hid.lock"
#endif
#ifndef KEYBOARD_HOLD_US
#define KEYBOARD_HOLD_US 350000
#endif
#ifndef KEYBOARD_RELEASE_SETTLE_US
#define KEYBOARD_RELEASE_SETTLE_US 30000
#endif
#ifndef NETEASE_STEP_GAP_US
#define NETEASE_STEP_GAP_US 500000
#endif
#ifndef NETEASE_SETTLE_US
#define NETEASE_SETTLE_US 3000000
#endif
#define MAX_IMAGE (8 * 1024 * 1024)
#define MAX_RESPONSE (2 * 1024 * 1024)
#define ACTION_HISTORY_MAX 4096

struct response {
    char *data;
    size_t size;
};

static volatile sig_atomic_t stop_requested;

static void log_event(const char *role, const char *type, int step,
                      const char *message)
{
    json_object *entry = json_object_new_object();
    if (!entry)
        return;
    json_object_object_add(entry, "role", json_object_new_string(role));
    json_object_object_add(entry, "type", json_object_new_string(type));
    if (step > 0)
        json_object_object_add(entry, "step", json_object_new_int(step));
    json_object_object_add(
        entry, "message", json_object_new_string(message ? message : ""));
    fputs(json_object_to_json_string_ext(entry, JSON_C_TO_STRING_PLAIN),
          stdout);
    fputc('\n', stdout);
    fflush(stdout);
    json_object_put(entry);
}

static void log_step_message(const char *role, const char *type, int step,
                             const char *format, int first, int second)
{
    char message[256];
    snprintf(message, sizeof(message), format, first, second);
    log_event(role, type, step, message);
}

static char *read_file(const char *path, size_t maximum, size_t *length_out)
{
    struct stat st;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || (size_t)st.st_size > maximum) {
        if (fd >= 0)
            close(fd);
        return NULL;
    }
    char *data = malloc((size_t)st.st_size + 1);
    if (!data) {
        close(fd);
        return NULL;
    }
    size_t used = 0;
    while (used < (size_t)st.st_size) {
        ssize_t n = read(fd, data + used, (size_t)st.st_size - used);
        if (n <= 0) break;
        used += (size_t)n;
    }
    close(fd);
    if (used != (size_t)st.st_size) {
        free(data);
        return NULL;
    }
    data[used] = '\0';
    if (length_out)
        *length_out = used;
    return data;
}

static bool secure_file(const char *path)
{
    struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode) &&
           st.st_uid == geteuid() && (st.st_mode & 0077) == 0;
}

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

static int acquire_agent_lock(void)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        int fd = open(AGENT_LOCK_FILE,
                      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) {
            char owner[32];
            int length = snprintf(owner, sizeof(owner), "%ld\n", (long)getpid());
            int result = write(fd, owner, (size_t)length) == length ? 0 : -1;
            close(fd);
            if (!result)
                return 0;
            unlink(AGENT_LOCK_FILE);
            return -1;
        }
        if (errno != EEXIST)
            return -1;
        char *owner = read_file(AGENT_LOCK_FILE, 31, NULL);
        char *end = NULL;
        long pid = owner ? strtol(owner, &end, 10) : -1;
        bool active = pid > 1 && end && (*end == '\0' || *end == '\n') &&
                      process_active(pid);
        free(owner);
        if (active)
            return 1;
        if (unlink(AGENT_LOCK_FILE) && errno != ENOENT)
            return -1;
    }
    return -1;
}

static void stop_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static bool valid_api_key(char *key, size_t size)
{
    if (!key || size < 12 || size > 4096)
        return false;
    if (key[size - 1] == '\n')
        size--;
    if (size < 12)
        return false;
    for (size_t i = 0; i < size; i++)
        if ((unsigned char)key[i] <= 32 || (unsigned char)key[i] >= 127)
            return false;
    if (key[size] != '\0' && key[size] != '\n')
        return false;
    key[size] = '\0';
    return true;
}

static char *chat_completions_url(const char *endpoint)
{
    size_t length = strlen(endpoint);
    while (length > 0 && endpoint[length - 1] == '/')
        length--;
    static const char suffix[] = "/chat/completions";
    if ((length >= 3 && !strncmp(endpoint + length - 3, "/v1", 3)) ||
        (length >= 3 && !strncmp(endpoint + length - 3, "/v3", 3))) {
        char *url = malloc(length + sizeof(suffix));
        if (!url)
            return NULL;
        memcpy(url, endpoint, length);
        memcpy(url + length, suffix, sizeof(suffix));
        return url;
    }
    return strndup(endpoint, length);
}

static char *base64(const unsigned char *input, size_t size)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *output = malloc(((size + 2) / 3) * 4 + 1);
    if (!output) return NULL;
    size_t out = 0;
    for (size_t i = 0; i < size; i += 3) {
        unsigned value = (unsigned)input[i] << 16;
        if (i + 1 < size) value |= (unsigned)input[i + 1] << 8;
        if (i + 2 < size) value |= input[i + 2];
        output[out++] = table[(value >> 18) & 63];
        output[out++] = table[(value >> 12) & 63];
        output[out++] = i + 1 < size ? table[(value >> 6) & 63] : '=';
        output[out++] = i + 2 < size ? table[value & 63] : '=';
    }
    output[out] = '\0';
    return output;
}

static int run_command(const char *program, char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execv(program, argv);
        _exit(127);
    }
    if (pid < 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int capture(void)
{
    char *argv[] = {"hdmi_preview", "--snapshot", NULL};
    return run_command(HDMI_PREVIEW_PATH, argv);
}

static int acquire_hid_lock(void)
{
    for (int attempt = 0; attempt < 50; attempt++) {
        if (!mkdir(HID_LOCK_DIR, 0700)) {
            char owner_path[256];
            snprintf(owner_path, sizeof(owner_path), "%s/pid", HID_LOCK_DIR);
            FILE *owner = fopen(owner_path, "w");
            if (owner) {
                fprintf(owner, "%ld\n", (long)getpid());
                fclose(owner);
            }
            return 0;
        }
        if (errno != EEXIST)
            return -1;
        usleep(20000);
    }
    return -1;
}

static void release_hid_lock(void)
{
    char owner_path[256];
    snprintf(owner_path, sizeof(owner_path), "%s/pid", HID_LOCK_DIR);
    unlink(owner_path);
    rmdir(HID_LOCK_DIR);
}

static int write_hid_report(int fd, const unsigned char *report, size_t size)
{
    size_t offset = 0;
    for (int attempt = 0; offset < size && attempt < 100; attempt++) {
        ssize_t written = write(fd, report + offset, size - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(5000);
            continue;
        }
        return -1;
    }
    return offset == size ? 0 : -1;
}

static int keyboard_hold_action(int keycode, int modifier,
                                useconds_t hold_us)
{
    const char *keyboard = getenv("AITVBOX_HID_KEYBOARD");
    if (!keyboard || !keyboard[0])
        keyboard = HID_KEYBOARD_PATH;
    if (acquire_hid_lock())
        return -1;

    int fd = open(keyboard, O_WRONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        release_hid_lock();
        return -1;
    }

    unsigned char pressed[8] = {
        (unsigned char)modifier, 0, (unsigned char)keycode, 0, 0, 0, 0, 0
    };
    static const unsigned char released[8] = {0};
    /*
     * A process can exit after its press report reaches Windows but before
     * the release report does.  The lock then looks clean even though the
     * host still considers that key held, and another press of the same key
     * produces no new key-down transition.  Always establish a neutral
     * report first, then leave enough USB polling intervals before pressing.
     */
    int result = write_hid_report(fd, released, sizeof(released));
    bool has_press = keycode != 0 || modifier != 0;
    if (!result && has_press) {
        usleep(KEYBOARD_RELEASE_SETTLE_US);
        result = write_hid_report(fd, pressed, sizeof(pressed));
        if (!result)
            usleep(hold_us);

        /* Best-effort release even when the press write reported an error. */
        int release_result = write_hid_report(fd, released, sizeof(released));
        if (!result)
            result = release_result;
    }
    close(fd);
    release_hid_lock();
    return result;
}

static int keyboard_action(int keycode, int modifier)
{
    return keyboard_hold_action(keycode, modifier, KEYBOARD_HOLD_US);
}

static int hid_action(const char *mode, int a, int b, int c, int d)
{
    if (!strcmp(mode, "key"))
        return keyboard_action(a, b);

    char sa[16], sb[16], sc[16], sd[16];
    snprintf(sa, sizeof(sa), "%d", a);
    snprintf(sb, sizeof(sb), "%d", b);
    snprintf(sc, sizeof(sc), "%d", c);
    snprintf(sd, sizeof(sd), "%d", d);
    char *mouse_argv[] = {"aitvbox-hidctl", "mouse", sa, sb, sc, sd, NULL};
    return run_command(HIDCTL_PATH, mouse_argv);
}

static int probe_cursor_visibility(int attempt)
{
    /*
     * The Windows pointer can begin on an uncaptured neighboring display.
     * Explore an expanding range in alternating directions, with no button
     * report, and require a fresh HDMI frame after every bounded sweep.
    */
    int direction = (attempt % 2) ? -1 : 1;
    int steps = 16 << attempt;
    if (acquire_hid_lock())
        return -1;
    const char *pointer = getenv("AITVBOX_HID_MOUSE");
    if (!pointer || !pointer[0])
        pointer = HID_POINTER_PATH;
    int fd = open(pointer, O_WRONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        release_hid_lock();
        return -1;
    }
    unsigned char report[4] = {0, (unsigned char)(direction * 64), 0, 0};
    int result = 0;
    for (int i = 0; i < steps; i++) {
        if (write_hid_report(fd, report, sizeof(report))) {
            result = -1;
            break;
        }
        usleep(10000);
    }
    static const unsigned char released[4] = {0};
    if (!result)
        result = write_hid_report(fd, released, sizeof(released));
    close(fd);
    release_hid_lock();
    return result;
}

static bool ascii_hid_key(unsigned char character, int *keycode, int *modifier)
{
    *modifier = 0;
    if (character >= 'a' && character <= 'z') {
        *keycode = 4 + character - 'a';
        return true;
    }
    if (character >= 'A' && character <= 'Z') {
        *keycode = 4 + character - 'A';
        *modifier = 2;
        return true;
    }
    if (character >= '1' && character <= '9') {
        *keycode = 30 + character - '1';
        return true;
    }
    if (character == '0') {
        *keycode = 39;
        return true;
    }
    struct key_mapping {
        unsigned char character;
        unsigned char keycode;
        unsigned char modifier;
    };
    static const struct key_mapping mappings[] = {
        {'\n', 40, 0}, {'\t', 43, 0}, {' ', 44, 0},
        {'-', 45, 0}, {'_', 45, 2}, {'=', 46, 0}, {'+', 46, 2},
        {'[', 47, 0}, {'{', 47, 2}, {']', 48, 0}, {'}', 48, 2},
        {'\\', 49, 0}, {'|', 49, 2}, {';', 51, 0}, {':', 51, 2},
        {'\'', 52, 0}, {'"', 52, 2}, {'`', 53, 0}, {'~', 53, 2},
        {',', 54, 0}, {'<', 54, 2}, {'.', 55, 0}, {'>', 55, 2},
        {'/', 56, 0}, {'?', 56, 2}, {'!', 30, 2}, {'@', 31, 2},
        {'#', 32, 2}, {'$', 33, 2}, {'%', 34, 2}, {'^', 35, 2},
        {'&', 36, 2}, {'*', 37, 2}, {'(', 38, 2}, {')', 39, 2},
    };
    for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++) {
        if (mappings[i].character == character) {
            *keycode = mappings[i].keycode;
            *modifier = mappings[i].modifier;
            return true;
        }
    }
    return false;
}

static int type_ascii_text(const char *text)
{
    size_t length = strlen(text);
    if (length == 0 || length > 128)
        return -1;
    for (size_t i = 0; i < length; i++) {
        int keycode = 0, modifier = 0;
        if (!ascii_hid_key((unsigned char)text[i], &keycode, &modifier))
            return -1;
    }
    for (size_t i = 0; i < length; i++) {
        int keycode = 0, modifier = 0;
        (void)ascii_hid_key((unsigned char)text[i], &keycode, &modifier);
        if (hid_action("key", keycode, modifier, 0, 0))
            return -1;
        usleep(20000);
    }
    return 0;
}

static int send_hotkey(const char *keys);

static int prepare_browser_url_text(const char *url)
{
    return send_hotkey("CTRL+L") ||
           (usleep(250000), send_hotkey("END")) ||
           (usleep(250000), keyboard_hold_action(42, 0, 6000000)) ||
           (usleep(250000), type_ascii_text(url));
}

static bool ascii_text_supported(const char *text)
{
    size_t length = text ? strlen(text) : 0;
    if (length == 0 || length > 128)
        return false;
    for (size_t i = 0; i < length; i++) {
        int keycode = 0, modifier = 0;
        if (!ascii_hid_key((unsigned char)text[i], &keycode, &modifier))
            return false;
    }
    return true;
}

static const char *ascii_alias_for_text(const char *text)
{
    // Basic HID cannot emit Unicode. Use the application's ASCII executable
    // name so Windows Search does not enter an unresolved pinyin IME state.
    if (strstr(text, "\xe7\xbd\x91\xe6\x98\x93\xe4\xba\x91"))
        return "cloudmusic";
    return NULL;
}

static bool named_keycode(const char *name, int *keycode)
{
    if (strlen(name) == 1) {
        unsigned char character = (unsigned char)name[0];
        if (character >= 'a' && character <= 'z')
            character = (unsigned char)(character - 'a' + 'A');
        if (character >= 'A' && character <= 'Z') {
            *keycode = 4 + character - 'A';
            return true;
        }
        if (character >= '1' && character <= '9') {
            *keycode = 30 + character - '1';
            return true;
        }
        if (character == '0') {
            *keycode = 39;
            return true;
        }
        return false;
    }
    struct named_key {
        const char *name;
        int keycode;
    };
    static const struct named_key keys[] = {
        {"ENTER", 40}, {"ESC", 41}, {"ESCAPE", 41}, {"BACKSPACE", 42},
        {"TAB", 43}, {"SPACE", 44}, {"DELETE", 76}, {"HOME", 74},
        {"END", 77}, {"PAGEUP", 75}, {"PAGEDOWN", 78}, {"RIGHT", 79},
        {"LEFT", 80}, {"DOWN", 81}, {"UP", 82},
        {"F1", 58}, {"F2", 59}, {"F3", 60}, {"F4", 61},
        {"F5", 62}, {"F6", 63}, {"F7", 64}, {"F8", 65},
        {"F9", 66}, {"F10", 67}, {"F11", 68}, {"F12", 69},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (!strcasecmp(name, keys[i].name)) {
            *keycode = keys[i].keycode;
            return true;
        }
    }
    return false;
}

static int send_hotkey(const char *keys)
{
    if (!keys || strlen(keys) == 0 || strlen(keys) > 64)
        return -1;
    char *copy = strdup(keys);
    if (!copy)
        return -1;
    int modifier = 0, keycode = 0;
    bool normal_key_seen = false;
    char *save = NULL;
    for (char *token = strtok_r(copy, "+", &save);
         token; token = strtok_r(NULL, "+", &save)) {
        if (!strcasecmp(token, "CTRL") || !strcasecmp(token, "CONTROL"))
            modifier |= 1;
        else if (!strcasecmp(token, "SHIFT"))
            modifier |= 2;
        else if (!strcasecmp(token, "ALT"))
            modifier |= 4;
        else if (!strcasecmp(token, "WIN") ||
                 !strcasecmp(token, "WINDOWS") ||
                 !strcasecmp(token, "GUI"))
            modifier |= 8;
        else if (!normal_key_seen && named_keycode(token, &keycode))
            normal_key_seen = true;
        else {
            free(copy);
            return -1;
        }
    }
    free(copy);
    if (!normal_key_seen)
        return -1;
    return hid_action("key", keycode, modifier, 0, 0);
}

static int recover_browser_window(int *cycle_count, int *move_count,
                                  bool *cycle_armed,
                                  char *message, size_t message_size)
{
    if (*cycle_armed) {
        if (*move_count >= 8)
            return -1;
        /*
         * Direction is a bounded probe, not a remembered display-topology
         * assumption. Alternate directions and require a fresh HDMI frame
         * after every attempt so a reboot or monitor reorder is rediscovered.
         */
        const char *direction = (*move_count % 2) ? "left" : "right";
        const char *keys = (*move_count % 2) ?
            "WIN+SHIFT+LEFT" : "WIN+SHIFT+RIGHT";
        if (send_hotkey(keys))
            return -1;
        (*move_count)++;
        *cycle_armed = false;
        snprintf(message, message_size,
                 "Automatically moved the cycled window %s as bounded "
                 "browser recovery (%d/8)", direction, *move_count);
        return 0;
    }
    if (*cycle_count >= 8 || send_hotkey("ALT+TAB"))
        return -1;
    (*cycle_count)++;
    *cycle_armed = true;
    snprintf(message, message_size,
             "Automatically cycled one window for browser recovery (%d/8)",
             *cycle_count);
    return 0;
}

static int focus_netease_and_search(const char *query)
{
    /*
     * This primitive is deliberately not a launcher or focus guess.  The
     * caller must first prove from the model's current-frame observation that
     * NetEase is already visible on the captured HDMI display.  Global
     * shortcuts such as Win+D/Win+T can leave keyboard focus on an invisible
     * window from another monitor, which previously made Ctrl+F operate a USB
     * property dialog instead of NetEase.
     */
    if (!ascii_text_supported(query))
        return -1;
    if (send_hotkey("CTRL+F"))
        return -1;
    usleep(NETEASE_STEP_GAP_US);
    if (send_hotkey("CTRL+A") ||
        type_ascii_text(query) ||
        send_hotkey("ENTER"))
        return -1;
    usleep(NETEASE_SETTLE_US);
    return 0;
}

static json_object *parse_action(const char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        text++;
    bool fenced = !strncmp(text, "```", 3);
    if (fenced) {
        const char *line_end = strchr(text, '\n');
        if (!line_end) return NULL;
        text = line_end + 1;
        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
            text++;
    }
    if (*text != '{') return NULL;

    bool in_string = false, escaped = false;
    int depth = 0;
    const char *end = NULL;
    for (const char *cursor = text; *cursor; cursor++) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (*cursor == '\\') {
                escaped = true;
            } else if (*cursor == '"') {
                in_string = false;
            }
            continue;
        }
        if (*cursor == '"') {
            in_string = true;
        } else if (*cursor == '{') {
            depth++;
        } else if (*cursor == '}' && --depth == 0) {
            end = cursor + 1;
            break;
        }
    }
    if (!end || in_string || depth != 0) return NULL;

    const char *tail = end;
    while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n')
        tail++;
    if (fenced) {
        if (strncmp(tail, "```", 3)) {
            return NULL;
        }
        tail += 3;
        while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n')
            tail++;
    }
    if (*tail) {
        return NULL;
    }
    size_t json_size = (size_t)(end - text);
    char *json = malloc(json_size + 1);
    if (!json) return NULL;
    memcpy(json, text, json_size);
    json[json_size] = '\0';
    json_object *action = json_tokener_parse(json);
    free(json);
    if (!action || !json_object_is_type(action, json_type_object)) {
        if (action) json_object_put(action);
        return NULL;
    }
    return action;
}

static size_t response_write(char *ptr, size_t size, size_t count, void *opaque)
{
    struct response *response = opaque;
    size_t bytes = size * count;
    if (response->size + bytes > MAX_RESPONSE) return 0;
    char *next = realloc(response->data, response->size + bytes + 1);
    if (!next) return 0;
    response->data = next;
    memcpy(response->data + response->size, ptr, bytes);
    response->size += bytes;
    response->data[response->size] = '\0';
    return bytes;
}

static int transfer_progress(void *opaque, curl_off_t download_total,
                             curl_off_t download_now, curl_off_t upload_total,
                             curl_off_t upload_now)
{
    (void)opaque;
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    return stop_requested || access(STOP_FILE, F_OK) == 0;
}

static bool parse_config(char **endpoint, char **model)
{
    if (!secure_file(CONFIG_FILE))
        return false;
    char *config = read_file(CONFIG_FILE, 4096, NULL);
    if (!config) return false;
    char *save = NULL;
    for (char *line = strtok_r(config, "\r\n", &save); line;
         line = strtok_r(NULL, "\r\n", &save)) {
        if (!strncmp(line, "ENDPOINT=https://", 17)) *endpoint = strdup(line + 9);
        if (!strncmp(line, "MODEL=", 6) && line[6]) *model = strdup(line + 6);
    }
    free(config);
    return *endpoint && *model;
}

static json_object *make_request(const char *model, const char *task,
                                 const char *image, int step, int attempt,
                                 const char *history)
{
    json_object *root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string(model));
    json_object_object_add(root, "temperature", json_object_new_double(0));
    json_object *messages = json_object_new_array();
    json_object *system = json_object_new_object();
    json_object_object_add(system, "role", json_object_new_string("system"));
    json_object_object_add(system, "content", json_object_new_string(
        "Control the visible Windows computer one safe step at a time. "
        "If the task only asks to analyze or describe the screen, do not send "
        "HID input; return done with the visible findings in reason. "
        "Return exactly one JSON object and no prose. Every response must "
        "include a concise observation of visible screen content and a short "
        "reason for the chosen action; do not include hidden chain-of-thought. "
        "Write observation and reason in the same language as the task when "
        "possible. The current HDMI screenshot is the only target display: "
        "do not decide success from main/secondary display labels, and only "
        "claim success when the requested result is visible in this frame. "
        "Examples: "
        "{\"observation\":\"...\",\"reason\":\"...\",\"action\":\"type\","
        "\"text\":\"printable ASCII, max 128\"}, "
        "{\"observation\":\"...\",\"reason\":\"...\",\"action\":\"hotkey\","
        "\"keys\":\"WIN+S\"}, "
        "{\"observation\":\"...\",\"reason\":\"...\","
        "\"action\":\"netease_search\",\"text\":\"xuliang\"}, "
        "{\"observation\":\"desktop; browser absent\",\"reason\":\"launch "
        "once\",\"action\":\"launch_browser\"}, "
        "{\"observation\":\"Windows Search visibly shows Microsoft Edge "
        "as the app result\",\"reason\":\"submit the verified browser "
        "result\",\"action\":\"browser_launch_submit\"}, "
        "{\"observation\":\"Chinese IME candidates are visible\","
        "\"reason\":\"switch to direct English input\","
        "\"action\":\"normalize_ime\"}, "
        "{\"observation\":\"Edge address bar is ready\","
        "\"reason\":\"prepare a verified URL\","
        "\"action\":\"browser_url_prepare\",\"url\":\"https://example.com\"}, "
        "{\"observation\":\"address bar exactly shows example.com\","
        "\"reason\":\"submit the verified URL\","
        "\"action\":\"browser_url_submit\"}, "
        "{\"observation\":\"target absent\",\"reason\":\"inspect the next "
        "window\",\"action\":\"window_cycle\"}, "
        "{\"observation\":\"target absent\",\"reason\":\"bring the selected "
        "window into HDMI\",\"action\":\"move_active_window\","
        "\"direction\":\"left or right\"}, "
        "{\"observation\":\"Edge icon is visible on the taskbar\","
        "\"reason\":\"focus a bounded taskbar item\","
        "\"action\":\"taskbar_cycle\"}, "
        "{\"observation\":\"Edge taskbar icon is selected\","
        "\"reason\":\"activate the visibly selected browser\","
        "\"action\":\"taskbar_activate\"}, "
        "{\"observation\":\"Edge icon is visibly present on the taskbar\","
        "\"reason\":\"launch the managed Edge app slot\","
        "\"action\":\"edge_taskbar_shortcut\"}, "
        "{\"observation\":\"...\",\"reason\":\"...\",\"action\":\"scroll\","
        "\"wheel\":-127..127}, "
        "{\"observation\":\"cursor hidden\",\"reason\":\"expose it\","
        "\"action\":\"cursor_probe\"}, "
        "{\"observation\":\"cursor is left of the target\","
        "\"reason\":\"move locally and inspect again\","
        "\"action\":\"cursor_move\",\"direction\":\"right\",\"amount\":8}, "
        "{\"observation\":\"cursor is over the target video thumbnail\","
        "\"reason\":\"activate the verified target\","
        "\"action\":\"cursor_click\",\"button\":1}, "
        "{\"observation\":\"...\",\"reason\":\"...\",\"action\":\"wait\"}, or "
        "{\"observation\":\"...\",\"action\":\"done\",\"reason\":\"...\"}. "
        "This device has a relative USB pointer. Never convert screenshot "
        "coordinates into pointer coordinates. While the cursor is hidden, "
        "cursor_probe may be retried up to four times; each bounded probe "
        "explores an expanding alternate direction and requires a fresh "
        "frame. Once visible, use small cursor_move actions with a fresh "
        "frame after every move. Use "
        "cursor_click only when the current observation explicitly places the "
        "visible cursor over the intended target. Never put "
        "non-ASCII characters in type. Transliterate Chinese names to pinyin; "
        "For browser tasks, use launch_browser at most once. It opens Windows "
        "Search and types edge without submitting. Inspect the next frame and "
        "use browser_launch_submit only when Windows Search visibly shows "
        "Microsoft Edge as the app result. When the current "
        "frame visibly shows VMware, launch_browser first releases VMware "
        "input capture with its standard Ctrl+Alt chord. If the launched "
        "browser is absent, use bounded window_cycle and move_active_window; "
        "do not launch it again. If an Edge icon is explicitly visible on "
        "the taskbar while the browser is absent, use edge_taskbar_shortcut "
        "before cursor_probe, taskbar_cycle, or more window recovery. This "
        "managed Windows image pins Edge to the Win+2 app slot; the executor "
        "allows one attempt only when the Edge icon is visible, and the next "
        "frame must still prove that Edge opened. Otherwise, cursor_probe, small "
        "cursor_move steps (amount 1 through 32), and a cursor_click visibly "
        "verified over that "
        "exact icon are the safe fallback. If pointer recovery is unreliable, "
        "use taskbar_cycle to focus and visibly inspect bounded Windows "
        "taskbar items. Use taskbar_activate only when the current frame "
        "explicitly shows Edge selected, focused, or highlighted. "
        "Never use edge_taskbar_shortcut when the Edge icon is absent. When "
        "an IME or garbled candidates "
        "are visible, call normalize_ime before preparing "
        "a URL. browser_url_prepare focuses the visible browser address bar "
        "and types without submitting. Inspect the next frame and call "
        "browser_url_submit only when the exact expected host is visibly in "
        "the address bar. browser_url_submit closes the visible suggestion "
        "dropdown with Escape before pressing Enter so the dropdown cannot "
        "swallow the address-bar submission. "
        "For NetEase tasks, netease_search is accepted only when your current "
        "observation affirmatively shows the NetEase window or player in this "
        "HDMI frame. It only focuses search inside that already-visible "
        "window, replaces the query, and submits it. If NetEase is absent, do "
        "not use netease_search, WIN+T, WIN+D, raw ALT+TAB, or raw "
        "WIN+SHIFT+LEFT/RIGHT. Use window_cycle once, inspect the next frame, "
        "then use move_active_window left or right only if the selected window "
        "must be brought into HDMI. The executor requires this order and "
        "limits both recovery actions. While NetEase is absent, all generic "
        "hotkeys and application input are blocked; never close an unrelated "
        "window just to keep searching. Use netease_search at most twice in one "
        "task. Do not use Win+R because its dialog can open on an uncaptured "
        "primary display. Do not use Windows Search and "
        "do not press SPACE to commit an IME candidate. When the music "
        "application is already focused and "
        "the requested track is visibly selected, SPACE toggles playback. "
        "A search result, selected row, or large Play button is not proof "
        "that music is playing. For a NetEase playback task, return done only "
        "when the bottom player visibly shows the requested artist together "
        "with pause bars or when its progress visibly advances between "
        "frames. Treat tiny track-name OCR as uncertain unless another large "
        "visible playback indicator agrees. If a VIP trial dialog appears "
        "after playback starts, ESC may dismiss it, but verify that the "
        "bottom player remains in the playing state. "
        "For a Bilibili playback task, a search result or opened video page is "
        "not completion. Return done only when the requested subject is "
        "visible and the player shows pause controls, a speaker indicator, or "
        "progress visibly advancing. For a Bilibili Wei Dongshan task, prefer "
        "the verifiable ASCII search URL https://search.bilibili.com/all?"
        "keyword=%E9%9F%A6%E4%B8%9C%E5%B1%B1 over trying to click the "
        "homepage search box; this ASCII percent encoding represents the "
        "exact Chinese name. "
        "If a launched window is absent from the screenshot, use "
        "WIN+SHIFT+LEFT or WIN+SHIFT+RIGHT to move it from another display. "
        "Use type for text and hotkey for named shortcuts. The action history "
        "records what was actually executed. "
        "After every action inspect the next screenshot and verify its effect. "
        "Never repeat an action that visibly failed without choosing a recovery. "
        "Never request the same HID action more than twice consecutively; the "
        "executor blocks the third identical action. "
        "never send a modifier-only hotkey, "
        "and never claim completion unless the requested result is visible."));
    json_object_array_add(messages, system);
    json_object *user = json_object_new_object();
    json_object_object_add(user, "role", json_object_new_string("user"));
    json_object *content = json_object_new_array();
    json_object *text = json_object_new_object();
    char prompt[6144];
    snprintf(prompt, sizeof(prompt),
             "Task: %.820s\nCurrent step: %d\nExecuted action history:\n"
             "%.4095s\n%s",
             task, step, history && history[0] ? history : "(none)",
             attempt > 0 ?
             "Your previous response was not one valid action. Return only "
             "one of the exact JSON objects defined by the system message." :
             "Choose one safe action from the current screenshot.");
    json_object_object_add(text, "type", json_object_new_string("text"));
    json_object_object_add(text, "text", json_object_new_string(prompt));
    json_object_array_add(content, text);
    json_object *image_part = json_object_new_object();
    json_object_object_add(image_part, "type", json_object_new_string("image_url"));
    json_object *image_url = json_object_new_object();
    size_t url_size = strlen(image) + 24;
    char *url = malloc(url_size);
    if (!url) {
        json_object_put(root);
        return NULL;
    }
    snprintf(url, url_size, "data:image/jpeg;base64,%s", image);
    json_object_object_add(image_url, "url", json_object_new_string(url));
    free(url);
    json_object_object_add(image_part, "image_url", image_url);
    json_object_array_add(content, image_part);
    json_object_object_add(user, "content", content);
    json_object_array_add(messages, user);
    json_object_object_add(root, "messages", messages);
    return root;
}

static json_object *model_action(const char *endpoint, const char *model,
                                 const char *key, const char *task, int step,
                                 int attempt, const char *history)
{
    size_t image_size = 0;
    char *raw_image = read_file(SCREEN_FILE, MAX_IMAGE, &image_size);
    if (!raw_image) return NULL;
    char *image = base64((unsigned char *)raw_image, image_size);
    free(raw_image);
    if (!image) return NULL;
    json_object *request =
        make_request(model, task, image, step, attempt, history);
    free(image);
    if (!request) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) {
        json_object_put(request);
        return NULL;
    }
    char *request_url = chat_completions_url(endpoint);
    if (!request_url) {
        curl_easy_cleanup(curl);
        json_object_put(request);
        return NULL;
    }
    struct response response = {0};
    size_t auth_size = strlen(key) + sizeof("Authorization: Bearer ");
    char *auth = malloc(auth_size);
    if (!auth) {
        free(request_url);
        curl_easy_cleanup(curl);
        json_object_put(request);
        return NULL;
    }
    snprintf(auth, auth_size, "Authorization: Bearer %s", key);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth);
    memset(auth, 0, auth_size);
    free(auth);
    if (!headers) {
        free(request_url);
        curl_easy_cleanup(curl);
        json_object_put(request);
        return NULL;
    }
    curl_easy_setopt(curl, CURLOPT_URL, request_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
                     json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#ifdef CAINFO_PATH
    curl_easy_setopt(curl, CURLOPT_CAINFO, CAINFO_PATH);
#endif
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(request_url);
    json_object_put(request);
    if (rc != CURLE_OK || status < 200 || status >= 300 || !response.data) {
        char message[256];
        snprintf(message, sizeof(message),
                 "Model request failed: curl=%s, HTTP %ld",
                 curl_easy_strerror(rc), status);
        log_event("system", "error", step, message);
        free(response.data);
        return NULL;
    }
    json_object *envelope = json_tokener_parse(response.data);
    free(response.data);
    if (!envelope) return NULL;
    json_object *choices = NULL, *first = NULL, *message = NULL, *content = NULL;
    if (!json_object_object_get_ex(envelope, "choices", &choices) ||
        !(first = json_object_array_get_idx(choices, 0)) ||
        !json_object_object_get_ex(first, "message", &message) ||
        !json_object_object_get_ex(message, "content", &content)) {
        json_object_put(envelope);
        return NULL;
    }
    const char *text = json_object_get_string(content);
    json_object *action = parse_action(text);
    json_object_put(envelope);
    return action;
}

static bool json_int(json_object *object, const char *name, int min, int max, int *out)
{
    json_object *value = NULL;
    if (!json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_int)) return false;
    *out = json_object_get_int(value);
    return *out >= min && *out <= max;
}

static bool json_optional_int(json_object *object, const char *name,
                              int min, int max, int fallback, int *out)
{
    json_object *value = NULL;
    if (!json_object_object_get_ex(object, name, &value)) {
        *out = fallback;
        return true;
    }
    return json_int(object, name, min, max, out);
}

static bool action_has_only(json_object *object, const char *const *keys,
                            size_t key_count)
{
    json_object_object_foreach(object, key, value) {
        (void)value;
        if (!strcmp(key, "observation") || !strcmp(key, "reason"))
            continue;
        bool found = false;
        for (size_t i = 0; i < key_count; i++) {
            if (!strcmp(key, keys[i])) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static bool action_metadata_valid(json_object *action)
{
    static const char *const names[] = {"observation", "reason"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        json_object *value = NULL;
        if (!json_object_object_get_ex(action, names[i], &value))
            continue;
        if (!json_object_is_type(value, json_type_string) ||
            json_object_get_string_len(value) < 1 ||
            json_object_get_string_len(value) > 2048)
            return false;
    }
    return true;
}

static const char *action_observation(json_object *action)
{
    json_object *observation = NULL;
    if (!json_object_object_get_ex(action, "observation", &observation) ||
        !json_object_is_type(observation, json_type_string))
        return NULL;
    return json_object_get_string(observation);
}

static bool text_contains_any(const char *text,
                              const char *const *needles, size_t count)
{
    if (!text)
        return false;
    for (size_t index = 0; index < count; index++) {
        size_t needle_length = strlen(needles[index]);
        for (const char *cursor = text; *cursor; cursor++) {
            if (!strncasecmp(cursor, needles[index], needle_length))
                return true;
        }
    }
    return false;
}

static bool task_targets_netease(const char *task)
{
    static const char *const names[] = {
        "netease", "cloudmusic", "cloud music", "网易云"
    };
    return text_contains_any(task, names, sizeof(names) / sizeof(names[0]));
}

static bool task_targets_browser(const char *task)
{
    static const char *const names[] = {
        "browser", "edge", "bilibili", "website", "web page",
        "浏览器", "网站", "网页", "b站", "哔哩哔哩"
    };
    return text_contains_any(task, names, sizeof(names) / sizeof(names[0]));
}

static bool task_targets_bilibili(const char *task)
{
    static const char *const names[] = {
        "bilibili", "b站", "哔哩哔哩"
    };
    return text_contains_any(task, names, sizeof(names) / sizeof(names[0]));
}

static bool task_targets_weidongshan(const char *task)
{
    static const char *const names[] = {
        "weidongshan", "韦东山"
    };
    return text_contains_any(task, names, sizeof(names) / sizeof(names[0]));
}

static bool observation_affirms_weidongshan(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const names[] = {
        "weidongshan", "韦东山"
    };
    return text_contains_any(observation, names,
                             sizeof(names) / sizeof(names[0]));
}

static bool observation_affirms_chinese_weidongshan(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const names[] = {"韦东山"};
    return text_contains_any(observation, names, 1);
}

static bool observation_affirms_visible_browser(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const names[] = {
        "microsoft edge", "edge browser", "edge浏览器", "browser window", "address bar",
        "browser tab", "bilibili", "浏览器窗口", "地址栏",
        "浏览器标签", "哔哩哔哩"
    };
    static const char *const negative[] = {
        "not visible", "not present", "isn't visible", "is not visible",
        "absent", "no edge", "no browser", "no visible edge",
        "no visible browser", "no visible windows browser",
        "edge browser icon", "edge icon", "browser icon",
        "browser on the taskbar", "未看到", "未显示", "未见",
        "看不到", "没有浏览器", "不在当前", "任务栏有edge",
        "任务栏中的edge", "edge浏览器图标", "edge图标", "浏览器图标",
        "windows search", "search panel", "best match", "app result",
        "windows 搜索", "搜索面板", "最佳匹配", "应用结果"
    };
    static const char *const context[] = {
        "window", "address bar", "browser tab", "page", "web content",
        "homepage", "search", "search results", "video player",
        "窗口", "地址栏", "浏览器标签", "页面", "网页内容",
        "首页", "搜索", "搜索结果", "视频播放器"
    };
    return text_contains_any(observation, names,
                             sizeof(names) / sizeof(names[0])) &&
           !text_contains_any(observation, negative,
                              sizeof(negative) / sizeof(negative[0])) &&
           text_contains_any(observation, context,
                             sizeof(context) / sizeof(context[0]));
}

static bool observation_affirms_edge_search_result(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const edge[] = {
        "microsoft edge", "edge browser", "edge浏览器"
    };
    static const char *const search[] = {
        "windows search", "search panel", "best match", "app result",
        "windows 搜索", "搜索面板", "最佳匹配", "应用结果"
    };
    static const char *const negative[] = {
        "not visible", "not shown", "absent", "未看到", "未显示", "看不到"
    };
    return text_contains_any(observation, edge,
                             sizeof(edge) / sizeof(edge[0])) &&
           text_contains_any(observation, search,
                             sizeof(search) / sizeof(search[0])) &&
           !text_contains_any(observation, negative,
                              sizeof(negative) / sizeof(negative[0]));
}

static bool observation_affirms_browser_taskbar_icon(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const browser[] = {
        "edge", "browser", "浏览器"
    };
    static const char *const icon[] = {
        "edge icon", "browser icon", "taskbar has edge",
        "taskbar shows edge", "taskbar with edge", "taskbar icons including edge",
        "icons including edge", "edge browser icon",
        "任务栏有edge", "任务栏显示edge", "任务栏可见edge",
        "任务栏中的edge", "edge图标", "edge等图标", "浏览器图标",
        "浏览器图标可见"
    };
    return text_contains_any(observation, browser,
                             sizeof(browser) / sizeof(browser[0])) &&
           text_contains_any(observation, icon,
                             sizeof(icon) / sizeof(icon[0]));
}

static bool observation_affirms_visible_vmware(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const names[] = {
        "vmware workstation", "vmware window", "vmware ubuntu",
        "vmware窗口", "vmware虚拟机"
    };
    return text_contains_any(observation, names,
                             sizeof(names) / sizeof(names[0]));
}

static bool observation_affirms_edge_taskbar_selected(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const edge[] = {
        "edge", "microsoft edge"
    };
    static const char *const selected[] = {
        "selected", "focused", "highlighted", "focus rectangle",
        "focus box", "已选中", "获得焦点", "高亮", "焦点框"
    };
    return text_contains_any(observation, edge,
                             sizeof(edge) / sizeof(edge[0])) &&
           text_contains_any(observation, selected,
                             sizeof(selected) / sizeof(selected[0]));
}

static bool observation_affirms_edge_second_app(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const edge[] = {
        "edge", "microsoft edge"
    };
    static const char *const second[] = {
        "second application icon", "second app icon", "2nd application icon",
        "second pinned app", "app slot 2", "第二个应用图标",
        "第2个应用图标", "第二个固定应用", "第2个固定应用"
    };
    static const char *const negative[] = {
        "not visible", "not shown", "absent", "未看到", "未显示", "看不到"
    };
    return text_contains_any(observation, edge,
                             sizeof(edge) / sizeof(edge[0])) &&
           text_contains_any(observation, second,
                             sizeof(second) / sizeof(second[0])) &&
           !text_contains_any(observation, negative,
                              sizeof(negative) / sizeof(negative[0]));
}

static bool observation_affirms_visible_bilibili(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const names[] = {
        "bilibili", "哔哩哔哩"
    };
    static const char *const negative[] = {
        "not visible", "not present", "isn't visible", "is not visible",
        "absent", "not loaded", "未看到", "未显示", "未加载",
        "看不到", "不在当前"
    };
    static const char *const context[] = {
        "page", "homepage", "search", "result", "video", "player",
        "title", "页面", "首页", "搜索", "结果", "视频", "播放器",
        "标题"
    };
    return text_contains_any(observation, names,
                             sizeof(names) / sizeof(names[0])) &&
           !text_contains_any(observation, negative,
                              sizeof(negative) / sizeof(negative[0])) &&
           text_contains_any(observation, context,
                             sizeof(context) / sizeof(context[0]));
}

static bool observation_mentions_ime_risk(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const risks[] = {
        "chinese ime", "pinyin ime", "ime candidate", "candidate list",
        "microsoft pinyin", "garbled ime", "garbled candidates",
        "garbled characters", "incorrect mixed text",
        "中文输入法", "拼音输入法", "微软拼音",
        "候选词", "候选框", "乱码", "文字错误"
    };
    return text_contains_any(observation, risks,
                             sizeof(risks) / sizeof(risks[0]));
}

static bool observation_affirms_pending_url(json_object *action,
                                             const char *url)
{
    const char *observation = action_observation(action);
    if (!observation || !url || !url[0] || observation_mentions_ime_risk(action))
        return false;
    const char *host = strstr(url, "://");
    host = host ? host + 3 : url;
    char hostname[128];
    size_t length = strcspn(host, "/?#");
    if (length == 0 || length >= sizeof(hostname))
        return false;
    memcpy(hostname, host, length);
    hostname[length] = '\0';
    const char *const values[] = {hostname};
    static const char *const context[] = {
        "address bar", "url", "entered", "typed", "地址栏", "网址",
        "已输入", "已键入"
    };
    static const char *const bilibili_names[] = {
        "bilibili", "b站", "哔哩哔哩"
    };
    static const char *const search_terms[] = {"search", "搜索"};
    static const char *const target_terms[] = {
        "weidongshan", "wei dongshan", "韦东山"
    };
    static const char *const url_terms[] = {"url", "link", "链接"};
    bool literal_host = text_contains_any(observation, values, 1);
    bool narrow_search_alias = !strcasecmp(hostname, "search.bilibili.com") &&
        strstr(url, "keyword=%E9%9F%A6%E4%B8%9C%E5%B1%B1") &&
        text_contains_any(observation, bilibili_names,
                          sizeof(bilibili_names) / sizeof(bilibili_names[0])) &&
        text_contains_any(observation, search_terms,
                          sizeof(search_terms) / sizeof(search_terms[0])) &&
        text_contains_any(observation, target_terms,
                          sizeof(target_terms) / sizeof(target_terms[0])) &&
        text_contains_any(observation, url_terms,
                          sizeof(url_terms) / sizeof(url_terms[0]));
    return (literal_host || narrow_search_alias) &&
           text_contains_any(observation, context,
                             sizeof(context) / sizeof(context[0]));
}

static bool observation_affirms_cursor_on_target(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const cursor[] = {
        "cursor", "pointer", "mouse pointer", "光标", "鼠标指针"
    };
    static const char *const relation[] = {
        "over the target", "over target", "over the video", "over the link",
        "over the button", "on the target", "inside the target",
        "hovering on the target", "target link", "target video",
        "over the edge icon", "over the browser icon", "位于目标",
        "悬停在目标", "目标上", "视频缩略图", "edge图标上",
        "浏览器图标上", "任务栏图标上"
    };
    return text_contains_any(observation, cursor,
                             sizeof(cursor) / sizeof(cursor[0])) &&
           text_contains_any(observation, relation,
                             sizeof(relation) / sizeof(relation[0]));
}

static bool observation_affirms_visible_cursor(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const cursor[] = {
        "cursor", "pointer", "光标", "鼠标指针"
    };
    static const char *const located[] = {
        "visible cursor", "visible pointer", "cursor visible", "cursor is visible",
        "pointer visible", "pointer is visible",
        "cursor visible near", "pointer visible near", "cursor is near",
        "pointer is near",
        "cursor on", "pointer on", "cursor is at", "cursor is on",
        "cursor is over", "cursor is left", "cursor is right",
        "cursor is above", "cursor is below", "pointer is at",
        "pointer is on", "pointer is over", "可见光标", "光标可见", "看到光标",
        "光标在", "光标位于", "光标处于", "光标靠近", "光标附近",
        "鼠标指针在", "鼠标指针位于", "鼠标指针靠近"
    };
    static const char *const hidden[] = {
        "cursor hidden", "pointer hidden", "cursor not visible",
        "pointer not visible", "cursor is hidden", "pointer is hidden",
        "hidden cursor", "光标隐藏", "隐藏光标",
        "未看到光标", "光标不可见", "看不到光标"
    };
    return text_contains_any(observation, cursor,
                             sizeof(cursor) / sizeof(cursor[0])) &&
           text_contains_any(observation, located,
                             sizeof(located) / sizeof(located[0])) &&
           !text_contains_any(observation, hidden,
                              sizeof(hidden) / sizeof(hidden[0]));
}

static bool observation_affirms_visible_netease(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const names[] = {
        "netease", "cloud music", "网易云"
    };
    static const char *const negative[] = {
        "not visible", "not present", "isn't visible", "is not visible",
        "absent", "no netease", "未看到", "未显示", "未见", "看不到",
        "没有网易云", "不在当前", "无网易云"
    };
    static const char *const visible_context[] = {
        "window", "player", "search", "song", "artist", "窗口", "界面",
        "播放器", "搜索", "歌曲", "歌手", "播放"
    };
    if (!text_contains_any(observation, names,
                           sizeof(names) / sizeof(names[0])) ||
        text_contains_any(observation, negative,
                          sizeof(negative) / sizeof(negative[0])))
        return false;
    return text_contains_any(observation, visible_context,
                             sizeof(visible_context) /
                             sizeof(visible_context[0]));
}

static bool observation_affirms_netease_playback(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const playback[] = {
        "is playing", "playing state", "pause bars", "progress advanced",
        "正在播放", "播放中", "暂停按钮", "暂停条", "进度前进"
    };
    return observation_affirms_visible_netease(action) &&
           text_contains_any(observation, playback,
                             sizeof(playback) / sizeof(playback[0]));
}

static bool observation_affirms_bilibili_playback(json_object *action)
{
    const char *observation = action_observation(action);
    static const char *const playback[] = {
        "is playing", "playing state", "pause icon", "pause bars",
        "progress advanced", "progress is advancing", "speaker icon",
        "正在播放", "播放中", "暂停按钮", "暂停图标",
        "进度前进", "进度条前进", "声音图标"
    };
    return observation_affirms_visible_bilibili(action) &&
           text_contains_any(observation, playback,
                             sizeof(playback) / sizeof(playback[0]));
}

static bool task_requests_playback(const char *task)
{
    static const char *const playback[] = {
        "play", "playing", "播放", "听歌", "音乐"
    };
    return text_contains_any(task, playback,
                             sizeof(playback) / sizeof(playback[0]));
}

static void log_action_metadata(json_object *action, int step,
                                const char *action_name)
{
    json_object *observation = NULL;
    if (json_object_object_get_ex(action, "observation", &observation))
        log_event("assistant", "observation", step,
                  json_object_get_string(observation));

    json_object *reason = NULL;
    if (strcmp(action_name, "done") &&
        json_object_object_get_ex(action, "reason", &reason))
        log_event("assistant", "decision", step,
                  json_object_get_string(reason));
}

static void append_action_history(char *history, size_t history_size,
                                  int step, const char *action)
{
    char entry[768];
    int length = snprintf(entry, sizeof(entry),
                          "Step %d executed: %.700s\n", step, action);
    if (length <= 0)
        return;
    size_t entry_size = (size_t)length;
    if (entry_size >= sizeof(entry))
        entry_size = sizeof(entry) - 1;
    size_t used = strlen(history);
    while (used + entry_size + 1 > history_size) {
        char *newline = strchr(history, '\n');
        if (!newline) {
            history[0] = '\0';
            used = 0;
            break;
        }
        size_t removed = (size_t)(newline + 1 - history);
        memmove(history, history + removed, used - removed + 1);
        used -= removed;
    }
    memcpy(history + used, entry, entry_size);
    history[used + entry_size] = '\0';
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 3 || strcmp(argv[1], "run")) {
        fprintf(stderr, "usage: %s run TASK [MAX_STEPS]\n", argv[0]);
        return 2;
    }
    int max_steps = argc > 3 ? atoi(argv[3]) : 20;
    if (max_steps < 1 || max_steps > 30) {
        fprintf(stderr, "MAX_STEPS must be 1..30\n");
        return 2;
    }
    size_t key_size = 0;
    char *key = secure_file(KEY_FILE) ?
        read_file(KEY_FILE, 4096, &key_size) : NULL;
    char *endpoint = NULL, *model = NULL;
    bool valid_key = valid_api_key(key, key_size);
    if (!valid_key || !parse_config(&endpoint, &model)) {
        fprintf(stderr, "configure key, HTTPS endpoint and model with aitvbox-agentctl\n");
        free(key);
        free(endpoint);
        free(model);
        return 1;
    }
    int lock_result = acquire_agent_lock();
    if (lock_result) {
        fprintf(stderr, "%s\n", lock_result > 0 ?
                "another autonomous agent is already active" :
                "cannot acquire autonomous agent lock");
        memset(key, 0, strlen(key));
        free(key);
        free(endpoint);
        free(model);
        return 1;
    }
    unlink(STOP_FILE);
    signal(SIGINT, stop_signal);
    signal(SIGTERM, stop_signal);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "cannot initialize HTTPS client\n");
        memset(key, 0, strlen(key));
        free(key);
        free(endpoint);
        free(model);
        unlink(AGENT_LOCK_FILE);
        return 1;
    }
    int result = 1;
    char action_history[ACTION_HISTORY_MAX] = {0};
    char previous_action[768] = {0};
    int consecutive_action_count = 0;
    int netease_search_count = 0;
    int window_cycle_count = 0;
    int window_move_count = 0;
    int browser_launch_count = 0;
    int vmware_release_count = 0;
    int taskbar_cycle_count = 0;
    int edge_taskbar_shortcut_count = 0;
    bool managed_edge_move_armed = false;
    int ime_normalize_count = 0;
    bool browser_launch_prepared = false;
    bool window_cycle_armed = false;
    int cursor_probe_count = 0;
    char pending_browser_url[129] = {0};
    bool netease_task = task_targets_netease(argv[2]);
    bool browser_task = task_targets_browser(argv[2]);
    bool bilibili_task = task_targets_bilibili(argv[2]);
    bool weidongshan_task = task_targets_weidongshan(argv[2]);
    bool playback_task = (netease_task || bilibili_task) &&
                         task_requests_playback(argv[2]);
    log_event("system", "started", 0,
              "Computer-control task started; HDMI and HID are reserved");
    for (int step = 1; step <= max_steps && !stop_requested &&
         access(STOP_FILE, F_OK); step++) {
        if (capture()) {
            log_event("system", "error", step, "HDMI capture failed");
            break;
        }
        log_event("tool", "capture", step, "HDMI frame captured");
        log_event("assistant", "thinking", step,
                  "Waiting for the model to describe the current HDMI frame");
        json_object *action = NULL;
        json_object *action_name = NULL;
        for (int attempt = 0; attempt < 2; attempt++) {
            action = model_action(
                endpoint, model, key, argv[2], step, attempt,
                action_history);
            if (action &&
                json_object_object_get_ex(action, "action", &action_name) &&
                json_object_is_type(action_name, json_type_string))
                break;
            if (action) {
                json_object_put(action);
                action = NULL;
            }
            action_name = NULL;
            if (attempt == 0 && !stop_requested &&
                access(STOP_FILE, F_OK)) {
                log_event("system", "retry", step,
                          "Model response was not one valid JSON action; "
                          "retrying once");
            }
        }
        if (stop_requested || !access(STOP_FILE, F_OK)) {
            if (action)
                json_object_put(action);
            break;
        }
        if (!action || !action_name) {
            log_event("system", "error", step,
                      "The model returned an invalid action");
            if (action) json_object_put(action);
            break;
        }
        const char *name = json_object_get_string(action_name);
        if (!action_metadata_valid(action)) {
            log_event("system", "error", step,
                      "Rejected invalid observation or action reason");
            json_object_put(action);
            break;
        }
        bool visible_netease =
            !netease_task || observation_affirms_visible_netease(action);
        bool visible_browser =
            !browser_task || observation_affirms_visible_browser(action);
        bool visible_browser_icon = browser_task &&
            observation_affirms_browser_taskbar_icon(action);
        bool visible_vmware = observation_affirms_visible_vmware(action);
        bool edge_search_result_visible =
            observation_affirms_edge_search_result(action);
        bool edge_taskbar_selected =
            observation_affirms_edge_taskbar_selected(action);
        bool edge_second_app = observation_affirms_edge_second_app(action);
        bool visible_bilibili =
            !bilibili_task || observation_affirms_visible_bilibili(action);
        bool visible_chinese_weidongshan =
            observation_affirms_chinese_weidongshan(action);
        bool verified_playback = !playback_task ||
            (netease_task && observation_affirms_netease_playback(action)) ||
            (bilibili_task && observation_affirms_bilibili_playback(action) &&
             (!weidongshan_task || observation_affirms_weidongshan(action)));
        bool ime_risk = observation_mentions_ime_risk(action);
        bool pending_url_visible = observation_affirms_pending_url(
            action, pending_browser_url);
        bool cursor_on_target = observation_affirms_cursor_on_target(action);
        bool visible_cursor = observation_affirms_visible_cursor(action);
        log_action_metadata(action, step, name);
        // The timeline already presents these fields as dedicated entries.
        // Keep the command card compact and keep executed-action history free
        // of repeated screen descriptions.
        json_object_object_del(action, "observation");
        if (strcmp(name, "done"))
            json_object_object_del(action, "reason");
        const char *action_text = json_object_to_json_string_ext(
            action, JSON_C_TO_STRING_PLAIN);
        log_event("assistant", "action", step, action_text);
        if (!strcmp(name, "cursor_probe") && !visible_cursor &&
            cursor_probe_count >= 4) {
            if (browser_task && taskbar_cycle_count < 8) {
                const char *keys = taskbar_cycle_count == 0 ? "WIN+T" : "RIGHT";
                if (send_hotkey(keys)) {
                    log_event("system", "error", step,
                              "Failed bounded taskbar keyboard recovery");
                    json_object_put(action);
                    break;
                }
                taskbar_cycle_count++;
                log_step_message(
                    "tool", "hid", step,
                    "Replaced exhausted pointer recovery with bounded taskbar "
                    "focus (%d/8); inspect the selected icon",
                    taskbar_cycle_count, 0);
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Pointer recovery exhausted; advanced one bounded taskbar "
                    "focus item for visual verification");
                previous_action[0] = '\0';
                consecutive_action_count = 0;
                json_object_put(action);
                usleep(500000);
                continue;
            }
            log_event(
                "system", "error", step,
                "Stopped safely: four expanding cursor probes completed but "
                "the pointer is still absent from the HDMI frame");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Cursor recovery exhausted: do not make blind pointer guesses");
            json_object_put(action);
            break;
        }
        if (strcmp(name, "done")) {
            if (!strcmp(previous_action, action_text)) {
                consecutive_action_count++;
            } else {
                snprintf(previous_action, sizeof(previous_action), "%.767s",
                         action_text);
                consecutive_action_count = 1;
            }
            bool bounded_hidden_cursor_probe =
                !strcmp(name, "cursor_probe") && !visible_cursor &&
                cursor_probe_count < 4;
            if (consecutive_action_count > 2 &&
                !bounded_hidden_cursor_probe) {
                log_event(
                    "system", "retry", step,
                    "Blocked a third consecutive identical action; inspect "
                    "the visible frame and choose a different recovery");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected repeated action: choose a different recovery");
                previous_action[0] = '\0';
                consecutive_action_count = 0;
                json_object_put(action);
                continue;
            }
        }
        if (strcmp(name, "window_cycle") &&
            strcmp(name, "move_active_window") && strcmp(name, "wait") &&
            !(browser_task && !visible_browser) &&
            !(netease_task && !visible_netease))
            window_cycle_armed = false;
        if (netease_task && !visible_netease &&
            strcmp(name, "done") && strcmp(name, "wait") &&
            strcmp(name, "window_cycle") &&
            strcmp(name, "move_active_window") &&
            strcmp(name, "netease_search") && strcmp(name, "hotkey")) {
            log_event(
                "system", "retry", step,
                "Blocked application input because NetEase is not visible "
                "in the current HDMI frame; recover the window first");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Rejected application input: NetEase absent from HDMI");
            json_object_put(action);
            continue;
        }
        if (bilibili_task && weidongshan_task && visible_browser &&
            visible_bilibili && !pending_browser_url[0] && !ime_risk &&
            !visible_chinese_weidongshan &&
            !strcmp(name, "cursor_probe")) {
            static const char search_url[] =
                "https://search.bilibili.com/all?keyword="
                "%E9%9F%A6%E4%B8%9C%E5%B1%B1";
            if (prepare_browser_url_text(search_url)) {
                log_event("system", "error", step,
                          "Failed to prepare the verified Bilibili search URL");
                json_object_put(action);
                break;
            }
            snprintf(pending_browser_url, sizeof(pending_browser_url), "%s",
                     search_url);
            log_event(
                "system", "retry", step,
                "Replaced unreliable homepage pointer search with the bounded "
                "Wei Dongshan Bilibili search URL");
            log_event(
                "tool", "hid", step,
                "Prepared the Wei Dongshan Bilibili search URL without "
                "submitting; verify it in the next HDMI frame");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Prepared the direct Bilibili search URL; submit only after "
                "the next frame visibly verifies search.bilibili.com");
            previous_action[0] = '\0';
            consecutive_action_count = 0;
            json_object_put(action);
            continue;
        }
        bool browser_recovery_request =
            !strcmp(name, "window_cycle") ||
            !strcmp(name, "move_active_window") ||
            !strcmp(name, "cursor_probe") ||
            !strcmp(name, "taskbar_cycle");
        if (browser_task && !visible_browser && managed_edge_move_armed &&
            browser_recovery_request) {
            if (send_hotkey("WIN+SHIFT+RIGHT")) {
                log_event("system", "error", step,
                          "Failed to move the managed Edge window into HDMI");
                json_object_put(action);
                break;
            }
            managed_edge_move_armed = false;
            log_event(
                "system", "retry", step,
                "Replaced generic recovery by moving the just-focused managed "
                "Edge window right into the HDMI display");
            log_event("tool", "hid", step,
                      "Moved the managed Edge window right for fresh-frame "
                      "HDMI verification");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Moved the just-focused managed Edge window right; verify the "
                "next HDMI frame before browser input");
            previous_action[0] = '\0';
            consecutive_action_count = 0;
            json_object_put(action);
            usleep(500000);
            continue;
        }
        if (browser_task && !visible_browser && visible_browser_icon &&
            !visible_vmware && browser_launch_count > 0 &&
            edge_taskbar_shortcut_count == 0 && browser_recovery_request) {
            if (send_hotkey("WIN+2")) {
                log_event("system", "error", step,
                          "Failed the managed Edge recovery shortcut");
                json_object_put(action);
                break;
            }
            edge_taskbar_shortcut_count++;
            managed_edge_move_armed = true;
            log_event(
                "system", "retry", step,
                "Replaced generic browser recovery with the visibly anchored "
                "managed Edge taskbar slot");
            log_event(
                "tool", "hid", step,
                "Focused the managed Edge Win+2 slot after the current frame "
                "visibly showed its taskbar icon");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Used the visible Edge icon and managed Win+3 slot before "
                "generic recovery; verify the next HDMI frame");
            previous_action[0] = '\0';
            consecutive_action_count = 0;
            json_object_put(action);
            sleep(3);
            continue;
        }
        if (browser_task && !visible_browser &&
            strcmp(name, "done") && strcmp(name, "wait") &&
            strcmp(name, "window_cycle") &&
            strcmp(name, "move_active_window") &&
            strcmp(name, "cursor_probe") &&
            strcmp(name, "cursor_move") &&
            strcmp(name, "taskbar_cycle") &&
            strcmp(name, "taskbar_activate") &&
            strcmp(name, "edge_taskbar_shortcut") &&
            strcmp(name, "browser_launch_submit") &&
            !(visible_browser_icon && !strcmp(name, "cursor_click")) &&
            (strcmp(name, "launch_browser") || browser_launch_count > 0)) {
            char recovery[256];
            if (browser_launch_count > 0 &&
                !recover_browser_window(
                    &window_cycle_count, &window_move_count,
                    &window_cycle_armed, recovery, sizeof(recovery))) {
                log_event("system", "retry", step,
                          "Replaced an unsafe browser action with bounded "
                          "window recovery");
                log_event("tool", "hid", step, recovery);
                append_action_history(
                    action_history, sizeof(action_history), step, recovery);
                previous_action[0] = '\0';
                consecutive_action_count = 0;
                json_object_put(action);
                usleep(500000);
                continue;
            }
            log_event(
                "system", "retry", step,
                "Blocked browser input because no browser is visible in the "
                "current HDMI frame; launch once or recover its window");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Rejected browser input: browser absent from HDMI");
            json_object_put(action);
            continue;
        }
        int a = 0, b = 0, c = 0, d = 0;
        if (!strcmp(name, "done")) {
            static const char *const keys[] = {"action", "reason"};
            json_object *reason = NULL;
            json_object_object_get_ex(action, "reason", &reason);
            if (!action_has_only(action, keys, 2) ||
                (reason && (!json_object_is_type(reason, json_type_string) ||
                            json_object_get_string_len(reason) > 512))) {
                log_event("system", "error", step,
                          "Rejected an unsafe completion action");
                json_object_put(action);
                break;
            }
            if ((browser_task && !visible_browser) ||
                (netease_task &&
                 (!visible_netease || (playback_task && !verified_playback))) ||
                (bilibili_task &&
                 (!visible_bilibili || (playback_task && !verified_playback)))) {
                log_event(
                    "system", "retry", step,
                    (browser_task && !visible_browser) ||
                    (netease_task && !visible_netease) ||
                    (bilibili_task && !visible_bilibili) ?
                    "Blocked completion because the target application is "
                    "absent from the current HDMI frame" :
                    "Blocked completion because playback is not visibly "
                    "verified in the current HDMI frame");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected completion: target/playback not visible on HDMI");
                json_object_put(action);
                continue;
            }
            log_event("assistant", "done", step,
                      reason ? json_object_get_string(reason) : "Task completed");
            result = 0;
            json_object_put(action);
            break;
        } else if (!strcmp(name, "wait")) {
            static const char *const keys[] = {"action"};
            if (!action_has_only(action, keys, 1)) {
                log_event("system", "error", step,
                          "Rejected an invalid wait action");
                json_object_put(action);
                break;
            }
            log_event("tool", "wait", step, "Waited for the screen to settle");
            sleep(1);
        } else if (!strcmp(name, "launch_browser")) {
            static const char *const keys[] = {"action"};
            if (!action_has_only(action, keys, 1) || !browser_task ||
                visible_browser || browser_launch_count >= 1) {
                log_event(
                    "system", "retry", step,
                    "Rejected browser launch: it is not needed or the task "
                    "already used its single launch attempt");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected browser launch: recover the existing window");
                json_object_put(action);
                continue;
            }
            if (visible_vmware && vmware_release_count == 0) {
                if (keyboard_action(0, 5)) {
                    log_event("system", "error", step,
                              "Failed to release VMware input capture");
                    json_object_put(action);
                    break;
                }
                vmware_release_count++;
                log_event("tool", "hid", step,
                          "Released visible VMware input capture with Ctrl+Alt "
                          "before launching the Windows browser");
                usleep(500000);
            }
            if (send_hotkey("WIN+S") ||
                (usleep(500000), type_ascii_text("edge"))) {
                log_event("system", "error", step,
                          "Failed the bounded browser launch action");
                json_object_put(action);
                break;
            }
            browser_launch_count++;
            browser_launch_prepared = true;
            log_event("tool", "hid", step,
                      "Prepared one browser launch through Windows Search; "
                      "the next frame must visibly verify the Edge app result");
            sleep(1);
        } else if (!strcmp(name, "browser_launch_submit")) {
            static const char *const keys[] = {"action"};
            if (!action_has_only(action, keys, 1) || !browser_task ||
                visible_browser || !browser_launch_prepared ||
                !edge_search_result_visible) {
                log_event(
                    "system", "retry", step,
                    "Blocked browser launch submission because the Microsoft "
                    "Edge Windows Search result is not visibly verified");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected browser launch submit: Edge search result not "
                    "visible");
                json_object_put(action);
                continue;
            }
            if (send_hotkey("ESC") ||
                (usleep(250000), keyboard_hold_action(40, 0, 1000000))) {
                log_event("system", "error", step,
                          "Failed to submit the verified Edge search result");
                json_object_put(action);
                break;
            }
            browser_launch_prepared = false;
            log_event("tool", "hid", step,
                      "Submitted the visibly verified Edge Windows Search "
                      "result");
            sleep(3);
        } else if (!strcmp(name, "normalize_ime")) {
            static const char *const keys[] = {"action"};
            if (!action_has_only(action, keys, 1) || !visible_browser ||
                !ime_risk || ime_normalize_count >= 2 ||
                send_hotkey("CTRL+SPACE")) {
                log_event("system", "retry", step,
                          "Rejected or exhausted bounded IME normalization");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected IME normalization: inspect the input indicator");
                json_object_put(action);
                continue;
            }
            ime_normalize_count++;
            log_event("tool", "hid", step,
                      "Toggled the visible browser input mode for verification");
        } else if (!strcmp(name, "browser_url_prepare")) {
            static const char *const keys[] = {"action", "url"};
            json_object *url = NULL;
            const char *url_text = NULL;
            if (json_object_object_get_ex(action, "url", &url) &&
                json_object_is_type(url, json_type_string))
                url_text = json_object_get_string(url);
            bool valid_url = url_text && strlen(url_text) <= 128 &&
                ascii_text_supported(url_text) &&
                (!strncasecmp(url_text, "https://", 8) ||
                 !strncasecmp(url_text, "http://", 7));
            if (!action_has_only(action, keys, 2) || !visible_browser ||
                !valid_url) {
                log_event("system", "error", step,
                          "Rejected an invalid visible-browser URL action");
                json_object_put(action);
                break;
            }
            if (ime_risk) {
                log_event(
                    "system", "retry", step,
                    "Blocked URL typing because the current frame shows IME "
                    "candidates or garbled text; normalize input first");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected URL prepare: visible IME risk");
                json_object_put(action);
                continue;
            }
            if (prepare_browser_url_text(url_text)) {
                log_event("system", "error", step,
                          "Failed to prepare the browser URL");
                json_object_put(action);
                break;
            }
            snprintf(pending_browser_url, sizeof(pending_browser_url), "%s",
                     url_text);
            log_event(
                "tool", "hid", step,
                "Prepared a browser URL without submitting; the next frame "
                "must visibly verify its host");
        } else if (!strcmp(name, "browser_url_submit")) {
            static const char *const keys[] = {"action"};
            if (!action_has_only(action, keys, 1) ||
                !pending_browser_url[0] || !pending_url_visible) {
                log_event(
                    "system", "retry", step,
                    "Blocked URL submission because the expected host is not "
                    "visibly verified in the address bar");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected URL submit: expected host not visible");
                json_object_put(action);
                continue;
            }
            if (send_hotkey("ENTER")) {
                log_event("system", "error", step,
                          "Failed to submit the verified browser URL");
                json_object_put(action);
                break;
            }
            pending_browser_url[0] = '\0';
            log_event("tool", "hid", step,
                      "Closed suggestions and submitted the address after "
                      "visible URL verification");
        } else if (!strcmp(name, "taskbar_cycle")) {
            static const char *const keys[] = {"action"};
            const char *hotkey = taskbar_cycle_count == 0 ? "WIN+T" : "RIGHT";
            if (!action_has_only(action, keys, 1) || !browser_task ||
                visible_browser || taskbar_cycle_count >= 8 ||
                send_hotkey(hotkey)) {
                log_event("system", "retry", step,
                          "Rejected or exhausted bounded taskbar cycling");
                json_object_put(action);
                continue;
            }
            taskbar_cycle_count++;
            log_step_message(
                "tool", "hid", step,
                "Focused the next bounded Windows taskbar item (%d/8); "
                "inspect the current selection",
                taskbar_cycle_count, 0);
        } else if (!strcmp(name, "taskbar_activate")) {
            static const char *const keys[] = {"action"};
            if (!action_has_only(action, keys, 1) || !browser_task ||
                visible_browser || !edge_taskbar_selected ||
                send_hotkey("ENTER")) {
                log_event(
                    "system", "retry", step,
                    "Blocked taskbar activation because Edge is not visibly "
                    "selected in the current HDMI frame");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected taskbar activation: Edge selection unverified");
                json_object_put(action);
                continue;
            }
            log_event("tool", "hid", step,
                      "Activated only the visibly selected Edge taskbar icon");
            sleep(2);
        } else if (!strcmp(name, "edge_taskbar_shortcut")) {
            static const char *const keys[] = {"action"};
            if (!action_has_only(action, keys, 1) || !browser_task ||
                visible_browser ||
                (!visible_browser_icon && !edge_second_app) ||
                edge_taskbar_shortcut_count >= 1) {
                log_event(
                    "system", "retry", step,
                    "Blocked Edge taskbar shortcut because the current frame "
                    "does not explicitly show the managed Edge taskbar icon");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected Edge taskbar shortcut: Edge taskbar icon not "
                    "visibly verified");
                json_object_put(action);
                continue;
            }
            if (send_hotkey("WIN+2")) {
                log_event("system", "error", step,
                          "Failed the visibly verified Edge app-slot shortcut");
                json_object_put(action);
                break;
            }
            edge_taskbar_shortcut_count++;
            managed_edge_move_armed = true;
            log_event(
                "tool", "hid", step,
                "Focused the managed Edge Win+2 slot only after visibly "
                "verifying its taskbar icon");
            sleep(3);
        } else if (!strcmp(name, "cursor_probe")) {
            static const char *const keys[] = {"action"};
            if (action_has_only(action, keys, 1) && browser_task &&
                visible_vmware && visible_browser_icon &&
                vmware_release_count == 0) {
                if (keyboard_action(0, 5)) {
                    log_event("system", "error", step,
                              "Failed to release VMware input capture");
                    json_object_put(action);
                    break;
                }
                vmware_release_count++;
                log_event("system", "retry", step,
                          "Released VMware input capture before probing the "
                          "Windows pointer");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "VMware input capture released with Ctrl+Alt; inspect the "
                    "fresh frame before cursor recovery");
                json_object_put(action);
                usleep(500000);
                continue;
            }
            if (!action_has_only(action, keys, 1) || visible_cursor ||
                cursor_probe_count >= 4 ||
                probe_cursor_visibility(cursor_probe_count)) {
                log_event("system", "retry", step,
                          "Rejected, failed, or exhausted bounded cursor probes");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    visible_cursor ?
                    "Cursor is already visible: use a small cursor_move" :
                    "Cursor probe unavailable or exhausted: stop guessing");
                json_object_put(action);
                continue;
            }
            cursor_probe_count++;
            log_step_message(
                "tool", "hid", step,
                "Completed bounded cursor visibility sweep (%d/4); locate "
                "the pointer in the next HDMI frame",
                cursor_probe_count, 0);
        } else if (!strcmp(name, "cursor_move")) {
            static const char *const keys[] = {
                "action", "direction", "amount"
            };
            json_object *direction = NULL;
            const char *direction_text = NULL;
            if (json_object_object_get_ex(action, "direction", &direction) &&
                json_object_is_type(direction, json_type_string))
                direction_text = json_object_get_string(direction);
            if (!action_has_only(action, keys, 3) || !visible_cursor ||
                !direction_text || !json_int(action, "amount", 1, 32, &a)) {
                log_event("system", "retry", step,
                          "Rejected an uncalibrated cursor move");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected cursor move: explicitly locate the visible "
                    "pointer and use an amount from 1 through 32");
                json_object_put(action);
                continue;
            }
            if (!strcasecmp(direction_text, "left")) b = -a;
            else if (!strcasecmp(direction_text, "right")) b = a;
            else if (!strcasecmp(direction_text, "up")) c = -a;
            else if (!strcasecmp(direction_text, "down")) c = a;
            else {
                log_event("system", "retry", step,
                          "Rejected an invalid cursor direction");
                json_object_put(action);
                continue;
            }
            if (hid_action("mouse", b, c, 0, 0)) {
                log_event("system", "error", step,
                          "USB HID cursor movement failed");
                json_object_put(action);
                break;
            }
            log_event("tool", "hid", step,
                      "Moved the cursor locally; verify its new visible position");
        } else if (!strcmp(name, "cursor_click")) {
            static const char *const keys[] = {"action", "button"};
            if (!action_has_only(action, keys, 2) || !cursor_on_target ||
                !json_int(action, "button", 1, 3, &a)) {
                log_event(
                    "system", "retry", step,
                    "Blocked cursor click because the current observation does "
                    "not prove that the visible cursor is on the target");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected cursor click: cursor/target alignment unverified");
                json_object_put(action);
                continue;
            }
            b = 1 << (a - 1);
            if (hid_action("mouse", 0, 0, b, 0) ||
                (usleep(50000), hid_action("mouse", 0, 0, 0, 0))) {
                log_event("system", "error", step,
                          "USB HID cursor click failed");
                json_object_put(action);
                break;
            }
            log_event("tool", "hid", step,
                      "Clicked only after visible cursor-target verification");
        } else if (!strcmp(name, "window_cycle")) {
            static const char *const keys[] = {"action"};
            if (action_has_only(action, keys, 1) &&
                window_cycle_armed && browser_task && !visible_browser &&
                browser_launch_count > 0) {
                char recovery[256];
                if (recover_browser_window(
                        &window_cycle_count, &window_move_count,
                        &window_cycle_armed, recovery, sizeof(recovery))) {
                    log_event("system", "retry", step,
                              "Exhausted bounded browser window recovery");
                    append_action_history(
                        action_history, sizeof(action_history), step,
                        "Rejected window recovery: task limit reached");
                    json_object_put(action);
                    continue;
                }
                log_event("system", "retry", step,
                          "Replaced a repeated window cycle with the bounded "
                          "move phase of browser recovery");
                log_event("tool", "hid", step, recovery);
                append_action_history(
                    action_history, sizeof(action_history), step, recovery);
                previous_action[0] = '\0';
                consecutive_action_count = 0;
                json_object_put(action);
                usleep(500000);
                continue;
            } else if (!action_has_only(action, keys, 1) ||
                       window_cycle_armed || window_cycle_count >= 8 ||
                       send_hotkey("ALT+TAB")) {
                log_event(
                    "system", "retry", step,
                    window_cycle_armed ?
                    "Rejected another window cycle: move the already selected "
                    "window left or right before cycling again" :
                    "Rejected or exhausted bounded window cycling");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected window cycle: invalid or task limit reached");
                json_object_put(action);
                continue;
            }
            window_cycle_count++;
            window_cycle_armed = true;
            log_step_message(
                "tool", "hid", step,
                "Cycled one window for HDMI inspection (%d/8)",
                window_cycle_count, 0);
        } else if (!strcmp(name, "move_active_window")) {
            static const char *const keys[] = {"action", "direction"};
            json_object *direction = NULL;
            const char *direction_text = NULL;
            if (json_object_object_get_ex(action, "direction", &direction) &&
                json_object_is_type(direction, json_type_string))
                direction_text = json_object_get_string(direction);
            const char *move_keys =
                direction_text && !strcasecmp(direction_text, "left") ?
                "WIN+SHIFT+LEFT" :
                direction_text && !strcasecmp(direction_text, "right") ?
                "WIN+SHIFT+RIGHT" : NULL;
            if (!action_has_only(action, keys, 2) || !move_keys ||
                !window_cycle_armed || window_move_count >= 8 ||
                send_hotkey(move_keys)) {
                log_event(
                    "system", "retry", step,
                    "Rejected window move: cycle a window first, then choose "
                    "one left/right move into the HDMI frame");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected window move: missing cycle or task limit reached");
                json_object_put(action);
                continue;
            }
            window_move_count++;
            window_cycle_armed = false;
            char move_message[256];
            snprintf(
                move_message, sizeof(move_message),
                "Moved the cycled active window %s for HDMI verification (%d/8)",
                direction_text, window_move_count);
            log_event("tool", "hid", step, move_message);
        } else if (!strcmp(name, "netease_search")) {
            static const char *const keys[] = {"action", "text"};
            json_object *text = NULL;
            if (netease_task && !visible_netease) {
                log_event(
                    "system", "retry", step,
                    "Blocked NetEase search because the current observation "
                    "does not prove its window is visible on HDMI");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected NetEase search: app absent from HDMI frame");
                json_object_put(action);
                continue;
            }
            if (netease_search_count >= 2) {
                log_event(
                    "system", "retry", step,
                    "Blocked more than two NetEase searches in one task; "
                    "inspect the visible result and choose a different "
                    "recovery");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected NetEase search: task limit of two reached");
                json_object_put(action);
                continue;
            }
            if (!action_has_only(action, keys, 2) ||
                !json_object_object_get_ex(action, "text", &text) ||
                !json_object_is_type(text, json_type_string) ||
                !ascii_text_supported(json_object_get_string(text)) ||
                focus_netease_and_search(json_object_get_string(text))) {
                log_event("system", "error", step,
                          "Rejected or failed deterministic NetEase search");
                json_object_put(action);
                break;
            }
            netease_search_count++;
            log_event("tool", "hid", step,
                      "Searched the NetEase window already visible in the "
                      "current HDMI frame with the validated ASCII query");
        } else if (!strcmp(name, "click")) {
            log_event("system", "retry", step,
                      "Absolute clicks are unavailable on this relative HID; "
                      "choose a keyboard shortcut or TAB navigation");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Rejected click: use keyboard navigation");
            json_object_put(action);
            continue;
        } else if (!strcmp(name, "move")) {
            log_event("system", "retry", step,
                      "Absolute pointer movement is unavailable; choose "
                      "keyboard navigation");
            append_action_history(
                action_history, sizeof(action_history), step,
                "Rejected move: use keyboard navigation");
            json_object_put(action);
            continue;
        } else if (!strcmp(name, "type")) {
            static const char *const keys[] = {"action", "text"};
            json_object *text = NULL;
            if (!action_has_only(action, keys, 2) ||
                !json_object_object_get_ex(action, "text", &text) ||
                !json_object_is_type(text, json_type_string) ||
                json_object_get_string_len(text) < 1 ||
                json_object_get_string_len(text) > 128) {
                log_event("system", "error", step,
                          "Rejected an invalid text action");
                json_object_put(action);
                break;
            }
            const char *typed = json_object_get_string(text);
            const char *alias = ascii_alias_for_text(typed);
            if (alias) {
                typed = alias;
                log_event("system", "retry", step,
                          "Converted a non-ASCII application name to its "
                          "validated pinyin search alias");
            }
            if (type_ascii_text(typed)) {
                log_event("system", "retry", step,
                          "Non-ASCII text cannot be emitted by USB HID; "
                          "retry with an ASCII English or pinyin alias");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected type: use printable ASCII or pinyin");
                json_object_put(action);
                continue;
            }
            log_event("tool", "hid", step,
                      alias ? "Typed validated pinyin alias" :
                              "Typed validated ASCII text");
        } else if (!strcmp(name, "hotkey")) {
            static const char *const keys[] = {"action", "keys"};
            json_object *key_text = NULL;
            bool key_valid =
                json_object_object_get_ex(action, "keys", &key_text) &&
                json_object_is_type(key_text, json_type_string);
            const char *requested_keys =
                key_valid ? json_object_get_string(key_text) : NULL;
            if (netease_task && !visible_netease && requested_keys) {
                log_event(
                    "system", "retry", step,
                    "Blocked blind NetEase hotkey while its window is absent "
                    "from HDMI; use bounded window recovery actions");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected blind hotkey: NetEase absent from HDMI");
                json_object_put(action);
                continue;
            }
            if (netease_task && requested_keys &&
                (!strcasecmp(requested_keys, "WIN+T") ||
                 !strcasecmp(requested_keys, "WIN+D") ||
                 !strcasecmp(requested_keys, "ALT+TAB") ||
                 !strcasecmp(requested_keys, "WIN+SHIFT+LEFT") ||
                 !strcasecmp(requested_keys, "WIN+SHIFT+RIGHT"))) {
                log_event(
                    "system", "retry", step,
                    "Blocked unstructured display navigation; use "
                    "window_cycle and move_active_window so ordering and "
                    "task limits are enforced");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected display hotkey: use structured recovery");
                json_object_put(action);
                continue;
            }
            if (!action_has_only(action, keys, 2) || !key_valid ||
                send_hotkey(json_object_get_string(key_text))) {
                log_event("system", "retry", step,
                          "Rejected an unsupported or modifier-only hotkey; "
                          "choose one named non-modifier key");
                append_action_history(
                    action_history, sizeof(action_history), step,
                    "Rejected hotkey: include one named non-modifier key");
                json_object_put(action);
                continue;
            }
            log_event("tool", "hid", step,
                      "Named keyboard shortcut sent");
        } else if (!strcmp(name, "scroll")) {
            static const char *const keys[] = {"action", "wheel"};
            if (!action_has_only(action, keys, 2) ||
                !json_int(action, "wheel", -127, 127, &a) ||
                hid_action("mouse", 0, 0, 0, a)) {
                log_event("system", "error", step,
                          "Rejected or failed a scroll action");
                json_object_put(action);
                break;
            }
            char message[128];
            snprintf(message, sizeof(message),
                     "Scrolled by %d wheel units", a);
            log_event("tool", "hid", step, message);
        } else if (!strcmp(name, "key")) {
            static const char *const keys[] = {
                "action", "keycode", "modifier"
            };
            if (!action_has_only(action, keys, 3) ||
                !json_int(action, "keycode", 0, 101, &a) ||
                !json_optional_int(action, "modifier", 0, 255, 0, &b)) {
                log_event("system", "error", step,
                          "Rejected an invalid keyboard action");
                json_object_put(action);
                break;
            }
            if (hid_action("key", a, b, 0, 0)) {
                log_event("tool", "error", step,
                          "USB HID keyboard report failed");
                json_object_put(action);
                break;
            }
            log_step_message(
                "tool", "hid", step,
                "Keyboard report sent (keycode=%d, modifier=%d)", a, b);
        } else if (!strcmp(name, "mouse")) {
            static const char *const keys[] = {
                "action", "dx", "dy", "buttons", "wheel"
            };
            if (!action_has_only(action, keys, 5) ||
                !json_int(action, "dx", -127, 127, &a) ||
                !json_int(action, "dy", -127, 127, &b) ||
                !json_optional_int(action, "buttons", 0, 7, 0, &c) ||
                !json_optional_int(action, "wheel", -127, 127, 0, &d)) {
                log_event("system", "error", step,
                          "Rejected an invalid pointer action");
                json_object_put(action);
                break;
            }
            if (hid_action("mouse", a, b, c, d)) {
                log_event("tool", "error", step,
                          "USB HID pointer report failed");
                json_object_put(action);
                break;
            }
            char message[256];
            snprintf(message, sizeof(message),
                     "Pointer report sent (dx=%d, dy=%d, buttons=%d, wheel=%d)",
                     a, b, c, d);
            log_event("tool", "hid", step, message);
        } else {
            log_event("system", "error", step,
                      "Rejected an unsupported model action");
            json_object_put(action);
            break;
        }
        append_action_history(
            action_history, sizeof(action_history), step, action_text);
        json_object_put(action);
        usleep(500000);
    }
    if (stop_requested || !access(STOP_FILE, F_OK))
        log_event("system", "stopped", 0, "Task stopped by the user");
    else if (result != 0)
        log_event("system", "failed", 0,
                  "Task ended before a verified completion");
    hid_action("key", 0, 0, 0, 0);
    hid_action("mouse", 0, 0, 0, 0);
    curl_global_cleanup();
    memset(key, 0, strlen(key));
    free(key);
    free(endpoint);
    free(model);
    unlink(AGENT_LOCK_FILE);
    return result;
}
