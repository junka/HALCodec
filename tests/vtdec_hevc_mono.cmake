# vtdec_hevc_mono.cmake — the two monochrome HEVC profiles through the vtbox
# decoder: Main Monochrome, whose buffer VideoToolbox calls 'L008', and Main 10
# Monochrome ('L010').
#
# Neither has a chroma plane, and the API says so by reporting *no planes at all*
# for these buffers (measured: 0), so the picture is one allocation read through
# CVPixelBufferGetBaseAddress rather than a plane index. Two things pin that read:
# the byte count (w*h per frame at 8 bits, w*h*2 at 10, because HAL's greyscale
# frames leave tightly packed the way every other single-plane grey frame in HAL
# does -- nvjpeg's GRAY is w bytes a row) and ffmpeg's samples for the same
# codestream.
#
# The sizes are the teeth for the pitch, and neither letter is enough on its own:
# VideoToolbox pads these rows to a 64-byte pitch, which at 320 and 640 wide is
# exactly the row the format carries (320, 640) but at 336 wide is not (384 bytes a
# row for 'L008' against a 336-byte one, 704 for 'L010' against 672). So 336x248 is
# what makes a stride invented from the width instead of the buffer, and a copy
# that walks the source as if it were already tight, both fail; the two aligned
# sizes cannot see either mistake. 336x248 also widens the gap between the buffer's
# dataSize and the rows it holds (measured: 98368 against 248 rows of 384 = 95232
# for 'L008', 180288 against 248 of 704 = 174592 for 'L010'; at 320x240 the same
# gap is 64 bytes), so a copy sized from dataSize instead of the rows reads bytes
# that belong to no pixel.
#
# The 10-bit parity is the awkward one and it is worth being exact about why:
# VideoToolbox stores the ten bits at the top of each 16-bit word, which is what
# HAL's GRAY10LE says, while ffmpeg's gray10le keeps them at the bottom and every
# 10↔16-bit conversion it offers *rescales* rather than shifts (measured: 897 goes
# to 57464, not 57408 -- so no grey output of ffmpeg's can be compared byte for
# byte). The reference therefore comes out of a p010le frame, where the luma plane
# holds the ten bits at the top exactly as HAL carries them, and the comparison
# takes that plane per frame: a p010le raw file is 3*w*h bytes, the picture's luma
# then a flat chroma plane, and that chroma is ffmpeg's invention since the
# picture has none, so only the leading w*h*2 bytes of each frame are compared --
# which is why the case asserts the reference's own frame size before trusting any
# offset into it. `-vf
# scale=in_range=pc:out_range=pc` is not decoration -- without it swscale treats
# the grey→semi-planar step as a range change and every sample comes back
# compressed (measured 897 → 53248 rather than 57408).
#
# Streams come from libx265 rather than from hal_enc so the encoder cannot cover
# for the decoder, and ffprobe's pix_fmt is asserted first: a stream that quietly
# stayed 4:2:0 would make this case test nothing. Skips when ffmpeg has no libx265
# mono support or the backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)

# HAL format | ffmpeg raw format for the source | bytes a decoded frame carries |
# the pix_fmt ffprobe must report | ffmpeg's own decode format.
set(CASES
    "gray|gray|1|gray"
    "gray10le|gray10le|2|p010le")

foreach(case ${CASES})
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 src_fmt)
    list(GET parts 1 want_probe_fmt)
    list(GET parts 2 bytes_per_sample)
    list(GET parts 3 ref_fmt)
    if(src_fmt STREQUAL "gray10le")
        set(is10 1)
    else()
        set(is10 0)
    endif()

    foreach(size 320x240 640x360 336x248)
        string(REPLACE "x" ";" wh "${size}")
        list(GET wh 0 width)
        list(GET wh 1 height)
        math(EXPR frame_bytes "${width} * ${height} * ${bytes_per_sample}")
        math(EXPR want_bytes "${frame_bytes} * ${FRAMES}")
        set(tag "mono_${src_fmt}_${size}")
        set(RAW "${WORKDIR}/${tag}_src.yuv")
        set(STREAM "${WORKDIR}/${tag}.h265")

        execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                        -f rawvideo -pixel_format nv12 -video_size 320x240
                        -framerate 25 -i ${INPUT_NV12}
                        -vf scale=${width}:${height} -frames:v ${FRAMES}
                        -pix_fmt ${src_fmt} -f rawvideo -y ${RAW}
                        RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
        if(NOT rc EQUAL 0)
            message("SKIP vtdec.hevc_mono: ffmpeg cannot produce ${src_fmt} (rc=${rc})")
            return()
        endif()

        execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                        -f rawvideo -pixel_format ${src_fmt} -video_size ${size}
                        -framerate 25 -i ${RAW} -c:v libx265 -preset ultrafast
                        -x265-params mono=1 -frames:v ${FRAMES} -f hevc -y ${STREAM}
                        RESULT_VARIABLE rc2 OUTPUT_VARIABLE oerr ERROR_VARIABLE eerr)
        if(NOT rc2 EQUAL 0)
            message("SKIP vtdec.hevc_mono: this ffmpeg cannot encode a mono ${src_fmt} "
                    "stream (${oerr}${eerr})")
            return()
        endif()

        # The assumption the rest of the case rests on: the codestream really is
        # single-component, at the depth under test.
        execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error
                        -select_streams v -show_entries stream=pix_fmt
                        -of default=nw=1 ${STREAM}
                        RESULT_VARIABLE rc3 OUTPUT_VARIABLE probe ERROR_QUIET)
        if(NOT rc3 EQUAL 0)
            message(FATAL_ERROR "ffprobe cannot read ${STREAM}")
        endif()
        if(NOT probe MATCHES "pix_fmt=${want_probe_fmt}")
            message(FATAL_ERROR "${src_fmt} at ${size} encoded as ${probe}, not "
                    "${want_probe_fmt} -- a stream that fell back to 4:2:0 leaves "
                    "this case asserting nothing")
        endif()

        set(HAL_OUT "${WORKDIR}/${tag}_hal.yuv")
        execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${STREAM} -o ${HAL_OUT}
                        --codec hevc
                        RESULT_VARIABLE rc4 OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(rc4 EQUAL 137 OR rc4 GREATER 0)
            message("SKIP vtdec.hevc_mono: hal_dec exited with ${rc4}\n${out}${err}")
            return()
        endif()
        if(NOT out MATCHES "Decode total frames: ${FRAMES}")
            message(FATAL_ERROR "${STREAM} (${size}) decoded to not ${FRAMES} "
                    "frames:\n${out}${err}")
        endif()
        if(err MATCHES "which HAL has no pixel format for")
            message(FATAL_ERROR "${size} ${src_fmt} decoded but the buffer's own "
                    "format was refused: ${err}")
        endif()
        file(SIZE ${HAL_OUT} hal_size)
        if(NOT hal_size EQUAL want_bytes)
            message(FATAL_ERROR "${size} ${src_fmt} decoded to ${hal_size} bytes, "
                    "expected ${want_bytes} (${frame_bytes}/frame) -- that is a "
                    "row pitch taken from the width instead of the buffer, or a "
                    "chroma plane that does not exist")
        endif()

        set(FF_REF "${WORKDIR}/${tag}_ffmpeg.yuv")
        if(is10)
            execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                            -i ${STREAM} -frames:v ${FRAMES}
                            -vf "scale=in_range=pc:out_range=pc"
                            -pix_fmt ${ref_fmt} -f rawvideo -y ${FF_REF}
                            RESULT_VARIABLE rc5 OUTPUT_QUIET ERROR_QUIET)
        else()
            execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                            -i ${STREAM} -frames:v ${FRAMES}
                            -pix_fmt ${ref_fmt} -f rawvideo -y ${FF_REF}
                            RESULT_VARIABLE rc5 OUTPUT_QUIET ERROR_QUIET)
        endif()
        if(NOT rc5 EQUAL 0)
            message("SKIP vtdec.hevc_mono: ffmpeg reference decode to ${ref_fmt} "
                    "failed (rc=${rc5})")
            return()
        endif()

        if(is10)
            # The reference frame is luma (w*h*2) then flat chroma (w*h), so the
            # picture is one plane at the head of each frame: walk them and compare
            # the luma bytes only.
            math(EXPR ref_frame "${width} * ${height} * 3")
            math(EXPR last "${FRAMES} - 1")
            file(SIZE ${FF_REF} ref_size)
            math(EXPR want_ref "${ref_frame} * ${FRAMES}")
            if(NOT ref_size EQUAL want_ref)
                message(FATAL_ERROR "ffmpeg's ${ref_fmt} reference is ${ref_size} "
                        "bytes, expected ${want_ref}")
            endif()
            set(mismatch "")
            foreach(f RANGE 0 ${last})
                math(EXPR ref_off "${f} * ${ref_frame}")
                math(EXPR hal_off "${f} * ${frame_bytes}")
                file(READ ${FF_REF} refhex OFFSET ${ref_off} LIMIT ${frame_bytes} HEX)
                file(READ ${HAL_OUT} halhex OFFSET ${hal_off} LIMIT ${frame_bytes} HEX)
                # Two short reads compare equal to each other, so insist that both
                # really returned a whole plane before believing the match.
                string(LENGTH "${refhex}" refhex_len)
                string(LENGTH "${halhex}" halhex_len)
                math(EXPR want_hex "${frame_bytes} * 2")
                if(NOT refhex_len EQUAL want_hex OR NOT halhex_len EQUAL want_hex)
                    message(FATAL_ERROR "${size} frame ${f}: read ${refhex_len} and "
                            "${halhex_len} hex characters, expected ${want_hex} -- "
                            "the luma plane is not where p010le says it is")
                endif()
                if(NOT refhex STREQUAL halhex)
                    set(mismatch ${f})
                endif()
            endforeach()
            if(NOT mismatch STREQUAL "")
                message(FATAL_ERROR "${size} 'L010' luma differs from ffmpeg's for "
                        "the same stream (first differing frame ${mismatch}) -- "
                        "check the 10-bit alignment (HAL and 'L010' both put the ten "
                        "bits at the top of the 16-bit word), the row pitch and the "
                        "single-plane read")
            endif()
        else()
            execute_process(COMMAND cmp ${HAL_OUT} ${FF_REF}
                            RESULT_VARIABLE same OUTPUT_VARIABLE cmp_out ERROR_QUIET)
            if(NOT same EQUAL 0)
                message(FATAL_ERROR "${size}: hal_dec's grey output differs from "
                        "ffmpeg's for the same stream (${cmp_out}) -- check the row "
                        "pitch and the tail padding that belongs to no row")
            endif()
        endif()
        message("vtdec.hevc_mono ${src_fmt} ${size}: ${FRAMES} frames, "
                "${hal_size} bytes, byte-identical to ffmpeg")
    endforeach()
endforeach()
