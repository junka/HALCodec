# vtdec_prores.cmake — decodes a real ProRes stream with the vtbox decoder and
# checks the two things a ProRes picture can get wrong. (1) The session is sized
# from the frame header, not from anything the container says: a description with
# the wrong dimensions is accepted in silence and hands back a buffer of the
# *described* size, so the frame is silently cropped or padded. (2) The output is
# labelled and cut by the pixel format VideoToolbox actually produced: ProRes
# returns 16-bit 4:2:2 'sv22', whose chroma plane is as tall as the luma one, so
# the half-height copy an NV12 frame needs drops half the picture. On this
# fixture the correct copy measures ~63 dB against ffmpeg's own decode of the same
# stream and a half-height chroma plane measures single digits. Also decodes a
# 4:4:4 stream, which VideoToolbox returns as a bi-planar 16-bit 'sv44' picture
# that has to reach HAL as a planar 16-bit 4:4:4 frame of the picture's own size
# (that half asserts its profile with ffprobe and skips without it), and asserts
# that an element stream ending mid-picture returns with the whole frames it holds
# rather than waiting forever for a byte count that will not be satisfied. Skips
# when ffmpeg is missing or the backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)
set(FRAME_BYTES 307200)   # 320x240 P210: 4:2:2, 16-bit storage (4 bytes/pixel)

set(MOV       "${WORKDIR}/pdec_422.mov")
set(ELEMENT   "${WORKDIR}/pdec_422.prores")
set(FF_REF    "${WORKDIR}/pdec_ffmpeg_ref.p210")
set(HAL_OUT   "${WORKDIR}/pdec_hal_out.p210")

# ProRes 422 (prores_ks profile 2) so the output is a format HAL can name.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12} -frames:v ${FRAMES} -c:v prores_ks -profile:v 2
                -y ${MOV}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtdec.prores: ffmpeg cannot encode ProRes (rc=${rc})")
    return()
endif()

# The decoder takes an elementary stream, and a .mov packet is byte-for-byte the
# frame it holds: ProRes frames are self-delimiting, so copying the packets with no
# container is the whole stream. (ffmpeg cannot read such a stream back, which is
# why the reference below is decoded from the .mov rather than from this file.)
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -i ${MOV} -c:v copy -f rawvideo -y ${ELEMENT}
                RESULT_VARIABLE rc2 OUTPUT_QUIET ERROR_QUIET)
if(NOT rc2 EQUAL 0)
    message("SKIP vtdec.prores: ffmpeg cannot write an element stream (rc=${rc2})")
    return()
endif()

execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${ELEMENT} -o ${HAL_OUT}
                RESULT_VARIABLE rc3 OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc3 EQUAL 137 OR rc3 GREATER 0)
    message("SKIP vtdec.prores: hal_dec exited with ${rc3}\n${out}${err}")
    return()
endif()
if(NOT out MATCHES "Decode total frames: ${FRAMES}")
    message(FATAL_ERROR "a ${FRAMES}-packet ProRes stream decoded to:\n${out}${err}")
endif()
file(SIZE ${HAL_OUT} hal_size)
math(EXPR want "${FRAMES} * ${FRAME_BYTES}")
if(NOT hal_size EQUAL want)
    message(FATAL_ERROR "${FRAMES} frames gave ${hal_size} bytes, expected ${want} "
            "(320x240 P210) -- check the picture size read from the frame header "
            "and the chroma plane height")
endif()

execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -i ${MOV} -frames:v ${FRAMES} -pix_fmt p210le -f rawvideo -y ${FF_REF}
                RESULT_VARIABLE rc4 OUTPUT_QUIET ERROR_QUIET)
if(NOT rc4 EQUAL 0)
    message("SKIP vtdec.prores: ffmpeg cannot decode to p210le (rc=${rc4})")
    return()
endif()

# Same samples or the frame is not the same picture. VideoToolbox keeps the extra
# bits below 10-bit precision that ffmpeg discards, so this is a PSNR rather than a
# byte compare: the two decoders measure ~63 dB apart here, while a half-height
# chroma copy or a 6-bit rescale lands in single digits.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                -s 320x240 -f rawvideo -pixel_format p210le -i ${FF_REF}
                -s 320x240 -f rawvideo -pixel_format p210le -i ${HAL_OUT}
                -lavfi "[0:v]settb=AVTB[a];[a][1:v]psnr" -f null -
                RESULT_VARIABLE rc5 ERROR_VARIABLE psnr_out OUTPUT_QUIET)
if(NOT rc5 EQUAL 0)
    message(FATAL_ERROR "psnr comparison failed: ${psnr_out}")
endif()
string(REGEX MATCH "average:([0-9]+)" _m "${psnr_out}")
set(PSNR "${CMAKE_MATCH_1}")
if(NOT PSNR GREATER 50)
    message(FATAL_ERROR "hal_dec and ffmpeg disagree on the same ProRes stream "
            "(PSNR ${PSNR} dB) -- check the dimensions, the per-plane copy and the "
            "chroma plane height:\n${psnr_out}")
endif()
message("vtdec.prores: passed (${FRAMES} frames, ${hal_size} bytes, PSNR ${PSNR} dB)")

# ---------------------------------------------------------------------------
# 4:4:4: VideoToolbox decodes it into a bi-planar 16-bit 'sv44' picture, whose
# chroma plane is as tall as the luma one and holds Cb and Cr interleaved at 4*w
# bytes a row, with rows kept on a 64-byte pitch. Both facts are load-bearing and
# each is caught twice over: the byte count goes wrong if the chroma plane is cut
# at h/2, if the interleaved pair is read as one sample per luma sample, or if the
# buffer's padded rows are handed over instead of the picture's own (which is why
# the second size below is 336 wide, where a padded row is 704 bytes and the
# picture's is 672); and the picture goes wrong if the even slot is taken for Cr
# rather than Cb, which no size check can see.
#
# The samples need no rescaling to compare with ffmpeg: measured, 'sv44' and
# ffmpeg's yuv444p16le put the same number in the same 16-bit word (one luma
# sample 53237 against 53232), so a handful of streams are byte-identical and the
# rest sit a few LSB apart -- hence the PSNR gate rather than cmp, and why an
# "inf" (identical) result counts as a pass.
# ---------------------------------------------------------------------------
set(F444 2)
if(NOT FFPROBE)
    message("vtdec.prores: skipped the 4:4:4 half (no ffprobe to assert the profile)")
else()
foreach(size 320x240 336x240)
    string(REPLACE "x" ";" wh "${size}")
    list(GET wh 0 w444)
    list(GET wh 1 h444)
    math(EXPR bytes444 "${w444} * ${h444} * 6")

    set(MOV444 "${WORKDIR}/pdec_4444_${size}.mov")
    set(ELEMENT444 "${WORKDIR}/pdec_4444_${size}.prores")
    set(HAL444 "${WORKDIR}/pdec_4444_${size}.yuv444p16le")
    set(FF444 "${WORKDIR}/pdec_4444_${size}_ffmpeg.yuv444p16le")
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                    -i ${INPUT_NV12} -frames:v ${F444} -s ${size}
                    -pix_fmt yuv444p10le -c:v prores_ks -profile:v 4
                    -y ${MOV444}
                    RESULT_VARIABLE rc6 OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc6 EQUAL 0)
        message("SKIP vtdec.prores_4444: ffmpeg has no 4:4:4 ProRes encoder (rc=${rc6})")
        break()
    endif()

    # Without this the case could pass on a 4:2:2 stream: prores_ks only really
    # writes 4:4:4 when its input is 4:4:4, and a profile silently falling back
    # would change what pixel format the decoder is being tested on.
    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error -select_streams v
                    -show_entries stream=profile,pix_fmt -of default=nw=1 ${MOV444}
                    RESULT_VARIABLE rc7 OUTPUT_VARIABLE probe444 ERROR_QUIET)
    if(NOT rc7 EQUAL 0)
        message(FATAL_ERROR "ffprobe cannot read ${MOV444}")
    endif()
    if(NOT probe444 MATCHES "profile=4444")
        message(FATAL_ERROR "the oracle stream is not ProRes 4444 (${size}):\n${probe444}")
    endif()

    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -i ${MOV444} -c:v copy -f rawvideo -y ${ELEMENT444}
                    RESULT_VARIABLE rc8 OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc8 EQUAL 0)
        message("SKIP vtdec.prores_4444: cannot make an element stream (rc=${rc8})")
        break()
    endif()

    execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${ELEMENT444} -o ${HAL444}
                    RESULT_VARIABLE rc9 OUTPUT_VARIABLE out444 ERROR_VARIABLE err444)
    if(rc9 EQUAL 137 OR rc9 GREATER 0)
        message(FATAL_ERROR "hal_dec refused a 4:4:4 ProRes stream (${size}) with "
                "${rc9}\n${out444}${err444}")
    endif()
    if(NOT out444 MATCHES "Decode total frames: ${F444}")
        message(FATAL_ERROR "${size} 4:4:4 decoded to:\n${out444}${err444}")
    endif()
    file(SIZE ${HAL444} hal444_size)
    math(EXPR want444 "${bytes444} * ${F444}")
    if(NOT hal444_size EQUAL want444)
        message(FATAL_ERROR "${size} 4:4:4 gave ${hal444_size} bytes, expected "
                "${want444} (${bytes444}/frame planar 16-bit 4:4:4) -- check the "
                "chroma plane height, the Cb:Cr split and the padded row pitch")
    endif()

    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -i ${MOV444} -frames:v ${F444} -pix_fmt yuv444p16le
                    -f rawvideo -y ${FF444}
                    RESULT_VARIABLE rc10 OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc10 EQUAL 0)
        message("SKIP vtdec.prores_4444: ffmpeg cannot decode to yuv444p16le")
        break()
    endif()

    execute_process(COMMAND cmp ${HAL444} ${FF444}
                    RESULT_VARIABLE same444 OUTPUT_QUIET ERROR_QUIET)
    if(same444 EQUAL 0)
        message("vtdec.prores_4444 ${size}: ${F444} frames, ${hal444_size} bytes, "
                "byte-identical to ffmpeg")
        continue()
    endif()
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                    -s ${size} -f rawvideo -pixel_format yuv444p16le -i ${FF444}
                    -s ${size} -f rawvideo -pixel_format yuv444p16le -i ${HAL444}
                    -lavfi "[0:v]settb=AVTB[a];[a][1:v]psnr" -f null -
                    RESULT_VARIABLE rc11 ERROR_VARIABLE psnr444 OUTPUT_QUIET)
    if(NOT rc11 EQUAL 0)
        message(FATAL_ERROR "psnr comparison failed: ${psnr444}")
    endif()
    string(REGEX MATCH "average:([0-9]+)" _m444 "${psnr444}")
    set(PSNR444 "${CMAKE_MATCH_1}")
    if(NOT PSNR444 GREATER 50)
        message(FATAL_ERROR "${size}: hal_dec and ffmpeg disagree on the same 4:4:4 "
                "stream (PSNR ${PSNR444} dB) -- check the Cb:Cr slot order and the "
                "per-row de-interleave:\n${psnr444}")
    endif()
    message("vtdec.prores_4444 ${size}: ${F444} frames, ${hal444_size} bytes, "
            "PSNR ${PSNR444} dB")
endforeach()
endif()

# A stream ending inside a picture must give back the pictures it holds and stop.
# The frame count is the proof it came back; the TIMEOUT is the proof it did not
# wait for the rest of the declared byte count.
set(ELEMENT_TRUNC "${WORKDIR}/pdec_trunc.prores")
file(SIZE ${ELEMENT} element_size)
# Cut a thousand bytes into the third picture, which is read from the two length
# counts in front of it rather than assumed: an arbitrary tail chop could land on a
# frame boundary and test nothing.
file(READ ${ELEMENT} first_hex OFFSET 0 LIMIT 4 HEX)
string(TOUPPER "${first_hex}" first_hex)
math(EXPR first_len "0x${first_hex}")
file(READ ${ELEMENT} second_hex OFFSET ${first_len} LIMIT 4 HEX)
string(TOUPPER "${second_hex}" second_hex)
math(EXPR second_len "0x${second_hex}")
math(EXPR cut "${first_len} + ${second_len} + 1000")
if(NOT cut LESS ${element_size})
    message("SKIP vtdec.prores: stream holds no third picture to cut into")
    return()
endif()
# dd rather than file(READ HEX): CMake cannot write a binary file from hex.
execute_process(COMMAND dd if=${ELEMENT} of=${ELEMENT_TRUNC} bs=${cut} count=1
                RESULT_VARIABLE rcz OUTPUT_QUIET ERROR_QUIET)
if(NOT rcz EQUAL 0)
    message("SKIP vtdec.prores: cannot truncate the element stream")
    return()
endif()
execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${ELEMENT_TRUNC}
                -o "${WORKDIR}/pdec_trunc.p210"
                RESULT_VARIABLE rc9 OUTPUT_VARIABLE outt ERROR_VARIABLE errt
                TIMEOUT 60)
if(NOT outt MATCHES "Decode total frames: 2")
    message(FATAL_ERROR "a stream cut mid-picture yielded '${outt}${errt}' (exit "
            "${rc9}) instead of the 2 whole frames it holds")
endif()
message("vtdec.prores: truncated picture handed back whole, no hang")
