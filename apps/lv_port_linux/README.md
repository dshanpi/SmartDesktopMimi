# Product Process and Build Guide

本文档说明当前产品的软件结构、运行进程、后端如何管理外部应用进程，以及各进程/app 的编译方式。目标硬件为 Allwinner A133 Tina 5.0 OpenWrt 目标环境，屏幕分辨率固定为 1024x768。

## 0. 快速开始

如果你现在的目标是尽快把整机产物拉下来、编译出来并推到板子上，直接按下面步骤走。

### 0.1 代码来源

AI-DeskTopBox 主仓库，本地目录示例名使用 `lv_port_linux`：

```sh
git clone git@github.com:100askTeam/AI-DeskTopBox.git lv_port_linux
cd lv_port_linux
git fetch origin
```

当前产品里几个模块的上游关系建议这样理解：

- `lv_port_linux` 主界面和后端：主分支或当前产品集成分支
- `xiaozhi`：上游开发分支 `origin/feature/xiaozhi`
- `hdmi_preview`：上游开发分支 `origin/feature/hdmi-preview`

如果你要单独查看对应功能分支：

```sh
git switch -c feature/xiaozhi --track origin/feature/xiaozhi
git switch -c feature/hdmi-preview --track origin/feature/hdmi-preview
```

### 0.2 快速编译

以下命令均在 SDK 根目录 `/home/ubuntu/A133-Tina5.0-v0.9` 下执行。

编译 `lv_port_linux`：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9/lv_port_linux
export STAGING_DIR=/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir/target
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=.trae/skills/cross-build/toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWERROR=ON
cmake --build build -j$(nproc)
```

编译 `control_center`：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
cmake -S xiaozhi/control_center \
  -B xiaozhi/control_center/build-openwrt-auto \
  -DCMAKE_TOOLCHAIN_FILE=/home/ubuntu/A133-Tina5.0-v0.9/xiaozhi/toolchain.cmake \
  -DA133_TARGET=openwrt
cmake --build xiaozhi/control_center/build-openwrt-auto -- -j$(nproc)
```

编译 `sound_app`：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
cmake -S xiaozhi/sound_app \
  -B xiaozhi/sound_app/build-openwrt-auto \
  -DCMAKE_TOOLCHAIN_FILE=/home/ubuntu/A133-Tina5.0-v0.9/xiaozhi/toolchain.cmake \
  -DA133_TARGET=openwrt \
  -DENABLE_SHERPA_ONNX=ON \
  -DSHERPA_ONNX_DIR=/home/ubuntu/A133-Tina5.0-v0.9/xiaozhi/sherpa_onnx_kws/sherpa-onnx-aarch64-shared-cpu
cmake --build xiaozhi/sound_app/build-openwrt-auto -- -j$(nproc)
```

编译 `hdmi_preview`：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
make -C hdmi_preview -j$(nproc)
```

### 0.3 快速 ADB 推送

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9

adb shell mkdir -p /mnt/UDISK/sherpa-onnx-aarch64-shared-cpu /mnt/UDISK/models
adb push lv_port_linux/build/bin/lv_backend /mnt/UDISK/lv_backend
adb push lv_port_linux/build/bin/lvglsim /mnt/UDISK/lvglsim
adb push xiaozhi/control_center/build-openwrt-auto/control_center /mnt/UDISK/control_center
adb push xiaozhi/sound_app/build-openwrt-auto/sound_app /mnt/UDISK/sound_app
adb push hdmi_preview/build/hdmi_preview /mnt/UDISK/hdmi_preview
adb push xiaozhi/sherpa_onnx_kws/sherpa-onnx-aarch64-shared-cpu/lib /mnt/UDISK/sherpa-onnx-aarch64-shared-cpu/
adb push xiaozhi/sherpa_onnx_kws/models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile /mnt/UDISK/models/
adb shell chmod 755 /mnt/UDISK/lv_backend /mnt/UDISK/lvglsim /mnt/UDISK/control_center /mnt/UDISK/sound_app /mnt/UDISK/hdmi_preview
```

### 0.4 快速启动

正式产品推荐只手动启动：

```sh
adb shell /mnt/UDISK/lv_backend
adb shell /mnt/UDISK/lvglsim
```

`control_center`、`sound_app`、`hdmi_preview` 由 `lv_backend` 自动管理，不建议手工重复拉起。

## 1. 产品概述

本产品以 `lv_port_linux` 为主应用，包含 LVGL 图形界面、后台服务层、HDMI 输入预览、蓝牙/Wi-Fi/视频/设置等本地功能，并集成 Xiaozhi AI 语音能力和本地 sherpa-onnx 关键词唤醒。

当前产品级进程主要包括：

| 进程 | 来源目录 | 作用 | 运行方式 |
| --- | --- | --- | --- |
| `lvglsim` | `lv_port_linux` | LVGL UI 主界面进程 | 产品 UI 主进程 |
| `lv_backend` | `lv_port_linux` | 后端服务进程，管理系统状态、设备服务和外部子进程 | 产品后端主进程 |
| `your_chat_bot_QIO_1.0.1.bin` | `TuyaOpen/apps/tuya.ai/your_chat_bot` | Tuya AI 聊天机器人服务 | 由 `lv_backend` 拉起和看护（当前固件默认 AI 运行时） |
| `control_center` | `xiaozhi/control_center` | Xiaozhi 云端激活、WebSocket 会话、AI 状态转发 | 由 `lv_backend` 拉起和看护（可选 AI 运行时） |
| `sound_app` | `xiaozhi/sound_app` | 录音、播放、Opus 编解码、本地 KWS 唤醒 | 由 `lv_backend` 拉起和看护（Xiaozhi 方案使用） |
| `hdmi_preview` | `hdmi_preview` | HDMI 输入采集并通过 Allwinner videoOutPort 显示 | 由 `lv_backend` 拉起和看护 |

当前 Tina 固件内置版默认使用 Tuya AI 运行时；Xiaozhi 方案相关路径和逻辑保留，可通过环境变量切换。

`sherpa_onnx_kws_demo` 是板端验证目录，用于单独验证 sherpa-onnx KWS 模型、麦克风和关键词文件。正式产品运行时不应同时启动独立 KWS demo 和 `sound_app`，因为它们会竞争 ALSA 录音设备。

## 2. 总体运行架构

运行时核心关系如下：

```text
lvglsim  <---- middleware IPC ---->  lv_backend
                                       |
                                       | fork/monitor
                                       v
                          your_chat_bot_QIO_1.0.1.bin   (Tuya AI)
                                       |
                                       | fork/monitor (可选)
                                       v
                               control_center
                                       ^
                                       | UDP UI channel 5678/5679
                                       |
                                       v
                                 sound_app
                                       ^
                                       | UDP audio/control 5676/5677/5680
                                       |
                                       v
                          ALSA capture/playback + sherpa KWS

lv_backend  ---- fork/monitor ---->  hdmi_preview --service
     ^                                      |
     | Unix datagram status socket          |
     +---- /tmp/hdmi_preview_status.sock <--+
```

`lvglsim` 与 `lv_backend` 之间通过 `lv_port_linux/src/middleware` 定义的 topic 通信。UI 不直接管理 AI 或 HDMI 进程，所有外部进程生命周期都集中在 `lv_backend` 的 service 层。

### 固件启动时的额外协调

在 Tina 固件内置部署模式下，系统启动流程还会涉及 `boot-play` 开机动画：

```text
boot-play  ---- 显示进度条  ---->  读取 /tmp/boot_progress
                                       ^
                                       |
/etc/init.d/aitvbox                    |
  ├── 启动 lv_backend                  |
  └── 启动 aitvbox-start-ui            |
         ├── 等待 lv_backend IPC socket
         ├── 写 /tmp/boot_progress = 100
         ├── 等待 boot-play 退出
         └── exec lvglsim
```

`aitvbox-start-ui` 是固件内置部署的协调脚本，确保 `lvglsim` 启动时 `boot-play` 已完全释放 `/dev/fb0`，避免进度条残影遮挡 UI。详细集成说明见 `AITVBOX_FIRMWARE_INTEGRATION.md`。

## 3. 后端进程管理逻辑

### 3.1 `lv_backend` 启动服务

`lv_backend` 的入口在 `lv_port_linux/src/backend_main.c`。启动后会初始化各类 service，包括：

- **`service_led` + `led_strip_hal`**：WS2812 状态灯（**最先 init**）。唯一打开 `/dev/ws2812-leds`，按优先级仲裁 WiFi/AI/OTA 等场景；UI 与其它进程不要直接写该设备。
- Wi-Fi、蓝牙、视频、传感器、设置等本地功能服务。
- `service_ai_runtime.c`：负责 Xiaozhi / Tuya 相关进程。
- `service_hdmi_preview.c`：负责 HDMI 预览进程。
- OTA / 云服务等。

后端周期性调用各 service 的 update 函数，接收子进程状态、重启异常进程，并向 UI 发布状态。

#### 状态灯（WS2812）

| 项 | 说明 |
| --- | --- |
| 源码 | `src/system/service_led.c`、`src/system/led_strip_hal.c`（仅 backend） |
| 设备 | `/dev/ws2812-leds`（内核 `leds-ws2812-spi`，10 灯，SPI2） |
| 模型 | 公共资源 + `request`/`release` + 优先级；**默认全灭**，事件短反馈 |
| 挂钩 | WiFi / AI / OTA；Dock 滑动经 `TOPIC_LED_COMMAND` |
| UI 联动 | `desktop_dock.c` 选中变化 → 冷白彗星（跟手指方向） |
| 物理方向 | 软件 index 0 在视觉右侧；进度条用 `n-1-i` 实现视觉左→右 |

完整说明见仓库文档 [`docs/led.md`](../../docs/led.md)。

### 3.2 AI 进程管理

管理代码在 `lv_port_linux/src/system/service_ai_runtime.c`。

当前固件默认使用 **Tuya AI 运行时**：

- 默认二进制：`/usr/bin/your_chat_bot_QIO_1.0.1.bin`
- 环境变量覆盖：`LV_AI_TUYA_BIN`、`TUYA_CHAT_BOT_BIN`
- 由 `service_ai_runtime.c` 在 `lv_backend` 初始化后启动和看护
- Tuya 运行时依赖 `/usr/lib/libMNN.so`、`/usr/lib/libMNN_Express.so` 以及 `/usr/share/tuyaopen_models/` 下的模型

同时保留 **Xiaozhi 运行时** 支持，通过 `LV_AI_RUNTIME=xiaozhi` 切换：

默认路径：

| 项 | 默认值 | 可通过环境变量覆盖 |
| --- | --- | --- |
| `control_center` | `/mnt/UDISK/control_center` | `LV_AI_CONTROL_CENTER_BIN` |
| `sound_app` | `/mnt/UDISK/sound_app` | `LV_AI_SOUND_APP_BIN` |
| sherpa lib | `/mnt/UDISK/sherpa-onnx-aarch64-shared-cpu/lib` | `LV_AI_SHERPA_LIB_DIR` |
| KWS model | `/mnt/UDISK/models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile` | `LV_AI_SHERPA_MODEL_DIR` |
| control log | `/tmp/control_center.log` | `LV_AI_CONTROL_CENTER_LOG` |
| sound log | `/tmp/sound_app.log` | `LV_AI_SOUND_APP_LOG` |

管理策略：

- `lv_backend` 启动时检查 `control_center` 和 `sound_app` 是否可执行。
- 如果 Wi-Fi 未连接或还没有拿到 IP，AI 状态为 `network_unavailable`，后端不会拉起 `control_center` 和 `sound_app`，避免无网络时后台反复访问云端。
- Wi-Fi 恢复并拿到 IP 后，后端自动切换到 `connecting` 并启动 Xiaozhi 进程。
- 如果可执行，后端 fork 子进程并用 `setpgid()` 建立进程组。
- 子进程设置 `PR_SET_PDEATHSIG=SIGTERM`，父进程退出时子进程自动收到终止信号。
- 子进程 stdout/stderr 重定向到 `/tmp/control_center.log` 和 `/tmp/sound_app.log`。
- `sound_app` 启动参数为 `--wake-notify`，默认进入本地唤醒通知模式。
- 后端定期 `waitpid(..., WNOHANG)` 检查进程是否退出，异常退出后按 backoff 自动重启。
- 后端通过 5678/5679 UDP 与 `control_center` 通信。
- 如果 30 秒内没有收到 `control_center` 心跳/状态，后端会把 AI 状态标记为离线或错误。

AI 控制链路：

- 如果 Wi-Fi 未连接或没有 IP，UI 显示“网络未连接”，开始对话按钮不可用。
- UI 点击开始监听。
- `lv_backend` 判断 `control_center` 是否在线且 AI 是否 idle。
- 如果蓝牙 A2DP 正在播放，`lv_backend` 暂停蓝牙音频。
- `lv_backend` 向 `control_center` 发送 `{"cmd":"start_listen"}`。
- `control_center` 通知 `sound_app` 停止播放、开始录音，并向云端发送 listen start。
- 云端 TTS 开始时，`control_center` 让 `sound_app` 停止录音、开始播放。
- TTS 播放结束后，系统回到本地唤醒状态。
- AI 回到 idle 后，如果蓝牙是被 AI 暂停的，`lv_backend` 尝试恢复蓝牙播放。

Xiaozhi 相关 UDP 端口：

| 端口 | 方向 | 用途 |
| --- | --- | --- |
| 5678 | `lv_backend` -> `control_center` | UI/后端控制命令 |
| 5679 | `control_center` -> `lv_backend` | AI 状态、文本、唤醒事件 |
| 5676 | `sound_app` -> `control_center` | Opus 音频上传和 sound event |
| 5677 | `control_center` -> `sound_app` | TTS Opus 音频下发 |
| 5680 | `control_center` -> `sound_app` | 音频通道控制命令 |

### 3.3 HDMI Preview 进程管理

管理代码在 `lv_port_linux/src/system/service_hdmi_preview.c`。

默认路径：

| 项 | 默认值 | 可通过环境变量覆盖 |
| --- | --- | --- |
| `hdmi_preview` | `/mnt/UDISK/hdmi_preview` | `HDMI_PREVIEW_BIN` |
| 状态 socket | `/tmp/hdmi_preview_status.sock` | `HDMI_PREVIEW_STATUS_SOCK` |

管理策略：

- `lv_backend` 启动时检查 `hdmi_preview` 是否可执行。
- 后端创建 Unix datagram socket：`/tmp/hdmi_preview_status.sock`。
- 后端 fork `hdmi_preview --service`。
- `hdmi_preview` 周期性检测 HDMI 信号，有信号时启动 V4L2 capture 和 videoOutPort 显示层。
- `hdmi_preview` 向 socket 上报 `LOCKED`、`RUNNING`、`NO_SIGNAL`、`ERROR`。
- 后端收到状态后发布 `TOPIC_HDMI_PREVIEW_STATUS`。
- UI 收到 active 状态后切到透明 HDMI preview screen；无信号或错误时恢复之前的 LVGL screen。
- `hdmi_preview` 异常退出时，后端记录状态并自动重启。

HDMI 状态含义：

| 状态 | 含义 | UI 行为 |
| --- | --- | --- |
| `LOCKED` | 已检测到 HDMI 信号，准备显示 | 切换到 preview screen |
| `RUNNING` | 已收到首帧并启用显示层 | 保持 preview screen |
| `NO_SIGNAL` | 无信号或信号丢失 | 回到之前 UI |
| `ERROR` | 设备、buffer、显示层初始化失败 | 回到之前 UI |

## 4. 编译方式

以下命令均在 SDK 根目录 `/home/ubuntu/A133-Tina5.0-v0.9` 下执行。

### 4.1 编译 `lv_port_linux`

推荐直接通过 cmake 交叉编译：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9/lv_port_linux
export STAGING_DIR=/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir/target
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=.trae/skills/cross-build/toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWERROR=ON
cmake --build build -j$(nproc)
```

输出示例：

```text
lv_port_linux/build/bin/lv_backend
lv_port_linux/build/bin/lvglsim
```

注意：

- 必须设置 `STAGING_DIR`，否则交叉工具链可能找不到 OpenWrt sysroot。
- 产物以 `build/bin/lv_backend` 和 `build/bin/lvglsim` 为准。
- 如果项目内仍有 `build_openwrt.sh`，它只是对上述命令的包装；源码修改后建议直接执行 cmake 命令，确保增量编译和源码变更被正确感知。

### 4.2 编译 `hdmi_preview`

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
make -C hdmi_preview -j$(nproc)
```

输出：

```text
hdmi_preview/build/hdmi_preview
```

安装到目标根文件系统或 U 盘部署目录时：

```sh
make -C hdmi_preview install DESTDIR=/path/to/rootfs
```

运行方式：

```sh
/mnt/UDISK/hdmi_preview --service
```

实际产品中不需要手动启动，`lv_backend` 会自动拉起。

### 4.3 编译 `xiaozhi/control_center`

推荐 CMake OpenWrt 构建：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
cmake -S xiaozhi/control_center \
  -B xiaozhi/control_center/build-openwrt-auto \
  -DCMAKE_TOOLCHAIN_FILE=/home/ubuntu/A133-Tina5.0-v0.9/xiaozhi/toolchain.cmake \
  -DA133_TARGET=openwrt

cmake --build xiaozhi/control_center/build-openwrt-auto -- -j$(nproc)
```

输出：

```text
xiaozhi/control_center/build-openwrt-auto/control_center
```

也可以使用 Makefile：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
make -C xiaozhi/control_center \
  CROSS_COMPILE=/home/ubuntu/A133-Tina5.0-v0.9/prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-830/toolchain/bin/aarch64-openwrt-linux- \
  STAGING_DIR=/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir \
  -j$(nproc)
```

Makefile 输出：

```text
xiaozhi/control_center/control_center
```

### 4.4 编译 `xiaozhi/sound_app`

`sound_app` 支持两种模式：

- 不启用 sherpa-onnx：只支持原有录音/播放链路，不支持本地唤醒。
- 启用 sherpa-onnx：正式产品推荐模式，支持本地 KWS 唤醒。

推荐 CMake OpenWrt 构建并启用 sherpa-onnx：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
cmake -S xiaozhi/sound_app \
  -B xiaozhi/sound_app/build-openwrt-auto \
  -DCMAKE_TOOLCHAIN_FILE=/home/ubuntu/A133-Tina5.0-v0.9/xiaozhi/toolchain.cmake \
  -DA133_TARGET=openwrt \
  -DENABLE_SHERPA_ONNX=ON \
  -DSHERPA_ONNX_DIR=/home/ubuntu/A133-Tina5.0-v0.9/xiaozhi/sherpa_onnx_kws/sherpa-onnx-aarch64-shared-cpu

cmake --build xiaozhi/sound_app/build-openwrt-auto -- -j$(nproc)
```

输出：

```text
xiaozhi/sound_app/build-openwrt-auto/sound_app
```

也可以使用 Makefile：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
make -C xiaozhi/sound_app \
  CROSS_COMPILE=/home/ubuntu/A133-Tina5.0-v0.9/prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-830/toolchain/bin/aarch64-openwrt-linux- \
  STAGING_DIR=/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir \
  SHERPA_ONNX_DIR=/home/ubuntu/A133-Tina5.0-v0.9/xiaozhi/sherpa_onnx_kws/sherpa-onnx-aarch64-shared-cpu \
  -j$(nproc)
```

Makefile 输出：

```text
xiaozhi/sound_app/sound_app
```

板端运行前，需要保证 sherpa 动态库和模型路径存在：

```sh
/mnt/UDISK/sherpa-onnx-aarch64-shared-cpu/lib/libsherpa-onnx-c-api.so
/mnt/UDISK/sherpa-onnx-aarch64-shared-cpu/lib/libonnxruntime.so
/mnt/UDISK/models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/
```

`lv_backend` 拉起 `sound_app` 时会设置：

```sh
LD_LIBRARY_PATH=/mnt/UDISK/sherpa-onnx-aarch64-shared-cpu/lib:$LD_LIBRARY_PATH
SHERPA_ONNX_KWS_MODEL_DIR=/mnt/UDISK/models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile
```

### 4.5 sherpa-onnx KWS demo

该目录用于板端验证，不是正式产品常驻进程。

准备 sherpa 包：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9/sherpa_onnx_kws_demo
SHERPA_ONNX_VERSION=1.12.21 ./scripts/fetch_sherpa_aarch64.sh
```

准备 KWS 模型：

```sh
./scripts/fetch_kws_model.sh
```

板端验证：

```sh
cd /path/to/sherpa_onnx_kws_demo
ALSA_DEVICE=plughw:0,0 ./scripts/run_board_kws_alsa.sh
```

如果 `sound_app` 已在运行，脚本会拒绝启动，避免抢占 ALSA capture。仅在底层调试时可以显式覆盖：

```sh
ALLOW_ALSA_CAPTURE_CONFLICT=1 ALSA_DEVICE=plughw:0,0 ./scripts/run_board_kws_alsa.sh
```

## 5. 部署建议

### 5.1 固件内置部署（当前产品形态）

当前 Tina 固件已将应用打包进 rootfs，固件启动后由 `/etc/init.d/aitvbox` 自启动：

```text
/usr/bin/lv_backend
/usr/bin/lvglsim
/usr/bin/aitvbox-start-ui
/usr/bin/your_chat_bot_QIO_1.0.1.bin
/usr/bin/hdmi_preview
/usr/lib/libMNN.so
/usr/lib/libMNN_Express.so
/usr/share/tuyaopen_models/
/usr/share/fonts/
```

详细集成、构建、更新流程见 `AITVBOX_FIRMWARE_INTEGRATION.md`。

### 5.2 U 盘/开发部署

开发调试时也可以部署到 `/mnt/UDISK`：

```text
/mnt/UDISK/lv_backend
/mnt/UDISK/lvglsim
/mnt/UDISK/control_center
/mnt/UDISK/sound_app
/mnt/UDISK/hdmi_preview
/mnt/UDISK/sherpa-onnx-aarch64-shared-cpu/lib/
/mnt/UDISK/models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/
```

如果二进制部署路径不同，需要通过环境变量覆盖：

```sh
export LV_AI_CONTROL_CENTER_BIN=/custom/path/control_center
export LV_AI_SOUND_APP_BIN=/custom/path/sound_app
export LV_AI_SHERPA_LIB_DIR=/custom/path/sherpa-onnx-aarch64-shared-cpu/lib
export LV_AI_SHERPA_MODEL_DIR=/custom/path/kws-model
export HDMI_PREVIEW_BIN=/custom/path/hdmi_preview
```

U 盘/开发模式下推荐只手动启动：

```sh
lv_backend &
lvglsim &
```

`control_center`、`sound_app`、`hdmi_preview` 由 `lv_backend` 自动管理，不建议在 init 脚本中重复拉起。

固件内置模式下，`lv_backend` 和 `lvglsim` 由 `/etc/init.d/aitvbox` 通过 procd 拉起，不需要手动启动。

## 6. 日志和长期运行检查

默认日志：

```text
/tmp/lv_backend.log          # 固件内置模式主后端日志
/tmp/lvglsim.log             # 固件内置模式 UI 日志
/tmp/control_center.log
/tmp/sound_app.log
/tmp/hdmi_preview.log
```

长期运行建议：

- init 脚本或 logrotate 定期截断 `/tmp/*.log`，避免 tmpfs 被日志占满。
- 无 Wi-Fi 启动时，AI 应保持“网络未连接”，且 `control_center` / `sound_app` 不应被反复拉起；连接 Wi-Fi 并获取 IP 后应自动进入连接服务流程。
- HDMI 插拔压力测试至少覆盖：无信号启动、有信号启动、运行中拔线、运行中换分辨率、反复插拔。
- AI 语音压力测试至少覆盖：唤醒取消、唤醒后说话、TTS 播放中再次唤醒、网络断开重连、蓝牙 A2DP 播放时唤醒。
- 不要同时运行独立 `sherpa_onnx_kws_demo` 和正式 `sound_app`。
- 如启用严格 TLS，板端需要有可用 CA 证书。`control_center` 会尝试加载 `/etc/ssl/certs/ca-certificates.crt`、`/etc/ssl/cert.pem`、`/etc/pki/tls/certs/ca-bundle.crt`、`/mnt/UDISK/ca-certificates.crt`；临时调试才使用 `XIAOZHI_TLS_INSECURE=1`。
- WebSocket 鉴权 token 可通过 `XIAOZHI_WS_TOKEN` 配置；日志中不得打印 `Authorization`、token、密码等敏感值。

## 7. 快速验证命令

主机侧确认产物架构：

```sh
file lv_port_linux/build/bin/lv_backend \
     lv_port_linux/build/bin/lvglsim \
     xiaozhi/control_center/build-openwrt-auto/control_center \
     xiaozhi/sound_app/build-openwrt-auto/sound_app \
     hdmi_preview/build/hdmi_preview
```

期望结果均为：

```text
ELF 64-bit LSB executable, ARM aarch64
```

脚本语法检查：

```sh
sh -n sherpa_onnx_kws_demo/scripts/run_board_kws_alsa.sh
```

代码空白检查：

```sh
git -C lv_port_linux diff --check
```
