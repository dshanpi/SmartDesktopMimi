# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在此代码库中工作时提供指导。

## 项目概述

这是 LVGL（轻量级通用图形库）在 Linux 上的移植项目，采用**双进程架构**，专为嵌入式系统（全志 A133）设计。

## 架构

### 双进程模型

1. **UI 进程（`lvglsim`）**：运行 LVGL 主循环，处理显示和用户输入，作为 IPC 客户端。
   - 入口：`src/main.c`
   - 构建源：`src/main.c` + `src/ui/` + `src/apps/` + `src/middleware/` + `src/ipc/`

2. **Backend 进程（`lv_backend`）**：独立进程，处理业务逻辑（Wi-Fi、音乐、视频），作为 IPC 服务端。
   - 入口：`src/backend_main.c`
   - 构建源：`src/backend_main.c` + `src/middleware/` + `src/system/` + `src/ipc/`

### IPC 通信

- **协议**：Unix Domain Socket（SOCK_STREAM），路径 `/tmp/lv_port_linux_backend.sock`。应用层定长 header（16B：magic/topic/payload_len/flags）+ 变长 payload 分帧，`recv` 循环读满以抗粘包/半包；magic 校验做协议同步。
- **中间件**：`src/middleware/middleware.h` — 发布/订阅消息代理
- **主题**：`TOPIC_MUSIC_*`、`TOPIC_WIFI_*`、`TOPIC_VIDEO_*`、`TOPIC_SYSTEM_STATUS`
- **底层 IPC**：`src/ipc/ipc_socket.h` — 处理连接、发送/接收、心跳

### 关键目录

- `src/lib/display_backends/` — 显示驱动（fbdev、DRM、X11、Wayland、GLFW、SDL、SunxiFB）
- `src/lib/indev_backends/` — 输入驱动（evdev、libinput）
- `src/apps/` — 应用程序（wifi、music、video、setting）
- `src/ui/` — 界面、图片、字体、主题
- `src/system/` — 系统服务（wifi、music、video）和应用管理器
- `src/middleware/` — UI 与 Backend 之间的消息中间件
- `src/ipc/` — Socket 级 IPC 实现

### 关键配置

- `LV_USE_SUNXIFB 1`（默认）— 全志 SunxiFB 显示驱动
- `LV_USE_EVDEV 1`（默认）— 输入设备支持
- `lv_conf.defaults` — 默认配置，预设配置在 `configs/*.defaults`

## 构建命令

**本地构建：**
```bash
cmake -B build
cmake --build build -j$(nproc)
```

**A133 (ARM64) 交叉编译：**
```bash
export STAGING_DIR=/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir/target
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=.trae/skills/cross-build/toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWERROR=ON
cmake --build build -j$(nproc)
```

**运行（先启动 backend，再启动 UI）：**
```bash
./build/bin/lv_backend &
./build/bin/lvglsim
```

**选择显示后端：**
```bash
./build/bin/lvglsim -b sdl      # SDL2
./build/bin/lvglsim -b wayland  # Wayland
./build/bin/lvglsim -B          # 列出所有支持的后端
```

## 交叉编译

项目使用 `.trae/skills/cross-build/toolchain.cmake` 工具链文件面向 ARM64（全志 A133）。`user_cross_compile_setup.cmake` 用于通用 ARM 交叉编译。部署前务必验证输出二进制为 ARM aarch64 架构。

## 代码模式

- Backend 通过 `mw_backend_register_handler(TOPIC_XXX, callback)` 按主题注册处理器
- UI 通过 `mw_subscribe(TOPIC_XXX, callback)` 在 LVGL 线程上下文中订阅主题
- 消息通过 `mw_process_ui_messages()` 定时器回调处理（每 10ms）
- Backend 在主循环中运行 `mw_process()` 处理传入消息
