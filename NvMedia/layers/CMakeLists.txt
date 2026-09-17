cmake_minimum_required(VERSION 3.10)

set(nv_src nvmediadecoder.cc nvmediaencoder.cc nvmedia_caps.cc)

add_library(nvmedia_layers SHARED ${nv_src})

# NvMedia SDK headers live under NvMedia/include (top level) and
# NvMedia/include/nvmedia_6x. NVMEDIA_SDK_DIR is set by the top-level
# CMakeLists when it locates the SDK on Linux aarch64.
target_include_directories(nvmedia_layers PUBLIC
        ${NVMEDIA_SDK_DIR}/include
        ${NVMEDIA_SDK_DIR}/include/nvmedia_6x)

# NvSci-backed API surface (see samples/nvmedia_6x/iep/Makefile) and the
# non-safety IEP init entry points used by the encoder.
target_compile_definitions(nvmedia_layers PRIVATE
        NVMEDIA_NVSCI_ENABLE
        NV_IS_SAFETY=0)

# Full data-path library set, mirroring the sample link lines:
#   ide:  -lnvscibuf -lnvscisync -lnvmedia_ide_parser -lnvmedia_ide_sci -lz -lm
#   iep:  -lnvscibuf -lnvscisync -lnvmedia_iep_sci
link_directories(${NVMEDIA_SDK_DIR}/lib-target)
target_link_libraries(nvmedia_layers PUBLIC
        halcodec_core
        ${CMAKE_DL_LIBS}
        -lnvscibuf
        -lnvscisync
        -lnvmedia_ide_parser
        -lnvmedia_ide_sci
        -lnvmedia_iep_sci
        -lz
        -lm)