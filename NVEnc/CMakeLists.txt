cmake_minimum_required(VERSION 3.10)


set(NVCODEC_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Video_Codec_SDK_13.0.19)
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


find_library(CUVID_LIB nvcuvid)
find_library(NVENCODEAPI_LIB nvidia-encode)


add_subdirectory(layers)