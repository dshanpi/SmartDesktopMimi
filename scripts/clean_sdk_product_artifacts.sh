#!/usr/bin/env bash
# Remove AITVBox product files left in Tina/OpenWrt incremental rootfs state.
#
# This is intentionally SDK-side cleanup only. It never touches the product
# repository build outputs under the active AI-DeskTopBox checkout's build/.
set -euo pipefail

SDK="${1:-}"

usage() {
    echo "usage: $0 <sdk-root>" >&2
    exit 2
}

[[ -n "${SDK}" ]] || usage
[[ -d "${SDK}/.repo" ]] || { echo "error: not a Tina SDK root: ${SDK}" >&2; exit 1; }

OPENWRT_OUT="${SDK}/out/a133/b6/openwrt"

if [[ ! -d "${OPENWRT_OUT}" ]]; then
    echo "SKIP missing OpenWrt output dir: ${OPENWRT_OUT}"
    exit 0
fi

ROOTS=(
    "${OPENWRT_OUT}/build_dir/target/root-a133-b6"
    "${OPENWRT_OUT}/build_dir/target/root.orig-a133-b6"
    "${OPENWRT_OUT}/staging_dir/target/root-a133-b6"
)

PRODUCT_PATHS=(
    "usr/bin/lv_backend"
    "usr/bin/lvglsim"
    "usr/bin/hdmi_preview"
    "usr/bin/your_chat_bot_QIO_1.0.1.bin"
    "usr/bin/aitvbox-start-ui"
    "usr/bin/aitvbox-mount-sdcard"
    "usr/bin/aitvbox-mcpd"
    "usr/bin/aitvbox-agentd"
    "usr/bin/aitvbox-controld"
    "usr/bin/aitvbox-appd"
    "usr/bin/aitvbox-app-policy"
    "usr/bin/aitvbox-appctl"
    "usr/bin/aitvbox-agentctl"
    "usr/bin/aitvbox-adminctl"
    "usr/bin/aitvbox-hidctl"
    "usr/bin/aitvbox-usb-hid-test"
    "usr/bin/aitvbox-ipkvmd"
    "usr/bin/aitvbox-kvm-video"
    "usr/bin/aitvbox-ipkvmctl"
    "usr/lib/libMNN.so"
    "usr/lib/libMNN_Express.so"
    "usr/share/fonts/SarasaUiSC-Regular.ttf"
    "usr/share/fonts/SarasaUiSC-SemiBold.ttf"
    "usr/share/tuyaopen_models"
    "usr/share/aitvbox"
    "etc/aitvbox"
    "etc/aitvbox-version"
    "etc/aitvbox-tuya-pid"
    "etc/init.d/aitvbox"
    "etc/init.d/aitvbox-data"
    "etc/init.d/aitvbox-usb-hid"
    "etc/init.d/aitvbox-control"
    "etc/init.d/aitvbox-apps"
    "etc/init.d/aitvbox-ipkvm"
    "etc/init.d/adbd"
    "etc/rc.d/S95aitvbox-data"
    # Compatibility cleanup for images produced before the S95 ordering fix.
    "etc/rc.d/S51aitvbox-data"
    "etc/rc.d/S99aitvbox"
    "etc/rc.d/S98aitvbox-ipkvm"
    "etc/rc.d/K10aitvbox"
    "factory/tuya"
)

PRODUCT_PACKAGES=(
    "aitvbox-suite"
    "aitvbox-platform"
    "aitvbox-usb-hid"
    "aitvbox-ipkvm"
)

remove_path() {
    local path="$1"
    if [[ -e "${path}" || -L "${path}" ]]; then
        rm -rf -- "${path}"
        echo "DEL  ${path}"
    fi
}

remove_glob() {
    local pattern="$1"
    local matches=()
    local path

    mapfile -t matches < <(compgen -G "${pattern}" || true)
    for path in "${matches[@]}"; do
        remove_path "${path}"
    done
}

for root in "${ROOTS[@]}"; do
    [[ -d "${root}" ]] || continue
    for rel in "${PRODUCT_PATHS[@]}"; do
        remove_path "${root}/${rel}"
    done
    for package in "${PRODUCT_PACKAGES[@]}"; do
        remove_glob "${root}/usr/lib/opkg/info/${package}.*"
    done
done

# Remove stale package build and generated package metadata so a future product
# build cannot reuse old copied artifacts.
for package in "${PRODUCT_PACKAGES[@]}"; do
    remove_glob "${OPENWRT_OUT}/build_dir/target/${package}-*"
    remove_glob "${OPENWRT_OUT}/extra/packages/aarch64_generic/base/${package}_*.ipk"
    remove_glob "${OPENWRT_OUT}/staging_dir/packages/a133-b6/${package}_*.ipk"
    remove_glob "${OPENWRT_OUT}/staging_dir/target/pkginfo/${package}.*"
    remove_path "${OPENWRT_OUT}/staging_dir/target/root-a133-b6/stamp/.${package}_installed"
    remove_glob "${OPENWRT_OUT}/tmp/info/.packageinfo-*_${package}"
done

# The HID package overlays the vendor adbd init script. Put the vendor script
# back into incremental roots so a subsequent pure-SDK rootfs cannot retain the
# product ADB policy through package cache reuse.
VENDOR_ADBD_INIT="${SDK}/openwrt/package/allwinner/usb/adbd/adbd.init"
if [[ -f "${VENDOR_ADBD_INIT}" ]]; then
    for root in "${ROOTS[@]}"; do
        [[ -d "${root}" ]] || continue
        install -D -m 0755 "${VENDOR_ADBD_INIT}" "${root}/etc/init.d/adbd"
        echo "RESTORE ${root}/etc/init.d/adbd"
    done
fi

# Tidy directories that become empty after removing product-only assets.
for root in "${ROOTS[@]}"; do
    [[ -d "${root}" ]] || continue
    rmdir --ignore-fail-on-non-empty \
        "${root}/usr/share/tuyaopen_models" \
        "${root}/usr/share/aitvbox" \
        "${root}/usr/share/fonts" \
        "${root}/etc/aitvbox/trusted-app-keys" \
        "${root}/etc/aitvbox" \
        "${root}/etc/rc.d" \
        "${root}/etc/init.d" \
        "${root}/usr/lib/opkg/info" \
        "${root}/usr/lib/opkg" \
        "${root}/usr/lib" \
        "${root}/usr/bin" \
        "${root}/usr/share" \
        "${root}/usr" \
        "${root}/factory/tuya" \
        "${root}/factory" \
        "${root}/etc" 2>/dev/null || true
done

echo "AITVBox product artifacts cleaned from SDK incremental rootfs state."
