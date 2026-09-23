#define _GNU_SOURCE

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "safe_io.h"

#ifndef CONTROL_SOCKET
#define CONTROL_SOCKET "/var/run/aitvbox/control.sock"
#endif
#ifndef KEY_FILE
#define KEY_FILE "/overlay/aitvbox/secrets/model-api-key"
#endif
#ifndef CONFIG_FILE
#define CONFIG_FILE "/overlay/aitvbox/secrets/model.conf"
#endif
#ifndef STOP_FILE
#define STOP_FILE "/var/run/aitvbox/agent.stop"
#endif
#ifndef AGENT_LOCK_FILE
#define AGENT_LOCK_FILE "/var/run/aitvbox/agent.lock"
#endif
#ifndef AGENT_PATH
#define AGENT_PATH "/usr/bin/aitvbox-agentd"
#endif
#ifndef HIDCTL_PATH
#define HIDCTL_PATH "/usr/bin/aitvbox-hidctl"
#endif
#ifndef AGENT_LOG
#define AGENT_LOG "/var/run/aitvbox/agent.log"
#endif
#ifndef DEFAULT_ENDPOINT
#define DEFAULT_ENDPOINT "https://api.openai.com/v1/chat/completions"
#endif
#ifndef DEFAULT_MODEL
#define DEFAULT_MODEL "gpt-4o-mini"
#endif

#define REQUEST_MAX 8192
#define TASK_MAX 512
#define PROVIDER_VALUE_MAX 2048
#define AGENT_LOG_MAX (64 * 1024)
#define AGENT_HISTORY_MAX 80
#define AGENT_HISTORY_LINE_MAX 2048

enum control_state {
    CONTROL_IDLE,
    CONTROL_RUNNING,
    CONTROL_STOPPING,
    CONTROL_SUCCEEDED,
    CONTROL_FAILED,
};

static volatile sig_atomic_t shutdown_requested;
static pid_t agent_pid = -1;
static enum control_state state = CONTROL_IDLE;

static void signal_handler(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

static bool secure_regular_file(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_uid == geteuid() && (st.st_mode & 0077) == 0;
}

static bool valid_api_key(const char *key)
{
    if (!key)
        return false;
    size_t length = strlen(key);
    if (length < 12 || length > 4096)
        return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char value = (unsigned char)key[i];
        if (value <= 32 || value >= 127)
            return false;
    }
    return true;
}

static bool valid_provider_value(const char *value, size_t max_length)
{
    if (!value)
        return false;
    size_t length = strlen(value);
    if (length == 0 || length > max_length)
        return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)value[i];
        if (byte < 32 || byte >= 127)
            return false;
    }
    return true;
}

static bool valid_endpoint(const char *endpoint)
{
    return valid_provider_value(endpoint, PROVIDER_VALUE_MAX) &&
           !strncmp(endpoint, "https://", 8);
}

static bool model_supports_computer_control(const char *model)
{
    return valid_provider_value(model, PROVIDER_VALUE_MAX) &&
           !strcasestr(model, "embedding");
}

static int write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        cursor += written;
        length -= (size_t)written;
    }
    return 0;
}

static int ensure_default_provider(void)
{
    if (secure_regular_file(CONFIG_FILE))
        return 0;
    char config[1024];
    int length = snprintf(config, sizeof(config), "ENDPOINT=%s\nMODEL=%s\n",
                          DEFAULT_ENDPOINT, DEFAULT_MODEL);
    if (length < 0 || length >= (int)sizeof(config))
        return -1;
    return aitvbox_atomic_write_file(CONFIG_FILE, config, (size_t)length,
                                     0600);
}

static bool read_provider(char *endpoint, size_t endpoint_size,
                          char *model, size_t model_size)
{
    endpoint[0] = '\0';
    model[0] = '\0';
    if (!secure_regular_file(CONFIG_FILE))
        return false;

    int fd = open(CONFIG_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    char buffer[PROVIDER_VALUE_MAX * 2 + 64];
    ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (length <= 0 || (size_t)length >= sizeof(buffer))
        return false;
    buffer[length] = '\0';

    char *save = NULL;
    for (char *line = strtok_r(buffer, "\n", &save);
         line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "ENDPOINT=", 9))
            snprintf(endpoint, endpoint_size, "%s", line + 9);
        else if (!strncmp(line, "MODEL=", 6))
            snprintf(model, model_size, "%s", line + 6);
    }
    return valid_endpoint(endpoint) &&
           valid_provider_value(model, PROVIDER_VALUE_MAX);
}

static bool object_has_only(json_object *object, const char *const *fields,
                            size_t field_count)
{
    json_object_object_foreach(object, name, value) {
        (void)value;
        bool found = false;
        for (size_t i = 0; i < field_count; i++) {
            if (!strcmp(name, fields[i])) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static json_object *response(bool ok, const char *message)
{
    json_object *root = json_object_new_object();
    json_object_object_add(root, "ok", json_object_new_boolean(ok));
    if (message)
        json_object_object_add(root, "message", json_object_new_string(message));
    return root;
}

static void reap_agent(void)
{
    if (agent_pid <= 0)
        return;
    int status_code = 0;
    pid_t result = waitpid(agent_pid, &status_code, WNOHANG);
    if (result < 0 && errno == ECHILD) {
        agent_pid = -1;
        state = state == CONTROL_STOPPING ? CONTROL_IDLE : CONTROL_FAILED;
        return;
    }
    if (result != agent_pid)
        return;
    agent_pid = -1;
    if (state == CONTROL_STOPPING) {
        state = CONTROL_IDLE;
    } else if (WIFEXITED(status_code) && WEXITSTATUS(status_code) == 0) {
        state = CONTROL_SUCCEEDED;
    } else {
        state = CONTROL_FAILED;
    }
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

static bool lock_owner_active(void)
{
    int fd = open(AGENT_LOCK_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    char buffer[32] = {0};
    ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (length <= 0)
        return false;
    char *end = NULL;
    long pid = strtol(buffer, &end, 10);
    return pid > 1 && end && (*end == '\0' || *end == '\n') &&
           process_active(pid);
}

static int run_hid_release(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        char *key[] = {"aitvbox-hidctl", "key", "0", "0", NULL};
        execv(HIDCTL_PATH, key);
        _exit(127);
    }
    if (pid < 0)
        return -1;
    int status_code = 0;
    while (waitpid(pid, &status_code, 0) < 0 && errno == EINTR) {}

    pid = fork();
    if (pid == 0) {
        char *mouse[] = {
            "aitvbox-hidctl", "mouse", "0", "0", "0", "0", NULL
        };
        execv(HIDCTL_PATH, mouse);
        _exit(127);
    }
    if (pid < 0)
        return -1;
    while (waitpid(pid, &status_code, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status_code) && WEXITSTATUS(status_code) == 0 ? 0 : -1;
}

static void request_stop(void)
{
    static const char stopped[] = "stop\n";
    (void)aitvbox_atomic_write_file(STOP_FILE, stopped,
                                    sizeof(stopped) - 1, 0600);
    if (agent_pid > 0) {
        state = CONTROL_STOPPING;
        (void)kill(agent_pid, SIGTERM);
        int status_code = 0;
        pid_t result = 0;
        for (int attempt = 0; attempt < 30; attempt++) {
            result = waitpid(agent_pid, &status_code, WNOHANG);
            if (result == agent_pid || (result < 0 && errno == ECHILD))
                break;
            if (result < 0 && errno != EINTR)
                break;
            usleep(100000);
        }
        if (result == 0) {
            (void)kill(agent_pid, SIGKILL);
            while ((result = waitpid(agent_pid, &status_code, 0)) < 0 &&
                   errno == EINTR) {}
        }
        agent_pid = -1;
        state = CONTROL_IDLE;
    } else {
        state = CONTROL_IDLE;
    }
    (void)run_hid_release();
}

static const char *state_name(void)
{
    switch (state) {
    case CONTROL_RUNNING: return "running";
    case CONTROL_STOPPING: return "stopping";
    case CONTROL_SUCCEEDED: return "succeeded";
    case CONTROL_FAILED: return "failed";
    default: return "idle";
    }
}

static char *read_agent_log(size_t *length_out)
{
    int fd = open(AGENT_LOG, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > AGENT_LOG_MAX) {
        close(fd);
        return NULL;
    }
    char *data = malloc((size_t)st.st_size + 1);
    if (!data) {
        close(fd);
        return NULL;
    }
    ssize_t length = read(fd, data, (size_t)st.st_size);
    close(fd);
    if (length <= 0) {
        free(data);
        return NULL;
    }
    data[length] = '\0';
    if (length_out)
        *length_out = (size_t)length;
    return data;
}

static json_object *agent_history(void)
{
    json_object *history = json_object_new_array();
    size_t length = 0;
    char *data = read_agent_log(&length);
    if (!data)
        return history;

    size_t count = 0;
    char *cursor = data;
    char *end = data + length;
    while (cursor < end && count < AGENT_HISTORY_MAX) {
        char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
        char *line_end = newline ? newline : end;
        while (line_end > cursor && line_end[-1] == '\r')
            line_end--;
        size_t line_length = (size_t)(line_end - cursor);
        if (line_length > 0 && line_length <= AGENT_HISTORY_LINE_MAX) {
            char saved = *line_end;
            *line_end = '\0';
            json_object *entry = json_tokener_parse(cursor);
            *line_end = saved;
            json_object *role = NULL, *type = NULL, *message = NULL;
            if (entry && json_object_is_type(entry, json_type_object) &&
                json_object_object_get_ex(entry, "role", &role) &&
                json_object_is_type(role, json_type_string) &&
                json_object_object_get_ex(entry, "type", &type) &&
                json_object_is_type(type, json_type_string) &&
                json_object_object_get_ex(entry, "message", &message) &&
                json_object_is_type(message, json_type_string) &&
                json_object_get_string_len(message) <= 1024) {
                json_object_array_add(history, entry);
                count++;
            } else if (entry) {
                json_object_put(entry);
            }
        }
        cursor = newline ? newline + 1 : end;
    }
    free(data);
    return history;
}

static int write_history_event(int fd, const char *role, const char *type,
                               int step, const char *message)
{
    json_object *entry = json_object_new_object();
    if (!entry)
        return -1;
    json_object_object_add(entry, "role", json_object_new_string(role));
    json_object_object_add(entry, "type", json_object_new_string(type));
    if (step > 0)
        json_object_object_add(entry, "step", json_object_new_int(step));
    json_object_object_add(entry, "message", json_object_new_string(message));
    const char *text = json_object_to_json_string_ext(
        entry, JSON_C_TO_STRING_PLAIN);
    int result = write_all(fd, text, strlen(text)) ||
                 write_all(fd, "\n", 1);
    json_object_put(entry);
    return result;
}

static json_object *status_response(void)
{
    reap_agent();
    if (agent_pid < 0 &&
        state != CONTROL_SUCCEEDED && state != CONTROL_FAILED) {
        state = lock_owner_active() ? CONTROL_RUNNING : CONTROL_IDLE;
    }

    json_object *root = response(true, NULL);
    char endpoint[PROVIDER_VALUE_MAX + 1];
    char model[PROVIDER_VALUE_MAX + 1];
    bool key_configured = secure_regular_file(KEY_FILE);
    bool provider_valid =
        read_provider(endpoint, sizeof(endpoint), model, sizeof(model));
    bool provider_configured =
        provider_valid && model_supports_computer_control(model);
    json_object_object_add(root, "state", json_object_new_string(state_name()));
    json_object_object_add(root, "configured",
                           json_object_new_boolean(key_configured &&
                                                   provider_configured));
    json_object_object_add(root, "keyConfigured",
                           json_object_new_boolean(key_configured));
    json_object_object_add(root, "providerConfigured",
                           json_object_new_boolean(provider_configured));
    if (provider_valid) {
        json_object_object_add(root, "endpoint",
                               json_object_new_string(endpoint));
        json_object_object_add(root, "model", json_object_new_string(model));
    }
    json_object *history = agent_history();
    json_object_object_add(root, "history", history);
    size_t history_size = json_object_array_length(history);
    if (history_size > 0 &&
        (state == CONTROL_SUCCEEDED || state == CONTROL_FAILED)) {
        json_object *last = json_object_array_get_idx(
            history, history_size - 1);
        json_object *message = NULL;
        if (last && json_object_object_get_ex(last, "message", &message) &&
            json_object_is_type(message, json_type_string))
            json_object_object_add(
                root, "detail",
                json_object_new_string(json_object_get_string(message)));
    }
    if (provider_valid && !provider_configured)
        json_object_object_add(
            root, "detail",
            json_object_new_string(
                "The selected model is embedding-only; API key permissions "
                "do not change that model's output type. For Volcengine "
                "Coding Plan, use doubao-seed-2.0-code."));
    return root;
}

static json_object *configure_provider(json_object *request)
{
    static const char *const fields[] = {"command", "endpoint", "model"};
    json_object *endpoint_object = NULL;
    json_object *model_object = NULL;
    reap_agent();
    if (!object_has_only(request, fields, 3) ||
        !json_object_object_get_ex(request, "endpoint", &endpoint_object) ||
        !json_object_is_type(endpoint_object, json_type_string) ||
        !json_object_object_get_ex(request, "model", &model_object) ||
        !json_object_is_type(model_object, json_type_string))
        return response(false, "invalid provider request");
    if (agent_pid > 0 || lock_owner_active())
        return response(false, "stop the current task first");

    const char *endpoint = json_object_get_string(endpoint_object);
    const char *model = json_object_get_string(model_object);
    if (!valid_endpoint(endpoint))
        return response(false, "endpoint must be a printable HTTPS URL");
    if (!valid_provider_value(model, PROVIDER_VALUE_MAX))
        return response(false, "model must be a printable value");
    if (!model_supports_computer_control(model))
        return response(
            false,
            "the selected model is embedding-only; for Volcengine Coding "
            "Plan use doubao-seed-2.0-code");

    size_t length = strlen(endpoint) + strlen(model) + 18;
    char *content = malloc(length);
    if (!content)
        return response(false, "out of memory");
    int written = snprintf(content, length, "ENDPOINT=%s\nMODEL=%s\n",
                           endpoint, model);
    int result = written < 0 || (size_t)written >= length ?
        -1 : aitvbox_atomic_write_file(CONFIG_FILE, content,
                                       (size_t)written, 0600);
    free(content);
    return result ? response(false, "cannot store provider configuration") :
                    response(true, "provider configured");
}

static json_object *set_key(json_object *request)
{
    static const char *const fields[] = {"command", "key"};
    json_object *key_object = NULL;
    if (!object_has_only(request, fields, 2) ||
        !json_object_object_get_ex(request, "key", &key_object) ||
        !json_object_is_type(key_object, json_type_string))
        return response(false, "invalid key request");
    const char *key_text = json_object_get_string(key_object);
    if (!valid_api_key(key_text))
        return response(false, "key must be a printable value of 12..4096 bytes");
    size_t length = strlen(key_text);
    char *copy = malloc(length + 2);
    if (!copy)
        return response(false, "out of memory");
    memcpy(copy, key_text, length);
    copy[length++] = '\n';
    copy[length] = '\0';
    int result = aitvbox_atomic_write_file(KEY_FILE, copy, length, 0600);
    aitvbox_secure_clear(copy, length + 1);
    free(copy);
    (void)json_object_set_string(key_object, "");
    if (result || ensure_default_provider())
        return response(false, "cannot store model configuration");
    return response(true, "key configured");
}

static json_object *clear_key(json_object *request)
{
    static const char *const fields[] = {"command"};
    reap_agent();
    if (!object_has_only(request, fields, 1))
        return response(false, "invalid clear request");
    if (agent_pid > 0 || lock_owner_active())
        return response(false, "stop the current task first");
    if (aitvbox_durable_unlink(KEY_FILE))
        return response(false, "cannot remove key");
    return response(true, "key cleared");
}

static json_object *start_task(json_object *request)
{
    static const char *const fields[] = {"command", "task", "maxSteps"};
    json_object *task_object = NULL;
    json_object *steps_object = NULL;
    reap_agent();
    if (!object_has_only(request, fields, 3) ||
        !json_object_object_get_ex(request, "task", &task_object) ||
        !json_object_is_type(task_object, json_type_string))
        return response(false, "invalid task request");
    if (agent_pid > 0 || lock_owner_active())
        return response(false, "a task is already running");
    if (!secure_regular_file(KEY_FILE) || ensure_default_provider())
        return response(false, "configure the model key first");
    char endpoint[PROVIDER_VALUE_MAX + 1];
    char model[PROVIDER_VALUE_MAX + 1];
    if (!read_provider(endpoint, sizeof(endpoint), model, sizeof(model)))
        return response(false, "configure a valid model provider first");
    const char *task = json_object_get_string(task_object);
    size_t task_length = strlen(task);
    if (task_length == 0 || task_length > TASK_MAX)
        return response(false, "task length must be 1..512 bytes");
    for (size_t i = 0; i < task_length; i++) {
        if ((unsigned char)task[i] < 32)
            return response(false, "task contains control characters");
    }
    int max_steps = 20;
    if (json_object_object_get_ex(request, "maxSteps", &steps_object)) {
        if (!json_object_is_type(steps_object, json_type_int))
            return response(false, "maxSteps must be an integer");
        max_steps = json_object_get_int(steps_object);
    }
    if (max_steps < 1 || max_steps > 30)
        return response(false, "maxSteps must be 1..30");

    int log_fd = open(AGENT_LOG,
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                      0600);
    if (log_fd < 0)
        return response(false, "cannot open agent log");
    if (write_history_event(log_fd, "user", "task", 0, task)) {
        close(log_fd);
        return response(false, "cannot initialize agent history");
    }
    if (!model_supports_computer_control(model)) {
        static const char error[] =
            "the selected model is embedding-only; for Volcengine Coding "
            "Plan use doubao-seed-2.0-code";
        (void)write_history_event(log_fd, "system", "error", 0, error);
        close(log_fd);
        state = CONTROL_FAILED;
        return response(false, error);
    }
    unlink(STOP_FILE);
    char steps[16];
    snprintf(steps, sizeof(steps), "%d", max_steps);
    pid_t pid = fork();
    if (pid == 0) {
        if (dup2(log_fd, STDOUT_FILENO) < 0 ||
            dup2(log_fd, STDERR_FILENO) < 0)
            _exit(127);
        close(log_fd);
        execl(AGENT_PATH, "aitvbox-agentd", "run", task, steps, (char *)NULL);
        _exit(127);
    }
    close(log_fd);
    if (pid < 0)
        return response(false, "cannot start agent");
    agent_pid = pid;
    state = CONTROL_RUNNING;
    return response(true, "task started");
}

static json_object *stop_task(json_object *request)
{
    static const char *const fields[] = {"command"};
    if (!object_has_only(request, fields, 1))
        return response(false, "invalid stop request");
    request_stop();
    return response(true, "stop requested");
}

static json_object *dispatch(json_object *request)
{
    json_object *command = NULL;
    if (!request || !json_object_is_type(request, json_type_object) ||
        !json_object_object_get_ex(request, "command", &command) ||
        !json_object_is_type(command, json_type_string))
        return response(false, "request must contain command");
    const char *name = json_object_get_string(command);
    if (!strcmp(name, "status")) {
        static const char *const fields[] = {"command"};
        if (!object_has_only(request, fields, 1))
            return response(false, "invalid status request");
        return status_response();
    }
    if (!strcmp(name, "setKey"))
        return set_key(request);
    if (!strcmp(name, "configure"))
        return configure_provider(request);
    if (!strcmp(name, "clearKey"))
        return clear_key(request);
    if (!strcmp(name, "start"))
        return start_task(request);
    if (!strcmp(name, "stop"))
        return stop_task(request);
    return response(false, "unknown command");
}

static json_object *parse_request(const char *buffer, size_t length)
{
    json_tokener *tokener = json_tokener_new();
    if (!tokener)
        return NULL;
    json_object *object = json_tokener_parse_ex(tokener, buffer, (int)length);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    size_t consumed = (size_t)tokener->char_offset;
    json_tokener_free(tokener);
    while (consumed < length &&
           (buffer[consumed] == ' ' || buffer[consumed] == '\t' ||
            buffer[consumed] == '\r' || buffer[consumed] == '\n'))
        consumed++;
    if (error != json_tokener_success || consumed != length) {
        if (object)
            json_object_put(object);
        return NULL;
    }
    return object;
}

static void handle_client(int client)
{
#ifdef SO_PEERCRED
    struct ucred credentials;
    socklen_t credential_length = sizeof(credentials);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED,
                   &credentials, &credential_length) ||
        credentials.uid != geteuid()) {
        json_object *denied = response(false, "unauthorized local client");
        const char *text = json_object_to_json_string_ext(
            denied, JSON_C_TO_STRING_PLAIN);
        (void)write_all(client, text, strlen(text));
        (void)write_all(client, "\n", 1);
        json_object_put(denied);
        return;
    }
#endif

    char buffer[REQUEST_MAX + 1];
    size_t used = 0;
    while (used < REQUEST_MAX) {
        ssize_t count = read(client, buffer + used, REQUEST_MAX - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
        if (memchr(buffer, '\n', used))
            break;
    }
    char *newline = memchr(buffer, '\n', used);
    size_t request_length = newline ? (size_t)(newline - buffer) : used;
    json_object *request = request_length ?
        parse_request(buffer, request_length) : NULL;
    json_object *reply = request ? dispatch(request) :
        response(false, "invalid JSON request");
    memset(buffer, 0, sizeof(buffer));
    const char *text = json_object_to_json_string_ext(
        reply, JSON_C_TO_STRING_PLAIN);
    (void)write_all(client, text, strlen(text));
    (void)write_all(client, "\n", 1);
    json_object_put(reply);
    if (request)
        json_object_put(request);
}

int main(void)
{
    struct sigaction action = {0};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) {
        perror("control socket");
        return 1;
    }
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s",
                 CONTROL_SOCKET) >= (int)sizeof(address.sun_path)) {
        fprintf(stderr, "control socket path is too long\n");
        close(server);
        return 1;
    }
    unlink(CONTROL_SOCKET);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) ||
        chmod(CONTROL_SOCKET, 0660) || listen(server, 8)) {
        perror("control listen");
        close(server);
        unlink(CONTROL_SOCKET);
        return 1;
    }

    while (!shutdown_requested) {
        reap_agent();
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("control accept");
            break;
        }
        handle_client(client);
        close(client);
    }
    if (agent_pid > 0)
        request_stop();
    close(server);
    unlink(CONTROL_SOCKET);
    return 0;
}
