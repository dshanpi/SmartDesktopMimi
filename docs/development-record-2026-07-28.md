# AITVBox 开发与实机验证记录（2026-07-28）

## 1. 记录用途

本文记录本轮 A133 开发板功能修改、问题根因、已经完成的验证、最终固件信息，以及虚拟机重启后需要继续执行的实机回归流程。

当前源码工作区：

```text
/home/ubuntu/AI-DeskTopBox
```

Tina SDK：

```text
/home/ubuntu/A133-Tina5.0-v0.9
```

开发板最近使用的局域网地址：

```text
192.168.1.44
```

测试网络 SSID 为 `Programmers7`。网络密码、IPKVM 临时密码、模型 API Key 等秘密不写入仓库，续测时从操作者提供的安全来源获取。

记录时的 Git 基线：

```text
1772b34c525c168972c2a1f1e1ce935d16cfe100
```

当前工作树为 `dirty`，包含本轮功能修改以及工作开始前已经存在的修改；尚未自动提交，避免把用户原有改动混入未经确认的提交。当前主仓库约有 64 项修改或新增路径，已跟踪 diff 约为 54 个文件、1711 行新增、680 行删除。LVGL 子模块基线为：

```text
eca785206815844c5ffe767c07e10c9102fd83a5
```

LVGL 子模块内部另有本轮 MT-only evdev 修改。虚拟机重启后必须保留整个工作目录，禁止使用 `git reset --hard`、`git checkout --` 或清理未跟踪文件。

## 2. 用户目标

1. 修复 `http://192.168.1.44/mode` 无法显示 HDMI 画面的问题。
2. 将 HDMI + HID MCP/Agent 完全集成到 IPKVM 浏览器设置，不再使用桌面“电脑”应用。
3. 验证用户新增应用功能，使用签名安装的 2048 应用进行真实测试。
4. 确保用户应用嵌入“应用”区域，不越界、不破坏桌面其他 UI。
5. 优化顶部任务栏，做成类似 Apple/Android 的下拉控制中心。
6. 结合硬件能力检查 HDMI、HID、USB Gadget、ADB 和蓝牙驱动/服务状态。
7. 构建、全量刷写最终固件，并完成刷写后的整机回归。

## 3. 已实现修改

### 3.1 IPKVM `/mode` 与 WebRTC

- 修复首次设置、登录和路由跳转流程。
- 修复 stale ref、pending ICE 和页面重新连接竞态。
- Wi-Fi 获得有效 IP 后创建或刷新 MultiUDPMux。
- 修复 IPKVM Unix Socket 重启时旧进程误删新监听 socket 的竞态。
- 增加基于 inode 所有权的 listener 包装及回归测试。
- 浏览器实际视频验证曾达到 `1920x1080`，`readyState=4`，播放时间持续推进。

相关主要文件：

```text
apps/ipkvm/upstream/web.go
apps/ipkvm/upstream/webrtc.go
apps/ipkvm/upstream/native.go
apps/ipkvm/upstream/native_socket_test.go
apps/ipkvm/upstream/cmd/board-webrtc-smoke/
tools/board/ipkvm_browser_smoke.mjs
```

### 3.2 浏览器内 HDMI + HID MCP/Agent

- 删除桌面“电脑”应用入口及实现。
- 在 IPKVM PC 和移动端设置中增加“HDMI + HID MCP”设置页。
- 支持浏览器配置模型 endpoint、model 和 API Key。
- 支持启动、紧急停止和状态查询。
- Agent 启动时暂停浏览器 WebRTC；停止后恢复实际 HDMI 视频。
- 增加前端 HTTP JSON-RPC 30 秒 `AbortController` 超时，避免弱 Wi-Fi 下请求无限挂起。
- 增加 Agent/HDMI 进程内资源预留，防止 WebRTC 在 Agent 启动期间抢回 HDMI。
- Agent 启动不再只检查 socket 是否可连接，而是等待并验证真实 JPEG 快照。

相关主要文件：

```text
apps/ipkvm/upstream/agent_control.go
apps/ipkvm/upstream/agent_control_test.go
apps/ipkvm/upstream/platform_a133.go
apps/ipkvm/upstream/platform_hid_a133.go
apps/ipkvm/upstream/ui/src/layout/components_setting/agent/
apps/ipkvm/upstream/ui/src/hooks/useJsonRpc.ts
tools/board/ipkvm_agent_browser_smoke.mjs
```

### 3.3 HDMI Headless Capture

- `hdmi_preview --service` 不再初始化本地显示层，避免与桌面显示冲突。
- 快照生成与本地显示 buffer 解耦。
- 忽略 `SIGPIPE`，避免客户端超时断开后服务被响应写操作杀死。
- 快照客户端超时调整为 7 秒，整体就绪窗口调整为 15 秒。
- Agent 停止后确保清理 headless capture 服务及资源预留。

相关文件：

```text
apps/hdmi_preview/src/main.cpp
```

### 3.4 HID、USB 和进程生命周期

- `/dev/hidg0` 使用带 Report ID 的键盘/鼠标复合 HID。
- HID shell、Go 平台层及服务进程均识别 zombie，不再把僵尸进程当作活动锁持有者。
- `aitvbox-hidctl` 写入前检查 UDC 必须处于 `configured`，USB Host 未连接时立即返回，不再永久阻塞在内核 `f_hidg_write`。
- Go HID 打开使用 `O_NONBLOCK`。
- `controld` 停止 Agent 时使用有界同步流程：
  - `SIGTERM`
  - 最多等待 3 秒
  - 必要时 `SIGKILL`
  - `waitpid` 回收
  - 状态恢复为 `idle`
- 处理 `ECHILD`，避免停止接口卡住。

相关主要文件：

```text
packaging/aitvbox-usb-hid/files/aitvbox-hidctl
packaging/aitvbox-usb-hid/files/aitvbox-usb-hid-test
apps/ipkvm/upstream/platform_hid_a133.go
apps/ipkvm/upstream/platform_hid_a133_test.go
apps/platform_services/src/agentd.c
apps/platform_services/src/controld.c
apps/platform_services/src/mcpd.c
```

### 3.5 MT-only 触摸支持

开发板触摸设备实际只报告：

```text
ABS_MT_POSITION_X
ABS_MT_POSITION_Y
```

旧逻辑依赖 legacy `ABS_X/ABS_Y`，导致部分触控功能不可靠。现已修改 LVGL evdev 驱动：

- 自动识别 MT-only 输入设备。
- 使用 MT 坐标范围进行校准。
- 支持 Type-B multitouch slot。

已增加完全模拟该硬件能力的测试工具：

```text
tools/board/uinput_touch.c
tools/board/touch_tap.c
```

LVGL 子模块内修改：

```text
apps/lv_port_linux/lvgl/src/drivers/evdev/lv_evdev.c
```

### 3.6 顶部下拉控制中心

顶部状态栏已改造成类似 Apple/Android 的下拉控制中心：

- 半透明全屏遮罩。
- 居中的深色圆角控制面板。
- 打开/关闭动画。
- Wi-Fi、HDMI、用户应用和设置快捷卡片。
- 电源状态显示。
- 点击遮罩关闭。
- 上滑关闭。
- 不修改桌面原有几何布局。

相关文件：

```text
apps/lv_port_linux/src/ui/desktop_status_bar.c
```

真实 framebuffer 截图曾保存为：

```text
/tmp/fb-mt-control.png
```

该 `/tmp` 文件可能在主机重启后消失，但测试结果已记录：MT-only 触控可以打开控制中心，SSID 文案经两行布局修正后不再裁切。

### 3.7 用户应用与 2048

- 增加真实 2048 Native RPC 示例应用。
- UI presentation 强制为 `embedded`，禁止全屏/外部应用影响桌面。
- 应用 UI 放入固定容器，限制宽高和滚动行为。
- 2048 的中文按钮使用包含 CJK 字符的主题字体。
- 新游戏、上、下、左、右按钮已通过触摸点击验证。
- 应用状态使用 `storage.app`，保存在应用独立数据目录。
- 修复旧 Tina 内核没有 `NoNewPrivs` 字段时的 appd 沙箱检查：只有 seccomp、UID、GID 和父进程检查均通过时才接受该内核差异。

示例应用：

```text
platform/app-sdk/examples/2048/
```

主要运行时和 UI 文件：

```text
apps/lv_port_linux/src/apps/user/user_app.c
apps/platform_services/src/appd.c
platform/runtime/aitvbox-appctl
```

应用数据验证路径：

```text
/overlay/aitvbox/apps/com.100ask.game2048/data/game-state.value
```

已验证：

- 文件权限 `0600`。
- appd 重启后状态仍存在。
- 应用内容不超出 `1024x768` 容器。
- 应用页面不影响桌面其他 UI。
- 签名、信任根、安装和策略拒绝流程通过。

### 3.8 蓝牙

- 蓝牙初始化即使发现 `bluetoothd` 已运行，也会继续校验并启动 `hci0`。
- 实机曾验证：
  - `hci0` 为 `UP RUNNING`
  - Powered
  - SSP
  - BR/EDR
  - LE
  - Secure Connections
- BlueZ 暴露：
  - PnP Information
  - AVRCP Target
  - AVRCP Controller
  - Audio Sink

相关文件：

```text
packaging/aitvbox-suite/files/aitvbox-bluetooth.init
packaging/aitvbox-suite/files/bt_init.sh
docs/bluetooth.md
```

### 3.9 USB Gadget 与 ADB

实机刷写前状态曾验证：

```text
idVendor  = 0x18d1
idProduct = 0xd003
functions = ffs.adb + hid.composite
device    = /dev/hidg0
adbd      = running
```

当时 UDC 为 `not attached`，原因是 VMware/物理 USB Host 没有接管设备，并非 Gadget 配置缺失。最终仍需在 full flash 后进行一次物理冷启动和 Host 侧枚举验证。

## 4. 已完成测试

### 4.1 主机自动化

以下测试在最终构建前已通过：

```text
/usr/local/go/bin/go test ./...
python3 -m unittest tests.platform.test_host_integration
git diff --check
```

其中 Host Integration 共 19 项通过。

同时通过：

- ARM64 平台服务构建。
- ARM64 `hdmi_preview` 构建。
- LVGL ARM64 构建。
- IPKVM UI `npm run build:device`。
- IPKVM Unix socket 重启竞态测试。
- HID zombie/lock 回归测试。
- Agent HDMI 资源预留测试。

UI 构建存在既有 CSS optimizer 警告和 browserslist 数据提示，但构建成功，不是本轮代码错误。

### 4.2 板端功能验证

在最终 full flash 之前已通过：

- `/mode` 浏览器真实 HDMI 视频播放。
- 三次连续 IPKVM 重启后 socket 正常恢复。
- 浏览器 Agent 启动、紧急停止、恢复视频完整流程。
- Agent 测试结果包含 `startedState=running`、`stoppedState=idle`。
- Agent 运行和停止后无横向页面溢出。
- Headless snapshot 输出有效 JPEG。
- HDMI snapshot 服务在客户端断开后仍存活。
- USB 未连接时 HID 命令快速失败且不遗留 lock。
- MT-only 触控打开顶部控制中心。
- MT-only 触控打开并操作 2048。
- 2048 状态持久化。
- 蓝牙 hci0 和音频 UUID。
- USB Gadget/ADB/HID 板端自检。

## 5. 最终构建产物

最终应用构建命令：

```bash
cd /home/ubuntu/AI-DeskTopBox
./scripts/build_apps.sh /home/ubuntu/A133-Tina5.0-v0.9
```

结果：成功。

最终固件构建命令：

```bash
cd /home/ubuntu/AI-DeskTopBox
./scripts/build_firmware.sh /home/ubuntu/A133-Tina5.0-v0.9
```

结果：成功；脚本完成最终 ext4 rootfs、产品服务权限和打包内容校验，并恢复 SDK pure mode。

固件路径：

```text
/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/a133_linux_b6_uart0.img
```

固件大小：

```text
556316672 bytes
```

固件 SHA-256：

```text
9684a42c63de106b3ad8463ea0d085b1504eb9cb73149c7149bf0cc82e56a6f5
```

## 6. 当前板卡与刷写状态

最后操作：

1. 通过串口 Agent 执行 `sync; reboot efex`。
2. 开发板成功进入 FEL。
3. OpenixCLI 首次检测到唯一一台设备：

```text
VID:PID = 1f3a:efe8
Mode    = FEL
```

4. 启动 full-erase 刷写。
5. OpenixCLI 成功完成 DRAM 初始化并将临时 U-Boot 下载到板上。
6. U-Boot 进入 `run usb efex` 后，VMware 因 USB 重新枚举丢失设备转发。
7. 重新连接后，虚拟机只看到 ROM FEL 模式，原刷写进程无法续接到 U-Boot 烧写协议。
8. 操作者准备重启虚拟机，因此已使用 `Ctrl-C` 安全停止等待进程。

重要结论：

- 固件数据尚未开始写入 eMMC。
- 未执行擦除。
- 板卡仍处于 FEL。
- 源码、应用产物和最终固件均已落盘。

## 7. 串口 Agent 使用要求

不要直接打开 `/dev/ttyUSB0`，所有串口操作必须通过 SDK 中的串口 Agent：

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
python3 tools/serial_agent/serial_agent_client.py status
python3 tools/serial_agent/serial_agent_client.py cmd '命令' --wait 3
python3 tools/serial_agent/serial_agent_client.py tail -n 100
```

相关技能说明：

```text
/home/ubuntu/A133-Tina5.0-v0.9/skills/a133-serial-agent-daemon/SKILL.md
/home/ubuntu/A133-Tina5.0-v0.9/skills/a133-build-flash/SKILL.md
```

## 8. 虚拟机重启后的续接流程

### 8.1 确认串口 Agent

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
python3 tools/serial_agent/serial_agent_client.py status
```

如果 daemon 未运行，严格按 `a133-serial-agent-daemon` 技能启动，禁止多个进程同时占用串口。

### 8.2 确认 FEL USB

在 VMware 中把 Allwinner FEL USB 连接到虚拟机，然后运行：

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
sudo tools/OpenixCLI/target/release/openixcli scan -l
```

必须确认只存在一台目标设备，模式为 FEL。

### 8.3 重新开始 full-erase

```bash
cd /home/ubuntu/A133-Tina5.0-v0.9
sudo tools/OpenixCLI/target/release/openixcli flash \
  out/a133/b6/openwrt/a133_linux_b6_uart0.img \
  --mode full_erase \
  --reconnect-timeout-sec 240 \
  --reconnect-interval-ms 300 \
  -v
```

注意：

- 从头重新执行，不尝试续接之前已终止的进程。
- 临时 U-Boot 重新枚举时，VMware 可能再次出现一个 Allwinner USB 设备。
- 必须立即将该新枚举设备连接到虚拟机。
- 等待 OpenixCLI 明确报告写入和校验成功。
- full flash 后按技能要求进行物理断电冷启动；软件 reboot 不能替代 USB PHY/UDC 冷复位。

## 9. Full flash 后必须完成的回归

### 9.1 恢复网络

full flash 会清空 overlay、Wi-Fi、IPKVM 配置、SSH 测试密钥、2048 安装和应用数据。

通过串口 Agent 配置测试 SSID，密码不要写入命令历史之外的仓库文件。确认：

```text
wlan0 获得 IPv4
ping 网关成功
IPKVM 监听端口正常
```

### 9.2 IPKVM 首次设置

- 访问 `/setup`。
- 设置仅用于回归的临时密码。
- 验证 `/login`。
- 验证 `/mode`。

回归结束必须删除临时 IPKVM 配置，使交付状态回到首次设置页。

### 9.3 `/mode` 视频

运行：

```bash
cd /home/ubuntu/AI-DeskTopBox
node tools/board/ipkvm_browser_smoke.mjs
```

必须确认：

- video `readyState=4`
- `currentTime` 持续递增
- 实际尺寸 `1920x1080`
- 页面无横向溢出
- 浏览器截图包含真实 HDMI 画面

### 9.4 浏览器 Agent

运行：

```bash
cd /home/ubuntu/AI-DeskTopBox
node tools/board/ipkvm_agent_browser_smoke.mjs
```

必须确认：

- 浏览器设置中存在 HDMI + HID MCP。
- 不存在桌面“电脑”应用。
- 保存 provider/model/key 正常。
- start 后状态为 `running`。
- HDMI 画面被 Agent 独占。
- emergency stop 后状态为 `idle`。
- WebRTC 实际视频自动恢复。
- 页面无横向溢出。
- 结束后默认 endpoint/model 恢复。
- `/overlay/aitvbox/secrets/model-api-key` 不存在。
- `/var/run/aitvbox/hid.lock` 不存在。

### 9.5 IPKVM 重启竞态

连续重启 IPKVM 服务至少三次，每次验证：

- Unix socket 存在。
- 新服务进程存活。
- HTTP 页面恢复。
- `/mode` 可以重新播放。

### 9.6 顶部控制中心

使用 MT-only uinput 工具模拟与板载触摸完全相同的输入能力，验证：

- 点击顶部状态栏打开控制中心。
- 动画和遮罩正常。
- Wi-Fi、HDMI、用户应用、设置卡片不裁切。
- 点击遮罩关闭。
- 上滑关闭。
- 打开/关闭不改变桌面几何布局。
- 保存 framebuffer 截图并人工检查。

### 9.7 2048 用户应用

重新生成临时测试签名密钥和公钥，构建并签名：

```text
platform/app-sdk/examples/2048/
```

把公钥放入：

```text
/overlay/aitvbox/trusted-app-keys/
```

使用 `aitvbox-appctl install` 安装签名 `.aitapp`，验证：

- 应用出现在“用户应用”区域。
- 点击后只在应用容器内显示。
- 所有中文按钮完整显示。
- 新游戏、上、下、左、右可点击。
- 棋盘和分数不越界。
- 桌面顶部栏和其他区域未被覆盖。
- 应用状态写入独立 data 目录。
- 权限为 `0600`。
- appd 重启后状态仍存在。
- 未信任签名的应用被拒绝。

### 9.8 蓝牙

验证：

```text
hci0 UP RUNNING
powered
ssp
br/edr
le
secure-conn
Audio Sink UUID
AVRCP Target/Controller UUID
```

### 9.9 USB Gadget、ADB 和 HID

物理冷启动并让 Host/VMware 接管产品 USB 后验证：

```text
UDC state = configured
VID:PID = 18d1:d003
ffs.adb 已绑定
hid.composite 已绑定
/dev/hidg0 存在
adbd 运行
Host 能发现 ADB
键盘和鼠标 Report ID 正常
```

拔掉 Host 后再次验证 HID 命令快速失败且不遗留 lock。

### 9.10 服务和残留审计

确认：

- 无遗留 `hdmi_preview --service`。
- 无遗留 Agent 子进程。
- 无 zombie 被误判为活跃进程。
- 无 `/var/run/aitvbox/hid.lock`。
- Agent 状态为 `idle`。
- 模型 API Key 文件不存在。

## 10. 最终清理

全部回归通过后：

1. 删除测试模型 API Key。
2. 删除临时 SSH authorized key。
3. 删除 IPKVM 临时配置并重启 IPKVM，使其回到首次设置状态。
4. 删除 `/tmp` 中的测试工具和截图。
5. 保留已安装的签名 2048 作为用户应用能力演示，除非交付要求必须为完全空白 overlay。
6. 再次记录最终固件 SHA-256、板端版本、服务状态和所有回归结果。

## 11. 已知环境因素

- 板端 Wi-Fi 信号曾约为 `-77 dBm`，SSH/HTTP 偶尔延迟 7 至 30 秒。
- 弱 Wi-Fi 下浏览器 RPC 可能变慢；前端已经增加 30 秒超时。
- 板端诊断优先使用串口 Agent，避免把网络抖动误判为服务故障。
- VMware 会在 FEL、临时 U-Boot 烧写模式和产品 USB Gadget 之间切换 USB 枚举，可能需要人工重新连接到虚拟机。
- 物理冷启动不能由软件 reboot 等价替代。

## 12. Full flash 后续验证与方舟兼容

虚拟机恢复后，板卡已经从最终固件正常启动并重新获得 `192.168.1.44`。本轮继续
完成了以下验证：

- USB Gadget 为 `configured`，复合 HID 已绑定，`/dev/hidg0` 存在。
- MCP `initialize`、`tools/list`、`hardware.capabilities` 均返回合法响应。
- `computer.capture` 返回 JPEG；解码后的实机截图为 10,449 字节，文件头为
  `FF D8`。
- `computer.key` 的全释放报告和 `computer.mouse` 的零位移/全释放报告均返回
  `OK`。
- 验证结束后 `hdmi_preview --service` 已停止，`hid.lock` 和 `agent.lock` 均无
  残留，IPKVM HTTP 恢复为 200。

模型兼容修改：

- API Key 不再硬编码限定为 `sk-`/`SK-`，现在接受 12..4096 字节的可打印服务商
  Key，包括方舟 `ark-`。
- provider 可填写完整 Chat Completions endpoint，也可直接填写以 `/v1` 或 `/v3`
  结尾的 OpenAI-compatible Base URL；Agent 自动补齐 `/chat/completions`。
- 浏览器对 embedding 模型显示警告，控制服务也会拒绝用 embedding 模型启动
  HDMI/HID 动作循环。

方舟公网实测：

- Base URL：`https://ark.cn-beijing.volces.com/api/coding/v3`
- 模型：`doubao-embedding-vision`
- `/embeddings` 成功返回 1 条 2048 维向量。
- `/responses` 返回 `AccessDenied`，服务端明确说明该 embedding 模型没有
  Responses API 访问能力。因此它只能用于向量检索，不能代替视觉控制生成模型。

板端已热部署新的 `aitvbox-agentd`、`aitvbox-controld`、`aitvbox-agentctl` 和
`aitvbox-ipkvmd`，并保存上述 Base URL、模型和测试 Key；两个配置文件均为
`0600`。部署前版本备份在：

```text
/overlay/aitvbox/hotfix-backup-20260728-0508/
```

临时 SSH 部署公钥和主机私钥均已删除。API Key 本身不记录在本文、日志或固件中。

包含上述修改的新完整固件已经通过 Tina 全量构建、HID/SECCOMP 内核配置检查、
ext4 rootfs 权限审计和最终打包：

```text
/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/a133_linux_b6_uart0.img
size:   556316672 bytes
sha256: a1eb72e635b7d19e964a79e2d8473be8f179b1572d1515d1324bf349941ae568
```

从最终 `rootfs.img` 反向提取并检查后，确认：

- `aitvbox-agentd` 包含 Base URL 自动补齐 `/chat/completions` 的实现。
- `aitvbox-controld` 包含 embedding 控制任务拒绝逻辑。
- `aitvbox-agentctl` 与当前源码 SHA-256 一致。
- `aitvbox-ipkvmd` 内嵌新的 embedding 警告 UI。

当前板端运行的是 overlay 热部署版本；新固件已生成但本轮没有再次擦写 eMMC，
避免破坏刚完成的配置和实机验证状态。

## 13. 浏览器控制台、HID 透传和用户应用收尾

针对 `http://192.168.1.44/` 的画面跳动、浏览器键鼠无响应、Agent 无历史记录和
用户应用越界，本轮继续完成并热部署了以下修改：

- 去掉虚拟键盘和终端隐藏态各自遗留的 `-500px` 负外边距；此前两项会让视频
  主区域额外增高 1000px，把新状态栏和输入提示挤出可视区。
- 视频容器改为固定可用空间内 `object-contain`，不再用同一元素的
  `ResizeObserver` 测量值反向控制自身尺寸，消除尺寸反馈循环和滚动条抖动。
- 控制台增加 `LIVE`、HDMI 分辨率、USB Gadget 和 HID DataChannel 状态，并明确
  提示点击画面后才接管浏览器键盘与鼠标。
- 浏览器键盘只在视频获得焦点或指针锁定时透传，拦截浏览器默认快捷键，并在失焦
  时释放按键；鼠标滚轮改用非 passive 监听，鼠标移动在不可靠通道缺失时回退到
  可靠 HID 通道。
- IPKVM 启动时和 RPC 通道建立时主动读取真实 UDC 状态。板端为
  `configured` 时不再因前端保留 `unknown` 而静默丢弃输入。
- “HDMI + HID MCP”页增加对话与执行时间线，记录用户任务、HDMI 抓图、模型决策、
  HID 结果、完成和错误事件。即使 embedding 模型拒绝启动，也会记录用户请求和
  明确的系统错误。
- “暂停直播并启动”现在由服务端主动关闭对应 WebRTC 会话并等待 HDMI 释放，修复
  仅关闭浏览器本地 peer 导致设备端会话长期占用 HDMI 的竞态。
- 用户应用容器关闭向外 overflow 和 scroll chain，页面主体使用受限 flex 宽高；
  2048 示例保持在“用户应用”容器内，不覆盖设备桌面。
- 浏览器“Applications”页直接给出生成 3072-bit RSA 密钥、交叉编译、签名打包、
  上传公钥和安装 `.aitapp` 的四步示例。完整说明见
  `docs/examples/user-app-2048-upload.md`。

板端实测结果：

- Playwright 在 1440×900 下连续采样视频框 24 次，`x/y/width/height` 最大波动
  均为 `0`，页面宽高均无 overflow。
- 实机 WebRTC 解码为 1920×1080，`readyState=4`。
- 浏览器发送 Shift 按下/释放和鼠标移动后记录到 3 个 HID DataChannel 报文，
  同时覆盖可靠 `hidrpc` 和不可靠低延迟通道。
- MCP `tools/list` 返回 capture/key/mouse/capabilities/stop 五个工具；
  键盘全释放、鼠标零位移全释放和 `safety.stop` 均返回 `OK`。
- HDMI 接管、embedding 模型拒绝和历史刷新完整走通，最终状态为 `failed`，历史
  含 1 条用户任务和 1 条系统错误，没有发送实际键鼠动作。
- 签名应用 `com.100ask.game2048` 版本 `1.0.0` 已通过浏览器 API 安装并保留；
  `new`、`left` RPC 均返回合法棋盘状态。
- Go 单元测试、前端 device build、平台服务 AArch64 交叉编译、20 项主机集成测试
  及 `git diff --check` 均通过。

最终清理时恢复了原 IPKVM 认证配置（恢复前后 SHA-256 一致），撤销临时 SSH
公钥并删除测试密码、测试 Cookie 和应用签名私钥。重启后重复的 framebuffer
进程已回收，只剩一个 `lvglsim`。Gadget 仍绑定
`5100000.udc-controller`，`hid.composite` 和 `/dev/hidg0` 均存在；但 UDC
状态为 `not attached`，表示重启后外部 Host/VMware 尚未重新接管产品 USB。
Host 重新连接该 USB 设备后应恢复为 `configured`，届时无需再次部署软件。

本轮热部署二进制 SHA-256：

```text
aitvbox-ipkvmd  9074854c3244173ef77613f67ba45a233f484ef411ecbc59cdcb8b28f276a9ce
aitvbox-controld 4eeb4698f77b65c4593b31a702575234fb7ac9fc4d30368232e572ecc65cb7cb
aitvbox-agentd   ab6d923493603fcdf62d8b900bf6bd1e6517e6ad2131393e500c571f772db0d0
lvglsim          099e374b857f2891620d06feccc4206626ac48eb9fa2ba7931db6ec9ef2efe33
```

## 14. 物理屏用户应用与浏览器闪帧修复

物理屏主界面的“用户应用”入口改为原生 LVGL 绘制的文件夹/应用宫格图标。Dock
在六个入口时按可用宽度计算步进，第六个入口不再越过 720px 轨道或侵入右侧动画
区域。用户应用页不再混用固定宽度、百分比宽度和 flex grow，而是根据
1024×768 显示尺寸显式计算左右栏坐标，并对列表、内容卡片及按钮启用边界裁剪。

板端 framebuffer 直读验证结果：

- 主界面六个入口全部位于 Dock 轨道内，“用户应用”图标完整可见。
- “用户应用”页左栏位于 `x=24..280`，右侧应用卡片位于 `x=304..1000`。
- 已安装的 2048 示例标题、说明和五个控制按钮均完整位于应用卡片内。
- 双缓冲两页逐像素一致，说明抓帧时页面自身没有撕裂或越界。

浏览器视频继续排查时，板端日志确认旧实现把 Unix socket 的到包间隔直接作为
WebRTC sample duration：第一次观看前视频空闲约 41 分钟，首帧因此得到
`duration_ms=2487868`；后续编码回调又在 `1ms` 和 `50..60ms` 间突跳。该时间轴
会让浏览器 jitter buffer 抖动。修复后：

- A133 硬件编码器从无法持续达到的 60fps 调整为稳定 30fps。
- 结合板端 `-74dBm`、约 `29.3Mbit/s` 的 Wi-Fi 上行实测，将 1080p 码率从
  8Mbit/s 调整为 5Mbit/s，降低弱信号下的分片丢包和参考帧损坏。
- WebRTC RTP sample duration 使用 1/8 平滑系数跟随真实编码吞吐；超过 250ms
  的空闲间隔不会进入 RTP 时间轴。板端长期实测编码吞吐约 22.3fps，因此不能
  固定假设为 30fps 或 60fps。
- IDR 间隔同步调整为 30 帧，实际约每 1..1.5 秒一个关键帧。
- 编码端所有长度头和 H.264 数据改为循环完整写入，防止大 IDR 帧发生部分写入后
  破坏后续帧边界。
- 编码器把 SPS 与 PPS 放在同一个 24 字节 Annex-B 包中；后端现在只缓存和拼接
  一次该参数集，不再在每个 IDR 前生成
  `[SPS+PPS][SPS+PPS][IDR]` 并周期性重置浏览器解码器。

Go 全量测试、前端 device 生产构建、A133 编码器交叉编译及 `git diff --check`
均通过。热部署后的运行版本：

```text
aitvbox-ipkvmd     420888f2528caf2ee8825438380332a18500a961737f06d72e07200e41fec147
aitvbox-kvm-video  ee0b00a402ff5ecca396b109e13449391b9da29b614cf5e82750be4649d8edbf
lvglsim             a8e3185344c71bb748054dc2129be3c23b955a475f7848504f119bcf5bd8c4b7
```

部署前的 IPKVM 二进制保存在
`/overlay/aitvbox/hotfix-backup-20260728-webrtc-flicker/`，物理屏二进制保存在
`/overlay/aitvbox/hotfix-backup-20260728-user-ui/`。

## 15. 直播常驻、按需 AI 快照和双端 HDMI MCP 工作区

针对“直播不能为 AI 降帧或被接管、A133 需要显示对话流、浏览器功能需要分区”
的后续要求，已完成新的并行架构：

- HDMI 输入和硬件 H.264 编码目标恢复为 `1920x1080@60`、12 Mbit/s。
- 编码进程为每个 H.264 包附加采集时钟微秒时间戳；Go/WebRTC 使用采集时间轴，
  不再从 Unix socket 的突发到包间隔猜测 RTP duration。
- 每个 WebRTC Session 使用有界异步视频队列。弱网发送只丢弃已经过期的待发帧，
  不再让同步 `WriteSample` 反压 Unix socket、硬件编码器和 HDMI 采集循环；
  新 IDR 会先清理队列中的旧帧，客户端恢复时从完整参考帧继续解码。
- `aitvbox-kvm-video` 内增加共享快照 socket。只有 AI 明确请求时才复制一帧，
  JPEG 缩放和编码在独立工作线程中进行。
- Agent 作为共享采集管线的引用计数消费者，不再关闭浏览器 PeerConnection，
  不再另起 headless HDMI 服务，也不再预留并独占 `/dev/video0`。
- 浏览器把 Live、AI HDMI MCP、Applications 和 Settings 分成顶层工作区。
  AI/应用管理显示在独立右侧面板，直播 `<video>` 节点保持挂载。
- A133 桌面新增“HDMI MCP”应用。应用轮询同一个 `controld` 状态/历史源，任务
  进入 running/stopping 时自动唤醒，并按 user/assistant/tool/system 展示对话
  和执行事件，同时提供本地急停。

本地验证已完成：

```text
Go tests                 PASS
IPKVM video cross-build  PASS
Browser device build     PASS
A133 LVGL full link      PASS
```

设备重新上电后，已在 `192.168.1.44` 完成热部署和实机回归：

- HDMI 驱动识别输入为 `1920x1080@60`。浏览器连接期间采集循环
  `41.489s / 2485 帧`，实测约 `59.9fps`；网络传输不再降低采集端帧率。
- 浏览器画面解码为 `1920x1080`、`readyState=4`。连续 24 次布局采样的
  `x/y/width/height` 差值均为 0，没有因工作区切换造成 `<video>` 重建或跳动。
- 浏览器鼠标/键盘事件记录到可靠 `hidrpc` 和不可靠低延迟 HID DataChannel；
  USB UDC 实测为 `configured`。
- 在 AI 工作区显式请求快照后，`/var/run/aitvbox/screen.jpg` 成功更新。请求前后
  PeerConnection 数量均为 1、连接状态保持 `connected`，视频时间从
  `0.030s` 前进到 `1.857s`。
- 当前保存的 `doubao-embedding-vision` 被明确拒绝用于电脑控制，历史中显示
  “embedding models cannot generate computer-control actions”；它仍可保留用于
  embedding API 验证，不会误发 HID。
- 使用非 embedding 的隔离测试配置启动任务时，A133 自动打开“HDMI MCP”应用。
  framebuffer 实图确认其中同步显示用户任务、HDMI 截图、AI 分析和终止事件；
  测试未发送任何键鼠动作。
- 测试后已按 SHA-256 原样恢复 IPKVM 认证配置和模型配置，API key 文件未改动；
  临时测试密码、Cookie、SSH 私钥、公钥授权和上传文件均已删除。

实机运行二进制 SHA-256：

```text
aitvbox-ipkvmd     1b86667398bf32b884e35ba76deec40d41ff6e089cd0966e95f0c4868fe4cd55
aitvbox-kvm-video  6b1e51f1b84ec072974ab53d01e47f9440d573aa0b87166482f6dccc1eaed353
lvglsim             344f7198dd7a84b000e763307059ac6efaf9427657964cb34aa6d9a06b065198
```

部署前二进制保存在
`/overlay/aitvbox/hotfix-backup-20260728-live-mcp/`，用于需要时手工回滚。

## 16. 浏览器鼠标端到端透传修复

用户实测发现浏览器能显示直播，但画面内鼠标移动和点击没有到达被控电脑。进一步
验证发现 USB Gadget 本身正常：UDC 为 `configured`，直接写入复合 HID 鼠标
报告时 `sunxi_usb_udc` 中断计数随每个报告增长。故障位于浏览器默认“绝对鼠标”
路径：它把坐标发到 `hidrpc-unreliable-ordered`；之前的自动化只记录了浏览器
调用 `RTCDataChannel.send()`，没有验证 Pion 和 USB Host 实际收到报文。

修复后：

- 绝对鼠标坐标改走可靠 `hidrpc`，防止丢失一个坐标后破坏后续相对增量基线。
- 相对鼠标和滚轮也允许在长连接只丢失握手回复时走同协议兼容发送，避免静默
  no-op；底层通道仍必须处于 `open`。
- 浏览器 smoke test 单独截取鼠标发送阶段，并要求所有鼠标报文都只使用可靠
  `hidrpc`。
- 新增只移动真实页面指针的独立测试，不发送键盘或点击动作。

实机鼠标独立测试结果：

```text
PointerReport(type=3)  7 reports, all on hidrpc
sunxi_usb_udc IRQ      3830 -> 3857
USB UDC state          configured
Browser video          1920x1080, readyState=4
```

运行二进制 `aitvbox-ipkvmd` SHA-256 为
`6e3194740fef692cf877579685c0866dabfad21a4d9a77a3b1941698e42408b2`；
修复前版本保存在
`/overlay/aitvbox/hotfix-backup-20260728-live-mcp/aitvbox-ipkvmd.pre-hid-fix`。

## 17. Windows 与板卡冷启动后的 HID 续测（2026-07-31）

操作者重新上电 A133 并重启 Windows 后，Ubuntu 虚拟机中的串口节点变为
`/dev/ttyUSB1`。串口 Agent 已按 single-owner 规则恢复，板卡仍使用
`192.168.1.44`，IPKVM HTTP、视频进程、桌面进程和控制服务均正常启动。

冷启动时板端最初恢复的是旧生产拓扑：

```text
VID:PID       18d1:d004
functions     hid.composite
nodes         /dev/hidg0
UDC state     configured
mouse mode    relative
```

该拓扑能完成 Windows 枚举，写入 HID 报告时
`sunxi_usb_udc` 中断也会按报告数增加，但相对移动、右键、Windows 键均没有形成
可见的 Windows/VMware 输入效果。这次测试因此不能把“UDC configured”和
“Host 轮询端点”等同于端到端输入通过。

检查源码发现生产自检已经要求 `18d1:d015`、`bcdDevice=0x0428`、分离键盘与
鼠标接口，但启动脚本仍调用旧的 `d004` report-ID 复合接口。已完成并热部署以下
一致性修复：

- 生产模式使用 `18d1:d015`，继续默认关闭 ADB。
- 键盘使用独立 boot keyboard 接口和 `/dev/hidg0`。
- 相对鼠标使用独立 boot mouse 接口和 `/dev/hidg1`。
- ADB service mode 仍保留 report-ID 复合 HID，不受生产拓扑修改影响。
- 启动后分别检查两个字符设备节点，不再只检查复合 `/dev/hidg0`。

部署后的板端自检全部通过：

```text
PASS production HID product identity
PASS production HID descriptor revision
PASS ADB function disabled
PASS separate keyboard linked
PASS separate mouse linked
PASS composite HID unlinked
PASS keyboard HID node
PASS mouse HID node
PASS relative mouse report length
PASS gadget bound
UDC state: configured
```

真实 Windows 鼠标验证通过。把相对鼠标压到组合桌面右下角并单击后，Windows
执行“显示桌面”并最小化 VMware；随后压到当前 HDMI 屏左下角单击，Windows
天气组件状态发生变化。测试期间 `sunxi_usb_udc` 中断由 `283` 增加到 `537`，
可见 UI 变化与报告写入一致。截图保存在：

```text
/tmp/aitvbox-d015-coldboot-20260731/
```

后续验证确认键盘接口本身正常。最初向 Ubuntu 终端发送 `x` 没有显示，是因为
VMware 底部状态栏明确处于“请在虚拟机内部单击或按 Ctrl+G”的未抓取输入状态；
这不能作为 Windows 未识别 USB 键盘的证据。改用不依赖 VMware 抓取状态的
Windows 全局快捷键 `Win+D` 后，直接写 `/dev/hidg0` 能稳定切换 Windows 桌面和
VMware 窗口。

为完成浏览器端闭环，测试临时安装了一把一次性 SSH 公钥，并使用随机临时 IPKVM
密码；没有要求操作者提供现有密码。浏览器自动化实测结果：

```text
Keyboard channel       hidrpc only
Keyboard message       type 2
Win+D frame difference 82.64 / 82.64
Final keyboard report  modifier 0 (all released)
Mouse channel          hidrpc only
Mouse reports          41 x type 6
USB UDC state          configured
```

两次 `Win+D` 分别把 VMware 切换为 Windows 桌面、再恢复 VMware，画面差异由
浏览器直接从 1920×1080 WebRTC 视频帧计算，不再只以 DataChannel
`send()` 作为通过条件。截图保存在：

```text
/tmp/aitvbox-browser-keyboard-autotest-20260731-r4/
/tmp/aitvbox-browser-mouse-autotest-20260731-r3/
```

该测试还暴露了快速 Meta/Windows 快捷键的时序竞争：原前端 10ms 兼容定时器会在
Meta 已释放后重发按下时缓存的修饰键，可能短暂粘住 Windows 键。现在定时器执行时
重新读取 HID store 的实时修饰键状态；回归的最后一帧键盘报告为
`[type=2, modifier=0]`，顶栏按键状态也恢复为 `-`。

IPKVM 启动脚本现在在生产分离拓扑下显式使用 `/dev/hidg1` 作为鼠标，在
ADB service mode 缺少 `/dev/hidg1` 时回退到复合 `/dev/hidg0`。修复后的前端和
Go 服务已重新构建并热部署，运行文件与工作区 SHA-256 一致：

```text
aitvbox-ipkvmd        3637ea48e86603ea0b8fd0a1e21755cbfabdc9a44070d0b941f16fa0488a3b1b
aitvbox-ipkvm.init    af4f887d880c5ac646fa1def3c30da1ff7edaf0641af6bdee35ab69df09ab350
aitvbox-usb-hid.init  9e11b81eb2734511f74692a0c973ef17fb5ecc8cb85de7c45227ae02e55e68fc
```

部署新 IPKVM 二进制时发现 overlay 已占用 100%。新文件最终 SHA 完整，但认证
配置的临时写入因 `ENOSPC` 失败。部署前二进制已校验后转存到容量充足的
`/mnt/UDISK/aitvbox-backups/20260731-meta-release/`，overlay 随即恢复 6.5MiB
可用空间，IPKVM 和视频进程正常重启。

板端保留 `d015` 热部署版本，旧 `d004` 启动脚本备份在：

```text
/overlay/aitvbox/hotfix-backup-20260731-hid-coldboot/
```

测试结束后已把 IPKVM 配置按原 SHA-256
`6c0c5770468bf94e0e9665e9980e55d396f6e37f2cea638acc10cf487a261a5b`
恢复；一次性 SSH 公钥计数为 0，本机私钥、临时密码、认证配置和板端测试文件均已
删除。`/var/run/aitvbox/hid.lock` 与 `agent.lock` 无残留，Windows 画面已恢复到
VMware。

最终板端 HID 自检 11 项全部通过。Shell 语法检查、`git diff --check`、前端
TypeScript/Vite device 生产构建、ARM64 Go 构建和 IPKVM `go test ./...` 均通过。
板卡再次执行软件重启后，`192.168.1.44`、`18d1:d015`、`bcdDevice=0x0428`、
UDC `configured`、两个 HID 节点、原认证 SHA 和 IPKVM/视频进程均自动恢复，
HTTP 首页返回 200，证明结果不是热部署残留。
当前 23 项主机集成测试仍有 4 项既有断言与最新工作区行为不一致（3 项 Agent
行为断言、1 项仍期待旧 Mouse Report ID 2），不能记录为全绿；后续应先更新或
修复这些断言，再重新构建完整固件。
