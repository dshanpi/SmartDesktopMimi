# 设置、持久数据与备份

日常设置、APP 数据和工厂身份有不同生命周期。升级保留了某个文件，不等于整个应用的数据和行为都保持正确。

## 数据放在哪里

以下是已记录的 Tina 布局，用于理解和核对；实际设备先执行 `mount` 并查看当前版本的 init 脚本。

| 类型 | 代表路径 | 维护要点 |
| --- | --- | --- |
| 桌面设置 | `/overlay/lv_port_linux_settings.bin` | 亮度、音量、主题等；验证当前版本是否使用该布局 |
| Wi-Fi | `/overlay/wifi/wpa_supplicant` | 含网络秘密，不公开内容 |
| 蓝牙 | `/overlay/bluetooth`、`/overlay/lib/bluetooth` | 配对/MAC 与实际连接一起验证 |
| 用户 APP | `/overlay/aitvbox/apps/` | 安装包、运行与隔离数据分别处理 |
| IPKVM | `/overlay/aitvbox/ipkvm/` | 登录、FRPC、TLS 配置；含私密信息 |
| 模型密钥 | `/overlay/aitvbox/secrets/` | 运行时保护文件，不作为普通资源发布 |
| 涂鸦授权和数据库 | `/overlay/factory/tuya`、`/overlay/tuyadb` | 工厂授权与用户绑定状态分开 |
| 云端设备身份 | 由 `/etc/100ask` 持久绑定的目录 | 不能跨设备复制身份 |

新更新框架还包含应用版本、事务与信任数据，应按该系统的清单识别，不能只照这张历史路径表做全量恢复。

## 升级前备份的步骤

1. 记录设备身份、应用/系统版本、分区布局和时间。
2. 明确本次更新可能影响的数据目录和正在写入的服务。
3. 按设备维护流程使数据处于一致状态，再导出必要数据；不要把运行中数据库的随意复制当作一致备份。
4. 将备份保存到设备外受控目录，设置仅管理员可读。
5. 对备份计算 SHA-256，检查归档内容和恢复方法。
6. 恢复时先确认备份属于当前设备、数据 schema 与目标版本兼容。

只读状态采集可使用：

```sh
mount
df -h
ls -ld /overlay /overlay/aitvbox
cat /proc/sys/kernel/random/boot_id
cat /proc/uptime
```

目录清单与版本信息可脱敏分享；密码、授权、身份签名和模型 key 不应进入 issue、截图或公共压缩包。

## 升级后如何比较

配置应与预期一致，原 APP 可正常打开，Wi-Fi/蓝牙可重连，云身份保持正确归属。对静态文件比较哈希；数据库可能合法变化，需要另外验证记录完整性和业务功能。

历史恢复记录中 25 个文件仍在、24 个字节一致、一个涂鸦运行数据库变化。这不能自动推出“全部数据保留通过”，数据库功能仍需要单独验收。

## 恢复出厂与固件更新

恢复出厂是清除指定用户状态；普通应用更新是切换应用版本；系统 OTA 是切换系统槽位。三者不能混同。尤其不要删除工厂 License、设备签名或可信公钥来“修复连接”。已绑定设备没有新配对码时，应查询绑定语义。

数据迁移若不可逆、旧版本不能读取，必须准备受控恢复方案后才升级。系统回退时也要检查当前持久应用是否兼容旧系统。

参考：[更新规范](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/application-update-policy.md)、[运行布局](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/runtime.md)、[平台业务逻辑](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/platform-business-logic.md)。
