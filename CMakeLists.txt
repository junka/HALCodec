
cmake_minimum_required(VERSION 3.10)
project(HALCodec)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add src directory to include paths
include_directories(${CMAKE_SOURCE_DIR}/src)

# Collect all source files in src directory
file(GLOB_RECURSE SRC ${CMAKE_SOURCE_DIR}/src/*.cc ${CMAKE_SOURCE_DIR}/src/*.h)

if(UNIX AND NOT APPLE)
    find_package(CUDA QUIET)
    if(CUDA_FOUND)
        add_subdirectory(NVEnc)
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
elseif(APPLE)
    add_subdirectory(VideoToolbox)
endif()


add_subdirectory(app)

