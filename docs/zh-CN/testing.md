# 测试矩阵

## 主机门禁

公开源码门禁不需要 Tina SDK 或受限 provider。它统一执行架构与主机集成测试、
Go 测试及 race detector、设备端 UI 构建和定向 lint、公开发布审计与 SBOM 可复现性检查：

```bash
python3 tools/aitvbox.py test --platform a133 --scope public
```

`.github/workflows/public-ci.yml` 使用 Python 3.11、`go.mod` 声明的 Go 版本和
Node 22.21.1 执行同一命令，所有 GitHub Actions 均固定到完整提交 SHA。无历史候选仓
会自动进入 candidate 模式，禁止任何清单中声明排除的路径残留。

```bash
python3 -m unittest tests.architecture.test_architecture
python3 scripts/check_architecture.py
python3 -m unittest tests.platform.test_host_integration
(cd apps/ipkvm/upstream && /usr/local/go/bin/go test ./...)
(cd apps/ipkvm/upstream && /usr/local/go/bin/go test -race ./...)
(cd apps/ipkvm/upstream/ui && npm ci && npm run build:device && npm run lint:public)
python3 tools/public_release_audit.py --candidate  # 只在导出候选仓执行
```

工厂助手 Rust 测试要求支持 lockfile v4 的 Cargo；工具链过旧属于前置失败，不能记为跳过通过。所有改动过的 A133 C/C++ 组件必须交叉编译。IPC 覆盖拆包、粘包、非法 JSON、空/超长帧、信封校验、前向兼容字段、身份拒绝和重连；平台测试禁止通用层依赖厂商内容。

完整 gate 烧录后，验证启动、分区、服务、USB 自检、UDC configured、分离 HID 与全部释放。对同一任务栏采集板端 JPEG 和浏览器 `--video-only` 1920×1080 图，检查底边、WebRTC 重连与 Capture 唯一拥有者。

先跑 Agent 只观察任务，再执行 HID。每次动作后获取新 HDMI 帧，第三次相同无效动作必须阻止。测试至少 250 ms 按键、Meta/blur/visibility 释放、急停与崩溃释放，并验证 Wi-Fi、蓝牙、传感器、LED 25 Hz/重复抑制、初始音量 50%、`你好涂鸦`、防自触发、真人打断、APP 沙箱、严格 TLS 云连接、签名 OTA 和回滚。

串口只能经串口代理。结束前删除临时 SSH/暂存文件，按哈希恢复认证并释放 HID。报告记录提交/脏状态、平台、工具、命令、数量、哈希、板卡别名、证据、限制和回退提交；硬件未执行只能记 `NOT_RUN`。
