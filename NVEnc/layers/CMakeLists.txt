cmake_minimum_required(VERSION 3.10)

set(enc_src nvencoder.cc nvdecoder.cc nvdevice.cc)
set(NVENC_SDK_SRC ${NV_DEC_DIR}/NvDecoder.cpp ${NV_ENC_DIR}/NvEncoder.cpp)
add_library(nvenc_layers SHARED ${enc_src} ${NVENC_SDK_SRC})
target_include_directories(nvenc_layers PUBLIC
        ${CUDA_INCLUDE_DIRS}
        ${NVCODEC_PUBLIC_INTERFACE_DIR}
        ${NV_ENC_DIR}
        ${NV_DEC_DIR}
        ${NVCODEC_UTILS_DIR}
        ${NV_CODEC_DIR})
target_link_libraries(nvenc_layers PUBLIC ${CUDA_CUDA_LIBRARY} ${CMAKE_DL_LIBS} ${NVENCODEAPI_LIB} ${CUVID_LIB})

