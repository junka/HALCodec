# tools/cmake/driveos-aarch64.cmake — CMake toolchain file for cross compiling
# HALCodec (NvMedia layer) against NVIDIA DRIVE OS (Linux aarch64).
#
# Usage:
#   cmake -B build-drive \
#       -DCMAKE_TOOLCHAIN_FILE=tools/cmake/driveos-aarch64.cmake
#
# Optional cache variables:
#   DRIVE_OS_TOOLCHAIN_PREFIX  cross compiler prefix, default aarch64-linux-gnu-.
#                              Point it at a gcc-linaro / L4T toolchain if not
#                              on the default PATH, e.g.:
#                              -DDRIVE_OS_TOOLCHAIN_PREFIX=/opt/gcc-linaro/bin/aarch64-linux-gnu-
#   DRIVE_OS_SYSROOT           path to the DRIVE OS sysroot. When set, the
#                              compiler runs with --sysroot and find_*() only
#                              looks inside it. The NvMedia runtime libs
#                              (-lnvscibuf/-lnvscisync/-lnvmedia_ide_*/-lnvmedia_iep_*)
#                              must then be reachable from the sysroot (e.g.
#                              <sysroot>/usr/lib/aarch64-linux-gnu or the DRIVE
#                              SDK lib-target), since the layers CMake points
#                              link_directories() at ${NVMEDIA_SDK_DIR}/lib-target.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(DRIVE_OS_TOOLCHAIN_PREFIX "aarch64-linux-gnu-" CACHE STRING
    "Cross compiler prefix for DRIVE OS (default aarch64-linux-gnu-).")

set(CMAKE_C_COMPILER ${DRIVE_OS_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${DRIVE_OS_TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${DRIVE_OS_TOOLCHAIN_PREFIX}gcc)

set(DRIVE_OS_SYSROOT "" CACHE PATH "Optional sysroot for DRIVE OS target libraries.")
if(DRIVE_OS_SYSROOT)
    set(CMAKE_SYSROOT "${DRIVE_OS_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${DRIVE_OS_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()