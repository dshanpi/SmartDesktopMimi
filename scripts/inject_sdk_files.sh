#!/usr/bin/env bash
# inject_sdk_files.sh — A/B OTA 地基文件的覆盖式注入/还原。
#
# 把产品层准备好的 A/B 分区表、env、U-Boot bootcount 配置注入 SDK，构建后由
# build_firmware.sh 的 trap 调 revert 还原 SDK 原件。遵循"不污染 SDK"原则：
#   - 整文件覆盖类：cp -a 备份 SDK 原件到产品仓库 build/.sdk-file-backups/，
#     再拷入产品成品；revert 时从备份还原。
#   - 标记块追加类（defconfig 片段）：用 marker 注释包裹追加；revert 时按 marker 删除。
#
# 备份目录在产品仓库 build/ 下（已 gitignore），与构建产物同生命周期。
# 覆盖式而非 git patch：SDK 子仓库已有零散脏改且 overlay 非 git 仓，patch 不可靠。
#
# 用法：
#   inject_sdk_files.sh apply  <sdk-root>   # 注入（构建前）
#   inject_sdk_files.sh revert <sdk-root>   # 还原（trap/构建后）
set -euo pipefail

ACTION="${1:-}"
SDK="${2:-}"
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

FILES_DIR="${PRODUCT_ROOT}/integrations/swupdate/files"
BACKUP_DIR="${PRODUCT_ROOT}/build/.sdk-file-backups"

usage() {
    cat >&2 <<USAGE
usage: $0 <apply|revert> <sdk-root>

Inject or restore A/B OTA foundation files into the Tina SDK.
  apply   Back up SDK originals, copy in product A/B files (partition/env),
          append bootcount fragment to U-Boot defconfig.
  revert  Restore SDK originals from backup, remove bootcount fragment.
USAGE
}

[[ "$ACTION" == "apply" || "$ACTION" == "revert" ]] || { usage; exit 2; }
[[ -n "$SDK" ]] || { usage; exit 2; }
[[ -d "$SDK/.repo" ]] || die "not a Tina SDK root: ${SDK}" "SDK 路径错或缺 .repo"

# 整文件覆盖清单：成品路径 => SDK 目标路径
declare -A OVERLAY_FILES=(
    ["${FILES_DIR}/sys_partition.fex"]="${SDK}/device/config/chips/a133/configs/b6/linux-4.9/sys_partition.fex"
    ["${FILES_DIR}/env.cfg"]="${SDK}/device/config/chips/a133/configs/b6/linux-4.9/env.cfg"
    ["${FILES_DIR}/fw_env.config"]="${SDK}/openwrt/target/a133/a133-common/base-files/etc/fw_env.config"
    # 修复蓝牙启动 rfkill 硬编码 bug：aidesktop overlay 原版 reset_bt_state 写死
    # rfkill3（实为 hci0 起来后的附属，AIC 蓝牙真身是 name=aic-bt）。频繁 A/B 切槽
    # 重启后 hciattach 握手失败、hci0 未建时 rfkill3 不存在 → 断电-上电复位被跳过
    # → bring up hci0 failed（软重启救不回，冷启动才行）。覆盖为按 name=aic-bt
    # 动态查找 AIC rfkill 做硬复位，容错逻辑（断电-上电 + 重试）沿用原版。
    #
    # 注意：aidesktop 有两份 bluetooth_init 副本，进 rootfs 的是
    #   1) openwrt/target/a133/a133-b6/base-files/etc/init.d/bluetooth_init  ← 真正进 rootfs 的源（base-files 包 install 用这份）
    #   2) a133-tina-aidesktop/overlay/.../bluetooth_init                     ← overlay 副本（手动 apply_overlay.sh 会覆盖 #1）
    # OVERLAY_FILES 只注册 #1（走 do_overlay_file 的备份/还原机制，确保 trap 还原）。
    # #2 + 各 rootfs 打包缓存（ipkg/root.orig/staging/root-a133-b6）由下方 BT_CACHEDIRS
    # 覆盖（这些是构建产物会被重建，无需备份还原）。详见下方 BT_CACHEDIRS 注释。
    ["${FILES_DIR}/bluetooth_init"]="${SDK}/openwrt/target/a133/a133-b6/base-files/etc/init.d/bluetooth_init"
)

# 标记块追加清单：片段路径 => SDK 目标 defconfig
# 片段内容必须用 marker 行包裹（见片段文件），revert 按 marker 删除。
declare -A FRAGMENT_FILES=(
    ["${FILES_DIR}/sun50iw10p1_tina_defconfig.ab_bootcount.fragment"]="${SDK}/brandy/brandy-2.0/u-boot-2018/configs/sun50iw10p1_tina_defconfig"
)

BOOTCOUNT_MARKER="AITVBOX_OTA_BOOTCOUNT"

# 整文件覆盖：apply 备份+拷入，revert 还原。
do_overlay_file() {
    local src="$1" dst="$2"
    local rel="${dst#${SDK}/}"
    local bkup="${BACKUP_DIR}/${rel}"

    if [[ "$ACTION" == "apply" ]]; then
        require_file "$src" "产品成品缺失：$src"
        require_file "$dst" "SDK 目标文件不存在（SDK 路径或版本变了？）：$dst"
        mkdir -p "$(dirname "$bkup")"
        # 已有备份则不覆盖（说明上次 apply 未还原，幂等保护）
        if [[ -e "$bkup" ]]; then
            echo "SKIP backup exists (上次 apply 未还原？复用备份): $rel"
        else
            cp -a "$dst" "$bkup"
        fi
        cp -f "$src" "$dst"
        echo "OVLY apply: $rel"
    else
        if [[ -e "$bkup" ]]; then
            cp -f "$bkup" "$dst"
            rm -f "$bkup"
            echo "OVLY revert: $rel"
        else
            warn_skip "no backup to restore (未注入过？): $rel"
        fi
    fi
}

# 标记块追加：apply 检查 marker 不存在则追加，revert 按 marker 删除区间。
do_fragment_file() {
    local frag="$1" dst="$2"
    local rel="${dst#${SDK}/}"

    if [[ "$ACTION" == "apply" ]]; then
        require_file "$frag" "片段缺失：$frag"
        require_file "$dst" "SDK 目标文件不存在：$dst"
        if grep -q "${BOOTCOUNT_MARKER}" "$dst"; then
            echo "FRAG already applied: $rel"
            return
        fi
        printf '\n' >> "$dst"
        cat "$frag" >> "$dst"
        echo "FRAG apply: $rel"
    else
        if ! grep -q "${BOOTCOUNT_MARKER}" "$dst"; then
            warn_skip "marker not found (未注入过？): $rel"
            return
        fi
        # 删除从含 marker 的 begin 行到含 marker 的 end 行的整段（含两端及中间所有行）。
        # 片段首尾行各含 marker；中间行无 marker，靠地址范围一并删除。
        sed -i "/${BOOTCOUNT_MARKER}/,/${BOOTCOUNT_MARKER}/d" "$dst"
        # 清理可能留下的连续空行尾巴
        sed -i -e :a -e '/^\n*$/{$d;N;ba}' "$dst"
        echo "FRAG revert: $rel"
    fi
}

echo "==> inject_sdk_files ${ACTION} (sdk=${SDK})"
for src in "${!OVERLAY_FILES[@]}"; do
    do_overlay_file "$src" "${OVERLAY_FILES[$src]}"
done
for src in "${!FRAGMENT_FILES[@]}"; do
    do_fragment_file "$src" "${FRAGMENT_FILES[$src]}"
done

# bluetooth_init 进 rootfs 的缓存覆盖由 build_firmware.sh 在 build.sh 之后、pack 之前
# 做（inject apply 在 build.sh 之前覆盖会被 build.sh 重建冲掉，实测无效）。此处不管。
# aidesktop overlay 副本(a133-tina-aidesktop/overlay/.../bluetooth_init)不进 rootfs
# （仅手动 apply_overlay.sh 时用），无需在此覆盖；OVERLAY_FILES 只管真正进 rootfs
# 的 openwrt/target/.../base-files/ 源。

if [[ "$ACTION" == "apply" ]]; then
    echo "A/B OTA foundation files injected."
    echo "提示：构建后由 build_firmware.sh 的 trap 自动 revert；手动还原用：$0 revert ${SDK}"
else
    # 还原后清掉空备份目录树（rmdir 只删顶层，残留 device/config/... 空目录）
    find "$BACKUP_DIR" -depth -type d -empty -delete 2>/dev/null || true
    echo "A/B OTA foundation files reverted. SDK 恢复单 rootfs 原态。"
fi
