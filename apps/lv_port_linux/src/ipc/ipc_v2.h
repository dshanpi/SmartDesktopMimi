#ifndef AITVBOX_IPC_V2_H
#define AITVBOX_IPC_V2_H

#include <stddef.h>
#include <json-c/json.h>

#define AITVBOX_IPC_V2_SOCKET "/run/aitvbox/backend-v2.sock"
#define AITVBOX_IPC_V2_MAX_FRAME (64U * 1024U)

typedef enum {
    AITVBOX_IPC_V2_OK = 0,
    AITVBOX_IPC_V2_IO = -1,
    AITVBOX_IPC_V2_INVALID = -2,
    AITVBOX_IPC_V2_TOO_LARGE = -3,
    AITVBOX_IPC_V2_UNSUPPORTED_VERSION = -4
} aitvbox_ipc_v2_status_t;

int aitvbox_ipc_v2_validate(json_object *message,
                            char *error_code, size_t error_code_size);
int aitvbox_ipc_v2_send(int fd, json_object *message);
int aitvbox_ipc_v2_receive(int fd, json_object **message);

#endif
