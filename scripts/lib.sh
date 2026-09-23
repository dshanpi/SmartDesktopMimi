#!/usr/bin/env bash
# shellcheck shell=bash
# 共享提示层：统一各脚本的错误提示、前置检查、联网步骤包装。
#
# 设计原则：
#   - 只声明函数，不在文件顶层执行有副作用的代码（被 source 时不干扰调用方）。
#   - 函数用 ${VAR:-} 兼容调用方的 set -u。
#   - 错误提示统一用两层：`error: <what>` + 缩进的 `<how to fix>`，便于用户定位。
#
# 用法：在各脚本顶部（SELF_DIR 定义之后）加：
#   # shellcheck source=scripts/lib.sh
#   source "${SELF_DIR}/lib.sh"

# die <msg> [hint...]
#   打印 error: <msg>，后跟每行缩进的 hint，然后 exit 1。
#   示例：die "missing product artifact: $f" "run: ./scripts/build_apps.sh ${SDK}"
die() {
    local msg="$1"; shift
    echo "error: ${msg}" >&2
    local hint
    for hint in "$@"; do
        echo "       ${hint}" >&2
    done
    exit 1
}

# warn_skip <msg>
#   软告警，打印 SKIP <msg>，不退出（用于可选步骤缺失）。
warn_skip() {
    echo "SKIP ${1}" >&2
}

# require_cmd <cmd> <install_hint>
#   检查命令是否存在，缺则 die 提示安装方式。
#   示例：require_cmd git "apt install git"
require_cmd() {
    local cmd="$1"
    local hint="$2"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        die "missing command: ${cmd}" "请安装：${hint}"
    fi
}

# require_file <path> [hint...]
#   检查文件存在，缺则 die（可附修复建议）。
require_file() {
    local path="$1"; shift
    if [[ ! -f "${path}" ]]; then
        die "missing file: ${path}" "$@"
    fi
}

# require_dir <path> [hint...]
#   检查目录存在，缺则 die（可附修复建议）。
require_dir() {
    local path="$1"; shift
    if [[ ! -d "${path}" ]]; then
        die "missing directory: ${path}" "$@"
    fi
}

# run_network_step <description> -- <cmd...>
#   包装联网命令（git clone/fetch/submodule 等）。执行前打印进度与"需联网"，
#   失败时 die 给出网络排错模板（不自动重试）。
#   示例：run_network_step "克隆 platform（约 2.5G，较慢）" -- git clone --branch foo url dir
run_network_step() {
    local description="$1"
    shift
    if [[ "${1:-}" != "--" ]]; then
        die "run_network_step: 第二个参数必须是 '--' 分隔符，实际: ${1:-（空）}"
    fi
    shift  # 吃掉 '--'
    [[ $# -gt 0 ]] || die "run_network_step: '--' 后未给命令"

    echo "==> ${description}（需联网）"
    if ! "$@"; then
        die "${description} 失败" \
            "检查网络/代理/凭据是否可达（如 git ls-remote 验证仓库连通性）" \
            "若是半成品残留导致重跑失败，用对应脚本的 --clean 重建" \
            "可暂时跳过：prepare 用 --no-platform（platform/LINUX 已就位时）或 --no-export（venv 已就位时）"
    fi
}

# sync_product_packages <product-root> <sdk-root>
#   Mirror product-owned OpenWrt package definitions into Tina. --delete is
#   intentional: removed init files or package assets must not survive in the
#   SDK and silently reappear in a later firmware.
sync_product_packages() {
    local product_root="$1"
    local sdk_root="$2"
    local package_src package_name package_dst

    require_cmd rsync "apt install rsync"
    for package_src in "${product_root}"/packaging/aitvbox-*; do
        [[ -d "${package_src}" ]] ||
            die "no AITVBox package definitions under ${product_root}/packaging"
        package_name="$(basename "${package_src}")"
        package_dst="${sdk_root}/openwrt/package/allwinner/custom/${package_name}"
        echo "SYNC product package: ${package_src} -> ${package_dst}"
        mkdir -p "${package_dst}"
        rsync -a --delete --exclude='.git' "${package_src}/" "${package_dst}/"
    done
}
