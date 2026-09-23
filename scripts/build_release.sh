#!/usr/bin/env bash
# AITVBox 正式发布唯一入口：预检 -> 应用干净重编 -> Tina 全量构建/pack ->
# rootfs 验收 ->（可选）签名 OTA -> 归档镜像、日志、版本与 SHA256。
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

SDK=""
CHECK_ONLY=0
WITH_OTA=0
INCLUDE_SDK_IMAGES=0
ALLOW_DIRTY=0
OUTPUT_DIR=""

usage() {
    cat >&2 <<USAGE
usage: $0 [options] <sdk-root>

Options:
  --check-only          只做环境、源码和 SDK 预检，不编译、不创建发布目录
  --with-ota            构建带 A/B 地基的固件，并生成 RSA 签名 .swu
  --include-sdk-images  除最终烧录镜像外，再归档 boot.img 和 rootfs.img
  --output-dir <path>   指定发布目录；默认 build/releases/<版本-提交-UTC时间>
  --allow-dirty         允许已跟踪源码有未提交修改（会记录在构建清单；不推荐）
  -h, --help

默认执行正式完整流程。开发期只重编某一层时，请直接使用 build_apps.sh 或
build_firmware.sh；不要用旧 build/ 产物手工调用 SDK pack。
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check-only) CHECK_ONLY=1; shift ;;
        --with-ota) WITH_OTA=1; shift ;;
        --include-sdk-images) INCLUDE_SDK_IMAGES=1; shift ;;
        --allow-dirty) ALLOW_DIRTY=1; shift ;;
        --output-dir)
            [[ $# -ge 2 ]] || die "--output-dir requires a path"
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
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

[[ -n "${SDK}" ]] || { usage; exit 2; }
[[ -d "${SDK}/.repo" ]] || die "not a Tina SDK root: ${SDK}" "SDK 路径错或缺 .repo"
SDK="$(cd "${SDK}" && pwd)"

for command in git make cmake rsync unsquashfs debugfs blkid sha256sum file; do
    require_cmd "${command}" "请安装提供 ${command} 的主机软件包"
done
for path in \
    "${PRODUCT_ROOT}/VERSION" \
    "${SDK}/build/envsetup.sh" \
    "${SDK}/build.sh" \
    "${SDK}/.buildconfig" \
    "${SELF_DIR}/build_apps.sh" \
    "${SELF_DIR}/build_firmware.sh" \
    "${SELF_DIR}/audit_sdk.sh" \
    "${SELF_DIR}/sync_to_sdk.sh"; do
    require_file "${path}"
done

VERSION="$(tr -d '[:space:]' < "${PRODUCT_ROOT}/VERSION")"
[[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    die "VERSION is not MAJOR.MINOR.PATCH: ${VERSION}"
COMMIT="$(git -C "${PRODUCT_ROOT}" rev-parse HEAD)"
SHORT_COMMIT="$(git -C "${PRODUCT_ROOT}" rev-parse --short=12 HEAD)"
BRANCH="$(git -C "${PRODUCT_ROOT}" symbolic-ref --short -q HEAD || echo detached)"
TRACKED_STATUS="$(git -C "${PRODUCT_ROOT}" status --short --untracked-files=no)"
if [[ -n "${TRACKED_STATUS}" && "${ALLOW_DIRTY}" -ne 1 ]]; then
    die "tracked source files have uncommitted changes" \
        "提交源码后重试，或明确使用 --allow-dirty（正式发布不推荐）" \
        "$(printf '%s' "${TRACKED_STATUS}" | head -n 1)"
fi

SUBMODULE_STATUS="$(git -C "${PRODUCT_ROOT}" submodule status --recursive)"
if grep -Eq '^[-+U]' <<<"${SUBMODULE_STATUS}"; then
    die "submodule is missing or not at the recorded commit" \
        "run: git submodule update --init --recursive"
fi
while IFS= read -r submodule_path; do
    [[ -z "${submodule_path}" ]] && continue
    if ! git -C "${PRODUCT_ROOT}/${submodule_path}" diff --quiet --ignore-submodules -- ||
       ! git -C "${PRODUCT_ROOT}/${submodule_path}" diff --cached --quiet --ignore-submodules --; then
        die "submodule has uncommitted tracked changes: ${submodule_path}"
    fi
done < <(git -C "${PRODUCT_ROOT}" config --file .gitmodules --get-regexp path 2>/dev/null | awk '{print $2}')

if [[ "${WITH_OTA}" -eq 1 ]]; then
    require_file "${PRODUCT_ROOT}/secrets/swupdate_priv.pem" \
        "生成方式见 docs/ota.md；私钥不得提交 Git"
    require_file "${PRODUCT_ROOT}/secrets/swupdate_priv.password"
    require_file "${PRODUCT_ROOT}/integrations/swupdate/keys/swupdate_public.pem"
    require_file "${SELF_DIR}/build_swu.sh"
fi

echo "AITVBox release preflight"
echo "  product : ${PRODUCT_ROOT}"
echo "  branch  : ${BRANCH}"
echo "  commit  : ${COMMIT}"
echo "  version : ${VERSION}"
echo "  SDK     : ${SDK}"
echo "  target  : A133 / B6 / UART0"
echo "  OTA     : ${WITH_OTA}"
echo

echo "==> 检查脚本语法"
while IFS= read -r script; do
    bash -n "${script}"
done < <(find "${SELF_DIR}" -maxdepth 1 -type f -name '*.sh' | sort)

echo "==> 检查产品薄包同步范围（dry-run）"
"${SELF_DIR}/sync_to_sdk.sh" --dry-run "${SDK}"

echo "==> 记录当前 SDK/产品审计"
"${SELF_DIR}/audit_sdk.sh" "${SDK}"

AVAILABLE_KIB="$(df -Pk "${SDK}" | awk 'NR == 2 {print $4}')"
if [[ "${AVAILABLE_KIB}" =~ ^[0-9]+$ && "${AVAILABLE_KIB}" -lt 15728640 ]]; then
    echo "warning: SDK 所在文件系统可用空间不足 15 GiB：${AVAILABLE_KIB} KiB" >&2
fi

if [[ "${CHECK_ONLY}" -eq 1 ]]; then
    echo
    echo "Preflight complete; no build was started."
    if [[ "${WITH_OTA}" -eq 1 ]]; then
        echo "正式构建: $0 --with-ota ${SDK}"
    else
        echo "正式构建: $0 ${SDK}"
    fi
    exit 0
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
if [[ -z "${OUTPUT_DIR}" ]]; then
    OUTPUT_DIR="${PRODUCT_ROOT}/build/releases/${VERSION}-${SHORT_COMMIT}-${STAMP}"
elif [[ "${OUTPUT_DIR}" != /* ]]; then
    OUTPUT_DIR="${PRODUCT_ROOT}/${OUTPUT_DIR}"
fi
[[ ! -e "${OUTPUT_DIR}" ]] || die "release output already exists: ${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}/logs" "${OUTPUT_DIR}/firmware" "${OUTPUT_DIR}/metadata"
INCOMPLETE_MARKER="${OUTPUT_DIR}/BUILD-INCOMPLETE"
touch "${INCOMPLETE_MARKER}"

release_exit() {
    local status=$?
    if [[ ${status} -ne 0 ]]; then
        echo "Release build failed; logs and marker retained: ${OUTPUT_DIR}" >&2
    fi
    exit "${status}"
}
trap release_exit EXIT

run_logged() {
    local name="$1"
    shift
    echo
    echo "==> ${name}"
    set +e
    "$@" 2>&1 | tee "${OUTPUT_DIR}/logs/${name}.log"
    local status=${PIPESTATUS[0]}
    set -e
    if [[ ${status} -ne 0 ]]; then
        die "release step failed: ${name}" "日志：${OUTPUT_DIR}/logs/${name}.log"
    fi
}

run_logged 10-build-apps "${SELF_DIR}/build_apps.sh" --clean "${SDK}"
if [[ "${WITH_OTA}" -eq 1 ]]; then
    run_logged 20-build-firmware env AITVBOX_OTA=1 \
        "${SELF_DIR}/build_firmware.sh" "${SDK}"
    run_logged 30-build-swu "${SELF_DIR}/build_swu.sh" "${SDK}"
else
    run_logged 20-build-firmware "${SELF_DIR}/build_firmware.sh" "${SDK}"
fi
run_logged 40-post-build-audit env AITVBOX_REPORT_ROOT="${OUTPUT_DIR}/audit" \
    "${SELF_DIR}/audit_sdk.sh" "${SDK}"

FIRMWARE_IMAGE="${SDK}/out/a133/b6/openwrt/a133_linux_b6_uart0.img"
BOOT_IMAGE="${SDK}/out/a133/b6/openwrt/boot.img"
ROOTFS_IMAGE="${SDK}/out/a133/b6/openwrt/rootfs.img"
require_file "${FIRMWARE_IMAGE}" "SDK pack 未生成最终烧录镜像"
require_file "${BOOT_IMAGE}" "SDK 构建未生成 boot.img"
require_file "${ROOTFS_IMAGE}" "SDK 构建未生成 rootfs.img"

# A release is not flashable merely because pack succeeded. Authenticate and
# execute the board-verified gate before copying the image into a release.
run_logged 50-bootchain-gate "${SELF_DIR}/a133-bootchain-gate.sh" \
    "${FIRMWARE_IMAGE}" "${SDK}"

echo "==> 归档发布产物"
cp --reflink=auto --preserve=timestamps "${FIRMWARE_IMAGE}" "${OUTPUT_DIR}/firmware/"
cp --preserve=timestamps \
    "${PRODUCT_ROOT}/build/aitvbox-version" \
    "${PRODUCT_ROOT}/build/aitvbox-tuya-pid" \
    "${OUTPUT_DIR}/metadata/"
if [[ "${INCLUDE_SDK_IMAGES}" -eq 1 ]]; then
    mkdir -p "${OUTPUT_DIR}/sdk-images"
    cp --reflink=auto --preserve=timestamps \
        "${BOOT_IMAGE}" "${ROOTFS_IMAGE}" "${OUTPUT_DIR}/sdk-images/"
fi
if [[ "${WITH_OTA}" -eq 1 ]]; then
    SWU_IMAGE="${PRODUCT_ROOT}/build/swu/openwrt_a133_b6-ab-sign-rollback.swu"
    require_file "${SWU_IMAGE}" "build_swu.sh 未生成预期 .swu"
    mkdir -p "${OUTPUT_DIR}/ota"
    cp --reflink=auto --preserve=timestamps "${SWU_IMAGE}" "${OUTPUT_DIR}/ota/"
fi

APP_SUMS="${OUTPUT_DIR}/metadata/APPLICATION-SHA256SUMS"
(
    cd "${PRODUCT_ROOT}"
    sha256sum \
        build/lv_port_linux/lv_backend \
        build/lv_port_linux/lvglsim \
        build/hdmi_preview/hdmi_preview \
        build/platform/aitvbox-mcpd \
        build/platform/aitvbox-agentd \
        build/platform/aitvbox-controld \
        build/platform/aitvbox-appd \
        build/platform/aitvbox-app-policy \
        build/ipkvm/aitvbox-ipkvmd \
        build/ipkvm/aitvbox-kvm-video \
        build/tuyaopen/your_chat_bot_QIO_1.0.1.bin
) > "${APP_SUMS}"

SDK_MANIFEST="$(readlink -f "${SDK}/.repo/manifest.xml" 2>/dev/null || true)"
SDK_MANIFEST_SHA="unknown"
if [[ -n "${SDK_MANIFEST}" && -f "${SDK_MANIFEST}" ]]; then
    SDK_MANIFEST_SHA="$(sha256sum "${SDK_MANIFEST}" | awk '{print $1}')"
fi
SDK_BUILDCONFIG_SHA="$(sha256sum "${SDK}/.buildconfig" | awk '{print $1}')"

{
    echo "AITVBOX_RELEASE_FORMAT=1"
    echo "AITVBOX_VERSION=${VERSION}"
    echo "PRODUCT_BRANCH=${BRANCH}"
    echo "PRODUCT_COMMIT=${COMMIT}"
    echo "PRODUCT_TRACKED_DIRTY=$([[ -n "${TRACKED_STATUS}" ]] && echo 1 || echo 0)"
    echo "BUILD_UTC=${STAMP}"
    echo "TARGET_SOC=a133"
    echo "TARGET_BOARD=b6"
    echo "PACK_VARIANT=uart0"
    echo "WITH_OTA=${WITH_OTA}"
    echo "SDK_ROOT=${SDK}"
    echo "SDK_MANIFEST=${SDK_MANIFEST:-unknown}"
    echo "SDK_MANIFEST_SHA256=${SDK_MANIFEST_SHA}"
    echo "SDK_BUILDCONFIG_SHA256=${SDK_BUILDCONFIG_SHA}"
    echo
    echo "[submodules]"
    printf '%s\n' "${SUBMODULE_STATUS:-none}"
    if [[ -n "${TRACKED_STATUS}" ]]; then
        echo
        echo "[tracked source changes]"
        printf '%s\n' "${TRACKED_STATUS}"
    fi
    echo
    echo "[SDK intermediate images]"
    sha256sum "${BOOT_IMAGE}" "${ROOTFS_IMAGE}"
} > "${OUTPUT_DIR}/BUILD-MANIFEST.txt"

PURE_CONFIGS=(
    "${SDK}/openwrt/target/a133/a133-b6/defconfig"
    "${SDK}/openwrt/openwrt/.config"
    "${SDK}/out/a133/b6/openwrt/tmp/.config"
)
for config in "${PURE_CONFIGS[@]}"; do
    [[ -f "${config}" ]] || continue
    for package in suite platform usb-hid ipkvm; do
        if ! grep -qx "# CONFIG_PACKAGE_aitvbox-${package} is not set" "${config}"; then
            die "SDK was not restored to pure mode" \
                "${config}: aitvbox-${package} is not disabled"
        fi
    done
done

CHECKSUM_DIRS=(firmware metadata logs audit)
[[ -d "${OUTPUT_DIR}/ota" ]] && CHECKSUM_DIRS+=(ota)
[[ -d "${OUTPUT_DIR}/sdk-images" ]] && CHECKSUM_DIRS+=(sdk-images)
(
    cd "${OUTPUT_DIR}"
    find "${CHECKSUM_DIRS[@]}" -type f -print0 | sort -z | xargs -0 sha256sum
    sha256sum BUILD-MANIFEST.txt
) > "${OUTPUT_DIR}/SHA256SUMS"

rm -f "${INCOMPLETE_MARKER}"
touch "${OUTPUT_DIR}/BUILD-COMPLETE"
trap - EXIT

echo
echo "Release build complete: ${OUTPUT_DIR}"
echo "Flash image: ${OUTPUT_DIR}/firmware/$(basename "${FIRMWARE_IMAGE}")"
echo "Checksums:  ${OUTPUT_DIR}/SHA256SUMS"
if [[ "${WITH_OTA}" -eq 1 ]]; then
    echo "OTA image:  ${OUTPUT_DIR}/ota/$(basename "${SWU_IMAGE}")"
fi
