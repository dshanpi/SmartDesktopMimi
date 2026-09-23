#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Out-of-tree 构建：TuyaOpen 源码 + venv 都在产品仓库侧（自包含），
# 只借用 SDK 的 toolchain + staging_dir。
#   用法: ./build_a133.sh <tuyaopen-root> <sdk-root> [-- <tos-build-args>...]
#   前两个裸位置参数为 TUYAOPEN_ROOT / SDK_ROOT；'--' 之后的参数透传给 tos.py build。
#   也可用环境变量 TUYAOPEN_ROOT / SDK_ROOT / VENV_ROOT 覆盖（此时可省略位置参数）。
# venv 默认用 TUYAOPEN_ROOT/.venv；若不存在，自动 source TUYAOPEN_ROOT/export.sh 重建
# （export.sh 用 TUYAOPEN_ROOT/.tools 里的 uv + python 本地安装，无需联网）。
TUYAOPEN_ROOT="${TUYAOPEN_ROOT:-}"
SDK_ROOT="${SDK_ROOT:-${AITVBOX_SDK_ROOT:-/home/ubuntu/A133-Tina5.0-v0.9}}"
BUILD_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tuyaopen-root) TUYAOPEN_ROOT="$2"; shift 2 ;;
        --sdk-root)      SDK_ROOT="$2"; shift 2 ;;
        --venv-root)     VENV_ROOT_OVERRIDE="$2"; shift 2 ;;
        --)              shift; BUILD_ARGS+=("$@"); break ;;
        *)
            # 前两个裸位置参数兼容 <tuyaopen-root> <sdk-root>
            if [[ -z "${_POS_TUYAOPEN_SET:-}" ]]; then
                TUYAOPEN_ROOT="$1"; _POS_TUYAOPEN_SET=1; shift
            elif [[ -z "${_POS_SDK_SET:-}" ]]; then
                SDK_ROOT="$1"; _POS_SDK_SET=1; shift
            else
                BUILD_ARGS+=("$1"); shift
            fi
            ;;
    esac
done

VENV_ROOT="${VENV_ROOT_OVERRIDE:-${VENV_ROOT:-${TUYAOPEN_ROOT}/.venv}}"
TOOLCHAIN_CONFIG="${SDK_ROOT}/openwrt/openwrt/.config"

if [[ -z "${TUYAOPEN_ROOT}" ]]; then
    echo "error: TUYAOPEN_ROOT not set. Pass it as \$1 or --tuyaopen-root or env var." >&2
    echo "usage: $0 <tuyaopen-root> <sdk-root> [-- <tos-build-args>...]" >&2
    exit 2
fi
if [[ ! -f "${TUYAOPEN_ROOT}/tos.py" ]]; then
    echo "error: tos.py not found under TUYAOPEN_ROOT: ${TUYAOPEN_ROOT}" >&2
    exit 1
fi
# venv 自包含于产品仓库：缺失则用 TuyaOpen 自带的 export.sh 本地重建（不联网）。
if [[ ! -x "${VENV_ROOT}/bin/python" ]]; then
    echo "venv not found at ${VENV_ROOT}, rebuilding via ${TUYAOPEN_ROOT}/export.sh ..."
    if [[ ! -f "${TUYAOPEN_ROOT}/export.sh" ]]; then
        echo "error: export.sh not found under TUYAOPEN_ROOT: ${TUYAOPEN_ROOT}" >&2
        exit 1
    fi
    # export.sh 用 .tools 里的 uv + python 本地安装并 uv sync，需在 TUYAOPEN_ROOT 下 source
    (
        cd "${TUYAOPEN_ROOT}"
        # shellcheck disable=SC1091
        source ./export.sh
    )
    if [[ ! -x "${VENV_ROOT}/bin/python" ]]; then
        echo "error: venv rebuild failed: ${VENV_ROOT}/bin/python still missing" >&2
        exit 1
    fi
fi

read_config_string() {
    local key="$1"
    sed -n "s/^${key}=\"\\(.*\\)\"$/\\1/p" "${TOOLCHAIN_CONFIG}" | tail -n 1
}

if [[ ! -f "${TOOLCHAIN_CONFIG}" ]]; then
    echo "error: missing Tina OpenWrt config: ${TOOLCHAIN_CONFIG}" >&2
    exit 1
fi

TOOLCHAIN="$(read_config_string CONFIG_TOOLCHAIN_ROOT)"
TOOLCHAIN_PREFIX="$(read_config_string CONFIG_TOOLCHAIN_PREFIX)"
TOOLCHAIN="${TOOLCHAIN//\$(LICHEE_TOP_DIR)/${SDK_ROOT}}"
TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-aarch64-openwrt-linux-}"

if [[ ! -x "${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}gcc" ]]; then
    echo "error: compiler not found: ${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}gcc" >&2
    exit 1
fi

export STAGING_DIR="${SDK_ROOT}/out/a133/b6/openwrt/staging_dir"
export PATH="${TOOLCHAIN}/bin:${VENV_ROOT}/bin:${PATH}"
export CC="${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}gcc"
export CXX="${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}g++"
export AR="${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}ar"
export RANLIB="${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}ranlib"
export STRIP="${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}strip"
export COMPILE_PREX="${TOOLCHAIN}/bin/${TOOLCHAIN_PREFIX}"

echo "Using Tina toolchain: ${TOOLCHAIN}"
echo "Using Tina compiler: $("${CC}" -dumpfullversion)"

cd "${APP_DIR}"
"${VENV_ROOT}/bin/python" "${TUYAOPEN_ROOT}/tos.py" config choice -c A133_B6.config

# vendored platform/LINUX 的 commit ≠ platform_config.yaml 记录的 811c996c，
# tos.py build 的 check_platform_commit 会交互提示更新。写 .dont_prompt_update_platform
# 标记跳过提示（vendored 源即所用源，无需按 commit 校验/联网更新）。
# 放在 config choice 之后、build 之前，确保 export.sh/config choice 即使清过 .cache 也补回。
mkdir -p "${TUYAOPEN_ROOT}/.cache"
touch "${TUYAOPEN_ROOT}/.cache/.dont_prompt_update_platform"

"${VENV_ROOT}/bin/python" "${TUYAOPEN_ROOT}/tos.py" build "${BUILD_ARGS[@]}"
