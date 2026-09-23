#!/usr/bin/env bash
set -euo pipefail

ACTION="${1:-}"
SDK="${2:-}"
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
source "${SELF_DIR}/lib.sh"

DEFCONFIG_REL="device/config/chips/a133/configs/b6/linux-4.9/openwrt_linux_defconfig"
DEFCONFIG="${SDK}/${DEFCONFIG_REL}"
ACTIVE_CONFIG_REL="kernel/linux-4.9/.config"
ACTIVE_CONFIG="${SDK}/${ACTIVE_CONFIG_REL}"
FRAGMENT="${PRODUCT_ROOT}/integrations/usb-hid/openwrt_linux_defconfig.fragment"
HID_DRIVER_REL="kernel/linux-4.9/drivers/usb/gadget/function/f_hid.c"
HID_DRIVER="${SDK}/${HID_DRIVER_REL}"
HID_DRIVER_PATCH="${PRODUCT_ROOT}/integrations/usb-hid/linux-4.9-f_hid-windows-control-requests.patch"
# Keep backups isolated per SDK root. Host tests use a synthetic SDK while a
# real Tina build may be running; sharing one global filename lets either
# process consume and delete the other's backup.
SDK_BACKUP_ID="$(
    printf '%s' "$(cd "${SDK}" && pwd -P)" |
        sha256sum |
        awk '{print $1}'
)"
BACKUP_ROOT="${PRODUCT_ROOT}/build/.sdk-file-backups/${SDK_BACKUP_ID}"
BACKUP="${BACKUP_ROOT}/${DEFCONFIG_REL}.platform"
ACTIVE_BACKUP="${BACKUP_ROOT}/${ACTIVE_CONFIG_REL}.platform"
HID_DRIVER_BACKUP="${BACKUP_ROOT}/${HID_DRIVER_REL}.platform"

usage() {
    echo "usage: $0 <apply|revert> <sdk-root>" >&2
}

[[ "${ACTION}" == "apply" || "${ACTION}" == "revert" ]] || { usage; exit 2; }
[[ -d "${SDK}/.repo" ]] || die "not a Tina SDK root: ${SDK}"

if [[ "${ACTION}" == "revert" ]]; then
    if [[ -f "${BACKUP}" ]]; then
        cp -f "${BACKUP}" "${DEFCONFIG}"
        rm -f "${BACKUP}"
        echo "Kernel config restored: ${DEFCONFIG_REL}"
    else
        warn_skip "kernel config backup not found"
    fi
    if [[ -f "${ACTIVE_BACKUP}" ]]; then
        cp -f "${ACTIVE_BACKUP}" "${ACTIVE_CONFIG}"
        rm -f "${ACTIVE_BACKUP}"
        echo "Kernel config restored: ${ACTIVE_CONFIG_REL}"
    fi
    if [[ -f "${HID_DRIVER_BACKUP}" ]]; then
        cp -f "${HID_DRIVER_BACKUP}" "${HID_DRIVER}"
        rm -f "${HID_DRIVER_BACKUP}"
        echo "Kernel HID driver restored: ${HID_DRIVER_REL}"
    fi
    rmdir "${BACKUP_ROOT}" 2>/dev/null || true
    exit 0
fi

require_file "${DEFCONFIG}"
require_file "${FRAGMENT}"

inject_config() {
    local config="$1"
    local backup="$2"
    local relative="$3"

    if [[ -f "${backup}" ]]; then
        echo "Kernel config already injected: ${relative}"
        return
    fi

    mkdir -p "$(dirname "${backup}")"
    cp -a "${config}" "${backup}"
    while IFS= read -r line; do
        [[ "${line}" == CONFIG_*=* ]] || continue
        key="${line%%=*}"
        sed -i -e "/^${key}=/d" -e "/^# ${key} is not set$/d" "${config}"
    done < "${FRAGMENT}"
    printf '\n' >> "${config}"
    cat "${FRAGMENT}" >> "${config}"
    echo "Kernel config injected: ${relative}"
}

inject_config "${DEFCONFIG}" "${BACKUP}" "${DEFCONFIG_REL}"
# A133's vendor kernel build only loads the defconfig when .config is absent.
# Patch the incremental config as well, otherwise a successful build can silently
# retain CONFIG_SECCOMP=n. The trap restores both files after packaging.
if [[ -f "${ACTIVE_CONFIG}" ]]; then
    inject_config "${ACTIVE_CONFIG}" "${ACTIVE_BACKUP}" "${ACTIVE_CONFIG_REL}"
else
    warn_skip "active kernel config not found; defconfig will be used"
fi

if [[ -f "${HID_DRIVER}" ]]; then
    require_file "${HID_DRIVER_PATCH}"
    if [[ -f "${HID_DRIVER_BACKUP}" ]]; then
        echo "Kernel HID driver already injected: ${HID_DRIVER_REL}"
    else
        mkdir -p "$(dirname "${HID_DRIVER_BACKUP}")"
        cp -a "${HID_DRIVER}" "${HID_DRIVER_BACKUP}"
        if ! patch -d "${SDK}" -p1 --forward --batch < "${HID_DRIVER_PATCH}"; then
            cp -f "${HID_DRIVER_BACKUP}" "${HID_DRIVER}"
            rm -f "${HID_DRIVER_BACKUP}"
            die "failed to inject Windows HID control-request support"
        fi
        echo "Kernel HID driver injected: ${HID_DRIVER_REL}"
    fi
else
    warn_skip "kernel HID driver source not found"
fi
