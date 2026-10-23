#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/**
 * @file ipc_socket.c
 * @brief Unix Domain Socket 进程间通信实现
 */

#include "ipc_socket.h"
#include "../system/config/app_config.h"
#include "../system/log/app_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <poll.h>
#include <time.h>
#include <stddef.h>
#include <sys/file.h>
#include <pthread.h>

#define IPC_IO_TIMEOUT_MS 20

/*****************************************************************************
 *                                 内部函数
 *****************************************************************************/

/**
 * @brief 设置 socket 为非阻塞模式
 * @note 当前未使用，保留供未来扩展
 */
__attribute__((unused))
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief 设置 socket 选项
 */
static int set_socket_options(int fd) {
    int opt = 1;

    /* 允许地址重用 */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 设置发送缓冲区 */
    int sndbuf = 8192;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    /* 设置接收缓冲区 */
    int rcvbuf = 8192;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    return 0;
}

static int wait_fd_ready(int fd, bool write_ready, int timeout_ms) {
    do {
        fd_set fdset;
        struct timeval tv;

        FD_ZERO(&fdset);
        FD_SET(fd, &fdset);

        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(fd + 1,
                         write_ready ? NULL : &fdset,
                         write_ready ? &fdset : NULL,
                         NULL,
                         &tv);
        if (ret >= 0) return ret;
    } while (errno == EINTR);

    return -1;
}

static int send_message(int fd, const void *buf, size_t len, int extra_flags) {
    const uint8_t *ptr = (const uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t ret = send(fd, ptr, remaining, MSG_NOSIGNAL | extra_flags);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ret = wait_fd_ready(fd, true, IPC_IO_TIMEOUT_MS);
                if (ret > 0) {
                    continue;
                }
                errno = (ret == 0) ? ETIMEDOUT : errno;
                return -1;
            }
            return -1;
        }
        if (ret == 0) {
            return -1;
        }
        ptr += ret;
        remaining -= (size_t)ret;
    }

    return 0;
}

static int recv_exact_nonblock(int fd, void *buf, size_t len, bool allow_empty) {
    uint8_t *ptr = (uint8_t *)buf;
    size_t remaining = len;
    size_t received = 0;

    while (remaining > 0) {
        ssize_t ret = recv(fd, ptr, remaining, MSG_DONTWAIT);
        if (ret > 0) {
            ptr += ret;
            remaining -= (size_t)ret;
            received += (size_t)ret;
            continue;
        }

        if (ret == 0) {
            errno = ECONNRESET;
            return -1;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (received == 0 && allow_empty) {
                errno = EAGAIN;
                return 0;
            }

            ret = wait_fd_ready(fd, false, IPC_IO_TIMEOUT_MS);
            if (ret > 0) {
                continue;
            }
            errno = (ret == 0) ? ETIMEDOUT : errno;
            return -1;
        }

        return -1;
    }

    return (int)len;
}

static int recv_ipc_message(int fd, ipc_msg_t *msg) {
    int ret;

    ret = recv_exact_nonblock(fd, &msg->header, sizeof(ipc_header_t), true);
    if (ret <= 0) {
        return ret;
    }

    if (msg->header.magic != IPC_MAGIC) {
        fprintf(stderr, "[IPC] Invalid magic: 0x%08X\n", msg->header.magic);
        errno = EPROTO;
        return -1;
    }

    if (msg->header.payload_len > IPC_MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "[IPC] Payload too large: %u\n", msg->header.payload_len);
        errno = EMSGSIZE;
        return -1;
    }

    if (msg->header.payload_len > 0) {
        ret = recv_exact_nonblock(fd, msg->payload, msg->header.payload_len, false);
        if (ret <= 0) {
            if (ret == 0) errno = ETIMEDOUT;
            return -1;
        }
    }

    return (int)(sizeof(ipc_header_t) + msg->header.payload_len);
}

/*****************************************************************************
 *                                 客户端实现
 *****************************************************************************/

static int64_t connect_now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int ipc_client_connect(int *sock_fd)
{
    if(!sock_fd){errno=EINVAL;return -1;}*sock_fd=-1;
    const app_config_t *cfg=app_config_get();struct sockaddr_un addr={.sun_family=AF_UNIX};
    if(!cfg){errno=EINVAL;return -1;}
    size_t size=strnlen(cfg->ipc_socket_path,sizeof(cfg->ipc_socket_path));
    if(!size || size>=sizeof(addr.sun_path)){errno=ENAMETOOLONG;return -1;}
    memcpy(addr.sun_path,cfg->ipc_socket_path,size+1);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
    int64_t start=connect_now_ms();if(start<0)goto fail;
    if(connect(fd,(struct sockaddr *)&addr,offsetof(struct sockaddr_un,sun_path)+size+1)<0){
        if(errno!=EINPROGRESS)goto fail;
        for(;;){
            int64_t now=connect_now_ms();if(now<0)goto fail;
            if(now-start>=IPC_CONNECT_TIMEOUT_MS){errno=ETIMEDOUT;goto fail;}
            struct pollfd p={.fd=fd,.events=POLLOUT};int rc=poll(&p,1,(int)(IPC_CONNECT_TIMEOUT_MS-(now-start)));
            if(rc<0 && errno==EINTR)continue;
            if(rc<=0){if(!rc)errno=ETIMEDOUT;goto fail;}
            now=connect_now_ms();if(now<0)goto fail;
            if(now-start>=IPC_CONNECT_TIMEOUT_MS){errno=ETIMEDOUT;goto fail;}
            if(p.revents&POLLNVAL){errno=EBADF;goto fail;}
            int error=0;socklen_t length=sizeof(error);
            if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length))goto fail;
            if(error){errno=error;goto fail;}
            if(!(p.revents&POLLOUT)){errno=ECONNABORTED;goto fail;}break;
        }
    }
    set_socket_options(fd);*sock_fd=fd;return 0;
fail: {int error=errno;close(fd);errno=error;return -1;}
}

int ipc_client_send(int sock_fd, uint32_t topic, const void *data, uint32_t len, uint32_t flags) {
    if (sock_fd < 0 || len > IPC_MAX_PAYLOAD_SIZE) {
        return -1;
    }
    if (len > 0 && !data) {
        return -1;
    }

    size_t total_len = sizeof(ipc_header_t) + len;
    uint8_t *buf = malloc(total_len);
    if (!buf) {
        perror("malloc");
        return -1;
    }

    ipc_header_t *hdr = (ipc_header_t *)buf;
    hdr->magic = IPC_MAGIC;
    hdr->topic = topic;
    hdr->payload_len = len;
    hdr->flags = flags;

    if (len > 0) {
        memcpy(buf + sizeof(ipc_header_t), data, len);
    }

    int send_ret = send_message(sock_fd, buf, total_len, 0);
    free(buf);

    if (send_ret != 0) {
        perror("send");
        return -1;
    }

    return 0;
}

int ipc_client_recv(int sock_fd, ipc_msg_t *msg, size_t max_len) {
    if (sock_fd < 0 || !msg) {
        return -1;
    }
    if (max_len < sizeof(ipc_msg_t)) {
        errno = EMSGSIZE;
        return -1;
    }

    return recv_ipc_message(sock_fd, msg);
}

void ipc_client_disconnect(int sock_fd) {
    if (sock_fd >= 0) {
        APP_LOGI("ipc", "client disconnecting");
        close(sock_fd);
    }
}

int ipc_client_heartbeat(int sock_fd) {
    return ipc_client_send(sock_fd, 0, NULL, 0, IPC_FLAG_HEARTBEAT);
}

/*****************************************************************************
 *                                 服务端实现
 *****************************************************************************/

static struct {
    pthread_mutex_t mutex;
    int fd, lock_fd;
    char path[108];
    dev_t device;ino_t inode;
} endpoint_owner={.mutex=PTHREAD_MUTEX_INITIALIZER,.fd=-1,.lock_fd=-1};
static bool same_socket_path(const char *path,dev_t device,ino_t inode)
{
    struct stat st;return !lstat(path,&st) && S_ISSOCK(st.st_mode) && st.st_dev==device && st.st_ino==inode;
}
int ipc_server_start(int *listen_fd)
{
    if(!listen_fd){errno=EINVAL;return -1;}*listen_fd=-1;
    const app_config_t *cfg=app_config_get();struct sockaddr_un addr={.sun_family=AF_UNIX};
    if(!cfg){errno=EINVAL;return -1;}
    size_t length=strnlen(cfg->ipc_socket_path,sizeof(cfg->ipc_socket_path));
    if(!length || length>=sizeof(addr.sun_path)){errno=ENAMETOOLONG;return -1;}
    memcpy(addr.sun_path,cfg->ipc_socket_path,length+1);
    char lock_path[sizeof(addr.sun_path)+6];snprintf(lock_path,sizeof(lock_path),"%s.lock",addr.sun_path);
    pthread_mutex_lock(&endpoint_owner.mutex);
    if(endpoint_owner.fd>=0){pthread_mutex_unlock(&endpoint_owner.mutex);errno=EALREADY;return -1;}
    int lock_fd=-1,fd=-1,error=0;bool bound=false;struct stat owned={0},st;
    lock_fd=open(lock_path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lock_fd<0)goto fail;
    if(fstat(lock_fd,&st))goto fail;
    if(!S_ISREG(st.st_mode) || st.st_uid!=geteuid() || st.st_nlink!=1 || (st.st_mode&077)){errno=EPERM;goto fail;}
    if(flock(lock_fd,LOCK_EX|LOCK_NB))goto fail;
    if(!lstat(addr.sun_path,&st)){
        if(!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid()){errno=EEXIST;goto fail;}
        int probe=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(probe<0)goto fail;
        int rc=connect(probe,(struct sockaddr *)&addr,offsetof(struct sockaddr_un,sun_path)+length+1);
        int probe_error=errno;close(probe);
        if(!rc || probe_error!=ECONNREFUSED){errno=EADDRINUSE;goto fail;}
        if(!same_socket_path(addr.sun_path,st.st_dev,st.st_ino)){errno=EAGAIN;goto fail;}
        if(unlink(addr.sun_path))goto fail;
    }else if(errno!=ENOENT)goto fail;
    fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)goto fail;
    if(bind(fd,(struct sockaddr *)&addr,offsetof(struct sockaddr_un,sun_path)+length+1))goto fail;
    if(lstat(addr.sun_path,&owned))goto fail;
    bound=true;
    if(chmod(addr.sun_path,(mode_t)cfg->ipc_socket_mode) || listen(fd,5))goto fail;
    endpoint_owner.fd=fd;endpoint_owner.lock_fd=lock_fd;
    memcpy(endpoint_owner.path,addr.sun_path,length+1);endpoint_owner.device=owned.st_dev;endpoint_owner.inode=owned.st_ino;
    *listen_fd=fd;pthread_mutex_unlock(&endpoint_owner.mutex);return 0;
fail:
    error=errno;
    if(bound && same_socket_path(addr.sun_path,owned.st_dev,owned.st_ino))unlink(addr.sun_path);
    if(fd>=0)close(fd);
    if(lock_fd>=0)close(lock_fd);
    pthread_mutex_unlock(&endpoint_owner.mutex);errno=error;return -1;
}

int ipc_server_accept(int listen_fd,int *client_fd)
{
    if(!client_fd || listen_fd<0){errno=EINVAL;return -1;}*client_fd=-1;
    int mode=fcntl(listen_fd,F_GETFL);if(mode<0)return -1;
    if(!(mode&O_NONBLOCK)){errno=EINVAL;return -1;}
    int fd=accept4(listen_fd,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
    if(fd<0)return -1;
    *client_fd=fd;return 0;
}

int ipc_server_send(int client_fd, uint32_t topic, const void *data, uint32_t len, uint32_t flags) {
    if (client_fd < 0 || len > IPC_MAX_PAYLOAD_SIZE) {
        return -1;
    }
    if (len > 0 && !data) {
        return -1;
    }

    size_t total_len = sizeof(ipc_header_t) + len;
    uint8_t *buf = malloc(total_len);
    if (!buf) {
        perror("malloc");
        return -1;
    }

    ipc_header_t *hdr = (ipc_header_t *)buf;
    hdr->magic = IPC_MAGIC;
    hdr->topic = topic;
    hdr->payload_len = len;
    hdr->flags = flags;

    if (len > 0) {
        memcpy(buf + sizeof(ipc_header_t), data, len);
    }

    int send_ret = send_message(client_fd, buf, total_len, MSG_DONTWAIT);
    free(buf);

    if (send_ret != 0) {
        perror("send");
        return -1;
    }

    return 0;
}

int ipc_server_recv(int client_fd, ipc_msg_t *msg, size_t max_len, bool block) {
    int ret;

    if (client_fd < 0 || !msg) {
        return -1;
    }
    if (max_len < sizeof(ipc_msg_t)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (block) {
        while (ipc_socket_poll(client_fd, IPC_IO_TIMEOUT_MS) == 0) {
            /* Keep waiting in block mode while still avoiding an indefinite recv(). */
        }
    }

    ret = recv_ipc_message(client_fd, msg);
    if (!block && ret == 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return ret;
}

void ipc_server_stop(int listen_fd)
{
    pthread_mutex_lock(&endpoint_owner.mutex);
    if(listen_fd>=0 && endpoint_owner.fd==listen_fd){
        if(same_socket_path(endpoint_owner.path,endpoint_owner.device,endpoint_owner.inode))unlink(endpoint_owner.path);
        close(endpoint_owner.fd);close(endpoint_owner.lock_fd);
        endpoint_owner.fd=-1;endpoint_owner.lock_fd=-1;endpoint_owner.path[0]=0;
    }
    pthread_mutex_unlock(&endpoint_owner.mutex);
}

int ipc_socket_poll(int sock_fd, int timeout_ms) {
    fd_set fdset;
    struct timeval tv;

    if (sock_fd < 0) {
        return -1;
    }

    FD_ZERO(&fdset);
    FD_SET(sock_fd, &fdset);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    return select(sock_fd + 1, &fdset, NULL, NULL, &tv);
}
