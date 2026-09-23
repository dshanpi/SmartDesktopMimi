# 固件版本规范（云端 OTA 匹配依据）

本文是 **AITVBox 固件版本号的唯一规范**，供云端后台做 OTA 推送匹配时参照。
设备端、后台、发版流程三方按此约定执行。

> 阅读对象：云端后台开发（做版本比对 / OTA 推送匹配）、固件开发、发版负责人。
> 后台只需看第 2、3、4 节即可完成对接，其余节是设备端/发版细节。

## 1. 核心原则：版本号与 git 彻底解耦

**OTA 比对用的版本号是纯语义版本（semver），来自仓库根 `VERSION` 文件，与 git 状态无关。**

- git 是代码版本管理工具，tag/dirty/commit 这些是给开发排查问题用的。
- OTA 匹配是另一回事：后台要能判断"设备当前版本 vs 目标版本，谁新谁旧"。
- 两者没有关系。把 git describe 当 OTA 版本号会导致：不可比、每次编译都变、
  同一版本被识别成无数个——OTA 匹配直接失效。

所以：

| 字段 | 来源 | 用途 | 是否参与 OTA 比对 |
|---|---|---|---|
| `AITVBOX_VERSION` | `VERSION` 文件（纯 semver） | OTA 比对、设备上报、后台匹配、推送 version 字段 | ✅ 是 |
| `VERSION_GIT` | `git describe` | 纯溯源日志，人排查用 | ❌ 否 |
| `PRODUCT_COMMIT` | `git rev-parse` | 纯溯源日志 | ❌ 否 |
| `BUILD_DATE` | 编译时刻 | 纯溯源日志 | ❌ 否 |

git 相关字段不做任何校验、不警告、不卡编译——git 该咋样咋样，OTA 比对完全不看它们。

## 2. 版本号格式（后台必看）

### 2.1 格式

```
MAJOR.MINOR.PATCH
```

- 三段，点号分隔，**每段纯数字**，无前导零。
- 例：`1.0.0`、`1.0.1`、`1.2.10`、`2.0.0`
- **禁止**：字母、`-`、`+`、`dirty`、日期、commit hash、`v` 前缀。

### 2.2 单调递增，不复用

发版时手动 bump：

- `PATCH`：缺陷修复、小改动（`1.0.1` → `1.0.2`）
- `MINOR`：向后兼容的新功能（`1.0.2` → `1.1.0`）
- `MAJOR`：不兼容的重大变更（`1.1.0` → `2.0.0`）

**同一版本号不可复用**——一旦发布 `1.0.1` 并对外推送 OTA，`1.0.1` 永久占用，
后续发版必须 bump 到 `1.0.2` 起步。绝不重新编译后用同一版本号覆盖。

### 2.3 唯一来源

比对版本**唯一来源**是产品仓库根目录的 `VERSION` 文件：

```
VERSION
└── 内容只有一行：1.0.0
```

- 整个产品只有这一个文件定义"当前版本是多少"。
- 编译时由 `scripts/build_apps.sh` 读取，写入 `/etc/aitvbox-version`。
- 任何代码、脚本都不应硬编码版本号；改版本就改 `VERSION` 文件。

## 3. OTA 消息协议（后台必看）

### 3.1 设备上报（设备 → 后台）

设备连上 MQTT 后，主动上报当前版本：

- **topic**：`100ask/device/{deviceId}/version`
- **payload**：`{"version":"1.0.1","boot_slot":"A","upgrade_state":"committed"}`
- **触发时机**：每次 MQTT 连接成功时上报一次；OTA commit 成功后再上报一次。
- **字段**：
  - `version` = 设备 `/etc/aitvbox-version` 的 `AITVBOX_VERSION` 值，**纯 semver**。
  - `boot_slot` = 当前启动槽 `"A"`/`"B"`（读 `/proc/cmdline` 的 `root=`，A=mmcblk0p4/B=mmcblk0p6）。
  - `upgrade_state` = `"committed"`（已确认）/ `"pending"`（升级待确认）。后台靠
    `upgrade_state=committed` 判定 OTA 成功。

后台据此知道每台设备当前版本与启动槽。

### 3.2 OTA 检查通知（后台 → 设备）

后台判定某设备该检查更新时，只下发一个"检查"信号——**不带固件 URL，不在此处比对版本**：

- **topic**：`100ask/device/{deviceId}/ota`
- **payload**：`{"action":"check"}`

| 字段 | 必填 | 类型 | 说明 |
|---|---|---|---|
| `action` | 是 | string | 固定 `"check"`，通知设备自检 |

设备收到后**自行** GET `latest.json`、自行比对版本、自行按 json 里的 `url` 下载
（见第 4 节）。固件地址/版本/大小/sha256 都在 `latest.json` 里，**不在推送消息里**。

> 设备 SDK 写死只认 `{"action":"check"}`，不再解析旧的 `{"version","url",...}` 直推格式
> （后台已不直推）。`latest.json` 托管地址见 [cloud.md](cloud.md)。

## 4. 比对规则与设备行为

### 4.1 设备侧比对（设备职责）

**版本比对在设备侧完成**，不在后台。设备收到 `{"action":"check"}` 后：

1. GET `latest.json`，解析 `version`/`url`/`sha256`/`enabled`/`mandatory`。
2. 按 **语义版本整数比对** `latest.version` vs 本地 `AITVBOX_VERSION`：
   ```
   将 "MAJOR.MINOR.PATCH" 按 '.' 拆成三段整数 (a.b.c) vs (x.y.z)
   比较：先比 MAJOR，相等再比 MINOR，相等再比 PATCH。
     latest > 本地 → 有更新，下载安装
     latest == 本地 → 无更新（no_update），除非 mandatory=true 强制
     latest < 本地 → 无更新（默认不降级），除非 mandatory=true 强制
   ```
3. `enabled=false` → 直接 `no_update`，不下载。
4. `mandatory=true` → 即使同版本/旧版本也强制下载安装（模态框隐藏"稍后"）。

**关键约束**：

1. **按整数比，不是字符串字典序**。否则 `1.0.10` 会被字典序判断小于 `1.0.2`（错），
   整数比 `10 > 2` 才对。设备 `version_cmp` 已 `sscanf` 每段后比数值。
2. **每段必须能解析为整数**。非法版本（旧固件上报 `cdb9612-dirty-...`）`sscanf` 解析为 0，
   不会误判——首次 OTA 升级到 `1.0.0+` 即纳入规范。

> 后台职责退为：判定该不该给某设备发 `{"action":"check"}`（按设备上报的 version/在线状态），
> 以及在管理页展示设备上报的 `ota/status` 进度。**新旧判断完全在设备侧。**

### 4.2 设备侧行为

设备收到 OTA 检查通知（`on_ota_check` 回调）时：

1. **不阻塞 MQTT 回调**：置标志，由主循环异步处理（拉 latest.json + 比对 + 下载）。
2. **上报 `checking`** → 拉 `latest.json` → 比对：
   - 无更新 → 上报 `no_update`，结束。
   - 有更新（或 mandatory）→ 上报 `update_available` → 静默下载（带 sha256 校验）。
3. **下载**：复用 `service_ota` 的 libcurl 下载链路；UI 顶栏云徽章显示 **蓝点 + 底边进度条**
   （跟 `TOPIC_OTA_STATUS` progress，不全屏打断）。下完用 `latest.json` 的 `sha256` 校验，
   不符上报 `failed` + 删包（完整性最终仍由 swupdate RSA 签名兜底）。
4. **全程上报 `ota/status`**：downloading→downloaded→installing→rebooting→health_check→
   completed（或 failed）。详见 [cloud.md](cloud.md) 第"OTA 状态上报"节。
5. **提示安装**：下载完成 → 顶栏 **橙点慢闪** + 全屏模态框「发现新版本 vX.X，是否立即安装？」
   - 立即安装 → swupdate A/B 双槽刷写 → reboot → 进新槽 → commit
   - 稍后 → 关框，包保留，OTA 页可手动装（mandatory 强制时无"稍后"）；顶栏回到连接色
6. **上报新版本**：commit 成功后上报 `{"version","boot_slot","upgrade_state":"committed"}`。

顶栏云徽章状态表与实现路径见 [cloud.md](cloud.md)「UI / 顶栏云徽章」一节。

## 5. 设备上的版本文件（/etc/aitvbox-version）

编译产物 `build/aitvbox-version`，由薄包 `aitvbox-suite` 安装到设备 `/etc/aitvbox-version`。
格式（`KEY=VALUE`，每行一个）：

```
AITVBOX_VERSION=1.0.0
VERSION_GIT=v1.0.0-3-gcdb9612
PRODUCT_COMMIT=cdb9612
BUILD_DATE=202606292255
```

| 字段 | 必填 | OTA 比对 | 说明 |
|---|---|---|---|
| `AITVBOX_VERSION` | 是 | ✅ 用 | 比对版本，纯 semver，来自 VERSION 文件。设备上报用此字段。 |
| `VERSION_GIT` | 否 | ❌ | `git describe`，溯源用。 |
| `PRODUCT_COMMIT` | 否 | ❌ | `git rev-parse --short HEAD`，溯源用。 |
| `BUILD_DATE` | 否 | ❌ | 编译时刻 `YYYYMMDDHHMM`，溯源用。 |

> 设备侧 `read_local_version` 只读 `AITVBOX_VERSION=` 这一行的值，其余字段忽略。
> 溯源字段缺了/变了都不影响 OTA 比对。

## 6. 发版流程

```
1. 改 VERSION 文件：1.0.0 → 1.0.1
2. git add VERSION && git commit -m "release: 1.0.1"
3. ./scripts/build_apps.sh           ← 读 VERSION，写 /etc/aitvbox-version
   ./scripts/build_firmware.sh       ← 编译 rootfs（含 /etc/aitvbox-version）
   ./scripts/build_swu.sh            ← 打 .swu（RSA 签名）
4. 上传 .swu 到 100ask 云固件托管
5. 后台创建固件版本记录：version=1.0.1 + .swu 的 url + md5 + size
6. 后台选目标设备/批次 → 推送 OTA（version 字段=1.0.1）
7. 设备 on_ota 收到 → 同版本去重（1.0.1 != 本地 1.0.0）→ 静默下载 → 弹模态框
   → 用户确认 → swupdate A/B 刷写 → reboot 进新槽 → commit
8. 设备重启后上报新版本 1.0.1 → 后台判定已是最新 → 不再推送
```

**唯一强制**：发版时改 `VERSION` 文件并 commit。**不需要打 git tag**——版本号不依赖 tag，
tag 可选（打了只是让 `VERSION_GIT` 溯源字符串更好看，不打也不影响 OTA）。
git 工作区是否 dirty 不影响 `AITVBOX_VERSION`（它来自文件，不来自 git），所以编译脚本
不校验、不警告 dirty。

## 7. 起始版本

当前线上设备跑的固件对应 git commit `cdb9612`（旧版本号 `cdb9612-dirty-202606292255`）。
本规范落地后，**起始比对版本定为 `1.0.0`**：

- `VERSION` 文件初始化为 `1.0.0`
- 后续发版从 `1.0.1` 起步

> 已部署设备（上报旧格式 `cdb9612-dirty-...`）后台按 4.1 第 2 条处理为"版本未知"，
> 通过一次 OTA 升级到 `1.0.0+` 即纳入规范。

## 8. 字段对照速查

| 环节 | 用哪个 | 格式 | 示例 |
|---|---|---|---|
| 仓库 `VERSION` 文件 | — | semver | `1.0.0` |
| 设备 `/etc/aitvbox-version` | `AITVBOX_VERSION` | semver | `1.0.0` |
| 设备上报 MQTT version topic | `version` (+boot_slot+upgrade_state) | semver | `1.0.0` |
| `latest.json` 的 `version` | 目标版本 | semver | `1.0.1` |
| OTA 检查推送 payload | `action` | 固定 | `{"action":"check"}` |
| `latest.json` 的 `url` | 固件下载地址 | HTTPS URL | `https://.../fw-1.0.1.swu` |
| 设备模态框显示 | version | semver | `发现新版本 1.0.1` |
| 人排查（日志/后台） | `VERSION_GIT` | git describe | `v1.0.0-3-gcdb9612` |

**一句话**：凡是要机器比对的，一律纯 semver（`1.0.0`），来自 `VERSION` 文件，与 git 无关；
凡是要人看代码来源的，用 git describe，纯日志，不参与比对。版本比对在**设备侧**（读
`latest.json` 比对 current vs target），后台只发 `{"action":"check"}` 信号。
