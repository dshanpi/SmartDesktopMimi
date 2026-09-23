#!/usr/bin/env bash
# factory_provision_device.sh — AITVBox 统一出厂助手的 CLI 参考实现。
#
# 正式 GUI 助手应复用同一协议和状态机。本脚本不包含签名私钥，也不从
# 本地 License 清单分配密钥；它使用工位 mTLS 证书调用量产云。
#
# 必填环境变量：
#   FACTORY_API_BASE       例如 https://factory.example.com
#   FACTORY_CLIENT_CERT    工位 mTLS 客户端证书
#   FACTORY_CLIENT_KEY     工位 mTLS 客户端私钥
#   FACTORY_CA_FILE        量产云 CA
#   FACTORY_SIGNING_PUBKEY device_sig 本地二次验签公钥
#   FACTORY_ID / LINE_ID / STATION_ID / BATCH_NUMBER
#   TUYA_PID               本批固件实际编译使用的涂鸦产品 PID
#   FIRMWARE_SHA256        本批次全志 img 的 SHA-256
#
# 可选环境变量：
#   FACTORY_HARDWARE_TEST_BIN
#       本机硬件测试程序。配置后以退出码 0/非 0 判定最终整机 PASS/FAIL；
#       未配置时本脚本只输出 CREDENTIAL PASS，不能作为整机最终 PASS。
#
# 用法：
#   ./scripts/factory_provision_device.sh [adb_serial]
#   FACTORY_MODE=repair ./scripts/factory_provision_device.sh [adb_serial]
set -euo pipefail
set +x
umask 077
ulimit -c 0 2>/dev/null || true

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

require_cmd adb "apt install adb"
require_cmd curl "apt install curl"
require_cmd python3 "apt install python3"
require_cmd sha256sum "install coreutils"
require_cmd openssl "apt install openssl"
require_cmd xxd "apt install xxd"

required_env=(
    FACTORY_API_BASE FACTORY_CLIENT_CERT FACTORY_CLIENT_KEY FACTORY_CA_FILE FACTORY_SIGNING_PUBKEY
    FACTORY_ID LINE_ID STATION_ID BATCH_NUMBER FIRMWARE_SHA256 TUYA_PID
)
for name in "${required_env[@]}"; do
    [[ -n "${!name:-}" ]] || die "missing environment variable: ${name}"
done
require_file "${FACTORY_CLIENT_CERT}" "由产线管理员安装工位证书"
require_file "${FACTORY_CLIENT_KEY}" "由产线管理员安装工位私钥"
require_file "${FACTORY_CA_FILE}" "安装量产云 CA"
require_file "${FACTORY_SIGNING_PUBKEY}" "安装 device_sig 验签公钥"

ADB_SERIAL="${ADB_SERIAL:-${1:-}}"
FACTORY_MODE="${FACTORY_MODE:-production}"
PASS_TIMEOUT_SEC="${PASS_TIMEOUT_SEC:-240}"
FACTORY_HARDWARE_TEST_BIN="${FACTORY_HARDWARE_TEST_BIN:-}"
FACTORY_API_BASE="${FACTORY_API_BASE%/}"

[[ "${FACTORY_MODE}" == "production" || "${FACTORY_MODE}" == "repair" ]] || \
    die "FACTORY_MODE 只能是 production 或 repair"
[[ "${FACTORY_API_BASE}" == https://* ]] || die "FACTORY_API_BASE 必须使用 HTTPS"
[[ "${FIRMWARE_SHA256}" =~ ^[0-9a-fA-F]{64}$ ]] || \
    die "FIRMWARE_SHA256 必须是 64 位十六进制"
[[ "${TUYA_PID}" =~ ^[A-Za-z0-9]{16}$ ]] || die "TUYA_PID 必须是 16 位字母或数字"
[[ "${PASS_TIMEOUT_SEC}" =~ ^[0-9]+$ && "${PASS_TIMEOUT_SEC}" -ge 30 ]] || \
    die "PASS_TIMEOUT_SEC 必须是不小于 30 的整数"
if [[ -n "${FACTORY_HARDWARE_TEST_BIN}" ]]; then
    [[ -x "${FACTORY_HARDWARE_TEST_BIN}" ]] || \
        die "FACTORY_HARDWARE_TEST_BIN 不存在或不可执行"
fi

adb_cmd() {
    if [[ -n "${ADB_SERIAL}" ]]; then
        adb -s "${ADB_SERIAL}" "$@"
    else
        adb "$@"
    fi
}

factory_curl() {
    curl --silent --show-error --fail \
        --cacert "${FACTORY_CA_FILE}" \
        --cert "${FACTORY_CLIENT_CERT}" \
        --key "${FACTORY_CLIENT_KEY}" \
        -H 'Accept: application/json' \
        "$@"
}

json_payload() {
    python3 -c '
import json, sys
keys = sys.argv[1::2]
values = sys.argv[2::2]
obj = dict(zip(keys, values))
if "protocolVersion" in obj:
    obj["protocolVersion"] = int(obj["protocolVersion"])
for key in ("rebootVerified",):
    if key in obj:
        obj[key] = obj[key].lower() == "true"
print(json.dumps(obj, separators=(",", ":")))
' "$@"
}

json_require_ok() {
    python3 -c '
import json, sys
obj = json.load(sys.stdin)
if obj.get("code") != 0:
    raise SystemExit(1)
'
}

read_cpuid() {
    local line value
    line="$(adb_cmd shell cat /sys/class/sunxi_info/sys_info 2>/dev/null | \
        grep -i 'sunxi_serial' | head -1)"
    value="${line#*:}"
    printf '%s' "${value}" | tr -d '[:space:]' | tr 'A-F' 'a-f'
}

read_firmware_version() {
    adb_cmd shell sed -n 's/^AITVBOX_VERSION=//p' /etc/aitvbox-version 2>/dev/null | \
        head -1 | tr -d '\r\n'
}

read_firmware_tuya_pid() {
    adb_cmd shell sed -n 's/^TUYA_PID=//p' /etc/aitvbox-tuya-pid 2>/dev/null | \
        head -1 | tr -d '\r\n'
}

wait_mountpoint() {
    local mountpoint="$1"
    for _ in $(seq 1 30); do
        if adb_cmd shell "awk '\$5==\"${mountpoint}\" { found=1 } END { exit(found ? 0 : 1) }' /proc/self/mountinfo" \
            >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

echo "==> 等待设备并读取稳定 CPUID"
adb_cmd root >/dev/null 2>&1 || true
adb_cmd wait-for-device
CPUID_FIRST="$(read_cpuid)"
sleep 1
CPUID_SECOND="$(read_cpuid)"
[[ "${CPUID_FIRST}" =~ ^[0-9a-f]{32}$ && "${CPUID_FIRST}" == "${CPUID_SECOND}" ]] || \
    die "CPUID 缺失、格式错误或两次读值不一致"
CPUID="${CPUID_FIRST}"
FIRMWARE_VERSION="$(read_firmware_version)"
[[ -n "${FIRMWARE_VERSION}" ]] || die "无法读取设备固件版本"
DEVICE_TUYA_PID="$(read_firmware_tuya_pid)"
[[ "${DEVICE_TUYA_PID}" == "${TUYA_PID}" ]] || \
    die "工位 TUYA_PID 与本机固件清单不一致；禁止从错误 License 池分配"

# 同一批次+设备使用稳定幂等键，工位断网后重试不会消耗第二组 License。
REQUEST_ID="$(printf '%s' "${FACTORY_ID}:${LINE_ID}:${STATION_ID}:${BATCH_NUMBER}:${CPUID}" | \
    sha256sum | awk '{print $1}')"

PREPARE_BODY="$(json_payload \
    protocolVersion 1 productId AITVBOX deviceId "${CPUID}" tuyaPid "${TUYA_PID}" \
    firmwareVersion "${FIRMWARE_VERSION}" firmwareSha256 "${FIRMWARE_SHA256,,}" \
    factoryId "${FACTORY_ID}" lineId "${LINE_ID}" stationId "${STATION_ID}" \
    batchNumber "${BATCH_NUMBER}" mode "${FACTORY_MODE}" requestId "${REQUEST_ID}")"

echo "==> 云端预绑定设备签名和涂鸦 License"
PREPARE_RESPONSE="$(factory_curl \
    -H 'Content-Type: application/json' \
    -H "Idempotency-Key: ${REQUEST_ID}" \
    --data-binary "${PREPARE_BODY}" \
    "${FACTORY_API_BASE}/api/v1/factory/devices/prepare")" || \
    die "量产云 prepare 失败；禁止继续写入或显示 PASS"

mapfile -t PREPARED < <(printf '%s' "${PREPARE_RESPONSE}" | python3 -c '
import json, re, sys
obj = json.load(sys.stdin)
if obj.get("code") != 0 or not isinstance(obj.get("data"), dict):
    raise SystemExit(1)
d = obj["data"]
fields = ["productionId", "deviceId", "deviceSig", "assignmentId", "tuyaPid",
          "provisionPolicy", "uuid", "authKey"]
for key in fields:
    value = d.get(key)
    if not isinstance(value, str) or not value or "\n" in value or "\r" in value:
        raise SystemExit(1)
    print(value)
')
unset PREPARE_RESPONSE
[[ ${#PREPARED[@]} -eq 8 ]] || die "量产云 prepare 响应缺少必需字段"

PRODUCTION_ID="${PREPARED[0]}"
RETURNED_DEVICE_ID="${PREPARED[1]}"
DEVICE_SIG="${PREPARED[2],,}"
ASSIGNMENT_ID="${PREPARED[3]}"
RETURNED_TUYA_PID="${PREPARED[4]}"
PROVISION_POLICY="${PREPARED[5]}"
TUYA_UUID="${PREPARED[6]}"
TUYA_AUTHKEY="${PREPARED[7]}"
unset PREPARED

[[ "${RETURNED_DEVICE_ID}" == "${CPUID}" ]] || die "云端返回 deviceId 与本机 CPUID 不一致"
[[ "${RETURNED_TUYA_PID}" == "${TUYA_PID}" ]] || die "云端返回了错误的涂鸦 PID"
if [[ "${FACTORY_MODE}" == "production" ]]; then
    [[ "${PROVISION_POLICY}" == "CREATE_IF_ABSENT" || \
       "${PROVISION_POLICY}" == "RESUME_CURRENT_JOB" ]] || \
        die "量产云返回了不适用于新生产的 provisionPolicy"
else
    [[ "${PROVISION_POLICY}" == "REPAIR_ROTATE_ONCE" ]] || \
        die "返修必须由量产云开启一次性 device_secret 轮换窗口"
fi
[[ "${DEVICE_SIG}" =~ ^[0-9a-f]{128,160}$ && $(( ${#DEVICE_SIG} % 2 )) -eq 0 ]] || \
    die "云端返回的 device_sig 格式错误"
[[ ${#TUYA_UUID} -eq 20 && "${TUYA_UUID}" != *[[:space:]]* ]] || \
    die "云端返回的涂鸦 UUID 格式错误"
[[ ${#TUYA_AUTHKEY} -eq 32 && "${TUYA_AUTHKEY}" != *[[:space:]]* ]] || \
    die "云端返回的涂鸦 AuthKey 格式错误"

openssl dgst -sha256 -verify "${FACTORY_SIGNING_PUBKEY}" \
    -signature <(printf '%s' "${DEVICE_SIG}" | xxd -r -p) \
    <(printf '100ask:%s' "${CPUID}") >/dev/null 2>&1 || \
    die "device_sig 本地公钥验签失败；禁止写入"

echo "==> 原子写入 device_sig（密钥内容不打印）"
wait_mountpoint /etc/100ask || die "/etc/100ask 持久化挂载未就绪"
if [[ "${FACTORY_MODE}" == "repair" ]]; then
    echo "==> 返修模式：清除本机旧 device_secret，强制走受控轮换"
    adb_cmd shell "rm -f /etc/100ask/secret && sync && test ! -e /etc/100ask/secret" || \
        die "无法清除旧 device_secret；禁止继续返修"
fi
SIG_TMP="/etc/100ask/.device_sig.factory.$$"
if ! printf '%s' "${DEVICE_SIG}" | adb_cmd shell \
    "umask 077 && cat > '${SIG_TMP}' && chmod 600 '${SIG_TMP}' && mv '${SIG_TMP}' /etc/100ask/device_sig && sync"; then
    adb_cmd shell "rm -f '${SIG_TMP}'" >/dev/null 2>&1 || true
    die "device_sig 写入失败；设备端临时文件已清理"
fi
SIG_HASH="$(printf '%s' "${DEVICE_SIG}" | sha256sum | awk '{print $1}')"
if ! REMOTE_SIG_HASH="$(adb_cmd exec-out cat /etc/100ask/device_sig | sha256sum | awk '{print $1}')"; then
    die "device_sig 写入后无法读回"
fi
REMOTE_SIG_MODE="$(adb_cmd shell stat -c '%a' /etc/100ask/device_sig 2>/dev/null | tr -d '\r[:space:]')"
REMOTE_SIG_OWNER="$(adb_cmd shell stat -c '%u:%g' /etc/100ask/device_sig 2>/dev/null | tr -d '\r[:space:]')"
[[ "${REMOTE_SIG_HASH}" == "${SIG_HASH}" && "${REMOTE_SIG_MODE}" == "600" && \
   "${REMOTE_SIG_OWNER}" == "0:0" ]] || die "device_sig 读回、所有者或权限校验失败"

echo "==> 原子写入云端已绑定的原涂鸦 License"
printf 'TUYA_OPENSDK_UUID=%s\nTUYA_OPENSDK_AUTHKEY=%s\n' "${TUYA_UUID}" "${TUYA_AUTHKEY}" | \
    ADB_SERIAL="${ADB_SERIAL}" "${SELF_DIR}/provision_tuya_license.sh" -

LICENSE_HASH="$(printf 'TUYA_OPENSDK_UUID=%s\nTUYA_OPENSDK_AUTHKEY=%s\n' \
    "${TUYA_UUID}" "${TUYA_AUTHKEY}" | sha256sum | awk '{print $1}')"
UUID_HASH="$(printf '%s' "${TUYA_UUID}" | sha256sum | awk '{print $1}')"
AUTHKEY_HASH="$(printf '%s' "${TUYA_AUTHKEY}" | sha256sum | awk '{print $1}')"
unset TUYA_AUTHKEY TUYA_UUID DEVICE_SIG

echo "==> 重启并验证两组凭据确实持久化"
adb_cmd reboot
adb_cmd wait-for-device
adb_cmd root >/dev/null 2>&1 || true
adb_cmd wait-for-device
wait_mountpoint /etc/100ask || die "重启后 /etc/100ask 持久化挂载未就绪"
wait_mountpoint /factory/tuya || die "重启后 /factory/tuya 持久化挂载未就绪"

REBOOT_CPUID="$(read_cpuid)"
REBOOT_FIRMWARE_VERSION="$(read_firmware_version)"
REBOOT_TUYA_PID="$(read_firmware_tuya_pid)"
[[ "${REBOOT_CPUID}" == "${CPUID}" ]] || die "重启后 CPUID 发生变化，禁止确认写入"
[[ "${REBOOT_FIRMWARE_VERSION}" == "${FIRMWARE_VERSION}" ]] || \
    die "重启后固件版本不一致，禁止确认写入"
[[ "${REBOOT_TUYA_PID}" == "${TUYA_PID}" ]] || \
    die "重启后固件 PID 清单不一致，禁止确认写入"

if ! REBOOT_SIG_HASH="$(adb_cmd exec-out cat /etc/100ask/device_sig | sha256sum | awk '{print $1}')"; then
    die "重启后无法读取 device_sig；禁止确认写入"
fi
if ! REBOOT_LICENSE_HASH="$(adb_cmd exec-out cat /factory/tuya/license.env | sha256sum | awk '{print $1}')"; then
    die "重启后无法读取涂鸦 License；禁止确认写入"
fi
REBOOT_SIG_MODE="$(adb_cmd shell stat -c '%a' /etc/100ask/device_sig 2>/dev/null | tr -d '\r[:space:]')"
REBOOT_SIG_OWNER="$(adb_cmd shell stat -c '%u:%g' /etc/100ask/device_sig 2>/dev/null | tr -d '\r[:space:]')"
REBOOT_LICENSE_MODE="$(adb_cmd shell stat -c '%a' /factory/tuya/license.env 2>/dev/null | tr -d '\r[:space:]')"
REBOOT_LICENSE_OWNER="$(adb_cmd shell stat -c '%u:%g' /factory/tuya/license.env 2>/dev/null | tr -d '\r[:space:]')"
[[ "${REBOOT_SIG_HASH}" == "${SIG_HASH}" && "${REBOOT_SIG_MODE}" == "600" && \
   "${REBOOT_SIG_OWNER}" == "0:0" ]] || die "device_sig 重启后持久化校验失败"
[[ "${REBOOT_LICENSE_HASH}" == "${LICENSE_HASH}" && "${REBOOT_LICENSE_MODE}" == "600" && \
   "${REBOOT_LICENSE_OWNER}" == "0:0" ]] || die "涂鸦 License 重启后持久化校验失败"

CONFIRM_BODY="$(json_payload \
    protocolVersion 1 productionId "${PRODUCTION_ID}" requestId "${REQUEST_ID}" \
    deviceId "${CPUID}" assignmentId "${ASSIGNMENT_ID}" \
    signatureSha256 "${SIG_HASH}" licenseFileSha256 "${LICENSE_HASH}" \
    uuidSha256 "${UUID_HASH}" authKeySha256 "${AUTHKEY_HASH}" \
    storageState stored firmwareVersion "${FIRMWARE_VERSION}" rebootVerified true)"

echo "==> 向云端确认设备读回校验结果"
CONFIRM_RESPONSE="$(factory_curl \
    -H 'Content-Type: application/json' \
    -H "Idempotency-Key: ${REQUEST_ID}:confirm" \
    --data-binary "${CONFIRM_BODY}" \
    "${FACTORY_API_BASE}/api/v1/factory/devices/confirm-write")" || \
    die "写入已成功，但云端确认失败；保持 ASSIGNED 并使用同一 CPUID 重试，禁止换 License"
printf '%s' "${CONFIRM_RESPONSE}" | json_require_ok || \
    die "云端拒绝写入确认；禁止显示 PASS"
unset CONFIRM_RESPONSE CONFIRM_BODY PREPARE_BODY

echo "==> 等待云端完成 provision、100ask 上线和涂鸦凭据验证"
deadline=$((SECONDS + PASS_TIMEOUT_SEC))
while (( SECONDS < deadline )); do
    STATUS_RESPONSE="$(factory_curl \
        "${FACTORY_API_BASE}/api/v1/factory/devices/${CPUID}/production-status")" || true
    CREDENTIAL_STATE="$(printf '%s' "${STATUS_RESPONSE:-}" | python3 -c '
import json, sys
try:
    obj = json.load(sys.stdin)
    print(obj.get("data", {}).get("credentialState", ""))
except Exception:
    print("")
')"
    unset STATUS_RESPONSE
    if [[ "${CREDENTIAL_STATE}" == "PASSED" ]]; then
        echo "CREDENTIAL PASS: 设备 ${CPUID} 的身份、持久化和两条云凭据已验证"
        if [[ -z "${FACTORY_HARDWARE_TEST_BIN}" ]]; then
            echo "    当前 CLI 未配置本地硬件测试，因此不得把本结果当作整机最终 PASS。"
            exit 0
        fi

        echo "==> 运行出厂工具本地硬件测试"
        if "${FACTORY_HARDWARE_TEST_BIN}" \
            --adb-serial "${ADB_SERIAL}" \
            --device-id "${CPUID}" \
            --production-id "${PRODUCTION_ID}"; then
            echo "PASS: 云端凭据验证和本地硬件测试均通过"
            exit 0
        fi
        die "本地硬件测试失败；云端凭据状态保持 PASSED，但整机不得出厂"
    fi
    if [[ "${CREDENTIAL_STATE}" == "FAILED" || "${CREDENTIAL_STATE}" == "QUARANTINED" ]]; then
        die "云端凭据状态为 ${CREDENTIAL_STATE}"
    fi
    sleep 3
done

die "等待云端凭据 PASS 超时（${PASS_TIMEOUT_SEC}s）" \
    "设备已保留原 assignment，修复网络/云端后使用同一 CPUID 重试，禁止分配新 License"
