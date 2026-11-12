# 记录设置并备份持久数据

本章用于升级、恢复或迁移前的数据检查。备份范围由实际固件与运行服务决定；应先识别持久化目录和写入进程，再导出一致的数据副本。

## 列出本次需要保留的数据

在**板端 shell**只读检查布局：

```sh
mount
df -h
ls -ld /overlay /overlay/aitvbox
cat /proc/sys/kernel/random/boot_id
cat /proc/uptime
```

将实际布局与该版本 init 脚本核对。以下为已有 Tina 记录中的代表路径，用作检查索引：

| 数据 | 代表位置 | 备份时关注 |
| --- | --- | --- |
| 桌面设置 | `/overlay/lv_port_linux_settings.bin` | 亮度、音量、主题等设置的版本兼容性 |
| Wi-Fi | `/overlay/wifi/wpa_supplicant` | 网络凭据、文件权限 |
| 蓝牙 | `/overlay/bluetooth`、`/overlay/lib/bluetooth` | 控制器标识、配对与实际重连 |
| 用户 APP | `/overlay/aitvbox/apps/` | 已安装版本、运行文件和独立数据 |
| IPKVM | `/overlay/aitvbox/ipkvm/` | 登录、TLS、FRP 配置及访问权限 |
| 模型密钥 | `/overlay/aitvbox/secrets/` | 单独受控保管，不纳入公开资料 |
| 涂鸦授权与运行库数据 | `/overlay/factory/tuya`、`/overlay/tuyadb` | 本机身份、数据库一致性 |
| 100ask 身份 | `/etc/100ask` 的实际持久绑定位置 | 本机归属，不跨设备复制 |

启用独立更新框架的系统还需按其清单识别应用版本、事务、信任与数据目录。不要仅凭本表恢复整个系统。

## 准备一致的备份

1. 记录设备身份、系统/应用版本、当前时间和计划操作。
2. 列出必须保留的数据目录及对应写入服务。
3. 按交付维护流程停止或暂停相关写入；数据库优先使用该服务提供的一致导出方式。
4. 由管理员将备份导出到设备外受控目录。
5. 确认文件数、权限、属主和恢复方法已记录。

运行中数据库的直接复制可能无法恢复。设备秘密与普通用户数据分开管理，备份不能发送到公共 issue 或未经授权的接收方。

## 校验主机上的备份

以下在**保存备份的 Ubuntu 主机**执行。示例假设已经按维护流程生成 `device-backup.tar.gz`；命令只校验已有文件，不负责从运行中设备创建备份。

```sh
cd /absolute/path/to/protected-backup
chmod 700 .
chmod 600 device-backup.tar.gz
gzip -t device-backup.tar.gz
tar -tzf device-backup.tar.gz > backup-file-list.txt
chmod 600 backup-file-list.txt
sha256sum device-backup.tar.gz > device-backup.tar.gz.sha256
sha256sum -c device-backup.tar.gz.sha256
```

**检查结果：** 归档可读、包含预期目录、摘要校验为 `OK`。这份摘要用于后续确认备份未变化，不替代数据一致性与恢复演练。

记录表至少包含：备份时间、设备标识、版本、数据范围、导出方法、归档摘要、恢复前提与验证结果。

## 更新后逐项读回

1. 核对系统和应用版本，确认本次更新的实际结果。
2. 打开设置，检查音量、亮度、主题等原值。
3. 连接原 Wi-Fi，重新连接蓝牙设备并验证音频。
4. 打开原用户 APP，读取其持久数据。
5. 检查云端归属与授权状态，确认仍属于本机。

静态配置可比较摘要；数据库在运行后可能合法变化，应检查记录和业务功能。仅目录存在不足以证明数据可用。

## 按恢复条件选择操作

恢复前确认备份属于当前设备、数据格式被目标版本支持，并按该服务规定的顺序停止写入、恢复文件、设置权限、启动服务和读回。

普通桌面更新、系统 OTA、恢复出厂与跨设备迁移具有不同的数据影响。存在不可逆迁移且缺少恢复方案时，暂停升级并补齐方案。系统回退时还要核对持久分区中的应用是否兼容旧系统。

完成本章后，应具备可校验备份和明确恢复步骤；继续[更新流程](ota.md)。

## 查阅依据

[持久布局](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/runtime.md) · [数据与更新约束](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/application-update-policy.md) · [平台数据职责](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/platform-business-logic.md)
