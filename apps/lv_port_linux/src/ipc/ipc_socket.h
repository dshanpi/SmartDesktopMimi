/**
 * @file ipc_socket.h
 * @brief 进程间通信 - Unix Domain Socket
 *
 * 架构说明：
 * - UI 进程作为客户端，Backend 进程作为服务端
 * - 使用 Unix Domain Socket (SOCK_STREAM) 进行双向通信
 * - 支持双向通信和心跳检测
 */

#ifndef IPC_SOCKET_H
#define IPC_SOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/*****************************************************************************
 *                                 配置参数
 *****************************************************************************/
#define IPC_MAX_PAYLOAD_SIZE    512
#define IPC_CONNECT_TIMEOUT_MS  1000
#define IPC_HEARTBEAT_INTERVAL  5       /* 心跳间隔 (秒) */
#define IPC_HEARTBEAT_TIMEOUT   15      /* 心跳超时 (秒) */

/*****************************************************************************
 *                                 消息格式
 *****************************************************************************/

/* 消息头 (16 字节，固定长度) */
typedef struct {
    uint32_t magic;             /* 魔数：0x4C56474C ("LVGL") */
    uint32_t topic;             /* 主题/消息类型 */
    uint32_t payload_len;       /* 负载长度 */
    uint32_t flags;             /* 标志位 */
} ipc_header_t;

/* 标志位定义 */
#define IPC_FLAG_NONE           0x00
#define IPC_FLAG_HEARTBEAT      0x01  /* 心跳消息 */
#define IPC_FLAG_REQUEST        0x02  /* 请求消息 */
#define IPC_FLAG_RESPONSE       0x04  /* 响应消息 */
#define IPC_FLAG_ERROR          0x08  /* 错误消息 */

/* 完整消息结构 */
typedef struct {
    ipc_header_t header;
    uint8_t payload[IPC_MAX_PAYLOAD_SIZE];
} ipc_msg_t;

/* 魔数校验 */
#define IPC_MAGIC               0x4C56474C

/*****************************************************************************
 *                                 客户端接口 (UI 进程使用)
 *****************************************************************************/

/**
 * @brief 连接到 Backend 服务
 * @param sock_fd 返回的 socket 文件描述符
 * @return 0 成功，-1 失败
 */
int ipc_client_connect(int *sock_fd);

/**
 * @brief 发送消息到 Backend
 * @param sock_fd Socket 文件描述符
 * @param topic 消息主题
 * @param data 数据指针
 * @param len 数据长度
 * @param flags 标志位
 * @return 0 成功，-1 失败
 */
int ipc_client_send(int sock_fd, uint32_t topic, const void *data, uint32_t len, uint32_t flags);

/**
 * @brief 从 Backend 接收消息 (非阻塞)
 * @param sock_fd Socket 文件描述符
 * @param msg 接收消息缓冲区
 * @param max_len 最大接收长度
 * @return 实际接收长度，-1 失败，0 无数据
 */
int ipc_client_recv(int sock_fd, ipc_msg_t *msg, size_t max_len);

/**
 * @brief 断开连接
 * @param sock_fd Socket 文件描述符
 */
void ipc_client_disconnect(int sock_fd);

/**
 * @brief 发送心跳
 * @param sock_fd Socket 文件描述符
 * @return 0 成功，-1 失败
 */
int ipc_client_heartbeat(int sock_fd);

/*****************************************************************************
 *                                 服务端接口 (Backend 进程使用)
 *****************************************************************************/

/**
 * @brief 创建并启动 Backend 服务
 * @param listen_fd 返回的监听 socket 文件描述符
 * @return 0 成功，-1 失败
 */
int ipc_server_start(int *listen_fd);

/**
 * @brief 接受客户端连接 (阻塞)
 * @param listen_fd 监听 socket 文件描述符
 * @param client_fd 返回的客户端 socket 文件描述符
 * @return 0 成功，-1 失败
 */
int ipc_server_accept(int listen_fd, int *client_fd);

/**
 * @brief 发送消息到客户端
 * @param client_fd 客户端 socket 文件描述符
 * @param topic 消息主题
 * @param data 数据指针
 * @param len 数据长度
 * @param flags 标志位
 * @return 0 成功，-1 失败
 */
int ipc_server_send(int client_fd, uint32_t topic, const void *data, uint32_t len, uint32_t flags);

/**
 * @brief 从客户端接收消息 (可阻塞/非阻塞)
 * @param client_fd 客户端 socket 文件描述符
 * @param msg 接收消息缓冲区
 * @param max_len 最大接收长度
 * @param block 是否阻塞
 * @return 实际接收长度，-1 失败，0 无数据
 */
int ipc_server_recv(int client_fd, ipc_msg_t *msg, size_t max_len, bool block);

/**
 * @brief 停止服务并清理
 * @param listen_fd 监听 socket 文件描述符
 */
void ipc_server_stop(int listen_fd);

/**
 * @brief 检查 Socket 是否有数据可读
 * @param sock_fd Socket 文件描述符
 * @param timeout_ms 超时时间 (毫秒), 0 表示不等待
 * @return >0 有数据，0 超时，-1 错误
 */
int ipc_socket_poll(int sock_fd, int timeout_ms);

#endif /* IPC_SOCKET_H */
