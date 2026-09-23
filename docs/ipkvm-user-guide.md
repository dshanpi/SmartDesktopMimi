# AITVBox 浏览器 IPKVM 使用与开发说明

## 1. 功能边界

设备通过 HDMI IN 获取 Windows/Linux 电脑画面，通过 USB Device 向电脑枚举标准
键盘和相对鼠标。浏览器只连接 A133 板，不需要在被控电脑安装客户端。

```text
被控电脑 HDMI OUT -> A133 HDMI IN -> A133 H.264 硬编
浏览器 <- HTTPS/WebSocket + WebRTC <- aitvbox-ipkvmd
浏览器操作 -> WebRTC DataChannel -> /dev/hidg0 复合键鼠 -> 被控电脑 USB
```

浏览器会话存在时，IPKVM 临时取得 HDMI capture；最后一个会话断开后释放，
`hdmi_preview` 可恢复本机桌面预览。本地 MCP Agent 运行时持有
`/var/run/aitvbox/agent.lock`，浏览器键鼠输入会被拒绝；停止 Agent 后浏览器可重新
取得 HID。两种模式共用同一硬件，不允许同时控制。

## 2. 接线和首次登录

1. 将电脑 HDMI OUT 接到板端 HDMI IN。
2. 将板端 USB Device/OTG 接到电脑 USB；Windows 和 Linux 应枚举标准键盘、鼠标。
3. 让板子和管理电脑位于同一可信局域网。
4. 在板端执行 `ip addr` 获取地址，浏览器打开 `http://<板端IP>/`。
5. 首次页面必须设置 8 至 128 位密码；平台模式不允许免密。
6. 登录后进入 KVM 页面，画面建立时硬编自动启动，关闭会话后自动停止。

服务检查：

```sh
aitvbox-ipkvmctl status
aitvbox-ipkvmctl log
ls -l /dev/hidg0
ss -lntup | grep -E ':(80|443|40000)[[:space:]]'
```

配置保存在 `/overlay/aitvbox/ipkvm/`，权限为 `0700`；登录配置、FRPC 配置和
私钥为 `0600`。忘记登录密码时只能由设备管理员通过 ADB 删除 `config.json` 后
重启服务，设备会回到首次设置状态。

## 3. HDMI + HID MCP

登录后打开“设置 → HDMI + HID MCP”，可配置 HTTPS Base URL（或完整的
`chat/completions` endpoint）、模型、API key、
任务和最大步数。API key 写入板端 `0600` 文件，状态响应只返回是否已配置，不返回
密钥正文。

开始任务时浏览器主动关闭 WebRTC，把 HDMI 所有权交给 `aitvbox-agentd`；急停会
释放全部键鼠状态。“停止任务并恢复实时视频”会重新建立 WebRTC。配置和长任务均在
电脑浏览器输入，触屏桌面不再提供电脑助手应用。

## 4. 局域网 HTTPS

默认端口 80 仅适合可信局域网。若存在
`/overlay/aitvbox/ipkvm/tls.crt` 和 `tls.key`，服务自动改为 443，并给登录 Cookie
增加 `Secure`。证书和私钥必须同时存在，否则服务拒绝启动。

```sh
adb push kvm.example.com.crt /overlay/aitvbox/ipkvm/tls.crt
adb push kvm.example.com.key /overlay/aitvbox/ipkvm/tls.key
adb shell chmod 600 /overlay/aitvbox/ipkvm/tls.crt \
  /overlay/aitvbox/ipkvm/tls.key
adb shell /etc/init.d/aitvbox-ipkvm restart
```

生产环境应使用管理域名对应的可信证书。自签证书只适合调试。

## 5. FRP 外网转发

Web 页面/信令走 TCP，视频和键鼠 DataChannel 走 WebRTC UDP。只转发 TCP 网页会
出现“页面能打开但没有画面”，所以必须同时转发固定 UDP 端口 `40000`。

先把 FRPS 服务器的公网数字 IP 写入设备，用于生成 WebRTC 公网候选地址：

```sh
printf '%s\n' '203.0.113.10' |
  adb shell 'umask 077; cat > /overlay/aitvbox/ipkvm/webrtc-public-ip'
adb shell /etc/init.d/aitvbox-ipkvm restart
```

然后在网页设置的 FRP 页粘贴 Tina 自带 FRPC `0.34.3` 的 INI 配置：

```ini
[common]
server_addr = 203.0.113.10
server_port = 7000
token = replace-with-a-long-random-token

[aitvbox-web]
type = tcp
local_ip = 127.0.0.1
local_port = 443
remote_port = 18443

[aitvbox-webrtc]
type = udp
local_ip = 127.0.0.1
local_port = 40000
remote_port = 40000
```

FRPS 防火墙需开放 `7000/tcp`、`18443/tcp` 和 `40000/udp`。浏览器访问
`https://kvm.example.com:18443/`；证书域名应匹配该地址。若尚未配置 TLS，可把
网页隧道改为本地 80/远端 18080，但这会明文传输密码和会话，只允许在已有 VPN
或可信测试网络中使用，禁止直接暴露到公网。

网页点击“停止”只停止本设备管理的 FRPC 子进程，不会全局 `pkill` 其他 FRPC。
勾选/启动后配置会持久化，设备服务正常重启不会清除自动启动状态。

## 6. 安装和升级 IPK

SDK 产物：

```text
build/packages/aitvbox-ipkvm_0.1.0-1_aarch64_generic.ipk
```

ADB 安装：

```sh
adb push build/packages/aitvbox-ipkvm_0.1.0-1_aarch64_generic.ipk /tmp/
adb shell opkg install /tmp/aitvbox-ipkvm_0.1.0-1_aarch64_generic.ipk
adb shell /etc/init.d/aitvbox-ipkvm enable
adb shell /etc/init.d/aitvbox-ipkvm restart
```

包依赖 `aitvbox-usb-hid`、`frpc` 和 A133 CedarX 编码库。完整产品固件应通过
`scripts/build_apps.sh` 和 `scripts/build_firmware.sh` 集成，不要把设备端源码复制
进 Tina SDK。

## 7. 故障定位

| 现象 | 检查 |
|---|---|
| 页面打不开 | `aitvbox-ipkvmctl status`、80/443 监听、防火墙 |
| 页面有登录但无画面 | HDMI 信号、`40000/udp`、FRPS UDP 代理、公网 IP 文件 |
| 浏览器不能操作键鼠 | `/dev/hidg0`、USB 接线、本地 Agent 是否正在运行 |
| 桌面预览暂时消失 | 浏览器会话取得 HDMI 所有权，属于正常互斥 |
| FRPC 启动失败 | 使用 INI 而非 TOML，查看 `aitvbox-ipkvmctl log` 和 `frpc.log` |
| HTTPS 服务不启动 | `tls.crt`/`tls.key` 是否成对存在且可读 |

Ubuntu 测试只能验证鉴权、协议、socket、HID 报告和 H.264 帧分发逻辑。HDMI
电气信号、A133 硬编实时性、USB 枚举以及公网 NAT/FRPS 链路必须在板端做最终验收。
