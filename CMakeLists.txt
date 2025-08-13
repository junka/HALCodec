
cmake_minimum_required(VERSION 3.7)
project(HALCodec)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add src directory to include paths
include_directories(${CMAKE_SOURCE_DIR}/src)

# Collect all source files in src directory
file(GLOB_RECURSE SRC ${CMAKE_SOURCE_DIR}/src/*.cc ${CMAKE_SOURCE_DIR}/src/*.h)

add_subdirectory(NVEnc)


add_subdirectory(app)

