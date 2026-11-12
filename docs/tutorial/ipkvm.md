# 通过浏览器查看并控制电脑

IPKVM 通过 HDMI 接收电脑画面，经 WebRTC 发送到管理浏览器，再将键鼠动作通过 USB HID 交给被控电脑。本章依次完成局域网登录、视频与输入验证、HTTPS，以及可选外网转发。

## 准备接线和测试窗口

按[硬件连接](hardware.md)连接电脑 HDMI OUT、小咪 HDMI IN，以及小咪 USB Device 与被控电脑。管理浏览器应能访问板端 IP，被控电脑已识别标准 HID 键盘和鼠标。

在被控电脑打开空白文本编辑器作为输入测试目标。以下 `192.168.1.50` 是示例板端地址，需替换为实际值。

## 登录局域网工作区

1. 在管理浏览器打开 `http://192.168.1.50/`；已启用 TLS 时使用设备对应的 HTTPS 地址。
2. 首次使用时按页面设置 8–128 位登录密码，已有密码时直接登录。
3. 进入 KVM 工作区，等待视频连接建立。
4. 对照被控电脑显示内容，确认浏览器显示的是目标 HDMI 画面。

**检查结果：** 浏览器画面持续更新。页面能登录但没有视频时，先处理下面的视频检查，不继续测试键鼠。

## 分别验证视频和输入

1. 在被控电脑移动窗口或改变编辑器内容，确认浏览器画面同步变化。
2. 在浏览器移动鼠标，核对被控电脑指针响应。
3. 点击测试编辑器，输入一个普通字符，检查字符只出现一次。
4. 测试一个常用组合键，随后松开，确认没有持续按键状态。
5. 结束测试时关闭会话，确认输入控制释放。

本地 AI 任务占用控制权时，浏览器键鼠可能被拒绝；先停止任务，再测试手动输入。控制权与资源关系见[AI 与 MCP](mcp.md)。

## 在板端检查服务和通道

在**板端 shell**执行：

```sh
aitvbox-ipkvmctl status
aitvbox-ipkvmctl log
ls -l /dev/hidg0
ss -lntup | grep -E ':(80|443|40000)[[:space:]]'
```

| 通道 | 典型端口 | 需要检查的结果 |
| --- | --- | --- |
| HTTP / HTTPS | TCP 80 / 443 | 对应 Web 端口监听，浏览器能登录 |
| WebRTC | UDP 40000 | 媒体会话能建立，视频持续更新 |
| USB HID | `/dev/hidg0` 与 USB 枚举 | 节点存在，被控电脑识别且输入有效 |

系统没有 `ss` 时，使用该固件随附的诊断工具。监听状态用于定位服务；最终仍以实际网页、视频和输入检查为准。

## 配置 HTTPS

准备与设备管理域名匹配的可信证书及私钥，并确认 ADB 已授权。以下在**管理主机**执行；替换 `DEVICE_SERIAL`、证书文件名和域名。服务重启会断开当前会话。

```sh
adb -s DEVICE_SERIAL push kvm.example.com.crt /overlay/aitvbox/ipkvm/tls.crt
adb -s DEVICE_SERIAL push kvm.example.com.key /overlay/aitvbox/ipkvm/tls.key
adb -s DEVICE_SERIAL shell chmod 600 /overlay/aitvbox/ipkvm/tls.crt /overlay/aitvbox/ipkvm/tls.key
adb -s DEVICE_SERIAL shell /etc/init.d/aitvbox-ipkvm restart
```

1. 使用对应的 `https://kvm.example.com/` 重新登录。
2. 检查证书域名、有效期与信任状态。
3. 再测试视频和一个普通按键。

证书与私钥必须成对存在；缺少其中一项时服务会拒绝启动。出错时先补齐正确文件与权限，再检查服务日志。私钥仅保存到受控主机与目标设备。

## 配置外网 FRP 转发

先完成局域网和 HTTPS 验证，再配置外网。准备 FRPS 服务器、公网数字 IP、访问域名与令牌。Tina 基线自带 FRPC `0.34.3`，配置格式为 **INI**。

1. 在管理主机将真实公网数字 IP 写入设备。`203.0.113.10` 为文档示例，必须替换：

```sh
printf '%s\n' '203.0.113.10' |
  adb -s DEVICE_SERIAL shell 'umask 077; cat > /overlay/aitvbox/ipkvm/webrtc-public-ip'
adb -s DEVICE_SERIAL shell /etc/init.d/aitvbox-ipkvm restart
```

2. 登录网页设置中的 FRP 页面，按服务器实际参数填写 INI：

```ini
[common]
server_addr = 203.0.113.10
server_port = 7000
token = replace-with-your-server-token

[mimi-web]
type = tcp
local_ip = 127.0.0.1
local_port = 443
remote_port = 18443

[mimi-webrtc]
type = udp
local_ip = 127.0.0.1
local_port = 40000
remote_port = 40000
```

3. 确认 FRPS 配置与防火墙允许对应的 `7000/tcp`、`18443/tcp`、`40000/udp`。
4. 在页面启动本设备的 FRPC，检查日志没有认证或端口冲突错误。
5. 从外部网络访问 `https://kvm.example.com:18443/`，复测登录、视频及键鼠。

网页通过 TCP，视频和输入 DataChannel 通过 WebRTC。只转发网页端口时，仍可能没有视频。外网访问使用 HTTPS 或已有受控 VPN，不直接暴露明文登录。

## 排除常见故障

| 症状 | 检查及处理 |
| --- | --- |
| 页面打不开 | 核对板端 IP、80/443 服务、网络与防火墙 |
| 页面能开、无视频 | 核对 HDMI 信号，再查 UDP 40000、公网候选及 FRP 转发 |
| 有画面、不能操作 | 检查 USB Device 接线、HID 枚举和 AI 控制权 |
| HTTPS 启动失败 | 核对成对证书、文件权限与服务日志 |
| FRPC 无法启动 | 核对 INI 格式、服务端令牌、端口占用和日志 |

结束时停止 AI 任务、关闭浏览器会话，并关闭无需使用的 HDMI 采集。不要额外启动第二个 `hdmi_preview --service` 争用现有采集资源。

## 查阅依据

[IPKVM 服务与 FRP 配置](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/ipkvm-user-guide.md) · [当前控制实现与状态](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/CURRENT_PROGRESS.md)
