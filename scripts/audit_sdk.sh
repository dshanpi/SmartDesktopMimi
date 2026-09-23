#!/usr/bin/env bash
# 审计产品层与 SDK 集成状态。
#   - 产品仓库侧：构建产物（build/）、vendor 库与模型、薄包定义是否就位。
#   - SDK 侧：全部 AITVBox 薄包是否已 sync、产品包开关状态。
# 新架构下产物与 vendor 都在产品仓库，SDK 只保留薄包 + 开关，因此审计对象与旧版不同。
set -euo pipefail

SDK="${1:-}"
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
REPORT_ROOT="${AITVBOX_REPORT_ROOT:-/tmp/aitvbox-product-audit/reports}"
STAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="${REPORT_ROOT}/${STAMP}"

usage() {
    echo "usage: $0 <sdk-root>" >&2
}

if [[ -z "${SDK}" ]]; then
    usage
    exit 2
fi

if [[ ! -d "${SDK}/.repo" ]]; then
    echo "error: not a Tina SDK root: ${SDK}" >&2
    exit 1
fi

mkdir -p "${REPORT_DIR}"

{
    echo "SDK=${SDK}"
    echo "PRODUCT_ROOT=${PRODUCT_ROOT}"
    echo "REPORT_DIR=${REPORT_DIR}"
    echo
    echo "[SDK: AITVBox 产品包开关]"
    for config in \
        "${SDK}/openwrt/target/a133/a133-b6/defconfig" \
        "${SDK}/openwrt/openwrt/.config" \
        "${SDK}/out/a133/b6/openwrt/tmp/.config"; do
        if [[ -f "${config}" ]]; then
            echo "${config}:"
            for package in suite platform usb-hid ipkvm; do
                key="CONFIG_PACKAGE_aitvbox-${package}"
                value="$(grep -E "^(${key}=y|# ${key} is not set)" "${config}" || true)"
                echo "  ${value:-missing ${key} entry}"
            done
        else
            echo "${config}: missing config file"
        fi
    done
    echo
    echo "[SDK: AITVBox 薄包同步状态]"
    for package in suite platform usb-hid ipkvm; do
        package_makefile="${SDK}/openwrt/package/allwinner/custom/aitvbox-${package}/Makefile"
        if [[ -f "${package_makefile}" ]]; then
            echo "OK   aitvbox-${package}"
        else
            echo "MISS aitvbox-${package} (run sync_to_sdk.sh)"
        fi
    done
    echo
    echo "[产品仓库: 构建产物 build/]"
} > "${REPORT_DIR}/summary.txt"

check_path() {
    local path="$1"
    if [[ -e "${path}" ]]; then
        echo "OK   ${path}" >> "${REPORT_DIR}/summary.txt"
    else
        echo "MISS ${path}" >> "${REPORT_DIR}/summary.txt"
    fi
}

# 产品仓库构建产物（由 build_apps.sh 生成）
check_path "${PRODUCT_ROOT}/build/lv_port_linux/lv_backend"
check_path "${PRODUCT_ROOT}/build/lv_port_linux/lvglsim"
check_path "${PRODUCT_ROOT}/build/hdmi_preview/hdmi_preview"
check_path "${PRODUCT_ROOT}/build/platform/aitvbox-mcpd"
check_path "${PRODUCT_ROOT}/build/platform/aitvbox-agentd"
check_path "${PRODUCT_ROOT}/build/platform/aitvbox-controld"
check_path "${PRODUCT_ROOT}/build/platform/aitvbox-appd"
check_path "${PRODUCT_ROOT}/build/platform/aitvbox-app-policy"
check_path "${PRODUCT_ROOT}/build/ipkvm/aitvbox-ipkvmd"
check_path "${PRODUCT_ROOT}/build/ipkvm/aitvbox-kvm-video"
check_path "${PRODUCT_ROOT}/build/tuyaopen/your_chat_bot_QIO_1.0.1.bin"

# 产品仓库 vendor 库与模型（薄包 install 时从 vendor/ 读）
check_path "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/MNN/libMNN.so"
check_path "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/MNN/libMNN_Express.so"
check_path "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/audio_subsys/libaudio_subsys.a"
check_path "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/opus/libopus.a"
check_path "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/models/mdtc_chunk_300ms.mnn"
check_path "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/models/tokens.txt"

# 产品仓库 app 源码字体（薄包 install 时从 apps/ 读）
check_path "${PRODUCT_ROOT}/apps/lv_port_linux/src/ui/font/SarasaUiSC-Regular.ttf"
check_path "${PRODUCT_ROOT}/apps/lv_port_linux/src/ui/font/SarasaUiSC-SemiBold.ttf"

# TuyaOpen 构建源（prepare 生成的完整可编副本）
check_path "${PRODUCT_ROOT}/scripts/prepare_tuyaopen_build_root.sh"
check_path "${PRODUCT_ROOT}/.tuyaopen-build/tos.py"
check_path "${PRODUCT_ROOT}/.tuyaopen-build/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN.so"

# SDK 薄包定义文件
check_path "${SDK}/openwrt/package/allwinner/custom/aitvbox-suite/Makefile"
check_path "${SDK}/openwrt/package/allwinner/custom/aitvbox-platform/Makefile"
check_path "${SDK}/openwrt/package/allwinner/custom/aitvbox-usb-hid/Makefile"
check_path "${SDK}/openwrt/package/allwinner/custom/aitvbox-ipkvm/Makefile"

{
    echo
    echo "[产品仓库: git 状态]"
} >> "${REPORT_DIR}/summary.txt"

write_status() {
    local name="$1"
    local dir="$2"
    local out="${REPORT_DIR}/${name}.git-status.txt"
    if [[ -d "${dir}/.git" ]]; then
        git -C "${dir}" status --short > "${out}"
        echo "  ${name}: $(wc -l < "${out}") dirty file(s) -> ${out}" >> "${REPORT_DIR}/summary.txt"
    else
        echo "not a git repository: ${dir}" > "${out}"
        echo "  ${name}: not a git repository" >> "${REPORT_DIR}/summary.txt"
    fi
}

write_status product "${PRODUCT_ROOT}"
write_status tuyaopen-build "${PRODUCT_ROOT}/.tuyaopen-build"

find "${SDK}/openwrt/package/allwinner/custom" -maxdepth 4 -type f \
    -path '*/aitvbox-*/*' > "${REPORT_DIR}/aitvbox-packages.files.txt" 2>/dev/null || true

echo "Audit report written to: ${REPORT_DIR}"
