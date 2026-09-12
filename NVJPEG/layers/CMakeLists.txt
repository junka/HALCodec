cmake_minimum_required(VERSION 3.10)

find_package(CUDA)

set(CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER})
set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS};-gencode arch=compute_86,code=\"sm_86,compute_86\")
if ( CMAKE_COMPILER_IS_GNUCC )
    if(NOT "${CUDA_NVCC_FLAGS}" MATCHES "-std=c\\+\\+11" )
        list(APPEND CUDA_NVCC_FLAGS -std=c++11)
    endif()
endif()

set(NVJPEG_SRC nvjpegdecoder.cc nvjpegencoder.cc)

add_library(nvjpeg_layers SHARED ${NVJPEG_SRC})
target_link_libraries(nvjpeg_layers PUBLIC nvjpeg cudart)