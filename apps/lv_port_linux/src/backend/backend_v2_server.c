#include "backend_v2_server.h"

#include "../ipc/ipc_v2.h"
#include "../system/log/app_log.h"

#include <errno.h>
#include <json-c/json.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static atomic_bool g_running;
static pthread_t g_thread;
static int g_listen_fd = -1;
static const aitvbox_platform_descriptor_t *g_platform;

static json_object *make_response(json_object *request, const char *error_code,
                                  const char *error_message)
{
    json_object *response = json_object_new_object();
    json_object *request_id = NULL;
    json_object *topic = NULL;
    json_object_object_get_ex(request, "requestId", &request_id);
    json_object_object_get_ex(request, "topic", &topic);
    json_object_object_add(response, "schemaVersion", json_object_new_int(2));
    json_object_object_add(response, "messageType", json_object_new_string("response"));
    json_object_object_add(response, "topic", json_object_new_string(json_object_get_string(topic)));
    json_object_object_add(response, "requestId", json_object_new_string(json_object_get_string(request_id)));
    json_object_object_add(response, "payload", json_object_new_object());
    if (error_code) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "code", json_object_new_string(error_code));
        json_object_object_add(error, "message", json_object_new_string(error_message));
        json_object_object_add(error, "retryable", json_object_new_boolean(false));
        json_object_object_add(response, "error", error);
    }
    return response;
}

static json_object *dispatch(json_object *request)
{
    json_object *topic_object = NULL;
    const char *topic;
    json_object *response;
    json_object *payload;

    json_object_object_get_ex(request, "topic", &topic_object);
    topic = json_object_get_string(topic_object);
    if (strcmp(topic, "system.ping") == 0) {
        response = make_response(request, NULL, NULL);
        json_object_object_get_ex(response, "payload", &payload);
        json_object_object_add(payload, "alive", json_object_new_boolean(true));
        return response;
    }
    if (strcmp(topic, "platform.describe") == 0) {
        response = make_response(request, NULL, NULL);
        json_object_object_get_ex(response, "payload", &payload);
        json_object_object_add(payload, "platformId", json_object_new_string(g_platform->platform_id));
        json_object_object_add(payload, "soc", json_object_new_string(g_platform->soc));
        json_object_object_add(payload, "providerVersion", json_object_new_string(g_platform->provider_version));
        json_object_object_add(payload, "abiVersion", json_object_new_int64(g_platform->abi_version));
        json_object_object_add(payload, "capabilities", json_object_new_int64((int64_t)g_platform->capabilities));
        return response;
    }
    return make_response(request, "UNKNOWN_TOPIC", "the requested topic is not registered");
}

static bool peer_allowed(int fd)
{
    struct ucred credentials;
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0)
        return false;
    return credentials.uid == 0 || credentials.uid == geteuid();
}

static void handle_client(int client)
{
    while (atomic_load(&g_running)) {
        struct pollfd descriptor = {.fd = client, .events = POLLIN};
        int ready = poll(&descriptor, 1, 250);
        json_object *request = NULL;
        json_object *response;
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) continue;
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (aitvbox_ipc_v2_receive(client, &request) != AITVBOX_IPC_V2_OK) break;
        response = dispatch(request);
        if (aitvbox_ipc_v2_send(client, response) != AITVBOX_IPC_V2_OK) {
            json_object_put(response);
            json_object_put(request);
            break;
        }
        json_object_put(response);
        json_object_put(request);
    }
}

static void *server_thread(void *unused)
{
    (void)unused;
    while (atomic_load(&g_running)) {
        struct pollfd descriptor = {.fd = g_listen_fd, .events = POLLIN};
        int ready = poll(&descriptor, 1, 250);
        int client;
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) continue;
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        client = accept(g_listen_fd, NULL, NULL);
        if (client < 0) continue;
        if (peer_allowed(client)) handle_client(client);
        else APP_LOGW("ipc-v2", "rejected unauthorized local peer");
        close(client);
    }
    return NULL;
}

int backend_v2_server_start(const aitvbox_platform_descriptor_t *platform)
{
    struct sockaddr_un address;
    if (!platform || atomic_load(&g_running)) return platform ? 0 : -1;
    if (mkdir("/run/aitvbox", 0755) != 0 && errno != EEXIST) return -1;
    g_listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_listen_fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", AITVBOX_IPC_V2_SOCKET);
    unlink(AITVBOX_IPC_V2_SOCKET);
    if (bind(g_listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(AITVBOX_IPC_V2_SOCKET, 0660) != 0 || listen(g_listen_fd, 8) != 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        unlink(AITVBOX_IPC_V2_SOCKET);
        return -1;
    }
    g_platform = platform;
    atomic_store(&g_running, true);
    if (pthread_create(&g_thread, NULL, server_thread, NULL) != 0) {
        atomic_store(&g_running, false);
        close(g_listen_fd);
        g_listen_fd = -1;
        unlink(AITVBOX_IPC_V2_SOCKET);
        return -1;
    }
    APP_LOGI("ipc-v2", "control socket ready at %s", AITVBOX_IPC_V2_SOCKET);
    return 0;
}

void backend_v2_server_stop(void)
{
    if (!atomic_exchange(&g_running, false)) return;
    if (g_listen_fd >= 0) shutdown(g_listen_fd, SHUT_RDWR);
    pthread_join(g_thread, NULL);
    if (g_listen_fd >= 0) close(g_listen_fd);
    g_listen_fd = -1;
    g_platform = NULL;
    unlink(AITVBOX_IPC_V2_SOCKET);
}
