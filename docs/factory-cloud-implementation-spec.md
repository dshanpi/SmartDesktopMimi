# AITVBox 量产云实施规范

- 文档版本：1.1.0
- 协议版本：1
- 日期：2026-07-17
- 产品标识：`AITVBOX`
- 当前涂鸦 PID：`alon7qgyjj8yus74`
- 上位标准：[出厂身份写入与涂鸦 License 量产标准](tuya-license-cloud-provisioning-spec.md)

本文是交给云端研发团队的实现合同。未写“可选”的字段和规则均为必做项。

## 1. 云端最终要交付什么

云端需要交付五个能力：

1. 工位身份认证：使用每工位独立的 mTLS 客户端证书鉴权。
2. 统一量产 API：实现 `prepare`、`confirm-write`、`production-status`。
3. 签名与 License 事务：将 CPUID、device_sig、涂鸦 UUID/AuthKey 永久绑定。
4. provision 返修改造：支持工位授权的一次性 device_secret 轮换。
5. MQTT 状态消费与凭据聚合：接收设备上线和 `tuya_license_ready`，满足云端条件后
   返回 `credentialState=PASSED`。

硬件测试不属于云端强制职责，由出厂工具在工位本地执行。最终整机 PASS 由出厂工具
综合云端凭据 PASS 和本地硬件测试结果决定。

云端不得把 device_secret 返回给出厂工具。涂鸦 AuthKey 只允许在单台设备本次
`prepare -> 写入` 会话中返回一次，其他查询接口均不得返回明文。

## 2. 服务拆分与职责

可以部署成单体服务，也可以拆分，但逻辑职责必须存在：

| 模块 | 职责 |
|---|---|
| Factory API | mTLS 鉴权、参数校验、幂等、prepare/confirm/status |
| Signature Service | 对 `100ask:<CPUID>` 做 ECDSA P-256 SHA-256 签名 |
| License Service | 导入、加密保存、预分配和恢复原涂鸦 License |
| Provision Service | 新机生成 device_secret、返修按 grant 轮换 secret |
| MQTT Consumer | 消费设备 status、online、version 等消息 |
| Credential Aggregator | 汇总写入、100ask 上线和涂鸦凭据验证，推进凭据状态机 |

Factory API、License Service 和 Provision Service 必须共享数据库事务，或通过可靠
事务消息/outbox 保证最终一致，不能靠无补偿的多个 HTTP 调用拼接。

## 3. 身份和固定格式

### 3.1 deviceId

- 值等于 Allwinner CPUID；
- 32 位小写十六进制；
- 正则：`^[0-9a-f]{32}$`；
- 全零、格式异常、绑定到其他序列号或发生冲突时拒绝。

### 3.2 device_sig

- 签名原文：UTF-8 `100ask:<deviceId>`，无换行；
- 算法：ECDSA P-256 + SHA-256；
- 输出：ASN.1 DER 二进制的小写十六进制；
- 云端必须保存首次签名结果，HTTP 重试不得重新签名；
- provision 后端按相同原文、公钥和算法验签。

### 3.3 涂鸦 License

- PID：16 位字母或数字；
- UUID：20 个非空白可打印字符；
- AuthKey：32 个非空白可打印字符；
- 同一个 UUID 只能分配给一个 deviceId；
- 同一个 deviceId 在同一 PID 下只能关联一个 assignment；
- 进入 `ASSIGNED` 后不得自动退回 `AVAILABLE`。

设备文件的规范字节序列固定为：

```text
TUYA_OPENSDK_UUID=<20-char-uuid>
TUYA_OPENSDK_AUTHKEY=<32-char-auth-key>
```

`licenseFileSha256` 必须对以上 UTF-8 字节计算，包含最后一个 LF，不得使用 CRLF。

## 4. 数据库最低模型

字段名可按现有后端规范调整，但语义和唯一约束不能删除。

### 4.1 `factory_stations`

```text
id
factory_id
line_id
station_id
certificate_fingerprint UNIQUE
allowed_product_ids
state                 ACTIVE / REVOKED
created_at / revoked_at
```

服务端从客户端证书映射工位身份，不接受请求 JSON 自称的工位身份覆盖证书授权范围。

### 4.2 `device_identities`

```text
id
product_id
device_id
signature_record_id
device_secret_ciphertext
device_secret_fingerprint
secret_version
state                 PENDING / ACTIVE / QUARANTINED / REVOKED
provisioned_at
last_online_at
created_at / updated_at
UNIQUE(product_id, device_id)
```

### 4.3 `signature_records`

```text
id
product_id
device_id
signing_key_id
signature_payload
device_sig
device_sig_sha256
created_at
UNIQUE(product_id, device_id, signing_key_id)
```

### 4.4 `tuya_license_inventory`

```text
id
tuya_pid
uuid
auth_key_ciphertext
auth_key_fingerprint
state                 AVAILABLE / ASSIGNED / DEVICE_STORED / TUYA_VERIFIED / QUARANTINED / REVOKED
assignment_id
assigned_product_id
assigned_device_id
assigned_at / stored_at / verified_at
license_file_sha256
uuid_sha256
auth_key_sha256
UNIQUE(tuya_pid, uuid)
UNIQUE(tuya_pid, assigned_device_id) WHERE assigned_device_id IS NOT NULL
UNIQUE(assignment_id)
```

### 4.5 `production_records`

```text
id                         productionId
protocol_version
product_id
device_id
assignment_id
request_id
idempotency_key
mode                       production / repair
provision_policy
factory_id / line_id / station_id
batch_number
firmware_version / firmware_sha256
tuya_pid
prepared_at
signature_written_at
license_stored_at
write_confirmed_at
reboot_verified
device_provisioned_at
device_online_at
tuya_credential_verified_at
credential_state
failure_code / failure_detail
passed_at
UNIQUE(station_id, request_id)
UNIQUE(idempotency_key)
```

同一设备可以有多次返修工单，但同一时刻只能有一个未结束工单。建议增加：

```text
UNIQUE(product_id, device_id) WHERE credential_state IN
('PREPARED','ASSIGNED','DEVICE_STORED','VERIFYING')
```

### 4.6 `repair_provision_grants`

```text
id
production_id UNIQUE
product_id / device_id
state                 OPEN / ISSUED / CONFIRMED / EXPIRED / REVOKED
new_secret_ciphertext
new_secret_fingerprint
new_secret_version
expires_at
issued_at / confirmed_at
retry_count
```

grant 允许多次网络重试，但只能生成一次新 secret。

### 4.7 `factory_audit_logs`

至少记录：工位证书指纹、接口、productionId、deviceId、requestId、批次、结果、
错误码、来源 IP、时间。不得记录 AuthKey、device_secret 或完整 prepare 响应。

## 5. 通用 HTTP 约定

- Base URL 示例：`https://factory.example.com`；
- 工位接口必须使用 HTTPS + mTLS；
- 请求和响应使用 UTF-8 JSON；
- 成功 HTTP 状态为 200，业务 `code=0`；
- 参数错误使用 400，未认证 401，无权限 403，状态冲突 409；
- 所有返回凭据的响应设置 `Cache-Control: no-store, private`；
- 网关、APM、反向代理和应用日志禁止记录请求/响应 body；
- 每个写接口都要求 `Idempotency-Key`；
- 相同 key + 相同 body 返回完全相同结果；
- 相同 key + 不同 body 返回 409 `IDEMPOTENCY_CONFLICT`。

通用成功响应：

```json
{"code":0,"message":"ok","data":{}}
```

通用失败响应不得包含 secret：

```json
{
  "code": 40901,
  "error": "DEVICE_ALREADY_ACTIVE",
  "message": "设备已量产，必须使用 repair 模式",
  "requestId": "..."
}
```

## 6. `POST /api/v1/factory/devices/prepare`

### 6.1 请求

```http
POST /api/v1/factory/devices/prepare
Idempotency-Key: <stable-request-id>
Content-Type: application/json
```

```json
{
  "protocolVersion": 1,
  "productId": "AITVBOX",
  "deviceId": "200804243260720c0021c5215a818c5a",
  "tuyaPid": "alon7qgyjj8yus74",
  "firmwareVersion": "1.0.0",
  "firmwareSha256": "<64 lowercase hex>",
  "factoryId": "factory-01",
  "lineId": "line-01",
  "stationId": "station-01",
  "batchNumber": "20260717-A",
  "mode": "production",
  "requestId": "<stable id>"
}
```

### 6.2 服务端前置校验

必须依次校验：

1. mTLS 证书有效、未吊销并映射到 ACTIVE 工位；
2. 证书允许操作请求中的 factory/line/station/product；
3. 协议版本、CPUID、PID、版本和 SHA256 格式正确；
4. 固件版本、镜像 SHA256、PID 和批次处于准入清单；
5. 该 CPUID 未与其他产品/SN 冲突；
6. 工位速率、批次数量和 License 库存未超限；
7. `production` 不得处理已经 CREDENTIAL_PASSED/ACTIVE 的历史设备；
8. `repair` 必须存在历史 identity、签名和原 License assignment。

### 6.3 production 事务

推荐事务伪代码：

```text
BEGIN
  lock idempotency_key
  if exact completed result exists: return same result

  lock device identity by (productId, deviceId)
  if device already CREDENTIAL_PASSED/ACTIVE and not current unfinished job:
      ROLLBACK -> DEVICE_ALREADY_ACTIVE

  get or create signature_record
  if no License assignment:
      SELECT one AVAILABLE License for tuyaPid
        FOR UPDATE SKIP LOCKED
      mark License ASSIGNED permanently
      bind assignment to deviceId

  create production_record credential_state=ASSIGNED
  provisionPolicy = CREATE_IF_ABSENT
  commit outbox/audit event
COMMIT
```

当前未完成工单使用相同 requestId 重试时，返回原 productionId、deviceSig 和
assignment，`provisionPolicy=RESUME_CURRENT_JOB`，不得再取一组 License。

### 6.4 repair 事务

```text
BEGIN
  lock device identity and current License assignment
  require historical identity + signature + assignment
  reject if assignment missing, quarantined without approval, or PID changed
  create production_record mode=repair, state=ASSIGNED
  create repair grant state=OPEN, expires_at=now+10min
  provisionPolicy = REPAIR_ROTATE_ONCE
  return original deviceSig and original UUID/AuthKey
COMMIT
```

返修查无历史时返回 409 `REPAIR_HISTORY_NOT_FOUND`，绝对不能从 AVAILABLE 池取新 License。

### 6.5 成功响应

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "productionId": "prod-uuid",
    "deviceId": "200804243260720c0021c5215a818c5a",
    "deviceSig": "<lowercase DER hex>",
    "signingKeyId": "factory-ca-2026-01",
    "assignmentId": "assignment-uuid",
    "tuyaPid": "alon7qgyjj8yus74",
    "provisionPolicy": "CREATE_IF_ABSENT",
    "uuid": "<20 chars>",
    "authKey": "<32 chars>",
    "existingAssignment": false
  }
}
```

repair 响应的 `provisionPolicy` 必须为 `REPAIR_ROTATE_ONCE`，`existingAssignment=true`。

## 7. `POST /api/v1/factory/devices/confirm-write`

### 7.1 请求

```json
{
  "protocolVersion": 1,
  "productionId": "prod-uuid",
  "requestId": "<prepare request id>",
  "deviceId": "200804243260720c0021c5215a818c5a",
  "assignmentId": "assignment-uuid",
  "signatureSha256": "<64 hex>",
  "licenseFileSha256": "<64 hex>",
  "uuidSha256": "<64 hex>",
  "authKeySha256": "<64 hex>",
  "storageState": "stored",
  "firmwareVersion": "1.0.0",
  "rebootVerified": true
}
```

### 7.2 服务端校验

服务端必须自行从数据库原值计算并比较：

- `signatureSha256 = SHA256(deviceSig ASCII lowercase hex，无换行)`；
- `uuidSha256 = SHA256(UUID 原始 20 字节)`；
- `authKeySha256 = SHA256(AuthKey 原始 32 字节)`；
- `licenseFileSha256 = SHA256(第 3.3 节规范文件字节)`；
- productionId、deviceId、assignmentId、工位和当前未完成工单完全对应；
- `rebootVerified` 必须为 JSON boolean `true`；
- 固件版本与 prepare 一致。

任何一个不一致都进入 `FAILED` 或 `QUARANTINED`，不得设置 `DEVICE_STORED`。

成功事务：

```text
production_record.signature_written_at = now
production_record.license_stored_at = now
production_record.write_confirmed_at = now
production_record.reboot_verified = true
production_record.credential_state = DEVICE_STORED
license.state = DEVICE_STORED
```

confirm 重试必须幂等。该接口只接收哈希，不接收 UUID/AuthKey/device_secret 明文。

## 8. 现有 provision 接口需要怎么改

设备仍调用：

```http
POST /api/device/provision
Content-Type: application/x-www-form-urlencoded
```

保留现有字段：`deviceId/name/chipid/signature/deviceModel/firmwareVersion`。

### 8.1 新机

当 identity 为 PENDING 且没有 secret：

1. 校验 deviceId=chipid；
2. 验证 `100ask:<chipid>` 的 ECDSA 签名；
3. 生成高强度随机 device_secret；
4. 加密保存 secret，并保存不可逆 fingerprint；
5. 更新 MQTT 鉴权记录；
6. 只向设备返回 `deviceId + secret`；
7. production record 标记 provisioned。

同一未完成生产工单重试时必须返回同一个 secret，不能反复生成。

### 8.2 已激活设备，无 repair grant

不得返回原 secret，返回 403：

```json
{"code":40321,"error":"REPAIR_GRANT_REQUIRED"}
```

### 8.3 已激活设备，有 repair grant

事务规则：

```text
lock repair grant by deviceId
require state OPEN or ISSUED and not expired
verify device signature

if state == OPEN:
    generate new secret once
    atomically replace identity secret + MQTT auth
    store new secret encrypted in grant
    state = ISSUED

if state == ISSUED:
    return the same new secret

never generate a second secret for the same grant
```

设备使用新 secret 成功连接 100ask MQTT 后：

```text
grant.state = CONFIRMED
grant.confirmed_at = now
production_record.device_provisioned_at = now
production_record.device_online_at = now
```

CONFIRMED、EXPIRED 或 REVOKED grant 不再返回 secret。出厂工具在 repair prepare 成功后
会删除设备本地旧 `/etc/100ask/secret`，确保设备一定调用 provision。

## 9. MQTT 消费与工单关联

### 9.1 Topic

设备状态 topic：

```text
100ask/device/<deviceId>/status
```

Broker 必须用 ACL 将 MQTT username/deviceId 绑定到发布路径，例如只允许 username
`<deviceId>` 发布 `100ask/device/<deviceId>/#`。量产云消费者只接收通过该 ACL 的消息；
不能只相信 payload 自报的 ID。若需要确认返修新 secret 已真正登录，使用 Broker 的
连接事件/Webhook 或鉴权后端回调取得 username、clientId 和连接时间。

### 9.2 涂鸦凭据验证事件

设备上报：

```json
{"tuya_license_ready":true,"source":"device_runtime"}
```

服务端规则：

- status payload 是局部字段更新，必须 merge，不能整行覆盖；
- 只接受 `received_at >= production_record.prepared_at` 的事件；
- `true` 设置 `tuya_credential_verified_at`；
- 当前工单的新 `false`、设备进程退出、超时或隔离可撤销验证；
- 历史设备状态绝不能直接让新返修工单 PASS；
- 新机收到 `TUYA_EVENT_DIRECT_MQTT_CONNECTED` 并生成有效 bind_url 即属于凭据已接纳；
- 不要求客户绑定涂鸦账号，也不要求客户扫码；`TUYA_EVENT_MQTT_CONNECTED` 主要用于
  已绑定返修设备的验证路径。

### 9.3 在线事件

设备使用当前 device_secret 成功连接 100ask MQTT 时，更新：

```text
device_identity.last_online_at
production_record.device_online_at
```

这里的 online 是设备使用 device_secret 连接 100ask MQTT 的机器在线状态，不是客户账号
绑定状态，也不依赖用户申请 bindToken。产线需要提供临时工厂网络，最终 PASS 后由
出厂工具从设备删除该网络配置。

repair 工单只有使用 grant 新 secret 的连接才能确认 grant；旧会话或早于 prepared_at 的
last_seen 不能用于本次返修 PASS。

## 10. `GET /api/v1/factory/devices/<deviceId>/production-status`

只对授权工位 mTLS 开放，不返回任何明文凭据。

响应：

```json
{
  "code": 0,
  "data": {
    "productionId": "prod-uuid",
    "deviceId": "200804243260720c0021c5215a818c5a",
    "signatureIssued": true,
    "signatureWritten": true,
    "provisioned": true,
    "deviceOnline": true,
    "tuyaLicenseAssigned": true,
    "tuyaLicenseStored": true,
    "tuyaCredentialVerified": true,
    "rebootVerified": true,
    "credentialState": "PASSED",
    "failureCode": ""
  }
}
```

云端凭据状态机：

```text
PREPARED
  -> ASSIGNED
  -> DEVICE_STORED
  -> VERIFYING
  -> CREDENTIAL_PASSED

任意未通过条件 -> FAILED
身份、PID、签名或重复分配冲突 -> QUARANTINED
```

只有以下布尔条件全部为 true 才能把 `credentialState` 原子推进到 `PASSED`
（数据库内部可记录为 `CREDENTIAL_PASSED`）：

```text
signatureIssued
signatureWritten
provisioned
deviceOnline
tuyaLicenseAssigned
tuyaLicenseStored
tuyaCredentialVerified
rebootVerified
firmwareApproved
pidMatched
```

返修模式还必须满足 `repairGrantConfirmed=true`。凭据 PASSED 一旦写入必须记录时间、
工位、批次、固件和 assignment，不得由普通心跳自动改写为另一条工单。

## 11. 硬件测试边界

屏幕、触摸、按键、扬声器、麦克风、传感器、灯效等硬件测试由出厂工具在工位本地
执行，不要求逐项上传云端，也不参与 `credentialState` 聚合。

最终判定固定为：

```text
cloudCredentialPassed && localHardwareTestPassed -> 最终整机 PASS
其他情况                                      -> FAIL / 不允许出厂
```

如果未来为了售后追溯希望上传结果，只允许上传测试项名称、测量值和 PASS/FAIL 摘要，
并作为可选审计记录；上传失败不能改变已经得出的本地硬件结果，也不能让云端代替工位
执行测试。

## 12. License 导入和库存

导入涂鸦 License List 时必须：

1. 校验 PID、UUID/AuthKey 长度和字符；
2. 拒绝重复 UUID；
3. AuthKey 使用 KMS/应用层信封加密保存；
4. 保存导入批次、来源文件摘要、操作者和数量；
5. 原始导入文件进入受控归档或安全销毁，不留在普通应用服务器；
6. 提供库存统计，但普通后台不得导出 AuthKey 明文；
7. AVAILABLE 不足时 prepare 返回明确错误，不能跨 PID 借用库存。

并发分配必须使用数据库行锁或等价原子操作，推荐：

```sql
SELECT id
FROM tuya_license_inventory
WHERE tuya_pid = :pid AND state = 'AVAILABLE'
ORDER BY id
FOR UPDATE SKIP LOCKED
LIMIT 1;
```

## 13. 必须实现的错误码

| HTTP | error | 含义 |
|---:|---|---|
| 400 | `INVALID_REQUEST` | JSON、字段或格式错误 |
| 401 | `STATION_CERT_REQUIRED` | 未提供有效客户端证书 |
| 403 | `STATION_UNAUTHORIZED` | 工位无该产品/产线权限 |
| 409 | `IDEMPOTENCY_CONFLICT` | 同一幂等键对应不同请求 |
| 409 | `DEVICE_ALREADY_ACTIVE` | production 请求命中已量产设备 |
| 409 | `DEVICE_ID_CONFLICT` | CPUID 与产品/SN/记录冲突 |
| 409 | `PID_MISMATCH` | 固件、工位、License PID 不一致 |
| 409 | `REPAIR_HISTORY_NOT_FOUND` | 返修设备没有原身份或 assignment |
| 409 | `WRITE_HASH_MISMATCH` | confirm 哈希与云端原值不一致 |
| 403 | `REPAIR_GRANT_REQUIRED` | active 设备 provision 未获返修授权 |
| 410 | `REPAIR_GRANT_EXPIRED` | 返修 provision grant 已过期 |
| 423 | `DEVICE_QUARANTINED` | 设备已隔离，需要人工处理 |
| 429 | `STATION_RATE_LIMITED` | 工位超出速率/批次数量 |
| 503 | `LICENSE_EXHAUSTED` | 对应 PID 无可用 License |

出厂助手面向工人只显示简短中文处理建议；完整错误进入受控审计日志。

## 14. 定时任务和异常恢复

云端至少需要以下任务：

- 将到期 OPEN/ISSUED repair grant 标记 EXPIRED；
- 将长时间未 confirm 的 production record 标记超时，但保留 assignment；
- 检测同 CPUID 并发工单、重复 UUID 和异常状态组合；
- 对账 License `ASSIGNED/DEVICE_STORED/TUYA_VERIFIED` 与生产记录；
- 重试 outbox/MQTT 消费失败，保证事件不丢；
- 生成每日设备数、License 消耗、PASS、FAIL、返修和隔离报表。

超时、断网和应用崩溃都不能把 ASSIGNED License 自动放回 AVAILABLE。需要回收时必须人工
确认该 UUID 从未写入任何设备，并留下审计记录。

## 15. 安全边界

- 工位只持有本站 mTLS 私钥，不持有 ECDSA 签名私钥；
- ECDSA 私钥只在 KMS/HSM/受控签名服务；
- device_secret 只返回设备，不返回 Factory API；
- AuthKey 只在 prepare 单台短会话内返回，不出现在 status、confirm、列表和日志；
- 普通管理员只能看指纹和状态，明文解密需要独立审批和审计；
- 测试/生产使用独立证书、签名密钥、数据库和 License 池；
- prepare 响应设置 no-store，代理缓存、APM body capture 和错误采样必须关闭；
- 本轮设备侧 100ask MQTT TLS 切换和生产私钥迁移按主标准列为延期项，不得误报已完成。

## 16. 云端验收测试

上线前至少自动化覆盖：

1. 新 CPUID prepare 得到一组签名和一组 License；
2. 相同请求重试返回完全相同结果；
3. 相同幂等键不同 body 返回冲突；
4. 100 个并发请求不重复分配 UUID；
5. 已凭据 PASSED 的设备用 production 模式被拒绝；
6. repair 返回原 UUID/AuthKey，不消耗新 License；
7. repair 查无历史被拒绝；
8. 错误 PID 被拒绝；
9. confirm 任一哈希错误进入失败/隔离；
10. 未 confirm 或未重启验证不能 PASS；
11. active 设备无 grant provision 不返回 secret；
12. repair grant 第一次生成新 secret，重试返回同一个；
13. 新 secret MQTT 上线后 grant 关闭；
14. 历史 `tuya_license_ready=true` 不能让新返修工单凭据 PASS；
15. 当前工单的新 ready 事件能正确推进；
16. AuthKey/device_secret 不出现在应用日志、网关日志和 APM；
17. 工位证书吊销后立即失去 API 权限；
18. License 库存耗尽返回明确错误且不产生半完成记录；
19. 服务在 prepare 事务中途崩溃后无重复 assignment；
20. 断网重试、进程重启和重复 confirm 最终只产生一个凭据 PASSED 工单。

## 17. 云端团队交付清单

- [ ] 数据库迁移脚本及唯一索引；
- [ ] 工位证书签发、映射和吊销机制；
- [ ] prepare 接口和事务；
- [ ] confirm-write 接口和服务端哈希复算；
- [ ] production-status 接口和 credentialState 聚合器；
- [ ] provision 新机幂等和 repair grant 状态机；
- [ ] MQTT online/status 消费与当前工单关联；
- [ ] License 导入、加密库存和并发分配；
- [ ] 审计日志、错误码、限流和告警；
- [ ] 本文第 16 节自动化测试报告；
- [ ] 测试环境 Base URL、CA、工位证书和测试 License 池；
- [ ] 与 `scripts/factory_provision_device.sh` 完成 production/repair 联调。

完成以上清单后，云端才具备正式接入出厂助手的条件。
