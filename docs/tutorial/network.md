# Wi-Fi、蓝牙与音频

先建立稳定的本地连接，再使用云端、IPKVM 和更新。将 Wi-Fi、蓝牙与声音分开检查，比笼统地判断“网络坏了”更容易定位。

## Wi-Fi：分三层验证

1. 在无线网络页扫描并连接接入点，确认认证成功。
2. 在板端执行 `ip addr`、`ip route`，确认地址与默认路由。
3. 从管理电脑访问板端 IPKVM；需要云端时再检查 DNS、时间和目标服务。

板端只读检查：

```sh
ip addr
ip route
cat /etc/resolv.conf
date
logread | tail -n 80
dmesg | tail -n 80
```

分享日志前遮盖 SSID、MAC、设备标识、账号信息和密钥。不要公开完整的 Wi-Fi 配置文件。

### 网络连接后又失联

固定快照保留了 SDIO/CMD52 超时、界面卡住/黑屏和重连阻塞记录。一次 ping 成功或重启后联网不能证明问题已解决。记录故障前后启动 ID、运行时间、内核日志以及 UI/后端是否继续响应，按[排错章节](troubleshooting.md)区分整机重启与服务阻塞。

## 蓝牙音箱：配对、连接、出声分开检查

1. 打开设备的蓝牙音频页面，确认控制器准备完成。
2. 在手机蓝牙设置中选择小咪并完成配对。
3. 在手机选择媒体输出为该蓝牙设备，播放一段已知音频。
4. 检查两端音量、静音和播放状态；必要时分别验证播放/暂停和音量控制。
5. 断开后观察页面回到实际断开状态，再重新连接。

设备作为 **A2DP Sink** 接收手机音频，AVRCP 提供部分媒体控制与状态；具体曲目信息取决于对端提供内容。手机已配对但没有音频输出，不一定是配对失败。

管理员可在板端检查：

```sh
hciconfig -a
ps | grep -E 'hciattach|lv_backend'
logread | tail -n 100
```

启动链中 `bluetooth_init` 准备控制器，后端再初始化 profile；未及时就绪时应走延迟重试。不要为了让图标变绿而在多个终端反复启动 `hciattach`。

## 配置与持久化

Wi-Fi、蓝牙 MAC/配对及桌面设置通过持久目录保存，旧布局包括 `/overlay/wifi/wpa_supplicant`、`/overlay/bluetooth` 和 `/overlay/lib/bluetooth`。实际版本的挂载关系以 `mount` 与 init 脚本为准。

升级前后应验证“仍能使用原网络和配对”，不能只核对目录存在。恢复出厂、解除配对和系统 OTA 是不同操作，数据影响要分别记录。

## 完成检查

Wi-Fi 能保持连接并访问所需服务；蓝牙能实际出声并完成断开/重连；UI 的状态与真实服务一致。长时间稳定性和断电保留另按[验证矩阵](validation.md)执行。

参考：[蓝牙实现与排错](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/bluetooth.md)、[运行与持久化](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/runtime.md)。
