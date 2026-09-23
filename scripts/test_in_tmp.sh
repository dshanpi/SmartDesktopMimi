#!/usr/bin/env bash
# 移植性冒烟测试：把产品仓库的脚本、打包规则、文档拷到 /tmp 的另一路径，
# 在那里做语法检查 + 审计脚本的路径自洽校验，确认不依赖产品仓库的绝对路径。
#
# 为什么需要：本架构要求"clone 即编"，脚本必须用相对自身路径定位资源，
# 不能硬编码某个开发机 checkout 路径。本测试把仓库挪到 /tmp 任意路径，
# 如果脚本仍正常，说明没有路径泄漏。
#
# 本测试是轻量的：不拷贝 .tuyaopen-build/、build/、.venv/ 等大目录，
# 也不重跑完整构建（那是 build_apps.sh / build_firmware.sh 的职责）。
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
SDK="${1:-}"
TMP_ROOT="${AITVBOX_TEST_ROOT:-/tmp/aitvbox-product-portability-test}"
DEST="${TMP_ROOT}/product"

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

if [[ -e "${DEST}" ]]; then
    echo "error: destination already exists: ${DEST}" >&2
    echo "choose a different AITVBOX_TEST_ROOT; this script never deletes old test data" >&2
    exit 1
fi

mkdir -p "${DEST}"

# 只拷轻量内容：脚本、打包规则、文档、配置。排除大目录与构建产物。
for sub in scripts packaging integrations docs; do
    if [[ -d "${PRODUCT_ROOT}/${sub}" ]]; then
        (cd "${PRODUCT_ROOT}/${sub}" \
            && tar --exclude=.git --exclude='*.pyc' -cpf - .) \
            | (mkdir -p "${DEST}/${sub}" && cd "${DEST}/${sub}" && tar -xpf -)
    fi
done

cp "${PRODUCT_ROOT}/README.md" "${DEST}/README.md" 2>/dev/null || true
cp "${PRODUCT_ROOT}/.gitignore" "${DEST}/.gitignore" 2>/dev/null || true
[[ -f "${PRODUCT_ROOT}/.gitmodules" ]] && cp "${PRODUCT_ROOT}/.gitmodules" "${DEST}/.gitmodules"

echo "== 临时副本就位: ${DEST}"
echo

echo "== [1/2] 语法检查所有脚本（在临时路径下）"
fail=0
while IFS= read -r script; do
    if ! bash -n "${script}"; then
        echo "  SYNTAX FAIL: ${script}"
        fail=1
    fi
done < <(find "${DEST}/scripts" -type f -name '*.sh' | sort)
if [[ ${fail} -ne 0 ]]; then
    echo "语法检查失败"
    exit 1
fi
echo "  全部通过"
echo

echo "== [2/2] 在临时路径跑 audit_sdk.sh（验证脚本用相对自身路径定位 PRODUCT_ROOT）"
# audit_sdk.sh 用 BASH_SOURCE 定位 PRODUCT_ROOT，挪到 /tmp 后应仍指向临时副本根。
REPORT_OUT="$("${DEST}/scripts/audit_sdk.sh" "${SDK}" 2>&1 | tee /dev/stderr || true)"
# 抓取报告目录行
REPORT_DIR_LINE="$(echo "${REPORT_OUT}" | grep '^Audit report written to:' || true)"
if [[ -z "${REPORT_DIR_LINE}" ]]; then
    echo "audit_sdk.sh 在临时路径下未能产出报告 —— 可能存在硬编码路径"
    exit 1
fi
REPORT_DIR="${REPORT_DIR_LINE#Audit report written to: }"
SUMMARY="${REPORT_DIR}/summary.txt"

# 关键断言：audit 报告里的 PRODUCT_ROOT 必须是临时路径，而非原始 checkout。
if grep -q "^PRODUCT_ROOT=${DEST}$" "${SUMMARY}"; then
    echo "  OK   audit_sdk.sh 正确以临时路径为 PRODUCT_ROOT（无路径泄漏）"
else
    echo "  FAIL audit_sdk.sh 的 PRODUCT_ROOT 未指向临时路径，存在硬编码："
    grep '^PRODUCT_ROOT=' "${SUMMARY}"
    exit 1
fi

echo
echo "移植性冒烟测试通过：脚本可换目录运行，无硬编码产品仓库绝对路径。"
echo "临时副本保留在: ${DEST}（手动清理）"
