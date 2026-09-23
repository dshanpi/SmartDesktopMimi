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

int ipc_client_connect(int *sock_fd) {
    struct sockaddr_un addr;
    struct timeval tv;
    fd_set fdset;
    int flags;
    int ret;
    const app_config_t *cfg = app_config_get();

    if (!sock_fd) {
        return -1;
    }

    /* 创建 socket */
    *sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*sock_fd < 0) {
        perror("socket");
        return -1;
    }

    /* 设置为非阻塞以便设置连接超时 */
    flags = fcntl(*sock_fd, F_GETFL, 0);
    fcntl(*sock_fd, F_SETFL, flags | O_NONBLOCK);

    /* 配置地址 */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", cfg->ipc_socket_path);

    /* 发起连接 */
    ret = connect(*sock_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        perror("connect");
        close(*sock_fd);
        *sock_fd = -1;
        return -1;
    }

    /* 等待连接完成 */
    FD_ZERO(&fdset);
    FD_SET(*sock_fd, &fdset);
    tv.tv_sec = IPC_CONNECT_TIMEOUT_MS / 1000;
    tv.tv_usec = (IPC_CONNECT_TIMEOUT_MS % 1000) * 1000;

    ret = select(*sock_fd + 1, NULL, &fdset, NULL, &tv);
    if (ret <= 0) {
        perror("connect timeout");
        close(*sock_fd);
        *sock_fd = -1;
        return -1;
    }

    {
        int so_error = 0;
        socklen_t so_error_len = sizeof(so_error);
        if (getsockopt(*sock_fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0 ||
            so_error != 0) {
            if (so_error != 0) errno = so_error;
            perror("connect");
            close(*sock_fd);
            *sock_fd = -1;
            return -1;
        }
    }

    /* 设置 socket 选项 */
    set_socket_options(*sock_fd);

    APP_LOGI("ipc", "client connected to backend");
    return 0;
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

int ipc_server_start(int *listen_fd) {
    struct sockaddr_un addr;
    const app_config_t *cfg = app_config_get();

    if (!listen_fd) {
        return -1;
    }

    /* 清理旧的 socket 文件 */
    unlink(cfg->ipc_socket_path);

    /* 创建 socket */
    *listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*listen_fd < 0) {
        perror("socket");
        return -1;
    }

    /* 设置 socket 选项 */
    set_socket_options(*listen_fd);

    /* 配置地址 */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", cfg->ipc_socket_path);

    /* 绑定地址 */
    if (bind(*listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(*listen_fd);
        return -1;
    }

    /* 设置权限 (可选，允许其他用户访问) */
    chmod(cfg->ipc_socket_path, (mode_t)cfg->ipc_socket_mode);

    /* 开始监听 */
    if (listen(*listen_fd, 5) < 0) {
        perror("listen");
        close(*listen_fd);
        return -1;
    }

    APP_LOGI("ipc", "server started on %s", cfg->ipc_socket_path);
    return 0;
}

int ipc_server_accept(int listen_fd, int *client_fd) {
    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);

    if (listen_fd < 0 || !client_fd) {
        return -1;
    }

    *client_fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (*client_fd < 0) {
        perror("accept");
        return -1;
    }

    set_nonblocking(*client_fd);
    APP_LOGI("ipc", "client accepted");
    return 0;
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

void ipc_server_stop(int listen_fd) {
    const app_config_t *cfg = app_config_get();
    if (listen_fd >= 0) {
        APP_LOGI("ipc", "server stopping");
        close(listen_fd);
        unlink(cfg->ipc_socket_path);
    }
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
