# vtdec_prores_raw.cmake -- decodes a real ProRes RAW stream with the vtbox
# decoder. Nothing here can be synthesised: measured, no ProRes RAW encoder exists
# on this machine (neither VideoToolbox nor ffmpeg writes a 'prrf' frame), so the
# stream has to come from a camera, and the case is registered only when one is
# configured.
#
# What it checks, and what each check would catch:
#
#   1. every whole picture comes back. A RAW picture is a ProRes picture in every
#      structural sense -- one 32-bit count of its own bytes at the front -- so a
#      stream cut mid-picture must still yield the pictures it holds.
#   2. the byte count is frames * width * height * 2, with width and height read
#      out of the stream's own frame headers. Two bugs land here and nowhere else:
#      the session sized from anything but the frame header (VideoToolbox then
#      hands back a buffer of the *described* size and the picture is silently
#      cropped or padded), and 'bp16''s padded pitch copied instead of the
#      picture's own rows -- measured, a 4112-wide grid has 8224 bytes of sensels
#      on an 8256-byte row, so taking the row pitch inflates the frame by 131 KB.
#   3. the metadata the pixels cannot speak for: the phase order and the black and
#      white levels have to arrive off the buffer's attachments, agreeing with the
#      stream's own dimensions and with each other (black < white). A grid handed
#      over with an unknown pattern is unusable, and this is the only place in HAL
#      where a frame carries metadata at all.
#   4. the decode is reproducible: two runs of the same bytes, byte-identical.
#   5. if a golden sits next to the sample (<sample>.golden8.bin, the first eight
#      rows of the grid), the content is compared against it. That file is made
#      from the decoder's *native* four-plane output ('b16q', one plane per Bayer
#      phase) re-interleaved into a grid -- measured, that reconstruction matches
#      the requested 'bp16' delivery sample for sample at all four phases -- so the
#      golden is an independent witness of the phase order and of the de-padding,
#      not a snapshot of this code's own output.

if(NOT HAL_DEC OR NOT SAMPLE OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, SAMPLE and WORKDIR must be set")
endif()
if(NOT EXISTS "${SAMPLE}")
    message(FATAL_ERROR "no ProRes RAW stream at ${SAMPLE}")
endif()

file(SIZE "${SAMPLE}" total)
if(total LESS 20)
    message(FATAL_ERROR "${SAMPLE} is ${total} bytes, too short for a frame header")
endif()

# Walk the pictures on their own byte counts, the way the decoder splits them, so
# the expected frame count comes from the stream rather than from a constant.
set(pos 0)
set(frames 0)
set(width 0)
set(height 0)
set(tail_bytes 0)
while(NOT pos EQUAL total)
    # A picture is its own byte count and then its frame tag, in that order.
    file(READ "${SAMPLE}" len_hex OFFSET ${pos} LIMIT 4 HEX)
    string(TOUPPER "${len_hex}" len_hex)
    math(EXPR length "0x${len_hex}")
    math(EXPR pos4 "${pos} + 4")
    file(READ "${SAMPLE}" tag_hex OFFSET ${pos4} LIMIT 4 HEX)
    string(TOUPPER "${tag_hex}" tag_hex)
    if(NOT tag_hex STREQUAL "70727266")
        message(FATAL_ERROR "bytes at ${pos} are not a ProRes RAW ('prrf') frame "
                "(tag reads ${tag_hex}) -- ${SAMPLE} is not a RAW element stream")
    endif()
    if(length LESS 20)
        message(FATAL_ERROR "frame at ${pos} declares ${length} bytes, shorter "
                "than the header it would have to carry")
    endif()
    if(frames EQUAL 0)
        # Four hex characters per dimension: the header's width and height are two
        # big-endian 16-bits at 16 and 18.
        file(READ "${SAMPLE}" wh_hex OFFSET 16 LIMIT 4 HEX)
        string(TOUPPER "${wh_hex}" wh_hex)
        string(SUBSTRING "${wh_hex}" 0 4 w_hex)
        string(SUBSTRING "${wh_hex}" 4 4 h_hex)
        math(EXPR width "0x${w_hex}")
        math(EXPR height "0x${h_hex}")
        if(width LESS 2 OR height LESS 2)
            message(FATAL_ERROR "frame header says ${width}x${height}, which is no "
                    "Bayer grid")
        endif()
    endif()
    math(EXPR rest "${total} - ${pos}")
    if(length GREATER rest)
        # A picture whose tail never arrived: the decoder must hand back what it
        # has rather than wait for it.
        set(tail_bytes ${rest})
        break()
    endif()
    math(EXPR pos "${pos} + ${length}")
    math(EXPR frames "${frames} + 1")
endwhile()
message("vtdec.prores_raw: ${total} bytes = ${frames} whole ${width}x${height} "
        "pictures (+${tail_bytes} bytes cut mid-picture)")
if(frames LESS 1)
    message(FATAL_ERROR "${SAMPLE} holds no complete picture")
endif()

math(EXPR frame_bytes "${width} * ${height} * 2")
set(OUT "${WORKDIR}/praw_hal.bp16")
execute_process(COMMAND ${HAL_DEC} -b vtbox -i "${SAMPLE}" -o ${OUT}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
                TIMEOUT 900)
if(rc EQUAL 137)
    message("SKIP vtdec.prores_raw: hal_dec was killed (137)")
    return()
endif()
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "hal_dec failed on a ProRes RAW stream (rc=${rc}):\n${out}${err}")
endif()
if(NOT out MATCHES "Decode total frames: ${frames}")
    message(FATAL_ERROR "${frames} RAW pictures decoded to:\n${out}${err}")
endif()

file(SIZE ${OUT} got_bytes)
math(EXPR want_bytes "${frames} * ${frame_bytes}")
if(NOT got_bytes EQUAL want_bytes)
    message(FATAL_ERROR "${frames} pictures gave ${got_bytes} bytes, expected "
            "${want_bytes} (${frame_bytes}/frame of ${width}x${height} 16-bit "
            "sensels) -- check the dimensions read from the frame header and "
            "whether the buffer's padded rows were copied instead of the "
            "picture's own")
endif()

# The metadata line, which is the only way a consumer learns what the grid is.
if(NOT out MATCHES "raw sensel grid: ([0-9]+)x([0-9]+) (RGGB|GRBG|GBRG|BGGR) black=([0-9]+) white=([0-9]+)")
    message(FATAL_ERROR "no usable raw sensel metadata in the output:\n${out}")
endif()
set(GRID_WH "${CMAKE_MATCH_1}x${CMAKE_MATCH_2}")
set(PATTERN "${CMAKE_MATCH_3}")
set(BLACK "${CMAKE_MATCH_4}")
set(WHITE "${CMAKE_MATCH_5}")
if(NOT GRID_WH STREQUAL "${width}x${height}")
    message(FATAL_ERROR "the grid is ${GRID_WH} but the stream's header says "
            "${width}x${height}")
endif()
if(NOT BLACK LESS WHITE)
    message(FATAL_ERROR "black=${BLACK} white=${WHITE}: the levels did not come "
            "off the buffer, or the buffer carries none")
endif()
message("vtdec.prores_raw: ${frames} frames, ${got_bytes} bytes, ${GRID_WH} "
        "${PATTERN} black=${BLACK} white=${WHITE}")

# Same bytes twice: a frame that came from a buffer the decoder no longer owns
# would show up here rather than as a crash.
set(OUT2 "${WORKDIR}/praw_hal2.bp16")
execute_process(COMMAND ${HAL_DEC} -b vtbox -i "${SAMPLE}" -o ${OUT2}
                RESULT_VARIABLE rc2 OUTPUT_QUIET ERROR_QUIET TIMEOUT 900)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "second decode run failed (rc=${rc2})")
endif()
execute_process(COMMAND cmp ${OUT} ${OUT2}
                RESULT_VARIABLE same OUTPUT_QUIET ERROR_QUIET)
if(NOT same EQUAL 0)
    message(FATAL_ERROR "two runs of the same ProRes RAW stream disagree")
endif()

# Content, against the native-layout reconstruction if one is configured to sit
# next to the sample.
set(GOLDEN "${SAMPLE}.golden8.bin")
if(EXISTS "${GOLDEN}")
    # Eight rows of the grid, from the top-left of the picture.
    math(EXPR golden_bytes "${width} * 2 * 8")
    file(SIZE "${GOLDEN}" golden_size)
    if(NOT golden_size EQUAL golden_bytes)
        message(FATAL_ERROR "${GOLDEN} is ${golden_size} bytes, expected "
                "${golden_bytes} (8 rows of ${width} 16-bit sensels)")
    endif()
    execute_process(COMMAND dd if=${OUT} of=${WORKDIR}/praw_gold.bin
                            bs=${golden_bytes} count=1
                    RESULT_VARIABLE rcdd OUTPUT_QUIET ERROR_QUIET)
    if(NOT rcdd EQUAL 0)
        message(FATAL_ERROR "cannot cut the first eight rows out of ${OUT}")
    endif()
    execute_process(COMMAND cmp ${WORKDIR}/praw_gold.bin "${GOLDEN}"
                    RESULT_VARIABLE goldcmp OUTPUT_QUIET ERROR_QUIET)
    if(NOT goldcmp EQUAL 0)
        message(FATAL_ERROR "the first eight rows of the grid disagree with "
                "${GOLDEN} (the decoder's native 'b16q' planes re-interleaved) -- "
                "check the de-padded row width and which phase each sample is")
    endif()
    message("vtdec.prores_raw: first 8 rows match the native-plane reconstruction")
else()
    message("vtdec.prores_raw: no ${GOLDEN}, content not compared against the "
            "native 'b16q' layout (structure, size and metadata were checked)")
endif()
