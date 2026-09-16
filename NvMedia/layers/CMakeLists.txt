cmake_minimum_required(VERSION 3.10)

set(nv_src nvmediadecoder.cc nvmediaencoder.cc nvmedia_caps.cc)

add_library(nvmedia_layers SHARED ${nv_src})

# NvMedia SDK headers live under NvMedia/include (top level) and
# NvMedia/include/nvmedia_6x. NVMEDIA_SDK_DIR is set by the top-level
# CMakeLists when it locates the SDK on Linux aarch64.
target_include_directories(nvmedia_layers PUBLIC
        ${NVMEDIA_SDK_DIR}/include
        ${NVMEDIA_SDK_DIR}/include/nvmedia_6x)

# Stub only calls IDE/IEP GetVersion/Create/Destroy; the symbols live in
# libnvmedia_ide_sci.so / libnvmedia_iep_sci.so. The full data path would
# additionally need the NvSci libs (see samples/nvmedia_6x/ide/Makefile:
# -lnvscibuf -lnvscisync -lnvmedia_ide_parser -lz -lm).
link_directories(${NVMEDIA_SDK_DIR}/lib-target)
target_link_libraries(nvmedia_layers PUBLIC -lnvmedia_ide_sci -lnvmedia_iep_sci)