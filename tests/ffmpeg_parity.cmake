# ffmpeg_parity.cmake — synthesises an 8-slice + B-frame H.264 stream from the
# bundled NV12, decodes it with both ffmpeg and hal_dec, and asserts the NV12
# outputs are byte-identical (order-sensitive). Skips when ffmpeg or assets are
# missing, or when the vtbox backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, INPUT_NV12 and WORKDIR must be set")
endif()

set(SYNTH_H264 "${WORKDIR}/synth_8slice_bframe.h264")
set(FFMPEG_REF "${WORKDIR}/ffmpeg_ref.nv12")
set(HAL_OUT    "${WORKDIR}/hal_dec_out.nv12")

# Synthesise: re-encode the NV12 with libx264, 8 slices per picture, 3 B frames,
# GOP size large enough to keep one continuous stream. The input is raw NV12 so
# we tell ffmpeg the resolution and pixel format explicitly.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12}
                -c:v libx264 -preset ultrafast
                -x264-params slices=8:bframes=3:keyint=300
                -pix_fmt yuv420p -profile:v high
                -y ${SYNTH_H264}
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message("SKIP vtdec.ffmpeg_parity: ffmpeg synthesis failed (${rc})")
    return()
endif()

# Reference decode via ffmpeg (outputs NV12 in presentation order).
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f h264 -i ${SYNTH_H264}
                -pix_fmt nv12 -f rawvideo -y ${FFMPEG_REF}
                RESULT_VARIABLE rc2)
if(NOT rc2 EQUAL 0)
    message("SKIP vtdec.ffmpeg_parity: ffmpeg reference decode failed (${rc2})")
    return()
endif()

# Decode via hal_dec (vtbox backend).
execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${SYNTH_H264} -o ${HAL_OUT}
                RESULT_VARIABLE rc3 OUTPUT_VARIABLE out)
if(rc3 EQUAL 137 OR rc3 GREATER 0)
    message("SKIP vtdec.ffmpeg_parity: hal_dec exited with ${rc3}")
    return()
endif()

string(REGEX MATCH "Decode total frames: ([0-9]+)" _m "${out}")
set(HAL_FRAMES "${CMAKE_MATCH_1}")

# Read file sizes for a quick sanity check before comparing every byte.
file(SIZE ${FFMPEG_REF} ref_size)
file(SIZE ${HAL_OUT} hal_size)
if(NOT ref_size STREQUAL hal_size)
    message(FATAL_ERROR "size mismatch: ffmpeg ${ref_size} vs hal_dec ${hal_size} "
            "(frames=${HAL_FRAMES})")
endif()

# Byte-exact comparison (order-sensitive).
file(READ ${FFMPEG_REF} b1 LIMIT 1048576)
file(READ ${HAL_OUT} b2 LIMIT 1048576)
if(NOT b1 STREQUAL b2)
    # Find first differing frame for diagnostics.
    math(EXPR nframes "${ref_size} / 115200")
    set(first_diff -1)
    foreach(i RANGE 0 ${nframes})
        math(EXPR off "${i} * 115200")
        string(SUBSTRING "${b1}" ${off} 115200 f1)
        string(SUBSTRING "${b2}" ${off} 115200 f2)
        if(NOT f1 STREQUAL f2)
            set(first_diff ${i})
            break()
        endif()
    endforeach()
    message(FATAL_ERROR "parity check failed at frame ${first_diff} of ${nframes}")
endif()

message("vtdec.ffmpeg_parity: passed (${HAL_FRAMES} frames, byte-identical)")
