#include "ui_i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *zh;
    const char *en;
} ui_translation_t;

#define T(zh_value, en_value) { (zh_value), (en_value) }

static const ui_translation_t translations[UI_STR_COUNT] = {
    [UI_STR_LOCKSCREEN_WAKE] = T("轻触屏幕唤醒", "Tap anywhere to wake"),
    [UI_STR_APP_VIDEO] = T("视频", "Video"),
    [UI_STR_APP_WIFI] = T("无线网络", "Wi-Fi"),
    [UI_STR_APP_SETTINGS] = T("设置", "Settings"),
    [UI_STR_APP_AI] = T("涂鸦助手", "Tuya Assistant"),
    [UI_STR_APP_BLUETOOTH] = T("蓝牙音响", "Bluetooth Audio"),
    [UI_STR_APP_OTA] = T("系统更新", "System Update"),
    [UI_STR_APP_USER] = T("用户应用", "User Apps"),
    [UI_STR_APP_HDMI_MCP] = T("HDMI 智能控制", "HDMI MCP"),
    [UI_STR_ALL_APPS] = T("全部应用", "All Apps"),
    [UI_STR_ALL_APPS_HINT] = T("选择一个应用，常用功能仍会保留在桌面。", "Choose an app. Favorites remain on the desktop."),
    [UI_STR_HOME] = T("桌面", "Home"),
    [UI_STR_AI_CHAT] = T("助手", "Assistant"),
    [UI_STR_CONTROL_CENTER] = T("控制中心", "Control Center"),
    [UI_STR_DISPLAY_MODE] = T("HDMI 直通", "HDMI passthrough"),
    [UI_STR_HDMI_DISPLAY] = T("HDMI 显示", "HDMI Display"),
    [UI_STR_CONNECTED] = T("已连接", "Connected"),
    [UI_STR_NOT_CONNECTED] = T("未连接", "Not connected"),
    [UI_STR_OFF] = T("已关闭", "Off"),
    [UI_STR_ON] = T("已开启", "On"),
    [UI_STR_CANCEL] = T("取消", "Cancel"),
    [UI_STR_CONNECT] = T("连接", "Connect"),
    [UI_STR_DISCONNECT] = T("断开连接", "Disconnect"),
    [UI_STR_REFRESH] = T("刷新", "Refresh"),
    [UI_STR_RETRY] = T("重试", "Retry"),
    [UI_STR_SAVE] = T("保存", "Save"),
    [UI_STR_DONE] = T("完成", "Done"),
    [UI_STR_STOP] = T("停止任务", "Stop task"),
    [UI_STR_LOADING] = T("正在加载…", "Loading…"),
    [UI_STR_FAILED] = T("失败", "Failed"),
    [UI_STR_READY] = T("就绪", "Ready"),
    [UI_STR_UNKNOWN] = T("未知", "Unknown"),
    [UI_STR_SCANNING] = T("正在扫描…", "Scanning…"),
    [UI_STR_WIFI_OFF] = T("无线网络已关闭", "Wi-Fi is off"),
    [UI_STR_AVAILABLE_NETWORKS] = T("可用网络", "Available Networks"),
    [UI_STR_CONNECTING] = T("正在连接…", "Connecting…"),
    [UI_STR_WIFI_PASSWORD] = T("网络密码", "Network password"),
    [UI_STR_SHOW_PASSWORD] = T("显示密码", "Show password"),
    [UI_STR_SECURED_NETWORK] = T("此网络需要密码", "This network requires a password"),
    [UI_STR_NO_NETWORKS] = T("暂未发现网络，点按刷新重新扫描。", "No networks found. Tap Refresh to scan again."),
    [UI_STR_WIFI_PASSWORD_SHORT] = T("密码至少需要 8 个字符", "Password must contain at least 8 characters"),
    [UI_STR_SETTINGS_APPEARANCE] = T("外观与语言", "Appearance & Language"),
    [UI_STR_SETTINGS_LANGUAGE] = T("界面语言", "Display language"),
    [UI_STR_SETTINGS_LANGUAGE_AUTO] = T("跟随系统", "System"),
    [UI_STR_SETTINGS_LANGUAGE_ZH] = T("简体中文", "Chinese"),
    [UI_STR_SETTINGS_LANGUAGE_EN] = T("English", "English"),
    [UI_STR_SETTINGS_THEME] = T("显示模式", "Appearance"),
    [UI_STR_SETTINGS_THEME_AUTO] = T("自动", "Auto"),
    [UI_STR_SETTINGS_THEME_LIGHT] = T("亮色", "Light"),
    [UI_STR_SETTINGS_THEME_DARK] = T("暗色", "Dark"),
    [UI_STR_SETTINGS_DISPLAY_SOUND] = T("显示与声音", "Display & Sound"),
    [UI_STR_SETTINGS_BRIGHTNESS] = T("亮度", "Brightness"),
    [UI_STR_SETTINGS_VOLUME] = T("音量", "Volume"),
    [UI_STR_SETTINGS_ACCENT] = T("桌面强调色", "Desktop accent"),
    [UI_STR_SETTINGS_FOLLOW_TIME] = T("随时间变化", "Follow time"),
    [UI_STR_SETTINGS_DEVICE_INFO] = T("设备信息", "Device Info"),
    [UI_STR_SETTINGS_DEVICE_NAME] = T("设备名称", "Device Name"),
    [UI_STR_SETTINGS_SYSTEM_VERSION] = T("系统版本", "System Version"),
    [UI_STR_SETTINGS_100ASK_PAIRING] = T("100ask.net 配对", "100ask.net Pairing"),
    [UI_STR_SETTINGS_100ASK_BOUND] = T("设备已绑定，配对码已隐藏", "Device bound · pairing code hidden"),
    [UI_STR_SETTINGS_100ASK_TAP_CODE] = T("点按获取配对码", "Tap to get pairing code"),
    [UI_STR_SETTINGS_100ASK_UNAVAILABLE] = T("当前版本未启用 100ask.net", "100ask.net unavailable in this build"),
    [UI_STR_SETTINGS_100ASK_RETRY] = T("获取失败，点按重试", "Failed · tap to retry"),
    [UI_STR_SETTINGS_PREVIEW] = T("桌面预览", "Desktop Preview"),
    [UI_STR_SETTINGS_AI_DESKTOP] = T("Mimi 智能桌面", "Mimi AI Desktop"),
    [UI_STR_AI_STARTING] = T("正在启动助手", "Starting assistant"),
    [UI_STR_AI_LISTENING] = T("正在聆听", "Listening"),
    [UI_STR_AI_THINKING] = T("正在思考", "Thinking"),
    [UI_STR_AI_SPEAKING] = T("正在回答", "Speaking"),
    [UI_STR_AI_NETWORK_OFFLINE] = T("无线网络未连接", "Wi-Fi disconnected"),
    [UI_STR_AI_SERVICE_ERROR] = T("助手服务异常", "Assistant service error"),
    [UI_STR_AI_HELLO] = T("你好，我是涂鸦助手", "Hello, I'm Tuya Assistant"),
    [UI_STR_AI_WAKE_HINT] = T("说“你好涂鸦”唤醒我", "Say “Ni Hao Tuya” to wake me"),
    [UI_STR_AI_FREE_CHAT] = T("自由对话", "Free Chat"),
    [UI_STR_AI_FREE_CHAT_DESC] = T("打开全屏语音助手", "Open the full-screen assistant"),
    [UI_STR_AI_LOADING_STATUS] = T("正在同步助手状态", "Loading assistant status"),
    [UI_STR_AI_DEVICE_CLOUD] = T("设备与云端", "Device & Cloud"),
    [UI_STR_AI_CHECK_PAIRING] = T("正在检查涂鸦设备绑定状态…", "Checking Tuya device pairing status…"),
    [UI_STR_AI_CHECK_SCHEMA] = T("正在检查物联功能模型…", "Checking Tuya IoT product functions…"),
    [UI_STR_AI_CURRENT_CONVERSATION] = T("当前对话", "Current Conversation"),
    [UI_STR_AI_NO_CONVERSATION] = T("还没有对话", "No conversation yet"),
    [UI_STR_AI_EXIT_HINT] = T("双击屏幕退出自由对话", "Double-tap the screen to exit Free Chat"),
    [UI_STR_AI_PAIRED_QR_HIDDEN] = T("已绑定涂鸦账号，配对二维码已隐藏。", "Paired to Tuya · pairing QR is hidden."),
    [UI_STR_AI_UNBIND_HINT] = T("如需更换账号，请先在智能生活中解绑或重置。", "Unbind or reset in Smart Life to pair another account."),
    [UI_STR_AI_NOT_PAIRED] = T("尚未绑定，请使用智能生活扫描二维码。", "Not paired · scan the QR code with Smart Life."),
    [UI_STR_AI_SCHEMA_CHECKING] = T("正在检查物联功能…", "Checking IoT controls…"),
    [UI_STR_AI_SCHEMA_LIMITED_FMT] = T("物联功能受限：云端已开放 %u/%u 个 DP", "IoT controls limited: %u/%u DPs available"),
    [UI_STR_AI_SCHEMA_READY_FMT] = T("物联功能已就绪：%u/%u 个 DP", "IoT controls ready: %u/%u DPs"),
    [UI_STR_AI_PAIR_TITLE] = T("绑定你的设备", "Pair Your Device"),
    [UI_STR_AI_PAIR_GUIDE] = T("绑定方法", "How to Pair"),
    [UI_STR_AI_PAIR_LATER] = T("稍后绑定并退出", "Pair Later and Exit"),
    [UI_STR_AI_PAIR_SCAN] = T("使用智能生活扫描", "Scan with Smart Life"),
    [UI_STR_AI_PAIR_WAITING] = T("等待扫码，绑定后将自动继续", "Waiting for scan · continues after pairing"),
    [UI_STR_AI_OPEN_FREE_CHAT] = T("打开自由对话", "Open Free Chat"),
    [UI_STR_AI_OPENING] = T("正在打开…", "Opening…"),
    [UI_STR_AI_WAITING_ASSISTANT] = T("正在等待助手响应", "Waiting for the assistant"),
    [UI_STR_AI_PAIR_FIRST] = T("请先绑定设备", "Pair Device First"),
    [UI_STR_AI_PAIR_TO_CONTINUE] = T("绑定设备后即可继续", "Pair this device to continue"),
    [UI_STR_AI_CONNECT_WIFI] = T("连接无线网络", "Connect to Wi-Fi"),
    [UI_STR_AI_WIFI_REQUIRED] = T("语音助手需要无线网络", "Wi-Fi is required"),
    [UI_STR_AI_SERVICE_UNAVAILABLE] = T("服务暂不可用", "Service Unavailable"),
    [UI_STR_AI_TRY_LATER] = T("涂鸦服务暂不可用，请稍后重试。", "Tuya service unavailable. Try again later."),
    [UI_STR_AI_CONNECTING_SERVICE] = T("正在连接涂鸦助手", "Connecting to Tuya Assistant"),
    [UI_STR_AI_CONVERSATION_PROGRESS] = T("对话进行中", "Conversation in Progress"),
    [UI_STR_AI_WAIT_CONVERSATION] = T("请等待当前对话结束", "Wait for the current conversation to finish"),
    [UI_STR_AI_OPEN_FAILED] = T("无法打开自由对话，请重试。", "Could not open Free Chat. Try again."),
    [UI_STR_AI_FREE_CHAT_ACTIVE] = T("自由对话已开启", "Free Chat Active"),
    [UI_STR_AI_SCAN_SMART_LIFE] = T("请使用智能生活扫描二维码绑定设备", "Scan with Smart Life to pair this device"),
    [UI_STR_AI_RETRIEVING_PAIRING] = T("正在获取绑定状态", "Retrieving pairing status"),
    [UI_STR_AI_SERVICE_STARTING] = T("涂鸦语音服务正在启动", "Starting the Tuya voice service"),
    [UI_STR_AI_IM_LISTENING] = T("我在听", "I'm listening"),
    [UI_STR_AI_REPLYING] = T("正在回复", "Replying"),
    [UI_STR_AI_SYNCING_STATUS] = T("正在同步涂鸦状态", "Syncing Tuya status"),
    [UI_STR_BT_RENAME] = T("修改蓝牙设备名称", "Rename Bluetooth Device"),
    [UI_STR_BT_NAME_PLACEHOLDER] = T("例如：客厅音响", "Example: Living Room Speaker"),
    [UI_STR_BT_AUTO_NAME] = T("使用自动名称", "Auto Name"),
    [UI_STR_BT_WAITING] = T("等待手机连接", "Waiting for connection"),
    [UI_STR_BT_AUDIO] = T("蓝牙音频", "Bluetooth Audio"),
    [UI_STR_BT_TURN_ON_HINT] = T("开启蓝牙后即可连接手机", "Turn on Bluetooth to connect a phone"),
    [UI_STR_BT_PHONE_PLAYER_CLOSED] = T("手机播放器尚未打开", "Phone player is not open"),
    [UI_STR_BT_OPEN_PLAYER_HINT] = T("请在手机上打开音乐播放器", "Open a music player on your phone"),
    [UI_STR_BT_DEVICE_MANAGEMENT] = T("设备与连接", "Device & Connections"),
    [UI_STR_BT_NAME_TAP_EDIT] = T("蓝牙名称，点按修改", "Bluetooth name · tap to edit"),
    [UI_STR_BT_MAC_ADDRESS] = T("MAC 地址", "MAC Address"),
    [UI_STR_VIDEO_LIBRARY] = T("视频库", "Video Library"),
    [UI_STR_VIDEO_CHOOSE] = T("选择一个视频", "Choose a Video"),
    [UI_STR_VIDEO_PLAYER] = T("视频播放器", "Video Player"),
    [UI_STR_VIDEO_SCAN_HINT] = T("正在扫描 microSD 卡…", "Scanning the microSD card…"),
    [UI_STR_VIDEO_EMPTY] = T("没有找到可播放的视频", "No playable videos found"),
    [UI_STR_VIDEO_UNABLE] = T("无法播放这个视频", "Unable to Play This Video"),
    [UI_STR_VIDEO_COMPAT_HINT] = T("支持最高 4K@30，无法兼容的文件会被跳过", "Up to 4K@30 · unsupported files are skipped"),
    [UI_STR_OTA_FIRMWARE_UPDATE] = T("系统更新", "Firmware Update"),
    [UI_STR_OTA_STATUS] = T("状态：", "Status:"),
    [UI_STR_OTA_IDLE] = T("等待检查", "Idle"),
    [UI_STR_OTA_CHECKING] = T("正在检查更新", "Checking for updates"),
    [UI_STR_OTA_AVAILABLE] = T("发现新版本", "Update available"),
    [UI_STR_OTA_UP_TO_DATE] = T("已是最新版本", "Up to date"),
    [UI_STR_OTA_DOWNLOADING] = T("正在下载", "Downloading"),
    [UI_STR_OTA_READY_INSTALL] = T("可以安装", "Ready to install"),
    [UI_STR_OTA_INSTALLING] = T("正在安装", "Installing"),
    [UI_STR_OTA_RESTARTING] = T("即将重启", "Restarting soon"),
    [UI_STR_OTA_FINALIZING] = T("正在完成更新", "Finalizing update"),
    [UI_STR_OTA_FAILED] = T("更新失败", "Update failed"),
    [UI_STR_OTA_CHECK] = T("检查更新", "Check for updates"),
    [UI_STR_OTA_DOWNLOAD] = T("下载", "Download"),
    [UI_STR_OTA_INSTALL] = T("立即安装", "Install now"),
    [UI_STR_OTA_POWER_HINT] = T("安装期间请保持电源连接", "Keep power connected during installation"),
    [UI_STR_USER_NO_APPS] = T("还没有安装应用", "No apps installed"),
    [UI_STR_USER_LAUNCH_FAILED] = T("无法启动应用", "Could not launch the app"),
    [UI_STR_USER_NOT_AUTHORIZED] = T("此操作未获得授权", "Action not authorized"),
    [UI_STR_USER_HARDWARE_FAILED] = T("硬件请求失败", "Hardware request failed"),
    [UI_STR_USER_REQUEST_COMPLETE] = T("操作已完成", "Request completed"),
    [UI_STR_MCP_CURRENT_TASK] = T("当前任务", "Current Task"),
    [UI_STR_MCP_SYNCING] = T("正在同步…", "Syncing…"),
    [UI_STR_MCP_OFFLINE] = T("服务离线", "Service offline"),
    [UI_STR_MCP_RUNNING] = T("AI 正在控制电脑", "AI is controlling the computer"),
    [UI_STR_MCP_STOPPING] = T("正在停止", "Stopping"),
    [UI_STR_MCP_SUCCEEDED] = T("任务已完成", "Task completed"),
    [UI_STR_MCP_FAILED] = T("任务失败", "Task failed"),
    [UI_STR_MCP_EMPTY] = T("从浏览器启动的任务会显示在这里。", "Tasks started in the browser appear here."),
    [UI_STR_MCP_HISTORY] = T("对话与操作记录", "Conversation and Actions"),
    [UI_STR_ENVIRONMENT_OFFLINE] = T("环境传感器离线", "Environment sensor offline"),
    [UI_STR_TIME_NOT_SET] = T("时间未设置", "Time not set"),
    [UI_STR_WELCOME_MIMI] = T("欢迎使用 Mimi", "Welcome to Mimi"),
    [UI_STR_HOME_AI_ASSISTANT] = T("Mimi 助手", "Mimi Assistant"),
    [UI_STR_HOME_GREETING_NIGHT] = T("夜深了，也可以找我", "Hello, night owl"),
    [UI_STR_HOME_GREETING_MORNING] = T("早上好", "Good morning"),
    [UI_STR_HOME_GREETING_AFTERNOON] = T("下午好", "Good afternoon"),
    [UI_STR_HOME_GREETING_EVENING] = T("晚上好", "Good evening"),
    [UI_STR_HOME_HELP_PROMPT] = T("今天想做些什么？", "What can I help with?"),
    [UI_STR_HOME_TALK_MIMI] = T("和 Mimi 对话", "Talk to Mimi"),
    [UI_STR_HOME_READY_CONTROL] = T("可以开始控制", "Ready to control"),
    [UI_STR_HOME_SECURE_HDMI] = T("安全的 HDMI 工作区", "Secure HDMI workspace"),
    [UI_STR_HOME_HDMI_LIVE] = T("HDMI 控制", "HDMI Control"),
    [UI_STR_HOME_OPEN_KVM] = T("打开控制", "Open KVM"),
    [UI_STR_HOME_APPS] = T("应用", "Apps"),
    [UI_STR_HOME_BROWSE_APPS] = T("浏览应用库", "Browse library"),
    [UI_STR_HOME_SCAN_CONNECT] = T("扫描并连接", "Scan & connect"),
    [UI_STR_HOME_UPDATES] = T("更新", "Updates"),
    [UI_STR_HOME_SYSTEM_SOFTWARE] = T("系统软件", "System software"),
    [UI_STR_HOME_DISPLAY_SOUND] = T("显示与声音", "Display & sound"),
    [UI_STR_CONTROL_APPS_HINT] = T("安装和打开应用", "Install and open apps"),
    [UI_STR_CONTROL_SETTINGS_HINT] = T("显示、声音和主题", "Display, sound, and theme"),
};

#undef T

static ui_language_t active_language = UI_LANG_EN_US;
static uint32_t generation = 1U;

static bool environment_is_chinese(void)
{
    const char *values[] = {
        getenv("LC_ALL"), getenv("LC_MESSAGES"), getenv("LANG")
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        const char *value = values[i];
        if (!value || !value[0]) continue;
        char lowered[32];
        size_t length = strlen(value);
        if (length >= sizeof(lowered)) length = sizeof(lowered) - 1;
        for (size_t j = 0; j < length; j++)
            lowered[j] = (char)tolower((unsigned char)value[j]);
        lowered[length] = '\0';
        if (!strncmp(lowered, "zh", 2) || strstr(lowered, "chinese"))
            return true;
        if (!strncmp(lowered, "en", 2) || strstr(lowered, "english"))
            return false;
        /* C/POSIX is not a user language choice on the embedded image. */
    }
    /* The shipping device is Chinese-first. English remains explicitly
     * selectable from Settings and through an en_* process locale. */
    return true;
}

static ui_language_t resolve_language(sys_locale_mode_t mode)
{
    if (mode == SYS_LOCALE_ZH_CN) return UI_LANG_ZH_CN;
    if (mode == SYS_LOCALE_EN_US) return UI_LANG_EN_US;
    return environment_is_chinese() ? UI_LANG_ZH_CN : UI_LANG_EN_US;
}

void ui_i18n_init(void)
{
    active_language = resolve_language((sys_locale_mode_t)
                                       sys_settings_get()->locale_mode);
}

ui_language_t ui_i18n_language(void) { return active_language; }

sys_locale_mode_t ui_i18n_mode(void)
{
    return (sys_locale_mode_t)sys_settings_get()->locale_mode;
}

void ui_i18n_set_mode(sys_locale_mode_t mode)
{
    if (mode < SYS_LOCALE_AUTO || mode > SYS_LOCALE_EN_US)
        mode = SYS_LOCALE_AUTO;
    ui_language_t resolved = resolve_language(mode);
    sys_settings_t *settings = sys_settings_get();
    bool changed = settings->locale_mode != (uint32_t)mode ||
                   active_language != resolved;
    settings->locale_mode = (uint32_t)mode;
    active_language = resolved;
    if (changed) {
        generation++;
        sys_settings_save();
    }
}

bool ui_i18n_is_zh(void) { return active_language == UI_LANG_ZH_CN; }
uint32_t ui_i18n_generation(void) { return generation; }

const char *ui_tr(ui_string_id_t id)
{
    if (id < 0 || id >= UI_STR_COUNT) return "";
    return active_language == UI_LANG_ZH_CN ? translations[id].zh
                                             : translations[id].en;
}

void ui_i18n_format_date(char *buffer, size_t size,
                         int year, int month, int day, int weekday)
{
    static const char *week_zh[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
    };
    static const char *week_en[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    if (!buffer || size == 0) return;
    if (weekday < 0 || weekday > 6) weekday = 0;
    if (ui_i18n_is_zh())
        snprintf(buffer, size, "%d年%d月%d日  %s", year, month, day, week_zh[weekday]);
    else
        snprintf(buffer, size, "%s, %02d/%02d/%04d", week_en[weekday], month, day, year);
}
