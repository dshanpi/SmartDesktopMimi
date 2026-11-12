# 按症状定位故障

排错先固定版本、时间和目标设备，再区分显示、UI、后端、网络和整机状态。不要把“SSH 断开”直接等同于“整机死机”。

## 快速定位表

| 症状 | 先检查 | 下一步 |
| --- | --- | --- |
| 停在启动图 | 后端 socket、启动日志 | 检查 UI 交接与更新器健康 |
| 黑屏但串口在线 | 显示/背光、UI 进程、日志 | 区分显示问题和主循环阻塞 |
| Wi-Fi 失联 | 地址、路由、SDIO/MMC 日志 | 保留故障时刻与串口证据 |
| 页面能开但 KVM 无画面 | HDMI 信号、UDP 40000 | 检查编码、WebRTC、FRP 候选 |
| 有画面无键鼠 | USB Device/HID、控制权 | 停止 AI 后验证普通输入 |
| 蓝牙已配对但无声 | 媒体输出、音量、profile | 分别验证手机与设备状态 |
| 用户 APP 安装失败 | 公钥、签名、版本与目标 | 查看明确拒绝原因 |
| 设备配对没有码 | 已绑定/未知语义 | 重新查询，不清除身份 |
| 更新被拒绝 | 兼容清单、签名、数据与空间 | 解决前置条件，不强制绕过 |

## 1. 采集一份最小状态

以下在板端执行，输出先保存在受控位置：

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

某个路径在你的版本不存在时记录“缺失”，不要临时创建空文件使检查通过。日志可能包含设备信息，公开前脱敏；不要一并导出授权或 secret 文件。

## 2. 区分重启与失联

故障前后比较 `boot_id` 与 `/proc/uptime`。启动 ID 变化表示新启动，但不能单凭它判断是供电、watchdog、panic 还是人为操作。记录操作者是否发出过重启，以及串口捕捉到的实际过程。

`kernel.panic=3` 只说明策略，不证明发生过 panic；watchdog 进程存在不证明它触发了复位；一次网络恢复也不证明底层故障修复。

## 3. UI 卡住与后端等待

进程还在，可能已经阻塞。若有当前版本提供的健康记录，应同时检查 UI 主循环与后端回复是否推进。再记录物理屏幕与触摸结果；主机心跳不能替代人看到的画面。

固定快照曾发现后端同步网络连接等待，并有 SDIO 超时同时出现。这些是调查线索，不足以概括所有历史黑屏/重启的因果。

## 4. HDMI/IPKVM 专项

```sh
aitvbox-ipkvmctl status
aitvbox-ipkvmctl log
aitvbox-agentctl status
ls -l /dev/hidg0
ls -l /var/run/aitvbox/capture.sock
```

不要另起采集进程抢占视频设备。先用 UI 的正常停止/关闭入口释放资源，再检查实际状态。只有网页连通时，应继续核对媒体通道；只有 HID 节点时，还要核对被控电脑枚举。

## 5. 刷写异常单独处理

重连目标变化、容量不符、MBR 失败或未知写入结果时停止继续刷写，保留工具日志和设备绑定。不能因为命令失败就断言“零写入”，也不能反复尝试不同设备位置碰碰运气。

先按受控恢复流程恢复可读状态，再确认旧版本、数据、分区和目标。旧版恢复成功与新包验收分开记录。

## 6. 提交可复现问题

描述中包含：实际板型、源码/包版本、触发步骤、期望与实际结果、故障时间、启动 ID 是否变化、脱敏日志和已经尝试的操作。截图注明实机/主机渲染、时间和来源。避免“全测通过但偶尔死机”这类矛盾总结。

关联历史：[SDIO 与物理表现](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-sdio-physical-report-20261026.json)、[恢复证据](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-board-install-20261029.json)。
