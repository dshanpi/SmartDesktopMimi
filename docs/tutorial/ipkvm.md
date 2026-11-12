# 浏览器 IPKVM 完整上手

IPKVM 把外部电脑 HDMI 画面送到浏览器，再把浏览器键鼠操作转成 USB HID。被控 Windows/Linux 无需安装专门的客户端。

## 1. 接线与网络

按[硬件连接图](hardware.md)：电脑 HDMI OUT 接板端 HDMI IN，板端 USB Device 接同一台被控电脑；管理浏览器能访问板端 IP。示例 `192.168.1.50` 只是占位，替换成设备真实地址。

## 2. 首次登录

1. 浏览器打开 `http://192.168.1.50/`。
2. 首次设置 8–128 位设备登录密码；平台模式不提供免密入口。
3. 登录进入 KVM 工作区，等待实时画面建立。
4. 在被控电脑打开空白文本编辑器，短测指针和一个普通按键，再测试常用组合键。
5. 关闭网页后确认会话与输入控制释放。

不要在密码输入框或有破坏性操作的窗口中做首次键鼠试验。该检查的目标是确认连接、映射和释放，不需要操作真实业务数据。

## 3. 理解端口与两条通道

| 通道 | 典型端口 | 内容 |
| --- | --- | --- |
| HTTP / HTTPS | TCP 80 / 443 | 登录、配置、信令 |
| WebRTC | UDP 40000 | 视频与键鼠 DataChannel |

能打开网页但无画面，经常意味着媒体通道、HDMI 信号或网络候选有问题。TCP 网页通不等于 UDP 视频也通。

管理员在板端执行：

```sh
aitvbox-ipkvmctl status
aitvbox-ipkvmctl log
ls -l /dev/hidg0
ss -lntup | grep -E ':(80|443|40000)[[:space:]]'
```

预期：IPKVM 服务运行，所选 Web 端口监听；使用 HID 时节点存在；视频会话能建立。系统没有 `ss` 时使用该固件提供的网络诊断工具，不要因此认定服务不存在。

## 4. HTTPS 配置

生产或跨网络使用配置与访问域名匹配的可信证书。服务在 `tls.crt` 和 `tls.key` 同时存在时切换到 443；缺一项会拒绝启动。以下在已授权的管理主机执行，显式指定设备：

```sh
adb -s DEVICE_SERIAL push kvm.example.com.crt /overlay/aitvbox/ipkvm/tls.crt
adb -s DEVICE_SERIAL push kvm.example.com.key /overlay/aitvbox/ipkvm/tls.key
adb -s DEVICE_SERIAL shell chmod 600 /overlay/aitvbox/ipkvm/tls.crt /overlay/aitvbox/ipkvm/tls.key
adb -s DEVICE_SERIAL shell /etc/init.d/aitvbox-ipkvm restart
```

证书私钥只交给指定设备，不提交 Git。重启服务会中断当前会话；完成后使用新 HTTPS 地址登录。

## 5. 可选：FRP 远程访问

先完成局域网测试与 HTTPS，再配置外网。Tina 基线中的 FRPC 为 `0.34.3`，使用 INI，不能直接照搬新版 TOML 示例。

```ini
[common]
server_addr = 203.0.113.10
server_port = 7000
token = replace-with-your-own-token

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

`203.0.113.10` 和 `kvm.example.com` 是文档保留地址，需要替换。服务器防火墙和 FRPS 要匹配上述 TCP/UDP 端口；板端 `/overlay/aitvbox/ipkvm/webrtc-public-ip` 保存真实公网数字 IP。网页端口可访问但 UDP 没转发时仍会黑屏。配置与公网候选的完整命令见[原始 IPKVM 手册](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/ipkvm-user-guide.md)。

## 6. 会话与 AI 的关系

键鼠控制存在独占：本地 AI 任务运行时，外部输入可能被拒绝；停止 AI 后再恢复手动操作。当前开发分支的 AI 路径从同一采集服务按需取帧，目标是保持 WebRTC 直播；早期手册中的“启动 AI 时关闭直播”描述属于旧实现。详见[AI/MCP 章节](mcp.md)。

用完关闭会话，按正常控制入口关闭 HDMI，确认采集/编码释放。不要手动再起一个 `hdmi_preview --service` 与已有采集进程争用硬件。
