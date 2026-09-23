# AITVBox 运行时架构

> 当前完整中文架构说明见 [zh-CN/architecture.md](zh-CN/architecture.md)，英文版见 [en/architecture.md](en/architecture.md)。本文件保留第一阶段迁移记录。

本文冻结目标依赖方向，并记录第一步迁移结果。这里的“分层”首先指源码职责和
依赖关系清晰，并不要求每层都立刻拆成独立进程。

## 依赖方向

```text
UI / Web / CLI（表现层）
      |
      v
命令路由与用例（应用层）
      |
      v
状态机与资源策略（领域层）
      |
      v
平台适配器（A133、LVGL、Tuya、100ask、WebRTC、SWUpdate）
```

依赖只能向下：UI 不直接依赖硬件实现，进程入口不承载产品用例，厂商 SDK 通过
适配器接入。

## 第一阶段：`lv_backend`

后端目前拆为三个明确职责：

| 组件 | 职责 |
| --- | --- |
| `backend_main.c` | 进程启动、信号、中间件与主循环 |
| `backend/backend_command_router.c` | 解码 UI 命令并分发用例 |
| `backend/backend_runtime.c` | 服务启动、轮询和关闭顺序 |

本次刻意保留既有服务顺序及 HDMI、HID、蓝牙、AI、云和 OTA 行为。它只建立后续
重构所需的接缝，不把结构调整与硬件行为修改混在一次变更里。

## 后续接缝

1. 用强类型领域事件替代服务之间的直接调用。
2. 为 HDMI Capture 和 USB HID 分别建立唯一资源所有者及消费者接口。
3. 版本化 UI/Backend 协议，停止直接传输裸 C 结构体。
4. 将厂商集成移到适配器接口之后。
5. 只有在隔离性或资源所有权确有收益时才继续拆部署进程。

## 多硬件平台约束

目标平台包括 A133、A527、V883、RK3576 和 RV1106。通用层不能通过散落的
`#ifdef SOC_*` 适配它们，每个平台必须作为独立 Provider 接入同一组 Ports：

```text
core/                 通用协议、用例、领域状态机
ports/                Capture、Input、Audio、Network、Update 等抽象接口
platform/a133/        A133 Provider/BSP
platform/a527/        A527 Provider/BSP
platform/v883/        V883 Provider/BSP
platform/rk3576/      RK3576 Provider/BSP
platform/rv1106/      RV1106 Provider/BSP
```

`platform_capabilities.h` 是第一个跨平台契约：上层依据 ABI 与能力位决定功能是否
可用，不依据 SoC 名称写业务分支。当前构建只提供 A133 Provider；选择尚未实现的
平台会在 CMake 配置阶段明确失败，避免静默生成错误固件。

每个新平台接入至少需要：独立 Toolchain、Platform Provider、Packaging Profile、
能力契约测试以及对应开发板冒烟测试。厂商多媒体 API、设备节点和内存布局只能出现
在对应平台目录内。
