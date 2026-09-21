
cmake_minimum_required(VERSION 3.10)
project(HALCodec)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add src directory to include paths
include_directories(${CMAKE_SOURCE_DIR}/src)

# Core library: owns the process-wide Registry singletons. Backends (linked or
# dlopen'd) and executables all depend on it so a backend registered at runtime
# is visible to the app. Without a shared core, each .so gets its own
# function-local static Map and runtime-loaded backends vanish.
add_library(halcodec_core SHARED
    ${CMAKE_SOURCE_DIR}/src/registry.cc
    ${CMAKE_SOURCE_DIR}/src/session.cc
    ${CMAKE_SOURCE_DIR}/src/frame.cc
    ${CMAKE_SOURCE_DIR}/src/metrics.cc)
set_target_properties(halcodec_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# Export only the Registry<Encoder/Decoder/CapabilityProvider> instantiations.
target_link_libraries(halcodec_core PUBLIC ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    find_package(CUDA QUIET)
    if(CUDA_FOUND)
        # Locate the NVIDIA Video Codec SDK under NVEnc/Video_Codec_SDK_*.
        # By default the highest installed version is picked; pin a specific
        # version with -DNVCODEC_SDK_VERSION=<ver> (e.g. 12.1.14). Mirrors the
        # version-independent glob used for AMF/QSV, but NVEnc additionally
        # resolves stub libraries at link time, so the selected SDK_DIR must
        # carry both headers and Lib/.
        file(GLOB NVCODEC_SDK_CANDIDATES ${CMAKE_SOURCE_DIR}/NVEnc/Video_Codec_SDK_*)
        if(NVCODEC_SDK_VERSION)
            set(NVCODEC_SDK_DIR ${CMAKE_SOURCE_DIR}/NVEnc/Video_Codec_SDK_${NVCODEC_SDK_VERSION})
            if(NOT IS_DIRECTORY ${NVCODEC_SDK_DIR})
                message(FATAL_ERROR "NVCODEC_SDK_VERSION=${NVCODEC_SDK_VERSION} selected but "
                    "directory not found: ${NVCODEC_SDK_DIR}. Available: ${NVCODEC_SDK_CANDIDATES}")
            endif()
        else()
            # Pick the candidate with the highest version number (semver-ish).
            set(_nvcodec_best "")
            set(_nvcodec_best_key "0")
            foreach(_candidate ${NVCODEC_SDK_CANDIDATES})
                get_filename_component(_ver ${_candidate} NAME)
                string(REPLACE "Video_Codec_SDK_" "" _ver "${_ver}")
                string(REGEX MATCHALL "[0-9]+" _parts "${_ver}")
                set(_key "0")
                foreach(_p ${_parts})
                    string(REGEX REPLACE "^0+(.)" "\\1" _p "${_p}")
                    set(_key "${_key}.${_p}")
                endforeach()
                if(_key STRGREATER _nvcodec_best_key)
                    set(_nvcodec_best_key ${_key})
                    set(_nvcodec_best ${_candidate})
                endif()
            endforeach()
            set(NVCODEC_SDK_DIR ${_nvcodec_best})
        endif()
        if(NVCODEC_SDK_DIR)
            message(STATUS "NvCodec SDK found: ${NVCODEC_SDK_DIR}")
            add_subdirectory(NVEnc)
        else()
            message(WARNING "No NVIDIA Video Codec SDK found under NVEnc/Video_Codec_SDK_*; "
                "skipping NVEnc. Import one with tools/update_sdks.sh --import <zip> nvdec")
        endif()
        add_subdirectory(NVJPEG)
    endif()
    # Locate the AMF SDK regardless of its version directory (AMF/AMF-*/).
    file(GLOB AMF_SDK_CANDIDATES ${CMAKE_SOURCE_DIR}/AMF/AMF-*)
    foreach(_candidate ${AMF_SDK_CANDIDATES})
        if(EXISTS ${_candidate}/amf/public/include/core/Factory.h)
            set(AMF_SDK_DIR ${_candidate})
            break()
        endif()
    endforeach()
    if(AMF_SDK_DIR)
        message(STATUS "AMF SDK found: ${AMF_SDK_DIR}")
        add_subdirectory(AMF/layers)
    endif()
    # Locate the libvpl SDK (QSV/QSVvpl-*/... -> QSV/libvpl-*/api/vpl/mfx.h).
    file(GLOB QSV_SDK_CANDIDATES ${CMAKE_SOURCE_DIR}/QSV/libvpl-*)
    foreach(_candidate ${QSV_SDK_CANDIDATES})
        if(EXISTS ${_candidate}/api/vpl/mfx.h)
            set(QSV_SDK_DIR ${_candidate})
            break()
        endif()
    endforeach()
    if(QSV_SDK_DIR)
        message(STATUS "QSV SDK found: ${QSV_SDK_DIR}")
        add_subdirectory(QSV/layers)
    endif()
    # NvMedia (DRIVE OS) — 仅 Linux + aarch64 且 SDK 头文件存在时启用。
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(NVMEDIA_SDK_DIR ${CMAKE_SOURCE_DIR}/NvMedia)
        # The bundled NvMedia/include headers are the 2023 SDK, whose
        # NvMediaEncodePicParamsH264 is 384 bytes. Thor (tegra264) boards ship
        # libnvmedia_iep_sci built from the 2024 SDK, where that struct is 640
        # bytes — feeding a too-small picParams to NvMediaIEPFeedFrame reads
        # 256 bytes of stack garbage and returns NVMEDIA_STATUS_ERROR. Prefer a
        # host-installed DRIVE-OS SDK (thor/thoru) whose headers match the
        # target lib when one is present.
        foreach(_cand
                $ENV{NVMEDIA_DRIVE_SDK}
                /opt/gwm/nv_sdk/thor/thoru-aarch64/drive-linux
                /opt/gwm/nv_sdk/thor/thor-aarch64/drive-linux
                /opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux)
            if(_cand AND EXISTS ${_cand}/include/nvmedia_6x/nvmedia_iep.h)
                set(NVMEDIA_DRIVE_SDK_DIR ${_cand})
                message(STATUS "NvMedia DRIVE-OS SDK (matched): ${_cand}")
                break()
            endif()
        endforeach()
        if(NVMEDIA_DRIVE_SDK_DIR)
            set(NVMEDIA_INCLUDE_DIR ${NVMEDIA_DRIVE_SDK_DIR}/include)
            set(NVMEDIA_INCLUDE_6X_DIR ${NVMEDIA_DRIVE_SDK_DIR}/include/nvmedia_6x)
        else()
            set(NVMEDIA_INCLUDE_DIR ${NVMEDIA_SDK_DIR}/include)
            set(NVMEDIA_INCLUDE_6X_DIR ${NVMEDIA_SDK_DIR}/include/nvmedia_6x)
        endif()
        if(EXISTS ${NVMEDIA_INCLUDE_6X_DIR}/nvmedia_ide.h)
            message(STATUS "NvMedia SDK found (include: ${NVMEDIA_INCLUDE_DIR}); enabling nvmedia_layers")
            add_subdirectory(NvMedia/layers)
        endif()
    endif()
elseif(APPLE)
    add_subdirectory(VideoToolbox)
endif()


add_subdirectory(app)

