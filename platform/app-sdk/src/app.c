#define _GNU_SOURCE

#include "aitvbox/app.h"

#include <json-c/json.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef AITVBOX_APP_SOCKET
#define AITVBOX_APP_SOCKET "/var/run/aitvbox/apps.sock"
#endif
#define APP_STORAGE_VALUE_MAX 4096

static int write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static json_object *parse_object(const char *text)
{
    size_t length = text ? strlen(text) : 0;
    if (!length || length > 4096)
        return NULL;
    json_tokener *tokener = json_tokener_new();
    if (!tokener)
        return NULL;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    json_object *result = json_tokener_parse_ex(tokener, text, (int)length);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    size_t consumed = (size_t)tokener->char_offset;
    while (consumed < length &&
           (text[consumed] == ' ' || text[consumed] == '\t' ||
            text[consumed] == '\r' || text[consumed] == '\n'))
        consumed++;
    json_tokener_free(tokener);
    if (error != json_tokener_success || consumed != length ||
        !json_object_is_type(result, json_type_object)) {
        if (result)
            json_object_put(result);
        return NULL;
    }
    return result;
}

const char *aitvbox_app_action(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "--action") || !argv[2][0] ||
        strlen(argv[2]) > 64)
        return NULL;
    return argv[2];
}

int aitvbox_app_reply(bool ok, const char *message)
{
    if (!message || strlen(message) > 512)
        return -1;
    json_object *response = json_object_new_object();
    if (!response)
        return -1;
    json_object_object_add(response, "ok", json_object_new_boolean(ok));
    json_object_object_add(response, "message", json_object_new_string(message));
    const char *text = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN);
    int result = printf("%s\n", text) < 0 || fflush(stdout) ? -1 : 0;
    json_object_put(response);
    return result;
}

int aitvbox_capability_call(const char *capability, const char *arguments_json,
                            char *result_json, size_t result_size)
{
    if (!capability || !capability[0] || strlen(capability) > 64 ||
        !result_json || result_size < 2)
        return -1;
    json_object *arguments = parse_object(arguments_json);
    if (!arguments)
        return -1;
    json_object *request = json_object_new_object();
    if (!request) {
        json_object_put(arguments);
        return -1;
    }
    json_object_object_add(request, "command",
                           json_object_new_string("capability"));
    json_object_object_add(request, "capability",
                           json_object_new_string(capability));
    json_object_object_add(request, "arguments", arguments);
    const char *request_text = json_object_to_json_string_ext(
        request, JSON_C_TO_STRING_PLAIN);

    int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client < 0) {
        json_object_put(request);
        return -1;
    }
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    const char *socket_path = getenv("AITVBOX_APP_SOCKET");
    if (!socket_path || !socket_path[0])
        socket_path = AITVBOX_APP_SOCKET;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s",
                 socket_path) >= (int)sizeof(address.sun_path) ||
        connect(client, (struct sockaddr *)&address, sizeof(address)) ||
        write_all(client, request_text, strlen(request_text)) ||
        write_all(client, "\n", 1)) {
        close(client);
        json_object_put(request);
        return -1;
    }
    json_object_put(request);

    size_t used = 0;
    bool complete = false;
    while (used + 1 < result_size) {
        ssize_t count = read(client, result_json + used, result_size - used - 1);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
        if (memchr(result_json, '\n', used)) {
            complete = true;
            break;
        }
    }
    close(client);
    result_json[used] = '\0';
    char *newline = memchr(result_json, '\n', used);
    if (!complete || !newline)
        return -1;
    *newline = '\0';
    json_object *response = parse_object(result_json);
    json_object *ok = NULL, *result = NULL;
    if (!response ||
        !json_object_object_get_ex(response, "ok", &ok) ||
        !json_object_is_type(ok, json_type_boolean) ||
        !json_object_get_boolean(ok) ||
        !json_object_object_get_ex(response, "result", &result)) {
        if (response)
            json_object_put(response);
        return -1;
    }
    const char *result_text = json_object_to_json_string_ext(
        result, JSON_C_TO_STRING_PLAIN);
    size_t result_length = strlen(result_text);
    if (result_length + 1 > result_size) {
        json_object_put(response);
        return -1;
    }
    memmove(result_json, result_text, result_length + 1);
    json_object_put(response);
    return 0;
}

int aitvbox_keyboard(unsigned keycode, unsigned modifier)
{
    if (keycode > 101 || modifier > 255)
        return -1;
    char arguments[80], response[1024];
    snprintf(arguments, sizeof(arguments),
             "{\"keycode\":%u,\"modifier\":%u}", keycode, modifier);
    return aitvbox_capability_call(
        "input.keyboard", arguments, response, sizeof(response));
}

int aitvbox_pointer(int dx, int dy, unsigned buttons, int wheel)
{
    if (dx < -127 || dx > 127 || dy < -127 || dy > 127 ||
        buttons > 7 || wheel < -127 || wheel > 127)
        return -1;
    char arguments[128], response[1024];
    snprintf(arguments, sizeof(arguments),
             "{\"dx\":%d,\"dy\":%d,\"buttons\":%u,\"wheel\":%d}",
             dx, dy, buttons, wheel);
    return aitvbox_capability_call(
        "input.pointer", arguments, response, sizeof(response));
}

static bool valid_storage_key(const char *key)
{
    size_t length = key ? strlen(key) : 0;
    return length > 0 && length <= 64 &&
           strspn(key, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "abcdefghijklmnopqrstuvwxyz"
                      "0123456789._-") == length;
}

int aitvbox_storage_read(const char *key, char *value, size_t value_size,
                         bool *found)
{
    if (!valid_storage_key(key) || !value || value_size < 1 || !found)
        return -1;
    json_object *arguments = json_object_new_object();
    if (!arguments)
        return -1;
    json_object_object_add(arguments, "op", json_object_new_string("read"));
    json_object_object_add(arguments, "key", json_object_new_string(key));
    const char *arguments_text = json_object_to_json_string_ext(
        arguments, JSON_C_TO_STRING_PLAIN);
    char result[APP_STORAGE_VALUE_MAX + 128];
    int status = aitvbox_capability_call(
        "storage.app", arguments_text, result, sizeof(result));
    json_object_put(arguments);
    if (status)
        return -1;

    json_object *response = parse_object(result);
    json_object *found_object = NULL, *value_object = NULL;
    if (!response ||
        !json_object_object_get_ex(response, "found", &found_object) ||
        !json_object_is_type(found_object, json_type_boolean)) {
        if (response)
            json_object_put(response);
        return -1;
    }
    *found = json_object_get_boolean(found_object);
    value[0] = '\0';
    if (*found) {
        if (!json_object_object_get_ex(response, "value", &value_object) ||
            !json_object_is_type(value_object, json_type_string)) {
            json_object_put(response);
            return -1;
        }
        size_t length = (size_t)json_object_get_string_len(value_object);
        if (length + 1 > value_size) {
            json_object_put(response);
            return -1;
        }
        memcpy(value, json_object_get_string(value_object), length);
        value[length] = '\0';
    }
    json_object_put(response);
    return 0;
}

int aitvbox_storage_write(const char *key, const char *value)
{
    if (!valid_storage_key(key) || !value ||
        strlen(value) > APP_STORAGE_VALUE_MAX)
        return -1;
    json_object *arguments = json_object_new_object();
    if (!arguments)
        return -1;
    json_object_object_add(arguments, "op", json_object_new_string("write"));
    json_object_object_add(arguments, "key", json_object_new_string(key));
    json_object_object_add(arguments, "value", json_object_new_string(value));
    const char *arguments_text = json_object_to_json_string_ext(
        arguments, JSON_C_TO_STRING_PLAIN);
    char result[256];
    int status = aitvbox_capability_call(
        "storage.app", arguments_text, result, sizeof(result));
    json_object_put(arguments);
    return status;
}
