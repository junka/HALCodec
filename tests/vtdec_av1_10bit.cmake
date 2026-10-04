# vtdec_av1_10bit.cmake — decodes a real 10-bit AV1 stream with the vtbox decoder
# and asserts the frames match ffmpeg's own CPU decode of the same stream byte
# for byte.
#
# This is the AV1 half of what vtdec_main10.cmake does for HEVC, and the two
# sizes are not redundancy: the packed 10-bit rows VideoToolbox hands back are
# padded to a 64-byte pitch, so row pitch is ceil(ceil(w/3)*4/64)*64 -- 448 at
# 320 wide but 896 at 640, i.e. row/w is 1.4 at one size and 1.4 again by a
# different route, and a pitch derived from the width by any fixed ratio breaks
# at one of them. Together with the unpack (three 10-bit samples per little-endian
# 32-bit word), the MSB alignment (HAL's P010 keeps the ten bits at the top of
# each 16-bit word, so a forgotten shift leaves every sample 64 times too small)
# and the plane-1 UV interleave, byte-exactness pins the whole chain. Nothing
# weaker than cmp is used: a wrong layout still yields the right frame count and
# the right picture size.
#
# The bit-depth assertion on the source stream is what makes the case worth
# running at all. libsvtav1 on this machine accepts -pix_fmt yuv420p10le and
# emits seq_profile 0 with high_bitdepth set -- a bit-depth fallback that
# silently produced 8-bit output instead would leave this script passing while
# testing nothing.
#
# Skips when ffmpeg has no libsvtav1 10-bit path or the backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)

foreach(size 320x240 640x360)
    string(REPLACE "x" ";" wh "${size}")
    list(GET wh 0 width)
    list(GET wh 1 height)
    math(EXPR frame_bytes "${width} * ${height} * 3")

    # 10-bit source from the bundled 8-bit NV12.
    set(SRC_P010 "${WORKDIR}/av1m10_src_${size}_p010.yuv")
    set(src_args ${FFMPEG} -hide_banner -loglevel error
                 -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                 -i ${INPUT_NV12} -frames:v ${FRAMES})
    if(NOT size STREQUAL "320x240")
        list(APPEND src_args -vf scale=${size})
    endif()
    list(APPEND src_args -pix_fmt p010le -f rawvideo -y ${SRC_P010})
    execute_process(COMMAND ${src_args}
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message("SKIP vtdec.av1_10bit: ffmpeg cannot produce p010le (rc=${rc})")
        return()
    endif()

    set(STREAM "${WORKDIR}/av1m10_${size}.ivf")
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -f rawvideo -pixel_format p010le -video_size ${size}
                    -framerate 25 -i ${SRC_P010}
                    -c:v libsvtav1 -preset 8 -pix_fmt yuv420p10le
                    -frames:v ${FRAMES} -f ivf -y ${STREAM}
                    RESULT_VARIABLE rc2 OUTPUT_VARIABLE oerr ERROR_VARIABLE eerr)
    if(NOT rc2 EQUAL 0)
        message("SKIP vtdec.av1_10bit: this ffmpeg cannot encode 10-bit AV1 "
                "(${oerr}${eerr})")
        return()
    endif()

    # The assumption the rest of the case rests on: the stream really is 10-bit
    # 4:2:0, otherwise hal_dec is decoding an 8-bit picture and passing for the
    # wrong reason.
    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error -select_streams v
                    -show_entries stream=pix_fmt,codec_name -of default=nw=1
                    ${STREAM}
                    RESULT_VARIABLE rc3 OUTPUT_VARIABLE probe ERROR_QUIET)
    if(NOT rc3 EQUAL 0)
        message(FATAL_ERROR "ffprobe cannot read ${STREAM}")
    endif()
    if(NOT probe MATCHES "pix_fmt=yuv420p10le")
        message(FATAL_ERROR "the oracle stream is not 10-bit 4:2:0 (${size}):\n${probe}")
    endif()

    set(FF_REF "${WORKDIR}/av1m10_${size}_ffmpeg_p010.yuv")
    set(HAL_OUT "${WORKDIR}/av1m10_${size}_hal_p010.yuv")
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -f ivf -i ${STREAM} -pix_fmt p010le -f rawvideo -y ${FF_REF}
                    RESULT_VARIABLE rc4 OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc4 EQUAL 0)
        message("SKIP vtdec.av1_10bit: ffmpeg CPU reference decode failed (rc=${rc4})")
        return()
    endif()

    execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${STREAM} -o ${HAL_OUT}
                    RESULT_VARIABLE rc5 OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc5 EQUAL 137 OR rc5 GREATER 0)
        message("SKIP vtdec.av1_10bit: hal_dec exited with ${rc5}\n${out}${err}")
        return()
    endif()
    if(NOT out MATCHES "Decode total frames: ${FRAMES}")
        message(FATAL_ERROR "${STREAM} (${size}) decoded to not ${FRAMES} "
                "frames:\n${out}${err}")
    endif()
    file(SIZE ${HAL_OUT} hal_size)
    math(EXPR want "${frame_bytes} * ${FRAMES}")
    if(NOT hal_size EQUAL want)
        message(FATAL_ERROR "${size} 10-bit AV1 decoded to ${hal_size} bytes, expected "
                "${want} (${frame_bytes}/frame packed P010) -- a decode that keeps "
                "the buffer's padded rows lands here")
    endif()

    # The real assertion: same samples, same order, same 16-bit alignment.
    execute_process(COMMAND cmp ${HAL_OUT} ${FF_REF}
                    RESULT_VARIABLE same OUTPUT_VARIABLE cmp_out ERROR_QUIET)
    if(NOT same EQUAL 0)
        message(FATAL_ERROR "${size}: hal_dec's P010 differs from ffmpeg's for the "
                "same AV1 stream (${cmp_out}) -- check the row pitch, the "
                "3-samples-per-word unpack, the MSB alignment and the chroma "
                "interleave")
    endif()
    message("vtdec.av1_10bit ${size}: ${FRAMES} frames, ${hal_size} bytes, "
            "byte-identical to ffmpeg")
endforeach()
