/**
 * @file middleware.c
 * @brief 多进程架构消息中间件实现
 */

#include "middleware.h"
#include "../ipc/ipc_socket.h"
#include "../system/log/app_log.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 4
#define UI_MAX_MESSAGES_PER_TICK 16

typedef struct {
    int fd;
    bool active;
    time_t last_heartbeat;
} client_t;

static struct {
    pthread_mutex_t lock;
    bool is_backend;
    bool initialized;
    int ipc_socket;
    int listen_socket;
    client_t clients[MAX_CLIENTS];
    mw_subscribe_cb_t ui_handlers[TOPIC_MAX][MW_MAX_SUBSCRIBERS];
    mw_subscribe_cb_t backend_handlers[TOPIC_MAX][MW_MAX_SUBSCRIBERS];
    uint8_t ui_subscriber_count[TOPIC_MAX];
    uint8_t backend_subscriber_count[TOPIC_MAX];
    uint32_t msg_sent;
    uint32_t msg_received;
} mw_state = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .ipc_socket = -1,
    .listen_socket = -1,
};

static void dispatch_ui_message(const ipc_msg_t *ipc_msg)
{
    mw_msg_t msg;
    uint8_t count;
    mw_subscribe_cb_t callbacks[MW_MAX_SUBSCRIBERS];

    if (ipc_msg->header.topic >= TOPIC_MAX) return;
    msg.topic = (mw_topic_t)ipc_msg->header.topic;
    msg.direction = MW_DIR_BACKEND_TO_UI;
    msg.id = 0;
    msg.data_len = ipc_msg->header.payload_len;
    if (ipc_msg->header.payload_len > 0) memcpy(msg.data, ipc_msg->payload, ipc_msg->header.payload_len);

    pthread_mutex_lock(&mw_state.lock);
    count = mw_state.ui_subscriber_count[msg.topic];
    if (count > MW_MAX_SUBSCRIBERS) count = MW_MAX_SUBSCRIBERS;
    for (uint8_t i = 0; i < count; i++) {
        callbacks[i] = mw_state.ui_handlers[msg.topic][i];
    }
    mw_state.msg_received++;
    pthread_mutex_unlock(&mw_state.lock);

    for (uint8_t i = 0; i < count; i++) {
        mw_subscribe_cb_t cb = callbacks[i];
        if (cb) cb(&msg);
    }
}

static void dispatch_backend_message(const ipc_msg_t *ipc_msg)
{
    mw_msg_t msg;
    uint8_t count;
    mw_subscribe_cb_t callbacks[MW_MAX_SUBSCRIBERS];

    if (ipc_msg->header.topic >= TOPIC_MAX) {
        APP_LOGW("middleware", "invalid backend topic %u", ipc_msg->header.topic);
        return;
    }
    msg.topic = (mw_topic_t)ipc_msg->header.topic;
    msg.direction = MW_DIR_UI_TO_BACKEND;
    msg.id = 0;
    msg.data_len = ipc_msg->header.payload_len;
    if (ipc_msg->header.payload_len > 0) memcpy(msg.data, ipc_msg->payload, ipc_msg->header.payload_len);

    pthread_mutex_lock(&mw_state.lock);
    count = mw_state.backend_subscriber_count[msg.topic];
    if (count > MW_MAX_SUBSCRIBERS) count = MW_MAX_SUBSCRIBERS;
    for (uint8_t i = 0; i < count; i++) {
        callbacks[i] = mw_state.backend_handlers[msg.topic][i];
    }
    mw_state.msg_received++;
    pthread_mutex_unlock(&mw_state.lock);

    if (count == 0) {
        APP_LOGW("middleware", "no backend handler for topic %d", msg.topic);
    }
    for (uint8_t i = 0; i < count; i++) {
        mw_subscribe_cb_t cb = callbacks[i];
        if (cb) cb(&msg);
    }
}

static int handle_new_connection(void)
{
    int client_fd = -1;
    if (ipc_server_accept(mw_state.listen_socket, &client_fd) != 0) return -1;

    pthread_mutex_lock(&mw_state.lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!mw_state.clients[i].active) {
            mw_state.clients[i].fd = client_fd;
            mw_state.clients[i].active = true;
            mw_state.clients[i].last_heartbeat = time(NULL);
            pthread_mutex_unlock(&mw_state.lock);
            APP_LOGI("middleware", "client slot %d connected", i);
            return 0;
        }
    }
    pthread_mutex_unlock(&mw_state.lock);

    APP_LOGW("middleware", "max clients reached, reject");
    close(client_fd);
    return -1;
}

static void handle_client_disconnect(int client_idx)
{
    int fd;
    pthread_mutex_lock(&mw_state.lock);
    if (!mw_state.clients[client_idx].active) {
        pthread_mutex_unlock(&mw_state.lock);
        return;
    }
    fd = mw_state.clients[client_idx].fd;
    mw_state.clients[client_idx].active = false;
    mw_state.clients[client_idx].fd = -1;
    pthread_mutex_unlock(&mw_state.lock);
    APP_LOGI("middleware", "client slot %d disconnected", client_idx);
    close(fd);
}

static int handle_client_message(int client_idx)
{
    ipc_msg_t msg;
    int client_fd;
    int recv_ret;

    pthread_mutex_lock(&mw_state.lock);
    if (!mw_state.clients[client_idx].active) {
        pthread_mutex_unlock(&mw_state.lock);
        return -1;
    }
    client_fd = mw_state.clients[client_idx].fd;
    pthread_mutex_unlock(&mw_state.lock);

    recv_ret = ipc_server_recv(client_fd, &msg, sizeof(msg), false);

    if (recv_ret < 0) return -1;
    if (recv_ret == 0) return 0;

    pthread_mutex_lock(&mw_state.lock);
    if (mw_state.clients[client_idx].active) {
        mw_state.clients[client_idx].last_heartbeat = time(NULL);
    }
    pthread_mutex_unlock(&mw_state.lock);
    if (!(msg.header.flags & IPC_FLAG_HEARTBEAT)) dispatch_backend_message(&msg);
    return 0;
}

static int mw_init_ui(void)
{
    pthread_mutex_lock(&mw_state.lock);
    memset(mw_state.ui_handlers, 0, sizeof(mw_state.ui_handlers));
    memset(mw_state.ui_subscriber_count, 0, sizeof(mw_state.ui_subscriber_count));
    pthread_mutex_unlock(&mw_state.lock);

    if (ipc_client_connect(&mw_state.ipc_socket) != 0) {
        APP_LOGE("middleware", "failed to connect backend");
        return -1;
    }

    pthread_mutex_lock(&mw_state.lock);
    mw_state.is_backend = false;
    mw_state.initialized = true;
    pthread_mutex_unlock(&mw_state.lock);
    APP_LOGI("middleware", "ui mode initialized");
    return 0;
}

static int mw_init_backend(void)
{
    pthread_mutex_lock(&mw_state.lock);
    memset(mw_state.backend_handlers, 0, sizeof(mw_state.backend_handlers));
    memset(mw_state.backend_subscriber_count, 0, sizeof(mw_state.backend_subscriber_count));
    memset(mw_state.clients, 0, sizeof(mw_state.clients));
    pthread_mutex_unlock(&mw_state.lock);

    if (ipc_server_start(&mw_state.listen_socket) != 0) {
        APP_LOGE("middleware", "failed to start ipc server");
        return -1;
    }

    pthread_mutex_lock(&mw_state.lock);
    mw_state.is_backend = true;
    mw_state.initialized = true;
    pthread_mutex_unlock(&mw_state.lock);
    APP_LOGI("middleware", "backend mode initialized");
    return 0;
}

int mw_init(bool is_backend_process)
{
    signal(SIGPIPE, SIG_IGN);
    return is_backend_process ? mw_init_backend() : mw_init_ui();
}

void mw_deinit(void)
{
    bool initialized;
    bool is_backend;
    int ipc_socket;
    int listen_socket;
    int client_fds[MAX_CLIENTS];
    int client_count = 0;

    pthread_mutex_lock(&mw_state.lock);
    initialized = mw_state.initialized;
    is_backend = mw_state.is_backend;
    ipc_socket = mw_state.ipc_socket;
    listen_socket = mw_state.listen_socket;
    mw_state.initialized = false;
    mw_state.ipc_socket = -1;
    mw_state.listen_socket = -1;
    if (is_backend) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (mw_state.clients[i].active) {
                client_fds[client_count++] = mw_state.clients[i].fd;
                mw_state.clients[i].active = false;
                mw_state.clients[i].fd = -1;
            }
        }
    }
    pthread_mutex_unlock(&mw_state.lock);

    if (!initialized) return;

    if (is_backend) {
        ipc_server_stop(listen_socket);
        for (int i = 0; i < client_count; i++) {
            close(client_fds[i]);
        }
    } else {
        ipc_client_disconnect(ipc_socket);
    }
    APP_LOGI("middleware", "deinitialized");
}

bool mw_is_initialized(void)
{
    bool initialized;
    pthread_mutex_lock(&mw_state.lock);
    initialized = mw_state.initialized;
    pthread_mutex_unlock(&mw_state.lock);
    return initialized;
}

int mw_publish(mw_topic_t topic, const void *data, uint32_t len, mw_direction_t direction)
{
    (void)direction;
    bool initialized;
    bool is_backend;
    int ui_socket;
    int backend_fds[MAX_CLIENTS];
    int backend_slots[MAX_CLIENTS];
    int failed_slots[MAX_CLIENTS];
    int backend_count = 0;
    int failed_count = 0;

    pthread_mutex_lock(&mw_state.lock);
    initialized = mw_state.initialized;
    is_backend = mw_state.is_backend;
    ui_socket = mw_state.ipc_socket;
    if (is_backend) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (mw_state.clients[i].active) {
                backend_fds[backend_count++] = mw_state.clients[i].fd;
                backend_slots[backend_count - 1] = i;
            }
        }
    }
    pthread_mutex_unlock(&mw_state.lock);

    if (!initialized) return -1;
    if (topic >= TOPIC_MAX) return -1;
    if (len > MW_MSG_DATA_MAX_SIZE) return -1;

    if (is_backend) {
        int sent_count = 0;
        for (int i = 0; i < backend_count; i++) {
            if (ipc_server_send(backend_fds[i], topic, data, len, IPC_FLAG_NONE) == 0) {
                sent_count++;
            } else {
                failed_slots[failed_count++] = backend_slots[i];
            }
        }
        for (int i = 0; i < failed_count; i++) {
            APP_LOGW("middleware", "client slot %d send failed, disconnecting", failed_slots[i]);
            handle_client_disconnect(failed_slots[i]);
        }
        if (sent_count > 0) {
            pthread_mutex_lock(&mw_state.lock);
            mw_state.msg_sent++;
            pthread_mutex_unlock(&mw_state.lock);
            return 0;
        }
        return -1;
    }

    if (ui_socket < 0) {
        int new_socket = -1;
        if (ipc_client_connect(&new_socket) != 0) return -1;
        pthread_mutex_lock(&mw_state.lock);
        if (mw_state.ipc_socket < 0) {
            mw_state.ipc_socket = new_socket;
            ui_socket = new_socket;
            new_socket = -1;
        } else {
            ui_socket = mw_state.ipc_socket;
        }
        pthread_mutex_unlock(&mw_state.lock);
        if (new_socket >= 0) {
            close(new_socket);
        }
    }

    int ret = ipc_client_send(ui_socket, topic, data, len, IPC_FLAG_NONE);
    if (ret < 0) {
        APP_LOGW("middleware", "send failed, reconnect once");
        pthread_mutex_lock(&mw_state.lock);
        if (mw_state.ipc_socket == ui_socket) {
            close(mw_state.ipc_socket);
            mw_state.ipc_socket = -1;
        }
        pthread_mutex_unlock(&mw_state.lock);

        if (ipc_client_connect(&ui_socket) == 0) {
            pthread_mutex_lock(&mw_state.lock);
            if (mw_state.ipc_socket < 0) {
                mw_state.ipc_socket = ui_socket;
            } else {
                close(ui_socket);
                ui_socket = mw_state.ipc_socket;
            }
            pthread_mutex_unlock(&mw_state.lock);
            ret = ipc_client_send(ui_socket, topic, data, len, IPC_FLAG_NONE);
        }
    }
    if (ret == 0) {
        pthread_mutex_lock(&mw_state.lock);
        mw_state.msg_sent++;
        pthread_mutex_unlock(&mw_state.lock);
    }
    return ret;
}

static int subscribe_into_table(mw_subscribe_cb_t table[TOPIC_MAX][MW_MAX_SUBSCRIBERS], uint8_t counts[TOPIC_MAX], mw_topic_t topic, mw_subscribe_cb_t cb)
{
    uint8_t count;
    if (topic >= TOPIC_MAX || !cb) return -1;
    count = counts[topic];

    for (uint8_t i = 0; i < count; i++) {
        if (table[topic][i] == cb) return 0;
    }
    if (count >= MW_MAX_SUBSCRIBERS) return -1;
    table[topic][count] = cb;
    counts[topic]++;
    return 0;
}

static int unsubscribe_from_table(mw_subscribe_cb_t table[TOPIC_MAX][MW_MAX_SUBSCRIBERS], uint8_t counts[TOPIC_MAX], mw_topic_t topic, mw_subscribe_cb_t cb)
{
    uint8_t count;
    if (topic >= TOPIC_MAX || !cb) return -1;
    count = counts[topic];
    for (uint8_t i = 0; i < count; i++) {
        if (table[topic][i] == cb) {
            for (uint8_t j = i; j + 1 < count; j++) {
                table[topic][j] = table[topic][j + 1];
            }
            table[topic][count - 1] = NULL;
            counts[topic]--;
            return 0;
        }
    }
    return -1;
}

int mw_subscribe(mw_topic_t topic, mw_subscribe_cb_t cb)
{
    int ret;
    pthread_mutex_lock(&mw_state.lock);
    if (mw_state.is_backend) {
        ret = subscribe_into_table(mw_state.backend_handlers, mw_state.backend_subscriber_count, topic, cb);
    } else {
        ret = subscribe_into_table(mw_state.ui_handlers, mw_state.ui_subscriber_count, topic, cb);
    }
    pthread_mutex_unlock(&mw_state.lock);
    return ret;
}

int mw_unsubscribe(mw_topic_t topic, mw_subscribe_cb_t cb)
{
    int ret;
    pthread_mutex_lock(&mw_state.lock);
    if (mw_state.is_backend) {
        ret = unsubscribe_from_table(mw_state.backend_handlers, mw_state.backend_subscriber_count, topic, cb);
    } else {
        ret = unsubscribe_from_table(mw_state.ui_handlers, mw_state.ui_subscriber_count, topic, cb);
    }
    pthread_mutex_unlock(&mw_state.lock);
    return ret;
}

void mw_process_ui_messages(void)
{
    int ui_socket;
    bool is_backend;

    pthread_mutex_lock(&mw_state.lock);
    is_backend = mw_state.is_backend;
    pthread_mutex_unlock(&mw_state.lock);
    if (is_backend) return;

    pthread_mutex_lock(&mw_state.lock);
    ui_socket = mw_state.ipc_socket;
    pthread_mutex_unlock(&mw_state.lock);

    if (ui_socket < 0) {
        if (ipc_client_connect(&ui_socket) != 0) return;
        pthread_mutex_lock(&mw_state.lock);
        if (mw_state.ipc_socket < 0) {
            mw_state.ipc_socket = ui_socket;
        } else {
            close(ui_socket);
            ui_socket = mw_state.ipc_socket;
        }
        pthread_mutex_unlock(&mw_state.lock);
        APP_LOGI("middleware", "ui reconnected");
    }

    for (int handled = 0; handled < UI_MAX_MESSAGES_PER_TICK; handled++) {
        ipc_msg_t msg;
        int ret = ipc_client_recv(ui_socket, &msg, sizeof(msg));
        if (ret < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                APP_LOGW("middleware", "ui recv error, reset connection");
                pthread_mutex_lock(&mw_state.lock);
                if (mw_state.ipc_socket == ui_socket) {
                    close(mw_state.ipc_socket);
                    mw_state.ipc_socket = -1;
                }
                pthread_mutex_unlock(&mw_state.lock);
            }
            break;
        }
        if (ret == 0) break;
        if (msg.header.flags & IPC_FLAG_HEARTBEAT) continue;
        dispatch_ui_message(&msg);
    }
}

int mw_pending_ui_messages_count(void)
{
    int ui_socket;
    bool is_backend;
    pthread_mutex_lock(&mw_state.lock);
    is_backend = mw_state.is_backend;
    ui_socket = mw_state.ipc_socket;
    pthread_mutex_unlock(&mw_state.lock);
    if (is_backend || ui_socket < 0) return 0;
    return ipc_socket_poll(ui_socket, 0) > 0 ? 1 : 0;
}

int mw_backend_register_handler(mw_topic_t topic, void (*cb)(const mw_msg_t *msg))
{
    bool is_backend;
    pthread_mutex_lock(&mw_state.lock);
    is_backend = mw_state.is_backend;
    pthread_mutex_unlock(&mw_state.lock);
    if (!is_backend) {
        APP_LOGE("middleware", "register handler only for backend mode");
        return -1;
    }
    return mw_subscribe(topic, cb);
}

void mw_process(void)
{
    bool initialized;
    bool is_backend;

    pthread_mutex_lock(&mw_state.lock);
    initialized = mw_state.initialized;
    is_backend = mw_state.is_backend;
    pthread_mutex_unlock(&mw_state.lock);

    if (!initialized) return;
    if (!is_backend) {
        mw_process_ui_messages();
        return;
    }

    struct pollfd fds[MAX_CLIENTS + 1];
    int client_slots[MAX_CLIENTS + 1];
    int nfds = 0;
    pthread_mutex_lock(&mw_state.lock);
    fds[nfds].fd = mw_state.listen_socket;
    fds[nfds].events = POLLIN;
    client_slots[nfds] = -1;
    nfds++;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (mw_state.clients[i].active) {
            fds[nfds].fd = mw_state.clients[i].fd;
            fds[nfds].events = POLLIN;
            client_slots[nfds] = i;
            nfds++;
        }
    }
    pthread_mutex_unlock(&mw_state.lock);

    int ret = poll(fds, nfds, 0);
    if (ret <= 0) return;

    if (fds[0].revents & POLLIN) {
        (void)handle_new_connection();
    }

    int fd_idx = 1;
    while (fd_idx < nfds) {
        int slot = client_slots[fd_idx];
        if (fds[fd_idx].revents & POLLIN) {
            if (handle_client_message(slot) < 0) handle_client_disconnect(slot);
        }
        if (fds[fd_idx].revents & (POLLERR | POLLHUP)) {
            handle_client_disconnect(slot);
        }
        fd_idx++;
    }
}

void mw_backend_check_heartbeat_timeouts(void)
{
    bool initialized;
    bool is_backend;
    time_t now = time(NULL);
    int timeout_slots[MAX_CLIENTS];
    int timeout_count = 0;

    pthread_mutex_lock(&mw_state.lock);
    initialized = mw_state.initialized;
    is_backend = mw_state.is_backend;
    if (initialized && is_backend) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (mw_state.clients[i].active &&
                now - mw_state.clients[i].last_heartbeat > IPC_HEARTBEAT_TIMEOUT) {
                timeout_slots[timeout_count++] = i;
            }
        }
    }
    pthread_mutex_unlock(&mw_state.lock);

    for (int i = 0; i < timeout_count; i++) {
        int slot = timeout_slots[i];
        APP_LOGW("middleware", "client %d heartbeat timeout", slot);
        handle_client_disconnect(slot);
    }
}

void mw_backend_run(void)
{
    bool is_backend;

    pthread_mutex_lock(&mw_state.lock);
    is_backend = mw_state.is_backend;
    pthread_mutex_unlock(&mw_state.lock);
    if (!is_backend) {
        APP_LOGE("middleware", "mw_backend_run only backend mode");
        return;
    }

    APP_LOGI("middleware", "backend run loop started");
    while (1) {
        mw_process();
        mw_backend_check_heartbeat_timeouts();
        usleep(1000);
    }
}

void mw_get_status(mw_status_t *status)
{
    if (!status) return;

    pthread_mutex_lock(&mw_state.lock);
    if (mw_state.is_backend) {
        status->connected = false;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (mw_state.clients[i].active) {
                status->connected = true;
                break;
            }
        }
    } else {
        status->connected = (mw_state.ipc_socket >= 0);
    }

    status->socket_fd = mw_state.ipc_socket;
    status->msg_sent = mw_state.msg_sent;
    status->msg_received = mw_state.msg_received;
    pthread_mutex_unlock(&mw_state.lock);
}

int mw_get_socket_fd(void)
{
    int fd;
    pthread_mutex_lock(&mw_state.lock);
    if (mw_state.is_backend || !mw_state.initialized) {
        pthread_mutex_unlock(&mw_state.lock);
        return -1;
    }
    fd = mw_state.ipc_socket;
    pthread_mutex_unlock(&mw_state.lock);
    return fd;
}
