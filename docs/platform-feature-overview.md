# 智能桌面小咪平台功能总览

“智能桌面小咪”（Smart Desktop Mimi）是一套运行在全志 A133/Tina Linux
上的桌面 AI 终端系统。产品把本地桌面、涂鸦语音对话、HDMI 输入、远程 KVM、
设备连接和安全 OTA 集成在一台设备中。

## 用户可见功能

| 功能 | 当前实现 |
|---|---|
| 英文桌面 | 英文 LVGL UI，支持触摸屏与遥控器，覆盖 Wi-Fi、蓝牙、AI、视频、设置和 OTA 页面 |
| AI 对话 | 涂鸦自由对话、英文识别/回复、流式文字与语音输出、AEC 防自激和外部人声打断 |
| 唤醒词 | 当前 A133 MNN 离线模型支持“你好涂鸦”；未把不受模型支持的 `Hey Tuya` 标记为可用 |
| 音频 | ALSA/DAUDIO 播放与采集，默认输出音量 50%，蓝牙 PCM 播放及诊断链路 |
| 网络 | Wi-Fi 扫描、连接、自动重连；涂鸦绑定、MQTT 在线状态和数据点同步 |
| HDMI 与 KVM | HDMI 实时预览、信号检测、截图、H.264 WebRTC、USB HID 键鼠控制 |
| AI Agent | 仅基于当前捕获的 HDMI 画面执行受限操作，提供急停、动作范围和 HID 自动释放 |
| 状态灯 | WS2812 显示联网、聆听、思考、说话等状态，并抑制重复帧和异常频闪 |
| 系统升级 | SWUpdate A/B 升级、失败回滚、设备端 UI 和 100ask Cloud OTA |
| 凭据安全 | Tuya UUID/AuthKey 与云端密钥按设备运行时注入，不写入通用固件或源码仓库 |

## 已实现模块

| 模块 | 文件/进程 | 职责 |
|---|---|---|
| 电脑控制 UI | IPKVM `HDMI + HID MCP` 设置页 | 浏览器录入 provider、密钥和任务，查看状态与急停 |
| 控制服务 | `aitvbox-controld` | 密钥安全保存、任务生命周期、HID 释放 |
| 视觉 Agent | `aitvbox-agentd` | HDMI 截图、HTTPS 多模态调用、受限动作循环 |
| MCP 工具层 | `aitvbox-mcpd` | stdio MCP 截图、键盘、鼠标、能力发现、急停 |
| USB HID | `aitvbox-hidctl` | USB Device 标准键盘/相对鼠标报告 |
| 用户 APP UI | `user_app.c` | 动态扫描并嵌入渲染已安装 APP |
| APP Broker | `aitvbox-appd` | action 启动、权限检查、能力转发、native 沙箱 |
| APP Policy | `aitvbox-app-policy` | schema、UI、权限、ELF 与版本校验 |
| APP Installer | `aitvbox-appctl` | 签名验签、原子安装、升级、卸载 |
| APP 浏览器管理 | IPKVM `Applications` 设置页 | 发布者公钥、HTTP 上传安装、版本列表和卸载 |
| 开发工具 | `tools/aitapp/aitapp.py` | init、A133 编译、build、verify、HTTP upload |
| APP C SDK | `platform/app-sdk` | native-rpc ABI 与 broker API |
| 管理工具 | `aitvbox-adminctl` | ADB 模式和 APP 发布者信任库 |
| 浏览器 IPKVM | `aitvbox-ipkvmd` | 鉴权页面、WebRTC 视频与受控 HID |
| A133 视频发送器 | `aitvbox-kvm-video` | HDMI capture、CedarX H.264、socket 分发 |
| IPKVM 管理工具 | `aitvbox-ipkvmctl` | 服务状态、启停和日志 |

## 用户业务

### 本地控制 Windows/Linux

用户连接 HDMI IN 和 USB Device，在 IPKVM 浏览器的“设置 → HDMI + HID MCP”
中填写 OpenAI-compatible HTTPS provider、自己的多模态 `sk-`/`SK-` 密钥和任务。
长文本不要求在设备触屏输入。板端读取电脑画面并通过 USB 标准 HID 操作电脑；
无需在 Windows/Linux 安装客户端。急停、任务上限、动作范围和 HID 自动释放均由
板端执行。

### 用户开发 APP

开发者使用 A133 Tina SDK 工具链编译 schema 2 C APP，或创建无本机代码的 schema 1
声明式 APP。发布者签名后经登录保护的浏览器接口上传，设备仅接受管理员在浏览器中
预先信任的公钥。APP 始终嵌入系统内容区，不能独占全屏，所有硬件操作必须经过权限
broker。

### 外部 MCP

高级用户可通过 ADB/SSH stdio 把 `aitvbox-mcpd` 接到外部 MCP Host。该模式由
Host 自己管理大模型，不需要把 Host 的模型密钥写入设备。它和板端本地助手是两种
入口，不是两套硬件实现。

### 浏览器 IPKVM

用户可在可信局域网或 HTTPS + FRP 链路中打开板端 KVM 页面。局域网访问使用
HTTP/HTTPS 信令和固定 `40000/udp` WebRTC；公网 FRP 必须同时转发网页 TCP 与
WebRTC UDP。浏览器和本地 MCP Agent 共用 HDMI/HID 仲裁，Agent 运行时浏览器不能
注入输入。详见 [IPKVM 使用说明](ipkvm-user-guide.md)。

## 当前硬件边界

已验证的软件接口是 HDMI capture、USB Device HID 和 USB Host 能力发现。
GPIO/I2C/SPI/UART 的权限名与 broker 扩展点已经保留。公开源码不分发板级原理图，
且没有足够的公开信息确认通用外设连接器、IO 电压和总线占用，因此默认报告不可用，
也不允许 APP 直接访问设备节点；量产适配必须在受控硬件资料和目标板上重新验证。

## 命名和扩展规则

- 守护进程：`aitvbox-<domain>d`，例如 `aitvbox-appd`、`aitvbox-controld`。
- 管理命令：`aitvbox-<domain>ctl`，只承载明确的管理操作。
- LVGL 应用：目录 `<domain>/`，入口 `<domain>_app.c/.h`，注册 ID
  `APP_ID_<DOMAIN>`。
- APP 能力：稳定点分名称，例如 `capture.snapshot`、`input.keyboard`、
  `hardware.i2c`。
- IPC command 使用动作名，字段使用 `camelCase`；C API 使用
  `aitvbox_<domain>_<verb>`。
- 板级实现放在 broker 后端，manifest 权限和 APP SDK ABI 不绑定 `/dev` 路径或
  pin 编号，换板时只替换后端映射。

## 尚未并入

- R818 外设逐路 pinmux、电压、冲突和 broker 实现。
- APP `network.https` 以及通用 GPIO/I2C/SPI/UART 操作协议。

这些内容必须建立在当前本地 HDMI/HID 所有权与安全模型之上，不能绕过
`appd`/`mcpd` 直接抢占设备。
