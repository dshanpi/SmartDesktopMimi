# 蓝牙启动与软重启稳定性

本文记录 AIC 蓝牙在多次软重启/A/B 切槽后偶发初始化失败的根因、修复点和验证方法。

## 启动链路

产品固件里蓝牙相关启动顺序如下：

```text
S95aitvbox-data
  -> bind /etc/bluetooth 到 /overlay/bluetooth
  -> bind /etc/lib/bluetooth 到 /overlay/lib/bluetooth

S96bluetooth_init
  -> reset AIC 蓝牙状态
  -> 校验/修复 /etc/bluetooth/aic_bt.conf
  -> hciattach /dev/ttyS1 aic
  -> hciconfig hci0 up

S99aitvbox
  -> lv_backend
       -> service_bt_init()
       -> bt_manager_preinit/init/enable
       -> A2DP Sink + AVRCP profile
```

正常情况下，`S96bluetooth_init` 成功后才进入 `S99aitvbox`。后端确实会启动蓝牙 profile 服务，但它应当发生在底层 `hci0` 已经 `UP` 之后。

## 已修复的问题

### 1. rfkill 目标错误

旧脚本硬编码 `/sys/class/rfkill/rfkill3/state`。真机上 AIC 蓝牙的上电 rfkill 并不固定为 `rfkill3`：

- AIC btlpm 注册的 rfkill `name` 是 `bluetooth`。
- 其 `device` 路径包含 `aic`。
- `rfkill3` 经常是 `hci0` 创建后蓝牙核心层注册的附属 rfkill，是结果，不是 AIC 芯片上电开关。

多次软重启后如果 `hciattach` 握手失败，`hci0` 可能根本没建出来，旧脚本就跳过复位，导致后续重试仍然失败。冷启动能恢复，是因为 AIC 被彻底断电。

当前修复在 `integrations/swupdate/files/bluetooth_init`：

- 动态扫描 `/sys/class/rfkill/rfkill*`。
- 只接受 `name=bluetooth` 且 `readlink -f device` 包含 `aic` 的 rfkill。
- 复位时执行 `hciconfig hci0 down`、`killall hciattach`、重载 `aic8800_btlpm`、rfkill `0 -> 1` 断电上电。

### 2. hciattach 尚未完成就 hciconfig up

实测日志中过：

```text
hciconfig hci0 up failed
...
AIC Bluetooth post process
Device setup complete
```

这说明 `/sys/class/bluetooth/hci0` 出现时，AIC vendor 初始化还没完全结束。旧流程可能过早执行 `hciconfig hci0 up`，失败后继续往后启动应用，最终 `hci0` 停在 `DOWN`。

当前 `bluetooth_init` 增加：

- `HCIATTACH_SETTLE_TIMEOUT=8`：等 `hciattach` 打完 `Device setup complete` 并退出。
- `HCI_UP_RETRIES=15`：`hciconfig hci0 up` 每秒重试。
- `BT_MAX_ATTEMPTS=3`：失败后做深度复位再重来。
- 只有 `hciconfig hci0 up` 成功后才打印 `Bluetooth init done.`。

### 3. 后端蓝牙服务抢跑

`lv_backend` 在 `service_bt_init()` 中会调用 `bt_manager_preinit/init/enable(true)` 并注册 A2DP Sink/AVRCP。正常顺序下它不会早于 `S96bluetooth_init`，但如果 `S96` 提前失败返回，`S99` 就会继续启动，后端可能在 `hci0` 未 `UP` 时碰 btmanager。

当前后端在 `apps/lv_port_linux/src/system/service_bt.c` 做了非阻塞防护：

- `service_bt_init()` 先用 BlueZ HCI 接口检查 `hci0` 是否 `UP`。
- 未 `UP` 时不调用 btmanager，只记录 `bt init deferred: hci0 is not UP`。
- `service_bt_update()` 每 1 秒重试一次，最多 90 次。
- 用户设置的蓝牙开关状态会先保存，初始化成功后再应用。
- `lv_backend`、UI、AI、HDMI preview 和 IPC 不会被蓝牙等待阻塞。

### 4. 新板首次启动 MAC 为全 0

2026-06-26 在一块从未烧录过蓝牙 MAC 的新板上复现：

```text
AIC Bluetooth: Setting local bd addr to 00:00:00:00:00:00
AIC Bluetooth: 70 fc 06 00 00 00 00 00 00
```

这不是后端应用导致的，发生点在 `hciattach /dev/ttyS1 aic` 阶段，早于 btmanager/profile 服务。

AIC 的 BlueZ 补丁会读取 `/etc/bluetooth/aic_bt.conf`。如果文件不存在，它会随机生成 `22 22 xx xx xx xx` 并写回；但如果文件存在且为空、全 0 或格式异常，旧逻辑只检查了 `9E:8B:00-3F` 这一段保留地址，没有把全 0 判非法，于是会直接把 `00:00:00:00:00:00` 写进控制器。

当前 `bluetooth_init` 在 hciattach 前增加了自愈：

- 只接受 6 个两位 hex 字节。
- 拒绝空文件、全 0、格式错误和 `9E:8B:00-3F` 保留地址。
- 异常时生成 `22 22 xx xx xx xx`，写回 `/etc/bluetooth/aic_bt.conf`。
- 如果修复发生时 `hci0` 已经存在，会复位并重跑 hciattach，确保新 MAC 真正写进控制器。
- 因为 `S95aitvbox-data` 已经把 `/etc/bluetooth` bind 到 `/overlay/bluetooth`，修复后的 MAC 会持久保留，跨重启和 A/B OTA 不再变化。

## 构建入口

完整固件仍走统一入口：

```sh
./scripts/build_apps.sh /home/ubuntu/A133-Tina5.0-v0.9
env AITVBOX_OTA=1 ./scripts/build_firmware.sh /home/ubuntu/A133-Tina5.0-v0.9
./scripts/build_swu.sh /home/ubuntu/A133-Tina5.0-v0.9
```

`build_firmware.sh` 会在 SDK 构建后、pack 前覆盖 rootfs 缓存里的 `bluetooth_init`，确保修复版进最终镜像。

## 验证

设备启动后检查：

```sh
grep -E 'HCIATTACH_SETTLE_TIMEOUT|HCI_UP_RETRIES|BT_MAX_ATTEMPTS' /etc/init.d/bluetooth_init
cat /etc/bluetooth/aic_bt.conf
hciconfig -a
```

预期：

- `/etc/init.d/bluetooth_init` 能看到 settle/up retry 参数。
- `/etc/bluetooth/aic_bt.conf` 是 6 个 hex 字节，不能是空文件或 `00 00 00 00 00 00`。
- `hciconfig -a` 中 `hci0` 为 `UP` 或 `UP RUNNING`。
- `hciconfig -a` 的 `BD Address` 与 `aic_bt.conf` 顺序一致，例如 `22 22 4b 11 f5 19` 对应 `22:22:4B:11:F5:19`。
- 串口先出现 `Bluetooth init done.`，后端再进入蓝牙服务初始化。

后端日志检查：

```sh
grep -E 'bt init deferred|deferred init waiting|hci0 is UP|service initialized' /tmp/lv_backend.log
```

正常快速启动时可能只看到：

```text
service initialized
```

如果蓝牙底层慢启动，允许出现：

```text
bt init deferred: hci0 is not UP
deferred init waiting for hci0 UP
hci0 is UP, retry bt service init
service initialized
```

## 压测结论

2026-06-25 验证：在加入 AIC rfkill 动态查找、hciattach settle/up retry、后端非阻塞延迟初始化后，设备重复多次软启动未再复现蓝牙初始化失败。

2026-06-26 补充：新板首次启动若 `/overlay/bluetooth/aic_bt.conf` 被固化为空/全 0/非法内容，`bluetooth_init` 会在 hciattach 前自动修复，避免控制器地址变成 `00:00:00:00:00:00`。

## 再复现时抓日志

如果后续仍遇到蓝牙异常，优先收集：

```sh
dmesg | grep -Ei 'bluetooth|aic|rfkill|hci'
cat /tmp/lv_backend.log | grep -Ei 'bt|hci'
hciconfig -a
cat /etc/bluetooth/aic_bt.conf
ls -l /overlay/bluetooth/aic_bt.conf
for r in /sys/class/rfkill/rfkill*; do
    echo "== $r =="
    cat "$r/name" 2>/dev/null
    readlink -f "$r/device" 2>/dev/null
    cat "$r/state" 2>/dev/null
done
```

串口日志要覆盖 `Starting bluetooth init...` 到 `Bluetooth init done.` 或 `bring up hci0 failed` 的完整区间。
