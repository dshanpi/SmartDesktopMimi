# 按症状定位并记录故障

本章用于设备启动、网络、音频、电脑控制及更新异常。先保存故障时的状态，再按症状检查；需要重启或恢复时，记录操作时间和前后版本。

## 选择检查入口

| 症状 | 第一检查点 | 对应操作章节 |
| --- | --- | --- |
| 停在启动图 | 后端启动、socket、桌面交接 | [架构与启动](architecture.md) |
| 黑屏或触摸无响应 | 串口是否在线、UI/后端是否响应 | 本章启动检查 |
| Wi-Fi 掉线 | 地址、路由、启动 ID、SDIO 日志 | [网络](network.md) |
| 蓝牙已连接但无声 | 手机媒体输出、音量、控制器/profile | [蓝牙音频](bluetooth.md) |
| 网页可开但无视频 | HDMI 信号、WebRTC UDP 通道 | [IPKVM](ipkvm.md) |
| 有画面但不能输入 | USB 枚举、HID 节点、控制权 | [IPKVM](ipkvm.md)、[MCP](mcp.md) |
| 智家指令结果不明 | 刷新设备实际状态 | [智家](iot.md) |
| 配对没有新码 | 已绑定/未绑定/未知状态 | [账号配对](pairing.md) |
| APP 或更新被拒绝 | 明确错误、签名和目标 | [APP](user-app.md)、[更新](ota.md) |

## 采集最小状态

在故障设备的**板端 shell 或串口**执行，先保存到受控记录中：

```sh
date
cat /etc/aitvbox-version
cat /proc/sys/kernel/random/boot_id
cat /proc/uptime
df -h
ip addr
ip route
ps | grep -E 'lv_backend|lvglsim|aitvbox|hdmi_preview'
ls -l /tmp/lv_port_linux_backend.sock
tail -n 100 /tmp/lv_backend.log
tail -n 100 /tmp/lvglsim.log
dmesg | tail -n 120
```

路径不存在时记录缺失及固件版本，不创建空文件补齐结果。日志可能包含网络与设备信息，公开前脱敏；无需导出 License、secret、密码或完整认证配置。

## 区分整机重启与网络失联

1. 比较故障前后的 `boot_id`。
2. 启动 ID 改变时，结合运行时间确认发生新启动。
3. 核对期间是否有人重启、断电或触发恢复。
4. 从串口和内核输出寻找该次故障过程。
5. 启动 ID 未变但网络失联时，继续检查网络服务与整机输入响应。

`kernel.panic=3` 是策略，watchdog 进程存在是配置状态；两者都不能单独判定复位原因。缺少故障过程证据时保留“原因待定位”。

## 检查启动图、黑屏与 UI 阻塞

1. 确认串口是否仍能执行命令。
2. 查看 `lv_backend`、`lvglsim` 及相关启动日志。
3. 停在启动图时检查后端 socket 与 UI 启动交接。
4. 进程存在但页面无响应时，检查健康记录、主循环推进与后端请求是否返回。
5. 对照实际屏幕与物理触摸结果，记录哪些操作能响应。

显示故障、输入故障和后端等待应分别记录。恢复网络后 UI 恢复是一条观察结果，完整原因还需结合日志确认。

## 检查视频与输入控制

```sh
aitvbox-ipkvmctl status
aitvbox-ipkvmctl log
aitvbox-agentctl status
ls -l /dev/hidg0
ls -l /var/run/aitvbox/capture.sock
```

网页可达时继续检查 HDMI 与媒体通道；HID 节点存在时继续核对被控电脑的 USB 枚举。AI 占用控制权时先正常停止，再测试手动输入。采集故障使用现有服务入口处理，避免另起进程争用设备。

## 处理写入或更新异常

刷写目标、容量、介质类型或分区信息不符时停止后续写入，保存工具完整日志和设备绑定。命令失败不能用于推断“没有发生写入”。

桌面更新失败时检查事务及回退结果，读回实际运行版本。恢复旧版本后保存数据与功能检查，新候选仍保留未通过状态。

## 提交可复现的问题记录

使用以下模板整理脱敏信息：

```text
设备：实物板型/修订，必要的脱敏标识
版本：系统版本、应用版本、包 SHA-256
时间：发生时间与时区
前提：网络、外设、正在执行的任务
步骤：按顺序列出可重复操作
预期：该操作应出现的状态
实际：界面提示、声音/输入/网络表现
启动：boot_id 是否改变，是否有人重启或断电
证据：日志、截图来源和采集方式
已处理：尝试过的操作及每次结果
待定位：尚缺少的证据或复现条件
```

截图注明实机或主机渲染。完成记录后按具体原因修复，并重测最初触发步骤及受影响功能。

## 查阅依据

[运行日志](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/runtime.md) · [SDIO 与实物表现记录](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-sdio-physical-report-20261026.json) · [安装恢复记录](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-board-install-20261029.json)
