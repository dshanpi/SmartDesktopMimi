#!/usr/bin/env bash
# build_swu.sh — 生成 AITVBox A/B 全量+签名 OTA 升级包（.swu）。
#
# 依赖：先跑 build_apps.sh（应用产物）+ build_firmware.sh（rootfs.img/boot.img）。
# 本脚本在 SDK trap 窗口内注入签名密钥/自定义 sw-description/签名配置，调用
# SDK 的 swupdate_pack_swu 生成 .swu，产物拷回产品仓库 build/swu/，trap 还原 SDK。
#
# 签名配置（CONFIG_SWUPDATE_CONFIG_SIGNED_IMAGES/SIGALG_RAWRSA）以文本形式追加到
# OpenWrt defconfig——swupdate_pack_swu 只 grep 这两行文本触发签名，不依赖 OpenWrt
# Kconfig 是否"认"这俩 symbol，故直接写文本即可（marker 包裹，trap 删除）。
#
# 用法：./scripts/build_swu.sh <sdk-root>
set -euo pipefail

SDK="${1:-}"
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

INTEGRATION_DIR="${PRODUCT_ROOT}/integrations/swupdate"
SWU_DESC="${INTEGRATION_DIR}/sw-description-ab-sign-rollback"
SWU_CFG="${INTEGRATION_DIR}/sw-subimgs-ab-sign-rollback.cfg"
PRIV_KEY="${PRODUCT_ROOT}/secrets/swupdate_priv.pem"
PASS_FILE="${PRODUCT_ROOT}/secrets/swupdate_priv.password"
PUB_KEY="${INTEGRATION_DIR}/keys/swupdate_public.pem"

SIGN_MARKER="AITVBOX_SWU_SIGN_CONFIG"

usage() {
    echo "usage: $0 <sdk-root>" >&2
}

if [[ -z "${SDK}" ]]; then
    usage
    exit 2
fi
[[ -d "${SDK}/.repo" ]] || die "not a Tina SDK root: ${SDK}" "SDK 路径错或缺 .repo"

# 前置检查
require_cmd openssl "apt install openssl"
require_file "${SDK}/build/envsetup.sh" "SDK 未初始化：${SDK}"
require_file "${SWU_DESC}" "sw-description 缺失：${SWU_DESC}"
require_file "${SWU_CFG}" "subimgs cfg 缺失：${SWU_CFG}"
require_file "${PRIV_KEY}" "签名私钥缺失：${PRIV_KEY}" "生成密钥见 docs/ota.md「签名密钥」"
require_file "${PASS_FILE}" "私钥口令缺失：${PASS_FILE}"
require_file "${PUB_KEY}" "签名公钥缺失：${PUB_KEY}"

# 板级 swupdate 配置目录（密钥/cfg 注入目标，也是 swupdate_pack_swu 优先查找处）
BOARD_SWU_DIR="${SDK}/openwrt/target/a133/a133-b6/swupdate"
DEFCONFIG="${SDK}/openwrt/target/a133/a133-b6/defconfig"
SWU_OUT_DIR="${SDK}/out/a133/b6/openwrt/swupdate"
EXPECTED_SWU="${SWU_OUT_DIR}/openwrt_a133_b6-ab-sign-rollback.swu"
PRODUCT_SWU_OUT="${PRODUCT_ROOT}/build/swu"

# --- trap 还原：撤销所有 SDK 临时改动 ---
swu_restore() {
    local status=$?
    echo
    echo "Restoring SDK (build_swu)..."
    # 删板级 dir 里我们注入的文件（私钥/口令/sw-description/cfg）
    if [[ -d "${BOARD_SWU_DIR}" ]]; then
        rm -f "${BOARD_SWU_DIR}/swupdate_priv.pem" \
              "${BOARD_SWU_DIR}/swupdate_priv.password" \
              "${BOARD_SWU_DIR}/sw-description-ab-sign-rollback" \
              "${BOARD_SWU_DIR}/sw-subimgs-ab-sign-rollback.cfg"
    fi
    # 删 defconfig 里我们追加的签名配置块
    if [[ -f "${DEFCONFIG}" ]] && grep -q "${SIGN_MARKER}" "${DEFCONFIG}"; then
        sed -i "/${SIGN_MARKER}/,/${SIGN_MARKER}/d" "${DEFCONFIG}"
        sed -i -e :a -e '/^\n*$/{$d;N;ba}' "${DEFCONFIG}"
    fi
    # 删临时压缩的 rootfs.img.gz（不污染 SDK）
    rm -f "${SDK}/out/a133/b6/openwrt/rootfs.img.gz"
    exit "${status}"
}
trap swu_restore EXIT

echo "==> [build_swu] 注入签名密钥与 sw-description 到 SDK 板级 swupdate dir"
mkdir -p "${BOARD_SWU_DIR}"
cp -f "${PRIV_KEY}" "${BOARD_SWU_DIR}/swupdate_priv.pem"
cp -f "${PASS_FILE}" "${BOARD_SWU_DIR}/swupdate_priv.password"
chmod 600 "${BOARD_SWU_DIR}/swupdate_priv.pem" "${BOARD_SWU_DIR}/swupdate_priv.password"
# sw-description 也放一份到 build/swupdate/（cfg 里以该相对路径引用）
mkdir -p "${SDK}/build/swupdate"
cp -f "${SWU_DESC}" "${SDK}/build/swupdate/sw-description-ab-sign-rollback"
# cfg 放板级 dir（swupdate_pack_swu 优先查找板级）
cp -f "${SWU_CFG}" "${BOARD_SWU_DIR}/sw-subimgs-ab-sign-rollback.cfg"

echo "==> [build_swu] 追加签名配置到 defconfig（marker 包裹）"
if ! grep -q "${SIGN_MARKER}" "${DEFCONFIG}"; then
    {
        printf '\n'
        echo "# --- begin ${SIGN_MARKER} ---"
        echo "CONFIG_SWUPDATE_CONFIG_SIGNED_IMAGES=y"
        echo "CONFIG_SWUPDATE_CONFIG_SIGALG_RAWRSA=y"
        echo "# --- end ${SIGN_MARKER} ---"
    } >> "${DEFCONFIG}"
fi

# 校验构建产物就位（boot.img/rootfs.img 由 build_firmware.sh 生成）
for f in "${SDK}/out/a133/b6/openwrt/boot.img" "${SDK}/out/a133/b6/openwrt/rootfs.img"; do
    [[ -f "$f" ]] || die "missing SDK image: $f" "先跑 build_firmware.sh ${SDK}"
done

# --- rootfs 压缩传输（降包大小）：rootfs.img 512M 实际只用 ~100M，剩 ~400M 空
# ext4 块原样打包白传。gzip 压缩后 ~110M。sw-description 里 rootfs 段设
# compressed="zlib"，设备端 CONFIG_GUNZIP=y 流式解压直写分区（installed-directly）。
# subimgs cfg 的 rootfs 行指向此 .gz，包内名仍 rootfs。trap 删 .gz 不污染 SDK。
ROOTFS_IMG="${SDK}/out/a133/b6/openwrt/rootfs.img"
ROOTFS_GZ="${SDK}/out/a133/b6/openwrt/rootfs.img.gz"
echo "==> [build_swu] gzip 压缩 rootfs（降包大小）"
ROOTFS_RAW_SIZE=$(stat -c %s "${ROOTFS_IMG}")
gzip -c "${ROOTFS_IMG}" > "${ROOTFS_GZ}"
ROOTFS_GZ_SIZE=$(stat -c %s "${ROOTFS_GZ}")
echo "    rootfs: ${ROOTFS_RAW_SIZE} -> ${ROOTFS_GZ_SIZE} bytes ($(awk "BEGIN{printf \"%.0f%%\", ${ROOTFS_GZ_SIZE}*100/${ROOTFS_RAW_SIZE}}") of raw)"

echo "==> [build_swu] 调用 swupdate_pack_swu -ab-sign-rollback"
cd "${SDK}"
# SDK 自带 envsetup/quick.sh 非 set -u/-e 安全（quick.sh 用未加引号的 $1）。与
# build_firmware.sh 同策：source envsetup 与调 swupdate_pack_swu 期间临时关闭
# nounset/errexit，靠下方显式 || die 检测真实失败。
set +eu
# shellcheck disable=SC1091
source build/envsetup.sh
swupdate_pack_swu -ab-sign-rollback \
    || die "swupdate_pack_swu 失败" \
           "看上方打包错误输出" \
           "确认 build_firmware.sh 已生成 boot.img/rootfs.img"
set -eu

[[ -f "${EXPECTED_SWU}" ]] || die "swu 产物未生成：${EXPECTED_SWU}" "看上方打包日志"

echo "==> [build_swu] 拷贝产物到产品仓库"
mkdir -p "${PRODUCT_SWU_OUT}"
cp -f "${EXPECTED_SWU}" "${PRODUCT_SWU_OUT}/"
echo
echo "OTA swu 生成完成："
ls -lh "${PRODUCT_SWU_OUT}/openwrt_a133_b6-ab-sign-rollback.swu"
echo
echo "下一步：把 .swu 推到 OTA 服务器；设备端经 LVGL 菜单触发下载与 swupdate -i。"
