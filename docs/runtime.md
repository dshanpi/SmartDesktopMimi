# 运行与启动流程

本文描述产品固件启动后，`boot-play`、`aitvbox`、`lv_backend`、`lvglsim`、
Tuya chatbot、HDMI preview 和蓝牙初始化的关系。

## rootfs 入口文件

产品包 `aitvbox-suite` 自身安装：

```text
/usr/bin/lv_backend
/usr/bin/lvglsim
/usr/bin/hdmi_preview
/usr/bin/your_chat_bot_QIO_1.0.1.bin
/usr/bin/aitvbox-start-ui
/usr/lib/libMNN.so
/usr/lib/libMNN_Express.so
/usr/share/tuyaopen_models/mdtc_chunk_300ms.mnn
/usr/share/tuyaopen_models/tokens.txt
/usr/share/fonts/SarasaUiSC-Regular.ttf
/usr/share/fonts/SarasaUiSC-SemiBold.ttf
/usr/share/aitvbox/emotions/*.json       # 10 个 Lottie 情绪素材
/etc/init.d/aitvbox-data
/etc/init.d/aitvbox
/etc/rc.d/S95aitvbox-data
/etc/rc.d/S99aitvbox
/etc/swupdate_public.pem                  # OTA 验签公钥
/etc/aitvbox-version                      # 固件版本戳
/etc/aitvbox-tuya-pid                     # 固件实际使用的涂鸦产品 PID
/etc/100ask/                              # 100ask Cloud（空目录出厂，可选 cloud.conf/ca.crt）
```

> 完整清单以 `packaging/aitvbox-suite/Makefile` 的 install 段为准。

`boot-play` 是依赖包，提供：

```text
/sbin/boot-play
/etc/init.d/play
/etc/rc.d/S25play
/usr/res/boot-play/
```

## procd 启动顺序

OpenWrt init/procd 按 `/etc/rc.d/S*` 顺序启动服务。产品相关顺序为：

```text
S25play
  -> /etc/init.d/play
  -> echo 5 > /tmp/boot_progress
  -> /sbin/boot-play boot &

S95aitvbox-data
  -> bind /etc/bluetooth 到 /overlay/bluetooth
  -> bind /etc/lib/bluetooth 到 /overlay/lib/bluetooth
  -> bind /etc/wifi/wpa_supplicant 到 /overlay/wifi/wpa_supplicant
  -> mkdir -p /overlay/tuyadb

S96bluetooth_init
  -> reset AIC 蓝牙状态
  -> hciattach /dev/ttyS1 aic
  -> hciconfig hci0 up

S99aitvbox
  -> /etc/init.d/aitvbox
  -> echo 78 > /tmp/boot_progress
  -> procd instance: lv_backend
  -> echo 84 > /tmp/boot_progress
  -> procd instance: lvglsim via /usr/bin/aitvbox-start-ui
```

`boot-play` 先占用 framebuffer 显示启动画面；LVGL UI 不会直接抢 framebuffer。

## aitvbox init

`/etc/init.d/aitvbox` 启动 `lv_backend` 时设置环境变量：

```text
HOME=/tmp
LV_SETTINGS_FILE=/overlay/lv_port_linux_settings.bin
LV_AI_RUNTIME=tuya
LV_AI_TUYA_BIN=/usr/bin/your_chat_bot_QIO_1.0.1.bin
TUYA_CHAT_BOT_BIN=/usr/bin/your_chat_bot_QIO_1.0.1.bin
HDMI_PREVIEW_BIN=/usr/bin/hdmi_preview
LD_LIBRARY_PATH=/usr/lib
```

### 卡片主题色（跟随时间）

设置页「卡片主题」控制时钟卡渐变主色，写入 `LV_SETTINGS_FILE`：

| 项 | 说明 |
|----|------|
| **跟随时间** | 默认开启；按本地时刻切换 5 段色调 |
| 05:00–08:00 | 清晨 · 柔玫瑰 |
| 08:00–12:00 | 上午 · 天蓝 |
| 12:00–17:00 | 下午 · 靛蓝 |
| 17:00–20:00 | 傍晚 · 暖珊瑚 |
| 20:00–05:00 | 夜晚 · 暮紫 |
| 手动点色点 | 关闭跟随时间，锁定该色 |
| 再开跟随时间 | 立即按当前时段恢复自动 |

UI 每分钟检查一次时段；跨段时更新主题并刷新桌面时钟卡。依赖 RTC/系统本地时间正确。

`lv_backend` stdout/stderr 进入：

```text
/tmp/lv_backend.log
```

`/etc/init.d/aitvbox` 还通过 procd 启动：

```text
/usr/bin/aitvbox-start-ui
```

## lv_backend 内部服务

`lv_backend` 启动后会初始化 middleware/IPC，并创建 Unix domain socket：

```text
/tmp/lv_port_linux_backend.sock
```

随后初始化后端服务：

```text
LED strip（最先）/ time / wifi / video / sensor / bluetooth
  / AI runtime / HDMI preview / OTA / cloud
```

**WS2812 状态灯**：`service_led` 是 `/dev/ws2812-leds` 的唯一所有者（默认熄灭，
事件反馈；Dock 滑动经 `TOPIC_LED_COMMAND`）。WiFi/AI/OTA 只 request/release，
不直接写设备。内核 `CONFIG_LEDS_WS2812_SPI`，DT `ws2812@0`（SPI2）。
详见 [led.md](led.md)。

蓝牙底层由 `S96bluetooth_init` 准备，`lv_backend` 只负责 btmanager/profile 层。
如果后端启动时 `hci0` 还没有 `UP`，`service_bt_init()` 会非阻塞延迟初始化，
后续由 `service_bt_update()` 周期重试，不阻塞 UI、IPC、AI 或 HDMI preview。
详细根因和验证命令见 [bluetooth.md](bluetooth.md)。

当前 AI runtime 为 Tuya。默认 `LV_AI_MANAGE_TUYA=1`，所以 `lv_backend` 会 fork/exec：

```text
/usr/bin/your_chat_bot_QIO_1.0.1.bin
```

Tuya 日志默认进入：

```text
/tmp/tuya_chat_bot.log
```

HDMI preview monitor 会 fork/exec：

```text
/usr/bin/hdmi_preview --service
```

并通过 Unix datagram socket 接收 preview 状态，发布给 UI。

## UI 交接

`/usr/bin/aitvbox-start-ui` 是启动 UI 的保护脚本，流程为：

```text
1. 最多等待 30 秒，直到 /tmp/lv_port_linux_backend.sock 出现
2. 写 /tmp/boot_progress = 100
3. 等 boot-play 自行退出
4. 若 boot-play 未退出，killall boot-play
5. 仍未退出则 killall -9 boot-play
6. 确认 boot-play 不再运行后，exec /usr/bin/lvglsim
```

`lvglsim` stdout/stderr 进入：

```text
/tmp/lvglsim.log
```

最终稳定运行态通常为：

```text
procd
  |- lv_backend
  |   |- your_chat_bot_QIO_1.0.1.bin
  |   `- hdmi_preview --service
  `- lvglsim
```

停止 `/etc/init.d/aitvbox` 时会杀掉 Tuya chatbot、HDMI preview、`lv_backend` 和 `lvglsim`。
