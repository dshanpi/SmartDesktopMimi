# 分层架构实施验证记录（2026-09-15）

## 范围与基线

- 产品提交：`f1193618ef0a5dfc65d6ec8be2c5da1ffbb6d6b4`，分支
  `feature/a133-smart-desktop-20260907`，在保留既有修改的脏工作树上实施。
- LVGL 子模块：`685aa79c2894b038c212ef7c26834bfd5b9120aa`。
- 实施前备份：`/home/ubuntu/AI-DeskTopBox-backups/20260915T060358-0400/`。
- 源码归档 SHA-256：
  `e35bc4f5a1c0469b9c8f66eac89a66be7501bdd87fd7821f3098aa421e1b2324`。
- Git bundle SHA-256：
  `99d77d95dafc6165dd15f4524b042866a6d19d7108f584a4113b61a53a4d2e20`。

## 自动验证结果

| 门禁 | 结果 | 证据摘要 |
| --- | --- | --- |
| 架构契约单测 | PASS | `python3 -m unittest tests.architecture.test_architecture`，15/15 |
| 分层依赖扫描 | PASS | `python3 scripts/check_architecture.py` |
| A133 主机集成测试 | PASS | `python3 -m unittest tests.platform.test_host_integration`，64/64 |
| IPKVM Go 测试 | PASS | `go test ./...` |
| IPKVM Go race | PASS | `go test -race ./...` |
| IPKVM Device UI | PASS（有警告） | 5782 modules；6 个既有 CSS 优化警告，Node 20 与声明的 Node 22 不一致 |
| A133 应用交叉编译 | PASS | LVGL 前后端、HDMI preview、平台服务、IPKVM、Tuya chatbot |
| A133 Tina 内核/rootfs/pack | PASS | 完整构建、rootfs 文件权限审计、pack 与 SDK 恢复均完成 |
| Boot-chain gate | PASS | 已审核 gate SHA-256 `71e81a98c74feb329509d661766f8627b19186a775fa4342a16b7ceb83588537` |
| 发布包校验 | PASS | `sha256sum -c SHA256SUMS` 全部通过 |
| SBOM 生成 | PASS | SPDX 2.3 与 CycloneDX 1.5 JSON 均可解析 |
| Rust 工厂助手 | BLOCKED | 主机 Cargo 1.75 不支持 lockfile v4，依赖还要求 Edition 2024；未伪记为通过 |
| 公共源码审计 | BLOCKED | 2417 个受限或生成路径：third_party 2303、vendor 106、tools 8 |

统一命令 `python3 tools/aitvbox.py test --platform a133 --scope all` 已再次整体通过。

## A133 发布产物

- 发布目录：
  `/home/ubuntu/AI-DeskTopBox/build/releases/1.0.7-f1193618ef0a-20260915T103258Z/`
- 固件：`firmware/a133_linux_b6_uart0.img`，556316672 bytes。
- 固件 SHA-256：
  `cc4785939b08d1ca0d5a5795574d5a266f69a89c33ad5d67e300c5c47cefc696`。
- `BUILD-COMPLETE` 存在，`BUILD-INCOMPLETE` 不存在；日志、审计、应用哈希和构建
  manifest 均包含在 `SHA256SUMS`。
- 本次是 `WITH_OTA=0` 的全量烧录镜像，不包含签名 `.swu`。

## 平台与实机状态

| 平台 | Doctor | SDK 构建 | 实机 |
| --- | --- | --- | --- |
| A133/B6 | PASS | PASS | PARTIAL：全擦烧录、恢复、启动和核心硬件通过；HDMI `no-frame` 使综合验收 FAIL |
| A527 | 预期退出 2 | NOT_RUN：无 SDK | NOT_RUN：无板卡 |
| V883 | 预期退出 2 | NOT_RUN：无 SDK | NOT_RUN：无板卡 |
| RK3576 | 预期退出 2 | NOT_RUN：无 SDK | NOT_RUN：无板卡 |
| RV1106 | 预期退出 2 | NOT_RUN：无 SDK | NOT_RUN：无板卡 |

骨架平台的 fail-closed 是设计结果，不代表移植通过。必须取得相应 SDK、板卡和合法分发
边界后，按 `docs/zh-CN/porting.md` 逐项开放 capability。

## A133 MCP 实机预检

通过 `lynx-a133-sdk` MCP 的 device 4 通道和受控串口代理完成了实机读取。板卡当前运行旧
`1.0.7` 固件（提交 `49709f26-dirty`），板型为 `a133-b6`、内核为 Linux 4.9.191，
`wlan0` 地址为 `192.168.1.37/24`。eMMC 约 14.6 GiB，共 9 个分区；活动根文件系统为
p4，p7 挂载到 `/overlay`，p9 挂载到 `/mnt/UDISK`。

- 核心服务进程和 TCP 22/80/5055/6668 已确认；停止进行备份后已恢复。
- UDC `5100000.udc-controller` 状态为 `configured`。
- `/dev/hidg0` 为 8 字节 boot keyboard，`/dev/hidg1` 为 4 字节 boot mouse。
- `/dev/media0`、`/dev/video0` 和 video worker 存在；尚未取得帧级稳定性证据。
- 新镜像通过 LYNX SFTP 原子传输，远端大小和 SHA-256 与本地一致；格式识别为未加密的
  Allwinner `IMAGEWTY` v3 vendor package，共 33 个组件。
- 首次尝试进入 FEL 前被 `ALLWINNER_FEL_USB_UNBOUND` 拒绝，没有发送 `reboot efex`，
  没有重启或调用刷写。完成绑定后再次复核，project 4/device 4 的新绑定与唯一在线的
  Allwinner FEL/FES 设备位置一致，VID:PID 为 `1f3a:f008`；工作台空闲且 Supervisor
  自动任务为空。
- 二次预检解包审计了 vendor package 的 `sys_partition.fex` 和 `dlinfo.fex`：仅
  boot-resource、env、bootA、rootfsA 带 `downloadfile`，但镜像同时包含 GPT/MBR，且
  p7/rootfs_data、p8/private、p9/UDISK 均没有可证明量产保留语义的 `keydata=1` 标记。
  LYNX/openixcli 能力默认是 `full_erase`，公开接口没有 `full_erase=false` 或排除
  p7/p8/p9 的可验证契约。因此数据保留门禁为 FAIL，未调用 `lynx_start_flash`、未创建
  taskId，eMMC 没有发生写入。
- 板卡当前保持在 FEL/FES 待机态。电源配置是 `dualPower` 且关联次级通道 5，为避免影响
  device 5，收尾阶段没有执行电源循环；串口句柄和临时 SDK 审计目录均已清理。

实机操作前新增源码备份：
`/home/ubuntu/AI-DeskTopBox-backups/20260915T074716-0400/`；工作树归档 SHA-256 为
`ae18843f0de004a432e2fba59646ee2aea88df333c78d75bbb3a82728ed2f072`，Git bundle 为
`2d8886bb5ae999bbd4b7374f35d8a327d2f58c52819911f78c84ab6568ddcb21`。

获得操作员对全擦、分区恢复及 dualPower 联动通道 5 的明确授权后，烧录前又创建了最终
源码快照 `/home/ubuntu/AI-DeskTopBox-backups/20260915T104119-0400/`。完整工作树归档
SHA-256 为 `87211b3bfdcc43369f4e3a29c23e912ff4af910c2df106fafdb304ba6b29f95e`，Git bundle
SHA-256 为 `63a422230fe9cb5ce256e1614b1104158c94915cb115dbfbef57a0f48ac2ff9b`；归档可读且
bundle 为完整历史。第一次生成的 `20260915T103618-0400.incomplete` 因 tar 检测到
`.git` 变化而退出，已明确标记为不完整且未用于门禁。

板卡恢复备份位于
`/home/ubuntu/AI-DeskTopBox-backups/device4-board-20260915.1KB0A2/`，包含 p7/rootfs_data
文件归档与原始镜像、p8/private 原始镜像、p9/UDISK 文件归档与原始镜像以及独立工厂身份
归档，全部权限为 `0600`，tar/gzip 自检和 SHA-256 已完成。临时 SSH 授权恢复为原先不
存在，ControlMaster、本机临时密钥和串口句柄均已清理。

## A133 全擦、恢复与实机结果

### 烧录与数据恢复

- LYNX project 4/device 4 的工作台、唯一 FEL/FES 设备及 USB 绑定复核通过。
- `lynx_start_flash` 只调用一次：taskId `flash-1789483979656842000`，参数为
  `full_erase`、`verify=true`、`postFlashAction=reboot`；未换镜像、未自动重试。
- 任务以 `exitCode=0` 结束，进度 100%，`committedBytes=552829952`，
  `verifyState=success`、校验错误码 0，自动重启成功。
- 新 eMMC 为约 14.6 GiB、9 个用户分区。p7 为 524288 sectors，p8 为 1024 sectors，
  p9 为 25413599 sectors。p7/rootfs_data 和 p9/UDISK 使用文件归档恢复；p8 在确认未挂载
  且精确为 524288 bytes 后只写入一次 raw，整分区回读 SHA-256 为
  `07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541`。
- p9 实测约 12.1 GiB；仍按预定安全规则禁止写入旧 p9 raw，只恢复文件内容。工厂 License
  已随 p7 恢复，未使用独立 factory fallback，也没有读取或输出其内容。
- 恢复后的软件重启与授权的双路物理断电 2 秒冷启动均成功。LYNX 报告通道 4、5 恢复
  `on`，device 4 约 35 秒恢复网络；冷启动后 p7/p9 挂载和 p8 哈希保持正确。

### 冷启动后验证矩阵

| 检查项 | 结果 | 实机证据 |
| --- | --- | --- |
| 固件/启动 | PASS | `1.0.7`、`f1193618-dirty`、构建 `202609150636`；Linux 4.9.191 |
| 分区恢复 | PASS | `/overlay`、`/mnt/UDISK` 正常；p8 冷启动后回读哈希一致 |
| 核心服务 | PASS | LVGL/backend、Tuya、appd、controld、IPKVM/video、Wi-Fi、Bluetooth 均运行 |
| 网络 | PASS | `wlan0=192.168.1.37/24`；网关测试 0% 丢包 |
| Tuya 基础状态 | PARTIAL | 本机进程自报 `control=online`、`bound=1`、`err=0`；云侧辅助状态同时提示设备签名不可用/格式异常，未读取或改写身份材料 |
| 蓝牙 | PASS | `hci0 UP RUNNING`，powered，Audio Sink 与 AVRCP UUID 存在 |
| USB Gadget/HID | PASS | `18d1:d015`、`bcdDevice=0x0428`、UDC `configured`、8-byte hidg0、4-byte hidg1 |
| HID/Agent 释放 | PASS | 只发送最终全零释放报告；两个 lock 均不存在 |
| 传感器基础 | PASS | AP3216C、红外、触摸、电源键、温度节点可枚举/读取 |
| WS2812 基础 | PASS | `/dev/ws2812-leds` 存在且由 `lv_backend` 持有 |
| LED 25 Hz/去重 | NOT_RUN | 无专用示波/事件计数证据 |
| APP 沙箱基础 | PASS | `CONFIG_SECCOMP=y`、`CONFIG_SECCOMP_FILTER=y`、app socket 存在 |
| 签名 APP 隔离 | NOT_RUN | 本轮没有准备测试应用实例 |
| 音频输出 | PASS | 校验 WAV 经 HPOUT 与 LINEOUT 播放均返回 0 |
| 麦克风独立录音 | FAIL | `CaptureMic` 被运行中的 Tuya 音频服务占用；为避免破坏已在线会话，没有强停 Tuya |
| 默认音量 50% | FAIL | 产品持久化设置实测为 20%，对应 `Headphone Volume` 1/7；`digital volume` 63/63 是编解码器增益基线，不是用户音量百分比 |
| 防自触发基础 | PASS | 两次测试音前后相关 Tuya 日志标记保持 11→11 |
| 真人唤醒/打断 | NOT_RUN | 现场没有真人语音输入 |
| IPKVM 控制面 | PASS | `web=running`、`control=ready`、`stream=ready` |
| 本地 HDMI 预览 | PASS | 操作员确认本机画面始终存在且正在操作；后续 LT6911 为 `detect=1`、`1920×1080@60` |
| 板端 HDMI JPEG | FAIL | IPKVM 快照路径返回 `ERROR no-frame`；取证发现本地 `hdmi_preview --service --display` 正持有 `/dev/video0` |
| 浏览器 WebRTC | NOT_RUN | 操作员正在使用本机画面，本轮停止切换 Capture 所有权，未建立新浏览器会话 |
| 1072→1080 底边 | NOT_RUN | 无有效 HDMI 帧可检查 |
| Agent observation-only | NOT_RUN | IPKVM 路径没有当前帧，且操作员正在使用本机预览，按安全策略未启动 |
| ≥250 ms HID 动作 | NOT_RUN | 缺少 IPKVM 动作前后新帧，禁止盲发按键或鼠标 |
| 严格 TLS/签名 OTA/回滚 | NOT_RUN | 本发布为 `WITH_OTA=0`，无签名 `.swu` 测试产物 |

因此本次结论是：**烧录与指定数据恢复 PASS，整机综合验收 FAIL**。HDMI 输入与本地预览
目前正常，剩余阻塞已收敛为本地预览和 IPKVM 之间的 Capture 所有权切换/验收路径。在不
中断操作员当前本机画面的前提下，不能把浏览器画面、底边重采样、Agent observation-only
或动作 HID 记为通过。

### HDMI 与音量跟进诊断

- 上轮无信号阶段，`/dev/video0` 的唯一持有者是产品视频进程 `aitvbox-kvm-video`；独立探针
  返回 busy 只是正常的独占所有权表现。产品自身 snapshot 同样返回 `ERROR no-frame`。
- LT6911 驱动已绑定，MIPI/CSI 时钟和 VIN 流程能够启动，但内核持续报告 `detect=0`、
  活动区 `0x0`、HS/VS 宽度为 0 和 `invalid HDMI timings`。这把故障边界收敛到外部 HDMI
  源未输出有效时序、线缆/接口链路，或 LT6911 未锁定，而不是浏览器 WebRTC 层。
- 只执行了一次受支持的 `aitvbox-ipkvmctl restart` 恢复尝试。停止路径超过脚本窗口未返回，
  随即用 Ctrl-C 安全中断；服务 PID 和 `/dev/video0` 所有权未改变，没有强杀 VIN、重复重启
  或猜测性访问 I2C/寄存器。板端未发现适用于 LT6911 的受支持 HPD/EDID sysfs 恢复入口。
- 由于没有新 HDMI 帧，未执行 Agent 或实际 HID 动作，也没有通过输入去盲调本机设置页。
  产品音量的受支持入口是本机设置页滑块；当前持久化值为 20%，因此保留现状并判定 50%
  默认音量门禁失败。`digital volume=63/63` 不再作为 100% 用户音量证据。
- 麦克风独立录音仍受 Tuya 的 `CaptureMic` 独占影响；本轮没有为单项测试中断在线 Tuya
  会话。最后复核核心服务、网络、蓝牙、UDC/HID、传感器和 WS2812 基础状态仍在，HID/Agent
  锁均不存在且没有进程持有 hidg0/hidg1。

操作员接入并持续使用 HDMI 源后的补充取证修正了上面的历史判断：LT6911 已转为
`detect=1`，有效区为 `1920×1080`，VIN 映射为 `1920×1080@60`，本地预览一直有画面。
此时 `/dev/video0` 由后起的 `hdmi_preview --service --display` 持有，而
`aitvbox-kvm-video` 的采集请求记录为 `video0 open busy`，其共享 snapshot 因而返回
`ERROR no-frame`。这不是 HDMI 源无信号，而是两个产品消费者之间的所有权切换尚未进入
IPKVM 路径。源码已有 `/var/run/aitvbox/hdmi-ipkvm.lock` 仲裁：浏览器或 Agent 会话必须先
创建 owner lock、等待本地预览释放，再启动 IPKVM 单一采集管线；直接在本地预览占用期间
运行 `hdmi_preview --snapshot` 不能作为 IPKVM 无画面的充分证据。

在操作员说明仍在操作后，后续 Agent、浏览器和 HID 测试已立即停止。此前已经发出的一次
device 4 软件重启无法撤回；没有执行 dualPower、强杀进程或第二次重启，受控串口在发送
reboot 后立即关闭。

临时 IPKVM 配置已逐字节恢复，SHA-256 为
`fdd09aa06228c6fb5c4ca51039f9051f03ec44254ec4b3dbbc6ecfac36658a6e`。任务 SSH 公钥已删除
且登录实测被拒绝；板端恢复包、音频文件、本机临时私钥和认证副本均已清理。串口句柄关闭，
Supervisor 任务为 0，最终 LYNX 工作台空闲。收尾复核确认 device 4 在线、`busy=false`，
串口句柄 `serial-7` 已关闭。

## 尚未关闭的硬件门禁

当前阶段完成了 Port ABI、Provider/manifest、IPC V2 并行服务、统一 CLI、开源治理和门禁，
也完成了 A133 全量刷机、分区恢复、启动、网络/蓝牙、UDC 和双 HID 基础验证。独占 daemon
切换仍须在操作员允许短暂切换 Capture 所有权后补齐 1072→1080 JPEG/WebRTC 底边、Agent
动作后新帧和受控 HID，
并补齐真人音频打断、LED 时序、签名 APP 沙箱及签名 OTA/回滚测试后才可合并。

当前开发仓含厂商二进制、生成目录和工厂工具输入，不能直接公开。轮换历史信任材料、完成
第三方许可证人工复核并使 `tools/public_release_audit.py` 通过后，才能用
`tools/export_public_source.py` 初始化全新公共历史。
