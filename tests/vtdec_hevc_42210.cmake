# vtdec_hevc_42210.cmake — HEVC Main 4:2:2 10 through the vtbox decoder, whose
# buffer VideoToolbox calls 'p422' (or 'pf22' for a full-range stream).
#
# This is the 10-bit unpack of vtdec.hevc_main10 with the chroma geometry turned
# the other way round: the rows are packed three 10-bit samples per little-endian
# word at a 64-byte pitch exactly as in 'pf20', but the chroma plane has a row
# against every picture row rather than against every second one (measured: for a
# 320x240 picture plane 0 is 320x240 at rowBytes 448 and plane 1 is 160x240 at the
# same pitch, holding w samples a row -- w/2 Cb interleaved with w/2 Cr). Half the
# chroma rows is the single thing a 4:2:0-shaped copy of this buffer gets wrong,
# and it is the one thing a frame count, a picture size and a luma plane all fail to
# notice, so the assertion is the whole frame against ffmpeg's p210le.
#
# ffmpeg's p210le was checked to be the same bytes as its planar yuv422p10le
# shifted six bits up, interleaved Cb:Cr at h rows -- i.e. HAL's P210 -- which is
# why a plain cmp is legitimate here where the ProRes 4:2:2 case can only manage a
# PSNR gate (ProRes is not a bit-exact codec; HEVC is).
#
# Two sizes because the pitch is not a fixed ratio of the width: 448 bytes a row at
# 320 wide and 896 at 640. The stream comes from libx265, not from hal_enc, so the
# encoder cannot cover for the decoder, and ffprobe's pix_fmt is asserted first:
# 4:2:0 output from the same session would still decode, frame for frame, to the
# right number of bytes only if the sizes were recomputed -- a wrong chroma height
# is what falls out. Skips when ffmpeg has no libx265 4:2:2 10-bit support or the
# backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)

foreach(size 320x240 640x360)
    string(REPLACE "x" ";" wh "${size}")
    list(GET wh 0 width)
    list(GET wh 1 height)
    # P210: luma w*h samples and w interleaved chroma samples per picture row,
    # both in 16-bit words -- 4 bytes per pixel.
    math(EXPR frame_bytes "${width} * ${height} * 4")
    math(EXPR want_bytes "${frame_bytes} * ${FRAMES}")
    set(SRC "${WORKDIR}/m422dec_src_${size}_yuv422p10le.yuv")
    set(STREAM "${WORKDIR}/m422dec_${size}.h265")

    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                    -i ${INPUT_NV12} -vf scale=${width}:${height}
                    -frames:v ${FRAMES} -pix_fmt yuv422p10le -f rawvideo -y ${SRC}
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message("SKIP vtdec.hevc_42210: ffmpeg cannot produce yuv422p10le (rc=${rc})")
        return()
    endif()

    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -f rawvideo -pixel_format yuv422p10le -video_size ${size}
                    -framerate 25 -i ${SRC} -c:v libx265 -preset ultrafast
                    -x265-params keyint=30 -frames:v ${FRAMES} -f hevc -y ${STREAM}
                    RESULT_VARIABLE rc2 OUTPUT_VARIABLE oerr ERROR_VARIABLE eerr)
    if(NOT rc2 EQUAL 0)
        message("SKIP vtdec.hevc_42210: this ffmpeg cannot encode 4:2:2 10-bit "
                "(${oerr}${eerr})")
        return()
    endif()

    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error
                    -select_streams v -show_entries stream=profile,pix_fmt
                    -of default=nw=1 ${STREAM}
                    RESULT_VARIABLE rc3 OUTPUT_VARIABLE probe ERROR_QUIET)
    if(NOT rc3 EQUAL 0)
        message(FATAL_ERROR "ffprobe cannot read ${STREAM}")
    endif()
    if(NOT probe MATCHES "pix_fmt=yuv422p10le")
        message(FATAL_ERROR "the oracle stream is not 10-bit 4:2:2 (${size}):\n${probe}")
    endif()

    set(HAL_OUT "${WORKDIR}/m422dec_${size}_hal_p210.yuv")
    execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${STREAM} -o ${HAL_OUT}
                    --codec hevc
                    RESULT_VARIABLE rc4 OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc4 EQUAL 137 OR rc4 GREATER 0)
        message("SKIP vtdec.hevc_42210: hal_dec exited with ${rc4}\n${out}${err}")
        return()
    endif()
    if(NOT out MATCHES "Decode total frames: ${FRAMES}")
        message(FATAL_ERROR "${STREAM} (${size}) decoded to not ${FRAMES} frames:\n"
                "${out}${err}")
    endif()
    if(err MATCHES "which HAL has no pixel format for")
        message(FATAL_ERROR "${size} Main 4:2:2 10 decoded but the buffer's own "
                "format was refused: ${err}")
    endif()
    file(SIZE ${HAL_OUT} hal_size)
    if(NOT hal_size EQUAL want_bytes)
        message(FATAL_ERROR "${size} Main 4:2:2 10 decoded to ${hal_size} bytes, "
                "expected ${want_bytes} (${frame_bytes}/frame P210) -- a 4:2:0 "
                "chroma height would land exactly here")
    endif()

    set(FF_REF "${WORKDIR}/m422dec_${size}_ffmpeg_p210.yuv")
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error -i ${STREAM}
                    -frames:v ${FRAMES} -pix_fmt p210le -f rawvideo -y ${FF_REF}
                    RESULT_VARIABLE rc5 OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc5 EQUAL 0)
        message("SKIP vtdec.hevc_42210: ffmpeg reference decode to p210le failed "
                "(rc=${rc5})")
        return()
    endif()

    execute_process(COMMAND cmp ${HAL_OUT} ${FF_REF}
                    RESULT_VARIABLE same OUTPUT_VARIABLE cmp_out ERROR_QUIET)
    if(NOT same EQUAL 0)
        message(FATAL_ERROR "${size}: hal_dec's P210 differs from ffmpeg's for the "
                "same stream (${cmp_out}) -- check the chroma row count, the "
                "3-samples-per-word unpack, the MSB alignment and which half of "
                "the interleaved chroma row goes where")
    endif()
    message("vtdec.hevc_42210 ${size}: ${FRAMES} frames, ${hal_size} bytes, "
            "byte-identical to ffmpeg")
endforeach()
