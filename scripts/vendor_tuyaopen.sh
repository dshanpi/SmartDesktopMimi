#!/usr/bin/env bash
# vendor_tuyaopen.sh — 一次性把 TuyaOpen 源码 + platform/LINUX + python 工具链
# vendor 成纯源码树（删 .git），提交进产品仓 third_party/TuyaOpen/，使构建全离线、
# 可复现。运行一次即可；升级 TuyaOpen 时重新运行。
#
# 来源（均为本地，不联网）：
#   - TuyaOpen 主源：third_party/TuyaOpen submodule 工作区（pre-patch，不含设备凭据）
#   - 内嵌子模块（FlashDB/littlefs/cJSON/backoff）：submodule 工作区已 checkout，作为普通目录
#   - platform/LINUX：.tuyaopen-build/platform/LINUX（先 git checkout . 还原 platform 补丁→干净）
#   - .tools（uv 0.11.18 + cpython 3.12.13）：.tuyaopen-build/.tools（可移植二进制）
#   - uv-cache（可选，--with-uv-cache）：~/.cache/uv → 100% 离线 uv sync
#
# 产物布局见 .claude/plans 的 vendored 布局。产物不含 .git、不含真实凭据、不含已打补丁。
# ⚠️ 警告：本脚本产出「不含已打补丁」的干净源，会覆盖 third_party/TuyaOpen/ 里已固化的
# 定制（0001/0002 与 platform 补丁，见 integrations/tuyaopen/patches-archive/）。
# 仅在升级 TuyaOpen 上游时运行！运行后须按 patches-archive/README.md 重新 apply 定制。
# 安全网：定制已 git 跟踪，re-vendor 后 `git diff third_party/TuyaOpen` 能看到全部丢失，可恢复。
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
SUBMODULE_DIR="${PRODUCT_ROOT}/third_party/TuyaOpen"
BUILD_ROOT="${TUYAOPEN_BUILD_ROOT:-${PRODUCT_ROOT}/.tuyaopen-build}"
STAGING="$(mktemp -d -t tuyaopen-vendor-XXXXXX)"
WITH_UV_CACHE=0

# shellcheck source=scripts/lib.sh
source "${SELF_DIR}/lib.sh"

usage() {
    cat >&2 <<USAGE
usage: $0 [options]

把 TuyaOpen 源码 + platform/LINUX + .tools vendor 进 third_party/TuyaOpen/（替换 submodule）。
Options:
  --with-uv-cache      额外 vendor ~/.cache/uv（~426M）→ 全新机器 100% 离线 uv sync
  --build-root <path>  .tuyaopen-build 路径（默认 ${BUILD_ROOT}）
  -h, --help
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-uv-cache) WITH_UV_CACHE=1; shift ;;
        --build-root) BUILD_ROOT="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

require_cmd rsync "apt install rsync"
require_cmd git "apt install git"

# 前置输入检查
require_file "${SUBMODULE_DIR}/tos.py" \
    "TuyaOpen submodule 未 checkout：git submodule update --init third_party/TuyaOpen"
require_file "${SUBMODULE_DIR}/export.sh" "同上：submodule 未 init"
require_dir "${BUILD_ROOT}/platform/LINUX/tuyaos_adapter" \
    ".tuyaopen-build/platform/LINUX 缺失：先跑一次旧 prepare/build_apps 生成"
require_dir "${BUILD_ROOT}/.tools/python" ".tuyaopen-build/.tools 缺失"
require_dir "${BUILD_ROOT}/.tools/uv" ".tuyaopen-build/.tools/uv 缺失"

# 内嵌子模块须已 checkout（作为普通目录）
for sm in src/tal_kv/FlashDB src/tal_kv/littlefs src/libcjson/cJSON src/common/backoffAlgorithm; do
    require_dir "${SUBMODULE_DIR}/${sm}" \
        "内嵌子模块未 checkout：cd ${SUBMODULE_DIR} && git submodule update --init"
done

echo "==> [1/6] 暂存 TuyaOpen 主源（pre-patch，剔除 .git）→ ${STAGING}"
rsync -a --exclude='.git' --exclude='.venv' "${SUBMODULE_DIR}/" "${STAGING}/"

echo "==> [2/6] 还原 platform/LINUX 补丁并暂存（干净 pre-patch 源）"
# platform 补丁是 .tuyaopen-build/platform/LINUX 的工作区改动，checkout 还原。
if git -C "${BUILD_ROOT}/platform/LINUX" diff --quiet 2>/dev/null; then
    echo "    platform/LINUX 无工作区改动（已是干净态）"
else
    git -C "${BUILD_ROOT}/platform/LINUX" checkout -- .
    git -C "${BUILD_ROOT}/platform/LINUX" clean -fd >/dev/null
    echo "    platform/LINUX 补丁已还原"
fi
mkdir -p "${STAGING}/platform/LINUX"
rsync -a --exclude='.git' "${BUILD_ROOT}/platform/LINUX/" "${STAGING}/platform/LINUX/"

echo "==> [3/6] 暂存 .tools（uv + python，可移植二进制；跳过冗余 archives）"
mkdir -p "${STAGING}/.tools"
rsync -a "${BUILD_ROOT}/.tools/uv/" "${STAGING}/.tools/uv/"
rsync -a "${BUILD_ROOT}/.tools/python/" "${STAGING}/.tools/python/"

if [[ ${WITH_UV_CACHE} -eq 1 ]]; then
    if [[ -d "${HOME}/.cache/uv" ]]; then
        echo "==> [3b] 暂存 uv 缓存（~426M，100% 离线 uv sync）"
        mkdir -p "${STAGING}/.tools/uv-cache"
        rsync -a "${HOME}/.cache/uv/" "${STAGING}/.tools/uv-cache/"
    else
        echo "    [SKIP] 无 ~/.cache/uv，跳过 uv-cache（首次 uv sync 需联网）"
    fi
fi

# 暂存校验
require_file "${STAGING}/tos.py" "暂存失败：tos.py 缺失"
require_dir "${STAGING}/platform/LINUX/tuyaos_adapter" "暂存失败：platform/LINUX 缺失"
require_dir "${STAGING}/.tools/python" "暂存失败：.tools/python 缺失"
echo "    暂存体积：$(du -sh "${STAGING}" | cut -f1)"

echo "==> [4/6] 移除 TuyaOpen submodule（deinit + rm gitlink）"
git -C "${PRODUCT_ROOT}" submodule deinit -f third_party/TuyaOpen
git -C "${PRODUCT_ROOT}" rm -f third_party/TuyaOpen
rm -rf "${PRODUCT_ROOT}/.git/modules/third_party/TuyaOpen"

echo "==> [5/6] 落地 vendored 树到 third_party/TuyaOpen/"
# git rm 后目录已空/不存在，重建。
mkdir -p "${PRODUCT_ROOT}/third_party/TuyaOpen"
rsync -a "${STAGING}/" "${PRODUCT_ROOT}/third_party/TuyaOpen/"
rm -rf "${STAGING}"

echo "==> [6/6] 清理 .gitmodules 的 TuyaOpen 段（保留 lvgl 段）"
GITMODULES="${PRODUCT_ROOT}/.gitmodules"
if [[ -f "${GITMODULES}" ]] && grep -q 'path = third_party/TuyaOpen' "${GITMODULES}"; then
    # 删除 [submodule "...TuyaOpen"] 到下一个 [submodule 或文件尾之间的行
    python3 - "${GITMODULES}" <<'PY'
import sys, re
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
# 删除以 [submodule "...TuyaOpen"] 开头的段，直到下一个 [ 开头的段或文末
s = re.sub(r'\[submodule "[^"]*TuyaOpen[^"]*"\]\n(?:[^\[]*?)(?=\[|\Z)', '', s, flags=re.S)
# 清理多余空行
s = re.sub(r'\n{3,}', '\n\n', s).strip() + '\n'
open(p, "w", encoding="utf-8").write(s)
print("    .gitmodules 已移除 TuyaOpen 段")
PY
else
    echo "    .gitmodules 无 TuyaOpen 段（跳过）"
fi

echo
echo "==> vendor 完成。落地：third_party/TuyaOpen/ 体积 $(du -sh "${PRODUCT_ROOT}/third_party/TuyaOpen" | cut -f1)"
echo "    下一步：git add third_party/TuyaOpen .gitmodules（勿提交设备凭据/构建产物）"
echo "    验证：产品 app 不应定义 TUYA_OPENSDK_UUID/TUYA_OPENSDK_AUTHKEY"
