# 发布检查清单

每次固件发布记录以下信息：

- Tina SDK 基线版本与 `a133-tina-aidesktop` 版本。
- 产品仓库 commit。
- TuyaOpen 上游 commit 与 `.tuyaopen-build` 副本来源。
- vendor 库 sha256（`vendor/tuyaopen-a133-b6-libs.sha256`）是否与发布一致。
- 输出固件镜像文件名与校验和。
- 构建应用与固件所用命令。
- `rootfs.img` 中产品文件抽查结果。
- 构建结束后 SDK 是否恢复纯模式。
- 硬件冒烟测试结果。
- 若发布含 OTA：`build/swu/*.swu` 校验和，及本机 `swupdate -i ... -k public.pem --check` 验签结果。

## 发布原则

- 发布固件必须使用完整 `build_firmware.sh`（完整 SDK 构建 + pack）。脚本不接受 rootfs-only 模式。
- `sync_to_sdk.sh` 只同步四个薄包，不打开任何 AITVBox 产品包。
- `build_firmware.sh` 会临时打开产品包，构建/pack 结束后恢复 SDK 纯模式并清理残留。
- **不要在 `build_firmware.sh` 之后单独 `clean_sdk_product_artifacts.sh` + `./build.sh pack`**：
  clean 后开关已 off，若 OpenWrt 增量重建 rootfs，新 `rootfs.img` 不含产品文件，
  pack 出无产品固件。重新 pack 请直接重跑 `build_firmware.sh`。详见 [build.md](build.md) 的"陷阱：pack 与 clean 的顺序"。
- 发布前用 `debugfs` 抽查 `rootfs.img` 确实含产品文件（见下"固件校验"），不要只看 pack 成功。

## 推荐发布命令序列

```sh
cd /home/ubuntu/AI-DeskTopBox
./scripts/auto_build_package.sh --check-only
./scripts/auto_build_package.sh
```

产物：

```text
/home/ubuntu/AI-DeskTopBox/build/releases/<版本-commit-时间>/firmware/a133_linux_b6_uart0.img
```

`auto_build_package.sh` 先检查并自动选择 SDK，然后调用 `build_release.sh`。后者内部调用
`build_apps.sh --clean` 和完整 `build_firmware.sh`，随后归档日志、
构建清单与 SHA256。发布只能交付含 `BUILD-COMPLETE` 且 `sha256sum -c SHA256SUMS` 通过的目录。
详细步骤见 [SDK 完整编译与发布手册](sdk-build-and-release.md)。

## 发布验证命令

记录镜像大小与校验和：

```sh
cd /home/ubuntu/A133-Tina5.0-v0.9
ls -lh out/a133/b6/openwrt/a133_linux_b6_uart0.img out/a133/b6/openwrt/rootfs.img
sha256sum out/a133/b6/openwrt/a133_linux_b6_uart0.img out/a133/b6/openwrt/rootfs.img
```

抽查 rootfs 里的产品文件：

```sh
debugfs -R 'stat /usr/bin/lv_backend' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/lvglsim' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/hdmi_preview' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/your_chat_bot_QIO_1.0.1.bin' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/aitvbox-start-ui' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/aitvbox-ipkvmd' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/bin/aitvbox-kvm-video' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/lib/libMNN.so' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /usr/share/tuyaopen_models/mdtc_chunk_300ms.mnn' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /etc/init.d/aitvbox' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /etc/init.d/play' out/a133/b6/openwrt/rootfs.img
```

确认 SDK 已恢复纯模式：

```sh
grep -E "CONFIG_PACKAGE_aitvbox-(suite|platform|usb-hid|ipkvm)" \
  openwrt/target/a133/a133-b6/defconfig \
  openwrt/openwrt/.config \
  out/a133/b6/openwrt/tmp/.config
```

期望三处的四个包均为：

```text
# CONFIG_PACKAGE_aitvbox-suite is not set
# CONFIG_PACKAGE_aitvbox-platform is not set
# CONFIG_PACKAGE_aitvbox-usb-hid is not set
# CONFIG_PACKAGE_aitvbox-ipkvm is not set
```

再运行一次集成审计：

```sh
cd /home/ubuntu/AI-DeskTopBox
./scripts/audit_sdk.sh /home/ubuntu/A133-Tina5.0-v0.9
```

## OTA 升级包校验

若本次发布含 OTA 升级包（详见 [ota.md](ota.md)），记录产物并本机验签：

```sh
cd /home/ubuntu/AI-DeskTopBox
ls -lh build/swu/*.swu
sha256sum build/swu/*.swu

# 本机验签（SWUpdate rawrsa = openssl dgst -sha256 -sign，主机无需 aarch64 swupdate）
TMP=$(mktemp -d) && cd "$TMP" \
  && cpio -id --quiet < /home/ubuntu/AI-DeskTopBox/build/swu/openwrt_a133_b6-ab-sign-rollback.swu \
  && openssl dgst -sha256 -verify \
       /home/ubuntu/AI-DeskTopBox/integrations/swupdate/keys/swupdate_public.pem \
       -signature sw-description.sig sw-description
# 期望输出: Verified OK
```

抽查 rootfs 含 OTA 相关文件：

```sh
debugfs -R 'stat /etc/swupdate_public.pem' out/a133/b6/openwrt/rootfs.img
debugfs -R 'stat /etc/aitvbox-version' out/a133/b6/openwrt/rootfs.img
```

## 启动链路检查

产品启动相关入口：

```text
/etc/rc.d/S25play -> /etc/init.d/play -> /sbin/boot-play boot &
/etc/rc.d/S99aitvbox -> /etc/init.d/aitvbox
/usr/bin/aitvbox-start-ui -> 等后端 socket，结束 boot-play，启动 lvglsim
```

主要日志：

```text
/tmp/lv_backend.log
/tmp/lvglsim.log
/tmp/tuya_chat_bot.log
```
