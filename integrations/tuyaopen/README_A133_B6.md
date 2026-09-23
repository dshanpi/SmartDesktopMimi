# A133 Tina SDK 使用说明

本文档记录 `your_chat_bot` demo 在 Allwinner A133 Tina 5.0 SDK 上的编译、部署和运行流程。

## 当前适配内容

- 目标平台：`A133_B6`
- 目标板卡配置：复用 `DshanPi_A1`
- 交叉工具链：从 `openwrt/openwrt/.config` 自动读取 Tina SDK 当前工具链
- 音频设备：
  - 录音：`default`
  - 播放：`default`
- 默认对话模式：语音唤醒模式 `AI_CHAT_MODE_WAKEUP`
- 当前唤醒词：`你好涂鸦`
- 支持两种部署方式：
  - **U 盘/开发部署**：二进制和模型放在 `/mnt/UDISK/`，适合调试
  - **固件内置部署**：二进制和模型打包进 rootfs，路径为 `/usr/bin/`、`/usr/lib/`、`/usr/share/`，由 `lv_backend` 自启动

当前 Tina 固件内置版使用以下路径：

```text
/usr/bin/your_chat_bot_QIO_1.0.1.bin
/usr/lib/libMNN.so
/usr/lib/libMNN_Express.so
/usr/share/tuyaopen_models/mdtc_chunk_300ms.mnn
/usr/share/tuyaopen_models/tokens.txt
```

开发部署（`/mnt/UDISK/`）时的模型路径：

```text
/mnt/UDISK/tuyaopen_models/mdtc_chunk_300ms.mnn
/mnt/UDISK/tuyaopen_models/tokens.txt
```

MNN 运行库路径：

```text
/mnt/UDISK/lib/libMNN.so
```

程序已设置 rpath，可从程序同级 `lib/` 目录加载动态库。

## 1. 准备 Tina SDK

先确保 Tina SDK 已经编译过 rootfs，并且当前 OpenWrt 配置里已经切到 A133 使用的 gcc-11.3 工具链。

在 SDK 根目录执行：

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
./build.sh
./build.sh pack
```

如果只是编译 `your_chat_bot`，通常不需要每次都重新执行 Tina 全量编译。只有更换 OpenWrt 工具链、glibc 或 rootfs 基础库后，才需要重新清理并编译 rootfs。

更换工具链后推荐用 Tina wrapper 清理，不要直接在 `openwrt/openwrt` 目录执行 `make dirclean`：

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
./build.sh openwrt_rootfs dirclean
./build.sh
./build.sh pack
```

## 2. 配置 Tuya 产品信息

`tuya_config.h` 只配置产品 PID。UUID/AuthKey 不再写入头文件或二进制。正式量产由
`scripts/factory_provision_device.sh` 通过量产云将 CPUID 与唯一 License 预绑定后，为每台
设备写入 `/factory/tuya/license.env`。`provision_tuya_license.sh` 的文件模式仅用于开发联调。

```bash
TuyaOpen/apps/tuya.ai/your_chat_bot/include/tuya_config.h
```

再确认产品 PID：

```bash
TuyaOpen/apps/tuya.ai/your_chat_bot/config/A133_B6.config
```

```text
CONFIG_TUYA_PRODUCT_ID="你的PID"
```

注意：UUID、AuthKey 属于一机一密，不要提交仓库或编进通用固件。

## 3. 编译 demo

进入 demo 目录：

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9/TuyaOpen/apps/tuya.ai/your_chat_bot
./build_a133.sh
```

`build_a133.sh` 会自动：

- 从 `openwrt/openwrt/.config` 读取 `CONFIG_TOOLCHAIN_ROOT`
- 导出 `CC/CXX/AR/RANLIB/STRIP`
- 导出 `STAGING_DIR`
- 选择 `config/A133_B6.config`
- 调用 `tos.py build`

编译成功后输出文件在：

```text
TuyaOpen/apps/tuya.ai/your_chat_bot/dist/your_chat_bot_1.0.1/your_chat_bot_QIO_1.0.1.bin
```

成功日志里应能看到：

```text
BUILD SUCCESS
Target    : your_chat_bot_QIO_1.0.1.bin
Chip      : A133_B6
```

## 4. U 盘/开发部署

确认板子 adb 在线：

```bash
adb devices
```

推送程序：

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
adb push TuyaOpen/apps/tuya.ai/your_chat_bot/dist/your_chat_bot_1.0.1/your_chat_bot_QIO_1.0.1.bin /mnt/UDISK/
adb shell chmod +x /mnt/UDISK/your_chat_bot_QIO_1.0.1.bin
```

推送 MNN 动态库：

```bash
adb shell mkdir -p /mnt/UDISK/lib
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN.so /mnt/UDISK/lib/
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN_Express.so /mnt/UDISK/lib/
```

推送唤醒模型：

```bash
adb shell mkdir -p /mnt/UDISK/tuyaopen_models
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/models/mdtc_chunk_300ms.mnn /mnt/UDISK/tuyaopen_models/
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/models/tokens.txt /mnt/UDISK/tuyaopen_models/
```

检查文件：

```bash
adb shell ls -l \
  /mnt/UDISK/your_chat_bot_QIO_1.0.1.bin \
  /mnt/UDISK/lib/libMNN.so \
  /mnt/UDISK/tuyaopen_models/mdtc_chunk_300ms.mnn \
  /mnt/UDISK/tuyaopen_models/tokens.txt
```

## 5. 固件内置部署

当前 Tina 固件已将 `your_chat_bot` 打包进 rootfs，启动后由 `lv_backend` 自动拉起，不需要手动 adb 推送。

固件内置路径：

```text
/usr/bin/your_chat_bot_QIO_1.0.1.bin
/usr/lib/libMNN.so
/usr/lib/libMNN_Express.so
/usr/share/tuyaopen_models/mdtc_chunk_300ms.mnn
/usr/share/tuyaopen_models/tokens.txt
```

构建和打包流程：

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9/TuyaOpen/apps/tuya.ai/your_chat_bot
./build_a133.sh

cd /home/ubuntu/A133-Tina5.0-v0.9
source build/envsetup.sh
./build.sh openwrt_rootfs
./build.sh pack
```

详细集成说明（包括 `aitvbox-suite` 包、`lv_backend` 启动参数、环境变量等）见：

```text
/home/ubuntu/A133-Tina5.0-v0.9/AITVBOX_FIRMWARE_INTEGRATION.md
```

## 6. 运行

### 6.1 U 盘/开发模式

```bash
cd /mnt/UDISK
./your_chat_bot_QIO_1.0.1.bin
```

也可以通过 adb 运行：

```bash
adb shell 'cd /mnt/UDISK && ./your_chat_bot_QIO_1.0.1.bin'
```

### 6.2 固件内置模式

固件启动后 `lv_backend` 会自动拉起 `your_chat_bot_QIO_1.0.1.bin`，无需手动执行。可以通过以下命令确认进程在运行：

```bash
ps | grep your_chat_bot
```

查看日志：

```bash
tail -f /tmp/lv_backend.log
```

正常启动时应看到类似日志：

```text
Platform chip:       A133_B6
Capture device: default
Playback device: default
ALSA audio device opened successfully
KWS feed callback registered
ai chat mode init mode 2 success
mqtt client connected!
MQTT direct connected!
Wait Tuya APP scan the Device QR code...
```

看到 `Wait Tuya APP scan the Device QR code...` 后，用手机「智能生活」App（应用商店下载，不是「涂鸦智能」）扫设备二维码进行绑定。

二维码 URL 在程序日志里由 `tuya_main.c` 生成，格式类似：

```text
https://smartapp.tuya.com/s/p?p=<PID>&uuid=<UUID>&v=2.0
```

## 7. 常见问题

### 缺少 libMNN.so

报错：

```text
error while loading shared libraries: libMNN.so: cannot open shared object file
```

U 盘/开发模式处理：

```bash
adb shell mkdir -p /mnt/UDISK/lib
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN.so /mnt/UDISK/lib/
```

当前程序已设置 rpath：`$ORIGIN/lib:$ORIGIN`，所以程序放在 `/mnt/UDISK/` 时，会自动查找 `/mnt/UDISK/lib/libMNN.so`。

固件内置模式应检查：

```bash
ls -l /usr/lib/libMNN.so /usr/lib/libMNN_Express.so
```

如果缺失，确认 `aitvbox-suite` 包已正确安装，或重新构建 rootfs。

### 运行时 Segmentation fault

如果崩在 KWS 初始化附近，不要用其他 Linux 平台的静态 `libMNN.a` 替换 A133 的 MNN 库。

A133 当前使用：

```text
TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN.so
```

其他平台的 `libMNN.a` 可能能链接成功，但运行时和 A133 的 audio_subsys/KWS 不兼容。

### MQTT Connection not authorized

报错：

```text
mqtt connect err: Connection not authorized(11)
```

通常是 UUID/AuthKey/PID 不匹配或仍是占位值。检查：

```bash
TuyaOpen/apps/tuya.ai/your_chat_bot/include/tuya_config.h
TuyaOpen/apps/tuya.ai/your_chat_bot/config/A133_B6.config
```

正常授权后应看到：

```text
mqtt client connected!
MQTT direct connected!
```

### 语音唤醒不了

当前 A133 适配默认使用语音唤醒模式，启动时应看到：

```text
ai chat mode init mode 2 success
mode wakeup state change form INIT to IDLE
```

历史版本可能保存过 `chat_mode=0`，表示按住说话模式。新版会自动迁移：

```text
migrate chat mode from HOLD(0) to WAKEUP(2)
save chat mode config: {"volume": 70, "chat_mode":2}
```

当前唤醒词是 `你好涂鸦`，不是 `你好小智`。识别成功时会打印：

```text
KWS wakeup detected: 0
```

如果没有这行日志，优先检查麦克风输入、录音音量、环境噪声，以及 `KWS feed callback registered` 是否出现。

### Authorization read failure

首次运行或设备未绑定时可能看到：

```text
Authorization read failure.
```

这表示本地 KV 里还没有已保存的授权/激活信息。只要后面能看到 `mqtt client connected!`，就不是致命问题。

### Cannot find mixer element: Master

日志：

```text
Cannot find mixer element: Master
Mixer not available, volume setting stored but not applied
```

这是 ALSA mixer 控件名不是 `Master`，会影响程序调音量，但不代表录音/播放设备打不开。当前录音和播放均使用 `default`。

### tkl_uart_init(port 0) failed

这是 demo CLI 串口初始化失败，一般不影响语音和云端连接主流程。

### 串口看不到二维码

A133 适配已将二维码输出改为 `stdout`，并额外打印绑定 URL：

```text
Tuya APP bind URL: https://smartapp.tuya.com/s/p?p=<PID>&uuid=<UUID>&v=2.0
```

如果串口终端仍然看不到二维码块，通常是终端不支持 UTF-8 方块字符，或字体/行距显示不完整。可以直接复制 `Tuya APP bind URL`，用任意二维码工具生成二维码后再用「智能生活」App 扫描。

### button no existence

当前 A133 适配没有实际按键节点，日志可能出现：

```text
button no existence
```

这不是启动失败原因。如需按键唤醒，需要后续补齐按键驱动或关闭相关按键注册逻辑。

## 8. 快速命令汇总

### 8.1 编译

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9/TuyaOpen/apps/tuya.ai/your_chat_bot
./build_a133.sh
```

### 8.2 U 盘/开发部署

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
adb push TuyaOpen/apps/tuya.ai/your_chat_bot/dist/your_chat_bot_1.0.1/your_chat_bot_QIO_1.0.1.bin /mnt/UDISK/
adb shell chmod +x /mnt/UDISK/your_chat_bot_QIO_1.0.1.bin

adb shell mkdir -p /mnt/UDISK/lib /mnt/UDISK/tuyaopen_models
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN.so /mnt/UDISK/lib/
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN_Express.so /mnt/UDISK/lib/
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/models/mdtc_chunk_300ms.mnn /mnt/UDISK/tuyaopen_models/
adb push TuyaOpen/platform/LINUX/tuyaos_adapter/src/tkl_audio/models/tokens.txt /mnt/UDISK/tuyaopen_models/

adb shell 'cd /mnt/UDISK && ./your_chat_bot_QIO_1.0.1.bin'
```

### 8.3 固件内置部署

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
source build/envsetup.sh
./build.sh openwrt_rootfs
./build.sh pack
```

烧录 `out/a133/b6/openwrt/a133_linux_b6_uart0.img` 后，`your_chat_bot` 会随固件自动启动。
