# supportappconfig 开发记忆

记录日期：2026-07-26；最近更新：2026-07-31
工作分支：`supportappconfig`
起始基线：`04c66ed5`（`aitvbox-product`）

本文记录可信用户 APP、板端 MCP/视觉 Agent、USB HID 和浏览器 IPKVM 的架构、
关键修复、验证结果与遗留边界。后续维护应先阅读本文，再分别查阅对应用户手册。

## 1. 目标和边界

本分支实现以下产品闭环：

1. A133 通过 HDMI IN 读取 Windows/Linux 画面，通过 USB Device 模拟标准键盘鼠标。
2. 用户在 IPKVM 浏览器填写多模态模型 provider、`sk-`/`SK-` 密钥并发起任务。
3. 高级用户可把设备的截图和 HID 能力作为 stdio MCP 工具接入外部 MCP Host。
4. 浏览器可通过 WebRTC 查看和控制同一台电脑，并可用 FRP 转发到公网。
5. 第三方开发者使用提供的 Tina 工具链构建、RSA 签名并通过 ADB 安装可信 APP。
6. APP 只能嵌入系统内容区，系统返回栏永久保留，不支持独占全屏。

当前硬件能力只开放已落实的 HDMI capture、USB keyboard 和 USB pointer。R818 图纸
尚不足以确认通用 GPIO/I2C/SPI/UART 接口的引脚、电压和资源占用，因此这些权限只
保留 ABI 名称，默认不可调用。

## 2. 模块和所有权

| 域 | 核心进程/目录 | 所有权 |
|---|---|---|
| 桌面 | `lvglsim`、`lv_backend` | 页面、返回栏、用户 APP 动态列表 |
| HDMI | `hdmi_preview` | 本地预览、JPEG 快照、capture 锁 |
| 本地控制 | `aitvbox-controld`、`aitvbox-agentd` | 密钥、任务状态、模型循环、急停 |
| MCP | `aitvbox-mcpd` | MCP JSON-RPC、截图和 HID 工具适配 |
| 输入 | `aitvbox-hidctl` | USB HID 报告和跨进程输入锁 |
| APP | `aitvbox-appctl`、`aitvbox-appd` | 验签、安装、权限、native 沙箱 |
| IPKVM | `aitvbox-ipkvmd`、`aitvbox-kvm-video` | Web 鉴权、WebRTC、H.264 转发 |
| 构建 | `scripts/`、`packaging/` | 产品源码与 Tina SDK 薄包分离 |

硬件资源必须只有一个明确入口。浏览器、Agent、MCP 和用户 APP 都通过公共仲裁进入
HDMI/HID，不允许各自直接打开设备节点。Agent 执行期间浏览器输入被拒绝；停止、
超时或进程退出时必须发送全键释放和鼠标释放报告。

## 3. 可信 APP 流程

开发端流程：

```text
aitapp init -> 编写 manifest/UI/native -> A133 交叉编译
-> aitapp build(RSA/SHA-256) -> aitapp verify -> ADB upload
```

设备端流程：

```text
临时上传 -> 包大小/路径/类型预检 -> 发布者公钥验签 -> 文件哈希复核
-> policy 校验 -> 同文件系统恢复式切换 -> 桌面动态扫描
-> 用户点击 action -> appd 再验身份/权限 -> 受限 native-rpc
```

schema 1 只包含声明式 `column/text/action`。schema 2 可包含一个 AArch64
`native-rpc` ELF；运行时使用 UID/GID 65534、无附加组、`no_new_privs`、rlimit
和 seccomp。两个 schema 都必须声明 `presentation=embedded`。

这里的安装切换可在下次操作时清理 `.install` 并恢复 `.old` 状态，在关键 rename 前后
`sync`，但不是跨目录事务，也不是 `renameat2(RENAME_EXCHANGE)` 的单系统调用交换。
量产时应把 APP 安装目录放在同一个可靠文件系统中。

## 4. MCP 和 IPKVM 流程

本地 Agent 是产品默认入口：IPKVM 浏览器设置页保存 provider/密钥后，
`controld` 启动 `agentd`；
`agentd` 循环获取 HDMI JPEG、调用用户配置的 HTTPS 多模态接口、严格解析受限动作，
再通过 HID 执行。密钥只保存在设备受限配置中，不进入 APP 包或日志，状态接口也
不会把密钥返回浏览器。

stdio MCP 是开发/集成入口：外部 Host 启动 `aitvbox-mcpd`，调用
`capture.snapshot`、`input.keyboard`、`input.pointer`、`system.capabilities`
和 `system.emergency_stop`。这一模式的大模型与密钥由 Host 管理。

IPKVM 使用 vendored `avaotaa1-kvmsource` Web/信令层，加上
`kvm_video_demo` 对应的 A133 CedarX H.264 sender。WebRTC 固定使用
`40000/udp`；公网 FRP 必须同时转发网页 TCP 和媒体 UDP。平台模式强制密码初始化，
重启后旧登录 token 失效，并关闭存储上传下载、SD 管理和禁用密码等高风险端点。

## 5. 本轮缺陷和修复

| 缺陷 | 影响 | 修复 |
|---|---|---|
| IPKVM 全局 session 并发读写 | Go race、错误控制会话 | 原子 session 指针和原子活动计数 |
| Unix stream 按单次 read 解 JSON | 拆包/粘包时响应丢失 | `json.Decoder` 连续解码 |
| native request 删除错误序号 | request map 泄漏 | 按原始 seq 清理并隔离锁 |
| 超时关闭响应 channel | 晚到响应触发 panic | 缓冲 channel，不由等待方关闭 |
| broadcaster 持锁执行回调 | 慢启动导致阻塞/死锁 | 锁内复制，锁外回调 |
| FRPC 启动即退出仍报告成功 | 管理状态失真 | 启动窗口内探测退出并返回错误 |
| HID 陈旧锁直接删除 | 删除另一个进程的新锁 | 仅回收超过宽限期的残缺锁 |
| IPKVM 兼容无密码配置 | 可绕过平台认证策略 | 启动迁移到密码初始化状态 |
| 登录 token 跨重启有效 | 泄漏 token 长期可用 | 每次启动失效并持久化 |
| Origin 只比 host | scheme 降级边界不严 | 同时校验 scheme、host 和 URL 组成 |
| APP 包无总解压上限 | tar 解压耗尽存储 | 主机端和设备端均限制 64 MiB |
| ADB 临时包名固定 | 并发上传互相覆盖 | 使用包 SHA-256 生成唯一名称并清理 |
| APP 运行目录为 0750 | UID 65534 无法连接 broker | 目录 0751、socket 仍按服务策略控制 |
| 快照文件可被非特权读取 | APP 绕过 capture broker | 临时快照强制 0600 |
| native seccomp 缺少新系统调用 | 可尝试 openat2/clone3/io_uring | 扩充失败关闭 deny list |
| MCP 超长请求按块继续解析 | JSON 流失步或绕过限制 | 超过 1 MiB 时排空整行并拒绝 |
| Agent Authorization 固定缓冲 | 长密钥截断 | 动态分配 header |
| Agent 停止受网络阻塞 | 急停延迟 | connect timeout、progress abort、NOSIGNAL |
| SDK 同步不删除旧薄包文件 | 删除内容仍残留到固件 | `rsync --delete` 精确镜像 |
| APP 编译污染 SDK 配置 | 后续纯 SDK 构建不确定 | trap 保存并精确恢复配置 |
| HDMI monitor 与 stop 同时 wait/kill | 子进程生命周期竞争 | monitor 独占回收，stop 只置位并 join |
| OTA/设备 ID 静默截断 | 错误目标或身份继续运行 | 显式长度校验和定长复制 |
| APP 锁目录创建后 PID 尚未写入 | 并发操作误删有效锁 | 残缺锁增加 5 秒宽限期后才允许回收 |
| APP 断电残留 `.install.*` | overlay 空间持续泄漏 | 持锁恢复时校验类型并清理中断 stage |
| `appd` 无并发上限和客户端期限 | 本地连接耗尽进程/内存 | 最多 8 个 worker，绝对读期限和 socket 读写超时 |
| IPKVM 配置原地截断写 | 断电丢失密码配置并回到初始化 | 同目录临时文件、`fsync`、原子 rename、目录 `fsync` |
| IPKVM 配置损坏后删除重置 | 意外重新开放管理员认领 | A133 模式保留坏文件并拒绝启动，认证失败关闭 |
| IPKVM 首次设置并发竞争 | 后写请求可能覆盖管理员密码 | 首次设置事务串行化，持久化成功后才签发 Cookie |
| IPKVM HTTP 服务无头部期限 | 慢连接长期占用服务资源 | HTTP/TLS 统一请求头、空闲超时和 32 KiB 头部上限 |

## 6. 安全不变量

- 设备只保存受信发布者公钥，开发者私钥不得上传。
- 安装器拒绝路径穿越、重复路径、符号链接、脚本、动态库、额外 ELF 和可执行资源。
- APP 声明权限只是申请，最终能力还必须由 broker 运行时确认。
- native APP 无 seccomp 时失败关闭，不允许降级执行。
- 浏览器控制必须先完成密码认证和同源校验。
- 模型返回只允许白名单键盘/鼠标动作，不能变成 shell 或任意设备 IO。
- HDMI/HID 锁超时回收必须验证 owner，不能无条件删除其他进程的活动锁。
- SDK 同步和构建结束后必须恢复产品包关闭状态，不在 SDK 中保留产品源码。

## 7. 验证记录

Ubuntu 已完成：

- `tests/platform/test_host_integration.py`：23 项通过。
- IPKVM `go test ./...` 和 `go test -race ./...` 通过。
- Python `py_compile`、Shell `bash -n`、`git diff --check` 通过。
- 平台服务、HDMI preview、IPKVM、LVGL 桌面/后端均使用
  `/home/ubuntu/A133-Tina5.0-v0.9` 完成交叉编译。
- `lv_backend` 产物确认为 AArch64 ELF；schema 2 示例使用 Tina GCC 8.3 编译。
- 完整 `build_firmware.sh` 构建与 pack 通过，生成 58 MiB
  `a133_linux_b6_uart0.img`；最终 SquashFS 已核对关键文件、root 所有权、0755
  权限、动态库依赖和启动顺序，SDK 随后恢复纯模式。

2026-07-31 已完成当前 A133、浏览器 HDMI 画面和 Windows 主机链路的定向实机
验证，包括 USB gadget 枚举与输入、1920×1080 WebRTC 画面、任务栏底边修复和
AI observation-only 任务。以下长期、切换和异常场景仍须按
[平台验证手册](platform-validation.md) 继续上板验收：

- HDMI HPD/DDC、分辨率切换、断流恢复和长时间采集。
- Linux USB 枚举、Windows/Linux 重连、挂起恢复和长时间急停释放。
- 浏览器 WebRTC 长时间画面延迟、多人连接仲裁、FRPS 公网 UDP。
- 真实模型供应商 API 的响应格式、限流、超时和费用边界。
- 断电期间 APP 安装恢复与 overlay 存储寿命。

## 8. 已知限制和后续顺序

1. 基线仓库仍跟踪两份内容相同的遗留 ECDSA 私钥
   `third_party/100ask-iot-sdk/100ask_keys/100ask_ecdsa.pem` 和
   `tools/factory-assistant/factory-assistant-v1/02-密钥/100ask_ecdsa.pem`。该密钥
   已进入 Git 历史，删除当前文件不能解除泄漏风险；必须视为已泄漏，轮换云端公钥，
   将新私钥迁入 KMS/HSM 或受控工位密钥目录，再单独清理历史。
2. schema 2 使用共享低权限 UID 和 seccomp 系统调用沙箱，不等同于容器、namespace
   或 Landlock 隔离；只接受管理员信任发布者的 APP。
3. native action 最长 5 秒，当前 LVGL 调用链可能在 action 期间短暂停顿；后续可增加
   后台 job ID 和 UI 轮询协议。
4. `network.https` 以及 GPIO/I2C/SPI/UART broker 尚未实现；`storage.app` 已开放。
5. APP UI v1 只支持 `column/text/action`，不接受任意 LVGL 代码注入。
6. IPKVM 固定媒体端口为 `40000/udp`，FRP/防火墙配置必须保持一致。
7. IPKVM 没有外部证书时监听局域网 HTTP。首次初始化必须在隔离管理网完成；公网
   转发必须使用受信 TLS 反向代理或部署 `tls.crt`/`tls.key`。量产前应确定设备证书
   签发、轮换和可靠时间源，不能把明文密码初始化直接暴露到客户网络。
8. IPKVM 首次设置端点尚无每设备出厂 bootstrap code 或物理确认，因此同一局域网中
   抢先访问设备的人仍可能先认领管理员密码。量产方案应绑定设备标签一次性码或触屏确认。
9. 上板稳定性测试通过后，再考虑扩大受信发布者范围和开放新的硬件能力。

## 9. 维护入口

- APP 开发：[user-app-development-guide.md](user-app-development-guide.md)
- APP ABI：[platform-app-sdk.md](platform-app-sdk.md)
- MCP/本地 Agent：[mcp-user-guide.md](mcp-user-guide.md)
- IPKVM：[ipkvm-user-guide.md](ipkvm-user-guide.md)
- 业务状态机：[platform-business-logic.md](platform-business-logic.md)
- 构建与 SDK 分离：[build.md](build.md)

新增模块继续遵循 `aitvbox-<domain>d`、`aitvbox-<domain>ctl`、
`APP_ID_<DOMAIN>` 和点分 capability 命名。板级 `/dev`、pin 和 ioctl 不进入公开
APP ABI，必须隐藏在 broker 后端。

## 10. 2026-07-31 HDMI、HID 与 AI MCP 实机闭环

后续处理同类问题前，必须先阅读工程内 Skill：
[AITVBox HDMI/HID Debugging](../.codex/skills/aitvbox-debug-hdmi-hid/SKILL.md)。
相关操作记录、已知良好基线和源码备份脚本均保存在该目录，不依赖用户级
`~/.codex/skills`。

### 10.1 操作目标的屏幕语义

- `http://192.168.1.44/` 浏览器中的实时 HDMI 画面是唯一控制目标。
- 不使用“主屏/副屏”判断动作落点；Windows 的这些标签不能说明采集卡实际看到哪块屏。
- 任务完成必须以结果出现在当前 HDMI 帧中为准。窗口落在其他屏时，先激活窗口，再用
  `Win+Shift+Left/Right` 移入当前帧。
- VMware 抓取键鼠属于宿主/虚拟机输入路由问题，与板端 USB HID 枚举是两个独立层次。

### 10.2 任务栏底部颜色拉伸根因

LT6911/VIN 对外报告 1920×1080，但 NV12 最后 8 行无效。旧规避方案把第 1071 行
复制到 1072–1079 行，虽然盖住了绿色坏行，却把任务栏运行指示器颜色复制成竖条。
正确修复是把 1072 行有效 Y 数据重采样到 1080 行，并把 536 行有效 UV 数据重采样
到 540 行；H.264 编码和 JPEG 快照必须采用同一策略。实现位置：

- `apps/ipkvm/video/main.cpp`
- `apps/hdmi_preview/src/main.cpp`

诊断时必须同时对比板端原始 JPEG 和浏览器 WebRTC 截图：两者都有缺陷时查
VIN/NV12/编码输入；只有浏览器异常时再查 H.264、stride、WebRTC 或 CSS。

### 10.3 鼠标键盘问题的组合根因

本次不是单一故障，而是五个问题叠加：

1. 旧配置曾让键盘、鼠标同时指向 `hidg0`，而浏览器和后端按独立设备解释报告。
2. Linux 4.9 旧 `f_hid` 未完整处理 Windows 使用的 HID class/control 请求。
3. Windows 会按 USB identity 缓存描述符；只改描述符而不改变产品/版本号，重启服务后
   仍可能沿用旧解释。
4. Chromium 在 Win/Meta 快捷键中可能不发送正常 `keyup`，旧逻辑会让 Win 键粘住。
5. VMware 可能截获宿主输入，造成“板端 HID 坏了”的假象。

已知良好量产配置为 `18d1:d015`、`bcdDevice 0x0428`，`/dev/hidg0` 是无 Report ID
的 8 字节 boot keyboard，`/dev/hidg1` 是无 Report ID 的 4 字节 relative mouse，
UDC 状态必须为 `configured`。复合 Report ID 仅用于 service-mode fallback。浏览器必须
在 Meta 快捷键、blur、visibility change 和失焦时主动释放键盘状态。

### 10.4 AI 交互可见性

AI 工作区左侧保留实时 HDMI，右侧按顺序显示任务、抓图、等待、简洁
`OBSERVATION`、动作理由、紧凑 `ACTION`、HID/工具结果和最终验证结果。这里显示的是
可核验的屏幕观察和动作摘要，不暴露模型隐藏思维链。安全的 observation-only 测试必须
能看到真实中文观察内容，并确认没有 HID 动作。

### 10.5 本轮已验证状态

- 主机集成测试 23/23 通过，Go 测试、设备 UI 构建和相关 A133 交叉编译通过。
- 板端 UDC 为 `configured`，键盘 `/dev/hidg0`、鼠标 `/dev/hidg1` 可用。
- 浏览器 WebRTC 以 1920×1080 显示，原始 JPEG 与浏览器任务栏底边均无颜色拉伸。
- AI observation-only 任务成功显示真实观察内容，且未发出 HID。
- 临时 SSH 授权、临时认证和上传文件已清理；原始 IPKVM 认证配置已按哈希恢复。
- 可恢复源码以 `supportappconfig` 远端分支为准；大归档和临时截图不提交到 Git。

### 10.6 2026-07-31 网易云任务耗尽与错屏修正

“播放网易云徐良的歌曲”任务并非服务崩溃，而是连续 20 步未取得可见进展后按上限
进入 `failed`。现场中短促的 HID 报告写入成功，CapsLock LED 也能同步，但 TAB、
Win+R 和普通输入会被当前 Windows 漏掉。将 `Win+D` 报告保持 1 秒后，当前 HDMI
画面立即变化，排除了 VMware 接管和 HDMI/USB 跨主机，根因是按下/释放间隔不足。

后续必须遵守：

- Agent 键盘报告至少保持 250ms，并在 Windows 按键重复阈值之前释放。
- “写入 `/dev/hidg0` 成功”不等于 Windows 已执行，必须检查下一帧。
- 相同 HID 动作连续两次无进展后必须换策略，第三次由执行器拦截。
- 扩展桌面中 `Win+R`、`Win+T`、`Win+D` 都可能改变未采集屏的焦点，不能作为
  自主任务定位应用的手段。网易云原子 `netease_search` 只允许在当前 HDMI 帧已明确
  看见网易云窗口时，聚焦搜索框、输入拼音并提交；它不再承担启动或猜焦点功能。
- 整个任务最多执行两次 `netease_search`，即使中间穿插等待或其他快捷键也不能
  重置计数；CapsLock LED 回读只证明主机消费了 HID 报告，不能证明焦点在 HDMI
  当前画面上。

本轮复发进一步确认：只在模型提示词里写“当前 HDMI 是唯一目标”不够。模型每一帧
都正确描述“未看到网易云”，旧执行器却仍然接受了 `netease_search`、`Win+T`、
`Win+D` 和 `Alt+F4`，导致 Ctrl+F 落到隐藏窗口，之后还把无关的
`BillBoard Device` 属性窗口移入并操作。问题不在本次鼠标缩放或 USB HID 传输，
而在模型输出与 HID 执行之间缺少不可绕过的语义门禁。

现在的执行器规则是：

1. 网易云未出现在当前 HDMI 帧时，拒绝搜索、完成、普通快捷键、键盘输入、鼠标和滚轮。
2. 此时只允许有总次数上限的 `window_cycle`；成功切换一次后，才允许一次
   `move_active_window left|right`，并立即抓新帧验证。
3. 未先切换窗口不得移动；不得用 `Alt+F4`/Escape 关闭无关窗口；连续第三次相同动作
   仍由执行器拦截。
4. Windows 重启或显示拓扑改变后必须重新探测移动方向，不得固定认为目标总在左侧或右侧。
5. 始终找不到目标时失败关闭并释放 HID，不能为了“完成任务”去操作不可见屏。
6. 播放任务只有当前帧同时证明网易云可见和真实播放状态时才允许 `done`；搜索结果页不算完成。

### 10.7 多屏缩放、相对鼠标与网易云播放验收

本机 Windows 的实测显示拓扑不是两个同尺寸的 1920×1080 逻辑屏：

- `DISPLAY1`：主屏，`X=0,Y=0,Width=2048,Height=1152`。
- `DISPLAY2`：HDMI 采集屏，`X=2048,Y=0,Width=1280,Height=720`。
- HDMI 物理采集仍为 1920×1080，因此 `DISPLAY2` 使用了 150% DPI 缩放。

这解释了此前“坐标看似正确却点到空白或另一项”的现象：采集像素、Windows 逻辑
坐标和相对 HID 位移不是 1:1。不能把截图 `(x,y)` 直接加到虚拟桌面原点，也不能从
全局边缘锚定后假设一个 HID 单位等于一个截图像素。

可靠恢复流程是：

1. 用慢速相对移动让 Windows 光标出现在新抓取的原始帧中。
2. 只根据该帧中“可见光标 → 可见按钮”的局部差值移动。
3. 点击后抓取两帧；静止时两帧 SHA 相同，只有动画、进度或状态变化时才继续判断。
4. 不使用隐藏主屏上的 `Win+R`/PowerShell 作为 HDMI 操作成功证据。
5. 不相信模型对底部小字的单帧 OCR；搜索页、选中行和大“播放”按钮都不是播放成功。

2026-07-31 最终实机闭环进入徐良歌手页并点击“播放全部”。原始 HDMI 帧明确显示：

- 底部曲目歌手包含“徐良”；
- 中央控制由红色播放三角变为暂停双竖线；
- VIP 试听弹窗用 `ESC` 关闭后，底部仍保持暂停双竖线。

最终干净帧板端 SHA-256：
`d76c3417a53932f7d0f26458e24f55da9ff8a32b9ab8c2b3d79387940a5fc6ed`。
本轮使用的诊断源码保存在 `tools/board/hid_relative_slow.c` 和
`tools/board/hid_keyboard_type.c`；它们不改变生产 USB 描述符，前者用于低速局部
光标校准，后者用于按指定保持时间发送受控 ASCII 键盘报告。

### 10.8 错屏门禁实机回归

使用同一任务“打开网易云播放音乐 那时雨 徐良的”回归时，当前 HDMI 帧只显示
PowerShell 或桌面。最终执行器只放行了有界的 `window_cycle` 和已由切换动作授权的
左右移窗；连续第三次切换被拒绝，搜索、点击、输入、关闭窗口和完成均未执行。验证后
主动停止任务并发送全键、鼠标释放。平台主机集成测试已增至 28 项并全部通过，覆盖：
目标缺失、盲目快捷键、未授权移窗、可见目标搜索和播放完成证据。
