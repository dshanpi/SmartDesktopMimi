# AITVBox 出厂身份写入与涂鸦 License 量产标准

- 文档版本：4.2.0
- 协议版本：1
- 状态：出厂助手、云端和设备端联调后冻结
- 日期：2026-07-17
- 适用产品：AITVBox / AI 桌面摆件
- 100ask 产品标识：`AITVBOX`
- 当前涂鸦 PID：`alon7qgyjj8yus74`

云端研发的接口、数据库和验收实现细则见
[AITVBox 量产云实施规范](factory-cloud-implementation-spec.md)。本文负责冻结三方边界，
实施规范负责给出云端可直接开发的合同。

## 1. 方案结论

AITVBox 保留现有 `CPUID + device_sig → provision → device_secret` 设备认证协议，
不再建设“设备仅凭 ID 向云端领取 device_sig”的复杂 bootstrap 链路。

新量产方案固定为：

1. 工人使用全志官方 PhoenixSuit/LiveSuit 烧录统一固件镜像。
2. 固件烧录完成并启动后，AITVBox 出厂助手通过 ADB 自动读取设备 CPUID。
3. 出厂助手使用工位 mTLS 证书调用量产云 `prepare` 接口。
4. 量产云为 CPUID 幂等生成/查询 `device_sig`，并预分配或返回原涂鸦
   `UUID + AuthKey`；绑定关系先于设备写入提交。
5. 出厂助手把 `device_sig` 和涂鸦 License 分别原子写入持久化目录，
   读回校验内容哈希和 `0600` 权限。
6. 出厂助手只上报签名和 License 的 SHA-256 指纹，云端将状态推进为
   `DEVICE_STORED`。
7. 新机沿用现有 provision 接口获取 `device_secret`；返修则由 prepare 开启一次性
   provision 授权，设备自行获取轮换后的新 secret。涂鸦进程从本地文件动态加载
   原 UUID/AuthKey 并联网。出厂助手始终不接触 device_secret。
8. 出厂助手轮询云端 `credentialState`。云端只负责身份、持久化、100ask 上线和
   涂鸦凭据验证；出厂助手在本地完成硬件测试，二者都通过后才显示整机 `PASS`。

工人不接触 CPUID、签名、私钥、secret、License、ADB 命令或云端后台。工人只操作全志
烧录工具，并根据最终 `PASS/FAIL` 处理设备。

## 2. 凭证关系

```text
100ask ECDSA 私钥
        +
UTF-8 "100ask:<CPUID>"
        |
        v
device_sig（写入设备，可用公钥验证）
        |
        +-- 设备提交 CPUID + device_sig 到 provision
        v
device_secret（云端生成，设备后续云端鉴权）

CPUID + 量产批次
        |
        +-- 量产云预分配并永久记录
        v
Tuya UUID + AuthKey（出厂助手写入，一机一密）
```

必须严格区分：

- **私钥**：真正的最高机密，只能位于签名服务的 KMS/HSM 或受控签名机。
- **device_sig**：私钥对单台设备 CPUID 的签名结果，不是私钥，允许写入设备。
- **公钥**：部署在 provision 后端，用于验证 `device_sig`。
- **device_secret**：provision 验签成功后生成的一机一密，用于设备连接 100ask 云。
- **Tuya AuthKey**：涂鸦一机一密凭证，与 100ask 私钥和 device_secret 相互独立。

## 3. 为什么采用这套方案

### 3.1 适配既有烧录约束

AITVBox 的完整 eMMC 固件必须使用全志 PhoenixSuit/LiveSuit 写入。出厂助手不替代、不封装
和不修改全志底层烧录过程，只处理设备烧录成功并启动后的身份写入与测试。

当前出厂镜像为：

```text
a133_linux_b6_uart0.img
```

### 3.2 复用现有设备端

设备端 `service_cloud` 已支持：

- 从 Allwinner `sunxi_serial` 读取 CPUID；
- 从 `/etc/100ask/device_sig` 读取出厂签名；
- 调用 `POST /api/device/provision`；
- 获取并持久化 `device_secret`；
- 使用设备身份连接 100ask 云。
- 从 `/factory/tuya/license.env` 安全加载 UUID/AuthKey；文件在进程启动后
  写入时，涂鸦进程会轮询并动态继续初始化。

因此无需重新设计设备首次注册状态机，只需把现有命令行签名写入流程产品化为出厂助手。

### 3.3 不把私钥分发给工厂电脑

现有开发脚本可以在本地使用 PEM 私钥签名，但这种方式禁止用于正式量产。代工厂电脑、
普通工位或出厂助手安装包一旦包含私钥，任意一台电脑被复制就可能伪造全部设备。

正式量产必须是：

```text
出厂助手提交 CPUID → 签名服务/KMS 签名 → 返回 device_sig
```

签名服务可以部署在 100ask 云端，也可以部署在工厂 VPN 内的集中签名机；关键要求是私钥
永远不离开 KMS/HSM/集中签名机。设备不直接调用该签名服务。

## 4. 角色与职责

| 角色 | 必须负责 | 禁止行为 |
|---|---|---|
| 全志烧录工具 | 将统一 `.img` 写入 eMMC | 处理 device_sig、License 或云端注册 |
| 普通工人 | 接线、使用全志工具烧录、查看 PASS/FAIL | 操作 ADB、复制 ID、查看或输入密钥 |
| AITVBox 出厂助手 | 检测设备、读 CPUID、调用量产云、短暂接收并写入原 License、校验、查询验收 | 保存私钥、读取 device_secret、显示/打印/落盘 AuthKey |
| 签名服务 | 鉴权工位、校验请求、使用私钥签名、审计 | 向设备下发私钥、把私钥返回工位 |
| provision 后端 | 用公钥验签；新机生成 secret；仅在工位授权的返修窗口内轮换 secret | 仅凭 CPUID 在公网返回 secret、向出厂工具返回 secret |
| License 服务 | 导入库存、在 prepare 事务中永久绑定 CPUID、返修返回原 License | 重复分配或自动回收已分配 License |
| 设备固件 | 保存签名和 secret、动态加载本地 License、连接两个云 | 主动领取第二组 License、在日志打印 secret/AuthKey |
| 产线管理员 | 安装工位证书、发布固件、处理异常、审计 | 把工位证书或私钥交给普通工人 |

## 5. 设备标识与签名格式

### 5.1 CPUID / deviceId

- 来源：`/sys/class/sunxi_info/sys_info` 中的 `sunxi_serial`；
- 协议字段统一称为 `deviceId`，现有 provision 同时使用 `deviceId` 和 `chipid`；
- A133 当前格式为 32 位小写十六进制；
- 正则：`^[0-9a-f]{32}$`；
- 全零、缺失、格式异常或与已有 SN 冲突时必须终止量产。

### 5.2 签名原文

保持与现有 100ask SDK 完全一致：

```text
100ask:<deviceId>
```

约束：

- UTF-8 编码；
- 不包含引号；
- 不包含空格；
- 末尾没有 CR/LF；
- deviceId 必须为规范化的小写值。

### 5.3 算法与编码

- 算法：ECDSA P-256 + SHA-256；
- OpenSSL 等价操作：`openssl dgst -sha256 -sign`；
- 签名二进制格式：ASN.1 DER；
- 设备文件编码：DER 字节的小写十六进制，不带 `0x` 和换行；
- DER ECDSA 签名长度可变，禁止把 `device_sig` 长度硬编码为固定 142 字符；
- 后端必须先 hex 解码，再用 ECDSA 公钥验签。

### 5.4 密钥版本

签名服务为每个签名记录 `signingKeyId`。provision 后端在密钥轮换期间至少支持当前和上一把
有效公钥。设备当前只保存 `device_sig`，不参与公钥验证；`signingKeyId` 由签名服务和云端
数据库用于审计、轮换与吊销。

## 6. 总体量产流程

```text
工人/全志工具         出厂助手                 量产云                  设备/涂鸦
     |                    |                         |                         |
     |-- 烧录统一 img --->|                         |                         |
     |     设备启动       |                         |                         |
     |                    |-- ADB 读取 CPUID ------>|                         |
     |                    |-- mTLS prepare -------->|                         |
     |                    |                         |-- CPUID 幂等绑定 ----|
     |                    |<-- sig + 原 UUID/AuthKey --|                         |
     |                    |-- ADB 原子写入 ---------------------------->|
     |                    |-- 读回哈希校验 -------------------------->|
     |                    |-- 重启并再次读回 ------------------------>|
     |                    |-- confirm-write ------->|                         |
     |                    |                         |<-- provision/凭据验证 --|
     |                    |-- production-status -->|                         |
     |                    |<-- credential PASSED ---|                         |
     |                    |-- 本地硬件测试          |                         |
     |<-- 最终 PASS/FAIL --|                         |                         |
```

标准顺序：

1. 管理员在全志工具中确认正确的产品镜像及 SHA256。
2. 工人连接设备并使用 PhoenixSuit/LiveSuit 烧录。
3. 烧录成功后设备自动启动，ADB 重新枚举。
4. 出厂助手检测到唯一设备并读取 CPUID。
5. 出厂助手以稳定 `requestId` 调用量产云 `prepare`。
6. 量产云在单个事务中查询/生成签名，并为 CPUID 预绑定涂鸦 License。
7. 出厂助手在设备持久化分区写入并读回校验两组凭据。
8. 出厂助手重启设备，再次核对 CPUID、固件、PID、文件哈希、所有者和权限。
9. 出厂助手调用 `confirm-write`，只上传哈希和结果，不回传 AuthKey。
10. 设备联网调用 provision；新机创建 secret，返修在一次性授权窗口内轮换 secret。
11. 涂鸦进程动态加载已写入的原 UUID/AuthKey，完成凭据运行时验证。
12. 出厂助手轮询 `production-status`，取得 `credentialState=PASSED`。
13. 出厂助手本地执行硬件测试；云端凭据与本地测试都通过后才显示最终 `PASS`。

## 7. 出厂助手标准

### 7.1 产品形态

出厂助手是安装在量产电脑上的后台程序或极简桌面程序。它不能替代全志工具，也不能要求
普通工人运行脚本或输入命令。

推荐界面只有：

```text
等待设备
正在配置，请勿断电
PASS
FAIL：<中文原因和错误码>
```

如果每个工位一次只处理一台设备，助手自动开始，无需“开始”按钮。多工位并行时必须按
USB 物理端口或夹具槽位绑定设备，禁止仅依赖可能变化的 ADB 序列顺序。

### 7.2 工位初始化

由管理员一次性完成：

- 安装 ADB 驱动和出厂助手；
- 安装每工位唯一的 mTLS 客户端证书；
- 配置 `factoryId/lineId/stationId`；
- 配置当前批次 `TUYA_PID`、固件版本和整机镜像 SHA-256；
- 配置统一量产 API（prepare/confirm-write/production-status）域名；
- 安装签名公钥供助手本地二次验签；
- 配置工厂网络，但不把账号密码硬编码进通用固件；
- 验证工位证书只能调用授权产品和产线接口。

工位证书必须可单独吊销。普通工人账号不能导出证书私钥。

### 7.3 设备检测

出厂助手必须：

- 等待全志工具释放 USB，设备正常启动并枚举为 ADB；
- 执行 `adb root` 后重新等待设备在线；
- 确认只选择目标 ADB serial/USB 槽位；
- 读取 `sunxi_serial` 两次并确认一致；
- 读取产品型号和固件版本；
- 读取 `/etc/aitvbox-tuya-pid`，确认固件 PID、工位批次 PID 和云端 License 池一致；
- 校验固件版本在当前批次准入清单；
- 若设备已有身份，进入“已配置设备处理规则”，不得静默覆盖。

### 7.4 已配置设备处理规则

| 本地状态 | 行为 |
|---|---|
| 无 `device_sig`、无 License | `production` 模式调 prepare，云端创建唯一绑定 |
| 已有部分凭据且属于当前未完成 productionId | 原 requestId 断点续作，只允许重写同一组内容 |
| 云端已为 `CREDENTIAL_PASSED/ACTIVE` | `production` 必须拒绝；只能进入受控 `repair` 或复检流程 |
| 已有 secret/涂鸦 KV 且进入返修 | 先查询云端和本地指纹；License 保持原组，device_secret 仅按 repair grant 轮换 |
| 签名验签失败 | `FAIL_IDENTITY_CORRUPT`，进入返修 |
| 本地 CPUID 与云端/签名记录冲突 | `FAIL_DEVICE_ID_CONFLICT`，隔离设备 |
| 有部分 License 文件 | 从云端取回该 CPUID 原 assignment 重写，禁止分配新组 |

## 8. 工厂签名服务接口

本节定义量产云内部签名能力。新出厂助手对外只调用第 12 节的统一
`prepare`，由 prepare 在服务端调用本能力，避免工位分别调签名和 License
接口后出现半完成状态。若为兼容旧工具保留独立路由，也必须受工位
mTLS 和相同幂等约束保护。设备永远不直接调用本接口。

### 8.1 请求

```http
POST /api/v1/factory/device-signatures
Content-Type: application/json
```

```json
{
  "protocolVersion": 1,
  "requestId": "77cbca29-dccd-4455-89c2-09c56469537e",
  "productId": "AITVBOX",
  "deviceId": "200804243260720c0021c5215a818c5a",
  "serialNumber": "AITV-20260717-000001",
  "batchNumber": "B20260717A",
  "factoryId": "factory-01",
  "lineId": "line-02",
  "stationId": "station-03",
  "firmwareVersion": "1.0.0",
  "firmwareSha256": "64-lowercase-hex"
}
```

服务端必须校验：

- mTLS 工位证书有效且未吊销；
- 证书权限匹配 factory/line/station/product；
- deviceId 格式正确；
- 固件版本和摘要在量产准入清单；
- deviceId 没有绑定另一个 SN；
- 当前批次处于允许生产状态；
- 单工位请求速率和批次数量未超限。

### 8.2 响应

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "requestId": "77cbca29-dccd-4455-89c2-09c56469537e",
    "productId": "AITVBOX",
    "deviceId": "200804243260720c0021c5215a818c5a",
    "signaturePayload": "100ask:200804243260720c0021c5215a818c5a",
    "signatureAlgorithm": "ECDSA_P256_SHA256_DER_HEX",
    "deviceSig": "3045...lowercase-der-hex",
    "signingKeyId": "factory-ca-2026-01",
    "issuedAt": 1784279420
  }
}
```

### 8.3 幂等规则

- `(productId, deviceId, signingKeyId)` 只能有一个有效签名记录；
- 同一 deviceId 重试必须返回数据库中首次保存的同一个 `deviceSig`；
- 同一 `requestId` 重试必须返回完全相同的业务结果；
- ECDSA 实现即使每次随机产生不同 DER 签名，也禁止在 HTTP 重试时重新签名；
- 已量产通过的 deviceId 再次申请，只能返回原签名和 `alreadyProvisioned=true`，不得生成新身份；
- 密钥轮换或返修重新签名必须通过独立审批接口并保留旧记录。

### 8.4 私钥管理

- 私钥必须位于 KMS/HSM 或访问受限的集中签名机；
- 签名服务进程只获得“调用签名”的权限，不应导出私钥；
- 禁止将 PEM 私钥打进出厂助手、固件、Docker 镜像或代码仓库；
- 禁止把同一私钥复制到每个工位；
- 所有签名请求记录工位证书、deviceId、SN、批次、时间、来源 IP 和结果；
- 签名服务与 provision 后端使用相匹配的私钥/公钥版本。

当前仓库中的 `100ask_ecdsa.pem` 只能视为开发遗留和安全整改项，不得随正式量产包分发。
生产密钥迁移完成前不能开始正式批量生产。

## 9. 出厂助手写入标准

目标文件：

```text
/etc/100ask/device_sig
/factory/tuya/license.env
```

当前系统应将 `/etc/100ask` 和 `/factory/tuya` 持久化到 A/B OTA 共享的
`/overlay`。助手必须先确认两个 bind mount 已完成，不能只写入只读
rootfs 或临时目录。

写入顺序：

1. 校验响应 productId、deviceId 与本机完全一致；
2. 使用签名公钥对 `100ask:<deviceId>` 和 `deviceSig` 本地验签；
3. 写入同目录临时文件；
4. 设置权限 `0600`；
5. 对文件和目录执行 `fsync`；
6. 原子重命名为 `/etc/100ask/device_sig`；
7. 读回文件并逐字节比较；
8. 再执行一次公钥验签；
9. 通过 stdin/IPC 将 UUID/AuthKey 写入 `/factory/tuya/license.env` 的同目录临时文件；
10. 将 License 文件设为 `0600`，原子替换并读回 SHA-256；
11. 记录两组文件指纹，而不在普通日志打印完整签名或 AuthKey；
12. 调用 `confirm-write`。`service_cloud` 可热加载签名，涂鸦进程会轮询加载
    License，无需为凭据写入强制重启整台设备。

写入或读回失败时禁止调用 `confirm-write` 和显示 PASS；云端已预分配的
assignment 保持 `ASSIGNED`，下次只能重试原内容。

## 10. 设备 provision 标准

设备重启并联网后调用现有接口：

```http
POST https://www.100ask.net/api/device/provision
Content-Type: application/x-www-form-urlencoded
```

现有请求字段保持：

```text
deviceId=<cpuid>
name=ai-desktop
chipid=<cpuid>
signature=<device_sig>
deviceModel=AITVBox
firmwareVersion=<version>
```

后端处理必须满足：

1. 严格校验 `deviceId == chipid`；
2. 校验 CPUID 格式；
3. hex 解码 `signature` 并检查 DER 合法性；
4. 使用对应公钥验证 `ECDSA-SHA256("100ask:<chipid>")`；
5. 签名有效后创建或读取该 deviceId 的 `device_secret`；
6. 设备处于 `pending`、首次响应丢失时，同一 deviceId 重试必须返回同一 secret；
   设备已用该 secret 成功鉴权并进入 `active` 后，禁止仅凭 device_sig 再次取回 secret；
7. deviceId 已绑定不同产品或被吊销时拒绝；
8. secret 使用 KMS 加密保存，禁止记录明文；
9. 返回的 deviceId 必须与请求值相同；
10. 写入审计和量产进度记录。

设备成功后原子保存：

```text
/etc/100ask/secret
```

`device_secret` 才是设备运行期的共享秘密。`device_sig` 可以长期保留，但不能代替运行期
HMAC/MQTT 鉴权。

## 11. 现有 provision 实现必须整改的问题

当前 `service_cloud.c` 主流程可以复用，但量产前必须整改：

- 开启 libcurl 服务器证书链验证；
- 开启主机名验证；
- 安装正确的 CA bundle；
- 禁止打印 provision 原始响应；
- 禁止在任何日志打印 secret；
- provision 成功落盘必须原子写入并 `fsync`；
- MQTT 从明文 1883 迁移到 TLS；
- 设备已有 secret 时不得再次调用 provision 轮换；
- provision 失败采用非阻塞指数退避，不阻塞 LVGL/UI 主线程。

生产中禁止存在：

```c
CURLOPT_SSL_VERIFYPEER = 0
CURLOPT_SSL_VERIFYHOST = 0
```

## 12. 量产云预分配与出厂写入

### 12.1 原则

- 通用 `.img` 不包含固定涂鸦 UUID/AuthKey；
- 涂鸦 License 由涂鸦平台签发，量产云负责加密库存和永久绑定；
- 云端必须先建立 `CPUID -> assignmentId -> UUID/AuthKey` 记录，才允许工位写入；
- 出厂助手可为完成该次写入而在进程内存短暂接收 AuthKey，但禁止落盘、
  显示、剪贴板、命令行参数和日志输出；
- 同一 CPUID 在生产、断网重试和返修时永久关联同一组 License；
- 写入失败只能重试原 assignment，禁止换新 License。

涂鸦官方要求 UUID 全局唯一、一个 License 只用于一台设备：

- [涂鸦生产授权](https://developer.tuya.com/en/docs/iot-device-dev/robot_production_authorization?id=Kdi38zn82vom4)
- [涂鸦生产采购与交付方式](https://developer.tuya.com/en/docs/iot/production-procurement?id=Kaiuyarmqhdgg)

### 12.2 统一 prepare 接口

```http
POST /api/v1/factory/devices/prepare
Idempotency-Key: <stable-request-id>
```

该接口只对工位 mTLS 证书开放。请求必须包含：

```json
{
  "protocolVersion": 1,
  "productId": "AITVBOX",
  "deviceId": "200804243260720c0021c5215a818c5a",
  "tuyaPid": "alon7qgyjj8yus74",
  "firmwareVersion": "1.0.0",
  "firmwareSha256": "<64 hex>",
  "factoryId": "factory-01",
  "lineId": "line-01",
  "stationId": "station-01",
  "batchNumber": "20260717-A",
  "mode": "production",
  "requestId": "<stable idempotency key>"
}
```

`mode=repair` 时，若 CPUID 没有历史 assignment，必须拒绝，不得新建 License。
成功响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "productionId": "<production record id>",
    "deviceId": "200804243260720c0021c5215a818c5a",
    "deviceSig": "<DER hex>",
    "signingKeyId": "factory-ca-2026-01",
    "assignmentId": "<stable assignment id>",
    "tuyaPid": "alon7qgyjj8yus74",
    "provisionPolicy": "CREATE_IF_ABSENT",
    "uuid": "<20 chars>",
    "authKey": "<32 chars>",
    "existingAssignment": false
  }
}
```

响应必须设置 `Cache-Control: no-store`。云端在单个数据库事务中：

1. 锁定 CPUID 身份记录；
2. 查询已有签名和 License assignment；
3. 若为新生产设备，`SELECT ... FOR UPDATE SKIP LOCKED` 预占一组库存；
4. 写入 CPUID 唯一关联并提交；
5. 所有重复请求返回同一签名和同一 assignment。

`provisionPolicy` 只能为：

- 新生产首次或未完成工单：`CREATE_IF_ABSENT`；
- 同一新生产工单断点续作：`RESUME_CURRENT_JOB`；
- 受控返修：`REPAIR_ROTATE_ONCE`。

`REPAIR_ROTATE_ONCE` 表示量产云为该 CPUID 和当前 productionId 创建一个短期、
只允许一次 secret 轮换的 provision grant。它不把 device_secret 返回给出厂工具。设备重启后仍按
原 provision 请求提交 CPUID 和 device_sig；provision 后端验证签名及 grant 后生成
新的 device_secret、原子替换 MQTT 鉴权记录并将新 secret 只返回设备。grant 成功
确认、超时或工单隔离后失效。若设备收到响应后本地原子保存失败，在 grant 有效期内
重复 provision 必须返回同一个新 secret，不能再次轮换；设备使用新 secret 成功连接
100ask MQTT 后，云端再将 grant 标为 `CONFIRMED` 并停止返回。

### 12.3 设备写入与确认

出厂助手原子写入：

```text
/etc/100ask/device_sig
/factory/tuya/license.env
```

当前持久化映射：

```text
/etc/100ask   -> /overlay/100ask
/factory/tuya -> /overlay/factory/tuya
```

写入要求：同目录临时文件、`0600`、`fsync`、原子 rename、读回 SHA-256。
读回成功后调用：

```http
POST /api/v1/factory/devices/confirm-write
Idempotency-Key: <request-id>:confirm
```

只上传 `productionId/deviceId/assignmentId`、签名 SHA-256、License 文件 SHA-256、
UUID SHA-256、AuthKey SHA-256、固件版本、`storageState=stored` 和
`rebootVerified=true`。禁止回传明文 AuthKey。`rebootVerified` 只有在设备重启后
重新确认 CPUID、固件版本、两个文件哈希、`root:root` 和 `0600` 全部一致时才可为 true。

### 12.4 License 状态

```text
AVAILABLE -> ASSIGNED -> DEVICE_STORED -> TUYA_VERIFIED
                 |
                 v
             QUARANTINED -> REVOKED
```

已进入 `ASSIGNED` 的 License 禁止自动回到 `AVAILABLE`。设备异常只能隔离并人工处理。

## 13. 出厂助手如何判断最终 PASS

出厂助手只在 `prepare -> 写入` 的短会话中接收 AuthKey；状态查询接口绝不
返回 secret、UUID 或 AuthKey 明文。提供：

```http
GET /api/v1/factory/devices/<deviceId>/production-status
```

响应示例：

```json
{
  "code": 0,
  "data": {
    "deviceId": "200804243260720c0021c5215a818c5a",
    "signatureIssued": true,
    "signatureWritten": true,
    "provisioned": true,
    "deviceOnline": true,
    "tuyaLicenseAssigned": true,
    "tuyaLicenseStored": true,
    "tuyaCredentialVerified": true,
    "credentialState": "PASSED"
  }
}
```

云端 `credentialState=PASSED` 只要求：

- 固件版本和 SHA256 正确；
- CPUID 格式正确且无冲突；
- device_sig 本地公钥验签通过；
- 写入后重启仍可读取相同签名；
- provision 已成功且设备使用 secret 上线；
- 涂鸦 License 已分配并确认落盘；
- 云端记录的 License 指纹与工位读回指纹完全一致；
- 固件内置 PID 清单、工位配置和 assignment 的 PID 完全一致；
- 涂鸦运行时已真实接纳该 License；
- 云端状态与本地写入结果一致。

出厂工具收到云端凭据 PASS 后，再在工位本地执行屏幕、触摸、按键、音频、麦克风、
传感器、灯效等本机硬件测试。硬件测试结果不要求上传云端。只有
`credentialState=PASSED && localHardwareTestPassed=true` 时，出厂工具才能向工人显示
最终整机 `PASS`。

### 13.1 100ask 在线不等于客户绑定

`deviceOnline=true` 只表示设备使用 CPUID + device_secret 成功连接 100ask MQTT。
这是设备到自有云的机器鉴权，不是客户账号绑定，也不依赖用户进入设置页获取
bindToken。产线只需要提供临时工厂网络，测试完成后由出厂工具删除该网络配置。

### 13.2 涂鸦凭据验证信号

量产时不能要求“用户已绑定”，否则全新未售设备无法出厂；也不能只检查涂鸦
进程存活，因为错误 UUID/AuthKey 时进程同样可能存在。设备端仅在以下任一事件后
将状态判定为 ready：

1. 未绑定新机收到 `TUYA_EVENT_DIRECT_MQTT_CONNECTED`，并生成有效 `bind_url`；
2. 已绑定返修设备收到 `TUYA_EVENT_MQTT_CONNECTED`。

第一种事件发生在客户扫码之前，所以新机出厂不需要客户账号、不需要扫码，也不能用
“客户是否绑定”作为产测条件。

`lv_backend` 随后通过已使用 `device_secret` 认证的
`100ask/device/<deviceId>/status` topic 上报：

```json
{"tuya_license_ready":true,"source":"device_runtime"}
```

不上报 UUID、AuthKey 或绑定 URL。涂鸦子进程退出时必须立即清空旧 ready 状态，
防止重启后沿用上一次会话的结果。`prepare` 创建本次 `productionId` 时必须把
`tuyaCredentialVerified` 清为 `false`。量产云只接受 `received_at >= prepared_at`
的 ready 事件；该事件允许先于 `confirm-write` 到达并暂存，confirm 时不得再次清掉，
这样既不会复用历史 PASS，也不会因设备验证比工位确认快而丢失结果。

云端 MQTT 消费者还必须校验 topic 中的 deviceId 与已认证客户端身份一致，并把
status JSON 当作“局部字段更新”合并，不能因后续 heartbeat 未携带该字段就把验证
结果覆盖为空；只有当前工单的新 `false`、超时或人工隔离才能撤销本次验证结果。

最终 PASS 后，助手应清除临时工厂 Wi-Fi、关闭量产调试入口，并将设备重启到正常用户界面。

## 14. 出厂助手状态机和错误码

```text
WAIT_DEVICE
  -> READ_DEVICE_ID
  -> CHECK_FIRMWARE
  -> CLOUD_PREPARE
  -> VERIFY_SIGNATURE
  -> WRITE_SIGNATURE_AND_LICENSE
  -> READBACK_VERIFY
  -> CONFIRM_WRITE
  -> WAIT_DEVICE_PROVISION
  -> WAIT_CLOUD_CREDENTIAL_PASS
  -> LOCAL_HARDWARE_TEST
  -> PASS / FAIL
```

| 错误码 | 含义 | 工人提示 |
|---|---|---|
| `F01_USB_NOT_FOUND` | 设备未重新枚举 | 请重新插拔 USB |
| `F02_ADB_UNAVAILABLE` | ADB 未就绪 | 等待后重试，仍失败放异常区 |
| `F03_CPUID_INVALID` | CPUID 缺失或异常 | 放入异常区 |
| `F04_FIRMWARE_MISMATCH` | 固件版本/摘要错误 | 联系线长检查固件 |
| `F05_STATION_UNAUTHORIZED` | 工位证书无权限 | 联系管理员 |
| `F06_SIGNATURE_REQUEST_FAILED` | 签名服务失败 | 检查工厂网络后重试 |
| `F07_SIGNATURE_INVALID` | 本地验签失败 | 停线并通知管理员 |
| `F08_SIGNATURE_WRITE_FAILED` | 设备落盘失败 | 重试；仍失败放维修区 |
| `F09_PROVISION_FAILED` | 云端身份注册失败 | 自动重试；超时放异常区 |
| `F10_LICENSE_FAILED` | License 预分配/写入/确认失败 | 重试原 assignment，禁止换新 License |
| `F11_TUYA_CREDENTIAL_UNVERIFIED` | 涂鸦凭据未被运行时接纳 | 检查 License、网络或涂鸦云 |
| `F12_HARDWARE_TEST_FAILED` | 硬件测试失败 | 放维修区 |
| `F13_IDENTITY_CONFLICT` | ID/SN/云端记录冲突 | 隔离，禁止重刷覆盖 |

重试必须幂等。网络失败不能申请第二个身份、第二个 secret 或第二组 License。

## 15. 数据库建议

### 15.1 `factory_signature_records`

- `id` UUID
- `product_id/device_id`，唯一组合
- `device_sig`
- `device_sig_sha256`
- `signing_key_id`
- `signature_payload`
- `serial_number/batch_number`
- `factory_id/line_id/station_id`
- `firmware_version/firmware_sha256`
- `request_id`
- `issued_at`
- `status`

### 15.2 `device_identities`

- `product_id/device_id`，唯一
- `signature_record_id`
- `secret_ciphertext/secret_key_version`
- `secret_fingerprint`
- `provisioned_at/last_online_at`
- `state`

### 15.3 `tuya_license_inventory`

- `tuya_pid`
- `uuid_ciphertext/auth_key_ciphertext`
- `uuid_fingerprint/auth_key_fingerprint`
- `import_batch`
- `state`
- `assigned_device_id`
- `assigned_at`
- `assignment_id`，唯一
- `stored_at/verified_at`
- `license_file_sha256/uuid_sha256/auth_key_sha256`

UUID 和 `assigned_device_id` 均必须有唯一索引。

### 15.4 `production_records`

- `device_id/serial_number/batch_number`
- `station_id/operator_id`
- `firmware_version/firmware_sha256`
- `signature_written_at/provisioned_at`
- `license_stored_at/tuya_verified_at`
- `credential_state`
- `credential_passed_at/failure_code`

## 16. 返修和重复烧录

### 16.1 重刷统一固件

如果全志整机烧录会清除 `/overlay`，重刷后 device_sig、device_secret 和 License
都可能丢失。返修必须使用
`FACTORY_MODE=repair`（或 GUI 的受控返修模式）调用同一 `prepare` 接口：

- device_sig 可以由签名服务返回原记录并重新写入；
- prepare 为当前 productionId 创建短期、单次 `REPAIR_ROTATE_ONCE` grant；
- 出厂助手在 grant 创建成功后删除本机旧 `/etc/100ask/secret`，强制设备走返修 provision；
- 设备自行调用 provision 获取轮换后的新 device_secret，出厂工具和普通日志均不可见；
- 旧 device_secret 在轮换事务提交后失效，避免返修电脑接触或保存原 secret；
- License 服务必须解密并返回该 CPUID 原 assignment；
- 禁止因为重刷而消耗新 License。

`repair` 模式查不到历史 assignment 时必须返回冲突/需人工审核，不能从
`AVAILABLE` 库存取新凭据。若返修前还能读取 `/overlay/tuyadb`，应先加密备份并
在同一设备上恢复；不得把该 KV 复制到其他 CPUID。

返修授权只能针对已核验的原 CPUID 和当前 productionId，不能生成第二台逻辑设备；
grant 重试必须幂等，只能产生一次新 secret，并在设备成功使用它上线后关闭。

### 16.2 CPUID 冲突

两块板读取到相同 CPUID 或 CPUID 与不同 SN 绑定时，必须立即隔离，禁止出厂助手自动覆盖
云端记录。

### 16.3 私钥轮换

本节是延期安全设计，本轮不修改现有生产私钥：

1. provision 后端先安装新公钥并保留旧公钥；
2. 签名服务切换到新 `signingKeyId`；
3. 新设备使用新私钥签名；
4. 已出厂设备的旧签名继续有效；
5. 私钥泄漏时吊销对应 keyId，并制定存量设备迁移方案。

## 17. 网络和安全要求

以下为目标安全基线；本轮延期范围以第 19 节为准：

- 签名服务只允许工厂 VPN/专网访问；
- 工位使用每站唯一 mTLS 证书；
- 工位 `prepare/confirm-write/production-status` 必须使用 HTTPS + mTLS 并严格验证证书；
- 设备 provision 当前使用 HTTPS，必须校验证书链和主机名；
- 设备侧 100ask MQTT TLS 切换属于延期项；涂鸦连接继续使用 SDK 既有 TLS；
- 工位无权查询 device_secret；AuthKey 只能由 prepare 对当前单台 CPUID 短暂返回，
  禁止批量导出和历史列表查询；
- 普通日志不打印完整 device_sig、secret、AuthKey 或 License 响应；
- 管理员查看敏感字段必须二次授权并审计；
- 签名服务、provision、License 数据库使用独立权限；
- 测试和生产使用不同私钥、公钥、数据库和 License 池；
- 出厂助手安装包必须签名，并校验自动更新包签名。

## 18. 给工厂的量产套件

正式交付不应只有一个 `.img`，而应包括：

- 全志官方 PhoenixSuit/LiveSuit 及对应驱动；
- 固定版本 `a133_linux_b6_uart0.img`；
- 固件 manifest、版本号和 SHA256；
- AITVBox 出厂助手安装包；
- 工位证书安装工具（仅管理员使用）；
- USB/夹具连接说明；
- 一页工人 SOP；
- 一份线长/管理员异常处理手册；
- 测试环境和生产环境切换说明。

量产套件严禁包含：

- 100ask ECDSA 私钥；
- 固定 device_sig；
- device_secret；
- 涂鸦 UUID/AuthKey；
- 可导出的通用工位证书。

## 19. 实施顺序

### 阶段 A：量产云

- 实现统一 `prepare/confirm-write/production-status`；
- 在同一事务内完成签名查询、License 预分配和 CPUID 永久绑定；
- 实现 requestId 与 CPUID 双重幂等、批次准入和工位审计；
- 订阅 `100ask/device/<deviceId>/status`，按本次 `prepared_at` 关联
  `tuya_license_ready`，禁止复用历史 PASS；
- 实现 `repair` 只返回原 assignment，查无历史立即隔离。

### 阶段 B：出厂助手

- 以 `scripts/factory_provision_device.sh` 为协议参考实现 GUI；
- 校验 CPUID、固件版本、镜像 SHA-256、固件 PID 清单和云端 PID；
- 本地验签，使用 stdin/IPC 写两组凭据，不落工位明文临时文件；
- 重启后复核两组哈希、所有者和权限，再 confirm；
- 云端 `credentialState=PASSED` 后执行本地硬件测试，两者共同决定最终 PASS。

### 阶段 C：设备和固件

- 固件携带 `/etc/aitvbox-tuya-pid`，但不携带任何单机凭据；
- `service_cloud` 热加载 device_sig，涂鸦运行时动态加载 License；
- 设备上报 `tuya_license_ready`，涂鸦子进程退出时清除旧状态；
- `/etc/100ask` 和 `/factory/tuya` 必须映射到跨 OTA 持久化分区。

### 阶段 D：小批试产

- 先做正常生产、断网重试、重复扫码、重启、掉电和返修重刷；
- 验证任何失败都不消耗第二组 License；
- 对账设备数、assignment 数、PASS 数和隔离数后再扩大批量。

### 本轮明确延期

按当前项目决定，生产私钥迁移/轮换，以及设备侧 HTTPS/MQTT TLS 切换不在本轮
修改范围内。它们仍是后续上线前的安全项，但不阻塞本轮先完成身份、License、
返修和持久化闭环。

## 20. 量产验收清单

- [ ] 工人只使用全志工具烧录并查看 PASS/FAIL。
- [ ] 出厂助手不参与全志底层烧录。
- [ ] 通用固件中没有任何单机凭证。
- [ ] 出厂助手安装包和量产电脑中没有 ECDSA 私钥。
- [ ] 工位证书可按单个工位吊销。
- [ ] 同一 CPUID 重试返回同一签名记录。
- [ ] device_sig 写入后读回和公钥验签通过。
- [ ] 设备重启后签名仍存在。
- [ ] 设备重启后原涂鸦 License 的哈希、所有者和权限均保持一致。
- [ ] 固件 PID 清单、工位批次 PID 与云端 assignment PID 完全一致。
- [ ] provision 重试返回同一 device_secret。
- [ ] 返修 provision 只在工位授权的一次性 grant 内轮换 device_secret，且工具不可见。
- [ ] 非法 CPUID、错误签名和跨设备复制签名均被拒绝。
- [ ] libcurl TLS 证书链和主机名验证均已开启。
- [ ] MQTT 使用 TLS（延期安全项，不纳入本轮功能闭环）。
- [ ] 100 台并发不会重复分配涂鸦 License。
- [ ] 断电和断网重试不会多消耗 License。
- [ ] 全志重刷后的返修不会创建第二个逻辑设备或领取第二组 License。
- [ ] 日志不存在私钥、secret、AuthKey 和完整敏感响应。
- [ ] 出厂助手仅在单台写入会话中短暂接收 AuthKey，不显示、不记录、不落盘。
- [ ] `repair` 模式重刷后返回原 UUID/AuthKey，查无历史时不新分配。
- [ ] `tuya_license_ready` 仅使用本次 prepare 后的新事件，不复用历史状态。
- [ ] 云端、助手和设备对 PASS 条件理解一致。

## 21. 最终冻结边界

```text
全志工具：只负责烧统一固件
出厂助手：读 CPUID、调 prepare、写入 device_sig+原 License、confirm 和验收
量产云：保管签名私钥和加密 License 库，永久记录 CPUID 映射
设备：携带 CPUID + device_sig 调用现有 provision
provision：公钥验签后返回 device_secret
License 服务：prepare 时为 CPUID 幂等预分配，返修只返回原 License
工人：烧录、等待、看 PASS/FAIL
```

这套方案的核心不是取消 `device_sig`，而是把它从“人工烧录步骤”变成出厂助手的自动动作，
同时把真正危险的私钥集中保护起来。设备端认证协议保持简单，工人操作不增加，现有代码也
能最大程度复用。
