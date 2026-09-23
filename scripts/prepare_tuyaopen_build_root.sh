#!/usr/bin/env bash
# Prepare a writable TuyaOpen build tree from the VENDORED source in third_party/TuyaOpen.
#
# 全离线、可复现：源码 + platform/LINUX + .tools(uv/python) 已 vendor 进产品仓
# （见 scripts/vendor_tuyaopen.sh）。本脚本不联网、不 git clone，只做：
#   1. rsync vendored 源（已含固化定制）-> .tuyaopen-build（每次新鲜副本，--delete 清残留）
#   2. 注入 vendor 库 + build_a133.sh/config（设备凭据由量产阶段运行时写入）
#   3.（按需）source export.sh 用本地 .tools 建 .venv（uv/python 不下载）
#
# 产物 .tuyaopen-build/ 是构建工作副本（gitignore，不提交）。.venv 跨次复用（rsync 排除）。
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

# vendored 源码树（普通目录，非 submodule）
SOURCE_ROOT="${PRODUCT_ROOT}/third_party/TuyaOpen"
BUILD_ROOT="${TUYAOPEN_BUILD_ROOT:-${PRODUCT_ROOT}/.tuyaopen-build}"
INTEGRATION_DIR="${PRODUCT_ROOT}/integrations/tuyaopen"
VENDOR_ROOT="${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs"
CLEAN=0
RUN_EXPORT=1

usage() {
    cat >&2 <<USAGE
usage: $0 [options]

从 vendored 源码 (third_party/TuyaOpen) 准备 TuyaOpen 构建树（全离线）。
Options:
  --build-root <path>   构建工作副本路径；默认 .tuyaopen-build 或 \$TUYAOPEN_BUILD_ROOT
  --clean               先删除构建工作副本再重建
  --no-export           不 source export.sh 建 .venv（已就位时用）
  -h, --help
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-root) BUILD_ROOT="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        --no-export) RUN_EXPORT=0; shift ;;
        --no-platform) shift ;;   # 兼容旧 flag，现已无联网步骤，忽略
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

require_cmd rsync "apt install rsync"

# 前置输入检查：vendored 源码须就位
require_file "${SOURCE_ROOT}/tos.py" \
    "vendored 源码缺失：先跑 ${SELF_DIR}/vendor_tuyaopen.sh"
require_file "${SOURCE_ROOT}/export.sh" "vendored 源码缺失（无 export.sh）"
require_dir "${SOURCE_ROOT}/platform/LINUX/tuyaos_adapter" \
    "vendored platform/LINUX 缺失：重跑 vendor_tuyaopen.sh"
require_dir "${SOURCE_ROOT}/.tools/python" \
    "vendored .tools 缺失：重跑 vendor_tuyaopen.sh"
require_dir "${VENDOR_ROOT}" \
    "vendor 库缺失，检查 vendor/tuyaopen-a133-b6-libs 是否完整"

# TuyaOpen 与 platform/LINUX 定制已固化进 third_party/TuyaOpen（原 patches/、
# platform-patches 已归档到 patches-archive/）。本脚本不再构建时打补丁，直接
# rsync 已含定制的 vendored 源。升级 TuyaOpen 上游须重跑 vendor_tuyaopen.sh
# 并从 patches-archive/ 重新 apply 定制。
if [[ ${CLEAN} -eq 1 && -e "${BUILD_ROOT}" ]]; then
    echo "==> Removing old build root: ${BUILD_ROOT}"
    rm -rf "${BUILD_ROOT}"
fi

# 1) 新鲜工作副本：每次从 vendored 源（已含固化定制）rsync，--delete 清残留，保留 .venv/.git。
echo "==> rsync vendored source → ${BUILD_ROOT}（保留 .venv / .git）"
mkdir -p "${BUILD_ROOT}"
rsync -a --delete --exclude='.venv' --exclude='.git' "${SOURCE_ROOT}/" "${BUILD_ROOT}/"

# tos.py 的 env_check 用 GitPython 对构建树调 Repo()，要求是合法 git 仓。
# vendored 源已删根 .gitmodules（内嵌库作普通目录 vendor），故 download_submoudules
# 见无 .gitmodules 直接返回，不会联网 git submodule update。
# 这里 git init + 空提交让 Repo()/head.commit 合法；.git 跨次复用（rsync 已排除）。
if [[ ! -d "${BUILD_ROOT}/.git" ]]; then
    echo "==> git init 构建树（tos.py env_check 需要）"
    git -C "${BUILD_ROOT}" init -q
    git -C "${BUILD_ROOT}" -c user.email=tuya@local -c user.name=tuya \
        commit -q --allow-empty -m "vendored snapshot"
fi

# tos.py build 的 download_platform → check_platform_commit 会对 platform/LINUX 调
# git_get_commit（须是 git 仓），且 vendored commit ≠ platform_config.yaml 的 811c996c
# 时会交互提示更新。这里给 platform/LINUX 也 git init，并写 .dont_prompt_update_platform
# 标记跳过提示（vendored 源即所用源，无需按 commit 校验/更新）。
PLATFORM_LINUX="${BUILD_ROOT}/platform/LINUX"
if [[ -d "${PLATFORM_LINUX}" && ! -d "${PLATFORM_LINUX}/.git" ]]; then
    git -C "${PLATFORM_LINUX}" init -q
    git -C "${PLATFORM_LINUX}" -c user.email=tuya@local -c user.name=tuya \
        commit -q --allow-empty -m "vendored platform snapshot"
fi

APP_DIR="${BUILD_ROOT}/apps/tuya.ai/your_chat_bot"
require_dir "${APP_DIR}"

# 2) 注入 A133_B6 vendor 库与模型（覆盖式，与上游 tuya.ai 集成一致）
echo "==> Injecting A133_B6 vendor libs and models into platform/LINUX"
mkdir -p "${BUILD_ROOT}/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6"
rsync -a --delete --exclude='models/' \
    "${VENDOR_ROOT}/" \
    "${BUILD_ROOT}/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/"
mkdir -p "${BUILD_ROOT}/platform/LINUX/tuyaos_adapter/src/tkl_audio/models"
rsync -a --delete \
    "${VENDOR_ROOT}/models/" \
    "${BUILD_ROOT}/platform/LINUX/tuyaos_adapter/src/tkl_audio/models/"

# 3) 拷 build_a133.sh / config / README 到 app 目录
mkdir -p "${APP_DIR}/config"
cp -f "${INTEGRATION_DIR}/build_a133.sh" "${APP_DIR}/build_a133.sh"
cp -f "${INTEGRATION_DIR}/README_A133_B6.md" "${APP_DIR}/README_A133_B6.md"
cp -f "${INTEGRATION_DIR}/config/A133_B6.config" "${APP_DIR}/config/A133_B6.config"
chmod +x "${APP_DIR}/build_a133.sh"

# 4) 设备 UUID/AuthKey 不进入构建树。量产时由 provision_tuya_license.sh
#    逐台写入 /factory/tuya/license.env，运行时通过 tuya_iot_license_read() 加载。
echo "==> Tuya device credentials: runtime provisioning (not embedded in firmware)"

# 5) .venv：用本地 .tools 建（uv/python 不下载）。若 vendored 了 uv-cache，指向它离线 sync。
if [[ ${RUN_EXPORT} -eq 1 ]]; then
    # vendored uv-cache（可选）→ 100% 离线 uv sync
    if [[ -d "${SOURCE_ROOT}/.tools/uv-cache" ]]; then
        export UV_CACHE_DIR="${SOURCE_ROOT}/.tools/uv-cache"
        echo "==> Using vendored uv-cache (offline): ${UV_CACHE_DIR}"
    fi
    if [[ ! -x "${BUILD_ROOT}/.venv/bin/python" ]]; then
        echo "==> Preparing .venv via local .tools (export.sh)"
        (
            cd "${BUILD_ROOT}"
            # shellcheck disable=SC1091
            source ./export.sh
        ) || die "export.sh bootstrap 失败" \
            "检查 vendored .tools 是否完整（uv 0.11.18 / python 3.12.13）" \
            "可加 --no-export 跳过（仅当 .venv 已就位时）"
    else
        echo "==> .venv 已就位，复用"
    fi
fi

# 6) 写 .dont_prompt_update_platform 标记（放最后，避免 export.sh 重建 venv 时清 .cache）。
#    tos.py build 的 check_platform_commit 见此标记即跳过交互提示（vendored commit ≠
#    platform_config.yaml 的 811c996c，但 vendored 源即所用源，无需按 commit 更新）。
#    build_a133.sh 在 tos.py build 前会再补一次，双保险。
mkdir -p "${BUILD_ROOT}/.cache"
touch "${BUILD_ROOT}/.cache/.dont_prompt_update_platform"

cat <<DONE

TuyaOpen build root is ready (offline):
  ${BUILD_ROOT}

Next build command:
  TUYAOPEN_BUILD_ROOT=${BUILD_ROOT} ${PRODUCT_ROOT}/scripts/build_apps.sh <sdk-root>
DONE
