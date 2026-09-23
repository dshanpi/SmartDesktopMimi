# HDMI Preview

`hdmi_preview` 是 AI-DeskTopBox 的 HDMI 输入预览模块，负责从板端 HDMI IN 采集视频帧，并通过 Allwinner `videoOutPort` 直接显示到屏幕上。

当前这套内容的上游维护位置，也不是一个独立 Git 仓库，而是 `lv_port_linux` 主仓库中的一个分支：

- 仓库：`git@github.com:100askTeam/AI-DeskTopBox.git`
- 分支：`feature/hdmi-preview`

这个目录是该分支内容在 SDK 中的落地副本，方便直接和 `lv_backend`、`lvglsim`、`xiaozhi` 一起集成调试。

## 概述

这个模块主要完成下面几件事：

- 打开 HDMI 输入对应的视频设备
- 检查 HDMI 信号是否有效
- 申请 V4L2 MMAP buffer，持续采集视频帧
- 把采集到的 NV12 数据拷贝到 Allwinner 显示缓冲区
- 使用 `videoOutPort` 打开硬件显示层，把 HDMI 画面直接显示出来
- 在服务模式下通过 Unix datagram socket 向后端上报状态

正式产品里，这个程序通常由 `lv_backend` 自动拉起，而不是手工长期运行。

## 目录结构

如果你在当前 SDK 快照目录中查看，本目录结构如下：

```text
hdmi_preview/
├── Makefile
├── build/
├── src/
│   ├── main.cpp
│   └── Makefile
└── README.md
```

说明：

- 顶层 [Makefile](/home/ubuntu/A133-Tina5.0-v0.9/hdmi_preview/Makefile) 是实际编译入口。
- [src/main.cpp](/home/ubuntu/A133-Tina5.0-v0.9/hdmi_preview/src/main.cpp) 是主程序源码。
- `src/Makefile` 只是转发到上一级 Makefile。
- 编译产物默认输出到 `build/hdmi_preview`。

## 拉取步骤

### 首次拉取 `feature/hdmi-preview`

```sh
git clone git@github.com:100askTeam/AI-DeskTopBox.git lv_port_linux
cd lv_port_linux
git fetch origin
git switch -c feature/hdmi-preview --track origin/feature/hdmi-preview
```

### 更新已有分支

```sh
cd lv_port_linux
git fetch origin
git switch feature/hdmi-preview
git pull --ff-only origin feature/hdmi-preview
```

如果你当前就是在 SDK 里的 `hdmi_preview/` 副本上工作，那么上面的命令主要用于同步上游分支；真正集成时仍然以 SDK 目录内的内容为准。

## 运行模式

程序支持两种启动方式：

### `--probe`

只检测 HDMI 信号，不进入持续采集。

```sh
./build/hdmi_preview --probe
```

返回值约定：

- `0`：检测到有效 HDMI 信号
- `1`：没有信号
- `2`：检测或初始化出错

### `--service`

持续运行，适合由产品后端守护。

```sh
./build/hdmi_preview --service
```

在这个模式下，程序会循环检测和拉起采集；如果 HDMI 信号丢失，会停止当前预览并等待重试。

## 状态上报

服务模式下，程序会通过 Unix datagram socket 上报状态，默认 socket 路径是：

```text
/tmp/hdmi_preview_status.sock
```

也可以通过环境变量覆盖：

```sh
export HDMI_PREVIEW_STATUS_SOCK=/tmp/your_status.sock
```

常见状态包括：

- `LOCKED`：已经锁定 HDMI 信号
- `RUNNING`：已经收到首帧并启用显示层
- `NO_SIGNAL`：当前没有 HDMI 信号，或者信号丢失
- `ERROR ...`：初始化或采集过程出错

在整机里，`lv_backend` 会监听这个状态并决定是否切换到 HDMI 预览界面。

## 单路截图

服务进程独占 HDMI V4L2 采集节点，并在
`/var/run/aitvbox/capture.sock` 提供快照接口，图片原子写入
`/var/run/aitvbox/screen.jpg`。其他模块不得再次打开
`/dev/video0`：

```sh
hdmi_preview --snapshot
# OK /var/run/aitvbox/screen.jpg <bytes>
```

命令等待服务采集的下一帧，将 NV12 缩放为 960x540 JPEG 后原子写入
`/var/run/aitvbox/screen.jpg`。无信号、服务未启动或并发请求会返回非零状态。

## 编译前提

这个项目依赖 A133 Tina 5.0 OpenWrt SDK 里的交叉编译环境和目标 sysroot。

编译前通常需要保证下面这些路径已经存在：

```text
/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir/target
/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/tmp/.config
```

如果这些路径还没准备好，需要先完成一次对应的 SDK/OpenWrt 构建。

## 编译步骤

### 方式 1：在 `lv_port_linux` 分支工作树中编译

这适合你直接在 `feature/hdmi-preview` 分支里开发和验证。

注意：这个分支本身不是 Tina SDK 根目录，所以编译时要显式传入 `SDK_ROOT`。

```sh
cd /path/to/lv_port_linux
git switch feature/hdmi-preview
make SDK_ROOT=/home/ubuntu/A133-Tina5.0-v0.9 -j$(nproc)
```

输出产物：

```text
build/hdmi_preview
```

### 方式 2：在当前 SDK 快照目录中编译

这适合你现在这台机器上的直接集成方式。

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9/hdmi_preview
make -j$(nproc)
```

或者：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
make -C hdmi_preview -j$(nproc)
```

输出产物：

```text
hdmi_preview/build/hdmi_preview
```

## 查看当前编译配置

如果想确认当前 Makefile 使用了哪个 SDK 根目录、`STAGING_DIR`、工具链或 OpenWrt 配置，可以执行：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9/hdmi_preview
make print-config
```

如果你是在分支工作树内构建，也可以这样看：

```sh
cd /path/to/lv_port_linux
make SDK_ROOT=/home/ubuntu/A133-Tina5.0-v0.9 print-config
```

## ADB 推送步骤

正式产品默认从 `/mnt/UDISK/hdmi_preview` 启动这个程序。

### 从 `feature/hdmi-preview` 分支工作树推送

```sh
cd /path/to/lv_port_linux
adb push build/hdmi_preview /mnt/UDISK/hdmi_preview
adb shell chmod 755 /mnt/UDISK/hdmi_preview
```

### 从当前 SDK 快照目录推送

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
adb push hdmi_preview/build/hdmi_preview /mnt/UDISK/hdmi_preview
adb shell chmod 755 /mnt/UDISK/hdmi_preview
```

推送完成后，可用下面两种方式简单验证：

```sh
adb shell /mnt/UDISK/hdmi_preview --probe
adb shell /mnt/UDISK/hdmi_preview --service
```

## 清理构建产物

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9/hdmi_preview
make clean
```

## 与整机的关系

在当前产品里，这个程序通常不会手工长期运行，而是由 `lv_backend` 统一拉起：

- 默认部署路径一般是 `/mnt/UDISK/hdmi_preview`
- 后端以 `--service` 模式启动它
- 后端通过 `/tmp/hdmi_preview_status.sock` 接收状态
- UI 根据后端状态切换到 HDMI 预览页或返回桌面

## 常见问题

`1. 编译时报找不到头文件或库`

优先检查 `staging_dir/target/usr/include` 和 `staging_dir/target/usr/lib` 是否已经生成并包含 Allwinner 相关头文件与库。

`2. 分支工作树里编译失败`

最常见原因是没有显式传 `SDK_ROOT=/home/ubuntu/A133-Tina5.0-v0.9`，导致 Makefile 把分支目录的上一级误当成 SDK 根目录。

`3. 程序能启动但没有画面`

常见原因包括：

- 当前没有有效 HDMI 输入信号
- HDMI 输入格式不受当前驱动支持
- V4L2 buffer 初始化失败
- `videoOutPort` 显示层初始化失败

这类问题通常需要结合程序日志里的 `NO_SIGNAL`、`ERROR display_init`、`ERROR reqbufs`、`ERROR streamon` 等状态一起看。

`4. 服务模式一直反复重试`

这通常说明程序能启动，但持续拿不到稳定信号，或者采集线程进入 watchdog 超时后自动退出。优先检查 HDMI 源是否稳定、分辨率是否兼容、驱动是否正常锁定输入时序。

## 相关文档

更完整的产品级进程管理和编译说明可参考：

- [README.md](https://github.com/100askTeam/AI-DeskTopBox/blob/master/README.md)
