cmake_minimum_required(VERSION 3.10)

# The unit tests link directly against the sources they exercise so they run on
# any platform (no VideoToolbox, no GPU driver). The integration cases below
# shell out to the built executables and skip when a backend or ffmpeg is not
# available.

enable_testing()

# ProRes RAW needs a sample handed to it: measured, no encoder for it exists on
# this machine -- neither VideoToolbox (no kCMVideoCodecType_AppleProResRAW*
# encoder appears in the enumerate) nor ffmpeg's prores encoder writes a 'prrf'
# frame -- so the codestream has to come from a camera. The cases that need it are
# registered only when the file is there; a registered case that skips itself still
# reports as Passed, which is how a missing sample would turn into a false green.
# Either a -DHAL_PRORES_RAW_SAMPLE=... path or tests/data/prores_raw_*.prrf works.
set(HAL_PRORES_RAW_SAMPLE "" CACHE FILEPATH
    "Path to a ProRes RAW ('prrf') elementary stream, enables vtdec.prores_raw")
file(GLOB _prrf_bundled "${CMAKE_CURRENT_SOURCE_DIR}/data/prores_raw_*.prrf")
if(NOT HAL_PRORES_RAW_SAMPLE AND _prrf_bundled)
    list(GET _prrf_bundled 0 HAL_PRORES_RAW_SAMPLE)
endif()
if(HAL_PRORES_RAW_SAMPLE AND NOT EXISTS "${HAL_PRORES_RAW_SAMPLE}")
    message(WARNING "HAL_PRORES_RAW_SAMPLE does not exist: "
            "${HAL_PRORES_RAW_SAMPLE} -- vtdec.prores_raw will not be registered")
endif()

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
# hevcpoc_test — the HEVC picture-order front end (H.265 8.3.1), same shape.
# ---------------------------------------------------------------------------
add_executable(hevcpoc_test
    hevcpoc_test.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/hevcpoc.cc)
target_include_directories(hevcpoc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers)
set_target_properties(hevcpoc_test PROPERTIES CXX_STANDARD 17)
add_test(NAME hevcpoc COMMAND hevcpoc_test)

# ---------------------------------------------------------------------------
# av1desc_test — the AV1 OBU/sequence-header front end and the av01 sample entry
# it synthesises. Same shape again: no framework, no decoder.
# ---------------------------------------------------------------------------
add_executable(av1desc_test
    av1desc_test.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/av1desc.cc)
target_include_directories(av1desc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers)
set_target_properties(av1desc_test PROPERTIES CXX_STANDARD 17)
add_test(NAME av1desc COMMAND av1desc_test)

# ---------------------------------------------------------------------------
# jpegdesc_test -- the JPEG codestream walk (marker chain, frame header, end of
# image). Same shape again: no framework, no decoder. argv[1] is a real encoded
# image, which is what makes the crafted markers credible.
# ---------------------------------------------------------------------------
add_executable(jpegdesc_test
    jpegdesc_test.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/jpegdesc.cc)
target_include_directories(jpegdesc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers)
set_target_properties(jpegdesc_test PROPERTIES CXX_STANDARD 17)
add_test(NAME jpegdesc COMMAND jpegdesc_test
         ${CMAKE_CURRENT_SOURCE_DIR}/data/vt_jpeg_320x240.jpg)

# ---------------------------------------------------------------------------
# proresdesc_test -- the ProRes frame-header read and the split of concatenated
# frames on their leading byte count, for both the 'icpf' and the 'prrf' (RAW)
# frame tags. Same shape again: no framework, no decoder. argv[1] is encoder
# output (three 320x240 Proxy frames), which is what makes the crafted headers
# credible; argv[2], when a RAW sample is configured, is real RAW material.
# ---------------------------------------------------------------------------
add_executable(proresdesc_test
    proresdesc_test.cc
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers/proresdesc.cc)
target_include_directories(proresdesc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/VideoToolbox/layers)
set_target_properties(proresdesc_test PROPERTIES CXX_STANDARD 17)
if(HAL_PRORES_RAW_SAMPLE AND EXISTS "${HAL_PRORES_RAW_SAMPLE}")
    # The second fixture is real ProRes RAW material, which is what makes the
    # 'prrf' cases more than a parser agreeing with itself.
    add_test(NAME proresdesc COMMAND proresdesc_test
             ${CMAKE_CURRENT_SOURCE_DIR}/data/prores_apco_320x240.prores
             ${HAL_PRORES_RAW_SAMPLE})
else()
    add_test(NAME proresdesc COMMAND proresdesc_test
             ${CMAKE_CURRENT_SOURCE_DIR}/data/prores_apco_320x240.prores)
endif()

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
find_program(FFPROBE_EXE NAMES ffprobe)
if(FFMPEG_EXE AND EXISTS "${_cars_h264}")
    set(_cars_nv12 "${CMAKE_SOURCE_DIR}/QSV/libvpl-2.17.0/examples/content/cars_320x240.nv12")
    if(EXISTS "${_cars_nv12}" AND HAL_DEC_EXE)
        add_test(NAME vtdec.ffmpeg_parity
            COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DINPUT_NV12=${_cars_nv12}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg_parity.cmake)

        # AV1 arrives as IVF, so this case encodes one and checks hal_dec's
        # VideoToolbox output against ffmpeg's own AV1 decode. The script skips
        # by itself if this ffmpeg build has no AV1 encoder.
        add_test(NAME vtdec.av1_ivf_parity
            COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DINPUT_NV12=${_cars_nv12}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/av1_ivf_parity.cmake)

        # JPEG is one image per codestream, so the decoder is fed a file ffmpeg
        # wrote rather than a video stream.
        add_test(NAME vtdec.jpeg
            COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DINPUT_NV12=${_cars_nv12}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtdec_jpeg.cmake)

        # ProRes arrives as a bare element stream (each picture states its own
        # length), and it decodes to 16-bit chroma at full plane height rather
        # than NV12 -- 4:2:2 as 'sv22', 4:4:4 as the interleaved 'sv44' -- so the
        # frame size and the chroma plane height are pinned against ffmpeg.
        add_test(NAME vtdec.prores
            COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DFFPROBE=${FFPROBE_EXE}
                    -DINPUT_NV12=${_cars_nv12}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtdec_prores.cmake)

        # 10-bit output is a different animal in the buffer: VideoToolbox hands
        # back a packed picture whose rows carry three 10-bit samples per word at
        # a padded pitch ('p420' or 'pf20', the letter being the stream's range
        # rather than its codec), while HAL's P010 is one sample per 16-bit word.
        if(FFPROBE_EXE AND HAL_DEC_EXE)
            add_test(NAME vtdec.hevc_main10
                COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                        -DFFMPEG=${FFMPEG_EXE}
                        -DFFPROBE=${FFPROBE_EXE}
                        -DINPUT_NV12=${_cars_nv12}
                        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/vtdec_main10.cmake)

            # The same unpack on the other 10-bit codec that this backend can
            # decode, so the shift and the pitch law are not HEVC-specific.
            add_test(NAME vtdec.av1_10bit
                COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                        -DFFMPEG=${FFMPEG_EXE}
                        -DFFPROBE=${FFPROBE_EXE}
                        -DINPUT_NV12=${_cars_nv12}
                        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/vtdec_av1_10bit.cmake)

            # 10-bit 4:2:2 ('p422'/'pf22'): the packed rows of 'pf20' with a
            # chroma row against every picture row, so the chroma height -- not
            # the unpack -- is what this case pins.
            add_test(NAME vtdec.hevc_42210
                COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                        -DFFMPEG=${FFMPEG_EXE}
                        -DFFPROBE=${FFPROBE_EXE}
                        -DINPUT_NV12=${_cars_nv12}
                        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/vtdec_hevc_42210.cmake)

            # The monochrome profiles ('L008', 'L010'), whose buffers the API
            # reports as having no planes at all: one allocation, no chroma.
            add_test(NAME vtdec.hevc_mono
                COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                        -DFFMPEG=${FFMPEG_EXE}
                        -DFFPROBE=${FFPROBE_EXE}
                        -DINPUT_NV12=${_cars_nv12}
                        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/vtdec_hevc_mono.cmake)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Integration: ProRes RAW decode -- a Bayer sensel grid plus the metadata that
# says what it is, so the checks are the grid's own byte count and the reported
# phase order and levels. Needs the sample configured above; registered only when
# it exists (see the note there).
# ---------------------------------------------------------------------------
if(HAL_PRORES_RAW_SAMPLE AND EXISTS "${HAL_PRORES_RAW_SAMPLE}" AND HAL_DEC_EXE)
    add_test(NAME vtdec.prores_raw
        COMMAND ${CMAKE_COMMAND} -DHAL_DEC=${HAL_DEC_EXE}
                -DSAMPLE=${HAL_PRORES_RAW_SAMPLE}
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/vtdec_prores_raw.cmake)
endif()

# ---------------------------------------------------------------------------
# Integration: HEVC Main 10 encode -- hal_enc takes P010 raw input and the
# stream must come back as Main 10 / yuv420p10le. Registered separately because
# it needs hal_enc and ffprobe rather than hal_dec.
# ---------------------------------------------------------------------------
find_program(HAL_ENC_EXE NAMES hal_enc PATHS ${CMAKE_BINARY_DIR}/app NO_DEFAULT_PATH)
if(FFMPEG_EXE AND FFPROBE_EXE AND HAL_ENC_EXE)
    set(_cars_nv12_enc "${CMAKE_SOURCE_DIR}/QSV/libvpl-2.17.0/examples/content/cars_320x240.nv12")
    if(EXISTS "${_cars_nv12_enc}")
        add_test(NAME vtenc.hevc_main10
            COMMAND ${CMAKE_COMMAND} -DHAL_ENC=${HAL_ENC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DFFPROBE=${FFPROBE_EXE}
                    -DINPUT_NV12=${_cars_nv12_enc}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtenc_main10.cmake)

        # Image coder: three staged pictures in, three standalone codestreams
        # out. Needs ffprobe to confirm each output is its own decodable image.
        add_test(NAME vtenc.jpeg
            COMMAND ${CMAKE_COMMAND} -DHAL_ENC=${HAL_ENC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DFFPROBE=${FFPROBE_EXE}
                    -DINPUT_NV12=${_cars_nv12_enc}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtenc_jpeg.cmake)

        # The same image coder over a directory whose inputs carry the --format
        # extension, so every output names the input it was made from. Above is
        # .yuv in / .nv12 out, where nothing collides; here the two are the same
        # path, which is what used to drop a picture and shift the rest.
        add_test(NAME vtenc.jpeg_collision
            COMMAND ${CMAKE_COMMAND} -DHAL_ENC=${HAL_ENC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DFFPROBE=${FFPROBE_EXE}
                    -DINPUT_NV12=${_cars_nv12_enc}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtenc_jpeg_collision.cmake)

        # The HEVC profiles this hardware answers for that the encoder had no
        # name for -- Main 4:2:2 10 and the two monochrome ones -- asserted
        # through ffprobe's pix_fmt, which is read out of the SPS.
        add_test(NAME vtenc.hevc_profiles
            COMMAND ${CMAKE_COMMAND} -DHAL_ENC=${HAL_ENC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DFFPROBE=${FFPROBE_EXE}
                    -DINPUT_NV12=${_cars_nv12_enc}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtenc_hevc_profiles.cmake)
    endif()
endif()

# ---------------------------------------------------------------------------
# Integration: ProRes encode -- a coder whose samples are the codestream, so its
# output is walked with the decoder's own frame parser and decoded back with the
# decoder for a fidelity score. Registered apart from the block above because it
# needs hal_dec but not ffprobe.
# ---------------------------------------------------------------------------
if(FFMPEG_EXE AND HAL_ENC_EXE AND HAL_DEC_EXE)
    set(_cars_nv12_pr "${CMAKE_SOURCE_DIR}/QSV/libvpl-2.17.0/examples/content/cars_320x240.nv12")
    if(EXISTS "${_cars_nv12_pr}")
        add_test(NAME vtenc.prores
            COMMAND ${CMAKE_COMMAND} -DHAL_ENC=${HAL_ENC_EXE}
                    -DHAL_DEC=${HAL_DEC_EXE}
                    -DFFMPEG=${FFMPEG_EXE}
                    -DPRORESDESC_TEST=${CMAKE_CURRENT_BINARY_DIR}/proresdesc_test
                    -DINPUT_NV12=${_cars_nv12_pr}
                    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtenc_prores.cmake)
    endif()
endif()
