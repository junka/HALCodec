# vtenc_jpeg.cmake — encodes three raw NV12 images with the vtbox JPEG encoder and
# checks what an image coder owes: one standalone JFIF codestream per input image,
# each holding the picture it was named after. Appending two JPEGs into one file
# leaves a stream no decoder can read past the first picture, so every output is
# probed on its own (mjpeg / Baseline / 320x240) and then compared by sample, not
# metadata: a picture encoded from the wrong input frame still probes perfectly.
# On this fixture the correct pairing measures ~32 dB and a crossed one ~22 dB
# (the gap against lossless is hal's full-range declaration, the same one the Main
# 10 case documents), so both sides of that gap are asserted. Skips when
# ffmpeg/ffprobe are missing, when the vtenc backend cannot load, or when
# VideoToolbox refuses JPEG.

if(NOT HAL_ENC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_ENC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAME_BYTES 115200)   # 320x240 NV12
set(IMAGES 3)
math(EXPR LAST "${IMAGES} - 1")

file(SIZE ${INPUT_NV12} src_size)
math(EXPR needed "${IMAGES} * ${FRAME_BYTES}")
if(src_size LESS needed)
    message("SKIP vtenc.jpeg: ${INPUT_NV12} holds fewer than ${IMAGES} frames")
    return()
endif()

# Fresh staging directory: with directory input hal_enc writes each image beside
# the file it was named after, so leftover outputs from a previous run would be
# read back as extra inputs.
set(JDIR "${WORKDIR}/jenc_320x240")
file(REMOVE_RECURSE "${JDIR}")
file(MAKE_DIRECTORY "${JDIR}")
foreach(i RANGE 0 ${LAST})
    # dd rather than file(READ HEX): CMake cannot write a binary file from hex.
    execute_process(COMMAND dd if=${INPUT_NV12} of=${JDIR}/f${i}.yuv
                    bs=${FRAME_BYTES} skip=${i} count=1
                    RESULT_VARIABLE rcs OUTPUT_QUIET ERROR_QUIET)
    file(SIZE "${JDIR}/f${i}.yuv" staged)
    if(NOT staged EQUAL ${FRAME_BYTES})
        message("SKIP vtenc.jpeg: could not stage frame ${i} (${staged} bytes)")
        return()
    endif()
endforeach()

execute_process(COMMAND ${HAL_ENC} -b vtenc --codec jpeg -f nv12 -i ${JDIR}
                --quality 90
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 137 OR rc GREATER 0)
    message("SKIP vtenc.jpeg: hal_enc exited with ${rc}\n${out}${err}")
    return()
endif()
if(NOT out MATCHES "Encode total frames: ${IMAGES}")
    message(FATAL_ERROR "hal_enc did not encode ${IMAGES} images:\n${out}${err}")
endif()

# Default output names take the codec's extension, so image i lands in f<i>.jpg
# beside the .yuv frame it came from.
foreach(i RANGE 0 ${LAST})
    set(jpg "${JDIR}/f${i}.jpg")

    file(SIZE ${jpg} size)
    if(NOT size GREATER 512)
        message(FATAL_ERROR "${jpg} holds ${size} bytes: the encoder did not give "
                "every image its own output file")
    endif()
    file(READ ${jpg} head LIMIT 2 HEX)
    math(EXPR tail_off "${size} - 2")
    file(READ ${jpg} tail OFFSET ${tail_off} LIMIT 2 HEX)
    string(TOUPPER "${head}" head)
    string(TOUPPER "${tail}" tail)
    if(NOT head STREQUAL "FFD8")
        message(FATAL_ERROR "${jpg} does not start with SOI (found ${head})")
    endif()
    if(NOT tail STREQUAL "FFD9")
        message(FATAL_ERROR "${jpg} does not end with EOI (found ${tail})")
    endif()

    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error
                    -show_entries stream=codec_name,profile,width,height
                    -of default=noprint_wrappers=1 ${jpg}
                    RESULT_VARIABLE rcf OUTPUT_VARIABLE meta ERROR_VARIABLE meta_err)
    if(NOT rcf EQUAL 0)
        message(FATAL_ERROR "ffprobe cannot read ${jpg}: ${meta_err}")
    endif()
    foreach(want "codec_name=mjpeg" "profile=Baseline" "width=320" "height=240")
        if(NOT meta MATCHES "${want}")
            message(FATAL_ERROR "${jpg} is not ${want}:\n${meta}")
        endif()
    endforeach()

    # Decoded samples, not container metadata: a picture encoded from the wrong
    # input frame still probes as a valid 320x240 baseline JPEG. Score every input
    # frame against this output and require its own frame to win -- that pins the
    # file assignment without depending on how lossy the encoder was. The margin is
    # real on this fixture: the matching frame measures ~32 dB, the next-best
    # ~22 dB, because consecutive cars frames are not nearly identical.
    set(own_psnr -1)
    set(rival_psnr -1)
    foreach(k RANGE 0 ${LAST})
        execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                        -s 320x240 -f rawvideo -pixel_format nv12
                        -i "${JDIR}/f${k}.yuv" -i ${jpg}
                        -lavfi "[0:v]settb=AVTB[a];[a][1:v]psnr" -f null -
                        RESULT_VARIABLE rcp ERROR_VARIABLE psnr_out OUTPUT_QUIET)
        if(NOT rcp EQUAL 0)
            message(FATAL_ERROR "psnr comparison failed for image ${i}: ${psnr_out}")
        endif()
        string(REGEX MATCH "average:([0-9]+\\.?[0-9]*)" _m "${psnr_out}")
        if(NOT CMAKE_MATCH_1)
            message(FATAL_ERROR "no psnr average for image ${i}: ${psnr_out}")
        endif()
        set(score "${CMAKE_MATCH_1}")
        if(k EQUAL i)
            set(own_psnr "${score}")
        elseif(score GREATER rival_psnr)
            set(rival_psnr "${score}")
        endif()
    endforeach()
    # A working encoder lands above 30 dB at q90; anything near the rival score is
    # the wrong frame, and a broken plane copy is single digits.
    if(NOT own_psnr GREATER 30)
        message(FATAL_ERROR "image ${i} does not match its own input frame (PSNR "
                "${own_psnr} dB, best rival ${rival_psnr} dB)")
    endif()
    if(NOT rival_psnr LESS own_psnr)
        message(FATAL_ERROR "image ${i} matches another input frame better than its "
                "own (${rival_psnr} dB vs ${own_psnr} dB) -- the per-frame buffers "
                "or output names are crossed")
    endif()
    message("vtenc.jpeg: image ${i} ok (${size} bytes, PSNR ${own_psnr} dB, "
            "best rival ${rival_psnr} dB)")
endforeach()

# The quality knob has to reach the session, not just be accepted. One image per
# directory keeps each run to a single output file.
set(QDIR "${WORKDIR}/jenc_q_320x240")
file(REMOVE_RECURSE "${QDIR}")
file(MAKE_DIRECTORY "${QDIR}")
file(COPY "${JDIR}/f0.yuv" DESTINATION "${QDIR}")
set(QSIZES "")
foreach(q 10 95)
    execute_process(COMMAND ${HAL_ENC} -b vtenc --codec jpeg -f nv12 -i ${QDIR}
                    --quality ${q}
                    RESULT_VARIABLE rcq OUTPUT_VARIABLE outq ERROR_VARIABLE errq)
    if(rcq EQUAL 137 OR rcq GREATER 0)
        message("SKIP vtenc.jpeg: quality sweep exited with ${rcq}\n${outq}${errq}")
        return()
    endif()
    file(SIZE "${QDIR}/f0.jpg" qsize)
    if(NOT qsize GREATER 512)
        message(FATAL_ERROR "--quality ${q} produced a ${qsize}-byte image")
    endif()
    list(APPEND QSIZES ${qsize})
    file(REMOVE "${QDIR}/f0.jpg")
endforeach()
list(GET QSIZES 0 q10_size)
list(GET QSIZES 1 q95_size)
if(NOT q10_size LESS q95_size)
    message(FATAL_ERROR "--quality is not applied: q10 gave ${q10_size} bytes, "
            "q95 gave ${q95_size}")
endif()
message("vtenc.jpeg: quality applied (q10 ${q10_size} B < q95 ${q95_size} B)")
