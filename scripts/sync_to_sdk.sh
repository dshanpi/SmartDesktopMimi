#!/usr/bin/env bash
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"
DRY_RUN=0
ALLOW_OVERWRITE=0
SDK=""

usage() {
    echo "usage: $0 [--dry-run] [--allow-overwrite] <sdk-root>" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --allow-overwrite)
            ALLOW_OVERWRITE=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            if [[ -n "${SDK}" ]]; then
                usage
                exit 2
            fi
            SDK="$1"
            shift
            ;;
    esac
done

if [[ -z "${SDK}" ]]; then
    usage
    exit 2
fi

if [[ ! -d "${SDK}/.repo" ]]; then
    echo "error: not a Tina SDK root: ${SDK}" >&2
    exit 1
fi

# 瘦身后的 sync：产品源码/TuyaOpen/vendor 不再拷进 SDK。
# app 在产品仓库内 out-of-tree 构建（build_apps.sh），产物落 build/；
# SDK 侧只保留全部 aitvbox-* 薄包；
# pack 时通过 $AITVBOX_PRODUCT_BUILD 读取产品产物。因此 sync 只同步包定义，
# 不打开产品包开关。产品固件由 build_firmware.sh 临时打开。

copy_dir() {
    local src="$1"
    local dst="$2"

    echo "DIR  ${src} -> ${dst}"
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        return
    fi
    if [[ -e "${dst}" && "${ALLOW_OVERWRITE}" -ne 1 ]]; then
        echo "error: target exists, use --allow-overwrite after reviewing --dry-run: ${dst}" >&2
        exit 1
    fi
    mkdir -p "${dst}"
    require_cmd rsync "apt install rsync"
    rsync -a --delete --exclude='.git' "${src}/" "${dst}/"
}

# 同步产品 OpenWrt 包，不拷贝应用源码。
# 不修改任何 CONFIG_PACKAGE_aitvbox-*，保持裸跑 SDK 时默认为纯 SDK。
for package_dir in "${PRODUCT_ROOT}"/packaging/aitvbox-*; do
    package_name="$(basename "${package_dir}")"
    copy_dir "${package_dir}" \
        "${SDK}/openwrt/package/allwinner/custom/${package_name}"
done

echo "Sync complete."
echo "注意：sync 不拷贝 app 源码/TuyaOpen/vendor，也不打开 AITVBox 产品包。"
echo "裸跑 SDK 仍是纯 SDK；产品固件请使用 build_firmware.sh 临时进入产品模式。"
echo "后续: ./scripts/build_apps.sh ${SDK}  &&  ./scripts/build_firmware.sh ${SDK}"
