#ifndef BACKEND_TYPES_H
#define BACKEND_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/*****************************************************************************
 *                                 Wi-Fi 相关定义
 *****************************************************************************/

/* Wi-Fi 指令结构体 */
typedef struct {
    enum {
        CMD_SCAN,
        CMD_CONNECT,
        CMD_DISCONNECT,
        CMD_ENABLE,
        CMD_DISABLE,
        CMD_GET_RUNTIME
    } action;
    char ssid[33]; /* 32 chars + NUL */
    char password[64];
} wifi_cmd_t;

/* Wi-Fi 状态结构体 (单个 AP 信息) */
typedef struct {
    char ssid[33];
    int rssi;         /* 0-4 */
    bool encrypted;
    bool connected;
} wifi_ap_info_t;

/* Wi-Fi 运行时状态（权威状态快照） */
typedef struct {
    bool enabled;
    bool scanning;
    bool connected;
    char connected_ssid[33];
    int32_t last_error_code; /* 0=ok, negative=error, positive=platform event code */
} wifi_runtime_status_t;

/*****************************************************************************
 *                               Network 相关定义
 *****************************************************************************/

typedef struct {
    bool has_ip;
    char ip_addr[16];       /* "xxx.xxx.xxx.xxx" */
    char ifname[16];        /* interface name, e.g. wlan0 */
} network_info_t;

/*****************************************************************************
 *                                 Sensor 相关定义
 *****************************************************************************/

typedef struct {
    bool valid;
    int32_t temp_mC;        /* milli Celsius */
    int32_t humi_mpermil;   /* milli percent, 65000 = 65.000% */
    int32_t err_code;       /* 0=ok, negative=error */
    /* INA219 整机电流监测（仅本机 UI 顶栏显示，不上云） */
    bool    ina_valid;      /* INA219 探测到且读到数据 */
    int32_t bus_mV;         /* in1_input, 总线电压 mV */
    int32_t current_mA;     /* curr1_input, 电流 mA（有符号） */
    int32_t power_uW;       /* power1_input, 功率 µW */
} sensor_status_t;

/*****************************************************************************
 *                                 Video 相关定义
 *****************************************************************************/

/* Video 控制指令 */
typedef struct {
    enum {
        VIDEO_CMD_PLAY,         /* 播放指定索引或路径 */
        VIDEO_CMD_PAUSE,        /* 暂停 */
        VIDEO_CMD_RESUME,       /* 恢复 */
        VIDEO_CMD_STOP,         /* 停止 */
        VIDEO_CMD_SEEK,         /* 跳转 */
        VIDEO_CMD_NEXT,         /* 下一首 (由后端维护列表) */
        VIDEO_CMD_PREV,         /* 上一首 */
        VIDEO_CMD_SCAN_FILES,   /* 扫描文件 */
        VIDEO_CMD_SET_RECT      /* 设置显示区域 */
    } action;
    int32_t val;                /* 通用参数: seek秒数, 播放索引 */
    char path[256];             /* 路径参数 (可选) */
    struct { int x, y, w, h; } rect; /* 区域参数 */
} video_cmd_t;

/* Video 状态广播 */
typedef struct {
    bool is_playing;            /* true=正在播 */
    bool is_paused;             /* true=已暂停（仍有画面，勿盖黑底） */
    int32_t current_sec;
    int32_t total_sec;
    int32_t current_index;      /* 当前播放的列表索引 */
    int32_t source_w;           /* 片源宽（用于 UI 黑边，0=未知） */
    int32_t source_h;           /* 片源高 */
    char current_file[192];     /* 当前显示名（UTF-8 文件名，可含中文） */
    char status_text[96];       /* 可选状态文案；空则 UI 用默认「正在播放/已暂停」 */
} video_status_t;

/* Video 播放列表项 (用于 IPC 传输，简化版) */
typedef struct {
    int32_t total_count;        /* >=0 表示扫描结束，此时 index 忽略 */
    int32_t index;
    char path[256];             /* 相对 video_dir 的路径或 basename（UTF-8） */
    int32_t width;              /* 探测到的宽，未知为 0 */
    int32_t height;
    int32_t fps;                /* 近似帧率，未知为 0 */
    uint8_t unsupported;        /* 1=超出设备能力，不可点播 */
    char reason[80];            /* 不可播原因（UTF-8），供列表/Toast */
} video_playlist_item_t;

/*****************************************************************************
 *                               Bluetooth 相关定义
 *****************************************************************************/

/* BT 控制指令 (UI -> Backend) */
typedef struct {
    enum {
        BT_CMD_ENABLE,          /* 打开蓝牙 */
        BT_CMD_DISABLE,         /* 关闭蓝牙 */
        BT_CMD_DISCONNECT,      /* 断开已连接设备 */
        BT_CMD_SET_VOLUME,      /* 设置音量 */
        BT_CMD_PLAY,            /* 播放 */
        BT_CMD_PAUSE,           /* 暂停 */
        BT_CMD_NEXT,            /* 下一首 */
        BT_CMD_PREV,            /* 上一首 */
        BT_CMD_GET_STATUS,      /* 拉取当前状态 */
        BT_CMD_SET_ADAPTER_NAME /* 设置本机蓝牙广播名 */
    } action;
    char bd_addr[18];           /* disconnect 时使用 */
    int  volume;                /* 0-100 */
    char adapter_name[48];      /* SET_ADAPTER_NAME 时使用；空串=恢复自动名 */
} bt_cmd_t;

/* BT 运行时状态快照 (Backend -> UI) */
typedef struct {
    bool enabled;                  /* 蓝牙是否已开启 */
    bool discoverable;             /* 是否处于可发现模式 */
    char adapter_name[64];         /* 本机蓝牙名称 */
    char adapter_addr[18];         /* 本机 MAC 地址 */
    bool a2dp_connected;           /* 是否有 A2DP Sink 设备已连接 */
    bool audio_streaming;          /* 是否正在接收音频流 */
    char connected_device_name[128];/* 已连接设备名称，UTF-8 */
    char connected_device_addr[18];/* 已连接设备 MAC */
    int  volume;                   /* 当前音量 0-100 */
} bt_runtime_t;

typedef enum {
    BT_PLAYBACK_UNKNOWN = 0,
    BT_PLAYBACK_STOPPED,
    BT_PLAYBACK_PAUSED,
    BT_PLAYBACK_PLAYING,
} bt_playback_state_t;

/* AVRCP 歌曲信息 (Backend -> UI) */
typedef struct {
    char title[160];
    char artist[96];
    char album[96];
    char duration[32];
    char genre[64];
    int32_t song_len_ms;
    int32_t song_pos_ms;
    bool is_playing;
    bool player_available;          /* 手机端存在可控制的媒体播放器 */
    bt_playback_state_t playback_state;
} bt_avrcp_info_t;

/*****************************************************************************
 *                              AI Assistant 相关定义
 *****************************************************************************/

typedef enum {
    AI_CMD_START_LISTEN = 0,
    AI_CMD_STOP_LISTEN,
    AI_CMD_GET_STATUS,
    AI_CMD_ENTER_FREE_CHAT,
    AI_CMD_EXIT_FREE_CHAT,
} ai_cmd_action_t;

typedef struct {
    ai_cmd_action_t action;
} ai_cmd_t;

typedef enum {
    AI_STATE_IDLE = 0,
    AI_STATE_CONNECTING,
    AI_STATE_LISTENING,
    AI_STATE_THINKING,
    AI_STATE_SPEAKING,
    AI_STATE_NETWORK_UNAVAILABLE,
    AI_STATE_ERROR,
} ai_state_t;

typedef enum {
    AI_CHAT_MODE_UNKNOWN = 0,
    AI_CHAT_MODE_WAKEUP,
    AI_CHAT_MODE_FREE,
} ai_chat_mode_t;

#define AI_EMOTION_NAME_MAX 32
#define AI_BIND_URL_MAX 256

typedef struct {
    ai_state_t state;
    ai_chat_mode_t chat_mode;
    bool free_chat_active;
    bool control_center_running;
    bool sound_app_running;
    bool bt_paused_by_ai;
    bool tuya_bound;
    bool tuya_bind_qr_pending;
    int32_t last_error_code;
    char emotion[AI_EMOTION_NAME_MAX];
    char bind_url[AI_BIND_URL_MAX];
} ai_status_t;

typedef struct {
    char text[384];
} ai_text_t;

/*****************************************************************************
 *                              HDMI Preview 相关定义
 *****************************************************************************/

typedef enum {
    HDMI_PREVIEW_STATE_IDLE = 0,
    HDMI_PREVIEW_STATE_LOCKED,
    HDMI_PREVIEW_STATE_RUNNING,
    HDMI_PREVIEW_STATE_NO_SIGNAL,
    HDMI_PREVIEW_STATE_ERROR,
} hdmi_preview_state_t;

typedef struct {
    bool active;          /* UI should switch to transparent HDMI screen */
    bool signal_locked;   /* last probe saw a frame */
    bool enabled;         /* HDMI preview detection enabled (toggle state) */
    int32_t state;        /* hdmi_preview_state_t */
    int32_t exit_code;    /* child exit code, or negative errno-style status */
} hdmi_preview_status_t;

/* UI -> Backend：HDMI 预览检测启停指令（镜像 bt_cmd_t） */
typedef struct {
    enum {
        HDMI_CMD_ENABLE,      /* 打开 HDMI 持续检测 */
        HDMI_CMD_DISABLE,     /* 关闭 HDMI 持续检测 */
        HDMI_CMD_GET_STATUS,  /* 拉取当前状态快照 */
    } action;
} hdmi_cmd_t;

/*****************************************************************************
 *                              OTA 升级相关定义
 *****************************************************************************/

/* UI -> Backend：OTA 指令 */
typedef struct {
    enum {
        OTA_CMD_CHECK,        /* 检查更新：拉取服务器版本 JSON 比对本地版本 */
        OTA_CMD_DOWNLOAD,     /* 下载 .swu 到 UDISK */
        OTA_CMD_APPLY,        /* 写入非活动槽并重启 */
        OTA_CMD_COMMIT,       /* 新槽启动后确认，清 upgrade_available */
        OTA_CMD_GET_STATUS,   /* 查询当前 OTA 状态 */
        OTA_CMD_CANCEL,       /* 取消下载/应用 */
    } action;
    char url[400];            /* 检查/下载用的 URL（受 512 payload 限制） */
    char sha256[65];          /* 下载后校验用（64 hex + NUL），空则不校验 */
} ota_cmd_t;

/* Backend -> UI：OTA 状态 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING,
    OTA_STATE_UPDATE_AVAILABLE,
    OTA_STATE_UP_TO_DATE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_DOWNLOAD_DONE,
    OTA_STATE_APPLYING,
    OTA_STATE_REBOOTING,
    OTA_STATE_COMMITTING,
    OTA_STATE_ERROR,
} ota_state_t;

typedef struct {
    ota_state_t state;
    int32_t progress;         /* 0-100，下载/应用进度 */
    int32_t error_code;       /* 0=无错，负数=errno 风格，正数=业务错误 */
    bool update_available;
    char version_local[64];   /* 当前固件版本（/etc/aitvbox-version） */
    char version_remote[64];  /* 服务器上报的最新版本 */
} ota_status_t;

/*****************************************************************************
 *                              100ask Cloud 相关定义                         *
 *****************************************************************************/

/* 云连接状态 */
typedef enum {
    CLOUD_STATE_DISCONNECTED = 0,  /* 未连接/离线 */
    CLOUD_STATE_CONNECTING,        /* 连接中 */
    CLOUD_STATE_CONNECTED,         /* 已连 100ask 云 */
    CLOUD_STATE_UNAVAILABLE,       /* 当前构建未包含云 provider */
} cloud_state_t;

/* Backend -> UI：云连接 + 云端 OTA 推送状态 */
typedef struct {
    cloud_state_t state;
    char device_id[64];      /* 本机设备 ID（连云后回填） */
    bool bind_token_refreshing; /* 正在按需申请新的用户绑定码 */
    int32_t bind_token_error;   /* 0=无错误，负数=本地/网络错误，正数=HTTP 状态 */
    uint32_t bind_token_expires_in; /* 绑定码剩余有效秒数；0 表示当前无有效码 */
    char bind_token[128];    /* 临时绑定码；不落盘、不包含 device_secret */
    bool ota_pending;        /* 云端推送了 OTA（下载中/已完成），待用户安装 */
    char ota_version[64];    /* 云端推送的目标版本号 */
    bool ota_mandatory;      /* 强制更新：true 时弹窗不显示「稍后」 */
} cloud_status_t;

/* UI -> Backend：云服务指令 */
typedef struct {
    enum {
        CLOUD_CMD_GET_STATUS,    /* 拉取当前云状态快照 */
        CLOUD_CMD_REFRESH_BIND_TOKEN, /* 用户打开设置页时按需申请短期绑定码 */
        CLOUD_CMD_DISMISS_OTA,   /* 用户已处理本次推送（稍后/已安装），清 ota_pending */
    } action;
} cloud_cmd_t;

/*****************************************************************************
 *                              LED 状态灯（UI 交互反馈）
 *****************************************************************************/

/* UI -> Backend：短时灯效（Dock 滑动等）。由 service_led 仲裁，低优先级。 */
typedef struct {
    enum {
        LED_CMD_SWIPE_LEFT = 0,   /* 光点/彗星向左（屏上向左选） */
        LED_CMD_SWIPE_RIGHT,      /* 向右 */
    } action;
} led_cmd_t;

#endif /* BACKEND_TYPES_H */
