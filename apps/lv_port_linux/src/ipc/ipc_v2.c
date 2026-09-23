#include "ipc_v2.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static int transfer_all(int fd, void *buffer, size_t length, bool sending)
{
    uint8_t *cursor = buffer;
    while (length > 0) {
        ssize_t amount = sending
            ? send(fd, cursor, length, MSG_NOSIGNAL)
            : recv(fd, cursor, length, 0);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) return AITVBOX_IPC_V2_IO;
        cursor += amount;
        length -= (size_t)amount;
    }
    return AITVBOX_IPC_V2_OK;
}

static int fail(char *buffer, size_t size, const char *code, int status)
{
    if (buffer && size > 0) {
        snprintf(buffer, size, "%s", code);
    }
    return status;
}

int aitvbox_ipc_v2_validate(json_object *message,
                            char *error_code, size_t error_code_size)
{
    json_object *value = NULL;
    const char *message_type;
    const char *topic;

    if (!message || !json_object_is_type(message, json_type_object))
        return fail(error_code, error_code_size, "INVALID_ENVELOPE", AITVBOX_IPC_V2_INVALID);
    if (!json_object_object_get_ex(message, "schemaVersion", &value) ||
        !json_object_is_type(value, json_type_int))
        return fail(error_code, error_code_size, "UNSUPPORTED_VERSION", AITVBOX_IPC_V2_UNSUPPORTED_VERSION);
    if (json_object_get_int(value) != 2)
        return fail(error_code, error_code_size, "UNSUPPORTED_VERSION", AITVBOX_IPC_V2_UNSUPPORTED_VERSION);
    if (!json_object_object_get_ex(message, "messageType", &value) ||
        !json_object_is_type(value, json_type_string))
        return fail(error_code, error_code_size, "INVALID_MESSAGE_TYPE", AITVBOX_IPC_V2_INVALID);
    message_type = json_object_get_string(value);
    if (strcmp(message_type, "request") != 0 &&
        strcmp(message_type, "response") != 0 &&
        strcmp(message_type, "event") != 0)
        return fail(error_code, error_code_size, "INVALID_MESSAGE_TYPE", AITVBOX_IPC_V2_INVALID);
    if (!json_object_object_get_ex(message, "topic", &value) ||
        !json_object_is_type(value, json_type_string))
        return fail(error_code, error_code_size, "INVALID_TOPIC", AITVBOX_IPC_V2_INVALID);
    topic = json_object_get_string(value);
    if (!topic[0] || strlen(topic) > 128)
        return fail(error_code, error_code_size, "INVALID_TOPIC", AITVBOX_IPC_V2_INVALID);
    if (!json_object_object_get_ex(message, "payload", &value) ||
        !json_object_is_type(value, json_type_object))
        return fail(error_code, error_code_size, "INVALID_PAYLOAD", AITVBOX_IPC_V2_INVALID);
    if (strcmp(message_type, "request") == 0 || strcmp(message_type, "response") == 0) {
        if (!json_object_object_get_ex(message, "requestId", &value) ||
            !json_object_is_type(value, json_type_string) ||
            !json_object_get_string(value)[0] ||
            strlen(json_object_get_string(value)) > 128)
            return fail(error_code, error_code_size, "INVALID_REQUEST_ID", AITVBOX_IPC_V2_INVALID);
    } else {
        if (!json_object_object_get_ex(message, "sequence", &value) ||
            !json_object_is_type(value, json_type_int) || json_object_get_int64(value) < 0)
            return fail(error_code, error_code_size, "INVALID_SEQUENCE", AITVBOX_IPC_V2_INVALID);
    }
    if (error_code && error_code_size > 0) error_code[0] = '\0';
    return AITVBOX_IPC_V2_OK;
}

int aitvbox_ipc_v2_send(int fd, json_object *message)
{
    const char *body;
    size_t size;
    uint32_t network_size;
    char error_code[64];

    if (fd < 0 || aitvbox_ipc_v2_validate(message, error_code, sizeof(error_code)) != 0)
        return AITVBOX_IPC_V2_INVALID;
    body = json_object_to_json_string_ext(message, JSON_C_TO_STRING_PLAIN);
    size = strlen(body);
    if (size == 0 || size > AITVBOX_IPC_V2_MAX_FRAME)
        return AITVBOX_IPC_V2_TOO_LARGE;
    network_size = htonl((uint32_t)size);
    if (transfer_all(fd, &network_size, sizeof(network_size), true) != 0)
        return AITVBOX_IPC_V2_IO;
    return transfer_all(fd, (void *)body, size, true);
}

int aitvbox_ipc_v2_receive(int fd, json_object **message)
{
    uint32_t network_size;
    uint32_t size;
    char *body;
    json_tokener *tokener;
    json_object *decoded;
    enum json_tokener_error parse_error;
    char error_code[64];

    if (fd < 0 || !message) return AITVBOX_IPC_V2_INVALID;
    *message = NULL;
    if (transfer_all(fd, &network_size, sizeof(network_size), false) != 0)
        return AITVBOX_IPC_V2_IO;
    size = ntohl(network_size);
    if (size == 0) return AITVBOX_IPC_V2_INVALID;
    if (size > AITVBOX_IPC_V2_MAX_FRAME) return AITVBOX_IPC_V2_TOO_LARGE;
    body = malloc((size_t)size + 1);
    if (!body) return AITVBOX_IPC_V2_IO;
    if (transfer_all(fd, body, size, false) != 0) {
        free(body);
        return AITVBOX_IPC_V2_IO;
    }
    body[size] = '\0';
    tokener = json_tokener_new();
    if (!tokener) {
        free(body);
        return AITVBOX_IPC_V2_IO;
    }
    decoded = json_tokener_parse_ex(tokener, body, (int)size);
    parse_error = json_tokener_get_error(tokener);
    free(body);
    json_tokener_free(tokener);
    if (parse_error != json_tokener_success || !decoded) {
        if (decoded) json_object_put(decoded);
        return AITVBOX_IPC_V2_INVALID;
    }
    if (aitvbox_ipc_v2_validate(decoded, error_code, sizeof(error_code)) != 0) {
        json_object_put(decoded);
        return AITVBOX_IPC_V2_INVALID;
    }
    *message = decoded;
    return AITVBOX_IPC_V2_OK;
}
