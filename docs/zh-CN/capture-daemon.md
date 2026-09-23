# Capture Daemon 编译、移植与验证

## 目标结构

```text
/dev/video0 -> Capture Provider -> 一次行修复/色彩处理 -> 共享帧
                                                        |-> 本地 Display
                                                        |-> JPEG 快照
                                                        `-> H264 -> WebRTC
```

`aitvbox-captured` 是视频设备唯一拥有者。`lv_backend` 只控制本地显示订阅，IPKVM Go
进程只控制 WebRTC 订阅；`hdmi_preview --audio-only` 只管理 HDMI 音频。这样不会因为 UI
预览和浏览器连接互相抢占 `/dev/video0` 而出现黑屏。

## A133 编译

先按平台 manifest 设置 SDK 路径，再分别构建采集进程、音频辅助进程、后端和 Go 服务：

```sh
make -C apps/ipkvm/video SDK_ROOT="$A133_SDK_ROOT" BUILD_DIR=build
make -C apps/hdmi_preview SDK_ROOT="$A133_SDK_ROOT" BUILD_DIR=build
cmake -S apps/lv_port_linux -B apps/lv_port_linux/build \
  -DCMAKE_TOOLCHAIN_FILE="$A133_TOOLCHAIN_FILE"
cmake --build apps/lv_port_linux/build --target lv_backend
(cd apps/ipkvm/upstream && go test ./... && go build ./cmd/...)
```

打包时 `aitvbox-kvm-video` 保留为兼容文件名，同时创建 `aitvbox-captured` 符号链接。
IPKVM 与 suite 的 init 脚本必须设置 `AITVBOX_SHARED_CAPTURE=1`，并使用同一个
`AITVBOX_CAPTURE_SOCKET`（默认 `/var/run/aitvbox/capture.sock`）。

## 控制协议

Unix stream socket 每次接收一行命令：

- `STATUS`：返回单行 JSON，包括 `state`、`capture`、`displayRequested`、`display`、
  `streamRequested`、`frames`、`frameAgeMs`、`recoveries`、`width`、`height` 和 `fps`。
- `DISPLAY 1` / `DISPLAY 0`：开启或关闭本地显示消费者。
- `SNAPSHOT`：返回 4 字节网络序长度和 JPEG 数据。

WebRTC 控制和 Annex-B H264 媒体仍使用 IPKVM 原有 socket，不与上述控制面混用。

## 低延迟约束

分层不等于在采集线程里串行等待所有消费者。A133 的共享帧分发顺序固定为：先提交硬件
编码，再处理按需快照，最后更新本地 Display。这样本地显示的缓存同步和图层 ioctl 不会
增加当前帧进入 IPKVM 网络路径的时间。新平台即使采用独立 Display worker，也必须使用
“最新帧优先、满则丢旧帧”的有界邮箱，禁止反向阻塞 Capture Provider。

Go WebRTC writer 使用 6 帧有界队列：按 60 fps 计算，应用层最多积压约 100 ms。队列满
时丢弃已经失去参考链的 GOP，并等新的 IDR 恢复，不能让网络抖动拖慢采集线程。浏览器端
在支持 `RTCRtpReceiver.playoutDelayHint` 时请求零额外播放缓冲；不支持该属性的浏览器
保持标准行为。

浏览器信令连接必须覆盖固件升级和服务维护窗口：WebSocket 不以固定 15 次重试作为终止
条件；连接断开时关闭旧 `RTCPeerConnection`，信令恢复后用新的设备元数据重新协商。
WebRTC 进入 `failed` 也应触发相同恢复流程，不能把页面永久停在黑屏状态。
旧连接遗留的超时、状态和 ICE 回调必须先核对连接所有权；它们只能清理创建自己的 peer，
不能通过全局引用关闭已经恢复的新 peer，否则维护重启后会形成反复重连和黑屏。
密码模式的当前会话令牌必须以 `0600` 配置原子持久化，使同一个浏览器的 session cookie
能跨 Web 服务维护重启继续认证；登录时轮换令牌，显式退出时立即清除，浏览器关闭时仍由
session cookie 的生命周期终止客户端会话。
如果升级前的旧会话令牌本来没有落盘，WebSocket 握手会返回 `401`。桌面端和移动端会再
用受保护的 HTTP 接口确认认证确实失效，然后自动跳转到登录页；普通网络中断或服务重启
仍保持原页面自动重连。

`STATUS.fps` 是平台配置的标称帧率，不是实时测量值。验收实际节拍时，应以带单调时间戳
的连续 `frames` 差值计算；不得用一次一秒 `sleep` 后的整数差值直接宣称精确 fps。

## 新平台适配

新 SoC 只替换三个平台实现：Capture V2、Display 和 Video Encoder。Capture 返回带显式
生命周期的 `aitvbox_video_frame_v2_t`；显示与编码器必须消费同一帧或同一 DMA-BUF，
不得建立第二条采集链。像 A133 的无效尾行修复属于 Provider，通用服务不出现 SoC 宏。

平台 bring-up 顺序：状态/热插拔、快照、本地显示、硬件编码、WebRTC、长稳。只有全部
证据归档后，才能把 platform manifest 从 `skeleton` 改为 `validated`。

## 板端验收

1. 启动 IPKVM 与 suite，确认只有 `aitvbox-captured` 持有视频节点。
2. 连续查询 `STATUS`，确认 `frames` 增长、`frameAgeMs` 小于 2 秒且 `display=true`。
3. 保存快照，检查 JPEG 可解码、非全黑且 1080 行底部无重复/绿边。
4. 浏览器连接 WebRTC，确认产生解码帧；断开后 `streamRequested=false`，本地显示继续。
5. 在 `streamRequested=true` 时采集至少 10 秒的帧计数、帧龄、浏览器解码/丢帧统计和
   端到端动作响应；本地显示开启与关闭各测一轮，记录准确起止时间。若 UI 后端会持续
   恢复显示，必须通过它的正式控制接口切换，不能把瞬时关屏样本当成有效 A/B。
6. HDMI 拔插或输入超时后，确认 `recoveries` 增加并在信号恢复后自动出图。
7. 连续连接/断开至少 50 次，确认没有第二个视频节点拥有者、fd 泄漏或进程重启循环。
8. 保持浏览器页面打开，单独重启 Web 服务并等待其恢复；确认无需刷新页面即可重新协商，
   `streamRequested` 自动回到 `true`，且采集进程 PID、帧推进和本地显示不受影响。

`/usr/bin/aitvbox-ipkvmctl status` 会同时检查进程、控制/媒体 socket 和 Capture 状态；
无信号可以是可诊断状态，但 socket 不可访问或采集状态不可读取应判为失败。
