cmake_minimum_required(VERSION 3.10)

set(qsv_src qsvdecoder.cc qsvencoder.cc qsv_caps.cc)

add_library(qsv_layers SHARED ${qsv_src})

# libvpl headers only. The libvpl dispatcher (libvpl.so.2) is dlopen'd at
# runtime, so there is no static link dependency. QSV_SDK_DIR is set by the
# top-level CMakeLists when it locates the libvpl SDK (version-independent).
target_include_directories(qsv_layers PUBLIC ${QSV_SDK_DIR}/api)
target_link_libraries(qsv_layers PUBLIC halcodec_core ${CMAKE_DL_LIBS})