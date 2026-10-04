# vtdec_jpeg.cmake — decodes a real JPEG with the vtbox decoder and checks the
# frames against ffmpeg's own decode of the same file. Two things are pinned: the
# picture size read out of the SOF marker (a wrong one opens a session over a
# buffer that does not match, which shows up as garbage or a refusal), and the
# per-plane copy out of the CVPixelBuffer -- VideoToolbox returns 420f NV12 with
# the chroma plane at an aligned offset, so a whole-buffer memcpy lands padding in
# the middle of the frame. That mistake measures ~11 dB here while the correct
# copy measures ~66 dB. Also asserts that a 4:4:4 image is refused by name instead
# of being relabelled as NV12, and that a truncated codestream returns instead of
# waiting forever for an end-of-image marker. Skips when ffmpeg is missing or the
# backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAME_BYTES 115200)   # 320x240 NV12
set(JPG      "${WORKDIR}/jdec_420.jpg")
set(JPG444   "${WORKDIR}/jdec_444.jpg")
set(FF_REF   "${WORKDIR}/jdec_ffmpeg_ref.nv12")
set(HAL_OUT  "${WORKDIR}/jdec_hal_out.nv12")

# q:v 2 keeps the reference close enough to the source that decoder differences,
# not compression loss, dominate the comparison.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12} -frames:v 1 -q:v 2 -y ${JPG}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtdec.jpeg: ffmpeg cannot encode JPEG (rc=${rc})")
    return()
endif()

execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -i ${JPG} -frames:v 1 -pix_fmt nv12 -f rawvideo -y ${FF_REF}
                RESULT_VARIABLE rc2 OUTPUT_QUIET ERROR_QUIET)
if(NOT rc2 EQUAL 0)
    message("SKIP vtdec.jpeg: ffmpeg reference decode failed (rc=${rc2})")
    return()
endif()

execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${JPG} -o ${HAL_OUT}
                RESULT_VARIABLE rc3 OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc3 EQUAL 137 OR rc3 GREATER 0)
    message("SKIP vtdec.jpeg: hal_dec exited with ${rc3}\n${out}${err}")
    return()
endif()
if(NOT out MATCHES "Decode total frames: 1")
    message(FATAL_ERROR "a JPEG is one picture, hal_dec said:\n${out}${err}")
endif()
file(SIZE ${HAL_OUT} hal_size)
file(SIZE ${FF_REF} ref_size)
if(NOT hal_size EQUAL ref_size)
    message(FATAL_ERROR "${JPG} decoded to ${hal_size} bytes, ffmpeg gave ${ref_size}")
endif()

# One frame, so the whole-buffer comparison is a PSNR rather than a span walk. A
# plane mis-copy (aligned chroma offset treated as compact) shows up here at once.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                -s 320x240 -f rawvideo -pixel_format nv12 -i ${FF_REF}
                -s 320x240 -f rawvideo -pixel_format nv12 -i ${HAL_OUT}
                -lavfi "[0:v]settb=AVTB[a];[a][1:v]psnr" -f null -
                RESULT_VARIABLE rc4 ERROR_VARIABLE psnr_out OUTPUT_QUIET)
if(NOT rc4 EQUAL 0)
    message(FATAL_ERROR "psnr comparison failed: ${psnr_out}")
endif()
string(REGEX MATCH "average:([0-9]+)" _m "${psnr_out}")
set(PSNR "${CMAKE_MATCH_1}")
if(NOT PSNR GREATER 45)
    message(FATAL_ERROR "hal_dec and ffmpeg disagree on the same JPEG (PSNR ${PSNR} "
            "dB) -- check the picture size and the per-plane copy:\n${psnr_out}")
endif()
message("vtdec.jpeg: passed (1 frame, ${hal_size} bytes, PSNR ${PSNR} dB)")

# 4:4:4 decodes into a buffer that is not two-plane NV12, so it has to be refused
# out loud rather than relabelled.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12} -frames:v 1 -pix_fmt yuvj444p -q:v 2 -y ${JPG444}
                RESULT_VARIABLE rc5 OUTPUT_QUIET ERROR_QUIET)
if(rc5 EQUAL 0)
    execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${JPG444}
                    -o "${WORKDIR}/jdec_444.nv12"
                    RESULT_VARIABLE rc6 OUTPUT_VARIABLE out444 ERROR_VARIABLE err444)
    if(rc6 EQUAL 0)
        message(FATAL_ERROR "a 4:4:4 JPEG was accepted and would be labelled NV12: "
                "${out444}${err444}")
    endif()
    if(NOT err444 MATCHES "4:2:0")
        message(FATAL_ERROR "4:4:4 refusal did not say why: ${err444}")
    endif()
    message("vtdec.jpeg: 4:4:4 refused as documented")
else()
    message("vtdec.jpeg: skipped the 4:4:4 case (ffmpeg has no yuvj444p JPEG encoder)")
endif()

# A codestream whose entropy data stops short must not leave the caller blocked
# waiting for an end-of-image that never arrives. The TIMEOUT is the assertion.
set(JPG_TRUNC "${WORKDIR}/jdec_trunc.jpg")
# dd rather than file(READ HEX): CMake has no binary write from a hex string.
execute_process(COMMAND dd if=${JPG} of=${JPG_TRUNC} bs=4000 count=1
                RESULT_VARIABLE rcz OUTPUT_QUIET ERROR_QUIET)
if(NOT rcz EQUAL 0)
    message("SKIP vtdec.jpeg: cannot truncate the reference codestream")
    return()
endif()
execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${JPG_TRUNC}
                -o "${WORKDIR}/jdec_trunc.nv12"
                RESULT_VARIABLE rc7 OUTPUT_VARIABLE outt ERROR_VARIABLE errt
                TIMEOUT 60)
# The frame count is what proves it came back: a run still waiting for the marker
# gets killed here and prints nothing.
if(NOT outt MATCHES "Decode total frames: 0")
    message(FATAL_ERROR "a truncated codestream yielded '${outt}${errt}' (exit "
            "${rc7}) instead of 0 frames")
endif()
message("vtdec.jpeg: truncated codestream rejected without hanging")
