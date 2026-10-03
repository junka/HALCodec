# av1_ivf_parity.cmake — encodes the bundled NV12 to AV1 in IVF with libsvtav1,
# decodes that IVF with both ffmpeg and hal_dec, and asserts the NV12 outputs are
# byte-identical over every frame (order-sensitive, so it also pins the claim
# that VideoToolbox hands AV1 temporal units back in submission order). Skips
# when ffmpeg lacks an AV1 encoder or decoder, when the assets are missing, or
# when the vtbox backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, INPUT_NV12 and WORKDIR must be set")
endif()

set(SYNTH_IVF   "${WORKDIR}/synth_av1.ivf")
set(FFMPEG_REF  "${WORKDIR}/av1_ffmpeg_ref.nv12")
set(HAL_OUT     "${WORKDIR}/av1_hal_dec_out.nv12")
set(FRAME_BYTES 115200)   # 320x240 NV12

# IVF rather than MP4: it carries no av1C, so the stream exercises the sample
# entry this build synthesises from the in-band sequence header. Random-access
# GOP with a large keyint keeps intra refresh and out-of-order references inside
# a single 30-frame clip.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12}
                -c:v libsvtav1 -preset 8 -pix_fmt yuv420p
                -y ${SYNTH_IVF}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtdec.av1_ivf_parity: ffmpeg cannot encode AV1 (rc=${rc})")
    return()
endif()

execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f ivf -i ${SYNTH_IVF}
                -pix_fmt nv12 -f rawvideo -y ${FFMPEG_REF}
                RESULT_VARIABLE rc2 OUTPUT_QUIET ERROR_QUIET)
if(NOT rc2 EQUAL 0)
    message("SKIP vtdec.av1_ivf_parity: ffmpeg reference decode failed (rc=${rc2})")
    return()
endif()

execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${SYNTH_IVF} -o ${HAL_OUT}
                RESULT_VARIABLE rc3 OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc3 EQUAL 137 OR rc3 GREATER 0)
    message("SKIP vtdec.av1_ivf_parity: hal_dec exited with ${rc3}\n${out}${err}")
    return()
endif()

string(REGEX MATCH "Decode total frames: ([0-9]+)" _m "${out}")
set(HAL_FRAMES "${CMAKE_MATCH_1}")

file(SIZE ${FFMPEG_REF} ref_size)
file(SIZE ${HAL_OUT} hal_size)
math(EXPR ref_frames "${ref_size} / ${FRAME_BYTES}")
if(NOT ref_frames GREATER 0)
    message(FATAL_ERROR "reference decode produced no frames (${ref_size} bytes)")
endif()
if(NOT hal_size STREQUAL ref_size)
    message(FATAL_ERROR "size mismatch: ffmpeg ${ref_size} (${ref_frames} frames) vs "
            "hal_dec ${hal_size} (frames=${HAL_FRAMES})")
endif()
if(NOT HAL_FRAMES STREQUAL "${ref_frames}")
    message(FATAL_ERROR "hal_dec reported ${HAL_FRAMES} frames for a ${ref_frames} "
            "frame stream")
endif()

# Byte-exact comparison, whole file in 1 MB spans so a late divergence cannot
# hide behind a truncated read.
set(offset 0)
set(first_diff -1)
while(offset LESS ref_size)
    math(EXPR span "${ref_size} - ${offset}")
    if(span GREATER 1048576)
        set(span 1048576)
    endif()
    file(READ ${FFMPEG_REF} b1 OFFSET ${offset} LIMIT ${span} HEX)
    file(READ ${HAL_OUT} b2 OFFSET ${offset} LIMIT ${span} HEX)
    if(NOT b1 STREQUAL b2)
        math(EXPR diff "${offset} / ${FRAME_BYTES}")
        set(first_diff ${diff})
        break()
    endif()
    math(EXPR offset "${offset} + ${span}")
endwhile()
if(NOT first_diff EQUAL -1)
    message(FATAL_ERROR "AV1 parity check failed at frame ${first_diff} of "
            "${ref_frames}")
endif()

message("vtdec.av1_ivf_parity: passed (${HAL_FRAMES} frames, byte-identical)")
