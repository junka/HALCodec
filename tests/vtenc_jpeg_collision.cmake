# vtenc_jpeg_collision.cmake — the directory-input case where an output names the
# input it was made from. Directory outputs are "<input without ext>.<format>", and
# --format names the *raw* layout, so `-f nv12` over a directory of .nv12 frames
# gives every picture the path of its own frame. That used to cost two things: the
# first output was opened for writing before any input was read, so its input read
# back empty and was never encoded (4 inputs gave 3 pictures), and every picture
# after it landed one name off -- still a valid JPEG, still 320x240, so only the
# sample comparison sees it. Same fixture as vtenc.jpeg, which stays on .yuv inputs
# where nothing collides.
#
# Asserted: one picture per input, no output left empty, no file invented, and each
# file holding the frame it was named after (own frame > 30 dB and better than every
# rival). Skips when ffmpeg/ffprobe or the vtenc backend are missing.

if(NOT HAL_ENC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_ENC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAME_BYTES 115200)   # 320x240 NV12
set(IMAGES 4)
math(EXPR LAST "${IMAGES} - 1")

file(SIZE ${INPUT_NV12} src_size)
math(EXPR needed "${IMAGES} * ${FRAME_BYTES}")
if(src_size LESS needed)
    message("SKIP vtenc.jpeg_collision: ${INPUT_NV12} holds fewer than ${IMAGES} frames")
    return()
endif()

set(CDIR "${WORKDIR}/jcoll_320x240")
set(RDIR "${WORKDIR}/jcoll_ref_320x240")
file(REMOVE_RECURSE "${CDIR}")
file(REMOVE_RECURSE "${RDIR}")
file(MAKE_DIRECTORY "${CDIR}")
file(MAKE_DIRECTORY "${RDIR}")
foreach(i RANGE 0 ${LAST})
    # dd rather than file(READ HEX): CMake cannot write a binary file from hex.
    execute_process(COMMAND dd if=${INPUT_NV12} of=${CDIR}/c${i}.nv12
                    bs=${FRAME_BYTES} skip=${i} count=1
                    RESULT_VARIABLE rcs OUTPUT_QUIET ERROR_QUIET)
    file(SIZE "${CDIR}/c${i}.nv12" staged)
    if(NOT staged EQUAL ${FRAME_BYTES})
        message("SKIP vtenc.jpeg_collision: could not stage frame ${i} (${staged} bytes)")
        return()
    endif()
    # The inputs are replaced by their codestreams, so the frames the outputs have
    # to match are kept out of the directory being encoded.
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy
                    "${CDIR}/c${i}.nv12" "${RDIR}/r${i}.yuv"
                    RESULT_VARIABLE rcc)
    if(NOT rcc EQUAL 0)
        message("SKIP vtenc.jpeg_collision: could not keep reference frame ${i}")
        return()
    endif()
endforeach()

execute_process(COMMAND ${HAL_ENC} -b vtenc --codec jpeg -f nv12 -i ${CDIR}
                --quality 90
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 137 OR rc GREATER 0)
    message("SKIP vtenc.jpeg_collision: hal_enc exited with ${rc}\n${out}${err}")
    return()
endif()
if(NOT out MATCHES "Encode total frames: ${IMAGES}")
    message(FATAL_ERROR "hal_enc did not encode ${IMAGES} images from ${IMAGES} "
            "inputs:\n${out}${err}")
endif()

# Nothing else in here: an output opened for a picture that never came, or a file
# read back as an input, both leave the directory with a name it did not start with.
file(GLOB landed "${CDIR}/*")
list(LENGTH landed landed_n)
if(NOT landed_n EQUAL ${IMAGES})
    message(FATAL_ERROR "${CDIR} holds ${landed_n} files after ${IMAGES} inputs: "
            "directory mode created or consumed a file that is not one picture")
endif()

foreach(i RANGE 0 ${LAST})
    set(jpg "${CDIR}/c${i}.nv12")

    # Each output has to hold a picture, not just exist: the run used to leave the
    # last name in the list open and unwritten.
    file(SIZE ${jpg} size)
    if(NOT size GREATER 512)
        message(FATAL_ERROR "${jpg} holds ${size} bytes: an input was named by an "
                "output that never got a picture")
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

    # Samples, not metadata, and against the frame this file is named after: a
    # picture shifted by one input probes exactly as well as the right one.
    set(own_psnr -1)
    set(rival_psnr -1)
    foreach(k RANGE 0 ${LAST})
        execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                        -s 320x240 -f rawvideo -pixel_format nv12
                        -i "${RDIR}/r${k}.yuv" -i ${jpg}
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
    if(NOT own_psnr GREATER 30)
        message(FATAL_ERROR "image ${i} does not match its own input frame (PSNR "
                "${own_psnr} dB, best rival ${rival_psnr} dB)")
    endif()
    if(NOT rival_psnr LESS own_psnr)
        message(FATAL_ERROR "image ${i} matches another input frame better than its "
                "own (${rival_psnr} dB vs ${own_psnr} dB) -- the pictures are not "
                "landing in the files their inputs were named after")
    endif()
    message("vtenc.jpeg_collision: image ${i} ok (${size} bytes, PSNR ${own_psnr} dB, "
            "best rival ${rival_psnr} dB)")
endforeach()
