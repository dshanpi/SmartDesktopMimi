# 构建说明

构建产品应用前，必须先准备好 Tina SDK（它提供交叉工具链与 OpenWrt staging 目录）。

正式发布优先使用一键入口；它会强制应用干净重编、完整 SDK build/pack、rootfs 验收并归档
哈希与日志：

```sh
cd /home/ubuntu/AI-DeskTopBox
./scripts/auto_build_package.sh --check-only
./scripts/auto_build_package.sh
```

一键入口会按参数、环境变量、默认路径和同级目录的顺序自动发现 SDK，汇总检查构建命令、
Go/Node、CPU/内存/磁盘、A133/B6 目标规格和受限 vendor 输入，再调用
`build_release.sh`。SDK 不在默认路径时使用 `--sdk-root <目录>`。

完整操作手册和发布目录说明见 [SDK 完整编译与发布手册](sdk-build-and-release.md)。下文保留
分层构建原理和故障排查细节。

Tina SDK **不是产品编译工作区**，它只提供交叉工具链、OpenWrt staging 目录、rootfs 构建与固件 pack 工具。
本产品仓库拥有源码、打包规则、集成脚本和 vendor 输入，并在仓库内完成 out-of-tree 应用构建。

## 架构：out-of-tree 构建

产品代码与 SDK 严格分离：

- 应用源码（`apps/`、TuyaOpen、vendor）只在产品仓库，**不拷进 SDK**。
- 应用在产品仓库内 out-of-tree 构建，只借用 SDK 的 toolchain + staging_dir，
  产物统一落到产品仓库 `build/`。
- SDK 侧的 `aitvbox-suite`、`aitvbox-platform`、`aitvbox-usb-hid` 和
  `aitvbox-ipkvm` 是薄包。
  install 阶段从产品仓库 `build/`、`vendor/`、`apps/` 和 `platform/` 读产物；
  SDK 默认不启用这些包。
- `build_firmware.sh` 在调用 SDK 构建前 `export AITVBOX_PRODUCT_BUILD=<产品仓库根>`，
  校验产物就位，临时打开四个 `CONFIG_PACKAGE_aitvbox-*`，结束后恢复纯 SDK 模式
  并清理 OpenWrt 增量 rootfs 中的产品残留。

产物布局（由 `build_apps.sh` 生成到 `build/`）：

```text
build/lv_port_linux/{lv_backend, lvglsim}
build/hdmi_preview/hdmi_preview
build/platform/{aitvbox-mcpd, aitvbox-agentd, aitvbox-controld,
                aitvbox-appd, aitvbox-app-policy}
build/ipkvm/{aitvbox-ipkvmd, aitvbox-kvm-video}
build/tuyaopen/your_chat_bot_QIO_1.0.1.bin
build/aitvbox-version
build/aitvbox-tuya-pid
```

## SDK 模式边界

SDK 有两种模式，默认应始终处于纯 SDK 模式：

```text
纯 SDK 模式:
  CONFIG_PACKAGE_aitvbox-{suite,platform,usb-hid,ipkvm} 全部关闭
  裸跑 source build/envsetup.sh && ./build.sh 不依赖产品仓

产品固件模式:
  只由 build_firmware.sh 临时进入
  临时打开四个 AITVBox 产品包
  export AITVBOX_PRODUCT_BUILD=<产品仓库根>
  pack 结束或失败后恢复纯 SDK 模式
```

相关脚本：

```text
scripts/sync_to_sdk.sh                  # 只同步薄包，不开产品包
scripts/toggle_product.sh on|off <sdk>  # 显式开关三处 OpenWrt 配置
scripts/clean_sdk_product_artifacts.sh  # 清理 SDK 输出目录里的产品残留
scripts/build_firmware.sh               # 产品固件唯一推荐入口
```

## 从全新机器开始构建

先准备 Tina SDK：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
source build/envsetup.sh
./build.sh
```

clone 本产品仓库并初始化 submodule：

```sh
cd /home/ubuntu
git clone <AI-DeskTopBox.git> AI-DeskTopBox
cd /home/ubuntu/AI-DeskTopBox
git submodule update --init --recursive
```

submodule 步骤是必需的，它会拉取：

- `apps/lv_port_linux/lvgl`: 产品 LVGL fork。
TuyaOpen 已固化在普通目录 `third_party/TuyaOpen/`，不是 submodule；构建脚本从该目录生成
可写工作副本 `.tuyaopen-build/`。

## 同步薄包到 SDK

`sync_to_sdk.sh` **不再搬源码进 SDK**，只拷四个 AITVBox 薄包；它不会打开
任何产品包，因此裸跑 SDK 仍保持纯 SDK。修改 SDK 前先用 dry-run 检查：

```sh
./scripts/audit_sdk.sh /home/ubuntu/A133-Tina5.0-v0.9
./scripts/sync_to_sdk.sh --dry-run /home/ubuntu/A133-Tina5.0-v0.9
```

确认 dry-run 无误后执行真实同步（会覆盖 SDK 现有 AITVBox 包，建议先备份）：

```sh
./scripts/sync_to_sdk.sh --allow-overwrite /home/ubuntu/A133-Tina5.0-v0.9
```

`sync_to_sdk.sh` 从不删除文件；默认拒绝覆盖已有 SDK 路径，需 `--allow-overwrite`。

## 构建应用

在产品仓库内构建应用（产物落 `build/`）：

```sh
./scripts/build_apps.sh /home/ubuntu/A133-Tina5.0-v0.9
```

`build_apps.sh` 依次 out-of-tree 构建 LVGL、HDMI 预览、平台服务、浏览器 IPKVM 和
TuyaOpen 聊天机器人，产物归集到 `build/`。IPKVM 操作见
[IPKVM 使用说明](ipkvm-user-guide.md)，TuyaOpen 构建源与 venv 见
[tuyaopen.md](tuyaopen.md)。

公开构建默认不包含外部 100ask SDK：`lv_backend` 使用无网络 stub，顶栏保留灰色云图标，
设置页明确显示此构建未启用云服务。内部固件必须显式提供私有 SDK，禁止自动探测：

```sh
AITVBOX_ENABLE_100ASK_CLOUD=ON \
AITVBOX_100ASK_SDK_ROOT=/absolute/private/100ask-iot-sdk \
./scripts/build_apps.sh /home/ubuntu/A133-Tina5.0-v0.9
```

SDK 目录必须包含 `include/100ask_iot.h` 和 `src/100ask_iot.c`。启用但路径或 Mosquitto
依赖缺失时构建会立即失败；默认公开构建不会准备或链接 Mosquitto。

## 打产品固件

打固件（SDK 负责 rootfs 组装 + pack）：

```sh
./scripts/build_firmware.sh /home/ubuntu/A133-Tina5.0-v0.9
```

`build_firmware.sh` 默认走完整 SDK 构建（`./build.sh`）后再 `pack`，这是发布安全路径，
等价于手动流程：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
source build/envsetup.sh
./build.sh
./build.sh pack
```

`build_firmware.sh` 会临时打开四个 AITVBox 产品包，打包完成或失败后都会恢复
SDK 纯净模式并清理产品残留。`build_firmware.sh` 始终执行完整 SDK 构建 + pack，
不允许只构建 rootfs——在该 SDK 上，仅打 rootfs 即便成功，boot 链或板级 pack 输入
可能仍是旧态，产出不可靠。

### 陷阱：pack 与 clean 的顺序

`build_firmware.sh` 内部顺序是 **clean → sync 薄包 → toggle on → (可选 OTA 注入) → export AITVBOX_PRODUCT_BUILD → 校验产物 → build.sh → bluetooth_init 缓存覆盖 → pack → trap(toggle off + clean)**。
关键点：**pack 在末尾的 clean 之前执行**，所以打包用的 `rootfs.img` 是带产品文件的，正确。

但 **build_firmware 退出后 SDK 已被恢复成纯态**——`build_dir/target/root-a133-b6/` 里的
产品文件已被 `clean_sdk_product_artifacts.sh` 清掉。此时若手动再跑 `./build.sh pack`，
pack **不会重建 rootfs**，它复用上一次（带产品的）`rootfs.img`，所以仍能打出带产品的固件——
这步本身安全。

**真正会出错的是**：build_firmware 之后手动跑了 `clean_sdk_product_artifacts.sh`（或
`toggle_product.sh off`，后者会顺带 clean），**再**单独 `./build.sh pack`。这时若 OpenWrt
增量构建恰好重建了 rootfs（产品包开关已 off），新 `rootfs.img` 就不含产品文件，pack 出来是
无产品固件。

规避：

- **要重新 pack，直接重跑 `build_firmware.sh`**（它会走完整 on→build→pack→off 闭环）。
- **不要**在 build_firmware 之后单独 `clean` + `pack`。需要纯 SDK pack 时，先确认
  `rootfs.img` 是你期望的那一份。
- `rootfs.img` 是否含产品文件可用 `debugfs` 抽查（见下节"打进 rootfs 的产品文件"）。

### 入口约束

- **出产品固件只能走 `build_firmware.sh`**：它负责 on → export `AITVBOX_PRODUCT_BUILD` →
  校验产物 → build → pack → off 的完整闭环。直接 `cd SDK && ./build.sh` 是纯 SDK（开关 off），
  不带产品；若开关残留 on 又没 export，薄包 Makefile 会 `$(error)` 中止。
- **`build_firmware.sh` 前必须先 `build_apps.sh`**：前者校验 `build/` 下全部产品产物就位，
  不自动编译。产物缺失会报错并提示运行 `build_apps.sh`。两者故意分离，可独立重跑。
- **`sync_to_sdk.sh` 只需在薄包更新时跑一次**：它只同步薄包、不开开关，跑完 SDK 仍是纯态。

生成的固件镜像在 SDK 输出目录：

```text
/home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/a133_linux_b6_uart0.img
```

## 打进 rootfs 的产品文件

`aitvbox-suite` 自身安装：

```text
/usr/bin/lv_backend
/usr/bin/lvglsim
/usr/bin/hdmi_preview
/usr/bin/your_chat_bot_QIO_1.0.1.bin
/usr/bin/aitvbox-start-ui
/usr/lib/libMNN.so
/usr/lib/libMNN_Express.so
/usr/share/tuyaopen_models/mdtc_chunk_300ms.mnn
/usr/share/tuyaopen_models/tokens.txt
/usr/share/fonts/SarasaUiSC-Regular.ttf
/usr/share/fonts/SarasaUiSC-SemiBold.ttf
/usr/share/aitvbox/emotions/*.json       # 10 个 Lottie 情绪素材
/etc/init.d/aitvbox-data
/etc/init.d/aitvbox
/etc/rc.d/S95aitvbox-data
/etc/rc.d/S99aitvbox
/etc/swupdate_public.pem                  # OTA 验签公钥
/etc/aitvbox-version                      # 固件版本戳
/etc/100ask/                              # 100ask Cloud（空目录出厂，可选 cloud.conf/ca.crt）
```

> 完整清单以 `packaging/aitvbox-suite/Makefile` 的 install 段为准。

`aitvbox-platform` 安装：

```text
/usr/bin/aitvbox-{mcpd,agentd,controld,appd,app-policy}
/usr/bin/aitvbox-{agentctl,appctl,adminctl}
/etc/init.d/aitvbox-{control,apps}
```

`aitvbox-usb-hid` 安装 HID/ADB 模式脚本、`aitvbox-hidctl` 和 USB gadget
启动服务。产品内核配置还必须包含 seccomp/filter，供可信 native APP 沙箱使用。
开发机 APP SDK 位于产品仓库 `platform/app-sdk/`，不需要复制到设备 rootfs。

`aitvbox-ipkvm` 安装：

```text
/usr/bin/aitvbox-ipkvmd
/usr/bin/aitvbox-kvm-video
/usr/bin/aitvbox-ipkvmctl
/etc/init.d/aitvbox-ipkvm
/etc/rc.d/S98aitvbox-ipkvm
```

网页资源嵌入 `aitvbox-ipkvmd`，rootfs 不需要单独复制静态目录。WebRTC 固定使用
`40000/udp`，外网 FRP 必须同时转发网页 TCP 和该 UDP 端口。

此外 `aitvbox-suite` 依赖 `boot-play`，所以产品固件还会包含 `boot-play` 包提供的：

```text
/sbin/boot-play
/etc/init.d/play
/etc/rc.d/S25play
/usr/res/boot-play/
```


## 构建后验证

检查固件与 rootfs：

```sh
ls -lh /home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/a133_linux_b6_uart0.img
sha256sum /home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/a133_linux_b6_uart0.img
```

检查最终 `rootfs.img` 是否包含产品文件：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
debugfs -R 'stat /usr/bin/lv_backend' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/lvglsim' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/aitvbox-start-ui' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/aitvbox-appd' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/aitvbox-controld' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /etc/init.d/aitvbox' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /etc/init.d/play' out/a133/b6/openwrt/rootfs.img
```

检查 SDK 已恢复纯模式：

```sh
grep -E "CONFIG_PACKAGE_aitvbox-(suite|platform|usb-hid)" \
  /home/ubuntu/A133-Tina5.0-v0.9/openwrt/target/a133/a133-b6/defconfig \
  /home/ubuntu/A133-Tina5.0-v0.9/openwrt/openwrt/.config \
  /home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/tmp/.config
```

## 纯 SDK 模式

若协作者只想构建 Tina SDK、不 clone 本产品仓库，四个 AITVBox 产品包都应保持关闭。
此时 SDK 构建不会打包 AITVBox 应用、平台服务或 USB 策略。

若 SDK 已被 sync 打开过该开关，用 `toggle_product.sh off` 关闭即可回到纯 SDK 编译：

```sh
./scripts/toggle_product.sh off /home/ubuntu/A133-Tina5.0-v0.9
```

详见 [README.md](../README.md) 的"单独编译 SDK"一节。

## 常见问题

构建脚本的联网/前置步骤失败时会打印 `error:` + 缩进的修复建议。以下是高频坑的系统性参考。

### TuyaOpen submodule clone 失败

`prepare_tuyaopen_build_root.sh` 的 `git submodule update --init` 会 clone cJSON、FlashDB、
littlefs、backoffAlgorithm 等 GitHub 仓库。失败（`Could not resolve host` / `unable to access`）
通常是网络或代理问题：

```sh
# 验证 GitHub 可达性
git ls-remote https://github.com/DaveGamble/cJSON.git HEAD
# 配代理（如需）
git config --global http.https://github.com.proxy http://127.0.0.1:7890
```

可达后重跑 prepare 即可（submodule update 是幂等的）。

### platform/LINUX clone 慢或失败

`git clone` platform/LINUX（`TuyaOpen-ubuntu`，约 2.5G）较慢，耐心等待。若中途中断留下
半成品目录，重跑可能因目录已存在报错，用 `--clean` 重建：

```sh
./scripts/prepare_tuyaopen_build_root.sh --clean
```

### export.sh 下载 uv/python 失败

首次 `source export.sh` 需联网下载 uv v0.11.18 + Python 3.12.13（3-10 分钟）。失败检查网络。
一旦 `.tools/` 就位，后续重建 venv 本地完成、不联网。可加 `--no-export` 跳过
（仅当 `.venv` 已就位时）。

### `Tuya device license unavailable`

日志出现 `Tuya device license unavailable; waiting for factory provisioning.`，说明该设备
尚未写入一机一密，或 `/factory/tuya/license.env` 格式/权限不符合要求。无需重新编译，执行：

```sh
./scripts/provision_tuya_license.sh /secure/path/device-license.env
```

运行时会自动加载；详情见 [tuyaopen.md](tuyaopen.md) 的“Tuya 一机一密”一节。

### `AITVBOX_PRODUCT_BUILD is not set`

直接在 SDK 里跑 `./build.sh` 时，rootfs 阶段报这个错，说明产品包开关被打开但没走
`build_firmware.sh` 入口（它负责 export 该变量）。两个解法：

- 要产品固件 → 用 `./scripts/build_firmware.sh`（它会 export + on/off 闭环）。
- 要纯 SDK → `./scripts/toggle_product.sh off` 关掉开关再 `./build.sh`。

详见上文"陷阱：pack 与 clean 的顺序"和"入口约束"。

### `not a Tina SDK root`

传给脚本的 SDK 路径不对，或缺 `.repo` 目录。确认是完整的 Tina SDK 根（含 `.repo`、
`build/envsetup.sh`、`build.sh`）。

### 缺命令（git/rsync/python3/make/cmake）

脚本开头会检查必需命令，缺时提示 `apt install <pkg>`。一次性装齐：

```sh
sudo apt install git rsync python3 gawk make cmake
```
