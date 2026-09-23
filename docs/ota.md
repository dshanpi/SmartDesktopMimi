# OTA 升级

本产品采用 **SWUpdate A/B 双槽 OTA**，由 LVGL UI 菜单触发，升级 kernel + rootfs（含 LVGL 应用、Tuya chatbot、MNN 库/模型、字体），带**失败自动回滚**。分两期交付：

- **Phase 1（当前）**：全量 A/B + RSA 签名，App 触发。**升级链路已真机端到端验证通过**（验签→写非活动槽→切 env→重启进新槽→commit）；回滚路径待真机实测。
- **Phase 2（规划）**：rdiff 增量降带宽。

> 本方案**不使用 Tuya 云 OTA**。产品只用 Tuya 平台的 MQTT/AI 对话能力；chatbot 二进制及所有依赖都在 rootfs 内，随 rootfs A/B 更新一起换掉。一个 `.swu` 包覆盖全部，无遗漏。

## 1. 出厂地基：A/B 双槽（Phase 0）

首版固件**出厂即 A/B 双槽**——单槽固件无法 OTA 成 A/B 布局。8G eMMC 足够切双槽。

地基由 `scripts/inject_sdk_files.sh` 在 `build_firmware.sh` 的 trap 窗口内注入到 SDK，退出时还原（不污染 SDK）：

| SDK 文件 | 改动 | 标记 |
|---|---|---|
| `sys_partition.fex` | `boot`→`bootA`+`bootB`，`rootfs`→`rootfsA`+`rootfsB`；bootA/rootfsA 有 downloadfile，bootB/rootfsB 空（OTA 写） | `AITVBOX_OTA_PARTITION` |
| `env.cfg` | 内核 `root=/dev/${rootdev}`（**硬编码分区号**，见下方"root= 必须硬编码"）；默认 `boot_partition=bootA`/`rootdev=mmcblk0p4`；加 `upgrade_available`/`bootcount`/`bootlimit`/`altbootcmd` | `AITVBOX_OTA_ENV` |
| `fw_env.config` | 注入单 env 版（注释掉 `env-redund` 行），修 `fw_printenv`/`fw_setenv` 读不到冗余 env 设备 | `AITVBOX_OTA_FWENV` |
| `sun50iw10p1_tina_defconfig` | 开 `CONFIG_BOOTCOUNT_LIMIT`+`CONFIG_BOOTCOUNT_ENV`（**不**开 `SUNXI_SWITCH_SYSTEM`） | `AITVBOX_OTA_BOOTCOUNT` |

### root= 必须硬编码分区号（实测教训）

`env.cfg` 的内核 `root=` **不能用** `/dev/by-name/${root_partition}`。原因：`/dev/by-name/*` 软链接由 rootfs 起来后的 `init_ota`（`set_parts_by_name` 解析 bootargs 的 `partitions=`）建立，**内核挂 rootfs 阶段尚不存在**，会卡在 `Waiting for root device /dev/by-name/rootfsA`。stock 用硬编码 `mmc_root=/dev/mmcblk0p4` 规避。

实测设备 `printenv` 确认分区号（`partitions=...rootfsA@mmcblk0p4:...rootfsB@mmcblk0p6...`）：
- `rootfsA = mmcblk0p4`，`rootfsB = mmcblk0p6`

故引入 `rootdev` 变量（`mmcblk0p4`/`mmcblk0p6`），`setargs_mmc` 的 `root=/dev/${rootdev}`。切槽时 SWUpdate/altbootcmd 改 `rootdev` 变量，内核 `root=` 随槽切到正确分区。`boot_partition=bootA/bootB` 给 U-Boot 的 `sunxi_flash read ${boot_partition}` 用（U-Boot 按名读，支持，不动）。

### fw_env.config 单 env（实测教训）

U-Boot 配了 `CONFIG_SYS_REDUNDAND_ENVIRONMENT=y`（`env`+`env-redund` 双分区），SDK 默认 `fw_env.config` 也配了两行。但 A/B 分区表只建了 `env`、没有 `env-redund` —— 用户态 libubootenv 按冗余模式读不到第二个设备，`fw_printenv`/`fw_setenv` 报 `Cannot open /dev/by-name/env-redund`，OTA 切槽写 env 失败。故注入单 env 版 `fw_env.config`（注释 `env-redund` 行），与 stock 行为一致，不动分区表、不动 rootdev 编号。若日后需要冗余 env 防断电，应在分区表加 `env-redund` 分区并同步调整所有后续分区的 `mmcblk0pN`。

## 2. 回滚机制

回滚**靠 env 实现，不写 U-Boot 代码**。A133 U-Boot 的 `sunxi_damage_switch_system()` 是无调用点的半成品，但 sunxi `bootcount_env.c` 已就绪（以 `upgrade_available` 为总开关）：

1. APPLY 时 sw-description 写入 `upgrade_available=1`、`bootcount=0`、`bootlimit=3`、方向化 `altbootcmd`，并把 `boot_partition`/`root_partition`/`rootdev` 切到新槽（`rootdev` 是内核 `root=` 的硬编码分区号，见上节）。
2. 重启进新槽。新槽正常启动后 backend 做 health check，通过则 `fw_setenv upgrade_available 0`（COMMIT），停止 bootcount 计数。
3. 若新槽起不来，U-Boot 每次启动 `bootcount++`；超过 `bootlimit=3` → 执行 `altbootcmd` 切回旧槽，旧槽正常启动。

代价：需重新编译 brandy（U-Boot），已包含在全量 `./build.sh` 构建里。

> bootcount 框架已编译启用（U-Boot 重编通过）。**回滚路径尚未真机实测**（需人为破坏新槽验证 bootcount 累积 + altbootcmd 切回），见"已知风险"。

## 3. 签名

- **RSA SHA256**，触发条件：`build_swu.sh` 在 defconfig 追加 `CONFIG_SWUPDATE_CONFIG_SIGNED_IMAGES=y` 等开关（`swupdate_pack_swu` 按文本 grep 识别，非 Kconfig）。
- **密钥一次性生成，不每构建重生成**——否则已出货设备验签失败。
  - 私钥：`secrets/swupdate_priv.pem` + `secrets/swupdate_priv.password`（gitignore，**绝不提交**）。
  - 公钥：`integrations/swupdate/keys/swupdate_public.pem`（提交），由 `aitvbox-suite` 薄包 install 到设备 `/etc/swupdate_public.pem`。
- 设备端 `swupdate -i <swu> -k /etc/swupdate_public.pem` 验签。

### swupdate 子选项（实测教训）

`swupdate` 包走自有 Kconfig（`SWUPDATE_CUSTOM=y` → `SWUPDATE_SYM=CONFIG`，`Build/Configure` 把 OpenWrt 侧 `CONFIG_SWUPDATE_CONFIG_*` grep+sed 成 swupdate 自身 `CONFIG_*`）。`toggle_ota.sh` 必须显式开启以下子选项全集，缺一即编译失败或运行报错：

- `LIBCONFIG`：sw-description 解析器（缺则 `libconfig.h: No such file` 编译失败）。
- `UBOOT`：fw_env 接口库（拉 uboot-envtools）。
- `BOOTLOADERHANDLER`：**解析/执行 sw-description 的 `bootenv:` 段**（A/B 切槽写 env 的关键）。仅开 `UBOOT` 不够——缺此 handler 则 swupdate 报 `bootloader support absent but sw-description has bootloader section`，OTA 切槽写 env 无从执行。
- `RAW`/`GUNZIP`/`SCRIPTS`：raw 镜像 handler / 解压 / 脚本。
- `SSL_IMPL_OPENSSL`/`SIGNED_IMAGES`/`SIGALG_RAWRSA`/`HASH_VERIFY`：RSA 签名链。

> **子选项变更后必须强制重编 swupdate**：OpenWrt 的 `STAMP_CONFIGURED` 用子选项 md5 标记，但实测改子选项后若 stamp 未变 swupdate 可能不重编（用缓存旧二进制）。改了子选项后应清掉 `build_dir/target/swupdate-*` + 相关 stamp 强制重编，否则固件里仍是旧 swupdate（运行时报错才暴露）。

> **私钥丢失**则无法再出可被老设备验签的包。需建立密钥备份流程（商业化运维项）。

## 4. 构建升级包

`scripts/build_swu.sh`（依赖一次已完成的 `build_apps.sh` + `build_firmware.sh`）：

1. 检查 `swupdate`/`openssl`、私钥、应用产物就位。
2. trap 窗口：注入私钥+口令到 SDK `openwrt/target/a133/a133-b6/swupdate/`；拷自定义 sw-description 到 `build/swupdate/`；拷 cfg 到板级 swupdate 目录；defconfig 追加签名开关块。
3. `source build/envsetup.sh` → `swupdate_pack_swu -ab-sign-rollback`。
4. 产物 `out/a133/b6/openwrt/swupdate/openwrt_a133_b6-ab-sign-rollback.swu` 拷回产品仓库 `build/swu/`。
5. trap 还原全部 SDK 改动。

自定义 sw-description（`integrations/swupdate/sw-description-ab-sign-rollback`）基于 SDK 的 `sw-description-ab-sign`：

- 保留 `now_A_next_B` / `now_B_next_A` 两分支，images 只含 `kernel` + `rootfs`（全量，**Phase 1 不 OTA bootloader**）。
- 每分支除原有 `boot_partition`/`root_partition`/`systemAB_next`/`swu_next=reboot` 外，加 `rootdev`（切内核 `root=` 到新槽硬编码分区号）、`upgrade_available=1`、`bootcount=0`、`bootlimit=3`、方向化 `altbootcmd`（altbootcmd 内也 `setenv rootdev` 切回旧槽）——这是回滚的关键，stock 模板缺这些。

### rootfs 压缩传输（Phase 1.5，降包大小）

全量包原 525M，根因是 rootfs 镜像 512M（分区固定大小）里实际只用 ~100M，剩 ~400M 空 ext4 块原样 cpio 打包白传。**gzip 压缩 rootfs 后包降到 ~55M（降 90%）**，纯打包侧改动，不动分区/设备端/A/B 布局。

机制：
- `build_swu.sh` 在 `swupdate_pack_swu` 前把 `rootfs.img` gzip 成 `rootfs.img.gz`，trap 删除不污染 SDK；日志打印压缩前后大小。
- `sw-subimgs-ab-sign-rollback.cfg` 的 rootfs 行指向 `.gz`，包内名仍 `rootfs`（sw-description 的 `filename` 不变）。
- `sw-description` 两处 rootfs image 段加 `compressed = "zlib";`。
- 设备端 `CONFIG_GUNZIP=y`（`toggle_ota.sh` 已开），swupdate `installed-directly=true` + `compressed="zlib"` **流式解压直写块设备**（`cpio_utils.c` gunzip 分支，16KB 缓冲，无落盘临时文件），UDISK 无需额外 525M 空间。
- sha256 对压缩字节（.gz）计算——`swupdate_pack_swu` 自动算 .gz 哈希填 `@rootfs`，设备端解压前校验。

> **哈希先写后校验的取舍**：`installed-directly=true` 流式下，哈希校验在写完分区后（`cpio_utils.c:533` 在写循环之后）。即目标槽已被写入才校验。A/B OTA 可接受——校验失败不提交事务、旧槽照常启动，目标槽下次升级覆盖。若需"先校验后写"，可去 `installed-directly` 改 COPY_FILE 路径（先落盘 .gz 到 UDISK 校验再写，需 ~55M 临时空间），当前不必要。

kernel 13M 不压缩（本身已压缩，收益小）。

## 5. 设备端 OTA 服务

`apps/lv_port_linux/src/system/service_ota.c`（仿现有 `service_*.c` 的 fork+execl 模式，耗时操作起独立线程避免阻塞 backend 10ms 主循环）。状态经 `TOPIC_OTA_STATUS` 上报给 UI。

| 指令 | 动作 |
|---|---|
| `OTA_CMD_CHECK` | `wget` 拉版本 JSON，解析 `version`/`url`，比对 `/etc/aitvbox-version` |
| `OTA_CMD_DOWNLOAD` | libcurl 下载 `.swu` 到 `/mnt/UDISK/upgrade.swu`（先写 `upgrade.swu.tmp` 再 rename，支持 HTTPS；设备 busybox wget 不支持 SSL，故用 libcurl C API） |
| `OTA_CMD_APPLY` | `fw_printenv boot_partition` 判当前槽 → 定方向 → `swupdate -i ... -e stable,<dir> -k /etc/swupdate_public.pem` → 成功 `reboot` |
| `OTA_CMD_COMMIT` | health check（backend 存活）→ `fw_setenv upgrade_available 0` |
| `OTA_CMD_GET_STATUS` | 上报当前状态快照 |
| `OTA_CMD_CANCEL` | Phase 1 未实现（留 Phase 2） |

**自动 COMMIT**：`service_ota_init()` 启动时若 `upgrade_available=1`，自动起 COMMIT 线程。

### 版本号

`build_apps.sh` 生成 `/etc/aitvbox-version`（`AITVBOX_VERSION=` + `git describe` + 日期），既用于 OTA 版本比对，也补上"固件无版本戳"的商业化缺口。

### 服务端（仓库外）

一个 HTTPS 静态服务放 `latest.json` 与 `.swu`。JSON 约定最简格式：

```json
{"version":"1.0.1","url":"https://ota.example.com/a133-b6/openwrt_a133_b6-ab-sign-rollback.swu","sha256":"<hex>"}
```

设备端用字符串扫描解析（不引入 JSON 库）。默认检查 URL 见 `service_ota.c` 的 `OTA_CHECK_URL_DEFAULT`，可由 `OTA_CMD_CHECK` 的 `url` 字段覆盖。

## 6. 涂鸦凭据与 OTA

chatbot 二进制不包含 UUID/AuthKey。每台设备的唯一 License 位于
`/factory/tuya/license.env`，其持久副本为 `/overlay/factory/tuya/license.env`。
S95 在应用启动前完成 bind mount，因此 A/B OTA 切槽后仍加载原设备凭据；OTA 包无需、
也不得携带或覆盖 License。

## 6.5 用户数据持久化（跨 A/B OTA 保留）

### 背景：overlayfs 未接管 /，/etc 在活动槽上

本固件 `/` 是裸 ext4（rootfsA/B，可写），**overlayfs 没有接管 /**：`mount_overlay()`（`busybox-init-base-files/files/init`）在 ext4 模式下走到 `fgrep '/dev/root / squashfs ro' /proc/mounts` 处即 `return`，不执行 overlay 合并与 `pivot_root`。

后果：`/etc`（含蓝牙配对、WiFi 密码、各类用户配置）落在**活动 rootfs 槽**上。单槽重启不丢（ext4 rw），但 **OTA 从 A 切到 B 时全部丢失**——商用产品每 OTA 一次用户就得重连 WiFi、重配蓝牙，不可接受。

### 存储区选型：/overlay（rootfs_data）

`rootfs_data`（mmcblk0p7）是单份、A/B 共享分区，启动时由 preinit 挂载到 `/overlay`（远早于任何 procd Sxx 服务，S95 时应已就绪；脚本仍会限时等待）。ext4 模式下 `mount_overlay()` 在 squashfs 检查处 return，**不动 /overlay 内容**，安全可复用。

为什么不改挂载点到 `/data`：`mount_overlay()` 硬编码把 rootfs_data 挂到 `/overlay`，改 fstab target 会冲突。`/overlay` 是零挂载链改动的唯一稳法。

为什么不启用 overlayfs 接管 /：会遮蔽新固件在 rootfsA/B 下更新的系统配置（`/etc/config/*` 读旧 overlay 值），且要动已验证的 `pivot_root` 启动链，风险高。

### 各类数据的持久化机制

| 数据 | 之前路径（丢） | 现在（/overlay=rootfs_data，保留） | 机制 |
|---|---|---|---|
| 涂鸦一机一密 | 固件编译期固定 UUID/AuthKey | `/overlay/factory/tuya/license.env` | S95 bind 到 `/factory/tuya`；产线逐台写入，OTA及用户恢复出厂均不得删除 |
| 涂鸦激活态（device_id/证书） | `/tmp/tuyadb`（每开重激活） | `/overlay/tuyadb` | Kconfig 设 `CONFIG_FLASH_FILE_PATH="/overlay/tuyadb"`（`integrations/tuyaopen/config/A133_B6.config`）+ 源码 patch 缩 `FLASH_FILE_SIZE` 256MB→1MB（`0002-persist-tuya-kv-on-rootfs_data.patch`，仅改 SIZE） |
| LVGL 偏好（亮度/音量/主题） | `/tmp/.lv_port_linux_settings.bin` | `/overlay/lv_port_linux_settings.bin` | 改 `aitvbox.init`/`start-ui.sh` 的 `LV_SETTINGS_FILE` |
| 蓝牙 adapter MAC（AIC 芯片无烧录地址） | `/etc/bluetooth/aic_bt.conf`（OTA 切槽丢 → MAC 变；异常内容会导致全 0 MAC） | `/overlay/bluetooth` | S95 `aitvbox-data.init` bind `/etc/bluetooth` 整个目录，S96 `bluetooth_init` 校验/修复 |
| 蓝牙配对（A2DP 手机配对 key） | `/etc/lib/bluetooth`（OTA 切槽丢） | `/overlay/lib/bluetooth` | S95 `aitvbox-data.init` bind mount |
| WiFi 密码 | `/etc/wifi/wpa_supplicant/wpa_supplicant.conf`（OTA 切槽丢） | `/overlay/wifi/wpa_supplicant` | S95 `aitvbox-data.init` bind 整个目录 |
| 100ask Cloud 凭据（device_sig/secret/cloud.conf） | `/etc/100ask`（OTA 切槽丢） | `/overlay/100ask` | S95 `aitvbox-data.init` bind 整个目录；机制详见 [cloud.md](cloud.md) |

> **蓝牙 MAC 持久化是配对保留的前提**：bluez 按 adapter MAC 分目录存配对（`/etc/lib/bluetooth/<adapter-MAC>/<phone-MAC>/info`）。AIC 芯片无烧录地址，`hciattach aic` 启动时读 `/etc/bluetooth/aic_bt.conf`，读不到就 `rand()` 随机生成并写回。该文件在活动 rootfs 槽，OTA 切槽即丢 → 下次启动 MAC 变 → 新建空配对目录 → 旧配对对不上 → 必须重新配对（配对数据其实没丢，是 MAC 对不上了）。故 S95 在配对 bind 之前先 bind `/etc/bluetooth`，让 MAC 跨 OTA 稳定，配对目录名才稳定可复用。S96 `bluetooth_init` 会在 hciattach 前额外校验该文件，空文件、全 0、格式错误和 AIC 保留地址都会被自动重生成。

### 为什么蓝牙/WiFi 用 bind mount 而非符号链接

蓝牙配对目录是运行时按 adapter-MAC 动态创建的（如 `/etc/lib/bluetooth/22:22:44:09:6C:A9/`），**链接名运行时才知，无法预先 bake 进 rootfs**。bind mount 是内核级、断电安全（重启重做 mount，数据持久在 /overlay），且不重编译 bluez/wifimanager（路径常量不变，只重定向目标）。

WiFi 的 `wpa_supplicant.conf` 文件名固定，本可用符号链接，但符号链接需在 rootfs 构建时 bake、依赖包 install 顺序；bind mount 在运行时做、与包顺序解耦，更稳。故蓝牙/WiFi 统一用 bind mount。

### aitvbox-data.init（S95）逻辑

`packaging/aitvbox-suite/files/aitvbox-data.init`，`START=95`（S40fstab 之后、S96 蓝牙/WiFi、S99 涂鸦之前）。`boot()` 钩子做：

1. 检查 `/overlay` 已挂载（未挂载则整体跳过，不阻断启动，退化为旧行为）。
2. `bind_to_overlay <src> <dst>`：首次 `/overlay` 下目标不存在 → 从 rootfs 源迁移（目录递归 `cp -a`、文件直接拷）→ 再 `mount -o bind <dst> <src>`；之后直接 bind。任一步失败只记日志。
3. 蓝牙 MAC：`bind_to_overlay /etc/bluetooth /overlay/bluetooth`（bind **整个目录**）。持久化 AIC 的 `aic_bt.conf`，让 adapter MAC 跨 OTA 稳定——这是下面配对目录名稳定、旧配对可复用的前提。必须早于 hciattach（S96）：本脚本 S95 先 bind，S96 `bluetooth_init` 先校验/修复 `aic_bt.conf`，再让 `hciattach aic` 读持久 MAC。首次 `aic_bt.conf` 不存在时会生成一次并写回 bind 层（/overlay），之后稳定。
4. 蓝牙配对：`bind_to_overlay /etc/lib/bluetooth /overlay/lib/bluetooth`（adapter 目录名即 MAC，上面 aic_bt.conf 已持久化 MAC，故配对目录名跨 OTA 稳定）。首次带 bluez 预置的空 mesh 目录，A2DP Sink 不用 mesh，无害。
5. WiFi：`bind_to_overlay /etc/wifi/wpa_supplicant /overlay/wifi/wpa_supplicant`（bind **整个目录**）。wifimanager `wifi -c` 通过 `SAVE_CONFIG` 把凭据写入 `wpa_supplicant.conf`；目录整体 bind 同时保留凭据、其他模式模板和 `sockets/`。S95 还会用 rootfs 默认值修复历史版本遗留的空模板，但不会覆盖已有的非空用户配置。
6. 涂鸦工厂 License：`bind_to_overlay /factory/tuya /overlay/factory/tuya`，目录权限收紧为 `0700`；License 文件由产线脚本写为 `0600`。
7. 涂鸦激活态：`mkdir -p /overlay/tuyadb`（配合 Kconfig 设的 `CONFIG_FLASH_FILE_PATH`，首次激活时 `__flash_file_init` 创建文件）。
8. 100ask Cloud：`bind_to_overlay /etc/100ask /overlay/100ask`（bind 整个目录）。持久化 `device_sig`（出厂烧录）/`secret`（provision 注册下发）/`cloud.conf`，跨 OTA 不丢。详见 [cloud.md](cloud.md)。

> **涂鸦 KV 路径为何走 Kconfig 而非源码改路径**：`tkl_flash.c` 里 `FLASH_FILE_PATH` 用 `#ifndef FLASH_FILE_PATH` 守卫，TuyaOpen 的 Kconfig（`boards/LINUX/TKL_Kconfig` 的 `config FLASH_FILE_PATH`）会先生成 `tuya_kconfig.h` 里的 `#define FLASH_FILE_PATH "./tuyadb"`，源码 `#ifndef` 守卫使其优先于源码字面量。所以改源码路径**不生效**，必须在 `A133_B6.config` 里设 `CONFIG_FLASH_FILE_PATH="/overlay/tuyadb"` 经 Kconfig 覆盖。`0002` patch 只负责改 `FLASH_FILE_SIZE`（256MB→1MB，避免 rootfs_data 上预占 256MB 稀疏文件）。

LVGL 偏好直接写 `/overlay/lv_port_linux_settings.bin`，无需本脚本处理。

### 验证（真机）

1. **蓝牙**：手机配对音箱 → OTA 升级 → 重启后手机仍连接（不重配）。
2. **WiFi**：连 WiFi → OTA 升级 → 重启后自动连。
3. **LVGL 偏好**：调亮度/音量 → 重启 → 设置保留。
4. **涂鸦**：激活 → 重启 → 不重新激活（无 `activated_data_read` 失败日志）。
5. **首次迁移**：刷入新固件首次启动，`ls /overlay/` 应见 `bluetooth/`、`lib/bluetooth/`、`wifi/`、`factory/tuya/`、`tuyadb/`、`100ask/`；`mount | grep -E "/etc/(lib/)?bluetooth|wpa_supplicant|/factory/tuya|100ask"` 应见 bind 挂载行。
6. **蓝牙 MAC 稳定**：`cat /overlay/bluetooth/aic_bt.conf` 记下 MAC → 重启 → `hciconfig hci0` 的 BD_ADDR 应与之一致（不再随机变）；该文件不能为空或 `00 00 00 00 00 00`；`ls /overlay/lib/bluetooth/` 的 adapter 目录名应不变。
7. **WiFi 凭据落点**：连 WiFi 后 `/overlay/wifi/wpa_supplicant/wpa_supplicant.conf` 应为非空并含 `network={ssid,psk}` 段；检查时避免把密码写入验收日志。

## 6.6 蓝牙与 OTA/软重启

蓝牙相关问题现在拆到独立文档维护，见 [bluetooth.md](bluetooth.md)。

OTA 文档只保留与 A/B 升级直接相关的结论：

- `S95aitvbox-data` 必须早于 `S96bluetooth_init`，先把 `/etc/bluetooth` 和 `/etc/lib/bluetooth` bind 到 `/overlay`，保证 AIC adapter MAC 和 BlueZ 配对目录跨槽稳定。
- `S96bluetooth_init` 覆盖 SDK/aidesktop 原脚本，动态查找 AIC rfkill，等待 `hciattach` 收尾，并重试 `hciconfig hci0 up`。
- `S99aitvbox` 后端只负责 btmanager/profile 层；如果 `hci0` 未 `UP`，后端会非阻塞延迟蓝牙初始化，不阻塞 UI、AI 或 HDMI preview。
- 2026-06-25 真机重复多次软启动验证：未再复现蓝牙初始化失败。

快速验证：

```sh
grep -E 'HCIATTACH_SETTLE_TIMEOUT|HCI_UP_RETRIES|BT_MAX_ATTEMPTS' /etc/init.d/bluetooth_init
hciconfig -a
grep -E 'bt init deferred|hci0 is UP|service initialized' /tmp/lv_backend.log
```

## 7. 手动测试（串口，无需触摸屏/服务器）

OTA 升级链路可不依赖 UI 和 OTA 服务器，**串口直接调 `swupdate -i`** 手动验证（与 LVGL 设置页"系统升级"走同一 `service_ota` 逻辑，等价）。前提：先跑过 `build_apps.sh` + `AITVBOX_OTA=1 build_firmware.sh` + `build_swu.sh`，产物就位：

- 出厂固件：`<SDK>/out/a133/b6/openwrt/a133_linux_b6_uart0.img`
- OTA 包：`build/swu/openwrt_a133_b6-ab-sign-rollback.swu`

### 7.1 刷出厂固件

用全志烧录工具（PhoenixSuit/LiveSuit）把 `a133_linux_b6_uart0.img` 刷进 eMMC。该固件出厂即 A/B 双槽，env 自带 `rootdev` 修复、单 env `fw_env.config`、含 bootenv handler 的 `swupdate`，刷完无需手动改任何配置。

### 7.2 验启动 + A/B 地基

启动进系统（`root@TinaLinux:/#`，**不应**卡 `Waiting for root device`）：

```sh
fw_printenv boot_partition       # bootA
fw_printenv rootdev              # mmcblk0p4
fw_printenv upgrade_available    # 0
fw_printenv bootlimit            # 3
fw_printenv altbootcmd           # 非空（切回 A，含 setenv rootdev）
ls /dev/by-name/                 # 含 bootA bootB rootfsA rootfsB env
swupdate --help 2>&1 | head -2   # 可执行
```

### 7.3 验 env 可写

```sh
fw_setenv test_var hello
fw_printenv test_var             # 显示 hello = OK
fw_setenv test_var               # 清掉
```

> 若报 `Cannot open /dev/by-name/env-redund`，说明固件未含单 env 修复（旧固件）——重刷 7.1 的新固件，或在设备上 `sed -i 's|^/dev/by-name/env-redund|#&|' /etc/fw_env.config` 临时绕过。

### 7.4 拷 OTA 包到设备

把 `.swu` 推到 `/mnt/UDISK/upgrade.swu`（路径名必须正好这个）：

```sh
# 主机端（adb，若设备开了 adb）
adb push build/swu/openwrt_a133_b6-ab-sign-rollback.swu /mnt/UDISK/upgrade.swu
# 或 scp / U 盘拷贝，目标都是 /mnt/UDISK/upgrade.swu
```

设备端确认：`ls -lh /mnt/UDISK/upgrade.swu`（当前 gzip rootfs 全量包期望约 55M）。

### 7.5 手动 APPLY（当前 A 槽，方向 now_A_next_B）

```sh
swupdate -i /mnt/UDISK/upgrade.swu -e stable,now_A_next_B -k /etc/swupdate_public.pem
```

> **`-e` 的值用逗号连成一个参数** `stable,now_A_next_B`，不能拆开（swupdate 的 `--select` 是 required_argument，`parse_image_selector` 按 `,` 拆分）。当前若在 B 槽则方向换 `now_B_next_A`。

预期输出：`Registered handlers: ... bootloader` → 验签通过 → 写 bootB + rootfsB → 写 bootenv → `SUCCESS`。成功后验证 env 已切：

```sh
fw_printenv boot_partition       # bootB
fw_printenv rootdev              # mmcblk0p6  ← 切槽核心
fw_printenv upgrade_available    # 1
fw_printenv bootcount            # 0
```

### 7.6 重启进新槽

```sh
sync && reboot
```

启动后**正常进系统**即切槽成功（内核 `root=/dev/mmcblk0p6` 挂载 rootfsB）：

```sh
fw_printenv boot_partition       # bootB
fw_printenv rootdev              # mmcblk0p6
cat /etc/aitvbox-version         # 新版本（读的是 B 槽 rootfs）
```

### 7.7 COMMIT（停止 bootcount 计数）

```sh
fw_setenv upgrade_available 0
```

至此 A→B 升级链路完整验证通过。`service_ota` 在 UI 触发时本会于启动后自动 COMMIT（health check 通过后），手动测即用 `fw_setenv`。

### 7.8 回滚测试（可选，验证失败保护）

验证"新槽起不来 → bootcount 超限 → altbootcmd 切回旧槽"。先回到一个稳定槽（重刷 7.1，或再 apply 一次到目标槽），保持 `upgrade_available=1` **不 commit**，破坏目标槽让它起不来：

```sh
# 破坏刚写好的目标槽 rootfs（例：刚升到 B，破坏 rootfsB）
dd if=/dev/zero of=/dev/by-name/rootfsB bs=1M count=20
sync && reboot
```

观察串口 U-Boot 日志：新槽启动失败 → 设备重启 → **bootcount 自增** → 超过 `bootlimit=3` → 执行 `altbootcmd`（切回 `bootA` + `rootdev=mmcblk0p4`）→ A 槽正常启动。验证：

```sh
fw_printenv boot_partition       # bootA（已回滚）
fw_printenv rootdev              # mmcblk0p4
```

> 回滚是当前唯一未真机验证项。若 bootcount 不持久化（sunxi env 保存路径有坑），需调整为 env 脚本手写 bootcount。

## 8. 验证

验证步骤详见**第 7 节手动测试**（7.1–7.8），此处只记结论。

### Phase 0 地基（✅ 真机验证通过）
- `build_firmware.sh` 产出 A/B 分区固件，`debugfs` 抽查 rootfs 含 `bootA`/`bootB`/`rootfsA`/`rootfsB` by-name 链接、`/etc/swupdate_public.pem`、`/etc/aitvbox-version`、`/sbin/swupdate`、`fw_printenv`/`fw_setenv`。
- 刷机后 `fw_printenv` 见 `boot_partition=bootA`、`rootdev=mmcblk0p4`、`upgrade_available=0`、`altbootcmd` 非空（见 7.2）。
- U-Boot 重编带 `BOOTCOUNT_LIMIT`+`BOOTCOUNT_ENV`（brandy 构建通过）。

### Phase 1 端到端（✅ 真机验证通过）
- `build_swu.sh` 产出 `openwrt_a133_b6-ab-sign-rollback.swu`，本机 `openssl dgst -sha256 -verify` 验签通过。
- 按 7.4–7.7 走完：apply 写 bootB/rootfsB + 写 bootenv → `rootdev=mmcblk0p6` → reboot 正常进 B 槽 → commit。**升级链路端到端打通**。
- UI 触发（LVGL 设置页"系统升级"）与串口手动 apply 走同一 `service_ota` 逻辑，等价。
- `toggle_ota.sh off` + inject revert 后，SDK 恢复纯态（`audit_sdk.sh` 校验）。

### 回滚测试（⏳ 待真机验证）
- 步骤见 7.8。bootcount 框架已编译启用，但"新槽起不来 → bootcount 累积 → altbootcmd 切回"整条路径尚未真机确认。

## 9. 已知风险与待确认

- **回滚路径未真机实测**：bootcount 框架已编译启用，但"新槽起不来 → bootcount 累积 → altbootcmd 切回"整条路径需按 7.8 确认。若 sunxi env 保存路径有坑（bootcount 不持久化），回滚方案需调整为 env 脚本手写 bootcount。
- **altbootcmd 方向化**：sw-description 每分支设不同 altbootcmd（含 `setenv rootdev`），swupdate bootenv 写入与 U-Boot 读取顺序需按 7.8 覆盖。
- **eMMC 实际容量**：以芯片为准，改分区表后 `pack` 阶段会校验，若不足需缩 rootfs_data。
- **签名密钥丢失**：私钥丢失则无法再出可被老设备验签的包，需建立备份流程。
- **蓝牙 MAC 首次随机（已知边界）**：AIC 芯片无烧录地址，靠 `aic_bt.conf` 持久化首次随机的 MAC。出厂首次启动若 `aic_bt.conf` 尚不存在或内容非法，`bluetooth_init` 会生成/修复一次并写回 bind 层（/overlay），**之后稳定**。若产线已具备 per-device 写入能力，建议改走产线烧录固定 MAC（写 /persist 或 efuse），彻底消除首次随机尾巴并保证全局唯一。详见 6.5「蓝牙 MAC 持久化」。
- **蓝牙启动 rfkill 时序（已修复）**：原 `bluetooth_init` 硬编码 rfkill3 导致频繁切槽重启偶发 `bring up hci0 failed`，已按 6.6 修复（动态找 `name=bluetooth` 且 `device` 路径含 `aic` 的 AIC rfkill）。仍建议产线/老化测试做连续切槽压测确认。

## 10. Phase 2（规划）：rdiff 增量

> **现状**：Phase 1.5 的 rootfs 压缩已落地（见第 4 节），全量包 525M → ~55M。对绝大多数 OTA 场景已够用。rdiff 增量为"小改动迭代"场景进一步把 delta 压到几 M，非必需，留待后续。

- `toggle_ota.sh` 增开 `CONFIG_SWUPDATE_CONFIG_RDIFFHANDLER`（拉 librsync）。
- `build_swu.sh` 增 `--delta <base.swu>` 模式：调 `swupdate_make_delta` 生成 `kernel.rdiff.delta`/`rootfs.rdiff.delta`，用 rdiff 版 sw-description 打包（`type=rdiff_image`，`rdiffbase=[/dev/by-name/bootA]`）。
- **base 管理**：发布仓保留每个发布版的全量 `.swu` 作下次 delta 的 base。首次升级（无匹配 base）仍走全量——`ota_check` 返回的 JSON 标注包类型，设备按类型选 `-e` 节点。
