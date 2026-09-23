# A133 Tina OpenWrt cross toolchain for lv_port_linux.
# Reads the active SDK toolchain from openwrt/openwrt/.config so this project
# follows the same compiler used to build the firmware.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(A133_SDK_ROOT "/home/ubuntu/A133-Tina5.0-v0.9" CACHE PATH "A133 Tina SDK root")
set(A133_OPENWRT_CONFIG "${A133_SDK_ROOT}/openwrt/openwrt/.config" CACHE FILEPATH "Tina OpenWrt .config")
set(STAGING_DIR_ROOT "${A133_SDK_ROOT}/out/a133/b6/openwrt/staging_dir")
set(SYSROOT_PATH "${STAGING_DIR_ROOT}/target")

function(a133_read_config_string out_var key fallback)
    if(EXISTS "${A133_OPENWRT_CONFIG}")
        file(STRINGS "${A133_OPENWRT_CONFIG}" _a133_config_line REGEX "^${key}=")
    endif()

    if(_a133_config_line)
        list(GET _a133_config_line 0 _a133_config_value)
        string(REGEX REPLACE "^${key}=\"?([^\"]*)\"?$" "\\1" _a133_config_value "${_a133_config_value}")
    else()
        set(_a133_config_value "${fallback}")
    endif()

    string(REPLACE "$$(LICHEE_TOP_DIR)" "${A133_SDK_ROOT}" _a133_config_value "${_a133_config_value}")
    string(REPLACE "$(LICHEE_TOP_DIR)" "${A133_SDK_ROOT}" _a133_config_value "${_a133_config_value}")
    set(${out_var} "${_a133_config_value}" PARENT_SCOPE)
endfunction()

a133_read_config_string(TOOLCHAIN_ROOT CONFIG_TOOLCHAIN_ROOT
    "${A133_SDK_ROOT}/prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-1130/toolchain")
a133_read_config_string(TOOLCHAIN_PREFIX CONFIG_TOOLCHAIN_PREFIX "aarch64-openwrt-linux-")

set(REAL_C_COMPILER "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}gcc")
set(REAL_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}g++")

set(CMAKE_AR "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_STRIP "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}strip")
set(CMAKE_LINKER "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}ld")
set(CMAKE_NM "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}nm")
set(CMAKE_OBJCOPY "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_OBJDUMP "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}objdump")
set(CMAKE_READELF "${TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}readelf")

if(NOT EXISTS "${REAL_C_COMPILER}")
    message(FATAL_ERROR "C compiler not found: ${REAL_C_COMPILER}")
endif()
if(NOT EXISTS "${REAL_CXX_COMPILER}")
    message(FATAL_ERROR "CXX compiler not found: ${REAL_CXX_COMPILER}")
endif()
if(NOT EXISTS "${SYSROOT_PATH}")
    message(FATAL_ERROR "OpenWrt sysroot not found: ${SYSROOT_PATH}")
endif()

set(ENV{STAGING_DIR} "${STAGING_DIR_ROOT}")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT_PATH}")
set(ENV{PKG_CONFIG_LIBDIR} "${SYSROOT_PATH}/usr/lib/pkgconfig:${SYSROOT_PATH}/usr/share/pkgconfig")

set(A133_WRAPPER_DIR "${CMAKE_BINARY_DIR}/.cmake/a133-toolchain")
file(MAKE_DIRECTORY "${A133_WRAPPER_DIR}")
file(WRITE "${A133_WRAPPER_DIR}/${TOOLCHAIN_PREFIX}gcc" "#!/bin/sh
export STAGING_DIR=\"${STAGING_DIR_ROOT}\"
export PATH=\"${TOOLCHAIN_ROOT}/bin:$PATH\"
exec \"${REAL_C_COMPILER}\" \"$@\"
")
file(WRITE "${A133_WRAPPER_DIR}/${TOOLCHAIN_PREFIX}g++" "#!/bin/sh
export STAGING_DIR=\"${STAGING_DIR_ROOT}\"
export PATH=\"${TOOLCHAIN_ROOT}/bin:$PATH\"
exec \"${REAL_CXX_COMPILER}\" \"$@\"
")
execute_process(COMMAND chmod +x
    "${A133_WRAPPER_DIR}/${TOOLCHAIN_PREFIX}gcc"
    "${A133_WRAPPER_DIR}/${TOOLCHAIN_PREFIX}g++")

set(CMAKE_C_COMPILER "${A133_WRAPPER_DIR}/${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${A133_WRAPPER_DIR}/${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")

set(CMAKE_SYSROOT "${SYSROOT_PATH}")

set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}" "${SYSROOT_PATH}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(A133_RPATH_LINK_FLAGS
    "-Wl,-rpath-link,${SYSROOT_PATH}/lib -Wl,-rpath-link,${SYSROOT_PATH}/usr/lib -Wl,-rpath-link,${SYSROOT_PATH}/root-a133-b6/lib -Wl,-rpath-link,${SYSROOT_PATH}/root-a133-b6/usr/lib")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${A133_RPATH_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${A133_RPATH_LINK_FLAGS}")

add_definitions(-D_GNU_SOURCE)

message(STATUS ">>> Cross-compiling for A133 (ARM64) - OpenWrt")
message(STATUS "A133 toolchain root: ${TOOLCHAIN_ROOT}")
message(STATUS "A133 compiler: ${REAL_C_COMPILER}")
message(STATUS "A133 STAGING_DIR: ${STAGING_DIR_ROOT}")
message(STATUS "A133 sysroot: ${SYSROOT_PATH}")
