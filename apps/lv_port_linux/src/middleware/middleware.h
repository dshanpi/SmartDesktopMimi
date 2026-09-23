/**
 * @file middleware.h
 * @brief 多进程架构消息中间件
 *
 * 架构说明：
 * - UI 进程：运行 LVGL，通过 IPC 发送请求到 Backend
 * - Backend 进程：独立进程，处理业务逻辑，通过 IPC 返回结果
 * - IPC：Unix Domain Socket 通信
 */

#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/*****************************************************************************
 *                                 配置参数
 *****************************************************************************/
#define MW_MAX_SUBSCRIBERS      10      /* 每个主题最大订阅者数量 */
#define MW_MSG_DATA_MAX_SIZE    512     /* 消息数据最大长度 */

/*****************************************************************************
 * 1. 定义消息主题 (Topics) - 按应用/模块分类
 *****************************************************************************/
typedef enum {
    /* System */
    TOPIC_SYSTEM_STATUS,

    /* WiFi App */
    TOPIC_WIFI_COMMAND,     /* UI -> Backend (Scan, Connect) */
    TOPIC_WIFI_STATUS,      /* Backend -> UI (Scan Results, Connection State) */
    TOPIC_WIFI_RUNTIME,     /* Backend -> UI (Enabled/Scanning/Connected snapshot) */

    /* Sensor */
    TOPIC_SENSOR_STATUS,    /* Backend -> UI (Temperature/Humidity) */

    /* Network */
    TOPIC_NETWORK_INFO,     /* Backend -> UI (IP address, interface state) */

    /* Video App */
    TOPIC_VIDEO_CONTROL,    /* UI -> Backend (Play, Pause, Seek, etc.) */
    TOPIC_VIDEO_STATUS,     /* Backend -> UI (Playing state, Progress) */
    TOPIC_VIDEO_PLAYLIST,   /* Backend -> UI (File list) */

    /* Bluetooth App */
    TOPIC_BT_COMMAND,       /* UI -> Backend (Enable, Disconnect, SetVol) */
    TOPIC_BT_RUNTIME,       /* Backend -> UI (Adapter state, A2DP state, Volume) */
    TOPIC_BT_AVRCP_INFO,    /* Backend -> UI (Track title, artist, album) */

    /* AI Assistant */
    TOPIC_AI_COMMAND,       /* UI -> Backend (Start/Stop listen, service control) */
    TOPIC_AI_STATUS,        /* Backend -> UI (AI state and process status) */
    TOPIC_AI_TEXT,          /* Backend -> UI (STT/TTS text) */

    /* HDMI Preview */
    TOPIC_HDMI_COMMAND,        /* UI -> Backend (Enable/Disable/GetStatus) */
    TOPIC_HDMI_PREVIEW_STATUS, /* Backend -> UI (probe/preview process state) */

    /* OTA Upgrade */
    TOPIC_OTA_COMMAND,         /* UI -> Backend (Check/Download/Apply/Commit) */
    TOPIC_OTA_STATUS,          /* Backend -> UI (OTA state and progress) */

    /* 100ask Cloud */
    TOPIC_CLOUD_COMMAND,       /* UI -> Backend (GetStatus) */
    TOPIC_CLOUD_STATUS,        /* Backend -> UI (cloud connection / OTA push state) */

    /* LED strip (UI interaction feedback → Backend service_led) */
    TOPIC_LED_COMMAND,         /* UI -> Backend (swipe / preview cues) */

    TOPIC_MAX
} mw_topic_t;

/*****************************************************************************
 * 2. 消息结构定义
 *****************************************************************************/

/* 消息方向标记 */
typedef enum {
    MW_DIR_UI_TO_BACKEND,   /* UI 发往后端 */
    MW_DIR_BACKEND_TO_UI    /* 后端发往 UI */
} mw_direction_t;

/* 消息对象 */
typedef struct {
    mw_topic_t topic;
    mw_direction_t direction;
    uint32_t id;                /* 消息 ID (可选，用于请求响应匹配) */
    uint32_t data_len;          /* 实际数据长度 */
    uint8_t data[MW_MSG_DATA_MAX_SIZE];  /* 内联数据缓冲区 */
} mw_msg_t;

/*****************************************************************************
 * 3. 回调函数原型
 *****************************************************************************/
typedef void (*mw_subscribe_cb_t)(const mw_msg_t * msg);

/*****************************************************************************
 * 4. 中间件初始化/销毁
 *****************************************************************************/

/**
 * @brief 初始化中间件
 * @param is_backend_process 是否为 Backend 进程
 * @return 0 成功，-1 失败
 */
int mw_init(bool is_backend_process);

/**
 * @brief 销毁中间件，释放资源
 */
void mw_deinit(void);

/**
 * @brief 获取是否初始化成功
 */
bool mw_is_initialized(void);

/*****************************************************************************
 * 5. 发布/订阅 API
 *****************************************************************************/

/**
 * @brief 发布消息 (Publish)
 * @param topic 主题
 * @param data 数据指针
 * @param len 数据长度 (不能超过 MW_MSG_DATA_MAX_SIZE)
 * @param direction 消息方向
 * @return 0 成功，-1 失败 (未连接或参数错误)
 */
int mw_publish(mw_topic_t topic, const void * data, uint32_t len, mw_direction_t direction);

/**
 * @brief 订阅消息 (Subscribe)
 * @note 必须在 UI 线程中调用
 * @param topic 感兴趣的主题
 * @param cb 回调函数
 * @return 0 成功，-1 失败
 */
int mw_subscribe(mw_topic_t topic, mw_subscribe_cb_t cb);

/**
 * @brief 取消订阅消息 (Unsubscribe)
 * @note 必须在 UI 线程中调用
 * @param topic 感兴趣的主题
 * @param cb 回调函数
 * @return 0 成功，-1 失败
 */
int mw_unsubscribe(mw_topic_t topic, mw_subscribe_cb_t cb);

/*****************************************************************************
 * 6. UI 线程消息处理
 *****************************************************************************/

/**
 * @brief 处理 Backend 发往 UI 的消息 (必须在 LVGL 主循环中定期调用)
 * @note 此函数从 IPC socket 读取消息并分发给订阅者
 */
void mw_process_ui_messages(void);

/**
 * @brief 获取待处理的 UI 消息数量
 */
int mw_pending_ui_messages_count(void);

/*****************************************************************************
 * 7. Backend 进程专用接口
 *****************************************************************************/

/**
 * @brief 运行中间件消息处理循环 (非阻塞，单次处理)
 */
void mw_process(void);

/**
 * @brief Backend 单次心跳超时检查
 */
void mw_backend_check_heartbeat_timeouts(void);

/**
 * @brief 运行中间件主循环 (阻塞，内部循环)
 * @note 仅用于简单场景，复杂场景建议使用 mw_process
 */
void mw_backend_run(void);

/**
 * @brief Backend 进程注册消息处理器
 * @param topic 感兴趣的主题
 * @param cb 处理回调
 * @return 0 成功，-1 失败
 */
int mw_backend_register_handler(mw_topic_t topic, void (*cb)(const mw_msg_t * msg));

/*****************************************************************************
 * 8. 辅助工具
 *****************************************************************************/

/**
 * @brief 获取中间件运行状态信息
 */
typedef struct {
    bool connected;             /* 是否连接到 Backend */
    int socket_fd;              /* IPC socket 文件描述符 */
    uint32_t msg_sent;          /* 发送消息数 */
    uint32_t msg_received;      /* 接收消息数 */
} mw_status_t;

void mw_get_status(mw_status_t * status);

/**
 * @brief 获取 UI 进程的 IPC socket 文件描述符
 * @return socket fd，失败返回 -1
 */
int mw_get_socket_fd(void);

#endif /* MIDDLEWARE_H */
