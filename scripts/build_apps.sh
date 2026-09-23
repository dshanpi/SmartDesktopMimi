#!/usr/bin/env bash
# 在产品仓库内 out-of-tree 构建全部应用，产物统一落到产品仓库 build/。
# SDK 仅提供 toolchain + staging_dir（借工具链，不搬源码进 SDK）。
#
# 产物布局:
#   build/lv_port_linux/{lv_backend,lvglsim}
#   build/hdmi_preview/hdmi_preview
#   build/tuyaopen/your_chat_bot_QIO_1.0.1.bin
#   build/platform/{aitvbox-mcpd,aitvbox-agentd,aitvbox-controld,aitvbox-appd,aitvbox-app-policy}
#   build/ipkvm/{aitvbox-ipkvmd,aitvbox-kvm-video}
set -euo pipefail

SDK=""
CLEAN_BUILD=0
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
BUILD_OUT="${PRODUCT_ROOT}/build"
PREPARE_TUYAOPEN="${SELF_DIR}/prepare_tuyaopen_build_root.sh"
ENABLE_100ASK_CLOUD="${AITVBOX_ENABLE_100ASK_CLOUD:-OFF}"
PRIVATE_100ASK_SDK_ROOT="${AITVBOX_100ASK_SDK_ROOT:-}"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

case "${ENABLE_100ASK_CLOUD^^}" in
    1|ON|TRUE|YES)
        ENABLE_100ASK_CLOUD=ON
        ;;
    0|OFF|FALSE|NO)
        ENABLE_100ASK_CLOUD=OFF
        ;;
    *)
        die "AITVBOX_ENABLE_100ASK_CLOUD must be ON or OFF"
        ;;
esac
export AITVBOX_ENABLE_100ASK_CLOUD="${ENABLE_100ASK_CLOUD}"
export AITVBOX_100ASK_SDK_ROOT="${PRIVATE_100ASK_SDK_ROOT}"

usage() {
    cat >&2 <<USAGE
usage: $0 [--clean] <sdk-root>

  --clean  删除本脚本管理的应用构建输出后再编译；正式发布时使用
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            CLEAN_BUILD=1
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
    die "not a Tina SDK root: ${SDK}" "SDK 路径错或缺 .repo"
fi

# 前置命令检查
require_cmd git "apt install git"
require_cmd make "apt install make"
require_cmd cmake "apt install cmake"   # build_openwrt.sh 用 cmake
require_cmd python3 "apt install python3"

python3 "${SELF_DIR}/audit_english_ui.py"

# 前置输入检查
require_file "${SDK}/build/envsetup.sh" "SDK 未初始化或路径错误：${SDK}"
require_file "${PRODUCT_ROOT}/apps/lv_port_linux/build_openwrt.sh"
require_file "${PRODUCT_ROOT}/apps/hdmi_preview/Makefile"
require_file "${PRODUCT_ROOT}/apps/platform_services/Makefile"
require_file "${PRODUCT_ROOT}/apps/ipkvm/Makefile"
require_dir "${PRODUCT_ROOT}/third_party/TuyaOpen" \
    "vendored 源码缺失：先跑 ./scripts/vendor_tuyaopen.sh"
require_dir "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs" \
    "vendor 库缺失，检查 vendor/tuyaopen-a133-b6-libs 是否完整"
if [[ "${ENABLE_100ASK_CLOUD}" == "ON" ]]; then
    [[ "${PRIVATE_100ASK_SDK_ROOT}" == /* ]] ||
        die "启用 100ask 云插件时 AITVBOX_100ASK_SDK_ROOT 必须是绝对路径"
    require_dir "${PRIVATE_100ASK_SDK_ROOT}" "私有 100ask SDK 目录不存在"
    PRIVATE_100ASK_SDK_ROOT="$(cd -- "${PRIVATE_100ASK_SDK_ROOT}" && pwd -P)"
    case "${PRIVATE_100ASK_SDK_ROOT}/" in
        "${PRODUCT_ROOT}/"*)
            die "AITVBOX_100ASK_SDK_ROOT 必须位于产品源码树之外"
            ;;
    esac
    export AITVBOX_100ASK_SDK_ROOT="${PRIVATE_100ASK_SDK_ROOT}"
    require_file "${PRIVATE_100ASK_SDK_ROOT}/include/100ask_iot.h" \
        "私有 100ask SDK 缺少公开头文件"
    require_file "${PRIVATE_100ASK_SDK_ROOT}/src/100ask_iot.c" \
        "私有 100ask SDK 缺少实现源码"
fi

# Dependency staging may temporarily enable package symbols. Preserve the
# caller's SDK configuration exactly so an application-only build does not
# leave Tina in product mode or alter a later pure-SDK build.
SDK_CONFIGS=(
    "${SDK}/openwrt/target/a133/a133-b6/defconfig"
    "${SDK}/out/a133/b6/openwrt/tmp/.config"
)
CONFIG_BACKUP_DIR="$(mktemp -d /tmp/aitvbox-sdk-config.XXXXXX)"
restore_sdk_configs() {
    local status=$?
    local index
    for index in "${!SDK_CONFIGS[@]}"; do
        if [[ -f "${CONFIG_BACKUP_DIR}/${index}" ]]; then
            cp -a "${CONFIG_BACKUP_DIR}/${index}" "${SDK_CONFIGS[$index]}"
        fi
    done
    rm -rf "${CONFIG_BACKUP_DIR}"
    exit "${status}"
}
for index in "${!SDK_CONFIGS[@]}"; do
    if [[ -f "${SDK_CONFIGS[$index]}" ]]; then
        cp -a "${SDK_CONFIGS[$index]}" "${CONFIG_BACKUP_DIR}/${index}"
    fi
done
trap restore_sdk_configs EXIT

if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
    echo "==> 清理应用构建输出（保留源码、vendor、TuyaOpen venv 与下载缓存）"
    make -C "${PRODUCT_ROOT}/apps/hdmi_preview" SDK_ROOT="${SDK}" clean
    make -C "${PRODUCT_ROOT}/apps/platform_services" SDK_ROOT="${SDK}" clean
    make -C "${PRODUCT_ROOT}/apps/ipkvm" SDK_ROOT="${SDK}" \
        BUILD_DIR="${BUILD_OUT}/ipkvm" clean
    rm -rf -- \
        "${BUILD_OUT}/lv_port_linux" \
        "${BUILD_OUT}/hdmi_preview" \
        "${BUILD_OUT}/platform" \
        "${BUILD_OUT}/tuyaopen"
    rm -f -- "${BUILD_OUT}/aitvbox-version" "${BUILD_OUT}/aitvbox-tuya-pid"
fi

mkdir -p "${BUILD_OUT}/lv_port_linux" "${BUILD_OUT}/hdmi_preview" \
    "${BUILD_OUT}/tuyaopen" "${BUILD_OUT}/platform" "${BUILD_OUT}/ipkvm"

# ensure_mosquitto_staging：lv_backend 链 libmosquitto（100ask Cloud），但 build_apps 阶段
# staging 可能还没有（mosquitto 由 aitvbox-suite 的 DEPENDS 在 build_firmware 全量时才编）。
# 这里检测 staging 无 libmosquitto.so 时，开 CONFIG_PACKAGE_libmosquitto-ssl 并跑一次
# openwrt_rootfs 把它编进 staging。已就位则跳过（幂等）。
ensure_mosquitto_staging() {
    local staging="${SDK}/out/a133/b6/openwrt/staging_dir"
    if find "${staging}" -name 'libmosquitto*.so' 2>/dev/null | grep -q .; then
        echo "==> libmosquitto 已在 staging，跳过"
        return 0
    fi
    echo "==> staging 无 libmosquitto，开 CONFIG_PACKAGE_libmosquitto-ssl 并编进 staging"
    local defcfg="${SDK}/openwrt/target/a133/a133-b6/defconfig"
    local tmpcfg="${SDK}/out/a133/b6/openwrt/tmp/.config"
    for cfg in "${defcfg}" "${tmpcfg}"; do
        [[ -f "$cfg" ]] || continue
        if grep -q "CONFIG_PACKAGE_libmosquitto-ssl" "$cfg"; then
            sed -i 's/^# CONFIG_PACKAGE_libmosquitto-ssl is not set/CONFIG_PACKAGE_libmosquitto-ssl=y/' "$cfg"
            sed -i 's/^CONFIG_PACKAGE_libmosquitto-ssl=.*/CONFIG_PACKAGE_libmosquitto-ssl=y/' "$cfg"
        else
            echo "CONFIG_PACKAGE_libmosquitto-ssl=y" >> "$cfg"
        fi
    done
    ( cd "${SDK}" && set +eu && source build/envsetup.sh 2>/dev/null; ./build.sh openwrt_rootfs ) \
        || die "openwrt_rootfs 构建失败（编 libmosquitto-ssl）" \
               "看 SDK 编译日志：${SDK}/out/a133/b6/openwrt/" \
               "可手动跑：cd ${SDK} && ./build.sh openwrt_rootfs"
    find "${staging}" -name 'libmosquitto*.so' 2>/dev/null | grep -q . \
        || die "openwrt_rootfs 完成但 staging 仍无 libmosquitto，检查 mosquitto 包是否编译成功"
    echo "==> libmosquitto 已编入 staging"
}
if [[ "${ENABLE_100ASK_CLOUD}" == "ON" ]]; then
    ensure_mosquitto_staging
else
    echo "==> 100ask 云插件未启用，跳过 libmosquitto staging"
fi

# agentd 需要带 HTTPS 的 libcurl。Tina 默认配置虽然选中 libcurl，但可能把 HTTP
# 协议裁掉；仅检查 .so 是否存在会产生能链接、运行时却报 unsupported protocol 的固件。
ensure_curl_http_staging() {
    local staging="${SDK}/out/a133/b6/openwrt/staging_dir/target"
    local curl_config="${SDK}/out/a133/b6/openwrt/build_dir/target/curl-7.82.0/lib/curl_config.h"
    if [[ -f "${staging}/usr/include/curl/curl.h" ]] &&
       [[ -f "${staging}/usr/lib/libcurl.so" ]] &&
       { [[ ! -f "${curl_config}" ]] || ! grep -q '^#define CURL_DISABLE_HTTP 1' "${curl_config}"; }; then
        echo "==> libcurl HTTPS 已在 staging，跳过"
        return 0
    fi

    echo "==> 开启 libcurl HTTP + wolfSSL 并编入 staging"
    local defcfg="${SDK}/openwrt/target/a133/a133-b6/defconfig"
    local tmpcfg="${SDK}/out/a133/b6/openwrt/tmp/.config"
    for cfg in "${defcfg}" "${tmpcfg}"; do
        [[ -f "${cfg}" ]] || continue
        for symbol in CONFIG_PACKAGE_libcurl CONFIG_LIBCURL_HTTP CONFIG_LIBCURL_WOLFSSL; do
            sed -i "s/^# ${symbol} is not set/${symbol}=y/" "${cfg}"
            if ! grep -q "^${symbol}=y$" "${cfg}"; then
                echo "${symbol}=y" >>"${cfg}"
            fi
        done
    done
    (
        cd "${SDK}"
        set +u
        source build/envsetup.sh >/dev/null 2>&1
        source .buildconfig
        /usr/bin/make -C openwrt/openwrt package/curl/clean
        /usr/bin/make -C openwrt/openwrt package/curl/compile -j"$(nproc)"
    ) || die "带 HTTPS 的 libcurl 构建失败"
    [[ -f "${staging}/usr/include/curl/curl.h" ]] ||
        die "curl 构建完成但 staging 缺头文件"
    ! grep -q '^#define CURL_DISABLE_HTTP 1' "${curl_config}" ||
        die "curl 构建完成但 HTTP 仍被禁用"
}
ensure_curl_http_staging

# hdmi_preview uses libjpeg directly to expose captured HDMI frames to the
# local MCP agent.  The product package declares libjpeg-turbo as a runtime
# dependency, but build_apps runs before build_firmware enables that package,
# so a fresh SDK may not have installed jpeglib.h into staging yet.
ensure_libjpeg_staging() {
    local staging="${SDK}/out/a133/b6/openwrt/staging_dir/target"
    if [[ -f "${staging}/usr/include/jpeglib.h" ]] &&
       find "${staging}/usr/lib" -maxdepth 1 -name 'libjpeg.so*' -print -quit 2>/dev/null | grep -q .; then
        echo "==> libjpeg-turbo 已在 staging，跳过"
        return 0
    fi

    echo "==> 编译 libjpeg-turbo 并安装到 staging"
    local defcfg="${SDK}/openwrt/target/a133/a133-b6/defconfig"
    local tmpcfg="${SDK}/out/a133/b6/openwrt/tmp/.config"
    local cfg
    for cfg in "${defcfg}" "${tmpcfg}"; do
        [[ -f "${cfg}" ]] || continue
        sed -i 's/^# CONFIG_PACKAGE_libjpeg-turbo is not set/CONFIG_PACKAGE_libjpeg-turbo=y/' "${cfg}"
        if ! grep -q '^CONFIG_PACKAGE_libjpeg-turbo=y$' "${cfg}"; then
            echo 'CONFIG_PACKAGE_libjpeg-turbo=y' >>"${cfg}"
        fi
    done
    (
        cd "${SDK}"
        set +u
        source build/envsetup.sh >/dev/null 2>&1
        source .buildconfig
        /usr/bin/make -C openwrt/openwrt package/libjpeg-turbo/compile -j"$(nproc)"
    ) || die "libjpeg-turbo staging 构建失败"
    [[ -f "${staging}/usr/include/jpeglib.h" ]] ||
        die "libjpeg-turbo 构建完成但 staging 缺 jpeglib.h"
    find "${staging}/usr/lib" -maxdepth 1 -name 'libjpeg.so*' -print -quit 2>/dev/null | grep -q . ||
        die "libjpeg-turbo 构建完成但 staging 缺 libjpeg.so"
}
ensure_libjpeg_staging

# The HDMI preview audio path resamples captured PCM before playback.  Like
# libjpeg-turbo above, libsamplerate is pulled into the final image by the
# product package, but its development files are needed one phase earlier
# while the out-of-tree binary is compiled.
ensure_libsamplerate_staging() {
    local staging="${SDK}/out/a133/b6/openwrt/staging_dir/target"
    if [[ -f "${staging}/usr/include/samplerate.h" ]] &&
       find "${staging}/usr/lib" -maxdepth 1 -name 'libsamplerate.so*' -print -quit 2>/dev/null | grep -q .; then
        echo "==> libsamplerate 已在 staging，跳过"
        return 0
    fi

    echo "==> 编译 libsamplerate 并安装到 staging"
    local defcfg="${SDK}/openwrt/target/a133/a133-b6/defconfig"
    local tmpcfg="${SDK}/out/a133/b6/openwrt/tmp/.config"
    local cfg
    for cfg in "${defcfg}" "${tmpcfg}"; do
        [[ -f "${cfg}" ]] || continue
        sed -i 's/^# CONFIG_PACKAGE_libsamplerate is not set/CONFIG_PACKAGE_libsamplerate=y/' "${cfg}"
        if ! grep -q '^CONFIG_PACKAGE_libsamplerate=y$' "${cfg}"; then
            echo 'CONFIG_PACKAGE_libsamplerate=y' >>"${cfg}"
        fi
    done
    (
        cd "${SDK}"
        set +u
        source build/envsetup.sh >/dev/null 2>&1
        source .buildconfig
        /usr/bin/make -C openwrt/openwrt package/libsamplerate/compile -j"$(nproc)"
    ) || die "libsamplerate staging 构建失败"
    [[ -f "${staging}/usr/include/samplerate.h" ]] ||
        die "libsamplerate 构建完成但 staging 缺 samplerate.h"
    find "${staging}/usr/lib" -maxdepth 1 -name 'libsamplerate.so*' -print -quit 2>/dev/null | grep -q . ||
        die "libsamplerate 构建完成但 staging 缺 libsamplerate.so"
}
ensure_libsamplerate_staging

echo "==> [1/5] lv_port_linux (out-of-tree)"
(cd "${PRODUCT_ROOT}/apps/lv_port_linux" && ./build_openwrt.sh "${SDK}") \
    || die "lv_port_linux 构建失败" "看上方 cmake/make 错误输出" "可单独跑：cd apps/lv_port_linux && ./build_openwrt.sh ${SDK}"
# build_openwrt.sh 产物在 apps/lv_port_linux/build/bin，归集到统一 build/
cp -f "${PRODUCT_ROOT}/apps/lv_port_linux/build/bin/lv_backend"  "${BUILD_OUT}/lv_port_linux/"
cp -f "${PRODUCT_ROOT}/apps/lv_port_linux/build/bin/lvglsim"    "${BUILD_OUT}/lv_port_linux/"

echo "==> [2/5] hdmi_preview (out-of-tree)"
make -C "${PRODUCT_ROOT}/apps/hdmi_preview" SDK_ROOT="${SDK}" -j"$(nproc)" \
    || die "hdmi_preview 构建失败" "看上方 make 错误输出" "可单独跑：make -C apps/hdmi_preview SDK_ROOT=${SDK}"
cp -f "${PRODUCT_ROOT}/apps/hdmi_preview/build/hdmi_preview" "${BUILD_OUT}/hdmi_preview/"

echo "==> [3/5] platform services (out-of-tree)"
make -C "${PRODUCT_ROOT}/apps/platform_services" SDK_ROOT="${SDK}" -j"$(nproc)" \
    || die "platform services 构建失败"
cp -f "${PRODUCT_ROOT}/apps/platform_services/build/aitvbox-mcpd" \
    "${PRODUCT_ROOT}/apps/platform_services/build/aitvbox-agentd" \
    "${PRODUCT_ROOT}/apps/platform_services/build/aitvbox-controld" \
    "${PRODUCT_ROOT}/apps/platform_services/build/aitvbox-appd" \
    "${PRODUCT_ROOT}/apps/platform_services/build/aitvbox-app-policy" \
    "${BUILD_OUT}/platform/"

echo "==> [4/5] IPKVM WebRTC + A133 H.264 sender (out-of-tree)"
make -C "${PRODUCT_ROOT}/apps/ipkvm" SDK_ROOT="${SDK}" \
    BUILD_DIR="${BUILD_OUT}/ipkvm" -j"$(nproc)" \
    || die "IPKVM 构建失败"

echo "==> [5/5] TuyaOpen chatbot (out-of-tree)"
# TuyaOpen 构建源已 vendor 在 third_party/TuyaOpen（源码+platform/LINUX+.tools，离线）。
# prepare 从 vendored 源 rsync 出 .tuyaopen-build 工作副本，打 A133 patch、注入 vendor 库。
TUYAOPEN_BUILD_ROOT="${TUYAOPEN_BUILD_ROOT:-${PRODUCT_ROOT}/.tuyaopen-build}"
if [[ "${AITVBOX_SKIP_TUYA_PREPARE:-0}" != "1" ]]; then
    "${PREPARE_TUYAOPEN}" --build-root "${TUYAOPEN_BUILD_ROOT}"
elif [[ ! -f "${TUYAOPEN_BUILD_ROOT}/tos.py" ]]; then
    die "TuyaOpen build source not found: ${TUYAOPEN_BUILD_ROOT}" \
        "Run: ${PREPARE_TUYAOPEN} --build-root ${TUYAOPEN_BUILD_ROOT}"
fi

if [[ ! -f "${TUYAOPEN_BUILD_ROOT}/tos.py" ]]; then
    die "TuyaOpen build source not found after prepare: ${TUYAOPEN_BUILD_ROOT}"
fi
if [[ ! -f "${TUYAOPEN_BUILD_ROOT}/platform/LINUX/tuyaos_adapter/src/tkl_audio/libs/A133_B6/MNN/libMNN.so" ]]; then
    die "A133_B6 TuyaOpen vendor libs are missing under platform/LINUX" \
        "Re-run: ${PREPARE_TUYAOPEN} --clean --build-root ${TUYAOPEN_BUILD_ROOT}"
fi

APP_DIR="${TUYAOPEN_BUILD_ROOT}/apps/tuya.ai/your_chat_bot"
(cd "${APP_DIR}" && ./build_a133.sh "${TUYAOPEN_BUILD_ROOT}" "${SDK}") \
    || die "TuyaOpen chatbot 构建失败" "看上方 tos.py build 错误输出" \
           "可单独跑：cd ${APP_DIR} && ./build_a133.sh ${TUYAOPEN_BUILD_ROOT} ${SDK}" \
           "若联网/venv 问题，先排查 prepare_tuyaopen_build_root.sh"
cp -f "${APP_DIR}/dist/your_chat_bot_1.0.1/your_chat_bot_QIO_1.0.1.bin" "${BUILD_OUT}/tuyaopen/"

# 固件必须显式携带实际编译 PID，供出厂助手与量产云交叉校验，避免工位配置
# 指向另一个产品的 License 池。PID 唯一来源为本次构建使用的 A133_B6.config。
TUYA_PID=$(sed -n 's/^CONFIG_TUYA_PRODUCT_ID="\([A-Za-z0-9]\{16\}\)"$/\1/p' \
    "${APP_DIR}/config/A133_B6.config" | head -1)
[[ "${TUYA_PID}" =~ ^[A-Za-z0-9]{16}$ ]] || \
    die "无法从本次 TuyaOpen 构建配置读取合法 PID"
printf 'TUYA_PID=%s\n' "${TUYA_PID}" > "${BUILD_OUT}/aitvbox-tuya-pid"
echo "==> tuya pid manifest: ${TUYA_PID}"

# 生成版本号文件 build/aitvbox-version，由 aitvbox-suite 薄包 install 到 /etc/aitvbox-version。
#
# 版本号与 git 彻底解耦：
#   AITVBOX_VERSION — OTA 比对用，纯语义版本（MAJOR.MINOR.PATCH），唯一来源是仓库根 VERSION
#                     文件。发版时手改 VERSION（如 1.0.0 → 1.0.1）并 commit 即可，平时不动。
#                     git 状态（tag/dirty/commit）不影响它，保证同一版本编译 N 次字符串一致、
#                     云端后台可按整数语义比对。设备上报、后台匹配、OTA 推送 version 字段全用它。
#   VERSION_GIT/PRODUCT_COMMIT/BUILD_DATE — 纯溯源日志，仅供人排查"这固件对应哪个 git 提交"，
#                     不参与任何 OTA 比对。不做校验、不警告、不卡编译——git 该咋样咋样。
VERSION_FILE="${PRODUCT_ROOT}/VERSION"
if [ ! -f "${VERSION_FILE}" ]; then
    echo "ERROR: VERSION file not found at ${VERSION_FILE}" >&2
    exit 1
fi
# 去首尾空白/换行，拿到纯 semver
AITVBOX_VERSION=$(tr -d '[:space:]' < "${VERSION_FILE}")
VERSION_GIT=$(git -C "${PRODUCT_ROOT}" describe --tags --always --dirty 2>/dev/null || echo "unknown")
VERSION_DATE=$(date +%Y%m%d%H%M 2>/dev/null || echo "nodate")
{
    echo "AITVBOX_VERSION=${AITVBOX_VERSION}"
    echo "VERSION_GIT=${VERSION_GIT}"
    echo "PRODUCT_COMMIT=$(git -C "${PRODUCT_ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "BUILD_DATE=${VERSION_DATE}"
} > "${BUILD_OUT}/aitvbox-version"
echo "==> version: ${BUILD_OUT}/aitvbox-version (AITVBOX_VERSION=${AITVBOX_VERSION}, git=${VERSION_GIT})"

echo
echo "Product app build complete. Artifacts under: ${BUILD_OUT}"
ls -l "${BUILD_OUT}/lv_port_linux/" "${BUILD_OUT}/hdmi_preview/" \
    "${BUILD_OUT}/platform/" "${BUILD_OUT}/ipkvm/" "${BUILD_OUT}/tuyaopen/"
