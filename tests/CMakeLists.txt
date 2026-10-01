cmake_minimum_required(VERSION 3.10)

# The unit tests link directly against the sources they exercise so they run on
# any platform (no VideoToolbox, no GPU driver). The integration cases below
# shell out to the built executables and skip when a backend or ffmpeg is not
# available.

enable_testing()

# ---------------------------------------------------------------------------
# h264au_test — pure C++, links h264au.cc + h264poc.cc + hevcpoc.cc directly.
# ---------------------------------------------------------------------------
add_executable(h264au_test
    h264au_test.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/h264au.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/h264poc.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/hevcpoc.cc)
target_include_directories(h264au_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers)
set_target_properties(h264au_test PROPERTIES CXX_STANDARD 17)
add_test(NAME h264au COMMAND h264au_test)

# ---------------------------------------------------------------------------
# h264poc_test — same idea, exercises only the picture-order front end.
# ---------------------------------------------------------------------------
add_executable(h264poc_test
    h264poc_test.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/h264poc.cc)
target_include_directories(h264poc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers)
set_target_properties(h264poc_test PROPERTIES CXX_STANDARD 17)
add_test(NAME h264poc COMMAND h264poc_test)

# ---------------------------------------------------------------------------
# registry_test — links halcodec_core (the shared library that owns the
# process-wide map). The core exports the storage accessor under HALCODEC_API.
# ---------------------------------------------------------------------------
add_executable(registry_test registry_test.cc)
target_link_libraries(registry_test PRIVATE halcodec_core)
set_target_properties(registry_test PROPERTIES CXX_STANDARD 17)
add_test(NAME registry COMMAND registry_test)

# ---------------------------------------------------------------------------
# Integration: decode determinism (bundled cars_320x240.h264 → exactly 30
# frames, two runs byte-identical). Skips when the vtbox backend cannot load.
# ---------------------------------------------------------------------------
find_program(HAL_DEC_EXE NAMES hal_dec PATHS ${CMAKE_BINARY_DIR}/app NO_DEFAULT_PATH)
if(HAL_DEC_EXE)
    set(_cars_h264 "${CMAKE_SOURCE_DIR}/QSV/libvpl-2.17.0/examples/content/cars_320x240.h264")
    if(EXISTS "${_cars_h264}")
        add_test(NAME vtdec.determinism
            COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                    -DINPUT_H264=${_cars_h264}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/decode_determinism.cmake)
    endif()
endif()

# ---------------------------------------------------------------------------
# Integration: ffmpeg parity — synthesises an 8-slice + B-frame stream from the
# bundled NV12 and asserts hal_dec's output matches ffmpeg's own decode byte for
# byte (order-sensitive). Skips when ffmpeg or the assets are missing.
# ---------------------------------------------------------------------------
find_program(FFMPEG_EXE NAMES ffmpeg)
if(FFMPEG_EXE AND EXISTS "${_cars_h264}")
    set(_cars_nv12 "${CMAKE_SOURCE_DIR}/QSV/libvpl-2.17.0/examples/content/cars_320x240.nv12")
    if(EXISTS "${_cars_nv12}" AND HAL_DEC_EXE)
        add_test(NAME vtdec.ffmpeg_parity
            COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DINPUT_NV12=${_cars_nv12}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg_parity.cmake)
    endif()
endif()
