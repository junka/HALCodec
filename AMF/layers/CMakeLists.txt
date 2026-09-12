cmake_minimum_required(VERSION 3.10)

set(enc_src amfdecoder.cc amfencoder.cc amf_caps.cc)

add_library(amf_layers SHARED ${enc_src})

# AMF SDK headers only. The AMF runtime (libamfrt64.so.1 on Linux) is dlopen'd
# at runtime, so there is no static link dependency. AMF_SDK_DIR is set by the
# top-level CMakeLists when it locates the AMF SDK (version-independent).
target_include_directories(amf_layers PUBLIC
        ${AMF_SDK_DIR}/amf/public/include)
target_link_libraries(amf_layers PUBLIC ${CMAKE_DL_LIBS})