#!/usr/bin/env bash
# Smart Desktop Mimi 一键构建入口：自动发现 SDK、检查完整主机依赖和资源，
# 展示目标硬件规格，再交给 build_release.sh 完成正式编译、验收和归档。
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"

PLATFORM="a133"
SDK_OVERRIDE=""
CHECK_ONLY=0
WITH_OTA=0
INCLUDE_SDK_IMAGES=0
ALLOW_DIRTY=0
OUTPUT_DIR=""

usage() {
    cat <<'USAGE'
用法：
  ./scripts/auto_build_package.sh [选项]

默认自动寻找 A133 Tina SDK，然后干净编译全部应用和固件、执行镜像验收，
最后将可交付镜像、日志、构建清单和 SHA256 归档到 build/releases/。

选项：
  --check-only             只检查环境、硬件规格、源码和 SDK，不开始编译
  --sdk-root <目录>        显式指定 Tina SDK；优先级高于环境变量和自动发现
  --platform <平台>        目标平台；当前正式支持 a133（默认）
  --with-ota               同时生成 RSA 签名的 A/B OTA .swu
  --include-sdk-images     归档 boot.img 和 rootfs.img
  --output-dir <目录>      指定发布目录
  --allow-dirty            允许已跟踪源码未提交（正式发布不推荐）
  -h, --help               显示帮助

SDK 自动发现顺序：
  1. --sdk-root
  2. AITVBOX_A133_SDK
  3. AITVBOX_TINA_SDK（兼容旧环境）
  4. /home/ubuntu/A133-Tina5.0-v0.9
  5. 产品仓库同级唯一的 A133-Tina* 目录
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check-only) CHECK_ONLY=1; shift ;;
        --sdk-root)
            [[ $# -ge 2 ]] || { echo "error: --sdk-root 需要目录参数" >&2; exit 2; }
            SDK_OVERRIDE="$2"
            shift 2
            ;;
        --platform)
            [[ $# -ge 2 ]] || { echo "error: --platform 需要平台参数" >&2; exit 2; }
            PLATFORM="$2"
            shift 2
            ;;
        --with-ota) WITH_OTA=1; shift ;;
        --include-sdk-images) INCLUDE_SDK_IMAGES=1; shift ;;
        --output-dir)
            [[ $# -ge 2 ]] || { echo "error: --output-dir 需要目录参数" >&2; exit 2; }
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --allow-dirty) ALLOW_DIRTY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "error: 未知参数：$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "${PLATFORM}" != "a133" ]]; then
    echo "error: ${PLATFORM} 目前只有适配骨架，尚无可发布的自动构建链路" >&2
    echo "       当前可用平台：a133" >&2
    exit 2
fi

declare -a REQUIRED_COMMANDS=(
    git make cmake python3 rsync gawk unsquashfs debugfs blkid sha256sum file
    node npm
)
declare -a MISSING_COMMANDS=()
for command_name in "${REQUIRED_COMMANDS[@]}"; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        MISSING_COMMANDS+=("${command_name}")
    fi
done
if [[ "${WITH_OTA}" -eq 1 ]] && ! command -v openssl >/dev/null 2>&1; then
    MISSING_COMMANDS+=(openssl)
fi

GO_COMMAND="${GO:-}"
if [[ -n "${GO_COMMAND}" ]]; then
    if [[ "${GO_COMMAND}" != */* ]]; then
        GO_COMMAND="$(command -v "${GO_COMMAND}" 2>/dev/null || true)"
    fi
elif [[ -x /usr/local/go/bin/go ]]; then
    GO_COMMAND=/usr/local/go/bin/go
else
    GO_COMMAND="$(command -v go 2>/dev/null || true)"
fi
if [[ -z "${GO_COMMAND}" || ! -x "${GO_COMMAND}" ]]; then
    MISSING_COMMANDS+=(go)
fi

if [[ ${#MISSING_COMMANDS[@]} -gt 0 ]]; then
    echo "环境检查失败，缺少命令：${MISSING_COMMANDS[*]}" >&2
    echo >&2
    echo "Ubuntu/Debian 基础依赖可执行：" >&2
    echo "  sudo apt-get update" >&2
    echo "  sudo apt-get install git make cmake python3 rsync gawk squashfs-tools e2fsprogs util-linux coreutils file nodejs npm" >&2
    if [[ " ${MISSING_COMMANDS[*]} " == *" go "* ]]; then
        echo "Go 请安装后确保 go 位于 PATH，或设置 GO=/绝对路径/go。" >&2
    fi
    exit 2
fi
export GO="${GO_COMMAND}"

valid_sdk() {
    local candidate="$1"
    [[ -d "${candidate}/.repo" &&
       -f "${candidate}/build/envsetup.sh" &&
       -f "${candidate}/build.sh" &&
       -f "${candidate}/.buildconfig" &&
       -d "${candidate}/openwrt/target/a133/a133-b6" ]]
}

select_sdk() {
    local candidate=""
    local source=""
    if [[ -n "${SDK_OVERRIDE}" ]]; then
        candidate="${SDK_OVERRIDE}"
        source="--sdk-root"
    elif [[ -n "${AITVBOX_A133_SDK:-}" ]]; then
        candidate="${AITVBOX_A133_SDK}"
        source="AITVBOX_A133_SDK"
    elif [[ -n "${AITVBOX_TINA_SDK:-}" ]]; then
        candidate="${AITVBOX_TINA_SDK}"
        source="AITVBOX_TINA_SDK"
    elif valid_sdk /home/ubuntu/A133-Tina5.0-v0.9; then
        candidate=/home/ubuntu/A133-Tina5.0-v0.9
        source="默认路径"
    fi

    if [[ -n "${candidate}" ]]; then
        candidate="$(realpath -m "${candidate}")"
        if ! valid_sdk "${candidate}"; then
            echo "error: ${source} 指向的目录不是完整 A133 Tina SDK：${candidate}" >&2
            echo "       需要 .repo、build/envsetup.sh、build.sh、.buildconfig 和 a133-b6 目标配置。" >&2
            exit 2
        fi
        SDK="${candidate}"
        SDK_SOURCE="${source}"
        return
    fi

    local sibling
    declare -a matches=()
    while IFS= read -r -d '' sibling; do
        if valid_sdk "${sibling}"; then
            matches+=("$(realpath "${sibling}")")
        fi
    done < <(find "$(dirname "${PRODUCT_ROOT}")" -mindepth 1 -maxdepth 1 \
        -type d -name 'A133-Tina*' -print0 2>/dev/null)

    if [[ ${#matches[@]} -eq 1 ]]; then
        SDK="${matches[0]}"
        SDK_SOURCE="同级目录自动发现"
    elif [[ ${#matches[@]} -gt 1 ]]; then
        echo "error: 找到多个可用 A133 Tina SDK，拒绝猜测：" >&2
        printf '       %s\n' "${matches[@]}" >&2
        echo "       请使用 --sdk-root 明确选择。" >&2
        exit 2
    else
        echo "error: 未找到完整 A133 Tina SDK" >&2
        echo "       使用 --sdk-root /path/to/sdk，或设置 AITVBOX_A133_SDK。" >&2
        exit 2
    fi
}

select_sdk

declare -a REQUIRED_INPUTS=(
    "${PRODUCT_ROOT}/VERSION"
    "${PRODUCT_ROOT}/apps/lv_port_linux/lvgl/CMakeLists.txt"
    "${PRODUCT_ROOT}/third_party/TuyaOpen/tos.py"
    "${PRODUCT_ROOT}/third_party/TuyaOpen/export.sh"
    "${PRODUCT_ROOT}/third_party/TuyaOpen/.tools/python/3.12.13/cpython-3.12.13-linux-x86_64-gnu/bin/python3.12"
    "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/MNN/libMNN.so"
    "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/MNN/libMNN_Express.so"
    "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/audio_subsys/libaudio_subsys.a"
    "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/opus/libopus.a"
    "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/models/mdtc_chunk_300ms.mnn"
    "${PRODUCT_ROOT}/vendor/tuyaopen-a133-b6-libs/models/tokens.txt"
)
declare -a MISSING_INPUTS=()
for input_path in "${REQUIRED_INPUTS[@]}"; do
    if [[ ! -e "${input_path}" ]]; then
        MISSING_INPUTS+=("${input_path#${PRODUCT_ROOT}/}")
    fi
done
if [[ ${#MISSING_INPUTS[@]} -gt 0 ]]; then
    echo "源码输入检查失败，缺少：" >&2
    printf '  - %s\n' "${MISSING_INPUTS[@]}" >&2
    echo "公开仓库不包含受限 A133_B6 vendor 库；请从合法的内部构建输入恢复后重试。" >&2
    exit 2
fi

echo "Smart Desktop Mimi 自动构建预检"
echo "  产品仓库 : ${PRODUCT_ROOT}"
echo "  目标平台 : ${PLATFORM}"
echo "  Tina SDK : ${SDK}（${SDK_SOURCE}）"
echo "  构建模式 : $([[ ${CHECK_ONLY} -eq 1 ]] && echo 仅检查 || echo 完整编译并打包)"
echo

python3 - "${SDK}" <<'PY'
from __future__ import annotations

from pathlib import Path
import os
import re
import sys

sdk = Path(sys.argv[1]).resolve()
config_path = sdk / ".buildconfig"
values = {}
for raw_line in config_path.read_text(encoding="utf-8").splitlines():
    match = re.fullmatch(r"export\s+([A-Z0-9_]+)=(.*)", raw_line.strip())
    if not match:
        continue
    values[match.group(1)] = match.group(2).strip().strip('"').strip("'")

expected = {
    "LICHEE_PLATFORM": "linux",
    "LICHEE_LINUX_DEV": "openwrt",
    "LICHEE_IC": "a133",
    "LICHEE_BOARD": "b6",
    "LICHEE_ARCH": "arm64",
}
errors = [
    f"{key}={values.get(key, '未设置')}（要求 {required}）"
    for key, required in expected.items()
    if values.get(key) != required
]
toolchain = Path(values.get("LICHEE_TOOLCHAIN_PATH", ""))
compiler = toolchain / "bin/aarch64-linux-gnu-gcc"
try:
    toolchain.relative_to(sdk)
except ValueError:
    errors.append(f"LICHEE_TOOLCHAIN_PATH 不在当前 SDK 内：{toolchain}")
if not compiler.is_file() or not os.access(compiler, os.X_OK):
    errors.append(f"交叉编译器不存在：{compiler}")
if errors:
    print("SDK 目标配置检查失败：", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    raise SystemExit(2)

print("SDK 目标配置")
print("  SoC / 板卡 : A133 / B6")
print("  系统 / 架构: OpenWrt / arm64")
print("  固件变体   : UART0")
print(f"  交叉编译器 : {compiler}")
PY

echo

python3 - "${SDK}" <<'PY'
from __future__ import annotations

import os
import platform
from pathlib import Path
import shutil
import sys

sdk = Path(sys.argv[1])
memory_kib = 0
try:
    for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
        if line.startswith("MemTotal:"):
            memory_kib = int(line.split()[1])
            break
except (OSError, ValueError, IndexError):
    pass
disk = shutil.disk_usage(sdk)
cpu_count = os.cpu_count() or 0
print("构建主机资源")
print(f"  系统       : {platform.system()} {platform.release()} ({platform.machine()})")
print(f"  CPU 线程   : {cpu_count or '未知'}")
print(f"  物理内存   : {memory_kib / 1024 / 1024:.1f} GiB" if memory_kib else "  物理内存   : 未知")
print(f"  SDK盘可用  : {disk.free / 1024**3:.1f} GiB")
print(f"  SDK盘占用率: {(disk.total - disk.free) / disk.total:.0%}")
if cpu_count and cpu_count < 4:
    print("  warning    : 少于 4 个 CPU 线程，完整构建会较慢")
if memory_kib and memory_kib < 8 * 1024 * 1024:
    print("  warning    : 少于 8 GiB 内存，建议增加 swap 或降低并行负载")
if disk.free < 15 * 1024**3:
    print("  error      : SDK 所在磁盘至少需要 15 GiB 可用空间", file=sys.stderr)
    raise SystemExit(2)
if disk.free < 30 * 1024**3:
    print("  warning    : 可用空间低于建议值 30 GiB；请避免并行保留旧固件产物")
if disk.free / disk.total < 0.05:
    print("  warning    : SDK 所在文件系统剩余空间低于 5%")
PY

echo
echo "工具链版本"
echo "  Python : $(python3 --version 2>&1)"
echo "  CMake  : $(cmake --version | head -n 1)"
echo "  Node   : $(node --version)（项目声明 ^22.21.1）"
echo "  npm    : $(npm --version)"
echo "  Go     : $("${GO}" version)"
if [[ ! "$(node --version)" =~ ^v22\. ]]; then
    echo "  warning: Node 主版本与项目声明不一致，npm 会警告且前端构建可能失败"
fi
GO_REQUIRED="$(awk '$1 == "go" { print $2; exit }' "${PRODUCT_ROOT}/apps/ipkvm/upstream/go.mod")"
GO_ACTUAL="$("${GO}" env GOVERSION | sed 's/^go//')"
python3 - "${GO_ACTUAL}" "${GO_REQUIRED}" <<'PY'
import re
import sys

def version(value):
    match = re.match(r"^(\d+)\.(\d+)(?:\.(\d+))?", value)
    if not match:
        raise ValueError(value)
    return tuple(int(part or 0) for part in match.groups())

try:
    actual, required = version(sys.argv[1]), version(sys.argv[2])
except ValueError as error:
    print(f"error: 无法解析 Go 版本：{error}", file=sys.stderr)
    raise SystemExit(2)
if actual < required:
    print(
        f"error: Go {sys.argv[1]} 低于 go.mod 要求的 {sys.argv[2]}",
        file=sys.stderr,
    )
    raise SystemExit(2)
PY
echo

python3 "${PRODUCT_ROOT}/tools/aitvbox.py" doctor \
    --platform "${PLATFORM}" --sdk-root "${SDK}"

declare -a RELEASE_ARGS=()
[[ ${CHECK_ONLY} -eq 1 ]] && RELEASE_ARGS+=(--check-only)
[[ ${WITH_OTA} -eq 1 ]] && RELEASE_ARGS+=(--with-ota)
[[ ${INCLUDE_SDK_IMAGES} -eq 1 ]] && RELEASE_ARGS+=(--include-sdk-images)
[[ ${ALLOW_DIRTY} -eq 1 ]] && RELEASE_ARGS+=(--allow-dirty)
if [[ -n "${OUTPUT_DIR}" ]]; then
    RELEASE_ARGS+=(--output-dir "${OUTPUT_DIR}")
fi
RELEASE_ARGS+=("${SDK}")

echo
echo "==> 进入正式发布流水线"
printf '+'
printf ' %q' "${PRODUCT_ROOT}/scripts/build_release.sh" "${RELEASE_ARGS[@]}"
echo
exec "${PRODUCT_ROOT}/scripts/build_release.sh" "${RELEASE_ARGS[@]}"
