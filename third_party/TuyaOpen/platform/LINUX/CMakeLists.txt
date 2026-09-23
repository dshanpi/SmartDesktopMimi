cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED BUILD_PARAM_PATH)
    message(FATAL_ERROR "BUILD_PARAM_PATH is not defined, using default value")
endif()

if(EXISTS ${BUILD_PARAM_PATH}/build_param.cmake)
    include(${BUILD_PARAM_PATH}/build_param.cmake)
else()
    message(FATAL_ERROR "${BUILD_PARAM_PATH}/build_param.cmake not found")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/compiler_setup.cmake")

project(TuyaOpen)

# Set a default build type if none was specified, and print it
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Set global compiler flags
add_compile_options(-fdiagnostics-color=always)

# Add an option to enable AddressSanitizer (ASan)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE MATCHES "Debug")
        message(WARNING "ASan is best used with Debug build type. Current build type: ${CMAKE_BUILD_TYPE}")
    endif()
    message(STATUS "AddressSanitizer enabled")
    add_compile_options(-fsanitize=address)
endif()

set(TUYAOPEN_FOUND_LIBRARIES "")
include("${CMAKE_CURRENT_LIST_DIR}/tuyaos_adapter/tuyaopen_adapter.cmake")

if(OPEN_HEADER_DIR)
    string(REPLACE ";" " " HEADER_DIR_LIST "${OPEN_HEADER_DIR}")
    separate_arguments(HEADER_DIR_LIST)
    foreach(header_dir ${HEADER_DIR_LIST})
        include_directories(${header_dir})
    endforeach()
endif()

add_executable(${CONFIG_PROJECT_NAME} ${SOURCES})
set_target_properties(${CONFIG_PROJECT_NAME} PROPERTIES
    BUILD_RPATH "$ORIGIN/lib;$ORIGIN"
    INSTALL_RPATH "$ORIGIN/lib;$ORIGIN"
)

# Set target-specific compile options based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${CONFIG_PROJECT_NAME} PRIVATE -g -O0)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${CONFIG_PROJECT_NAME} PRIVATE -O3 -DNDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(${CONFIG_PROJECT_NAME} PRIVATE -O2 -g -DNDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    target_compile_options(${CONFIG_PROJECT_NAME} PRIVATE -Os -DNDEBUG)
endif()

# Library linking
if(ENABLE_ASAN)
    target_compile_options(${CONFIG_PROJECT_NAME} PRIVATE -fsanitize=address)
    list(APPEND TUYAOPEN_FOUND_LIBRARIES "-fsanitize=address")
endif()

list(APPEND TUYAOPEN_FOUND_LIBRARIES "pthread")

if((NOT DEFINED CONFIG_COMPILE_PREX OR "${CONFIG_COMPILE_PREX}" STREQUAL "") AND NOT IS_CROSS_COMPILE)
    list(APPEND TUYAOPEN_FOUND_LIBRARIES "systemd")
endif()

if(PLATFORM_NEED_LIBS)
    string(REPLACE " " ";" LIBS_LIST "${PLATFORM_NEED_LIBS}")
    list(APPEND TUYAOPEN_FOUND_LIBRARIES
                -L${OPEN_LIBS_DIR}
                ${LIBS_LIST}
    )
endif()

# # Include dependency detection modules
# include(${CMAKE_CURRENT_SOURCE_DIR}/deps/alsa.cmake)

list(APPEND TUYAOPEN_FOUND_LIBRARIES "m" "stdc++")

if(PLATFORM_CHIP STREQUAL "A133_B6")
    # libaudio_subsys.a uses GCC 13 atomic helpers; ALSA and MNN also need
    # the traditional glibc split libraries when linked by Tina GCC 8.
    list(APPEND TUYAOPEN_FOUND_LIBRARIES "atomic" "dl" "rt")
endif()

if(CONFIG_ENABLE_BLUETOOTH)
    list(APPEND TUYAOPEN_FOUND_LIBRARIES "glib-2.0" "gio-2.0" "gobject-2.0" "bluetooth" "dbus-1")
endif()

# SDL2 display support (Raspberry Pi desktop window display)
if(DEFINED CONFIG_ENABLE_SDL_DISPLAY AND CONFIG_ENABLE_SDL_DISPLAY STREQUAL "y")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    list(APPEND TUYAOPEN_FOUND_LIBRARIES ${SDL2_LDFLAGS} ${SDL2_LDFLAGS_OTHER})
    message(STATUS "SDL2 display enabled: ${SDL2_LDFLAGS}")
endif()

# Try to link with libmvec for vectorized math functions used by MNN
# libmvec is part of glibc on ARM64/AArch64 systems and provides SIMD optimized math functions
find_library(MVEC_LIBRARY mvec)
if(MVEC_LIBRARY)
    list(APPEND TUYAOPEN_FOUND_LIBRARIES "mvec")
    message(STATUS "Found libmvec: ${MVEC_LIBRARY}")
else()
    message(WARNING "libmvec not found. MNN library requires libmvec for vectorized math operations.")
    message(WARNING "On Debian/Ubuntu: sudo apt-get install libc6-dev")
    message(WARNING "If link errors persist, the MNN library may need to be rebuilt without vector math optimizations.")
endif()

target_link_libraries(${CONFIG_PROJECT_NAME}
    -Wl,--start-group
    ${TUYAOPEN_FOUND_LIBRARIES}
    -Wl,--end-group
)
