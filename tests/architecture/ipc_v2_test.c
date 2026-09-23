#include "ipc_v2.h"

#include <arpa/inet.h>
#include <assert.h>
#include <json-c/json.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static json_object *request(const char *request_id)
{
    json_object *message = json_object_new_object();
    json_object_object_add(message, "schemaVersion", json_object_new_int(2));
    json_object_object_add(message, "messageType", json_object_new_string("request"));
    json_object_object_add(message, "topic", json_object_new_string("system.ping"));
    json_object_object_add(message, "requestId", json_object_new_string(request_id));
    json_object_object_add(message, "payload", json_object_new_object());
    return message;
}

int main(void)
{
    int sockets[2];
    json_object *sent = request("c-test");
    json_object *received = NULL;
    json_object *request_id = NULL;
    char error[64];
    uint32_t oversized = htonl(AITVBOX_IPC_V2_MAX_FRAME + 1U);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(aitvbox_ipc_v2_validate(sent, error, sizeof(error)) == 0);
    assert(aitvbox_ipc_v2_send(sockets[0], sent) == 0);
    assert(aitvbox_ipc_v2_receive(sockets[1], &received) == 0);
    assert(json_object_object_get_ex(received, "requestId", &request_id));
    assert(strcmp(json_object_get_string(request_id), "c-test") == 0);
    json_object_put(received);
    json_object_put(sent);

    assert(send(sockets[0], &oversized, sizeof(oversized), 0) == sizeof(oversized));
    assert(aitvbox_ipc_v2_receive(sockets[1], &received) == AITVBOX_IPC_V2_TOO_LARGE);
    close(sockets[0]);
    close(sockets[1]);
    return 0;
}
