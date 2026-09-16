cmake_minimum_required(VERSION 3.10)


# NVCODEC_SDK_DIR is resolved by the top-level CMakeLists (highest installed
# Video_Codec_SDK_* by default, overridable via -DNVCODEC_SDK_VERSION=<ver>).
if(NOT NVCODEC_SDK_DIR)
    message(FATAL_ERROR "NVCODEC_SDK_DIR not set; the top-level CMakeLists must "
        "locate the NVIDIA Video Codec SDK before add_subdirectory(NVEnc).")
endif()
set(NVCODEC_PUBLIC_INTERFACE_DIR ${NVCODEC_SDK_DIR}/Interface)
set(NVCODEC_UTILS_DIR ${NVCODEC_SDK_DIR}/Samples/Utils)
set(NV_CODEC_DIR ${NVCODEC_SDK_DIR}/Samples/NvCodec)
set(NV_ENC_DIR ${NV_CODEC_DIR}/NvEncoder)
set(NV_DEC_DIR ${NV_CODEC_DIR}/NvDecoder)
set(NV_LAYERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/layers)

if(CMAKE_VERSION VERSION_GREATER "3.27.0" OR CMAKE_VERSION VERSION_EQUAL "3.27.0")
    set(CMAKE_MODULE_PATH ${NVCODEC_SDK_DIR}/Samples/External/cmake/Modules)
    message("Setting CMAKE_MODULE_PATH to ${CMAKE_MODULE_PATH}")
endif()


find_package(CUDA)

set(CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER})
set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS};-gencode arch=compute_50,code=\"sm_50,compute_50\")
if ( CMAKE_COMPILER_IS_GNUCC )
    if(NOT "${CUDA_NVCC_FLAGS}" MATCHES "-std=c\\+\\+11" )
        list(APPEND CUDA_NVCC_FLAGS -std=c++11)
    endif()
endif()


# NVIDIA ships link-time stubs (symbols only) under Lib/linux/stubs/<arch>;
# the real implementation comes from the driver at runtime. Pick the arch from
# the target so cross-compiling to aarch64 lands on the right stubs.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(NVCODEC_STUB_ARCH aarch64)
else()
    set(NVCODEC_STUB_ARCH x86_64)
endif()
set(NVCODEC_STUB_DIR ${NVCODEC_SDK_DIR}/Lib/linux/stubs/${NVCODEC_STUB_ARCH})

find_library(CUVID_LIB nvcuvid PATHS ${NVCODEC_STUB_DIR} NO_DEFAULT_PATH)
find_library(NVENCODEAPI_LIB nvidia-encode PATHS ${NVCODEC_STUB_DIR} NO_DEFAULT_PATH)
if(NOT CUVID_LIB OR NOT NVENCODEAPI_LIB)
    message(FATAL_ERROR "NVIDIA codec stub libraries not found under "
        "${NVCODEC_STUB_DIR} (arch=${NVCODEC_STUB_ARCH}, sdk=${NVCODEC_SDK_DIR}). "
        "This SDK copy may be incomplete (missing Lib/). Re-import with "
        "tools/update_sdks.sh --import <zip> nvdec or pick another version via "
        "-DNVCODEC_SDK_VERSION=<ver>.")
endif()


add_subdirectory(layers)