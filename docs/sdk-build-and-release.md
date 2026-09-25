# SDK 完整编译与发布手册

本文是 AITVBox 当前工程的固定发布入口。目标板与当前 SDK 基线如下：

```text
产品仓库: /home/ubuntu/AI-DeskTopBox
Tina SDK: /home/ubuntu/A133-Tina5.0-v0.9
目标板:   Allwinner A133 / B6 / UART0
版本文件: /home/ubuntu/AI-DeskTopBox/VERSION
```

SDK 不保存产品源码。产品源码、薄包定义和 vendor 输入均在本仓库；SDK 只提供 AArch64
交叉工具链、OpenWrt staging/rootfs 构建和 Allwinner pack。不要把两个目录合并，也不要把
SDK 生成目录提交到产品仓库。

## 首次准备

首次拿到一份新 SDK，先让纯 SDK 完整构建一次，以生成工具链和 staging：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
source build/envsetup.sh
./build.sh
```

然后初始化产品仓库唯一的 Git submodule（LVGL）：

```sh
cd /home/ubuntu/AI-DeskTopBox
git submodule update --init --recursive
```

`third_party/TuyaOpen/` 是仓库内固化的普通目录，不是 submodule。正式构建默认离线使用这里
的源码和 `.tools`，并在 `.tuyaopen-build/` 生成可写工作副本。

主机至少需要 `git`、`make`、`cmake`、`python3`、`rsync`、`gawk`、
`squashfs-tools`、`e2fsprogs`、`util-linux`、`coreutils`、Node/npm 和 Go。
前端以 Node 22.21.1 为可复现基线，Go 版本不得低于 `go.mod` 声明。
`auto_build_package.sh` 会在开始前一次性汇总缺项，验证受限 vendor 库、SDK 的
`A133/B6/OpenWrt/arm64` 配置和交叉编译器，并显示 CPU、内存、磁盘和目标板规格。
SDK 所在文件系统至少保留 15 GiB，建议 30 GiB；低于最低值会在编译前直接停止。

## 正式发布：先预检，再一键构建

```sh
cd /home/ubuntu/AI-DeskTopBox

# 只检查，不编译、不创建发布目录
./scripts/auto_build_package.sh --check-only

# 普通单槽正式固件
./scripts/auto_build_package.sh
```

脚本默认自动使用 `AITVBOX_A133_SDK`、兼容变量 `AITVBOX_TINA_SDK`、固定默认路径，
或仓库同级唯一的 `A133-Tina*` 目录。存在多个候选时不会猜测，需显式指定：

```sh
./scripts/auto_build_package.sh --sdk-root /path/to/A133-Tina-SDK
```

`build_release.sh` 固定执行以下闭环：

1. 校验版本号、Git 已跟踪源码、submodule、主机工具和 SDK 输入。
2. dry-run 检查四个产品薄包的同步范围，并生成构建前审计报告。
3. `build_apps.sh --clean` 清理由本工程管理的旧对象，重新编译全部应用。
4. `build_firmware.sh` 临时打开产品包，执行完整 Tina `./build.sh` 和 `./build.sh pack`。
5. 直接检查最终 `rootfs.img` 的产品可执行文件、root 权限、HID/seccomp 内核配置和私钥泄漏。
6. 退出前恢复 SDK 纯模式，再做一次审计。
7. 归档最终镜像、应用哈希、SDK 基线哈希、构建日志和统一 `SHA256SUMS`。

脚本默认拒绝已跟踪源码有未提交修改，避免生成无法复现的正式固件。确实需要做临时工程包时
可以显式加 `--allow-dirty`，其脏状态会写入 `BUILD-MANIFEST.txt`。未跟踪且不参与构建的
本地资料不会阻断发布。

## 发布产物

默认发布目录格式：

```text
build/releases/<VERSION>-<12位commit>-<UTC时间>/
├── BUILD-COMPLETE
├── BUILD-MANIFEST.txt
├── SHA256SUMS
├── firmware/a133_linux_b6_uart0.img
├── logs/
└── metadata/
    ├── aitvbox-version
    ├── aitvbox-tuya-pid
    └── APPLICATION-SHA256SUMS
```

最终烧录文件是 `firmware/a133_linux_b6_uart0.img`。`BUILD-COMPLETE` 存在且
`sha256sum -c SHA256SUMS` 全部通过，才表示包完整：

```sh
cd /home/ubuntu/AI-DeskTopBox/build/releases/<本次目录>
sha256sum -c SHA256SUMS
```

若还要归档 SDK 的 `boot.img` 和 512 MiB `rootfs.img`，使用：

```sh
./scripts/auto_build_package.sh --include-sdk-images
```

这两个中间镜像较大，默认只在清单中记录它们的 SDK 路径和哈希，不重复复制。

## 构建签名 OTA 包

OTA 发布要求本机已有未入 Git 的：

```text
secrets/swupdate_priv.pem
secrets/swupdate_priv.password
```

一次命令完成 A/B 固件和签名 `.swu`：

```sh
cd /home/ubuntu/AI-DeskTopBox
./scripts/auto_build_package.sh --with-ota
```

发布目录会额外包含 `ota/openwrt_a133_b6-ab-sign-rollback.swu`。私钥只在 SDK 的 trap
窗口内临时注入，不会复制进发布目录或 rootfs。密钥生成、设备端升级和回滚验证见
[OTA 说明](ota.md)。

## 开发期单独重编

正式发布统一使用 `build_release.sh`。调试时可分层执行：

```sh
# 所有应用；不清理时允许 make 增量
./scripts/build_apps.sh /home/ubuntu/A133-Tina5.0-v0.9

# 所有应用干净重编
./scripts/build_apps.sh --clean /home/ubuntu/A133-Tina5.0-v0.9

# 使用当前 build/ 应用重新做完整 SDK 构建与 pack
./scripts/build_firmware.sh /home/ubuntu/A133-Tina5.0-v0.9
```

不要在应用变化后跳过 `build_apps.sh`，也不要手工清理 SDK 产品产物后只调用
`./build.sh pack`。这两种操作都可能把旧应用或纯 SDK rootfs 封进看似成功的新镜像。

## 失败恢复

- 发布失败时，发布目录保留 `BUILD-INCOMPLETE` 和已经产生的日志，不能拿去烧录。
- `build_apps.sh` 会恢复它临时改过的 SDK 配置；`build_firmware.sh` 和 `build_swu.sh`
  都有退出 trap，成功或失败都会关闭产品包并撤销临时注入。
- 修复错误后直接重新运行 `build_release.sh`。每次使用新的时间戳目录，不覆盖旧发布包。
- 只有发布目录内的最终镜像可交付；SDK `out/` 是工作输出，会被后续构建覆盖。

更深入的包开关、rootfs 文件清单和 pack/clean 顺序见 [构建说明](build.md)，量产发布检查见
[发布检查清单](release.md)。
