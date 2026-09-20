cmake_minimum_required(VERSION 3.10)

set(nv_src nvmediadecoder.cc nvmediaencoder.cc nvmedia_caps.cc)

add_library(nvmedia_layers SHARED ${nv_src})

# NvMedia SDK headers. NVMEDIA_INCLUDE_DIR / NVMEDIA_INCLUDE_6X_DIR are resolved
# by the top-level CMakeLists: a host DRIVE-OS SDK (thor) is preferred when
# present so the struct layouts (e.g. the 640-byte NvMediaEncodePicParamsH264)
# match the target libnvmedia; otherwise the bundled NvMedia/include (2023 SDK)
# is used. NVMEDIA_SDK_DIR still points at the bundled tree for lib-target libs.
target_include_directories(nvmedia_layers PUBLIC
        ${NVMEDIA_INCLUDE_DIR}
        ${NVMEDIA_INCLUDE_6X_DIR})

# NvSci-backed API surface (see samples/nvmedia_6x/iep/Makefile) and the
# non-safety IEP init entry points used by the encoder.
target_compile_definitions(nvmedia_layers PRIVATE
        NVMEDIA_NVSCI_ENABLE
        NV_IS_SAFETY=0)

# Full data-path library set, mirroring the sample link lines:
#   ide:  -lnvscibuf -lnvscisync -lnvmedia_ide_parser -lnvmedia_ide_sci -lz -lm
#   iep:  -lnvscibuf -lnvscisync -lnvmedia_iep_sci
# Link by absolute path rather than -l + link_directories(): the latter is
# directory-scoped and silently emits no -L when the path is not under a
# CMAKE_FIND_ROOT_PATH (it breaks cross-compile). Absolute paths always work.
# libnvscibuf.so / libnvscisync.so are not shipped in the SDK lib-target
# (only libnvscibuf.so.1 is); pull them from the target device into lib-target.
target_link_libraries(nvmedia_layers PUBLIC
        halcodec_core
        ${CMAKE_DL_LIBS}
        ${NVMEDIA_SDK_DIR}/lib-target/libnvscibuf.so
        ${NVMEDIA_SDK_DIR}/lib-target/libnvscisync.so
        ${NVMEDIA_SDK_DIR}/lib-target/libnvmedia_ide_parser.so
        ${NVMEDIA_SDK_DIR}/lib-target/libnvmedia_ide_sci.so
        ${NVMEDIA_SDK_DIR}/lib-target/libnvmedia_iep_sci.so
        ${CMAKE_DL_LIBS}
        -lz
        -lm)
