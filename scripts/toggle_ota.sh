#!/usr/bin/env bash
# 切换 SDK 中 SWUpdate OTA 相关包的开关状态。
#   on  : CONFIG_PACKAGE_swupdate=y + ota-burnboot=y + uboot-envtools=y（启用 A/B OTA 运行时）
#   off : 三者 # ... is not set（纯 SDK，不含 OTA 运行时）
#
# 与 toggle_product.sh 同构：三处配置同步修改
#   defconfig（种子）+ .config（生效）+ out/.../tmp/.config（构建读取）。
#
# 注意：本脚本只管 OpenWrt 包级开关（CONFIG_PACKAGE_*）。swupdate 包内子选项
# （签名/uboot/awboot/libconfig 等 SWUPDATE_CONFIG_*）属 swupdate 自有 Kconfig，
# 不在 OpenWrt defconfig 树内——直接写 OpenWrt defconfig 可能被 make oldconfig 丢弃。
# 子选项在首次开包后由 swupdate 的 Build/Configure 读包内配置决定，落地方式以
# 实际构建验证为准（见 docs/ota.md「swupdate 子选项」）。Phase 1 先靠包默认值跑通。
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

# 需要同步开关的包级 KEY 列表。
KEYS=(
    "CONFIG_PACKAGE_swupdate"
    "CONFIG_PACKAGE_ota-burnboot"
    "CONFIG_PACKAGE_uboot-envtools"   # 提供 fw_printenv/fw_setenv，service_ota 读写 env 用
)

# swupdate 包内子选项（CONFIG_SWUPDATE_CONFIG_*）。必须显式设，原因：
#   1. Makefile 的条件 DEPENDS（+SWUPDATE_CONFIG_LIBCONFIG:libconfig 等）按这些
#      symbol 触发——不设则依赖库（libconfig/libopenssl/uboot-envtools）不会被
#      先构建，swupdate 编译到 #include <libconfig.h> 即 fatal error。
#   2. Build/Configure grep 这些 symbol 写进 swupdate 自身 .config（SWUPDATE_CUSTOM
#      默认 y → SWUPDATE_SYM=CONFIG）。不设则走 swupdate 内部 Kconfig 默认，与
#      OpenWrt 侧 DEPENDS 不一致。
# 故"靠包默认值"不可行（实测 libconfig.h 缺失）。此处显式开启签名 A/B 所需全集。
SWUPDATE_SUBOPTIONS=(
    "CONFIG_SWUPDATE_CUSTOM"                # 开 CUSTOM → 用 CONFIG_* 集（默认即 y，显式确保）
    "CONFIG_SWUPDATE_CONFIG_LIBCONFIG"      # sw-description 解析器，拉 libconfig
    "CONFIG_SWUPDATE_CONFIG_UBOOT"          # bootenv fw_env 接口库，拉 uboot-envtools
    "CONFIG_SWUPDATE_CONFIG_BOOTLOADERHANDLER"  # bootenv handler：解析 sw-description 的 bootenv: 段并写 env（A/B 切槽关键）
    "CONFIG_SWUPDATE_CONFIG_RAW"            # raw 镜像 handler（kernel/rootfs 裸写）
    "CONFIG_SWUPDATE_CONFIG_GUNZIP"         # 解压
    "CONFIG_SWUPDATE_CONFIG_SCRIPTS"        # pre/postinstall 脚本
    "CONFIG_SWUPDATE_CONFIG_SSL_IMPL_OPENSSL"  # 签名 SSL 后端，拉 libopenssl
    "CONFIG_SWUPDATE_CONFIG_SIGNED_IMAGES"  # 签名校验（select HASH_VERIFY）
    "CONFIG_SWUPDATE_CONFIG_SIGALG_RAWRSA"  # RSA SHA256 签名算法（与 build_swu.sh 一致）
    "CONFIG_SWUPDATE_CONFIG_HASH_VERIFY"    # 哈希校验
)
# 注：子选项带 depends/select（如 SIGNED_IMAGES 依赖 SSL_IMPL_OPENSSL、select
# HASH_VERIFY）。全设上后由 ./build.sh 的 make oldconfig 解析 select/depends，
# 写进 defconfig 即可被保留（这些 symbol 经 Config.in source 注册进 OpenWrt Kconfig）。

if [[ "$ACTION" == "on" ]]; then
    SUFFIX="=y"
    UNSET_FORM="# %s is not set"
else
    SUFFIX=""
fi

set_config() {
    local cfg="$1" key="$2"
    [[ -f "$cfg" ]] || { echo "SKIP missing config: $cfg"; return; }
    local new old
    if [[ "$ACTION" == "on" ]]; then
        new="${key}=y"
        old="# ${key} is not set"
    else
        new="# ${key} is not set"
        old="${key}=y"
    fi
    if grep -q "^${new}$" "$cfg"; then
        echo "OK   already $ACTION: ${key} @ ${cfg##*/}"
        return
    fi
    if grep -q "^${old}$" "$cfg"; then
        sed -i "s|^${old}$|${new}|" "$cfg"
        echo "SET  $ACTION: ${key} @ ${cfg##*/}"
    elif grep -q "^${key}" "$cfg"; then
        # 形态异常（如 =m），整行替换
        sed -i "s|^${key}=.*|${new}|" "$cfg"
        echo "SET  $ACTION (replaced): ${key} @ ${cfg##*/}"
    else
        printf '\n%s\n' "$new" >> "$cfg"
        echo "ADD  $ACTION: ${key} @ ${cfg##*/}"
    fi
}

for key in "${KEYS[@]}"; do
    set_config "$SDK/openwrt/target/a133/a133-b6/defconfig" "$key"
    set_config "$SDK/openwrt/openwrt/.config" "$key"
    set_config "$SDK/out/a133/b6/openwrt/tmp/.config" "$key"
done

# swupdate 子选项仅在 swupdate 包启用时设；关闭时一并清，保持 SDK 纯态。
if [[ "$ACTION" == "on" ]]; then
    for key in "${SWUPDATE_SUBOPTIONS[@]}"; do
        set_config "$SDK/openwrt/target/a133/a133-b6/defconfig" "$key"
        set_config "$SDK/openwrt/openwrt/.config" "$key"
        set_config "$SDK/out/a133/b6/openwrt/tmp/.config" "$key"
    done
else
    for key in "${SWUPDATE_SUBOPTIONS[@]}"; do
        set_config "$SDK/openwrt/target/a133/a133-b6/defconfig" "$key"
        set_config "$SDK/openwrt/openwrt/.config" "$key"
        set_config "$SDK/out/a133/b6/openwrt/tmp/.config" "$key"
    done
fi

echo "swupdate OTA packages are now $ACTION."
if [[ "$ACTION" == "on" ]]; then
    echo "已开启 swupdate 包内子选项（LIBCONFIG/UBOOT/RAW/签名），由 build.sh oldconfig 解析 select/depends。"
fi
