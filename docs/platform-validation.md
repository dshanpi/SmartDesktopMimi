# AITVBox 平台验证

## Ubuntu 模拟回归

```sh
cd /home/ubuntu/AI-DeskTopBox
AITVBOX_TINA_SDK=~/A133-Tina5.0-v0.9 \
  python3 -m unittest -v tests/platform/test_host_integration.py
```

测试在 Ubuntu 实际编译并运行 x86_64 服务，用 Unix Socket 模拟 HDMI capture、
日志文件模拟 USB HID、本地受信 TLS Server 模拟多模态服务，同时调用真实 A133
Tina 工具链编译 native 示例。

平台集成测试覆盖：

- MCP initialize/initialized 生命周期、tools/list、截图、HID、能力发现、急停。
- JSON parse error、未知方法、多余参数、键码/鼠标范围和自主任务输入互斥。
- HTTPS 多模态请求、三步视觉动作循环、步数限制、尾随/越界动作拒绝、HID 释放。
- 浏览器控制服务的密钥保存、provider 配置、启动、状态、停止和 socket 身份限制。
- schema 1/2 创建、A133 GCC 8.3 编译、RSA build/verify 和 ADB upload 流程。
- 设备原生 policy、签名篡改、路径穿越、符号链接、脚本、SO、额外 ELF 拒绝。
- 原子安装、断电恢复、数据保留、版本门禁和发布者指纹所有权。
- APP 锁初始化宽限、断电 stage 回收、`appd` worker 上限、客户端期限和繁忙响应。
- `appd` 权限 broker、平台 action、native action、超时/输出协议。
- native UID/GID、父 appd、`NoNewPrivs`、seccomp、目录权限和指纹再校验。
- 沙箱拒绝 AF_INET 与文件写入，直接在 appd 外执行已安装 ELF 无法使用 broker。
- 四个产品包统一开关、SDK 原生 `adbd` 恢复和旧产物清理。
- 超过 1 MiB 的 MCP 请求排空拒绝、64 MiB APP 解压上限和 ADB 临时包清理。
- SDK 薄包精确镜像，已删除的产品文件不会残留到后续固件。

IPKVM Go 测试另覆盖密码强制、并发初始化单一成功者、配置原子落盘与 `0600` 权限、
损坏认证配置失败关闭、重启 token 失效、Cookie、同源策略、存储/调试端点关闭、
RPC 白名单、HID 报告、Agent 输入互斥、HDMI 锁、固定 WebRTC 端口配置、Unix
stream 拆包/粘包、并发 session、FRPC 启动失败和 socket 帧协议：

```sh
cd apps/ipkvm/upstream
/usr/local/go/bin/go test ./...
/usr/local/go/bin/go test -race ./...
```

## A133 构建结果

以下目标已用 `/home/ubuntu/A133-Tina5.0-v0.9` 交叉编译并链接：

- `lv_backend`、含“用户应用”的 `lvglsim`
- `hdmi_preview`
- `aitvbox-mcpd`
- `aitvbox-agentd`
- `aitvbox-controld`
- `aitvbox-appd`
- `aitvbox-app-policy`
- schema 2 示例 `platform/app-sdk/examples/native-status/bin/app`
- `aitvbox-ipkvmd`
- `aitvbox-kvm-video`

本地 IPK 产物：

```text
build/packages/aitvbox-platform_1.0.0-2_aarch64_generic.ipk
build/packages/aitvbox-suite_1.0.0-9_aarch64_generic.ipk
build/packages/aitvbox-usb-hid_1.0.0-2_aarch64_generic.ipk
build/packages/aitvbox-ipkvm_0.1.0-1_aarch64_generic.ipk
```

`aitvbox-platform` 包内已核对 `appd`、`controld`、`mcpd`、安装工具和两个 init
脚本；`aitvbox-suite` 已核对 `lvglsim`、`lv_backend` 和 `hdmi_preview`。
打包验证结束后 `toggle_product.sh off` 会把 SDK 三处配置中的四个产品包恢复为关闭并清除
SDK 增量 rootfs 产品文件，本地 `build/packages/` 产物保留。

完整 `build_firmware.sh` 已通过并生成 58 MiB 固件：

```text
/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/a133_linux_b6_uart0.img
```

构建入口在 pack 后自动读取最终 SquashFS，确认 17 个关键程序/init 脚本均存在、
权限为 `0755` 且所有者为 `root:root`，并拒绝遗留设备签名私钥、TLS 私钥或 SSH
私钥进入固件。人工解包另确认所有产品 ELF 均为 AArch64、动态库依赖完整，启动顺序
为数据层 S95、USB HID S81、APP S83、控制服务 S84、IPKVM S98、桌面 S99。

## 板端冒烟测试

烧录带产品内核配置的新固件后执行：

```sh
# seccomp 是 native APP 的强制前置条件
zcat /proc/config.gz | grep -E 'CONFIG_SECCOMP(=|_FILTER=)'

# 服务和 socket
pidof aitvbox-appd aitvbox-controld
ls -l /var/run/aitvbox/apps.sock /var/run/aitvbox/control.sock

# USB gadget 同时存在 ADB 与单接口复合键盘/鼠标
aitvbox-usb-hid-test
ls -l /dev/hidg0

# HDMI
pidof hdmi_preview
hdmi_preview --snapshot
file /var/run/aitvbox/screen.jpg

# MCP
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport adb capabilities

# APP
aitvbox-appctl list

# 浏览器 IPKVM
aitvbox-ipkvmctl status
ss -lntup | grep -E ':(80|443|40000)[[:space:]]'
```

触摸验证：

1. 浏览器打开“设置 → HDMI + HID MCP”，保存测试 provider/`sk-` 密钥，提交一个
   无破坏性任务、急停并恢复实时视频。
2. 确认被控 Windows 和 Linux 均枚举标准键盘/鼠标并响应。
3. 安装 `2048` 示例，从“用户应用”打开，执行移动并确认重进后存档仍在。
4. 确认任何 APP 页面都有系统返回栏，不能覆盖成独占全屏。
5. 浏览器登录 IPKVM，验证画面、键盘、相对鼠标和断开后桌面预览恢复。
6. FRPS 同时转发 HTTPS TCP 与 `40000/udp`，从外网验证 WebRTC 连接。

首次密码初始化只能在隔离管理网进行。未配置
`/overlay/aitvbox/ipkvm/tls.crt` 和 `tls.key` 时服务使用 HTTP，禁止直接公网转发；
量产验收必须覆盖设备证书签发、更新、过期和恢复流程。

## 尚需真机确认

Ubuntu 无法替代真实 HDMI HPD/DDC、USB 电气连接、Windows/Linux 枚举、触摸输入和
真实模型服务端到端结果。R818 图纸也没有确认通用 GPIO/I2C/SPI 连接器和 UART
电压，所以这些 broker 仍保持关闭。IPKVM 已完成 Ubuntu 协议测试和 A133
交叉编译，但 HDMI、USB 和公网 FRPS 的最终结论仍需真机验收。
