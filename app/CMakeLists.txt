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
#   camera_source_sipl.cc  aarch64 + NVMEDIA_DRIVE_SDK_DIR(SIPL 直接链接)
#   camera_source_v4l2.cc  Linux(需 <linux/videodev2.h>)
#   camera_source_av.mm    APPLE(AVFoundation,ObjC++),链系统框架
add_executable(hal_cam hal_cam.cc camera_source_factory.h)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(hal_cam PRIVATE camera_source_v4l2.cc)
    target_compile_definitions(hal_cam PRIVATE HAL_CAM_HAVE_V4L2=1)
elseif(APPLE)
    enable_language(OBJCXX)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(AV_FRAMEWORK AVFoundation REQUIRED)
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(CORE_MEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(CORE_VIDEO_FRAMEWORK CoreVideo REQUIRED)
    target_sources(hal_cam PRIVATE camera_source_av.mm)
    # ObjC++ + ARC:delegate 与 session 对象由 ARC 管理,CF 类型仍手动。
    set_source_files_properties(camera_source_av.mm PROPERTIES
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
    target_sources(hal_cam PRIVATE camera_source_sipl.cc)
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
    target_link_libraries(hal_cam PUBLIC
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvsipl.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvsipl_query.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvscibuf.so
        ${NVMEDIA_DRIVE_SDK_DIR}/lib-target/libnvscisync.so
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
# Each exe depends on the backends so a backend rebuild triggers the exe's
# POST_BUILD copy.
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
foreach(_tgt hal_enc codecinfo hal_dec hal_session hal_transcode hal_cam)
    if(NOT TARGET ${_tgt})
        continue()
    endif()
    foreach(_layer ${_halcodec_backends})
        add_dependencies(${_tgt} ${_layer})
        add_custom_command(TARGET ${_tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${_layer}> $<TARGET_FILE_DIR:${_tgt}>)
    endforeach()
endforeach()