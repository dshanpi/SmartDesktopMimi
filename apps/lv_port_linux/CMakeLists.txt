cmake_minimum_required(VERSION 3.11)
project(lvgl)

option(LV_USE_SUNXI_G2D "Allwinner sunxi-g2d draw unit (FILL acceleration, RGB565)" OFF)
option(AITVBOX_ENABLE_100ASK_CLOUD
    "Build the optional 100ask cloud provider (requires a private SDK checkout)"
    OFF)
set(AITVBOX_100ASK_SDK_ROOT "" CACHE PATH
    "Path to the private 100ask IoT SDK when AITVBOX_ENABLE_100ASK_CLOUD=ON")
set(AITVBOX_PLATFORM "a133" CACHE STRING "AITVBox board adapter")
set_property(CACHE AITVBOX_PLATFORM PROPERTY STRINGS a133 a527 v883 rk3576 rv1106)

if(AITVBOX_PLATFORM STREQUAL "a133")
    set(AITVBOX_PLATFORM_SOURCES
        src/platform/a133/platform_capabilities_a133.c)
else()
    message(FATAL_ERROR
        "AITVBOX_PLATFORM=${AITVBOX_PLATFORM} has no adapter yet. "
        "Add src/platform/${AITVBOX_PLATFORM}/ and its toolchain profile.")
endif()

if(AITVBOX_ENABLE_100ASK_CLOUD)
    if(NOT IS_ABSOLUTE "${AITVBOX_100ASK_SDK_ROOT}")
        message(FATAL_ERROR
            "AITVBOX_100ASK_SDK_ROOT must be an absolute path when "
            "AITVBOX_ENABLE_100ASK_CLOUD=ON")
    endif()
    get_filename_component(AITVBOX_100ASK_SDK_REAL
        "${AITVBOX_100ASK_SDK_ROOT}" REALPATH)
    get_filename_component(AITVBOX_PRODUCT_ROOT
        "${CMAKE_SOURCE_DIR}/../.." REALPATH)
    string(FIND "${AITVBOX_100ASK_SDK_REAL}/"
        "${AITVBOX_PRODUCT_ROOT}/" AITVBOX_100ASK_IN_PRODUCT_TREE)
    if(AITVBOX_100ASK_IN_PRODUCT_TREE EQUAL 0)
        message(FATAL_ERROR
            "AITVBOX_100ASK_SDK_ROOT must be outside the product source tree; "
            "private SDK files must never be consumed from a public checkout")
    endif()
    foreach(required_file IN ITEMS include/100ask_iot.h src/100ask_iot.c)
        if(NOT EXISTS "${AITVBOX_100ASK_SDK_REAL}/${required_file}")
            message(FATAL_ERROR
                "Private 100ask SDK is incomplete: missing "
                "${AITVBOX_100ASK_SDK_REAL}/${required_file}")
        endif()
    endforeach()
    set(AITVBOX_CLOUD_SOURCES
        src/system/service_cloud.c
        src/system/service_cloud_validation.c
        "${AITVBOX_100ASK_SDK_REAL}/src/100ask_iot.c")
    message(STATUS "100ask cloud provider: enabled (private SDK outside public tree)")
else()
    set(AITVBOX_CLOUD_SOURCES src/system/service_cloud_stub.c)
    message(STATUS "100ask cloud provider: disabled (public stub)")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
add_definitions(-D_GNU_SOURCE)

# 解决 librt.so.1 not found 的问题
# 使用 -rpath-link 显式告诉链接器去哪里找间接依赖库
set(LIBRT_PATH "${SYSROOT_PATH}/root-a133-b6/lib")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath-link,${LIBRT_PATH}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath-link,${LIBRT_PATH}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-rpath-link,${LIBRT_PATH}")

# 显式添加 wifimg 库的路径
link_directories(
    "${SYSROOT_PATH}/usr/lib"
    "${SYSROOT_PATH}/lib"
)

message(STATUS ">>> [Agent Skill] Cross-compiling for A133 (ARM64) - OpenWrt")

option(WERROR "Treat warnings as errors for LVGL targets" OFF)
set(WIFI_PROVIDER "aw_wifimg" CACHE STRING "WiFi provider implementation")
set_property(CACHE WIFI_PROVIDER PROPERTY STRINGS aw_wifimg)

# Set policy to allow to run the target_link_libraries cmd on targets that are build
# in another directory.
# Currently, the linking is not handled by env_support/cmake/os.cmake
# This means that if a driver is enabled and it requires linking an external library
# it needs to be handled in the top-level project.

cmake_policy(SET CMP0079 NEW)

# Uncomment if the program needs debugging
#set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 99) # LVGL officially supports C99 and above
set(CMAKE_CXX_STANDARD 17) #C17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Wno-psabi")

set(CONFIG "default" CACHE STRING "Config name to use. Leave empty for default `lv_conf.defaults`")

set(LV_CONF_DEFAULTS_PATH "${CMAKE_SOURCE_DIR}/lv_conf.defaults")

# If CONFIG is not default, update LV_CONF_DEFAULTS_PATH to the selected config
if(NOT CONFIG STREQUAL "default")
    set(LV_CONF_DEFAULTS_PATH "${CMAKE_SOURCE_DIR}/configs/${CONFIG}.defaults")
endif()

if(NOT EXISTS "${LV_CONF_DEFAULTS_PATH}")
    message(FATAL_ERROR "Config file not found: ${LV_CONF_DEFAULTS_PATH}")
endif()
message(STATUS "Using config: ${LV_CONF_DEFAULTS_PATH}")

set(LV_BUILD_SET_CONFIG_OPTS ON CACHE BOOL
    "create CMAKE variables from lv_conf_internal.h" FORCE)

set(LV_BUILD_CONF_PATH
    "${CMAKE_BINARY_DIR}/lv_conf.h"
    CACHE PATH "path to lv_conf.h" FORCE)

set(LVGL_TEMPLATE_PATH "${CMAKE_SOURCE_DIR}/lvgl/lv_conf_template.h")
set(GENERATE_SCRIPT_PATH "${CMAKE_SOURCE_DIR}/lvgl/scripts/generate_lv_conf.py")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LVGL_TEMPLATE_PATH})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LV_CONF_DEFAULTS_PATH})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GENERATE_SCRIPT_PATH})

execute_process(
  COMMAND
    ${Python3_EXECUTABLE} ${GENERATE_SCRIPT_PATH} --template
    ${LVGL_TEMPLATE_PATH} --defaults ${LV_CONF_DEFAULTS_PATH} --config
    ${LV_BUILD_CONF_PATH}
  RESULT_VARIABLE config_result
  OUTPUT_VARIABLE config_output
  ERROR_VARIABLE config_output)

if(NOT config_result EQUAL 0)
  message(FATAL_ERROR "Failed to generate lv_conf.h: ${config_output}")
endif()

message(STATUS "${LV_BUILD_CONF_PATH} generated successfully")

# The product does not link upstream LVGL demos or examples.  Keep these
# targets disabled explicitly so the pruned public source snapshot remains
# buildable after those unused source directories are removed.
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)

add_subdirectory(lvgl)

if (CONFIG_LV_USE_EVDEV)
    message("Including EVDEV support")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(EVDEV REQUIRED libevdev)

    list(APPEND PKG_CONFIG_LIB ${EVDEV_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${EVDEV_INCLUDE_DIRS})
    list(APPEND LV_LINUX_BACKEND_SRC src/lib/indev_backends/evdev.c)

endif()

if (CONFIG_LV_USE_LINUX_DRM)

    message("Including DRM support")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBDRM REQUIRED libdrm)

    list(APPEND PKG_CONFIG_LIB ${LIBDRM_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${LIBDRM_INCLUDE_DIRS})
    list(APPEND LV_LINUX_BACKEND_SRC src/lib/display_backends/drm.c)

endif()

if (CONFIG_LV_USE_LINUX_DRM_GBM_BUFFERS OR CONFIG_LV_LINUX_DRM_USE_EGL)

    message("Including GBM support")

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBGBM REQUIRED gbm)

    list(APPEND PKG_CONFIG_LIB ${LIBGBM_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${LIBGBM_INCLUDE_DIRS})

endif()

if (CONFIG_LV_USE_LIBINPUT)
    message("Including libinput support")

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBINPUT REQUIRED libinput)

    list(APPEND PKG_CONFIG_LIB ${LIBINPUT_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${LIBINPUT_INCLUDE_DIRS})

endif()

if (CONFIG_LV_USE_FREETYPE)
    message("Including Freetype support")

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFREETYPE REQUIRED freetype2)

    list(APPEND PKG_CONFIG_LIB ${LIBFREETYPE_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${LIBFREETYPE_INCLUDE_DIRS})

endif()

if (CONFIG_LV_USE_SDL OR CONFIG_LV_USE_DRAW_SDL)

    message("Including SDL2 support")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)

    list(APPEND PKG_CONFIG_LIB ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})

    list(APPEND LV_LINUX_BACKEND_SRC src/lib/display_backends/sdl.c)
endif()

# -----------------------------------------------------------------------------
# wifimg Support (for Backend)
# -----------------------------------------------------------------------------
# 查找 wifimg 库及其依赖
# 注意：OpenWrt 环境下通常不需要 find_package，直接链接库名即可，
# 但为了保险起见，我们手动指定一些路径或依赖。
# wifimg 依赖: libwifimg, liblog, libz, libpthread 等

# 假设 sysroot 环境变量已设置，CMake 会自动在 sysroot 中查找
# wifimg 库名为 libwifimg-v2.0.so，需要明确指定
find_library(WIFIMG_LIB NAMES wifimg-v2.0 wifimg)
find_library(WIFIMG_LOG_LIB log) # 假设依赖 liblog (从 demo 来看可能需要)

if (WIFIMG_LIB)
    message(STATUS "Found wifimg: ${WIFIMG_LIB}")
    list(APPEND BACKEND_LIBS ${WIFIMG_LIB})
else()
    message(WARNING "wifimg library not found. WiFi features will be disabled or linking may fail.")
    # 尝试直接链接具体的库名
    list(APPEND BACKEND_LIBS wifimg-v2.0)
endif()

# 如果有 liblog 也链接上
if (WIFIMG_LOG_LIB)
    list(APPEND BACKEND_LIBS ${WIFIMG_LOG_LIB})
endif()

# -----------------------------------------------------------------------------
# Wayland Support
# -----------------------------------------------------------------------------
if (CONFIG_LV_USE_WAYLAND)

    message("Including Wayland support")

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(WAYLAND_CURSOR REQUIRED wayland-cursor)
    pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)

    list(APPEND PKG_CONFIG_LIB ${WAYLAND_CLIENT_LIBRARIES})
    list(APPEND PKG_CONFIG_LIB ${WAYLAND_CURSOR_LIBRARIES})
    list(APPEND PKG_CONFIG_LIB ${XKBCOMMON_LIBRARIES})

    # Wayland protocols
    pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.25)
    pkg_get_variable(WAYLAND_PROTOCOLS_BASE wayland-protocols pkgdatadir)

    if(DEFINED ENV{SDKTARGETSYSROOT} AND DEFINED ENV{SYSROOT})
        message(FATAL_ERROR "Both SDKTARGETSYSROOT and SYSROOT are set. Please set only one.")
    endif()
    
    if(DEFINED ENV{SDKTARGETSYSROOT})
        set(PROTOCOL_ROOT "$ENV{SDKTARGETSYSROOT}/usr/share/wayland-protocols")
    elseif(DEFINED ENV{SYSROOT})
        set(PROTOCOL_ROOT "$ENV{SYSROOT}/usr/share/wayland-protocols")
    else()
        set(PROTOCOL_ROOT "/usr/share/wayland-protocols")
    endif()
    
    if(NOT EXISTS ${PROTOCOL_ROOT})
        message(FATAL_ERROR "Wayland protocols not found at ${PROTOCOL_ROOT}")
    endif()
    
    set(PROTOCOLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
    file(MAKE_DIRECTORY ${PROTOCOLS_DIR})
    
    set(WAYLAND_PROTOCOLS_SRC "")

    # Use public-code for shared libs, private-code for static libs
    if(BUILD_SHARED_LIBS)
        set(WAYLAND_SCANNER_CODE_MODE "public-code")
    else()
        set(WAYLAND_SCANNER_CODE_MODE "private-code")
    endif()

    # Generate xdg-shell protocol (always)
    set(XDG_SHELL_XML "${PROTOCOL_ROOT}/stable/xdg-shell/xdg-shell.xml")
    set(XDG_SHELL_HEADER "${PROTOCOLS_DIR}/wayland_xdg_shell.h")
    set(XDG_SHELL_SOURCE "${PROTOCOLS_DIR}/wayland_xdg_shell.c")
    
    if(NOT EXISTS ${XDG_SHELL_HEADER} OR NOT EXISTS ${XDG_SHELL_SOURCE})
        execute_process(COMMAND wayland-scanner client-header ${XDG_SHELL_XML} ${XDG_SHELL_HEADER})
        execute_process(COMMAND wayland-scanner ${WAYLAND_SCANNER_CODE_MODE} ${XDG_SHELL_XML} ${XDG_SHELL_SOURCE})
    endif()
    list(APPEND WAYLAND_PROTOCOLS_SRC ${XDG_SHELL_SOURCE})
    
    # Generate dmabuf protocol (if config is set)
    if(CONFIG_LV_WAYLAND_USE_DMABUF)
        set(DMABUF_XML "${PROTOCOL_ROOT}/stable/linux-dmabuf/linux-dmabuf-v1.xml")
        set(DMABUF_HEADER "${PROTOCOLS_DIR}/wayland_linux_dmabuf.h")
        set(DMABUF_SOURCE "${PROTOCOLS_DIR}/wayland_linux_dmabuf.c")
        
        if(NOT EXISTS ${DMABUF_HEADER} OR NOT EXISTS ${DMABUF_SOURCE})
            execute_process(COMMAND wayland-scanner client-header ${DMABUF_XML} ${DMABUF_HEADER})
            execute_process(COMMAND wayland-scanner ${WAYLAND_SCANNER_CODE_MODE} ${DMABUF_XML} ${DMABUF_SOURCE})
        endif()
        list(APPEND WAYLAND_PROTOCOLS_SRC ${DMABUF_SOURCE})
    endif()

    if(CONFIG_LV_WAYLAND_USE_EGL)
        message("Including WAYLAND EGL support")
        pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
        list(APPEND PKG_CONFIG_LIB ${WAYLAND_EGL_LIBRARIES})
        list(APPEND PKG_CONFIG_INC ${WAYLAND_EGL_INCLUDE_DIRS})
    endif()
    
    list(APPEND PKG_CONFIG_INC ${PROTOCOLS_DIR})
    list(APPEND LV_LINUX_BACKEND_SRC src/lib/display_backends/wayland.c ${WAYLAND_PROTOCOLS_SRC})

endif()

if (CONFIG_LV_USE_X11)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11 REQUIRED x11)

    message("Including X11 support")

    list(APPEND PKG_CONFIG_INC ${X11_INCLUDE_DIRS})
    list(APPEND PKG_CONFIG_LIB ${X11_LIBRARIES})
    list(APPEND LV_LINUX_BACKEND_SRC src/lib/display_backends/x11.c)

endif()

if (CONFIG_LV_USE_LINUX_FBDEV)

    # FBDEV has no dependencies
    message("Including FBDEV support")
    list(APPEND LV_LINUX_BACKEND_SRC src/lib/display_backends/fbdev.c)

endif()

if (CONFIG_LV_USE_SUNXIFB)
    message("Including SunxiFB support")
    list(APPEND LV_LINUX_BACKEND_SRC src/lib/display_backends/sunxifb_custom.c)
endif()

if (CONFIG_LV_USE_GLFW)

    message("Including GLFW support")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW3 REQUIRED glfw3)
    pkg_check_modules(GLEW REQUIRED glew)

    list(APPEND PKG_CONFIG_LIB ${GLFW3_LIBRARIES})
    list(APPEND PKG_CONFIG_LIB ${GLEW_LIBRARIES})
    list(APPEND LV_LINUX_BACKEND_SRC src/lib/display_backends/glfw3.c)

endif()

if (CONFIG_LV_USE_DRAW_G2D)
    message("Including G2D support")
    find_library(G2D_LIBRARY NAMES g2d)
    list(APPEND PKG_CONFIG_LIB ${G2D_LIBRARY})
endif()

if (CONFIG_LV_USE_GLTF)
    message(STATUS "Including with GLTF support")
    include(FetchContent)
  
    FetchContent_Declare(
      fastgltf
      GIT_REPOSITORY https://github.com/spnda/fastgltf
      GIT_TAG 4e2261350888bae7c35a1f39991f6233d57795f5)
  
    set(FASTGLTF_ENABLE_DEPRECATED_EXT
        ON
        CACHE BOOL "" FORCE)
  
    set(FASTGLTF_DIFFUSE_TRANSMISSION_SUPPORT
        ON
        CACHE BOOL "" FORCE)
  
    FetchContent_MakeAvailable(fastgltf)
  
    set(WEBP_BUILD_ANIM_UTILS
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX
        OFF
        CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS
        OFF
        CACHE BOOL "" FORCE)
  
    FetchContent_Declare(
      webp
      GIT_REPOSITORY https://github.com/webmproject/libwebp
      GIT_TAG fa6f56496a442eed59b103250021e4b14ebf1427)
  
    FetchContent_MakeAvailable(webp)
  
    list(APPEND PKG_CONFIG_LIB fastgltf webp)
endif()

if (CONFIG_LV_USE_GSTREAMER)

    message("Including GSTREAMER support")
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    pkg_check_modules(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
    pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)

    list(APPEND PKG_CONFIG_LIB ${GSTREAMER_VIDEO_LIBRARIES} ${GSTREAMER_APP_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${GSTREAMER_VIDEO_INCLUDE_DIRS} ${GSTREAMER_APP_INCLUDE_DIRS})
endif()

# Check for external demos
if(CONFIG_LV_USE_DEMO_FLEX_LAYOUT
   OR CONFIG_LV_USE_DEMO_MULTILANG
   OR CONFIG_LV_USE_DEMO_TRANSFORM
   OR CONFIG_LV_USE_DEMO_SCROLL
   OR CONFIG_LV_USE_DEMO_EBIKE
   OR CONFIG_LV_USE_DEMO_HIGH_RES
   OR CONFIG_LV_USE_DEMO_SMARTWATCH)

    set(USE_EXTERNAL_DEMOS ON)
else()
    set(USE_EXTERNAL_DEMOS OFF)
endif()

if(USE_EXTERNAL_DEMOS)
    message("Including external demos")
    include(FetchContent)
    FetchContent_Declare(lv_demos_ext GIT_REPOSITORY https://github.com/lvgl/lv_demos)
    FetchContent_MakeAvailable(lv_demos_ext)
    list(APPEND PKG_CONFIG_LIB lv_demos_ext)
endif()


file(GLOB LV_LINUX_SRC src/lib/*.c)
set(LV_LINUX_INC src/lib)

# Allwinner sunxi-g2d draw unit（产品侧源码，FILL 加速）。须在 GLOB LV_LINUX_SRC 之后 append。
if(LV_USE_SUNXI_G2D)
    message("Including Allwinner sunxi-g2d draw unit (FILL accel, RGB565)")
    file(GLOB SUNXI_G2D_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/draw/sunxi_g2d/*.c)
    list(APPEND LV_LINUX_SRC ${SUNXI_G2D_SRC})
    find_library(UAPI_LIBRARY NAMES uapi)
    if(NOT UAPI_LIBRARY)
        message(FATAL_ERROR "libuapi not found in sysroot (required by sunxi-g2d draw unit)")
    endif()
    list(APPEND PKG_CONFIG_LIB ${UAPI_LIBRARY})
endif()

target_include_directories(lvgl PUBLIC ${PKG_CONFIG_INC})

# Library type is determined by BUILD_SHARED_LIBS (shared if ON, static if OFF)
add_library(lvgl_linux ${LV_LINUX_SRC} ${LV_LINUX_BACKEND_SRC})
target_include_directories(lvgl_linux PUBLIC ${PKG_CONFIG_INC})

# If LVGL is configured to use LV_CONF_PATH or Kconfig
# Set the exactly the same definitions on the lvgl_linux target
set_target_properties(lvgl_linux PROPERTIES COMPILE_DEFINITIONS "${LVGL_COMPILER_DEFINES}")
target_include_directories(lvgl_linux PUBLIC
    ${LV_LINUX_INC} ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/lib ${LVGL_CONF_INC_DIR})

# sunxi-g2d: 给 lvgl_linux（含 sunxifb_custom.c + draw unit 源码）和 lvglsim（含 main.c）
# 注入 LV_USE_SUNXI_G2D 宏 + g2d 头文件目录。PUBLIC 让 lvglsim 也拿到。
if(LV_USE_SUNXI_G2D)
    target_compile_definitions(lvgl_linux PUBLIC LV_USE_SUNXI_G2D=1)
    target_include_directories(lvgl_linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/draw/sunxi_g2d)
    # ion_mem_alloc.h 在 sysroot usr/include，但本工具链 --sysroot 不把它加入 <...> 搜索，
    # 需显式指定（与 lvglsim 的 SYSTEM PRIVATE ${SYSROOT_PATH}/usr/include 一致）。
    target_include_directories(lvgl_linux SYSTEM PRIVATE ${SYSROOT_PATH}/usr/include)
endif()

# Link LVGL with external dependencies - Modern CMake/CMP0079 allows this
target_link_libraries(lvgl PUBLIC ${PKG_CONFIG_LIB} m pthread)

# -----------------------------------------------------------------------------
# Source Files
# -----------------------------------------------------------------------------
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "src/ui/*.c")
file(GLOB_RECURSE UI_CPP_SOURCES "src/ui/*.cpp")
file(GLOB_RECURSE UI_HPP_SOURCES "src/ui/*.hpp")
file(GLOB_RECURSE SYSTEM_SOURCES "src/system/*.c")
file(GLOB_RECURSE MIDDLEWARE_SOURCES "src/middleware/*.c")
file(GLOB_RECURSE IPC_SOURCES "src/ipc/*.c")
file(GLOB_RECURSE FONT_SOURCES "src/ui/font/*.c")
file(GLOB_RECURSE IMG_SOURCES "src/ui/images/*.c")
file(GLOB_RECURSE THEME_SOURCES "src/ui/theme/*.c")

# Exclude backend-only sources from generic globs
list(FILTER SYSTEM_SOURCES EXCLUDE REGEX "src/system/service_.*\\.c$")
list(FILTER SYSTEM_SOURCES EXCLUDE REGEX "src/system/wifi_hal\\.c$")
list(FILTER SYSTEM_SOURCES EXCLUDE REGEX "src/system/bluez_player_monitor\\.c$")
list(FILTER SYSTEM_SOURCES EXCLUDE REGEX "src/system/led_strip_hal\\.c$")

# App sources (exclude tplayer_wrapper.c from UI build)
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "src/apps/*.c")
list(FILTER APP_SOURCES EXCLUDE REGEX "src/apps/video/tplayer_wrapper\\.c$")

# -----------------------------------------------------------------------------
# TPlayer Support
# -----------------------------------------------------------------------------
# TPlayer dependencies: libtplayer, libcedarx (cdx_base), librt, libstdc++
find_library(TPLAYER_LIB NAMES tplayer)
find_library(CDX_BASE_LIB NAMES cdx_base)
find_library(ASOUND_LIB NAMES asound)

# Manually add TPlayer include path
# Based on search: /home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir/target/usr/include/allwinner
# And vdecoder.h in: /home/ubuntu/A133-Tina5.0-v0.9/out/a133/b6/openwrt/staging_dir/target/usr/include/allwinner/include
set(TPLAYER_INCLUDE_DIR 
    "${SYSROOT_PATH}/usr/include/allwinner"
    "${SYSROOT_PATH}/usr/include/allwinner/include"
)

if (TPLAYER_LIB)
    message(STATUS "Found TPlayer: ${TPLAYER_LIB}")
    list(APPEND TPLAYER_LIBS ${TPLAYER_LIB})
else()
    message(WARNING "TPlayer lib not found, assuming tplayer")
    list(APPEND TPLAYER_LIBS tplayer)
endif()

if (CDX_BASE_LIB)
    message(STATUS "Found CedarX Base: ${CDX_BASE_LIB}")
    list(APPEND TPLAYER_LIBS ${CDX_BASE_LIB})
else()
    message(WARNING "CedarX Base lib not found, assuming cdx_base")
    list(APPEND TPLAYER_LIBS cdx_base)
endif()

# -----------------------------------------------------------------------------
# Build Backend Process (lv_backend)
# -----------------------------------------------------------------------------
# Backend 独立进程，负责业务逻辑 (Wi-Fi, 音乐等)
# 它不依赖 LVGL 图形库，只依赖 middleware 和系统库

add_executable(lv_backend
    src/backend_main.c
    src/backend/backend_command_router.c
    src/backend/backend_runtime.c
    src/backend/backend_v2_server.c
    src/platform/platform_capabilities.c
    ${AITVBOX_PLATFORM_SOURCES}
    src/middleware/middleware.c
    src/ipc/ipc_socket.c
    src/ipc/ipc_v2.c
    src/system/backlight_control.c
    src/system/volume_control.c
    src/system/led_strip_hal.c
    src/system/service_led.c
    src/system/config/app_config.c
    src/system/settings.c
    src/system/log/app_log.c
    src/system/wifi_hal.c
    src/system/service_wifi.c
    src/system/service_time.c
    src/system/service_video.c
    src/system/service_sensor.c
    src/system/bluez_player_monitor.c
    src/system/bt_pcm_player.c
    src/system/service_bt.c
    src/system/service_ai_runtime.c
    src/system/service_hdmi_preview.c
    src/system/service_ota.c
    ${AITVBOX_CLOUD_SOURCES}
    src/platform/wifi/aw_wifimg_driver.c
    src/apps/video/tplayer_wrapper.c
)

# -----------------------------------------------------------------------------
# btmanager Support (Bluetooth A2DP Sink)
# -----------------------------------------------------------------------------
find_library(BTMG_LIB NAMES btmg)
find_library(BT_SHARED_MAINLOOP_LIB NAMES shared-mainloop)
find_library(BLUETOOTH_INTERNAL_LIB NAMES bluetooth-internal)
find_library(BLUETOOTH_LIB NAMES bluetooth)
find_library(GIO_LIB NAMES gio-2.0)
find_library(GOBJECT_LIB NAMES gobject-2.0)
find_library(GLIBCORE_LIB NAMES glib-2.0)
find_library(SBC_LIB NAMES sbc)
find_library(JSONC_LIB NAMES json-c)
find_library(DBUS_LIB NAMES dbus-1)
find_path(DBUS_INCLUDE_DIR NAMES dbus/dbus.h
    HINTS "${SYSROOT_PATH}/usr/include/dbus-1.0"
    PATH_SUFFIXES dbus-1.0)
find_path(DBUS_ARCH_INCLUDE_DIR NAMES dbus/dbus-arch-deps.h
    HINTS "${SYSROOT_PATH}/usr/lib/dbus-1.0/include"
    PATH_SUFFIXES dbus-1.0/include)
find_library(READLINE_LIB NAMES readline)
find_library(NCURSES_LIB NAMES ncurses)
find_library(WIRELESSCOM_LIB NAMES wirelesscom)

# OTA 与可选云 provider 的 HTTPS 请求使用 libcurl。
find_library(CURL_LIB NAMES curl)

if(AITVBOX_ENABLE_100ASK_CLOUD)
    find_library(MOSQUITTO_LIB NAMES mosquitto)
    if(NOT MOSQUITTO_LIB)
        message(FATAL_ERROR
            "AITVBOX_ENABLE_100ASK_CLOUD=ON requires libmosquitto in the target sysroot")
    endif()
endif()

set(BT_LIBS
    ${BTMG_LIB}
    ${BT_SHARED_MAINLOOP_LIB}
    ${BLUETOOTH_INTERNAL_LIB}
    ${BLUETOOTH_LIB}
    ${GIO_LIB}
    ${GOBJECT_LIB}
    ${GLIBCORE_LIB}
    ${SBC_LIB}
    ${JSONC_LIB}
    ${DBUS_LIB}
    ${READLINE_LIB}
    ${NCURSES_LIB}
    ${WIRELESSCOM_LIB}
)

target_link_libraries(lv_backend
    PRIVATE
    pthread
    rt
    m
    ${ASOUND_LIB}
    ${BACKEND_LIBS} # 包含 wifimg, log 等
    ${TPLAYER_LIBS}
    ${BT_LIBS}
    ${CURL_LIB}
)

if(AITVBOX_ENABLE_100ASK_CLOUD)
    target_link_libraries(lv_backend PRIVATE ${MOSQUITTO_LIB})
endif()

target_include_directories(lv_backend PRIVATE
    ${DBUS_INCLUDE_DIR}
    ${DBUS_ARCH_INCLUDE_DIR}
)

target_include_directories(lv_backend SYSTEM PRIVATE
    ${TPLAYER_INCLUDE_DIR}
    "${SYSROOT_PATH}/usr/include"
)

if(AITVBOX_ENABLE_100ASK_CLOUD)
    target_include_directories(lv_backend PRIVATE
        "${AITVBOX_100ASK_SDK_REAL}/include")
endif()

# -----------------------------------------------------------------------------
# Build UI Process (lvglsim)
# -----------------------------------------------------------------------------
# UI 进程，负责界面显示和用户交互
add_executable(lvglsim
    src/main.c
    ${UI_SOURCES}
    ${UI_CPP_SOURCES}
    ${APP_SOURCES}
    ${SYSTEM_SOURCES}
    ${MIDDLEWARE_SOURCES}
    ${IPC_SOURCES}
    ${FONT_SOURCES}
    ${IMG_SOURCES}
    ${THEME_SOURCES}
)

# Repeat lvgl_linux to resolve circular dependency with lvgl
target_link_libraries(lvglsim 
    lvgl_linux 
    lvgl 
    lvgl_linux
    ${ASOUND_LIB}
    ${JSONC_LIB}
)

target_include_directories(lvglsim SYSTEM PRIVATE
    "${SYSROOT_PATH}/usr/include"
)

# UI Process NO LONGER links TPlayer
# target_include_directories(lvglsim PRIVATE ${TPLAYER_INCLUDE_DIR})

# Suppress errors for TPlayer headers (zero-size array in CdxKeyedVector.h)
set_source_files_properties(src/apps/video/tplayer_wrapper.c PROPERTIES COMPILE_FLAGS "-Wno-pedantic -Wno-error=pedantic -Wno-unused-function")

if(WERROR)
    target_compile_options(lvglsim PRIVATE -Werror)
    target_compile_options(lvgl PRIVATE -Werror)
    target_compile_options(lvgl_linux PRIVATE -Werror)
endif()


# Install the lvgl_linux library and its headers
install(DIRECTORY src/lib/
    DESTINATION include/lvgl
    FILES_MATCHING
    PATTERN "backends*" EXCLUDE
    PATTERN "*.h")

install(TARGETS lvgl_linux
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

add_custom_target(run COMMAND ${EXECUTABLE_OUTPUT_PATH}/lvglsim DEPENDS lvglsim)
