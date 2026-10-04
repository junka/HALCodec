# vtdec_main10.cmake — decodes a real HEVC Main 10 stream with the vtbox decoder
# and asserts the frames match ffmpeg's own decode of the same stream byte for
# byte.
#
# The teeth are in the byte-exactness, and it pins three separate things at once:
# the row pitch (VideoToolbox hands back a packed 10-bit picture -- 'p420' for a
# video-range stream, 'pf20' for a full-range one, and both pad each row to a
# 64-byte pitch: 448 bytes for a 320-wide picture, 896 for 640, so a pitch
# derived from the width by any fixed ratio fails at one of the two sizes this
# case runs); the sample packing (three 10-bit samples per little-endian 32-bit
# word, not one per 16-bit word); and the alignment (HAL's P010 carries the ten
# bits in the top of each 16-bit word, so an unpack that forgets the shift leaves
# every sample 64 times too small). A 4:2:0 10-bit stream that decoded into the
# wrong layout would still produce the right number of frames and the right
# picture size, which is why nothing weaker than cmp is used here.
#
# The stream comes from libx265 rather than from hal_enc so that the encoder
# cannot cover for the decoder. Skips when ffmpeg has no libx265 or the backend
# cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)
set(H265_320 "${WORKDIR}/m10dec_320x240.h265")
set(H265_640 "${WORKDIR}/m10dec_640x360.h265")

# 10-bit source from the bundled 8-bit NV12; the second size pins the pitch law.
set(P010_320 "${WORKDIR}/m10dec_src_320x240_p010.yuv")
set(P010_640 "${WORKDIR}/m10dec_src_640x360_p010.yuv")
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12} -frames:v ${FRAMES} -pix_fmt p010le
                -f rawvideo -y ${P010_320}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtdec.hevc_main10: ffmpeg cannot produce p010le (rc=${rc})")
    return()
endif()
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12} -frames:v ${FRAMES}
                -vf scale=640:360 -pix_fmt p010le -f rawvideo -y ${P010_640}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtdec.hevc_main10: cannot build the 640x360 source (rc=${rc})")
    return()
endif()

foreach(size 320x240 640x360)
    string(REPLACE "x" ";" wh "${size}")
    list(GET wh 0 width)
    list(GET wh 1 height)
    math(EXPR frame_bytes "${width} * ${height} * 3")
    if(size STREQUAL "320x240")
        set(src "${P010_320}")
        set(stream "${H265_320}")
    else()
        set(src "${P010_640}")
        set(stream "${H265_640}")
    endif()

    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -f rawvideo -pixel_format p010le -video_size ${size}
                    -framerate 25 -i ${src} -c:v libx265 -preset ultrafast
                    -x265-params keyint=30 -frames:v ${FRAMES}
                    -f hevc -y ${stream}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE oerr ERROR_VARIABLE eerr)
    if(NOT rc EQUAL 0)
        message("SKIP vtdec.hevc_main10: this ffmpeg cannot encode Main 10 "
                "(${oerr}${eerr})")
        return()
    endif()

    # Confirm the assumption the rest of the case rests on: 10-bit, and the
    # profile VideoToolbox answers with a packed buffer.
    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error -select_streams v
                    -show_entries stream=profile,pix_fmt -of default=nw=1
                    ${stream}
                    RESULT_VARIABLE rc4 OUTPUT_VARIABLE probe ERROR_QUIET)
    if(NOT rc4 EQUAL 0)
        message(FATAL_ERROR "ffprobe cannot read ${stream}")
    endif()
    if(NOT probe MATCHES "profile=Main 10")
        message(FATAL_ERROR "the oracle stream is not Main 10 (${size}):\n${probe}")
    endif()
    if(NOT probe MATCHES "pix_fmt=yuv420p10le")
        message(FATAL_ERROR "the oracle stream is not 10-bit 4:2:0:\n${probe}")
    endif()

    set(FF_REF "${WORKDIR}/m10dec_${size}_ffmpeg_p010.yuv")
    set(HAL_OUT "${WORKDIR}/m10dec_${size}_hal_p010.yuv")
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -i ${stream} -f rawvideo -pix_fmt p010le -y ${FF_REF}
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message("SKIP vtdec.hevc_main10: ffmpeg reference decode failed (rc=${rc})")
        return()
    endif()

    execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${stream} -o ${HAL_OUT}
                    --codec hevc
                    RESULT_VARIABLE rc3 OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc3 EQUAL 137 OR rc3 GREATER 0)
        message("SKIP vtdec.hevc_main10: hal_dec exited with ${rc3}\n${out}${err}")
        return()
    endif()
    if(NOT out MATCHES "Decode total frames: ${FRAMES}")
        message(FATAL_ERROR "${stream} (${size}) decoded to not ${FRAMES} "
                "frames:\n${out}${err}")
    endif()
    file(SIZE ${HAL_OUT} hal_size)
    math(EXPR want "${frame_bytes} * ${FRAMES}")
    if(NOT hal_size EQUAL want)
        message(FATAL_ERROR "${size} Main 10 decoded to ${hal_size} bytes, expected "
                "${want} (${frame_bytes}/frame packed P010) -- an unpack that "
                "keeps the buffer's padded rows lands here")
    endif()

    # The real assertion: same samples, same order, same 16-bit alignment.
    execute_process(COMMAND cmp ${HAL_OUT} ${FF_REF}
                    RESULT_VARIABLE same OUTPUT_VARIABLE cmp_out ERROR_QUIET)
    if(NOT same EQUAL 0)
        message(FATAL_ERROR "${size}: hal_dec's P010 differs from ffmpeg's for the "
                "same stream (${cmp_out}) -- check the row pitch, the "
                "3-samples-per-word unpack, the MSB alignment and the chroma "
                "interleave")
    endif()
    message("vtdec.hevc_main10 ${size}: ${FRAMES} frames, ${hal_size} bytes, "
            "byte-identical to ffmpeg")
endforeach()
