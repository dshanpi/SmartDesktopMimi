# 100ask Cloud 云端 OTA

> **量产身份标准说明：** 新量产协议统一以
> [AITVBox 出厂身份写入与涂鸦 License 量产标准](tuya-license-cloud-provisioning-spec.md)
> 为准。全志工具只负责烧录统一固件；出厂助手读取 CPUID，通过量产云预绑定
> `device_sig + 原涂鸦 UUID/AuthKey`，再原子写入设备；设备继续通过现有 provision
> 获取 `device_secret`。返修整机重刷时，出厂助手仍看不到 secret；量产云通过
> `REPAIR_ROTATE_ONCE` grant 授权设备自行取得轮换后的新 secret。

云端开发必须同时遵循 [量产云实施规范](factory-cloud-implementation-spec.md)，其中定义了
接口字段、数据库唯一约束、返修 grant、MQTT 消费和 PASS 聚合规则。

本产品对接 **100ask IoT Cloud** 做云端 OTA：设备连云 → 云端推送 OTA →
设备**静默下载**（顶栏云徽章可视化进度，不全屏打断）→ **下完后全屏模态框提示安装**
→ 用户确认后复用已有的 SWUpdate A/B 双槽刷写链路（RSA 验签 + bootcount 回滚）。

> 本方案**不重写安全刷写**。云端只负责"通知有新版 + 给下载 URL"，下载/验签/刷写/回滚
> 全部复用 [ota.md](ota.md) 描述的 `service_ota` A/B 链路。

## 架构

```
┌─────────────────┐   MQTT ota topic        ┌──────────────────┐
│ 100ask Cloud    │ ──────────────────────→ │ AITVBox 设备      │
│                 │  {version,url,size,md5} │                  │
│ 后台推送 OTA     │                         │  lv_backend 内    │
│ 固件文件托管     │  ←────────────────────  │   ├ service_cloud │
│                 │  version 上报           │   │ (MQTT 连云)   │
└────────┬────────┘                         │   └ on_ota 回调   │
         │                                  │       ↓ 静默下载  │
         │ 上传 .swu                        │  service_ota      │
         └─── 发布固件时上传 ───────────────│  (libcurl 下 .swu │
                                            │   swupdate A/B    │
                                            │   RSA 验签+回滚)  │
                                            └──────────────────┘
```

**两条云并存**：本产品同时用 Tuya（`your_chat_bot_QIO` 进程连 `m2.tuyacn.com:8883`，
管 AI 对话）和 100ask Cloud（`lv_backend` 内 `service_cloud` 连 100ask 云，管设备管控 + OTA）。
两者独立，各连各的，互不冲突。

涂鸦 UUID/AuthKey 的量产云预分配、出厂写入、返修恢复和库存状态机见
[AITVBox 出厂身份写入与涂鸦 License 量产标准](tuya-license-cloud-provisioning-spec.md)。

## 设备认证协议（出厂助手自动化目标）

> 本节记录现有设备协议。正式量产保留该协议，但禁止把私钥分发到工位；签名生成和
> `device_sig` 写入必须由文首标准中的出厂助手自动完成。

采用 100ask SDK 的"SoC 芯片签名"方式（`examples/provision/provision_demo.c`，
同事已验证通过）。设备用芯片 cpuid 作 deviceId，新机首次开机或受控返修窗口内调用
provision API（ECDSA 验签），服务器下发 secret，设备缓存后连 MQTT。

**出厂阶段（工厂，每台一次）**：
1. 全志工具烧录统一固件，出厂助手读取该台芯片 cpuid（Allwinner `sunxi_serial`）。
2. 出厂助手用工位证书调用量产云 prepare，云端先将 CPUID 与签名和
   唯一涂鸦 License 永久关联。
3. 出厂助手写入 `/etc/100ask/device_sig` 和 `/factory/tuya/license.env`，读回哈希校验后
   向量产云 confirm-write。

**设备运行阶段（每次开机，service_cloud_init）**：
1. 读 cpuid（`/sys/class/sunxi_info/sys_info`）→ deviceId。
2. 读 device_sig（`/etc/100ask/device_sig`）。缺则不连云。
3. 读缓存 secret（`/etc/100ask/secret`）：有则直接用；无则走 4 注册。
4. **provision（新机首次或返修 grant）**：`POST https://www.100ask.net/api/device/provision`
   带 `deviceId`/`name`/`chipid`/`signature` → 服务器 ECDSA 验签 → 只返回 `{"deviceId","secret"}`
   → 设备缓存 secret 到 `/etc/100ask/secret`。已激活设备只有当前工位 mTLS 授权的
   `REPAIR_ROTATE_ONCE` grant 才能轮换；重复请求返回同一个新 secret，成功上线后关闭 grant。
5. 用 (cpuid, secret) 连 MQTT（username=cpuid, password=secret）→ 上报版本。
6. 用户进入设置页时，设备才用 `deviceId + secret` 按需申请 1 小时有效的
   bindToken；只在内存/UI 显示，不落盘、不打印。

> 关键：deviceId = cpuid 自动读，不用人工指定；secret 是服务器向设备下发并缓存，
> 不是出厂烧的。每台不同的出厂写入数据是 `device_sig` 和涂鸦 UUID/AuthKey。
> 100ask 私钥只在 KMS/HSM 或受控签名服务，设备和工位都不持有私钥。

## 凭据与配置

**固件只编一次**——固件二进制不含任何设备凭据，所有设备烧同一份。每台差异数据由
出厂助手在固件启动后写入：`/etc/100ask/device_sig` 和 `/factory/tuya/license.env`。

**跨 OTA 持久化**：`/etc/100ask` 和 `/factory/tuya` 由 `aitvbox-data.init`（S51）bind mount 到
`/overlay`（rootfs_data 分区，A/B 共享，OTA 不刷）。device_sig/secret/License 落 overlay，
OTA 切槽不丢——**用户 OTA 后无需重烧任何凭据**。机制同蓝牙配对/WiFi 密码/Tuya KV。

### 设备上的文件（/etc/100ask/，持久化到 /overlay）

| 文件 | 来源 | 每台是否不同 | 说明 |
|---|---|---|---|
| `device_sig` | 出厂助手写入 | 每台不同 | cpuid 的 ECDSA 签名，由受控签名服务生成 |
| `secret` | 运行时生成 | 每台不同 | provision 注册服务器下发，service_cloud 缓存（重启复用） |
| `cloud.conf` | 可选（薄包装或 adb 写） | 所有设备相同 | 服务器配置，缺失用默认值 |
| `ca.crt` | 可选（仅 TLS） | 所有设备相同 | broker CA 证书 |

> 旧版本的 `/etc/100ask/bind_token` 属于过期的一次性数据；新版本启动时会删除，
> 设置页每次打开都申请新绑定码。

### cloud.conf 格式（服务器配置，可选）

`/etc/100ask/cloud.conf`，`KEY=VALUE` 每行一个，`#` 注释。**只放服务器配置，不含凭据**：

```
MQTT_HOST=120.76.140.213
MQTT_PORT=1883
USE_TLS=0
PROVISION_URL=https://www.100ask.net/api/device/provision
# CA_FILE=/etc/100ask/ca.crt   # USE_TLS=1 时必填
```

| 字段 | 必填 | 默认值 | 说明 |
|---|---|---|---|
| `MQTT_HOST` | 否 | `120.76.140.213` | MQTT broker 地址 |
| `MQTT_PORT` | 否 | `1883` | MQTT 端口 |
| `USE_TLS` | 否 | `0` | 是否 TLS |
| `PROVISION_URL` | 否 | `https://www.100ask.net/api/device/provision` | 注册 API |
| `LATEST_JSON_URL` | 否 | `https://dl.100ask.net/Hardware/MPU/ai-desktop/ai-desktop-system.json` | OTA 版本信息 JSON |
| `CA_FILE` | TLS 时 | — | broker CA 证书路径 |

**cloud.conf 缺失也行**——service_cloud 用内置默认地址（120.76.140.213:1883 +
www.100ask.net provision），联调零配置。有 cloud.conf 时可覆盖默认值。

### 出厂/量产流程（固件只编一次，OTA 后不丢凭据）

```
1. 编译一次通用固件（不含 device_sig/secret，cloud.conf 可选）→ 出 .img
2. 使用全志 PhoenixSuit/LiveSuit 烧录到每一台（所有设备固件相同）
3. 烧录启动后，出厂助手自动读 cpuid → mTLS 调用量产云 prepare
4. 助手先校验 `/etc/aitvbox-tuya-pid` 与工位批次 PID 一致；量产云再返回
   已预绑定的签名+对应 PID 的涂鸦 License → 助手写入两个持久化目录
5. 设备 provision 拿 secret；涂鸦进程加载原 UUID/AuthKey，并以绑定 URL 或
   MQTT_CONNECTED 事件证明当前凭据已被运行时接纳
6. 出厂助手 confirm-write 并等待云端 `credentialState=PASSED`；这只代表凭据链路通过
7. 出厂助手在工位本地完成硬件测试，两者都通过后才显示最终整机 PASS
8. 用户 OTA 升级 → device_sig/secret/License 在 /overlay 保留，无需重烧
```

`provision_device.sh` 和文件模式的 `provision_tuya_license.sh` 只作为开发联调工具。
正式量产参考 `scripts/factory_provision_device.sh`；量产电脑不得保存签名私钥或
涂鸦 License 清单。

### 明文 vs TLS

100ask 云 MQTT 用 `120.76.140.213:1883` 明文。provision 用 `https://www.100ask.net`（TLS）。
本产品 SDK 已加 TLS 支持（`use_tls`/`ca_file` + `mosquitto_tls_set`），由 `cloud.conf` 的
`USE_TLS` 决定 MQTT 走哪种。当前默认明文（USE_TLS=0）。

| 模式 | MQTT_PORT | USE_TLS | CA 证书 | 说明 |
|---|---|---|---|---|
| 明文（当前） | 1883 | 0 | 不需要 | 与 100ask 验证一致 |
| TLS | 8883 | 1 | 需要 broker CA | secret 加密传输，需云开 8883 |

## 后端 service_cloud

`apps/lv_port_linux/src/system/service_cloud.c`（仿 `service_ai_runtime.c` 骨架）：

- `service_cloud_init()`（方式1）：读 cloud.conf（服务器配置，可选）→ 读 cpuid（deviceId）
  + device_sig → 读缓存 secret（有则直接可用）。**不在此 provision/连 MQTT**——延后到 update
  等网络就绪再做（避免开机瞬间 WiFi 未就绪导致 provision 失败）。
- `service_cloud_update()`（10ms 主循环调）状态机：
  - **MQTT 未启动**：等 `service_wifi_is_network_ready()` → 若无缓存 secret 则 provision
    （后台工作线中执行 libcurl POST，严格校验 HTTP 200、JSON schema 和 deviceId；
    只将 secret 原子写入 /overlay）→ `iot_init`+`iot_connect` 启动 MQTT。
    provision 失败从 5s 开始指数退避，最长 5min（不阻塞主循环）。
  - AI 服务确认涂鸦 License 已进入有效绑定流程或 MQTT 已连接后，通过已认证的
    100ask MQTT status topic 上报 `tuya_license_ready=true`；进程退出会清旧状态，
    量产云不得把历史结果直接用于新的返修工单。

这里的 100ask 在线是设备使用 device_secret 的机器鉴权，不需要客户账号；未绑定新机
收到涂鸦 `DIRECT_MQTT_CONNECTED` 并生成 bind_url 即完成 License 验证，也不需要客户扫码。
硬件测试由出厂工具本地完成，不要求上传云端。
  - **MQTT 已启动**：`iot_loop(100ms)` + 退避重连（5s）+ change-gated `publish_cloud_status()`。
- 出厂助手可在 backend 启动后写入 `device_sig`；服务每秒热检测，写入后无需重启 UI/backend。
- 设置页打开时发送 `CLOUD_CMD_REFRESH_BIND_TOKEN`；后台用已缓存 secret 申请新码，
  通过 `TOPIC_CLOUD_STATUS` 返回 UI。绑定码不进文件和日志。
- provision 用 **libcurl**（非 wget）：设备 busybox wget 不支持 https/`--header`，故用 libcurl
  C API。按后台文档第 2.1 节用 `application/x-www-form-urlencoded`（`CURLOPT_POSTFIELDS` +
  `url_encode`），传 `deviceId/name/chipid/signature/deviceModel/firmwareVersion`。libcurl + libssl
  设备 rootfs 自带（薄包依赖 `+libcurl`）。
- `on_ota_check()`：后台推 `{"action":"check"}` 时回调。**不在回调里下载/拉 json**（避免阻塞
  MQTT keepalive），置 `check_pending=true`，由 `service_cloud_update` 主循环异步调
  `process_ota_check`：拉 `latest.json` → 比对版本 → 上报 `update_available` → 触发下载。
  同时置 `ota_pending=true` + `ota_mandatory` 通知 UI。`mandatory=true` 时即使同版本也强制。
  process_ota_check 内有三道守卫（见下「重下载守卫 / 稍后延后」）。
- 缺 cpuid/device_sig、或 `iot_init` 失败：记日志、不连云、设备其余功能不受影响。

### 重下载守卫 / 稍后延后（商业级体验）

`process_ota_check` 在判定需下载后，先过两道守卫，避免重复下载和打扰：

1. **稍后延后守卫**：读 `/etc/100ask/ota_later`（用户点过「稍后」时写入，含 `until_ts` +
   `target_version` + `target_sha256`）。若当前时间 < until 且目标版本相同 → 跳过本次
   （上报 `no_update`，不弹窗）。时间未校准（年份 < 2025，开机初期未 NTP）时不触发此守卫，
   避免误判。until 过期或版本变了 → 清记录、正常流程。
2. **包就绪守卫**：调 `service_ota_is_package_ready(sha256)` 校验 `/mnt/UDISK/upgrade.swu`
   存在且 sha256 匹配。匹配 → **不重复下载**，直接 `service_ota_mark_package_downloaded()`
   推一次 `DOWNLOAD_DONE` 让 UI 弹安装提示（跳过下载不会进 service_ota 下载流程，需主动
   推 DOWNLOAD_DONE 才能触发弹窗）。不匹配/不存在 → 照常 `OTA_CMD_DOWNLOAD` 重下。

**稍后持久化**：用户点「稍后」→ `service_cloud_clear_ota_pending()` 写 `/etc/100ask/ota_later`
（当前时间 + 24h，带目标版本与 sha256），落 /overlay 跨重启持久。同版本 24h 内不再弹。

**开机兜底重弹**：`service_cloud_init` 末尾，若本地包就绪（用 ota_later 的 sha256 校验）
且不在稍后静默期内 → 主动 `mark_package_downloaded()` 触发 UI 弹窗，重启后重新提醒安装。
OTA 页（apps/ota）仍保留手动安装入口。

### 与 service_ota 的接合点

`service_ota.c` 的 `OTA_CMD_DOWNLOAD` 带 `cmd.url` 时直接覆写 `g_ota.remote_url`（并记
`cmd.sha256`）触发下载——云端 `latest.json` 解析出的 `.swu` URL 走这里。下载完成后用 `sha256`
校验，通过则 `service_ota` 经 `TOPIC_OTA_STATUS` 推 `DOWNLOAD_DONE`，触发 UI 弹安装模态框。
用户确认后 `OTA_CMD_APPLY` → swupdate A/B + 自动 reboot + 回滚，**刷写链路未改动**。

`service_ota` 暴露四个接口给云上报/联动用：
- `service_ota_get_boot_slot()`：读 `/proc/cmdline` 的 `root=` 返回当前槽 A/B，供 version/ota-status 上报。
- `service_ota_is_package_ready(sha256)`：本地包是否存在且 sha256 匹配，供重下载守卫/开机兜底判断。
- `service_ota_mark_package_downloaded()`：跳过下载场景主动推一次 `DOWNLOAD_DONE`，触发 UI 弹窗。
- `service_ota_set_status_listener(cb)`：注册进程内状态监听器，本地 OTA 状态变化时回调
  `service_cloud` 的 `on_ota_status`，映射成云端 `ota/status` 上报。

`worker_apply` 上报分段进度（10=准备 / 30=写入中 / 80=写完待重启 / 100=REBOOTING），
配 UI spinner 反馈安装过程；真实 swupdate 百分比需起进度 socket，本期未做。

## IPC 协议

`middleware.h` 新增：
- `TOPIC_CLOUD_COMMAND`（UI → Backend）：`CLOUD_CMD_GET_STATUS`（拉状态）、
  `CLOUD_CMD_DISMISS_OTA`（用户点"稍后"，清 `ota_pending`）。
- `TOPIC_CLOUD_STATUS`（Backend → UI）：`cloud_status_t`（`state`/`device_id`/
  `ota_pending`/`ota_version`/`ota_mandatory`）。`ota_mandatory=true` 时弹窗不显示「稍后」。

## UI

### 顶栏云徽章（连接状态 + OTA 静默下载反馈）

实现位置：

| 模块 | 路径 | 职责 |
|---|---|---|
| 布局 | `src/ui/desktop_status_bar.c` | 云图 + 状态点 + 底边进度条（默认隐藏） |
| 状态机 | `src/ui/ui_helpers.c` | 订阅 `TOPIC_CLOUD_STATUS` / `TOPIC_OTA_STATUS`，刷新徽章 |
| 启动同步 | `src/ui/ui.c` `ui_init` | `CLOUD_CMD_GET_STATUS` + `OTA_CMD_GET_STATUS` |

结构（WiFi 徽章旁，约 54×48）：

```text
┌────────┐
│  ☁  ●  │  ← 云图标 + 右下角状态点
│  ▬▬▬   │  ← 底边 2–3px 进度条（仅检查/下载/安装时显示）
└────────┘
```

**设计原则**：下载继续「静默」（不全屏打断桌面），但顶栏要能看出「在下 / 下完了」。
OTA 阶段优先级高于裸连接色，避免下载中仍显示「一切正常」的绿点。

**状态点颜色 / 动效**：

| 阶段 | 条件（摘要） | 圆点 | 进度条 |
|---|---|---|---|
| 离线 | `CLOUD_STATE_DISCONNECTED` 且无 OTA 活动 | 灰 `#A6B2BA` | 隐藏 |
| 连接中 | `CLOUD_STATE_CONNECTING` | 蓝 `#5B9BD5` + 慢闪 | 隐藏 |
| 已连接空闲 | `CLOUD_STATE_CONNECTED`，无 pending / 下载 | 绿 `#55A579` | 隐藏 |
| **检查 / 下载中** | `OTA_STATE_CHECKING` / `DOWNLOADING` | **蓝** | **显示，宽 = progress 0–100%** |
| **下完待安装** | `ota_pending` 或 `DOWNLOAD_DONE` | **橙 `#E0A14F` + 慢闪** | 隐藏 |
| 安装 / 重启中 | `APPLYING` / `REBOOTING` | 蓝 | 显示（REBOOTING 拉满） |
| 失败 | `OTA_STATE_ERROR` | 红 `#D96060` | 隐藏 |

慢闪：对状态点 `opa` 做 700ms 往返无限动画（`LV_OPA_40` ↔ `COVER`），离开闪烁态时 `lv_anim_delete` 复位。

进度条节流：progress 变化 **≥ 2%**（或到 0/100）才改 fill 宽度，减轻无 G2D 时的重绘。

用户可见路径：

```text
绿点（连上）
  → 蓝点 + 底边条变长（静默下载）
  → 橙点闪（包已就绪）
  → 全屏安装弹窗（PROMPT）
  → 用户确认 → INSTALLING 模态 + 自动 reboot
```

### 全屏安装模态框

**全屏安装模态框**（`ui/cloud_ota_modal.c`，多态 + 跨屏全局置顶）：
挂在 `lv_layer_top()` 上，**不被 app 的 `lv_scr_load` 切走**——无论用户在桌面还是任意 app，
弹窗都置顶可见；遮罩 `CLICKABLE` 拦截底层点击防点穿。两个态：

- **PROMPT 态**（下载完成，问是否安装）：图标盘子 + 「发现新版本」+ 版本号 + 提示文案 + 按钮。
  - 触发：`cloud_ota_status_callback` 检测到 `ota_pending==true` 且 OTA 状态 `DOWNLOAD_DONE`
    时弹窗（防重复弹）。`cloud_ota_modal_show(version, mandatory)`。
  - 同时顶栏进入「橙点闪」待安装态（见上表）。
  - `mandatory=true` → 只显示「立即安装」，**无「稍后」**（强制更新不可跳过）。
  - `mandatory=false` → 「稍后」+「立即安装」。
  - 「稍后」→ 关闭模态 + 发 `CLOUD_CMD_DISMISS_OTA`（清 `ota_pending` + 写 ota_later 延后提醒）；
    顶栏橙点随 `ota_pending=false` 回到连接色。
- **INSTALLING 态**（点安装后切换，不关弹窗）：spinner 转圈 + 「正在安装…请勿断电」+
  「设备将自动重启」。点「立即安装」时 `cloud_ota_modal_enter_installing()` 立即切态（即时
  反馈，不等状态回调）；APPLYING 状态回调驱动 spinner，REBOOTING 不关弹窗（reboot 自然黑屏），
  ERROR 才关。**模态内无刷写百分比条**——swupdate 刷写期间真实百分比未接进度 socket，
  顶栏仍可用蓝点 + 粗粒度 progress 作辅助；模态以 spinner 表达「进行中」。

> 早期版本点安装后弹窗立即消失（APPLYING 态 hide）→ 用户不知装多久，已修复为切 INSTALLING 态。
> 早期顶栏只显示连云绿/灰，静默下载无反馈 → 已扩展为上表 OTA 生命周期可视化。

**OTA 页保留**：现有 `apps/ota/ota_app.c`（设置→系统升级）不改，仍是手动检查/安装的
fallback。云端推送下载完成后，OTA 页「立即安装」按钮也会自动启用——模态框与页面双入口。

## 100ask SDK

100ask SDK 是不随公开源码或 SBOM 分发的可选私有 provider。公开版使用无网络
stub；内部构建必须显式设置 `AITVBOX_ENABLE_100ASK_CLOUD=ON` 和绝对路径
`AITVBOX_100ASK_SDK_ROOT`。私钥只能由受控工程包或签名服务提供。

依赖：
- `libmosquitto`：薄包 `DEPENDS` 加 `+libmosquitto-ssl`，rootfs 自带 `libmosquitto.so`；
  仅在显式启用 provider 时，`build_apps.sh` 的 `ensure_mosquitto_staging` 才会在 staging 缺该库时自动开
  `CONFIG_PACKAGE_libmosquitto-ssl=y` 并跑 `openwrt_rootfs` 编进 staging（幂等，换机器不卡）。
- `libcurl`：provision HTTPS POST 用。薄包 `DEPENDS` 加 `+libcurl`，rootfs 自带 `libcurl.so.4` +
  `libssl`。CMake 链 `${CURL_LIB}`。设备 busybox wget 不支持 https/`--header`，故 provision 用 libcurl。

## OTA 检查消息格式（联调关键）

后台 OTA 采用 **"通知检查 + 设备自检"** 模型（不直推固件 URL）：

1. 后台判定某设备该检查更新 → 推 `100ask/device/{deviceId}/ota` payload `{"action":"check"}`。
2. 设备收到 → 自行 GET `latest.json` → 比对版本 → 自行按 json 里的 `url` 下载。

SDK 的 `on_message`（`100ask_iot.c` ota 分支）**只认 `{"action":"check"}`**，收到后调
`g_ota_check_cb`（service_cloud 注册的 `on_ota_check`），**不在回调里阻塞**——置标志由主循环
异步拉 `latest.json` + 比对 + 下载。不再解析旧的 `{"version","url",...}` 直推格式（后台已不直推）。

### latest.json（设备拉取，固件信息来源）

托管地址（cloud.conf 的 `LATEST_JSON_URL`，缺失用默认值）：
```
https://dl.100ask.net/Hardware/MPU/ai-desktop/ai-desktop-system.json
```

字段（已与后台确认）：

```json
{
  "schema": 1,
  "enabled": true,                 // false → 设备直接 no_update，不下载
  "product": "AITVBox",
  "model": "A133-B6",
  "version": "1.0.1",              // 目标版本，纯 semver，设备与本地整数比对
  "filename": "openwrt_a133_b6-ab-sign-rollback.swu",
  "url": "https://dl.100ask.net/.../openwrt_a133_b6-ab-sign-rollback.swu",
  "size": 57082368,
  "sha256": "7384e5cf...",         // 设备下载后校验，不符 failed+删包（与 swupdate RSA 验签叠加）
  "title": "AITVBox 1.0.1",
  "description": "修复蓝牙启动问题，优化 OTA 稳定性。",
  "mandatory": false,              // true → 即使同版本/旧版本也强制下载安装，模态框无"稍后"
  "published_at": "2026-06-30T18:30:00+08:00"
}
```

设备用到的字段：`enabled`/`version`/`url`/`sha256`/`mandatory`（其余展示用）。
版本比对规则、mandatory/sha256/enabled 行为见 [versioning.md](versioning.md) 第 4 节。

### OTA 状态上报（设备 → 后台，进度同步）

设备在 `100ask/device/{deviceId}/ota/status` 上报完整状态机，后台设备页据此显示进度：

```json
{"status":"downloading","current_version":"1.0.0","target_version":"1.0.1",
 "progress":45,"boot_slot":"A","message":"正在下载升级包"}
```

状态机：`checking` → `update_available`/`no_update` → `downloading` → `downloaded` →
`installing` → `rebooting` → `health_check` → `completed`（失败 `failed`，回滚 `rolled_back`）。

设备由 `service_ota` 的进程内状态监听器（`service_ota_set_status_listener`）联动上报：
本地 OTA 状态变化时映射成云端 status 字符串调 `iot_report_ota_status`。`completed` 后再发一次
version topic 带 `upgrade_state=committed`。

> **MQTT loop 不可被阻塞**：latest.json 拉取/比对在主循环异步跑（`process_ota_check`），
> 下载/安装在 `service_ota` 独立 pthread worker，`service_cloud_update` 每 10ms 进一次
> `iot_loop(100ms)`。耗时任务都不卡 MQTT 保活。

### 心跳保活

- MQTT 协议保活：`keepalive=60`，`iot_loop` 持续调用，PINGREQ/PINGRESP 由 mosquitto 库自动发。
- 业务心跳：连云后每 60s 主动发 `{"online":true,"source":"heartbeat"}` 到 status topic 刷官网
  `last_seen_at`。上线发 `source:device`，LWT 发 `source:mqtt-will`。

## 发布流程（给客户用）

```
1. ./scripts/build_apps.sh + build_firmware.sh + build_swu.sh  → 出 .swu
2. 上传 .swu 到 100ask Cloud 固件托管（后台或对象存储）
3. 100ask 后台创建固件版本记录：version + .swu 的 URL + md5
4. 后台选目标设备/批次 → 推送 OTA（发 ota topic）
5. 设备 on_ota 收到 → 静默下载（顶栏蓝点+进度条）→ 下完橙点闪 + 弹模态框
   → 用户确认 → swupdate A/B → reboot 进新槽 → commit
6. 失败自动回滚旧槽（bootcount，已有机制）
```

客户全程只需在弹窗点「立即安装」（或选「稍后」），无需 adb、无需手动 swupdate。

## 联调怎么验证

地址都已确认（MQTT `120.76.140.213:1883`、provision `https://www.100ask.net/api/device/provision`），
设备端 deviceId/secret 运行时自动获取，cloud.conf 缺失用默认值。联调只需给测试设备签一个
device_sig。开发环境可暂用脚本，正式量产必须使用出厂助手：

```sh
# 仅开发联调：设备已 adb 连接，读 cpuid → 开发私钥签名 → 写 /etc/100ask/device_sig
AITVBOX_100ASK_SDK_ROOT=/absolute/private/100ask-iot-sdk \
  ./scripts/provision_device.sh
adb reboot
```

重启后设备自动：等 WiFi 就绪 → provision 注册（日志见 `provision ok (http=200), secret obtained`）
→ 缓存 secret → 连 MQTT → 顶栏云徽章绿点。

查日志：`adb shell cat /tmp/lv_backend.log | grep cloud`

> **OTA 推送格式**：后台推 `100ask/device/{deviceId}/ota` payload `{"action":"check"}`
> （SDK 只认 check，不在推送里直推固件 URL）。设备收到后自行拉 `latest.json` 比对下载。

> **MQTT 认证已通**：后端修复 EMQX 认证后，provision 拿到 secret → MQTT 连
> `120.76.140.213:1883` 成功（日志 `Connected to cloud` + `Reported version`）。
> 早期 `Connection Refused: not authorised` 问题已由后端排查解决。

> 安全提醒：`100ask_keys/100ask_ecdsa.pem` 是 ECDSA 签名私钥。生产环境必须迁移到
> KMS/HSM 或集中签名服务，不进设备固件、公开仓库、出厂助手、量产电脑和量产交付包。

## 验证

1. **编译**：默认公开构建使用 stub 且不链接 Mosquitto；显式私有构建链接
   `libmosquitto.so.1` + `libcurl.so.4`。
2. **无 device_sig 不阻断**：缺 `/etc/100ask/device_sig` → `service_cloud` 不连云，设备其余
   功能正常。
3. **provision**：写入 device_sig 后设备开机 → 等 WiFi 就绪 → provision（libcurl HTTPS POST
   www.100ask.net）→ 日志 `provision ok (http=200), secret obtained`，secret 缓存到 /overlay。
4. **连云**：provision 拿到 secret 后连 MQTT `120.76.140.213:1883` → 顶栏云徽章绿点。
   重启读缓存 secret 直连（不再 provision）。
5. **持久化（跨 OTA）**：OTA 升级后 `/etc/100ask` 的 device_sig/secret 仍在（bind 到 /overlay，
   rootfs_data 不被刷）。已验证：OTA 后日志 `using cached secret`，无需重烧凭据。
6. **端到端 OTA**：后台推 `{"action":"check"}` → 设备拉 latest.json → 静默下载
   （libcurl HTTPS；顶栏 **蓝点 + 底边进度条** 跟 `TOPIC_OTA_STATUS` progress）→
   下完 **橙点闪** + 安装模态 → 点「立即安装」→ 切 INSTALLING 态（spinner +「请勿断电」）→
   swupdate A/B → reboot 进新槽 → commit；失败 bootcount 回滚。
7. **顶栏云徽章**：
   - 离线灰 / 连接中蓝闪 / 已连绿；
   - 下载中蓝点+进度条；待安装橙闪；失败红点；
   - 断网恢复且无 OTA 活动时回到绿点。
8. **交互体验**：mandatory=true 弹窗无「稍后」；点「稍后」写 ota_later，24h 内同版本不重弹，
   开机兜底重弹；包就绪时不重下（sha256 校验）；弹窗挂 layer_top 跨屏置顶；INSTALLING 态
   spinner 反馈安装过程。
9. **回归**：Tuya 语音、蓝牙、HDMI、手动 OTA 页不受影响。
