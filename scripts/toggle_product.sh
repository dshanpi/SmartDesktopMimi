#!/usr/bin/env bash
# 切换 SDK 中全部 AITVBox 产品包的开关状态。
#   on  : suite/platform/usb-hid/ipkvm=y          （产品固件编译）
#   off : suite/platform/usb-hid/ipkvm is not set （纯 SDK 编译，不碰产品代码）
#
# 三处配置同步修改：defconfig（种子）+ .config（生效）+ out/.../tmp/.config（构建读取）。
# 注意：openwrt_rootfs 不会重建 .config，但 menuconfig 会用 defconfig 覆盖 .config，
# 所以 defconfig 必须同步改，否则下次 menuconfig 会冲掉。
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ACTION="${1:-}"
SDK="${2:-}"

usage() {
    echo "usage: $0 <on|off> <sdk-root>" >&2
    exit 2
}

[[ "$ACTION" == "on" || "$ACTION" == "off" ]] || usage
[[ -n "$SDK" ]] || usage
[[ -d "$SDK/.repo" ]] || { echo "error: not a Tina SDK root: $SDK" >&2; exit 1; }

KEYS=(
    "CONFIG_PACKAGE_aitvbox-suite"
    "CONFIG_PACKAGE_aitvbox-platform"
    "CONFIG_PACKAGE_aitvbox-usb-hid"
    "CONFIG_PACKAGE_aitvbox-ipkvm"
)

set_config() {
    local cfg="$1"
    local key="$2"
    local new old

    [[ -f "$cfg" ]] || { echo "SKIP missing config: $cfg"; return; }
    if [[ "$ACTION" == "on" ]]; then
        new="${key}=y"
        old="# ${key} is not set"
    else
        new="# ${key} is not set"
        old="${key}=y"
    fi

    if grep -q "^${new}$" "$cfg"; then
        echo "OK   already $ACTION: $key in $cfg"
        return
    fi
    if grep -q "^${old}$" "$cfg"; then
        sed -i "s|^${old}$|${new}|" "$cfg"
        echo "SET  $ACTION: $key in $cfg"
    elif grep -q "^${key}=" "$cfg"; then
        # 形态异常（如 =m），整行替换
        sed -i "s|^${key}=.*|${new}|" "$cfg"
        echo "SET  $ACTION (replaced): $key in $cfg"
    else
        printf '\n%s\n' "$new" >> "$cfg"
        echo "ADD  $ACTION: $key in $cfg"
    fi
}

for cfg in \
    "$SDK/openwrt/target/a133/a133-b6/defconfig" \
    "$SDK/openwrt/openwrt/.config" \
    "$SDK/out/a133/b6/openwrt/tmp/.config"; do
    for key in "${KEYS[@]}"; do
        set_config "$cfg" "$key"
    done
done

echo "AITVBox product packages are now $ACTION."
if [[ "$ACTION" == "off" ]]; then
    echo "Now you can build a pure SDK rootfs without touching product code:"
    echo "  cd $SDK && source build/envsetup.sh && ./build.sh openwrt_rootfs"
    if [[ -x "${SELF_DIR}/clean_sdk_product_artifacts.sh" ]]; then
        "${SELF_DIR}/clean_sdk_product_artifacts.sh" "${SDK}"
    fi
fi
