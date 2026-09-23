# AI-DeskTopBox 解决方案网站内容素材包

> 用途：面向开发者与方案商的中英双语长页面内容稿
> 品牌结构：AI-DeskTopBox 平台 + Smart Desktop Mimi 参考产品
> 页面目标：帮助技术决策者快速理解能力、边界和开发入口，并进入开发文档
> 内容原则：已实现能力与扩展框架分开表达；功能承诺以对应硬件、固件与验收清单为准

## 1. 页面元信息

| 项目 | 中文 | English |
|---|---|---|
| 页面标题 | AI-DeskTopBox｜嵌入式 AI 桌面与远程控制平台 | AI-DeskTopBox — Embedded AI Desktop and Remote Control Platform |
| SEO 标题 | AI-DeskTopBox：AI 桌面、IPKVM、MCP 与安全 APP 平台 | AI-DeskTopBox: AI Desktop, IPKVM, MCP and Secure App Platform |
| Meta description | 面向开发者与方案商的嵌入式 AI 平台，集成桌面交互、语音 AI、HDMI/IPKVM、USB HID、MCP、签名 APP、A/B OTA 与量产工具链。 | An embedded AI platform for developers and solution providers, integrating desktop UX, voice AI, HDMI/IPKVM, USB HID, MCP, signed apps, A/B OTA and production tooling. |
| 分享标题 | 一套平台，连接 AI、屏幕、电脑控制与量产交付 | One Platform for AI, Displays, Computer Control and Production |
| 分享摘要 | 从 A133 参考实现出发，快速构建可扩展、可维护的带屏 AI 终端。 | Start from a proven A133 reference implementation and build extensible, maintainable screen-based AI devices. |
| 建议关键词 | 嵌入式 AI、桌面终端、IPKVM、MCP、USB HID、LVGL、A133、Tina Linux、A/B OTA | embedded AI, desktop terminal, IPKVM, MCP, USB HID, LVGL, A133, Tina Linux, A/B OTA |

不在页面标题中固定软件版本或发布日期。版本信息应由网站发布系统或下载页动态维护。

---

## 2. 首屏 Hero

**眉题 / Eyebrow**

> EMBEDDED AI PLATFORM · REFERENCE DESIGN · DEVELOPER READY

**中文标题**

> 让 AI 看见屏幕、理解任务，并安全连接真实设备

**English headline**

> Give AI a Screen, a Safe Control Plane, and Access to Real Devices

**中文副标题**

> AI-DeskTopBox 是面向开发者与方案商的嵌入式 AI 桌面平台。它把 LVGL 桌面、语音交互、HDMI/IPKVM、USB HID、MCP、签名 APP、设备身份与 A/B OTA 组合成可构建、可扩展、可量产的系统底座。

**English subheadline**

> AI-DeskTopBox is an embedded AI desktop platform for developers and solution providers. It brings together an LVGL desktop, voice interaction, HDMI/IPKVM, USB HID, MCP, signed apps, device identity and A/B OTA in a buildable, extensible and production-oriented foundation.

**CTA**

- 主按钮：`进入开发文档` / `Explore Developer Docs`
  - 链接：[平台功能总览](../platform-feature-overview.md)
- 次按钮：`查看参考产品` / `View the Reference Product`
  - 链接：[智能桌面小咪解决方案](smart-desktop-mimi-solution.md)

**首屏能力标签**

`A133 / Tina Linux` · `LVGL` · `Voice AI` · `HDMI Capture` · `WebRTC IPKVM` · `USB HID` · `MCP` · `Signed Apps` · `A/B OTA`

**建议配图**

![Smart Desktop Mimi concept visual](assets/smart-desktop-mimi-hero.png)

图片脚注：

- 中文：Smart Desktop Mimi 是 AI-DeskTopBox 的参考产品。图片为方案阶段概念视觉，以最终硬件为准。
- English: Smart Desktop Mimi is the reference product for AI-DeskTopBox. Concept visual; final hardware may vary.

---

## 3. 为什么选择 AI-DeskTopBox

**区块标题**

- 中文：不是又一个 AI Demo，而是一套设备平台
- English: More Than an AI Demo — A Complete Device Platform

**引导文案**

- 中文：从板级驱动到用户界面，从电脑控制到应用权限，从设备凭据到升级回滚，AI-DeskTopBox 提供一条可以继续开发、验证和交付的完整路径。
- English: From board support to user experience, computer control, app permissions, device credentials and safe updates, AI-DeskTopBox provides a complete path for development, validation and delivery.

### 四张价值卡片

| 中文标题 | 中文说明 | English title | English copy |
|---|---|---|---|
| 快速形成产品原型 | 复用已运行的 A133 参考实现、桌面 UI、服务进程和打包链路，减少从 BSP 与驱动开始的重复开发。 | Build a Product Prototype Faster | Reuse a working A133 reference implementation, desktop UI, system services and packaging flow instead of restarting from the BSP. |
| 统一多种交互 | 在同一设备中组织屏幕、语音、网络、蓝牙、HDMI、键鼠控制与状态反馈。 | Unify Device Interaction | Coordinate display, voice, networking, Bluetooth, HDMI, keyboard and pointer control, and status feedback in one device. |
| 安全开放能力 | APP、MCP 和视觉 Agent 通过权限、资源仲裁与白名单动作使用硬件，不让扩展逻辑直接抢占设备节点。 | Extend Through Controlled Interfaces | Apps, MCP tools and visual agents use permissions, arbitration and allowlisted actions instead of directly competing for device nodes. |
| 面向持续维护 | 把一机一密、恢复镜像、A/B OTA、失败回滚、版本审计和诊断入口纳入交付体系。 | Designed for Ongoing Operations | Build per-device identity, recovery, A/B OTA, rollback, version auditing and diagnostics into the delivery model. |

---

## 4. 平台能力矩阵

**区块标题**

- 中文：从本地交互到远程控制的四组核心能力
- English: Four Core Capability Domains, from Local UX to Remote Control

### 4.1 AI 桌面与设备交互 / AI Desktop and Device Interaction

**中文文案**

> 英文 LVGL 桌面覆盖 AI、Wi-Fi、蓝牙、视频、设置、升级和用户 APP。涂鸦 AI 链路提供流式文字与语音回复，并结合 AEC、人声打断和 WS2812 状态反馈，形成完整的桌面交互体验。

**English copy**

> The English LVGL desktop brings together AI, Wi-Fi, Bluetooth, video, settings, updates and user apps. The Tuya AI path supports streaming text and speech responses, with AEC, human voice interruption and WS2812 status feedback for a coherent desktop experience.

**功能标签**

- LVGL desktop and application shell
- Touch and remote-control input
- Streaming voice conversation
- AEC and human voice interruption
- Wi-Fi scanning, connection and recovery
- Bluetooth audio and device management
- WS2812 listening, thinking and speaking states

**配图建议**：[桌面实机画面](../debug-evidence/a133-en-us-20260906/final/desktop.png) + [AI 对话](../debug-evidence/a133-en-us-20260906/final/ai-chat.png)

### 4.2 HDMI、IPKVM 与安全电脑控制 / HDMI, IPKVM and Safe Computer Control

**中文文案**

> 设备从 HDMI 输入获取 Windows 或 Linux 画面，并通过标准 USB HID 提供键盘和相对鼠标控制。浏览器可查看 WebRTC 实时画面；板端视觉 Agent 或外部 MCP Host 可在资源仲裁和急停机制下执行受限任务，被控电脑无需安装客户端。

**English copy**

> The device captures a Windows or Linux display through HDMI and provides keyboard and relative pointer control through standard USB HID. A browser can display the live WebRTC stream, while an on-device visual agent or an external MCP host can perform constrained tasks under resource arbitration and emergency-stop controls. No client software is required on the controlled computer.

**功能标签**

- HDMI live preview and snapshots
- Hardware H.264 and WebRTC streaming
- Browser-based authenticated IPKVM
- Standard USB keyboard and relative pointer
- OpenAI-compatible HTTPS multimodal provider
- Allowlisted actions and parameter validation
- Task step limits, emergency stop and automatic HID release

**配图建议**：[浏览器指针校准](../debug-evidence/kvm-pointer-calibration.png) + [AI HDMI MCP](../debug-evidence/a133-en-us-20260906/hdmi-mcp.png)

### 4.3 签名 APP 与 MCP 扩展 / Signed Apps and MCP Extensions

**中文文案**

> 开发者可以创建声明式 APP 或带 C 逻辑的 native-rpc APP，使用 RSA 签名并通过浏览器安装。APP 只能调用清单中声明的能力；MCP Server 则向外部 AI Host 提供截图、键盘、鼠标、能力发现和急停工具。两种扩展方式复用同一套硬件所有权与安全边界。

**English copy**

> Developers can create declarative apps or native-rpc apps with C logic, sign them with RSA and install them through the browser. Apps can invoke only the capabilities declared in their manifests. The MCP server exposes screenshots, keyboard, pointer, capability discovery and emergency stop to external AI hosts. Both extension paths share the same hardware ownership and safety boundaries.

**功能标签**

- Declarative schema 1 apps
- Native-rpc schema 2 C apps
- RSA signatures and trusted publisher keys
- Atomic install, update and uninstall
- Permission broker and isolated app storage
- MCP over local, ADB or SSH stdio transport
- Capture, input, capabilities and safety tools

**配图建议**：[用户 APP](../debug-evidence/a133-en-us-20260906/final/all-apps.png) + [浏览器应用管理](../debug-evidence/a133-en-us-20260906/final/all-apps.png)

### 4.4 量产、身份与升级 / Production, Identity and Updates

**中文文案**

> 通用固件不包含设备密钥。Tuya License 与云端设备身份在运行时逐台写入，并保存在跨 A/B 槽共享的数据区。SWUpdate 负责签名升级、槽位切换与失败回滚；发布流程同时保留恢复、哈希、版本和第三方组件审计信息。

**English copy**

> Generic firmware contains no device credentials. Tuya licenses and cloud identities are provisioned per device at runtime and stored in data shared across A/B slots. SWUpdate handles signed updates, slot switching and rollback, while the release flow preserves recovery artifacts, hashes, version metadata and third-party component records.

**功能标签**

- Per-device runtime credential provisioning
- Credentials excluded from generic firmware and source releases
- Signed A/B system updates
- Boot health check and rollback path
- Persistent Wi-Fi, Bluetooth and device identity data
- Recovery image and reproducible release artifacts
- Local UI and optional cloud-triggered update flow

**配图建议**：[系统升级页面](../debug-evidence/a133-en-us-20260906/system-update.png)

---

## 5. 三条开发采用路径

**区块标题**

- 中文：选择适合项目阶段的扩展方式
- English: Choose the Extension Path That Fits Your Project

### 路径一：开发设备 APP / Build a Device App

- 中文：用声明式 UI 快速创建轻量应用，或使用 C APP SDK 编写短动作逻辑。完成 A133 交叉编译、RSA 签名和本地验签后，通过受登录保护的浏览器上传，安装后无需重启设备。
- English: Create lightweight apps with declarative UI, or use the C App SDK for short native actions. Cross-compile for A133, sign and verify the package with RSA, then upload it through the authenticated browser interface without rebooting the device.
- CTA：`阅读 APP 开发手册` / `Read the App Development Guide`
- 链接：[AITVBox 用户 APP 开发手册](../user-app-development-guide.md)

### 路径二：接入 MCP Host / Connect an MCP Host

- 中文：通过本机、ADB 或 SSH stdio 启动板端 MCP Server，把当前 HDMI 截图、USB HID、能力发现和急停接入支持命令型 MCP Server 的 Host。模型与编排运行在开发者自己的环境中。
- English: Start the on-device MCP server over local, ADB or SSH stdio and expose the current HDMI screenshot, USB HID, capability discovery and emergency stop to a host that supports command-based MCP servers. Models and orchestration remain in the developer’s own environment.
- CTA：`查看 MCP 工具与协议` / `Explore MCP Tools and Protocols`
- 链接：[电脑控制与 MCP 使用手册](../mcp-user-guide.md)

### 路径三：集成浏览器 IPKVM / Integrate Browser IPKVM

- 中文：通过受密码保护的 Web 页面、WebRTC 视频和标准 USB HID 管理目标电脑。局域网可使用 HTTP/HTTPS；公网方案必须同时规划 HTTPS 信令和 WebRTC UDP，并完成项目级网络与证书验收。
- English: Manage a target computer through an authenticated web interface, WebRTC video and standard USB HID. HTTP or HTTPS can be used on trusted local networks. Internet-facing deployments must account for both HTTPS signaling and WebRTC UDP, with project-specific network and certificate validation.
- CTA：`阅读 IPKVM 集成说明` / `Read the IPKVM Integration Guide`
- 链接：[浏览器 IPKVM 使用与开发说明](../ipkvm-user-guide.md)

---

## 6. 架构区块

**区块标题**

- 中文：清晰的依赖方向，明确的硬件所有权
- English: Clear Dependency Direction and Explicit Hardware Ownership

**中文说明**

> 前端和服务传输层只调用应用用例；应用层依赖领域策略与稳定 Ports；只有平台 Provider 和外部 Integration 可以依赖厂商 SDK。Capture、Input、Agent、APP 和 Update 使用独立故障边界，避免多个功能争用 HDMI 或 HID。

**English copy**

> Frontends and service transports call application use cases. Application logic depends on domain policy and stable ports, while only platform providers and external integrations depend on vendor SDKs. Capture, input, agent, app and update services use separate failure boundaries to prevent competing access to HDMI or HID resources.

**网页架构图文字**

```text
LVGL UI · Web · CLI
        ↓
Application Use Cases · Command Routing
        ↓
Domain Policy · Typed Events · Resource Arbitration
        ↓
Stable Ports · IPC V2 · V1 Compatibility
        ↓
A133 Provider · Tuya · WebRTC · MCP · SWUpdate · Cloud
```

**架构旁注**

- Single-owner capture path shared by local display, JPEG snapshots and WebRTC encoding.
- Exclusive input ownership with automatic key and button release on stop or failure.
- Capability-driven platform behavior instead of business logic tied to device nodes.
- IPKVM remains an independently packaged process behind documented sockets.

CTA：`查看系统架构` / `View System Architecture` — [中文](../zh-CN/architecture.md) · [English](../en/architecture.md)

---

## 7. 已实现与可扩展

**区块标题**

- 中文：现在可以使用什么，哪些需要项目适配
- English: What Is Available Today and What Requires Project Adaptation

### A133 参考实现 / A133 Reference Implementation

页面状态标识：`已实现 / Implemented`

- LVGL 桌面、后端服务与产品打包。
- Tuya AI 语音与设备连接链路。
- HDMI 单一采集所有权、本地显示、JPEG 与 H.264/WebRTC 消费路径。
- 浏览器 IPKVM、USB HID、板端视觉 Agent 与 MCP Server。
- 签名 APP、权限 Broker、浏览器安装管理与隔离存储。
- 一机一密、A/B OTA、恢复与发布审计。
- A133 工具链交叉编译和主机协议/安全回归。

English summary:

> The A133 reference implementation includes the desktop and backend, Tuya AI integration, shared HDMI capture, local display, JPEG and H.264/WebRTC consumers, browser IPKVM, USB HID, on-device visual agent, MCP server, signed apps, permission brokerage, per-device identity and A/B updates.

### 平台适配框架 / Platform Adaptation Framework

页面状态标识：`需要适配 / Adaptation Required`

- A527、V883、RK3576 和 RV1106 已保留平台 manifest、Provider 和构建接缝，但当前不是可交付实现。
- 新平台必须补齐独立 toolchain、Provider、打包配置、能力契约测试和开发板冒烟测试。
- 通用 GPIO、I²C、SPI、UART 权限名与 Broker 扩展点已经预留；具体连接器、IO 电压、pinmux 和总线占用必须基于目标硬件重新确认。

English summary:

> A527, V883, RK3576 and RV1106 have platform manifests and provider integration points, but are not currently presented as delivered implementations. Each target requires its own toolchain, provider, packaging profile, capability tests and board validation. Generic GPIO, I²C, SPI and UART access remains hardware-specific work.

**统一边界声明**

- 中文：主机回归和交叉编译不能替代目标产品的 HDMI 电气、USB 枚举、声学结构、触摸、网络/NAT、温升与整机认证测试。
- English: Host regression and cross-compilation do not replace product-specific validation of HDMI electrical behavior, USB enumeration, acoustics, touch, networking/NAT, thermals or regulatory compliance.

---

## 8. 工程可信度

**区块标题**

- 中文：可验证，而不是只可演示
- English: Built to Be Verified, Not Merely Demonstrated

| 中文标题 | 中文说明 | English title | English copy |
|---|---|---|---|
| 自动化回归 | 覆盖 MCP 生命周期、输入范围、急停、HTTPS 模型调用、APP 签名、安装恢复、权限隔离、鉴权和 IPC 边界。 | Automated Regression | Covers MCP lifecycle, input limits, emergency stop, HTTPS model calls, app signing, install recovery, permission isolation, authentication and IPC boundaries. |
| A133 交叉构建 | 核心进程、APP SDK 示例、IPKVM 与产品包均通过真实 Tina 工具链构建检查。 | A133 Cross-Builds | Core services, App SDK examples, IPKVM and product packages are checked with the real Tina toolchain. |
| 安全默认值 | 密钥不进入通用固件；配置原子写入；敏感文件限制权限；未知能力与未授权调用默认拒绝。 | Secure Defaults | Credentials stay out of generic firmware, configuration is written atomically, sensitive files use restricted permissions, and unknown or unauthorized operations fail closed. |
| 可恢复发布 | 构建产物带版本、哈希和检查记录；升级采用签名 A/B 路径并保留恢复方案。 | Recoverable Releases | Artifacts include versions, hashes and validation records, while signed A/B updates preserve a recovery path. |

CTA：`查看平台验证记录` / `Review Platform Validation` — [平台验证](../platform-validation.md)

---

## 9. 参考产品区块

**区块标题**

- 中文：Smart Desktop Mimi：平台能力的参考产品
- English: Smart Desktop Mimi — A Reference Product Built on the Platform

**中文文案**

> Smart Desktop Mimi 把 AI-DeskTopBox 的桌面、语音、网络、灯效、应用和升级能力整合为带屏桌面终端。它既是可演示的产品形态，也是品牌 UI、角色音色、行业知识、设备面板和硬件配置定制的起点。

**English copy**

> Smart Desktop Mimi combines the AI-DeskTopBox desktop, voice, connectivity, lighting, app and update capabilities in a screen-based desktop terminal. It is both a demonstrable product form and a starting point for branded UI, personas and voices, domain knowledge, device panels and hardware configuration.

**展示标签**

`Reference Hardware` · `English Desktop` · `Voice AI` · `Brandable UI` · `Per-device Identity` · `OTA Ready`

**建议配图**：[概念分镜](assets/smart-desktop-mimi-storyboard.png)

CTA：`查看完整解决方案` / `View the Full Solution` — [Smart Desktop Mimi 解决方案](smart-desktop-mimi-solution.md)

---

## 10. 页尾 CTA

**中文标题**

> 从一个可运行的平台开始，而不是从零拼接功能

**English headline**

> Start from a Working Platform, Not a Collection of Disconnected Features

**中文说明**

> 先了解平台能力和开发路径，再根据目标硬件、AI 服务、网络环境与交付规模确定适配范围。

**English copy**

> Review the platform capabilities and development paths, then define adaptation work around your target hardware, AI provider, network environment and delivery scale.

**按钮顺序**

1. `进入开发文档` / `Explore Developer Docs` → [平台功能总览](../platform-feature-overview.md)
2. `查看系统架构` / `View Architecture` → [系统架构](../zh-CN/architecture.md)
3. `评估参考产品` / `Evaluate the Reference Product` → [客户需求确认表](customer-requirements-template.md)

页尾声明：

- 中文：功能可用性取决于目标硬件、固件配置、第三方服务和项目验收。页面中的概念图不代表冻结的工业设计。
- English: Feature availability depends on target hardware, firmware configuration, third-party services and project acceptance. Concept visuals do not represent a frozen industrial design.

---

## 11. 图片素材清单

| 页面位置 | 素材 | 类型 | 建议比例 | 中文 alt | English alt |
|---|---|---|---|---|---|
| 首屏 | `assets/smart-desktop-mimi-hero.png` | 概念图 | 16:9 | Smart Desktop Mimi 带屏桌面 AI 终端概念图 | Smart Desktop Mimi screen-based AI desktop concept |
| AI 桌面 | `../debug-evidence/a133-en-us-20260906/final/desktop.png` | 真实设备截图 | 4:3 | A133 设备上的英文 LVGL 桌面 | English LVGL desktop running on the A133 device |
| AI 对话 | `../debug-evidence/a133-en-us-20260906/final/ai-chat.png` | 真实设备截图 | 4:3 | Smart Desktop Mimi AI 对话界面 | Smart Desktop Mimi AI conversation interface |
| 用户 APP | `../debug-evidence/a133-en-us-20260906/final/all-apps.png` | 真实设备截图 | 4:3 | 设备内的用户应用页面 | User applications page on the device |
| 系统升级 | `../debug-evidence/a133-en-us-20260906/system-update.png` | 真实设备截图 | 4:3 | 设备端系统升级页面 | On-device system update interface |
| IPKVM | `../debug-evidence/kvm-pointer-calibration.png` | 真实 Web UI | 16:9 | 浏览器 IPKVM 指针校准 | Pointer calibration in the browser IPKVM |
| AI HDMI MCP | `../debug-evidence/a133-en-us-20260906/hdmi-mcp.png` | 真实设备截图 | 4:3 | 设备上的 AI HDMI MCP 工作区 | AI HDMI MCP workspace on the device |
| APP 管理 | `../debug-evidence/a133-en-us-20260906/final/all-apps.png` | 真实设备截图 | 4:3 | 设备内的应用页面 | Applications on the device |
| 参考产品 | `assets/smart-desktop-mimi-storyboard.png` | 概念分镜 | 16:9 | Smart Desktop Mimi 使用场景概念分镜 | Smart Desktop Mimi concept usage storyboard |

图片使用规则：

- 概念图必须保留“以最终硬件为准”的说明，不能作为量产外观承诺。
- 真实截图允许裁切无关留白，但不得修改状态、伪造功能或遮盖错误后继续作为成功证据。
- 对外发布前检查账号、API key、Wi-Fi 密码、设备序列号、内部 IP 和调试日志。
- 移动端优先使用单张 16:9 图或 4:3 图，不直接展示纵向 contact sheet。

---

## 12. 推荐页面顺序与响应式规则

桌面端顺序：

1. Hero
2. 四项平台价值
3. 四组能力矩阵
4. 三条开发路径
5. 分层架构
6. 已实现与可扩展
7. 工程可信度
8. Smart Desktop Mimi 参考产品
9. 文档 CTA 与边界声明

移动端保持相同叙事顺序，双栏与四栏统一改为单列。中英文建议通过语言切换展示，
不要在同一张功能卡内同时堆叠两种语言。CTA 在移动端保持“开发文档”为第一个按钮。

---

## 13. 术语与表达规范

| 推荐表达 | 不建议表达 | 原因 |
|---|---|---|
| A133 reference implementation | Supports every SoC | 其他平台目前只有接入框架 |
| OpenAI-compatible multimodal provider | Built-in OpenAI | 平台接入兼容协议，不绑定单一服务商 |
| Signed apps with permission brokerage | Run any Linux app | APP 类型、包内容和权限均受策略约束 |
| Browser IPKVM with project-specific network validation | Works everywhere over the Internet | 公网 UDP、NAT、证书和 FRP 需项目验收 |
| A/B update and rollback path | Updates can never fail | 升级仍需断电、回滚和目标硬件验证 |
| Concept visual; final hardware may vary | Production product photo | 当前概念图不是冻结工业设计 |
| Hardware capabilities exposed when validated | GPIO/I²C/SPI/UART ready | 通用外设连接和电气条件尚未冻结 |

产品名首次出现时使用：

- `AI-DeskTopBox embedded AI platform`
- `Smart Desktop Mimi reference product`

后续可简称 `AI-DeskTopBox` 和 `Smart Desktop Mimi`。技术名保持 `IPKVM`、`MCP`、
`USB HID`、`WebRTC`、`LVGL`、`A/B OTA`，不做不必要的中文缩写。
