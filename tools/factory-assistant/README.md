# AITVBOX 出厂助手

Tauri 2 + Rust + React/TypeScript 的跨平台生产工具。工人界面只展示设备状态和 PASS/FAIL；ADB、量产协议与密钥处理位于 Rust/受控执行器一侧。

工程中的 `factory-core` 是不依赖 Tauri、GTK 或 WebView 的纯 Rust 核心库，当前 Ubuntu 20.04 也可以直接编译和测试。

## 当前能力

- 检查 ADB 是否可用并扫描设备。
- 只在恰好连接一台授权设备时读取 CPUID、固件版本和涂鸦 PID。
- 检查量产工位环境变量是否完整，但不向前端返回密钥内容。
- 在 Linux 开发环境中调用仓库的 `scripts/factory_provision_device.sh` 协议参考实现。
- 将凭据验证和最终整机 PASS 明确区分。
- 提供不会写设备的演示流程，方便检查完整 UI 状态。
- 限制 Tauri capability，前端没有通用 shell 权限。
- 提供独立于云端凭据的本地硬件检测流程，并在界面显示每项状态、实测值、耗时和失败原因。
- 自动检测 CPUID/固件、eMMC、内存、Wi-Fi 接口与 RF 扫描、蓝牙、AHT20、INA219、ALSA 与触控控制器。
- 屏幕显示、全屏触摸、扬声器和麦克风音质采用人工确认，避免仅凭驱动节点误判整机通过；扬声器项播放双音测试音，麦克风项录制 3 秒、计算真实 RMS/峰值并原音回放。

当前 Linux 执行器仍是过渡适配层。后续将云端协议和凭据写入逐段迁移到 Rust 核心，Windows 版本不依赖 Bash。

## Ubuntu 开发

```bash
cd tools/factory-assistant
npm install
npm run dev
```

`npm run dev` 会启动仅挂载在本机 Vite 开发服务器上的受限 ADB 桥。浏览器界面的设备扫描、硬件检测和测试音均调用 `factory-core`，读取真实设备，不生成模拟硬件结果。该桥只开放固定动作，不接受任意 shell 命令。

正式桌面应用使用 Tauri：

```bash
source "$HOME/.cargo/env"
npm run tauri dev
```

Tauri 2 在 Linux 使用 WebKitGTK 4.1。建议在 Ubuntu 22.04 或 Debian 12 构建。当前项目开发机是 Ubuntu 20.04，只适合前端预览和 Rust非图形核心开发，不应混装新发行版的 GTK 包。

Ubuntu 22.04 构建依赖：

```bash
sudo apt update
sudo apt install libwebkit2gtk-4.1-dev build-essential curl wget file \
  libxdo-dev libssl-dev libayatana-appindicator3-dev librsvg2-dev adb
```

## 工位配置

复制 `.env.example` 的字段到工位启动环境。正式密钥不得放入仓库或普通 `.env` 文件，应该由工位管理员安装并设置只读权限。

启动真实流程前必须满足：

- 恰好一台状态为 `device` 的 ADB 设备。
- 所有必需工位配置通过格式和文件存在性检查。
- 设备身份与当前扫描结果一致。
- 量产协议执行器存在。

本地硬件检测不要求云端配置，只要求 ADB 可用、恰好连接一台授权设备且身份读取成功。默认 BOM 为 16 GB eMMC + 2 GB RAM，可通过 `HW_EXPECTED_STORAGE_GB` 和 `HW_EXPECTED_MEMORY_MIB` 调整。eMMC 按十进制标称容量校验并显示 GiB 实测值，内存显示 Linux 系统可见容量。

麦克风产测与涂鸦使用完全相同的 ALSA 参数：`default`、PCM `S16_LE`、16 kHz、单声道、16-bit。量产固件必须将 `pcm.!default.capture.pcm` 指向 `CaptureDsnoop`，从而让涂鸦唤醒采集与出厂录音共享真实 `hw:audiocodec,0`，测试期间不停止 UI 或语音服务。默认有效信号阈值为 RMS `-50 dBFS` 且峰值 `-35 dBFS`，可通过 `HW_MIC_MIN_RMS_DBFS`、`HW_MIC_MIN_PEAK_DBFS` 按声学金样标定。

## 构建

前端生产构建：

```bash
npm run build
```

纯 Rust 核心测试：

```bash
source "$HOME/.cargo/env"
cargo test --manifest-path factory-core/Cargo.toml
```

Linux包需要在目标基线系统上构建：

```bash
npm run tauri build
```

Windows 正式包使用 NSIS `-setup.exe`。开源源码包不内置未分类的 Google
Platform Tools 二进制；工位需要安装官方 `adb` 并加入 PATH，或显式设置
`AITVBOX_ADB_PATH`。当前 Ubuntu 开发机可以通过 Tauri 官方的
`cargo-xwin` 方案生成 Windows x64 包；量产发布仍建议在 Windows CI 中复现构建并执行代码签名。

当前 Windows 测试安装包位于：

```text
release/windows/AITVBOX-Factory-Assistant-0.1.0-x64-setup.exe
```

Windows 工位仍需安装能够识别 A133 设备的 USB/ADB 驱动。当前云端 provision 执行器依赖 Linux Bash 脚本，Windows 版本现阶段只承诺本地硬件检测流程。
