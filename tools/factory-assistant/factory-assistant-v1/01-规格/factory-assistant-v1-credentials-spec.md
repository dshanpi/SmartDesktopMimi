# 出厂助手 V1：身份写入与涂鸦 License 管理规格

- 文档版本：1.1.0
- 日期：2026-07-16
- 已确认：100ask 量产私钥与现网 provision 公钥已配对；涂鸦交付为 xlsx，当前可先单条联调。
- 适用：`tools/factory-assistant`（仓库样本）及 Windows 正式版（逻辑对齐，实现可有差异）
- 读者：出厂助手开发
- 产品：AITVBox / A133
- **前提决策（已冻结）**：
  1. **100ask 业务云保持现状**，不新增 `prepare / confirm-write / production-status` 等量产云接口。
  2. 已验证链路继续复用：设备 `provision → secret → MQTT → 云端 OTA`。
  3. **出厂签名、涂鸦 License 分配、写机、校验、台账全部由出厂助手本地完成。**
  4. 工位由自有人员操作；实现以**防误操作、可追溯、可重试**为主，不按不可信代工厂最高安全等级设计。
  5. 当前助手已具备 **硬件测试**；本文规定待补的 **100ask 身份 + 涂鸦 License + 凭据验收**。

> 旧文档 `tuya-license-cloud-provisioning-spec.md` / `factory-cloud-implementation-spec.md` 描述的是“量产云 API”方案，**V1 不实现那套云接口**。设备路径、文件格式、签名算法仍与那些文档及现有脚本一致；仅把“云 prepare”替换为“助手本地签发/发料”。

---

## 1. 目标与非目标

### 1.1 V1 必须做到

对一台已用 PhoenixSuit 烧好**统一固件**的设备，出厂助手一键/自动完成：

| 步骤 | 结果 |
|---|---|
| 读身份 | CPUID、固件版本、设备内涂鸦 PID |
| 签 100ask 身份 | 生成 `device_sig` 并写入设备 |
| 发涂鸦 License | 从本地库存预占一组 UUID/AuthKey 并写入设备 |
| 写后校验 | 读回哈希、权限、所有者；必要时重启再读 |
| 等设备自注册 | 设备用现有逻辑 provision 拿 `secret` 并尽量确认 100ask 侧可用 |
| 等涂鸦就绪 | 设备 runtime 接受 License（未要求用户 App 绑定） |
| 硬件测试 | 复用现有硬件检测（可与凭据并行策略见第 8 节） |
| 记台账 | 本地数据库留下可追溯记录 |
| 最终 PASS | 仅当「凭据通过 + 硬件通过」 |

工人界面仍以 **等待设备 / 进行中 / PASS / FAIL+中文原因** 为主，不展示 AuthKey、私钥、完整 secret。

### 1.2 V1 明确不做

| 不做 | 说明 |
|---|---|
| 改 100ask 云接口 | 不新增工厂 API |
| 出厂助手执行 OTA 刷写 | OTA 由设备连云后云端推送；助手只保证设备**具备连云身份** |
| 用户扫码绑定作为产测条件 | 新机未售，不能要求 App 绑定 |
| 把 `device_secret` 显示/导出 | secret 仅设备 provision 自取自存 |
| 每台设备不同固件镜像 | 统一 `.img`，差异只写 overlay 凭据 |
| 完整代工厂 mTLS/KMS 体系 | V1 本地受控即可；预留后续替换点 |

### 1.3 和“OTA”的关系（避免误解）

```text
出厂助手 V1 负责：让设备合法拥有 device_sig + 涂鸦 License
        ↓
设备自己：provision → secret → 连 100ask MQTT
        ↓
上市后：云端推 OTA（已验证能力，与助手无关）
```

助手 **不** 下载 `.swu`，**不** 调 `swupdate`。  
若产线要做“升级能力抽检”，可在凭据通过后，由测试人员在云后台对样机推一次 OTA；这不属于助手默认流水线。

---

## 2. 现状盘点（仓库样本）

路径：`tools/factory-assistant/`

| 模块 | 现状 | V1 要求 |
|---|---|---|
| `factory-core` ADB | 扫描、读 CPUID/版本/PID、shell、受限 push/pull | 扩展：root、等挂载、原子写文件、读回校验、重启 |
| `factory-core` hardware | 自动项 + 人工确认项已实现 | 保持；整机 PASS 与其 AND |
| `RunPhase` | 已有 `preparing/writing/verifying/validating/credentialPassed/...` | 真正跑通，不再只是占位 |
| Tauri/前端 | 硬件测试 UI 完整 | 增加凭据阶段进度、库存余量、台账查询（可简） |
| 配置检查 | 仍检查旧“量产云”环境变量 | **改为 V1 本地配置**（见第 9 节） |
| Linux 脚本桥 | 曾计划调 `factory_provision_device.sh` | Windows 正式版 **必须纯 Rust 实现**，不依赖 Bash |
| 参考脚本 | `scripts/provision_device.sh`、`provision_tuya_license.sh` | 行为金标准；逻辑迁入 `factory-core` |

已具备的设备读取（勿重复发明）：

- CPUID：`/sys/class/sunxi_info/sys_info` → `sunxi_serial`，32 位小写 hex  
- 固件：`/etc/aitvbox-version` → `AITVBOX_VERSION=`  
- 涂鸦 PID：`/etc/aitvbox-tuya-pid` → `TUYA_PID=`（16 位字母数字）

---

## 3. 总体架构（不改云端）

```text
┌─────────────────────────────────────────────────────────────┐
│ 出厂助手（Windows 工位，自有人员）                            │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │ 签名模块      │  │ License 库存  │  │ 生产台账 SQLite   │  │
│  │ 本地 PEM 私钥 │  │ 导入/预占/绑定 │  │ 每台 PASS/FAIL    │  │
│  └──────┬───────┘  └──────┬───────┘  └─────────┬─────────┘  │
│         │                 │                    │            │
│         └────────────┬────┴────────────────────┘            │
│                      ▼                                      │
│              流水线状态机 + ADB 写机                           │
└──────────────────────┬──────────────────────────────────────┘
                       │ USB ADB
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ 设备（统一固件）                                              │
│  /etc/100ask/device_sig     ← 助手写入                       │
│  /factory/tuya/license.env  ← 助手写入                       │
│  /etc/100ask/secret         ← 设备 provision 自写（助手不写） │
│  service_cloud / 涂鸦进程    ← 已有运行时，不改协议            │
└─────────────────────────────────────────────────────────────┘
                       │ 工厂 Wi-Fi（产测阶段）
                       ▼
              100ask 云（现状）+ 涂鸦云（现状）
```

**职责切分：**

| 角色 | 负责 |
|---|---|
| 全志 PhoenixSuit | 只烧统一 `.img` |
| 出厂助手 | 签名、License、写机、校验、等就绪、硬件测、台账、PASS |
| 设备固件 | provision、连云、涂鸦加载、OTA 客户端（已有） |
| 100ask 云 | 验签发 secret、MQTT、OTA 推送（已有，不改） |
| 涂鸦云 | 接受 UUID/AuthKey（已有，不改） |

---

## 4. 两套凭据，不要混

| 名称 | 作用 | 谁产生 | 写入位置 | 助手是否接触明文 |
|---|---|---|---|---|
| `device_sig` | 证明 CPUID 被 100ask 私钥签过，用于 provision | 助手用本地私钥签 | `/etc/100ask/device_sig` | 是（写入需要） |
| `device_secret` | 设备连 100ask MQTT / 后续 OTA 身份 | 设备 provision 向云端领取 | `/etc/100ask/secret` | **否**（禁止 cat 到 UI/日志） |
| 涂鸦 UUID+AuthKey | 涂鸦 AI/MQTT 一机一密 | 涂鸦采购后导入助手库存，再分配 | `/factory/tuya/license.env` | 是（写入需要，UI 不展示 AuthKey） |

签名原文（必须与现网一致）：

```text
UTF-8: 100ask:<cpuid小写32位hex>
算法: ECDSA P-256 + SHA-256
输出: ASN.1 DER 二进制 → 小写十六进制字符串（无 0x、无换行）
写入文件: 仅该 hex 字符串，无换行
```

参考实现：私有 SDK 中的 `tools/sign_device.sh`（由 `AITVBOX_100ASK_SDK_ROOT` 定位）。
OpenSSL 等价：

```bash
printf '100ask:%s' "$CPUID" | openssl dgst -sha256 -sign 100ask_ecdsa.pem | xxd -p -c 256
```

DER hex 长度可变，校验时：

- 全小写 hex
- 偶数长度
- 建议长度范围约 128～160 字符（不要写死 142）
- 可用公钥对 `100ask:<cpuid>` 本地验签一次再写入

---

## 5. 涂鸦 License 如何管理（V1 核心设计）

云端不接管库存时，**出厂助手 = License 仓库管理员**。  
推荐：工位本地 **SQLite**（可单机；多工位见 5.6）。

### 5.1 从涂鸦拿到什么

#### 交付物形态（已确认样本命名）

涂鸦侧交付常见为 **Excel（`.xlsx`）**，文件名类似：

```text
cto703cud3ffkxja-Vc9JufEXJzNpeev4-2-20260611101900.xlsx
```

命名一般含：令牌/批次片段、数量、时间戳等，**以表内列为准，不要解析文件名当 UUID**。

当前阶段：

- 可能只有 **1 条** License 做联调——完全够用：导入 1 条 → 分给 1 台设备跑通流水线。
- 批量采购后格式可能微调；导入层应 **按列名/表头识别**，不要写死某一版列顺序。
- 若首版 xlsx 解析成本高，允许 V1 先支持「从 xlsx 另存/粘贴为 CSV」或「手动录入一组 UUID+AuthKey」；核心状态机不变。

#### 表内必需字段

每行至少：

| 字段 | 约束 | 说明 |
|---|---|---|
| UUID | 恰好 **20** 个非空白可打印字符 | 列名可能是 `UUID` / `uuid` / `设备UUID` 等，导入时做别名映射 |
| AuthKey | 恰好 **32** 个非空白可打印字符 | 列名可能是 `AuthKey` / `authkey` / `AUTHKEY` 等 |
| PID（可选列） | 16 位 | 有则必须与本批次 `TUYA_PID` 一致；无则用工位配置 PID |

拿到首份真实 xlsx 后，开发应打开看一眼表头，把实际列名补进别名表；**设备落盘格式不随 xlsx 变化**。

设备落盘规范字节（哈希必须按此计算，**LF 结尾，禁止 CRLF**）：

```text
TUYA_OPENSDK_UUID=<20-char-uuid>
TUYA_OPENSDK_AUTHKEY=<32-char-auth-key>
```

注意最后一行也有 `\n`。  
`licenseFileSha256 = SHA256(上述 UTF-8 字节)`。

### 5.2 推荐库存表 `tuya_licenses`

```sql
CREATE TABLE tuya_licenses (
  id                INTEGER PRIMARY KEY,
  tuya_pid          TEXT NOT NULL,          -- 16 位
  uuid              TEXT NOT NULL,          -- 20
  auth_key          TEXT NOT NULL,          -- 32；V1 可明文存受控目录库内
  uuid_sha256       TEXT NOT NULL,          -- 便于日志只打指纹
  auth_key_sha256   TEXT NOT NULL,
  state             TEXT NOT NULL,          -- 见状态机
  assigned_cpuid    TEXT,                   -- 绑定后永久记录
  production_id     TEXT,                   -- 关联生产记录
  imported_at       TEXT NOT NULL,
  assigned_at       TEXT,
  written_at        TEXT,
  note              TEXT,
  UNIQUE(tuya_pid, uuid),
  UNIQUE(tuya_pid, assigned_cpuid)          -- 同一 PID 下 CPUID 最多一组
);

CREATE INDEX idx_tuya_lic_state ON tuya_licenses(tuya_pid, state);
```

### 5.3 License 状态机（本地）

```text
AVAILABLE  --预占-->  RESERVED  --写机成功-->  WRITTEN  --产测PASS-->  SHIPPED
                |                  |
                |写失败/放弃        |产测FAIL 且确认需隔离
                v                  v
            AVAILABLE          QUARANTINED
            (仅未绑定 CPUID 的 RESERVED 可回退)
```

**硬规则（必须写进代码）：**

1. **同一 `uuid` 全局只分配一次**（同一 PID 池内）。  
2. **同一 `cpuid` 在同一 PID 下只绑定一组 License**。  
3. 设备已绑定过则：  
   - 重试/重写 **只能用原 UUID/AuthKey**；  
   - **禁止**失败后自动换下一组新 License。  
4. 仅当 `RESERVED` 且 **尚未成功写入并确认**、且 **未写入 `assigned_cpuid` 永久绑定** 时，才允许管理员操作“释放回 AVAILABLE”。  
   一旦 `assigned_cpuid` 落库，默认不可回池（防止一密多机）。  
5. 导入时校验 UUID/AuthKey 长度与字符；重复 uuid 跳过或报错。  
6. UI 可显示：库存剩余 `AVAILABLE` 数量、本班次消耗；**列表默认只显示 uuid 前后缀 + 状态，不展示 AuthKey**。

### 5.4 分配算法（单机事务）

```text
BEGIN;
  -- 若该 cpuid 已有绑定记录：返回原行（RESUME）
  SELECT * FROM tuya_licenses
   WHERE tuya_pid=? AND assigned_cpuid=? AND state IN ('RESERVED','WRITTEN','SHIPPED','QUARANTINED');

  -- 否则预占一行
  SELECT id FROM tuya_licenses
   WHERE tuya_pid=? AND state='AVAILABLE'
   ORDER BY id
   LIMIT 1;
  -- 更新 state=RESERVED, assigned_cpuid=?, assigned_at=now, production_id=?
COMMIT;
```

SQLite 下用立即事务 + 单写者即可；多助手进程禁止同时写同一库文件（见 5.6）。

### 5.5 导入功能（管理员）

助手提供「导入 License」：

1. 选择 **xlsx 和/或 CSV**（优先 xlsx，因涂鸦交付多为 xlsx）。  
2. 读第一张表（或用户选 sheet）；按表头别名找到 UUID / AuthKey 列。  
3. 校验每一行长度与字符。  
4. 与当前工位配置 `TUYA_PID` 比对（表内有 PID 列时）。  
5. 批量插入 `AVAILABLE`。  
6. 显示成功 N 条 / 跳过重复 M 条 / 失败列表。

**联调最小路径（仅 1 条 License 时）：**

- 导入该 xlsx（或手动录入这一组 UUID/AuthKey）→ 库存 `AVAILABLE=1`  
- 跑一台干净设备 → 预占后 `AVAILABLE=0`  
- 同机重试不得再要第二组；换机时若库存空则 `FAIL_LICENSE_EMPTY`（符合预期）

**CSV 兜底示例（xlsx 未接好时可用）：**

```csv
uuid,auth_key
uuid20charactersxxx,authkey32charactersxxxxxxxxxxxx
```

导入文件用完可删除；**不要**把全量 License 明文长期放在桌面。数据库文件放在工位数据目录，ACL 仅当前用户。

### 5.6 多工位怎么分（V1 简单策略）

| 方案 | 适用 | 做法 |
|---|---|---|
| A. 每工位独立库存文件 | 推荐 V1 | 管理员把 License 池 **按数量拆成多个 CSV** 导入各工位；互不重叠 |
| B. 共享 NAS 上一个 SQLite | 不推荐除非有文件锁经验 | 易锁库、易损坏 |
| C. 以后上云库存 | V2 | 替换 `LicenseProvider` 实现 |

V1 选 **方案 A**：人肉拆池比共享库简单可靠。

### 5.7 AuthKey 存储与日志

- V1 允许 SQLite 内存/文件保存 AuthKey 明文（工位自用）。  
- 日志、事件流、UI、导出报表：**只打** `uuid` 掩码、`uuid_sha256` 前 8 位、`auth_key_sha256` 前 8 位。  
- 禁止 `console.log(authKey)`、禁止把 AuthKey 拼进 adb 命令行参数（应用 stdin/`shell` 重定向写入，参考现有脚本）。

---

## 6. 100ask 签名如何管理（V1）

### 6.1 私钥放置

| 项 | 要求 |
|---|---|
| 文件 | 工位配置目录下的 `100ask_ecdsa.pem`（ECDSA P-256） |
| 来源 | 与现网 provision 验签公钥匹配的受控密钥；公开仓库不提供，**量产应用量产钥** |
| 权限 | Windows：仅工位账户可读；不要提交 Git、不要打包进公开安装包默认资源 |
| 配置键 | `SIGNING_PRIVATE_KEY_PATH` |
| 可选 | `SIGNING_PUBLIC_KEY_PATH` 用于写前本地验签 |

### 6.2 签名模块 API（建议）

```text
sign_device_sig(cpuid: &str) -> Result<String /* lowercase der hex */>
verify_device_sig(cpuid: &str, sig_hex: &str) -> Result<bool>
```

规则：

- 入参 cpuid 先规范化为 32 位小写 hex，非法直接失败。  
- **同一 cpuid 多次签名**：ECDSA 每次 DER 可能不同；台账应保存**实际写入设备的那次** sig 及其 sha256。  
- 若该 cpuid 台账已有成功写入的 sig，重试写机时 **优先重写同一 sig**（从台账读回），避免无意义换签名（非必须，但推荐）。

### 6.3 与“云端不改”的关系

设备 provision 仍然：

```http
POST https://www.100ask.net/api/device/provision
Body: deviceId/chipid/signature/...
```

云端用**已部署的公钥**验签。  
因此助手本地私钥必须与云端公钥是一对。换钥需与 100ask 后端同步，**不要助手私自 genkey 除非云端同步换公钥**。

---

## 7. 设备写入规范（金标准）

实现必须与下列脚本行为一致（建议直接对照移植，而非重新发明路径）：

- `scripts/provision_device.sh` — device_sig  
- `scripts/provision_tuya_license.sh` — license.env  

### 7.1 前置

1. `adb devices` 恰好 **1** 台 `device` 状态（或明确选中 serial）。  
2. `adb root` 后 `wait-for-device`。  
3. 两次读取 CPUID 一致。  
4. 固件版本在批次允许列表内（配置 `ALLOWED_FIRMWARE_VERSIONS` 或精确 `FIRMWARE_VERSION`）。  
5. 设备 `/etc/aitvbox-tuya-pid` 与工位 `TUYA_PID` 一致。  
6. 持久化挂载就绪（**否则禁止写**）：

```text
/proc/self/mountinfo 中存在挂载点：
  /etc/100ask
  /factory/tuya
```

超时建议 30s 轮询。未就绪写入会导致重启/OTA 后凭据丢失。

### 7.2 写 `device_sig`

| 项 | 值 |
|---|---|
| 路径 | `/etc/100ask/device_sig` |
| 内容 | 小写 DER hex，无换行 |
| 权限 | `0600` |
| 所有者 | `root:root`（0:0） |
| 方式 | 同目录临时文件 → `chmod 600` → `mv` 原子替换 → `sync` |
| 校验 | `cat` 读回与写入逐字相等；`stat` 权限 600 |

### 7.3 写涂鸦 License

| 项 | 值 |
|---|---|
| 路径 | `/factory/tuya/license.env` |
| 内容 | 第 5.1 节规范两行 + 最终 LF |
| 权限 | 目录 `0700`，文件 `0600` |
| 所有者 | `0:0` |
| 方式 | 同目录临时文件原子替换 + sync；**内容经 adb stdin 管道，避免命令行暴露 AuthKey** |
| 校验 | 整文件 SHA-256 与本地计算一致；权限与所有者 |

写成功后建议：

```text
killall your_chat_bot_QIO_1.0.1.bin
```

（名称以固件实际二进制为准；杀进程后 `lv_backend` 会拉起并重新加载 License。）  
`device_sig` 支持 backend 热加载，可不强求重启 UI。

### 7.4 写后复核（推荐）

V1 建议默认 **重启一次再读**：

1. 写两组文件并即时校验通过。  
2. `adb reboot` → 等 ADB 回来 → root。  
3. 再确认挂载、`device_sig`、license 哈希、权限。  
4. CPUID 仍与工单一致。  

若追求节拍可配置 `REBOOT_VERIFY=0` 跳过，但试产阶段应开启。

### 7.5 已有凭据设备策略

| 设备本地状态 | 行为 |
|---|---|
| 两者皆无 | 新生产：签名 + 预占 License + 写入 |
| 有 sig 无 license / 相反 | 查台账；有历史则补写同一套；无历史则 FAIL 人工处理 |
| 两者都有且与台账一致 | 可跳过写入，直接进入「等待就绪 + 硬件测」或「复检」 |
| 两者都有且与台账冲突 | `FAIL_IDENTITY_CONFLICT`，不要覆盖 |
| 台账已 SHIPPED 再来 | 默认拒绝生产；提供「复检模式」只测硬件不重发 License |

---

## 8. 凭据验收（不靠量产云 status）

因不改云端，助手用 **ADB 观测设备侧结果**。

### 8.1 100ask 身份通过条件

在超时 `CREDENTIAL_TIMEOUT_SEC`（建议默认 180～240）内满足：

1. `/etc/100ask/device_sig` 仍正确。  
2. `/etc/100ask/secret` **文件存在**且权限为 `0600`（**不要读取内容到主机日志**）。  
3. 可选加强：`/tmp/lv_backend.log` 或 `/tmp/cloud.log` 出现 provision 成功 / MQTT connected 类关键字（关键字需按现网日志标定，做宽松匹配）。  

说明：secret 出现通常表示 provision 已成功；MQTT 是否连上还依赖工厂 Wi-Fi。  
产线必须提供可上网的工厂 Wi-Fi，并在测试前配置设备联网（见 8.3）。

### 8.2 涂鸦 License 通过条件

满足其一即可（与设备 `service_ai_is_tuya_license_ready` 语义对齐）：

1. 日志/状态表明未绑定新机已进入可绑定态（有 bind_url / `tuya_bind_qr_pending` 等价信号）；或  
2. 已绑定设备 MQTT 已连接。  

**助手侧落地方式（选一种实现，推荐 A+B）：**

| 方式 | 做法 |
|---|---|
| A. 日志轮询 | `adb exec-out log` 或读 `/tmp/tuya_chat_bot.log`、`/tmp/lv_backend.log`，匹配已约定成功关键字 |
| B. 状态文件（若设备后续可加） | 读固定路径 JSON；**当前若无文件，先用 A** |
| C. 人工确认 | 仅作开发调试，量产不作为默认 |

**不要**把“用户是否在手机完成绑定”当作 PASS 条件。

超时未就绪：

- License 保持 `WRITTEN` 绑定，不回池。  
- 工单 `FAIL_TUYA_NOT_READY` 或 `FAIL_CLOUD_NOT_READY`。  
- 允许同一 CPUID 复检重试（不换 License）。

### 8.3 工厂网络

流水线在「等待 provision / 涂鸦」前需要设备能上网：

**V1 建议顺序：**

1. 硬件测试里已有 Wi-Fi RF 扫描（不连路由）。  
2. 凭据等待阶段：由工艺保证设备已连工厂 SSID，或助手通过 ADB 调用设备已有联网配置接口（若无可先 **工艺+人工连网**，助手只轮询就绪）。  
3. PASS 后：**删除工厂 Wi-Fi 配置**，避免流出带厂内密码的机器（有现成命令则自动，否则检查清单人工项）。

若短期无法自动配网：文档与 UI 明确「请先连接工厂 Wi-Fi」，超时给出中文提示。

### 8.4 与硬件测试的顺序

推荐默认：

```text
写凭据并校验
  →（可选重启复核）
  → 并行或先后：等待凭据就绪 + 硬件自动项
  → 硬件人工确认项
  → 汇总 PASS
```

也可以配置：

- `ORDER=credentials_first`（默认）：先保证能连云，再测交互  
- `ORDER=hardware_first`：先硬件后写号（适合硬件坏件不浪费 License）  

**强烈建议试产使用 `hardware_first` 中的“自动硬件项先跑，PASS 后再预占 License”**，减少坏机吞 License：

```text
扫描身份
  → 自动硬件项（identity/存储/内存/射频等）
  → 若自动项 FAIL：不分配 License，直接 FAIL
  → 签名 + 预占 License + 写入
  → 等待凭据就绪
  → 人工硬件项（屏/触/喇叭/麦）
  → 总 PASS → License 标 SHIPPED
```

人工项 FAIL：License 已写入则标 `WRITTEN`/`QUARANTINED`，**不回池**，进返修台账。

---

## 9. 工位配置（替换旧云变量）

V1 配置项（Windows 可用环境变量或 `station.json`，二选一；敏感项不要进前端）。

| 键 | 必填 | 说明 |
|---|---|---|
| `STATION_ID` | 是 | 工位名，写入台账 |
| `BATCH_NUMBER` | 是 | 批次号 |
| `TUYA_PID` | 是 | 16 位，须与设备 `/etc/aitvbox-tuya-pid` 一致 |
| `SIGNING_PRIVATE_KEY_PATH` | 是 | 100ask PEM 私钥路径 |
| `SIGNING_PUBLIC_KEY_PATH` | 建议 | 写前验签 |
| `FACTORY_DATA_DIR` | 是 | SQLite、导入缓存、日志目录 |
| `FIRMWARE_VERSION` 或允许列表 | 是 | 准入固件 |
| `FIRMWARE_SHA256` | 建议 | 批次镜像校验（人工确认烧录包时用） |
| `CREDENTIAL_TIMEOUT_SEC` | 否 | 默认 240 |
| `REBOOT_VERIFY` | 否 | 默认 1 |
| `PIPELINE_ORDER` | 否 | `hardware_auto_first`（推荐）/ `credentials_first` |
| `FACTORY_WIFI_SSID` | 否 | 仅提示或未来自动连接 |

**删除/忽略 V1 必填：**  
`FACTORY_API_BASE`、`FACTORY_CLIENT_CERT/KEY`、`FACTORY_CA_FILE` 等量产云 mTLS 项（可留扩展，但不阻塞启动）。

配置检查 UI：对 V1 必填项显示绿/红；`cloudConfigured` 语义建议改名为 `stationConfigured` 或保留字段但按新规则计算。

---

## 10. 生产台账

### 10.1 表 `production_records`

```sql
CREATE TABLE production_records (
  id                 TEXT PRIMARY KEY,     -- uuid
  station_id         TEXT NOT NULL,
  batch_number       TEXT NOT NULL,
  adb_serial         TEXT,
  cpuid              TEXT NOT NULL,
  firmware_version   TEXT,
  tuya_pid           TEXT,
  device_sig_sha256  TEXT,
  license_uuid       TEXT,                 -- 可存全文或掩码；AuthKey 不存明文到此表亦可
  license_file_sha256 TEXT,
  phase              TEXT NOT NULL,
  credential_ok      INTEGER NOT NULL DEFAULT 0,
  hardware_ok        INTEGER NOT NULL DEFAULT 0,
  final_result       TEXT,                 -- PASS / FAIL / IN_PROGRESS
  failure_code       TEXT,
  failure_detail     TEXT,
  started_at         TEXT NOT NULL,
  finished_at        TEXT,
  operator_note      TEXT
);

CREATE INDEX idx_prod_cpuid ON production_records(cpuid);
CREATE INDEX idx_prod_started ON production_records(started_at);
```

### 10.2 导出

管理员可导出 CSV（无 AuthKey 列）：时间、工位、批次、CPUID、固件、UUID、结果、失败码。  
用于 ERP/表格对账与售后。

---

## 11. 流水线状态机（与现有 `RunPhase` 对齐）

现有枚举（`factory-core` / 前端 `types.ts`）：

```text
Idle → Scanning → ReadingIdentity → Preparing → Writing
  → Verifying → Validating → HardwareTest
  → CredentialPassed → Passed | Failed
```

### 11.1 推荐映射

| Phase | 助手动作 | progress 建议 |
|---|---|---|
| `scanning` | adb devices | 5 |
| `readingIdentity` | CPUID/版本/PID，校验格式与批次 | 15 |
| `preparing` | 查台账；签名；License 预占（或硬件自动项已通过后再预占） | 30 |
| `writing` | 写 device_sig + license.env | 45 |
| `verifying` | 读回哈希/权限；可选 reboot 再读 | 60 |
| `validating` | 等 secret 出现 + 涂鸦就绪（工厂网络） | 75 |
| `credentialPassed` | 凭据子结果 OK（尚未等于整机 PASS） | 80 |
| `hardwareTest` | 现有硬件流程（若未提前做完） | 90 |
| `passed` | 台账 PASS，License→SHIPPED | 100 |
| `failed` | 写 failure_code，License 按规则保留绑定 | — |

注意：若采用「自动硬件优先」，可将自动硬件放在 `preparing` 之前，用现有 `hardwareTest` 只跑人工项，或拆分 progress 文案，但 **最终 Passed 必须两者都 OK**。

### 11.2 失败码（写入台账与 UI）

| 代码 | 含义 |
|---|---|
| `FAIL_NO_DEVICE` | 无设备/多设备 |
| `FAIL_ADB` | adb/root 失败 |
| `FAIL_CPUID` | CPUID 读失败或两次不一致 |
| `FAIL_FIRMWARE` | 版本/PID 与批次不符 |
| `FAIL_MOUNT` | 持久化挂载未就绪 |
| `FAIL_SIGN` | 签名失败 |
| `FAIL_LICENSE_EMPTY` | 库存无 AVAILABLE |
| `FAIL_LICENSE_BIND` | CPUID 与 License 绑定冲突 |
| `FAIL_WRITE_SIG` | device_sig 写入/校验失败 |
| `FAIL_WRITE_LICENSE` | license 写入/校验失败 |
| `FAIL_REBOOT_VERIFY` | 重启后读回不一致 |
| `FAIL_CLOUD_NOT_READY` | 超时无 secret / 100ask 未就绪 |
| `FAIL_TUYA_NOT_READY` | 超时涂鸦未接受 License |
| `FAIL_HARDWARE` | 硬件测试未通过 |
| `FAIL_IDENTITY_CONFLICT` | 设备已有凭据与台账冲突 |
| `FAIL_ALREADY_SHIPPED` | 已出货设备禁止重复生产 |

中文 `status_message` 示例：`库存不足，请导入涂鸦 License`、`请确认设备已连接工厂 Wi-Fi`。

---

## 12. 模块划分（给 Windows 正式版实现）

建议全部放进 `factory-core`（或正式版等价 crate），**禁止**业务逻辑只写在前端：

```text
factory-core/
  adb.rs              # 已有；扩展 root/wait/mount/write
  hardware.rs         # 已有
  signing.rs          # 新增：openssl/rustcrypto ECDSA P-256
  license_store.rs    # 新增：SQLite 库存
  production_store.rs # 新增：台账
  pipeline.rs         # 新增：状态机编排
  credential_watch.rs # 新增：轮询 secret/日志就绪
  model.rs            # 扩展字段
```

### 12.1 ADB 写入注意（Windows）

- 使用已有 `AITVBOX_ADB_PATH` / 内置 platform-tools。  
- 写文件优先：  
  `adb exec-out` / `adb shell` + stdin；或先写主机临时文件再 `adb push` 到 `/factory/tuya/.tmp` 后 `mv`（push 目标勿突破安全校验时可先扩展 `adb.rs` 白名单：允许 `/etc/100ask/`、`/factory/tuya/` 下临时名）。  
- 当前样本 `push_file` 仅允许 `/tmp/`，**量产写凭据需扩展白名单或改用 shell 重定向**，与 `provision_tuya_license.sh` 一致更佳。  
- 所有 adb 调用带 serial；超时可配置。

### 12.2 前端改动要点

1. 配置区：V1 本地项，去掉强制云 mTLS。  
2. 主按钮：「开始量产」= 跑 `pipeline`（不仅是硬件）。  
3. 可保留「仅硬件测试」入口（已有）。  
4. 显示：库存剩余、当前 phase、CPUID、固件、结果。  
5. 管理员页：导入 License、导出台账、（可选）释放误预占。  
6. 事件流不出现 AuthKey/私钥/secret。

### 12.3 与样本 `RunPhase` 的兼容

样本已有 `credentialPassed` 与 `cloudConfigured`：  
实现时让真实 pipeline 驱动这些字段，删除“演示假进度”，避免 UI 假 PASS。

---

## 13. 端到端验收清单（交给测试）

### 13.1 单机 Happy Path

1. 导入测试 License（**1 条即可**联调；有多条更好）。  
2. 配置私钥与 `TUYA_PID`。  
3. 烧录统一固件的干净设备接入 USB。  
4. 跑通流水线 → **PASS**。  
5. 检查设备：  
   - `device_sig` 600 且非空  
   - `license.env` 600 且哈希正确  
   - `secret` 存在（内容不导出）  
6. 台账一条 `PASS`，License 状态 `SHIPPED`。  
7. 库存 `AVAILABLE` 减 1。

### 13.2 幂等与防呆

8. 同一设备不断线再跑：不消耗第二组 License。  
9. 写 license 过程中拔 USB：License 不错误标 SHIPPED；重试仍同一 UUID。  
10. 库存为空：明确 `FAIL_LICENSE_EMPTY`。  
11. PID 与设备不一致：开写前失败。  
12. 未挂载 `/factory/tuya`：拒绝写入。

### 13.3 凭据与网络

13. 不连 Wi-Fi：应超时 `FAIL_CLOUD_NOT_READY` 或 `FAIL_TUYA_NOT_READY`，不误报 PASS。  
14. 连工厂网后复检：同一 License 可通过。  

### 13.4 硬件门禁

15. 强制假失败某一自动硬件项：不得预占 License（若启用 hardware_auto_first）。  
16. 凭据已写后人工项 FAIL：License 不回池，结果 FAIL 可追溯。

### 13.5 回归

17. 「仅硬件测试」仍可用。  
18. 前端永不显示 AuthKey。  
19. Windows 包无 Bash 依赖即可完成上述路径。

---

## 14. 参考命令与路径速查

```text
# CPUID
cat /sys/class/sunxi_info/sys_info | grep sunxi_serial

# 版本 / PID
cat /etc/aitvbox-version
cat /etc/aitvbox-tuya-pid

# 挂载
grep -E '/etc/100ask|/factory/tuya' /proc/self/mountinfo

# 目标文件
/etc/100ask/device_sig
/etc/100ask/secret          # 只检查存在与权限
/factory/tuya/license.env

# 日志（关键字需按实机标定）
/tmp/lv_backend.log
/tmp/cloud.log
/tmp/tuya_chat_bot.log
```

仓库金标准脚本：

```text
scripts/provision_device.sh
scripts/provision_tuya_license.sh
$AITVBOX_100ASK_SDK_ROOT/tools/sign_device.sh
```

设备行为说明：

```text
docs/cloud.md
docs/tuyaopen.md
```

---

## 15. 实现优先级（建议排期）

| 优先级 | 项 | 预估意义 |
|---|---|---|
| P0 | SQLite License 导入/预占/绑定规则 | 没有就无法量产涂鸦 |
| P0 | 本地签名 + 写 device_sig | 没有就无法 provision/OTA 身份 |
| P0 | 写 license.env + 校验 | 涂鸦运行时 |
| P0 | 流水线串硬件 + 台账 PASS | 整机闭环 |
| P1 | 重启复核、secret/涂鸦就绪轮询 | 降低流出坏号 |
| P1 | 管理员导入/导出 UI | 自己人好用 |
| P2 | 自动连工厂 Wi-Fi、扫码 SN | 节拍优化 |
| P2 | 加密库内 AuthKey、多工位同步 | 规模化再做 |

---

## 16. 给开发的一句话结论

> **不要等云端改造。**  
> 在出厂助手内实现：本地私钥签名、本地涂鸦库存、ADB 原子写机、等待设备自注册与涂鸦就绪、再与现有硬件测试做 AND，本地 SQLite 出台账。  
> 设备侧协议与 100ask/涂鸦云保持现状；助手补齐的是「量产时每台机器的号从哪来、怎么写、怎么记」。

---

## 17. 已确认 / 仍待确认

### 已确认（2026-07）

| 项 | 结论 |
|---|---|
| 100ask 私钥与现网公钥 | **已配对**，助手用现有量产/现网钥签名即可 |
| 涂鸦交付形态 | **xlsx**，样本名类似 `…-2-20260611101900.xlsx` |
| 当前采购量 | 可先 **1 条** 联调；批量后格式允许再适配 |
| 设备落盘格式 | 固定 `license.env` 两行，不随 xlsx 变 |

### 仍建议实现前对齐

1. 首份真实 xlsx 的**表头列名**（开发打开一次即可写进别名表）  
2. 产线默认 `hardware_auto_first` 还是先写号？  
3. 工厂 Wi-Fi 是助手自动配还是工艺保证？  
4. 外壳 SN 是否本 V1 录入？（建议有则加一列，售后极有用）

按本文编码；若与 Windows 正式版目录结构不同，保持 **状态机与数据规则一致** 即可。
