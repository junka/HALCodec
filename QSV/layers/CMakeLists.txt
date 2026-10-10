cmake_minimum_required(VERSION 3.10)

set(qsv_src qsvdecoder.cc qsvencoder.cc qsv_caps.cc)

# Optional libva headers for the VP8/VP9 decode VA-surface allocator
# (qsv_va_allocator.cc). The allocator dlopens libva.so.2 at runtime (no
# link dependency); the headers are only needed for VA struct layouts
# (VASurfaceID, VAImage, VASurfaceAttrib). find_path gates the source: on
# Linux with libva-dev installed, QSV_HAS_VA is defined and the source is
# compiled; otherwise the whole file is #ifdef'd out and the QSV decode
# path falls back to GetSurfaceForDecode (h264/hevc/av1/jpeg unaffected;
# VP8/VP9 stay blocked but the build still succeeds).
find_path(QSV_VA_INCLUDE_DIR va/va.h)
if(QSV_VA_INCLUDE_DIR)
    list(APPEND qsv_src qsv_va_allocator.cc)
endif()

add_library(qsv_layers SHARED ${qsv_src})

# libvpl headers only. The libvpl dispatcher (libvpl.so.2) is dlopen'd at
# runtime, so there is no static link dependency. QSV_SDK_DIR is set by the
# top-level CMakeLists when it locates the libvpl SDK (version-independent).
target_include_directories(qsv_layers PUBLIC ${QSV_SDK_DIR}/api)
# ONEVPL_EXPERIMENTAL gates the mfxFrameSurfaceInterface::Export vtable slot
# and the mfxSurfaceVAAPI concrete struct in the libvpl headers. The runtime
# (libvpl >= 2.10) exposes Export regardless; this define only makes the header
# struct layout expose the slot. Required for the QSV zero-copy Export/Import
# surface-sharing path. Pure compile-time header gate — no new link deps.
target_compile_definitions(qsv_layers PRIVATE ONEVPL_EXPERIMENTAL)
if(QSV_VA_INCLUDE_DIR)
    target_include_directories(qsv_layers PRIVATE ${QSV_VA_INCLUDE_DIR})
    target_compile_definitions(qsv_layers PRIVATE QSV_HAS_VA)
    message(STATUS "QSV: libva headers found at ${QSV_VA_INCLUDE_DIR} — VP8/VP9 VA-surface allocator enabled")
else()
    message(STATUS "QSV: libva headers not found — VP8/VP9 VA-surface allocator disabled (VP8/VP9 decode will stay blocked)")
endif()
target_link_libraries(qsv_layers PUBLIC halcodec_core ${CMAKE_DL_LIBS})