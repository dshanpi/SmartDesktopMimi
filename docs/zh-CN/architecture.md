# 系统架构

## 分层原则

依赖只能向内：前端和服务传输层调用应用用例；应用层依赖领域策略与 Ports；只有平台
Provider 和第三方 Integration 可以依赖厂商 SDK。UI、领域对象和通用服务不得判断 SoC，
也不得直接打开硬件设备节点。

```text
前端（LVGL/Web/CLI）
        |
应用用例 <--- IPC V1 兼容适配器
        |
领域策略和强类型事件
        |
core/ports 与 IPC V2 契约
        |
平台 Provider 与外部 Integration
```

目标部署采用混合分离模型。`aitvbox-core` 负责系统状态编排；Capture、Input、Agent、APP、
Update 因资源冲突、安全或故障隔离而使用独立进程。Wi-Fi、蓝牙、传感器和云业务可以先
作为 core 内部模块，不为“微服务化”而拆进程。

当前迁移里程碑已经提供稳定 Port ABI、平台 Provider/manifest、架构依赖门禁和增量 IPC
V2 服务，现有 V1 调用方保持可用。A133 已把 HDMI 视频收敛到单一 Capture Daemon；
本地显示、WebRTC 编码和 JPEG 快照不再分别打开 `/dev/video0`。HID 继续由独立 Input
进程独占，两个资源边界均通过公开 Socket 与上层通信。

Capture 是 HDMI 输入唯一拥有者，本地显示、JPEG、H264/WebRTC 共用同一帧修复策略。
A133 的 LT6911/VIN 最后 8 行无效，必须把 1072 行 Y 重采样到 1080、536 行 UV 重采样
到 540，禁止复制第 1071 行填底。

在 A133 上，`aitvbox-captured` 常驻采集并负责热插拔恢复。它把修复后的同一块 ION
缓冲区送给本地 Display、硬件 H264 编码器和 JPEG 快照；没有 WebRTC 订阅者时停止编码，
但只要本地显示开启就保持采集。`STATUS` 返回帧计数、帧龄、显示/流订阅状态和恢复次数，
`DISPLAY 0/1` 与 `SNAPSHOT` 是控制面命令。音频旁路由 `hdmi_preview --audio-only`
运行，不接触视频设备节点。

Input 是 `/dev/hidg0` 键盘与 `/dev/hidg1` 相对鼠标唯一拥有者。量产模式不带 Report ID；
组合 Report ID 只用于维修回退。退出、超时和错误路径必须释放全部按键和鼠标按钮。

IPKVM 保持 GPL-2.0 独立进程和软件包，只使用公开控制/媒体 Socket，不静态链接进产品核心。

## 协议与平台契约

V1 是现有 16 字节原生 C 头和 512 字节载荷。V2 在
`/run/aitvbox/backend-v2.sock` 使用 4 字节大端长度加 JSON；媒体仍走二进制通道。
V1 保留两个公开版本，每迁移一个 topic 都要做 V1/V2 一致性测试。

`core/ports/aitvbox_ports.h` 是稳定 C ABI。每个固件只静态链接一个 Provider；上层依据
capability 决定功能，无能力时返回 `AITVBOX_UNSUPPORTED`。平台 manifest 是 SDK、架构、
构建、打包、烧录和板端验证信息的唯一来源。

ABI V2 为视频帧增加 `frame_id`、单调时钟、plane/stride、CPU 或 DMA-BUF 内存类型以及
显式 `acquire_frame/release_frame` 生命周期；硬件编码器通过独立 Encoder Port 接收同一
帧。V1 同步指针接口仍保留，旧平台可以逐能力迁移。
