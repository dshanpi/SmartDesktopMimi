/**
 * @file middleware.c
 * @brief 多进程架构消息中间件实现
 */

#include "middleware.h"
#include "../ipc/connection.h"
#include "../ipc/ipc_socket.h"
#include "../system/log/app_log.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 4
#define UI_MAX_MESSAGES_PER_TICK 16

typedef struct {
    mimi_connection *connection;
    int fd;
    bool active;
    time_t last_heartbeat;
} client_t;

static struct {
    pthread_mutex_t lock;
    bool is_backend;
    bool initialized;
    int ipc_socket;
    mimi_connection *ui_connection;
    uint64_t ui_epoch;
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

/* A slot owns one reference; each operation acquires another under lock. */
static void ui_detach(mimi_connection *expected)
{
    bool removed=false;
    pthread_mutex_lock(&mw_state.lock);
    if(mw_state.ui_connection==expected){mw_state.ui_connection=NULL;mw_state.ipc_socket=-1;removed=true;}
    pthread_mutex_unlock(&mw_state.lock);
    if(removed){mimi_connection_stop(expected);mimi_connection_release(expected);}
}
static mimi_connection *ui_acquire(bool reconnect)
{
    pthread_mutex_lock(&mw_state.lock);
    if(!mw_state.initialized || mw_state.is_backend){pthread_mutex_unlock(&mw_state.lock);return NULL;}
    uint64_t epoch=mw_state.ui_epoch;
    mimi_connection *c=mw_state.ui_connection;
    if(c && mimi_connection_retain(c))c=NULL;
    pthread_mutex_unlock(&mw_state.lock);
    if(c || !reconnect)return c;
    int fd=-1;if(ipc_client_connect(&fd))return NULL;
    mimi_connection *created=mimi_connection_create(fd);
    if(!created){close(fd);return NULL;}
    pthread_mutex_lock(&mw_state.lock);
    if(mw_state.initialized && !mw_state.is_backend && mw_state.ui_epoch==epoch){
        if(!mw_state.ui_connection){mw_state.ui_connection=created;mw_state.ipc_socket=fd;created=NULL;}
        c=mw_state.ui_connection;if(mimi_connection_retain(c))c=NULL;
    }
    pthread_mutex_unlock(&mw_state.lock);
    if(created)mimi_connection_release(created);
    return c;
}
static bool ui_is_current(mimi_connection *c)
{
    pthread_mutex_lock(&mw_state.lock);
    bool current=mw_state.initialized && !mw_state.is_backend && mw_state.ui_connection==c;
    pthread_mutex_unlock(&mw_state.lock);return current;
}

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

static void backend_detach(int slot,mimi_connection *expected,bool timed,time_t observed)
{
    bool removed=false;
    pthread_mutex_lock(&mw_state.lock);
    if(mw_state.clients[slot].active && mw_state.clients[slot].connection==expected &&
       (!timed || mw_state.clients[slot].last_heartbeat==observed)){
        mw_state.clients[slot].active=false;mw_state.clients[slot].fd=-1;
        mw_state.clients[slot].connection=NULL;removed=true;
    }
    pthread_mutex_unlock(&mw_state.lock);
    if(removed){mimi_connection_stop(expected);mimi_connection_release(expected);}
}
static int handle_new_connection(int listener,uint64_t epoch)
{
    int fd=-1;if(ipc_server_accept(listener,&fd))return -1;
    mimi_connection *c=mimi_connection_create(fd);if(!c){close(fd);return -1;}
    pthread_mutex_lock(&mw_state.lock);
    if(mw_state.initialized && mw_state.is_backend && mw_state.ui_epoch==epoch){
        for(int i=0;i<MAX_CLIENTS;i++)if(!mw_state.clients[i].active){
            mw_state.clients[i].connection=c;mw_state.clients[i].fd=fd;
            mw_state.clients[i].active=true;mw_state.clients[i].last_heartbeat=time(NULL);
            pthread_mutex_unlock(&mw_state.lock);return 0;
        }
    }
    pthread_mutex_unlock(&mw_state.lock);mimi_connection_release(c);return -1;
}
static int handle_client_message(int slot,mimi_connection *c)
{
    pthread_mutex_lock(&mw_state.lock);
    bool current=mw_state.clients[slot].active && mw_state.clients[slot].connection==c;
    pthread_mutex_unlock(&mw_state.lock);if(!current)return 0;
    ipc_msg_t msg;int ret=mimi_connection_receive(c,&msg,1000);
    if(ret<=0)return ret;
    pthread_mutex_lock(&mw_state.lock);
    current=mw_state.clients[slot].active && mw_state.clients[slot].connection==c;
    if(current)mw_state.clients[slot].last_heartbeat=time(NULL);
    pthread_mutex_unlock(&mw_state.lock);
    if(current && !(msg.header.flags&IPC_FLAG_HEARTBEAT))dispatch_backend_message(&msg);
    return 0;
}

static int mw_init_ui(void)
{
    int fd=-1;if(ipc_client_connect(&fd))return -1;
    mimi_connection *c=mimi_connection_create(fd);if(!c){close(fd);return -1;}
    pthread_mutex_lock(&mw_state.lock);
    if(mw_state.initialized || mw_state.ui_epoch==UINT64_MAX){pthread_mutex_unlock(&mw_state.lock);mimi_connection_release(c);return -1;}
    ++mw_state.ui_epoch;
    memset(mw_state.ui_handlers,0,sizeof(mw_state.ui_handlers));
    memset(mw_state.ui_subscriber_count,0,sizeof(mw_state.ui_subscriber_count));
    mw_state.ui_connection=c;mw_state.ipc_socket=fd;
    mw_state.is_backend=false;mw_state.initialized=true;
    pthread_mutex_unlock(&mw_state.lock);return 0;
}

static int mw_init_backend(void)
{
    pthread_mutex_lock(&mw_state.lock);
    if(mw_state.initialized || mw_state.ui_epoch==UINT64_MAX){pthread_mutex_unlock(&mw_state.lock);return -1;}
    ++mw_state.ui_epoch;
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
    mimi_connection *ui_connection;
    int listen_socket;
    mimi_connection *connections[MAX_CLIENTS];
    int client_count = 0;

    pthread_mutex_lock(&mw_state.lock);
    initialized = mw_state.initialized;
    is_backend = mw_state.is_backend;
    ui_connection = mw_state.ui_connection;
    mw_state.ui_connection = NULL;
    if(mw_state.ui_epoch!=UINT64_MAX)++mw_state.ui_epoch;
    listen_socket = mw_state.listen_socket;
    mw_state.initialized = false;
    mw_state.ipc_socket = -1;
    mw_state.listen_socket = -1;
    if (is_backend) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (mw_state.clients[i].active) {
                connections[client_count++] = mw_state.clients[i].connection;
                mw_state.clients[i].connection = NULL;
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
            mimi_connection_stop(connections[i]);mimi_connection_release(connections[i]);
        }
    } else {
        if(ui_connection){mimi_connection_stop(ui_connection);mimi_connection_release(ui_connection);}
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

int mw_publish(mw_topic_t topic,const void *data,uint32_t len,mw_direction_t direction)
{
    (void)direction;
    if(topic>=TOPIC_MAX || len>MW_MSG_DATA_MAX_SIZE || (len && !data))return -1;
    mimi_connection *connections[MAX_CLIENTS];int slots[MAX_CLIENTS],count=0;
    pthread_mutex_lock(&mw_state.lock);
    bool initialized=mw_state.initialized,backend=mw_state.is_backend;
    if(initialized && backend)for(int i=0;i<MAX_CLIENTS;i++){
        mimi_connection *c=mw_state.clients[i].connection;
        if(mw_state.clients[i].active && c && !mimi_connection_retain(c)){connections[count]=c;slots[count++]=i;}
    }
    pthread_mutex_unlock(&mw_state.lock);
    if(!initialized)return -1;
    if(backend){
        int sent=0;
        for(int i=0;i<count;i++){
            if(!mimi_connection_send(connections[i],topic,data,len,IPC_FLAG_NONE,20))sent++;
            else if(mimi_connection_stopped(connections[i]))backend_detach(slots[i],connections[i],false,0);
            mimi_connection_release(connections[i]);
        }
        if(sent){pthread_mutex_lock(&mw_state.lock);mw_state.msg_sent++;pthread_mutex_unlock(&mw_state.lock);return 0;}
        return -1;
    }
    mimi_connection *c=ui_acquire(true);if(!c)return -1;
    int ret=mimi_connection_send(c,topic,data,len,IPC_FLAG_NONE,100);
    if(ret<0){if(mimi_connection_stopped(c))ui_detach(c);}
    else{pthread_mutex_lock(&mw_state.lock);mw_state.msg_sent++;pthread_mutex_unlock(&mw_state.lock);}
    mimi_connection_release(c);return ret;
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
    mimi_connection *c=ui_acquire(true);if(!c)return;
    for(int handled=0;handled<UI_MAX_MESSAGES_PER_TICK && ui_is_current(c);handled++){
        ipc_msg_t msg;int ret=mimi_connection_receive(c,&msg,1000);
        if(ret<0){if(errno!=EAGAIN && errno!=EWOULDBLOCK)ui_detach(c);break;}
        if(!ret)break;
        if(!ui_is_current(c))break;
        if(!(msg.header.flags&IPC_FLAG_HEARTBEAT))dispatch_ui_message(&msg);
    }
    mimi_connection_release(c);
}
int mw_pending_ui_messages_count(void)
{
    mimi_connection *c=ui_acquire(false);if(!c)return 0;
    int pending=ipc_socket_poll(mimi_connection_fd(c),0)>0;
    mimi_connection_release(c);return pending;
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
    struct pollfd fds[MAX_CLIENTS+1];mimi_connection *connections[MAX_CLIENTS];int slots[MAX_CLIENTS],count=0;
    pthread_mutex_lock(&mw_state.lock);
    if(!mw_state.initialized){pthread_mutex_unlock(&mw_state.lock);return;}
    if(!mw_state.is_backend){pthread_mutex_unlock(&mw_state.lock);mw_process_ui_messages();return;}
    uint64_t epoch=mw_state.ui_epoch;
    int listener=fcntl(mw_state.listen_socket,F_DUPFD_CLOEXEC,0);
    fds[0]=(struct pollfd){.fd=listener,.events=POLLIN};
    for(int i=0;i<MAX_CLIENTS;i++){
        mimi_connection *c=mw_state.clients[i].connection;
        if(mw_state.clients[i].active && c && !mimi_connection_retain(c)){
            connections[count]=c;slots[count]=i;fds[++count]=(struct pollfd){.fd=mimi_connection_fd(c),.events=POLLIN};
        }
    }
    pthread_mutex_unlock(&mw_state.lock);
    int ret=poll(fds,count+1,0);
    if(ret>=0){
        if(fds[0].revents&POLLIN)handle_new_connection(listener,epoch);
        for(int i=0;i<count;i++){
            if(handle_client_message(slots[i],connections[i])<0)
                backend_detach(slots[i],connections[i],false,0);
            if(fds[i+1].revents&(POLLERR|POLLHUP|POLLNVAL))backend_detach(slots[i],connections[i],false,0);
        }
    }
    if(listener>=0)close(listener);
    for(int i=0;i<count;i++)mimi_connection_release(connections[i]);
}
void mw_backend_check_heartbeat_timeouts(void)
{
    mimi_connection *expired[MAX_CLIENTS];int slots[MAX_CLIENTS],count=0;time_t seen[MAX_CLIENTS],now=time(NULL);
    pthread_mutex_lock(&mw_state.lock);
    if(mw_state.initialized && mw_state.is_backend)for(int i=0;i<MAX_CLIENTS;i++){
        client_t *slot=&mw_state.clients[i];
        if(slot->active && now-slot->last_heartbeat>IPC_HEARTBEAT_TIMEOUT && !mimi_connection_retain(slot->connection)){
            expired[count]=slot->connection;slots[count]=i;seen[count++]=slot->last_heartbeat;
        }
    }
    pthread_mutex_unlock(&mw_state.lock);
    for(int i=0;i<count;i++){backend_detach(slots[i],expired[i],true,seen[i]);mimi_connection_release(expired[i]);}
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
