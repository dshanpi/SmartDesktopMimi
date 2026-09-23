#!/usr/bin/env bash
# provision_device.sh — 100ask 设备签名写入开发工具（禁止直接用于正式量产）。
#
# 照 100ask example 流程：读设备 cpuid → 用 100ask 私钥签 cpuid → 烧进设备
# /etc/100ask/device_sig。设备首次开机凭 (cpuid, device_sig) provision 注册拿 secret。
#
# 用法：
#   ./scripts/provision_device.sh                       # 自动 adb 读 cpuid + 烧录
#   ./scripts/provision_device.sh <cpuid>               # 手动指定 cpuid + 烧录
#   ./scripts/provision_device.sh <cpuid> <adb_serial>  # 指定 cpuid + 多设备时选 adb 序列号
#   ADB_SERIAL=xxx ./scripts/provision_device.sh        # 用环境变量指定 adb 设备
#
# 安全说明：本脚本当前依赖本地 PEM 私钥，只允许开发联调。正式量产必须由出厂助手
# 调用受控签名服务取得 device_sig，量产电脑和量产包中禁止保存私钥。
# 开发私钥与签名工具由 AITVBOX_100ASK_SDK_ROOT 指向的私有 SDK 提供（勿外泄）。
#
# 前置：本机有 adb + openssl；设备已通过 adb 连接且 adb root 可用。
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
SDK_DIR="${AITVBOX_100ASK_SDK_ROOT:-}"

# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

require_cmd adb "apt install adb"
require_cmd openssl "apt install openssl"
[[ "${SDK_DIR}" == /* ]] ||
  die "AITVBOX_100ASK_SDK_ROOT 必须指向私有 100ask SDK 的绝对路径"
require_dir "${SDK_DIR}" "私有 100ask SDK 目录不存在"
SDK_DIR="$(cd -- "${SDK_DIR}" && pwd -P)"
case "${SDK_DIR}/" in
  "${PRODUCT_ROOT}/"*)
    die "AITVBOX_100ASK_SDK_ROOT 必须位于产品源码树之外"
    ;;
esac
SIGN_TOOL="${SDK_DIR}/tools/sign_device.sh"
KEY_DIR="${SDK_DIR}/100ask_keys"
PRIV_KEY="${KEY_DIR}/100ask_ecdsa.pem"
require_file "${SIGN_TOOL}" "签名工具缺失：${SIGN_TOOL}"
require_file "${PRIV_KEY}" "私钥缺失：${PRIV_KEY}（从受控私有存储恢复）"

CPUID="${1:-}"
ADB_SERIAL="${ADB_SERIAL:-${2:-}}"

# adb 命令前缀（多设备时用 -s 指定）
adb_cmd() {
  if [[ -n "${ADB_SERIAL}" ]]; then
    adb -s "${ADB_SERIAL}" "$@"
  else
    adb "$@"
  fi
}

# 读设备 cpuid：/sys/class/sunxi_info/sys_info 的 sunxi_serial 行
read_cpuid_from_device() {
  local line
  line="$(adb_cmd shell cat /sys/class/sunxi_info/sys_info 2>/dev/null | grep -i 'sunxi_serial' | head -1)"
  # 形如 "sunxi_serial      : 2848151601044f2400005c0000000000"
  local val="${line#*:}"
  val="$(echo "${val}" | tr -d '[:space:]')"
  echo "${val}"
}

# 1. 确定 cpuid。即使调用者传了 CPUID，也必须与当前 ADB 设备读值一致，
# 避免把 A 板签名写到 B 板。
echo "==> adb root"
adb_cmd root >/dev/null 2>&1 || true
adb_cmd wait-for-device
echo "==> 读设备 cpuid"
DEVICE_CPUID="$(read_cpuid_from_device)"
DEVICE_CPUID="${DEVICE_CPUID,,}"
if [[ ! "${DEVICE_CPUID}" =~ ^[0-9a-f]{32}$ ]]; then
  die "读不到合法 cpuid：必须是 32 个十六进制字符" \
      "检查 adb devices 和 /sys/class/sunxi_info/sys_info"
fi
if [[ -z "${CPUID}" ]]; then
  CPUID="${DEVICE_CPUID}"
else
  CPUID="${CPUID,,}"
  if [[ ! "${CPUID}" =~ ^[0-9a-f]{32}$ ]]; then
    die "指定的 cpuid 格式错误：必须是 32 个十六进制字符"
  fi
  if [[ "${CPUID}" != "${DEVICE_CPUID}" ]]; then
    die "cpuid 与当前 ADB 设备不匹配，拒绝写入" \
        "指定：${CPUID}" \
        "设备：${DEVICE_CPUID}"
  fi
fi
echo "    cpuid = ${CPUID}"

# 2. 用私钥签 cpuid（照 example sign_device.sh）
echo "==> 用 100ask 私钥签名（ECDSA-SHA256）"
SIGN_OUT="$(KEY_DIR="${KEY_DIR}" bash "${SIGN_TOOL}" sign "${CPUID}")"
SIG_HEX="$(echo "${SIGN_OUT}" | grep -E '^signature:' | head -1 | awk '{print $2}')"
if [[ -z "${SIG_HEX}" ]]; then
  die "签名失败，sign_device.sh 输出：" "${SIGN_OUT}"
fi
SIG_HEX="${SIG_HEX,,}"
if [[ ! "${SIG_HEX}" =~ ^[0-9a-f]{128,160}$ || $(( ${#SIG_HEX} % 2 )) -ne 0 ]]; then
  die "签名格式错误：应为 128~160 个、偶数长度的 DER 十六进制字符"
fi
echo "    device_sig = ${SIG_HEX:0:32}...（$(echo -n "${SIG_HEX}" | wc -c) hex 字符）"

# 3. 烧进设备 /etc/100ask/device_sig
echo "==> 烧录 device_sig 到设备 /etc/100ask/device_sig"
adb_cmd root >/dev/null 2>&1 || true
adb_cmd wait-for-device

echo "==> 等待 /etc/100ask 持久化挂载就绪"
PERSIST_READY=0
for _ in $(seq 1 30); do
  if adb_cmd shell "awk '\$5==\"/etc/100ask\" { found=1 } END { exit(found ? 0 : 1) }' /proc/self/mountinfo" \
      >/dev/null 2>&1; then
    PERSIST_READY=1
    break
  fi
  sleep 1
done
if [[ "${PERSIST_READY}" != "1" ]]; then
  die "/etc/100ask 尚未 bind 到持久化分区，拒绝写入，避免签名在重启/OTA 后丢失"
fi

adb_cmd shell mkdir -p /etc/100ask
# 临时文件必须与目标在同一目录，mv 才能落成原子 rename。
REMOTE_TMP="/etc/100ask/.device_sig.tmp.$$"
adb_cmd shell "umask 077 && printf '%s' '${SIG_HEX}' > '${REMOTE_TMP}' && chmod 600 '${REMOTE_TMP}' && mv '${REMOTE_TMP}' /etc/100ask/device_sig && sync"

# 4. 校验
VERIFY="$(adb_cmd shell cat /etc/100ask/device_sig 2>/dev/null | tr -d '[:space:]')"
if [[ "${VERIFY}" != "${SIG_HEX}" ]]; then
  die "烧录校验失败：设备读回 [${VERIFY:0:32}...] ≠ 写入 [${SIG_HEX:0:32}...]"
fi
VERIFY_MODE="$(adb_cmd shell stat -c '%a' /etc/100ask/device_sig 2>/dev/null | tr -d '\r[:space:]')"
if [[ "${VERIFY_MODE}" != "600" ]]; then
  die "device_sig 权限错误：期望 600，实际 ${VERIFY_MODE:-unknown}"
fi
echo "    烧录成功，校验通过"

echo
echo "==> 完成。设备重启后将自动 provision 注册："
echo "    adb reboot"
echo "    串口/日志看：/tmp/cloud.log（provision POST → 拿 secret → 连 MQTT 120.76.140.213:1883）"
echo
echo "提示："
echo "  - 私钥 ${PRIV_KEY} 是开发机密，勿提交/外泄。"
echo "  - 本脚本禁止直接用于正式量产；量产由不含私钥的出厂助手调用签名服务。"
