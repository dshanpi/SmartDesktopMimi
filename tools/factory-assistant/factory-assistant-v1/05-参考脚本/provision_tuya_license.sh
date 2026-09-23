#!/usr/bin/env bash
# provision_tuya_license.sh — 涂鸦 UUID/AuthKey 一机一密烧录。
#
# 用法：
#   ./scripts/provision_tuya_license.sh <license.env> [adb_serial]
#   printf 'TUYA_OPENSDK_UUID=...\nTUYA_OPENSDK_AUTHKEY=...\n' | \
#       ./scripts/provision_tuya_license.sh - [adb_serial]
#   ADB_SERIAL=xxx ./scripts/provision_tuya_license.sh <license.env>
#
# license.env 格式（每台设备一份）：
#   TUYA_OPENSDK_UUID=20字符UUID
#   TUYA_OPENSDK_AUTHKEY=32字符AuthKey
#
# 凭据被原子写入 /factory/tuya/license.env。该目录在设备启动时 bind 到
# /overlay/factory/tuya，因此跨 A/B OTA 保留，也不属于用户恢复出厂数据。
set -euo pipefail
set +x
umask 077
ulimit -c 0 2>/dev/null || true

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

LICENSE_SOURCE="${1:-}"
ADB_SERIAL="${ADB_SERIAL:-${2:-}}"
REMOTE_DIR="/factory/tuya"
REMOTE_FILE="${REMOTE_DIR}/license.env"

if [[ -z "${LICENSE_SOURCE}" ]]; then
    echo "usage: $0 <license.env|-> [adb_serial]" >&2
    exit 2
fi

require_cmd adb "apt install adb"
require_cmd sha256sum "install coreutils"
if [[ "${LICENSE_SOURCE}" == "-" ]]; then
    LICENSE_CONTENT="$(cat)"
else
    require_file "${LICENSE_SOURCE}" "从涂鸦生产 License List 导出该设备的 UUID/AuthKey 文件"
    LICENSE_CONTENT="$(<"${LICENSE_SOURCE}")"
fi

adb_cmd() {
    if [[ -n "${ADB_SERIAL}" ]]; then
        adb -s "${ADB_SERIAL}" "$@"
    else
        adb "$@"
    fi
}

read_license_value() {
    local key="$1"
    printf '%s\n' "${LICENSE_CONTENT}" | awk -F= -v wanted="${key}" '
        /^[[:space:]]*#/ { next }
        {
            name=$1
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", name)
            if (name == wanted) {
                value=substr($0, index($0, "=") + 1)
                gsub(/^[[:space:]]+|[[:space:]\r]+$/, "", value)
                print value
            }
        }
    '
}

UUID="$(read_license_value TUYA_OPENSDK_UUID)"
AUTHKEY="$(read_license_value TUYA_OPENSDK_AUTHKEY)"

if [[ "${UUID}" == *$'\n'* || ${#UUID} -ne 20 ]]; then
    die "UUID 缺失、重复或长度错误：必须恰好 20 个字符"
fi
if [[ "${AUTHKEY}" == *$'\n'* || ${#AUTHKEY} -ne 32 ]]; then
    die "AuthKey 缺失、重复或长度错误：必须恰好 32 个字符"
fi
if [[ "${UUID}" =~ [[:space:]] || "${AUTHKEY}" =~ [[:space:]] ]]; then
    die "UUID/AuthKey 不能包含空白字符"
fi

# 不在工位文件系统创建明文临时文件。标准输入模式下，AuthKey 只经过
# 进程内存和 ADB stdin；文件模式仅保留给开发联调。
LOCAL_HASH="$(printf 'TUYA_OPENSDK_UUID=%s\nTUYA_OPENSDK_AUTHKEY=%s\n' \
    "${UUID}" "${AUTHKEY}" | sha256sum | awk '{print $1}')"

echo "==> adb root"
adb_cmd root >/dev/null 2>&1 || true
adb_cmd wait-for-device

echo "==> 等待 /factory/tuya 持久化挂载就绪"
PERSIST_READY=0
for _ in $(seq 1 30); do
    if adb_cmd shell "awk '\$5==\"/factory/tuya\" { found=1 } END { exit(found ? 0 : 1) }' /proc/self/mountinfo" \
        >/dev/null 2>&1; then
        PERSIST_READY=1
        break
    fi
    sleep 1
done
if [[ "${PERSIST_READY}" != "1" ]]; then
    die "/factory/tuya 尚未 bind 到持久化分区，拒绝写入，避免 License 在重启/OTA 后丢失"
fi

REMOTE_TMP="${REMOTE_DIR}/.license.env.tmp.$$"
echo "==> 写入该设备的涂鸦一机一密（UUID/AuthKey 不打印）"
adb_cmd shell "mkdir -p '${REMOTE_DIR}' && chmod 700 '${REMOTE_DIR}'"
if ! printf 'TUYA_OPENSDK_UUID=%s\nTUYA_OPENSDK_AUTHKEY=%s\n' "${UUID}" "${AUTHKEY}" | \
    adb_cmd shell "umask 077 && cat > '${REMOTE_TMP}' && chmod 600 '${REMOTE_TMP}' && mv '${REMOTE_TMP}' '${REMOTE_FILE}' && sync"; then
    adb_cmd shell "rm -f '${REMOTE_TMP}'" >/dev/null 2>&1 || true
    die "涂鸦 License 写入失败；设备端临时文件已清理"
fi

if ! REMOTE_HASH="$(adb_cmd exec-out cat "${REMOTE_FILE}" | sha256sum | awk '{print $1}')"; then
    die "涂鸦 License 写入后无法读回"
fi
if [[ "${REMOTE_HASH}" != "${LOCAL_HASH}" ]]; then
    die "涂鸦 License 写入后校验失败；该 License 不得标记为已消耗"
fi

REMOTE_MODE="$(adb_cmd shell stat -c '%a' "${REMOTE_FILE}" 2>/dev/null | tr -d '\r[:space:]')"
if [[ "${REMOTE_MODE}" != "600" ]]; then
    die "涂鸦 License 权限校验失败：期望 600，实际 ${REMOTE_MODE:-unknown}"
fi
REMOTE_OWNER="$(adb_cmd shell stat -c '%u:%g' "${REMOTE_FILE}" 2>/dev/null | tr -d '\r[:space:]')"
if [[ "${REMOTE_OWNER}" != "0:0" ]]; then
    die "涂鸦 License 所有者校验失败：期望 0:0，实际 ${REMOTE_OWNER:-unknown}"
fi

# 只重启涂鸦子进程；lv_backend 会自动拉起并动态加载新凭据。
adb_cmd shell killall your_chat_bot_QIO_1.0.1.bin >/dev/null 2>&1 || true

echo "    写入成功，SHA-256 校验通过，所有者 root:root，权限为 0600"
echo "==> 完成：涂鸦运行时将从 ${REMOTE_FILE} 动态加载凭据"
echo "    正式量产仍须由统一出厂助手重启读回、confirm-write，并等待云端 PASS。"

unset AUTHKEY UUID LICENSE_CONTENT
