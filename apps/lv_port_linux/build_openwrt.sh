#!/bin/bash
set -e

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LVGL_EVDEV_PATCH="${APP_DIR}/../../integrations/lvgl/0001-evdev-map-a133-remote-keys.patch"
LVGL_PATCH_APPLIED=0

restore_lvgl_patch() {
  local status=$?
  if [ "${LVGL_PATCH_APPLIED}" -eq 1 ]; then
    git -C "${APP_DIR}/lvgl" apply --reverse "${LVGL_EVDEV_PATCH}" || true
  fi
  exit "${status}"
}
trap restore_lvgl_patch EXIT

if git -C "${APP_DIR}/lvgl" apply --check "${LVGL_EVDEV_PATCH}"; then
  git -C "${APP_DIR}/lvgl" apply "${LVGL_EVDEV_PATCH}"
  LVGL_PATCH_APPLIED=1
elif ! git -C "${APP_DIR}/lvgl" apply --reverse --check "${LVGL_EVDEV_PATCH}"; then
  echo "Error: LVGL evdev source does not match the recorded A133 remote-key patch." >&2
  exit 1
fi

# Out-of-tree 构建：源码与产物留在本仓库，只借用 SDK 的 toolchain + staging_dir。
# SDK 路径优先级：命令行参数 > 环境变量 AITVBOX_SDK_ROOT > 默认值。
SDK_ROOT="${1:-${AITVBOX_SDK_ROOT:-/home/ubuntu/A133-Tina5.0-v0.9}}"
CLOUD_ENABLED="${AITVBOX_ENABLE_100ASK_CLOUD:-OFF}"
CLOUD_SDK_ROOT="${AITVBOX_100ASK_SDK_ROOT:-}"

# 清理旧构建
rm -rf build
mkdir build

# 设置 OpenWrt 必须的环境变量
export STAGING_DIR="${SDK_ROOT}/out/a133/b6/openwrt/staging_dir"

# 配置 CMake
# -DA133_SDK_ROOT 覆盖 toolchain.cmake 第 8 行的 CACHE PATH，使工具链/staging 跟随传入的 SDK。
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
  -DA133_SDK_ROOT="${SDK_ROOT}" \
  -DAITVBOX_ENABLE_100ASK_CLOUD="${CLOUD_ENABLED}" \
  -DAITVBOX_100ASK_SDK_ROOT="${CLOUD_SDK_ROOT}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWERROR=ON

# 编译
cmake --build build -j$(nproc)

# 验证
TARGET_BIN=$(find build/bin -maxdepth 1 -type f -executable ! -name "*.so" | head -n 1)

if [ -z "$TARGET_BIN" ]; then
    echo "Error: No executable found."
else
    echo "Verifying $TARGET_BIN..."
    file "$TARGET_BIN"
fi
