cmake_minimum_required(VERSION 3.10)

# Set policy CMP0167 to NEW to suppress warnings about FindBoost removal
if(POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(hal_enc hal_enc.cc)

add_executable(codecinfo codecinfo.cc)

add_executable(hal_dec hal_dec.cc parse_cli.h)

add_executable(hal_session hal_session.cc parse_cli.h)

add_executable(hal_transcode hal_transcode.cc parse_cli.h)

# hal_camera: DRIVE-OS (aarch64) SIPL camera -> nvmediaenc zero-copy encode.
# Only built when the NvMedia DRIVE-OS SDK is present (same condition as
# nvmedia_layers in the top-level CMakeLists) and we are cross-compiling for
# aarch64. It links SIPL directly (NvSIPLCamera is a C++ API, not dlopen'd),
# so this target is absent on x86/macOS builds.
if(NVMEDIA_DRIVE_SDK_DIR AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    add_executable(hal_camera hal_camera.cc parse_cli.h)
    target_include_directories(hal_camera PRIVATE
            ${NVMEDIA_INCLUDE_DIR}
            ${NVMEDIA_INCLUDE_6X_DIR})
    target_compile_definitions(hal_camera PRIVATE
            NVMEDIA_NVSCI_ENABLE
            NV_IS_SAFETY=0)
    # SIPL runtime libs live in the SDK lib-target alongside the NvMedia libs.
    # Link by absolute path (see NvMedia/layers/CMakeLists.txt for why: -l +
    # link_directories() silently drops -L under cross-compile root paths).
    target_link_libraries(hal_camera PUBLIC
            halcodec_core
            Boost::program_options
            ${CMAKE_DL_LIBS}
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
foreach(_tgt hal_enc codecinfo hal_dec hal_session hal_transcode hal_camera)
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
