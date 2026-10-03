cmake_minimum_required(VERSION 3.10)

set(enc_src vtdecoder.cc vtencoder.cc vt_caps.cc h264poc.cc hevcpoc.cc h264au.cc av1desc.cc)

add_library(vtbox_layers SHARED ${enc_src})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
)
find_library(VIDEO_TOOLBOX_FRAMEWORK VideoToolbox REQUIRED)
find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
find_library(CORE_MEDIA_FRAMEWORK CoreMedia REQUIRED)
find_library(CORE_VIDEO_FRAMEWORK CoreVideo REQUIRED)

target_link_libraries(vtbox_layers PUBLIC halcodec_core ${CMAKE_DL_LIBS}
    ${VIDEO_TOOLBOX_FRAMEWORK}
    ${CORE_FOUNDATION_FRAMEWORK}
    ${CORE_MEDIA_FRAMEWORK}
    ${CORE_VIDEO_FRAMEWORK}
    PkgConfig::LIBAV
)

