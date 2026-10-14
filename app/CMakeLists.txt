cmake_minimum_required(VERSION 3.10)

# Set policy CMP0167 to NEW to suppress warnings about FindBoost removal
if(POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})

# camera_source_av.mm 是 Objective-C++(AVFoundation 仅 macOS)。
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

add_executable(hal_enc hal_enc.cc)

add_executable(codecinfo codecinfo.cc)

add_executable(hal_dec hal_dec.cc parse_cli.h)

add_executable(hal_session hal_session.cc parse_cli.h)

add_executable(hal_transcode hal_transcode.cc parse_cli.h)

# hal_cam:统一相机→编码应用(归并 hal_camera/hal_camera_v4l2)。源实现按平台
# 编译,逐源打 HAL_CAM_HAVE_* 宏,hal_cam.cc 据此路由 --source:
#   camera/camera_source_sipl.cc  aarch64 + NVMEDIA_DRIVE_SDK_DIR(SIPL 直接链接)
#   camera/camera_source_v4l2.cc  Linux(需 <linux/videodev2.h>)
#   camera/camera_source_av.mm    APPLE(AVFoundation,ObjC++),链系统框架
# 相机源抽象独立在 camera/ 目录(平行于 app/),头文件经顶层 include_directories
# 暴露,平台源文件按需编入 hal_cam。
add_executable(hal_cam hal_cam.cc ${CMAKE_SOURCE_DIR}/camera/camera_source_factory.h)
# camera/ 源文件 include "parse_cli.h"(仍由 app/ 提供),故把 app/ 自身暴露给 hal_cam。
target_include_directories(hal_cam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Layers 1-3 (platform-neutral) channel backend and fan-out: the loopback channel
# (in-process multi-consumer), FrameRouter (producer side), SharedFrameSource
# (consumer side). The unixfd / nvscistream backends append here per platform.
# All register into the halcodec_core registries at static-init.
target_sources(hal_cam PRIVATE
    ${CMAKE_SOURCE_DIR}/camera/loopback_channel.cc
    ${CMAKE_SOURCE_DIR}/camera/frame_router.cc
    ${CMAKE_SOURCE_DIR}/camera/camera_source_shared.cc)

# Layer 2, Linux cross-process backend: POSIX shared ring (memfd) + AF_UNIX
# SCM_RIGHTS fd passing. Linux-only by construction (memfd/SCM_RIGHTS), so the
# kind "unixfd" is absent elsewhere and the factory says so.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(hal_cam PRIVATE ${CMAKE_SOURCE_DIR}/camera/unixfd_channel.cc)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(hal_cam PRIVATE ${CMAKE_SOURCE_DIR}/camera/camera_source_v4l2.cc)
    target_compile_definitions(hal_cam PRIVATE HAL_CAM_HAVE_V4L2=1)
elseif(APPLE)
    enable_language(OBJCXX)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(AV_FRAMEWORK AVFoundation REQUIRED)
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(CORE_MEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(CORE_VIDEO_FRAMEWORK CoreVideo REQUIRED)
    target_sources(hal_cam PRIVATE ${CMAKE_SOURCE_DIR}/camera/camera_source_av.mm)
    # ObjC++ + ARC:delegate 与 session 对象由 ARC 管理,CF 类型仍手动。
    set_source_files_properties(${CMAKE_SOURCE_DIR}/camera/camera_source_av.mm PROPERTIES
        LANGUAGE OBJCXX COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(hal_cam PRIVATE HAL_CAM_HAVE_AV=1)
    target_link_libraries(hal_cam PUBLIC
        ${FOUNDATION_FRAMEWORK}
        ${AV_FRAMEWORK}
        ${CORE_FOUNDATION_FRAMEWORK}
        ${CORE_MEDIA_FRAMEWORK}
        ${CORE_VIDEO_FRAMEWORK})
endif()
# SIPL 源:aarch64 + DRIVE-OS SDK 存在时启用(条件与顶层 NvMedia 相同)。
if(NVMEDIA_DRIVE_SDK_DIR AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    target_sources(hal_cam PRIVATE ${CMAKE_SOURCE_DIR}/camera/camera_source_sipl.cc)
    target_include_directories(hal_cam PRIVATE
        ${NVMEDIA_INCLUDE_DIR}
        ${NVMEDIA_INCLUDE_6X_DIR})
    target_compile_definitions(hal_cam PRIVATE
        HAL_CAM_HAVE_SIPL=1
        NVMEDIA_NVSCI_ENABLE
        NV_IS_SAFETY=0)
    # SIPL runtime libs live in the SDK lib-target alongside the NvMedia libs.
    # Link by absolute path (see NvMedia/layers/CMakeLists.txt for why: -l +
    # link_directories() silently drops -L under cross-compile root paths).
    # nvsciipc/nvscibuf frame_share_sipl.cc: 与 nvsciipc_channel.cc 必须编在同一个
    # 可执行文件里(共享 NvSciBufModule 与端点状态),故都在本块内。
    target_sources(hal_cam PRIVATE
        ${CMAKE_SOURCE_DIR}/camera/nvsciipc_channel.cc
        ${CMAKE_SOURCE_DIR}/camera/frame_share_sipl.cc)
    target_link_libraries(hal_cam PUBLIC
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvsipl.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvsipl_query.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvscibuf.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvscisync.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvsciipc.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvmedia_iep_sci.so)
endif()

# Backends are loaded at runtime via dlopen (see src/plugin_loader.h) instead of
# link-time binding. Link-time binding forces the dynamic loader to resolve every
# backend's NEEDED entries (libnvidia-encode.so.1, libnvjpeg.so.12, ...) at
# process start, which makes the executable unusable on machines lacking those
# driver libraries. With dlopen, a backend whose deps are missing is simply
# skipped and the rest still work. This applies on macOS too: vtbox_layers is a
# dylib whose only external deps are system frameworks, so dlopen failure there
# is unlikely, but the load path stays uniform across platforms.

target_link_libraries(hal_enc PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})
target_link_libraries(codecinfo PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})
target_link_libraries(hal_dec PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})
target_link_libraries(hal_session PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})
target_link_libraries(hal_transcode PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})
target_link_libraries(hal_cam PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})

# Stage backend shared libraries (*_layers.so / *_layers.dylib) next to the
# executables so plugin_loader's ExeDir() search finds them in the build tree.
# All the executables share this directory, so one staged copy per backend
# suffices.
#
# The copy is declared as a file-level rule whose OUTPUT is the staged file and
# whose DEPENDS names the backend target, rather than as an exe POST_BUILD step:
# an exe's POST_BUILD only reruns when the exe itself relinks, and a shared
# library is not an input to the link, so a backend rebuild would have left a
# stale copy sitting next to the exe -- which then silently decodes with the old
# backend code.
set(_halcodec_backends "")
if(TARGET vtbox_layers)
    list(APPEND _halcodec_backends vtbox_layers)
endif()
if(TARGET nvenc_layers)
    list(APPEND _halcodec_backends nvenc_layers)
endif()
if(TARGET nvjpeg_layers)
    list(APPEND _halcodec_backends nvjpeg_layers)
endif()
if(TARGET amf_layers)
    list(APPEND _halcodec_backends amf_layers)
endif()
if(TARGET qsv_layers)
    list(APPEND _halcodec_backends qsv_layers)
endif()
if(TARGET nvmedia_layers)
    list(APPEND _halcodec_backends nvmedia_layers)
endif()

set(_halcodec_staged "")
foreach(_layer ${_halcodec_backends})
    set(_staged_lib
        "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}${_layer}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    add_custom_command(OUTPUT ${_staged_lib}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${_layer}> ${_staged_lib}
        DEPENDS ${_layer})
    list(APPEND _halcodec_staged ${_staged_lib})
endforeach()
add_custom_target(halcodec_stage_backends DEPENDS ${_halcodec_staged})
foreach(_tgt hal_enc codecinfo hal_dec hal_session hal_transcode hal_cam)
    if(TARGET ${_tgt})
        add_dependencies(${_tgt} halcodec_stage_backends)
    endif()
endforeach()

# hal_player: cross-platform hard-decode player via HAL Decoder + SDL2 + OpenGL.
# Falls back to the Apple-specific Objective-C++ build only when SDL2 is
# unavailable (kept so the pure VT path still builds on a bare macOS SDK).
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
endif()

if(SDL2_FOUND)
    message(STATUS "[hal_player] SDL2 found — building cross-platform SDL+OpenGL player")
    add_executable(hal_player hal_player.cc)
    set_target_properties(hal_player PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    if(APPLE)
        find_library(GL_FRAMEWORK OpenGL REQUIRED)
        find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
        target_link_libraries(hal_player PRIVATE halcodec_core ${SDL2_LIBRARIES} ${GL_FRAMEWORK} ${COCOA_FRAMEWORK})
    else()
        target_link_libraries(hal_player PRIVATE halcodec_core ${SDL2_LIBRARIES})
    endif()
    target_include_directories(hal_player PRIVATE ${SDL2_INCLUDE_DIRS})
    if(TARGET SDL2::SDL2)
        target_link_libraries(hal_player PRIVATE SDL2::SDL2)
    endif()
    add_dependencies(hal_player halcodec_stage_backends)
elseif(APPLE)
    message(STATUS "[hal_player] SDL2 NOT found — falling back to Apple-only hal_player.mm")
    enable_language(OBJCXX)
    find_library(VT_FRAMEWORK VideoToolbox REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREIMAGE_FRAMEWORK CoreImage REQUIRED)
    add_executable(hal_player hal_player.mm)
    set_target_properties(hal_player PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    target_compile_options(hal_player PRIVATE -fobjc-arc)
    target_link_libraries(hal_player PRIVATE
        halcodec_core Boost::program_options
        ${FOUNDATION_FRAMEWORK} ${APPKIT_FRAMEWORK} ${AV_FRAMEWORK}
        ${CORE_FOUNDATION_FRAMEWORK} ${CORE_MEDIA_FRAMEWORK}
        ${CORE_VIDEO_FRAMEWORK} ${COREIMAGE_FRAMEWORK} ${VT_FRAMEWORK})
    add_dependencies(hal_player halcodec_stage_backends)
else()
    message(STATUS "[hal_player] SDL2 not found — hal_player target skipped")
endif()
