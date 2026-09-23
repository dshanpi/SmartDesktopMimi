# TuyaOpen 集成

TuyaOpen 是上游开源依赖。**产品仓已将 TuyaOpen 主仓源码、platform/LINUX、python 工具链
（`.tools`）整体 vendor 进 `third_party/TuyaOpen/`（删 `.git` 历史）**，构建全离线、
可复现，不再依赖 tuya/github 公网。TuyaOpen 与 platform/LINUX 的产品定制已固化进
`third_party/TuyaOpen/`（原 patches 已归档到 `integrations/tuyaopen/patches-archive/`），
真正编译时，prepare 脚本从这份已含定制的 vendored 源 rsync 出一份可写构建副本，不再打补丁。

## 来源与边界

产品仓上传这些内容：

```text
third_party/TuyaOpen/                         # vendored 源码（主仓+platform/LINUX+.tools，无 .git）
integrations/tuyaopen/build_a133.sh           # A133 交叉构建入口
integrations/tuyaopen/config/A133_B6.config   # A133_B6 配置
integrations/tuyaopen/patches-archive/*.patch # 已固化的 TuyaOpen/platform 补丁（归档参考，构建不再 apply）
vendor/tuyaopen-a133-b6-libs/                 # A133_B6 二进制库、头文件、模型
```

本地生成但不提交：

```text
.tuyaopen-build/      # 默认 TuyaOpen 可写构建副本（每次从 vendored 源 rsync 重建）
build/tuyaopen/       # chatbot 构建产物归集目录
```

vendored TuyaOpen 主仓源自上游某 commit 剥 `.git` 而来（已无 `.git` 历史，
首次 vendor 的 commit 号见仓库提交记录）；
platform/LINUX 源自 `TuyaOpen-ubuntu@811c996c`（platform_config.yaml 中的 repo/commit
现仅作元数据保留，不再用于克隆）。vendor 过程见 `scripts/vendor_tuyaopen.sh`。

## 准备构建副本

首次 clone 产品仓后，`build_apps.sh` 会自动调用 prepare；也可手动执行：

```sh
cd /home/ubuntu/aitvbox-product
./scripts/prepare_tuyaopen_build_root.sh
```

`prepare_tuyaopen_build_root.sh` 全程离线，会：

1. 从 `third_party/TuyaOpen`（已含固化定制）`rsync --delete` 出 `.tuyaopen-build/`
   （每次新鲜副本，保留 `.venv` 复用）。**不再打补丁**--定制已在 vendored 源里。
2. 将 `vendor/tuyaopen-a133-b6-libs/` 注入到 `platform/LINUX/tuyaos_adapter/src/tkl_audio/`。
3. 复制 `build_a133.sh`、`README_A133_B6.md`、`A133_B6.config` 到 chatbot app。
4. 注入本地凭据到 `tuya_config.h`。
5. source `export.sh`，用本地 `.tools`（uv 0.11.18 + python 3.12.13）建 `.venv`（不下载）。

可覆盖构建副本位置：

```sh
TUYAOPEN_BUILD_ROOT=/tmp/aitvbox-tuya-prepare-verify \
  ./scripts/prepare_tuyaopen_build_root.sh --build-root /tmp/aitvbox-tuya-prepare-verify
```

## 编译 Tuya chatbot

推荐通过产品统一入口编译：

```sh
./scripts/build_apps.sh /home/ubuntu/A133-Tina5.0-v0.9
```

单独编译 Tuya：

```sh
cd /home/ubuntu/aitvbox-product/.tuyaopen-build/apps/tuya.ai/your_chat_bot
./build_a133.sh /home/ubuntu/aitvbox-product/.tuyaopen-build /home/ubuntu/A133-Tina5.0-v0.9
```

产物：

```text
<tuyaopen-build-root>/apps/tuya.ai/your_chat_bot/dist/your_chat_bot_1.0.1/your_chat_bot_QIO_1.0.1.bin
/home/ubuntu/aitvbox-product/build/tuyaopen/your_chat_bot_QIO_1.0.1.bin
```

## .tools 与 .venv

`.tools/`（uv 0.11.18 + cpython 3.12.13，可移植二进制）已随 vendored 源提交在
`third_party/TuyaOpen/.tools/`，构建时 rsync 进 `.tuyaopen-build/.tools/`。`export.sh`
用本地 `.tools` 建 `.venv`--`.tools` 就位时 `tuya_install_uv`/`tuya_setup_python` 的存在性
检查会跳过下载，全程离线（export.sh 本身仍含 github/astral 下载回退路径，仅当 `.tools`
缺失时才会触发联网）。

`.venv/` 不提交（含本机绝对路径，不可移植），位于 `.tuyaopen-build/` 内，缺失时由
`export.sh` 用本地 `.tools` 重建，跨次复用。

`uv sync`（PyPI 依赖包）：本机若已有 `~/.cache/uv` 则命中缓存、不联网；全新机器首次
需联网一次。若需 100% 离线，用 `./scripts/vendor_tuyaopen.sh --with-uv-cache` 把
`~/.cache/uv` 一并 vendor 进 `.tools/uv-cache/`，prepare 会设 `UV_CACHE_DIR` 指向它。

## A133_B6 vendor 注入

官方 `TuyaOpen-ubuntu` 平台仓不包含 A133_B6 的 MNN/audio/opus 库和唤醒模型。
prepare 脚本会从产品仓 vendor 注入：

```text
vendor/tuyaopen-a133-b6-libs/MNN/          -> platform/LINUX/.../libs/A133_B6/MNN/
vendor/tuyaopen-a133-b6-libs/audio_subsys/ -> platform/LINUX/.../libs/A133_B6/audio_subsys/
vendor/tuyaopen-a133-b6-libs/opus/         -> platform/LINUX/.../libs/A133_B6/opus/
vendor/tuyaopen-a133-b6-libs/alsa/         -> platform/LINUX/.../libs/A133_B6/alsa/
vendor/tuyaopen-a133-b6-libs/models/       -> platform/LINUX/.../models/
```

这些文件也会由 `aitvbox-suite` 薄包注入最终 rootfs。

## Tuya 一机一密（运行时加载）

量产云预分配、出厂工具写入及返修恢复协议见
[AITVBox 出厂身份写入与涂鸦 License 量产标准](tuya-license-cloud-provisioning-spec.md)。

Tuya 聊天机器人运行时需要真实设备凭据（UUID + AuthKey）才能连上 Tuya MQTT
（`m2.tuyacn.com:8883`）。量产固件不得包含固定凭据：每台设备使用涂鸦平台分配的
唯一 UUID/AuthKey，在产线阶段写入持久化工厂目录。

### 固件与运行时行为

- `tuya_config.h` 只保留产品 PID，不定义 UUID/AuthKey。
- `prepare_tuyaopen_build_root.sh` 不再读取 `secrets/tuya_open.env`，同一份固件可烧录所有设备。
- `build_apps.sh` 从本次 A133 配置生成 `/etc/aitvbox-tuya-pid` 固件清单；出厂助手
  会把它与工位 `TUYA_PID` 和云端返回值交叉校验，避免取错 License 池。
- Linux `tuya_iot_license_read()` 在运行时读取 `/factory/tuya/license.env`。
- 缺失、格式错误、长度错误、非 root 所有、符号链接或权限不是 `0600` 时拒绝加载；涂鸦进程后台等待，文件写入后无需重启整机即可继续初始化。
- 可用环境变量 `TUYA_LICENSE_FILE` 覆盖路径，仅供开发和自动测试使用。

License 文件固定格式：

```sh
TUYA_OPENSDK_UUID=20字符UUID
TUYA_OPENSDK_AUTHKEY=32字符AuthKey
```

### 产线写入

正式量产不允许工位直接读取 License List。出厂助手先使用工位 mTLS 证书调用
量产云，云端将 CPUID 与唯一 UUID/AuthKey 永久预绑定，助手再通过 ADB stdin
写入，不在工位生成 AuthKey 明文临时文件：

```sh
FACTORY_API_BASE=https://factory.example.com \
FACTORY_CLIENT_CERT=/secure/station/client.crt \
FACTORY_CLIENT_KEY=/secure/station/client.key \
FACTORY_CA_FILE=/secure/station/ca.crt \
FACTORY_SIGNING_PUBKEY=/secure/station/device-signing.pub \
TUYA_PID=alon7qgyjj8yus74 \
FACTORY_ID=factory-01 LINE_ID=line-01 STATION_ID=station-01 \
BATCH_NUMBER=20260717-A FIRMWARE_SHA256=<img-sha256> \
./scripts/factory_provision_device.sh <adb_serial>
```

- 出厂助手同时写入 `device_sig` 和云端预绑定的原 UUID/AuthKey。
- 凭据不打印、不进命令行、不落工位临时文件；读回 SHA-256 且权限必须为 `0600`。
- `/factory/tuya` 由 S51 启动脚本 bind 到 `/overlay/factory/tuya`，因此 A/B OTA 不会覆盖。
- 量产云按 `AVAILABLE -> ASSIGNED -> DEVICE_STORED -> TUYA_VERIFIED` 推进；任何失败都重试原 assignment。
- 未绑定新机收到 `DIRECT_MQTT_CONNECTED` 并生成有效 `bind_url`，或已绑定返修设备
  收到 `MQTT_CONNECTED` 后，设备才通过
  已认证的 100ask MQTT 上报 `tuya_license_ready=true`；仅进程存活不算验证成功。
- 新机的 `bind_url` 验证发生在客户扫码之前，量产过程不要求客户账号或扫码。
- 用户恢复出厂只能清除 `/overlay/tuyadb` 中的激活/绑定数据，不得删除 `/overlay/factory/tuya/license.env`。
- 返修重刷使用 `FACTORY_MODE=repair`，量产云只返回该 CPUID 原 UUID/AuthKey，查无历史时禁止新分配。
- 返修丢失的 100ask `device_secret` 不经过出厂工具；prepare 只开启一次性轮换 grant，
  设备自行 provision 并保存新 secret。

开发机仍可以使用 `provision_tuya_license.sh <license.env>` 写测试 License，但文件模式
禁止用于正式量产。

## 已验证流程

已用干净临时目录验证：

```text
/tmp/aitvbox-tuya-prepare-verify
```

验证内容：

- 从 `third_party/TuyaOpen` clone 出可写构建副本。
- 拉取官方 `TuyaOpen-ubuntu` 到 `platform/LINUX`。
- （platform 定制已固化进 vendored 源，构建不再 apply 补丁。）
- 注入 A133_B6 vendor 库和模型。
- （TuyaOpen 主仓定制已固化进 vendored 源，构建不再 apply 补丁。）
- 首次生成 `.tools/.venv`。
- 使用 Tina SDK gcc 11.3.0 编译 `your_chat_bot_QIO_1.0.1.bin` 成功。

验证产物：

```text
/tmp/aitvbox-tuya-prepare-verify/apps/tuya.ai/your_chat_bot/dist/your_chat_bot_1.0.1/your_chat_bot_QIO_1.0.1.bin
sha256 cde37fc29f9838403e16fd4197ddb709f54a012fdf57d474d61fbe3d3e9e8095
```

## 升级 TuyaOpen

TuyaOpen 已 vendored，升级即重新 vendor：

1. 临时恢复 submodule 并更新到目标 commit（或直接在 `third_party/TuyaOpen` 拉取目标版本源码）。
2. 跑一次旧 prepare 把新 `platform/LINUX` 克隆到 `.tuyaopen-build/`（仅升级时联网一次）。
3. 重新运行 `./scripts/vendor_tuyaopen.sh`（按需加 `--with-uv-cache`），它会重新剥 `.git`、
   还原 platform 补丁、刷新 `.tools`，覆盖 `third_party/TuyaOpen/`。
   **⚠️ 会冲掉已固化的产品定制**（git 跟踪，`git diff third_party/TuyaOpen` 可见丢失）。
4. 按 `integrations/tuyaopen/patches-archive/README.md` 把归档补丁重新 apply 到新 vendored
   源（冲突手动解决），把定制重新固化进去。
5. `git add -f third_party/TuyaOpen`（强制，避开 `build/`/`.cache/` 等忽略规则）。
6. `./scripts/build_apps.sh <sdk>` 重新编译，记录产物 sha256。
