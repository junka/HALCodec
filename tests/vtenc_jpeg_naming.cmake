# vtenc_jpeg_naming.cmake — hal_enc's default output names stay off its inputs.
# --format describes the *raw* layout of the input, and deriving the output
# extension from it meant `-f nv12` over a directory of .nv12 frames handed every
# picture the path of the frame it was made from: the raw frames were replaced by
# codestreams, and one of them was truncated before it had even been read. The
# default extension now comes from --codec, and with a directory input -o names the
# directory the codestreams go to.
#
# Asserted, over four .nv12 frames encoded with --codec jpeg:
#   * four .jpg outputs, each holding the frame it is named after (own frame
#     > 30 dB and better than every rival), and no other file invented;
#   * every input still present, still its full raw byte count, still the same
#     bytes -- nothing was overwritten;
#   * -o on a directory input puts the outputs in that directory (created if
#     missing) and adds nothing to the input directory;
#   * the single-file default follows the same rule: x.nv12 with --codec jpeg
#     writes x.jpg, not x.nv12.
# Skips when ffmpeg/ffprobe or the vtenc backend are missing.

if(NOT HAL_ENC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_ENC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAME_BYTES 115200)   # 320x240 NV12
set(IMAGES 4)
math(EXPR LAST "${IMAGES} - 1")

file(SIZE ${INPUT_NV12} src_size)
math(EXPR needed "${IMAGES} * ${FRAME_BYTES}")
if(src_size LESS needed)
    message("SKIP vtenc.jpeg_naming: ${INPUT_NV12} holds fewer than ${IMAGES} frames")
    return()
endif()

# The directory names carry 320x240 because hal_enc reads dimensions out of the
# input path when the command line does not give them. BDIR is a clean copy of the
# same frames for the -o run: the directory scan takes every regular file, so
# re-running over a directory this case already wrote into would encode the outputs.
set(CDIR "${WORKDIR}/jname_320x240")
set(BDIR "${WORKDIR}/jname_o_320x240")
set(ODIR "${WORKDIR}/jname_out_320x240")
file(REMOVE_RECURSE "${CDIR}")
file(REMOVE_RECURSE "${BDIR}")
file(REMOVE_RECURSE "${ODIR}")
file(MAKE_DIRECTORY "${CDIR}")
file(MAKE_DIRECTORY "${BDIR}")
foreach(i RANGE 0 ${LAST})
    # dd rather than file(READ HEX): CMake cannot write a binary file from hex.
    execute_process(COMMAND dd if=${INPUT_NV12} of=${CDIR}/c${i}.nv12
                    bs=${FRAME_BYTES} skip=${i} count=1
                    RESULT_VARIABLE rcs OUTPUT_QUIET ERROR_QUIET)
    file(SIZE "${CDIR}/c${i}.nv12" staged)
    if(NOT staged EQUAL ${FRAME_BYTES})
        message("SKIP vtenc.jpeg_naming: could not stage frame ${i} (${staged} bytes)")
        return()
    endif()
    # Recorded before the encode, so the inputs can be compared with themselves
    # afterwards. Unlike the previous version of this case there is no reference
    # copy to keep somewhere else: the frames are never touched now. Hashed with
    # `cmake -E md5sum` rather than file(MD5): CMake 4.4 takes that subcommand's
    # arguments as (path, out-var), the reverse of what it documents on older
    # CMake, so neither spelling is portable. -E md5sum is documented on both and
    # prints "<digest>  <path>".
    execute_process(COMMAND ${CMAKE_COMMAND} -E md5sum "${CDIR}/c${i}.nv12"
                    OUTPUT_VARIABLE h OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^[0-9a-f]+" ref_md5_${i} "${h}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy
                    "${CDIR}/c${i}.nv12" "${BDIR}/c${i}.nv12"
                    RESULT_VARIABLE rcb)
    if(NOT rcb EQUAL 0)
        message("SKIP vtenc.jpeg_naming: could not copy frame ${i} for the -o run")
        return()
    endif()
endforeach()

execute_process(COMMAND ${HAL_ENC} -b vtenc --codec jpeg -f nv12 -i ${CDIR}
                --quality 90
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 137 OR rc GREATER 0)
    message("SKIP vtenc.jpeg_naming: hal_enc exited with ${rc}\n${out}${err}")
    return()
endif()
if(NOT out MATCHES "Encode total frames: ${IMAGES}")
    message(FATAL_ERROR "hal_enc did not encode ${IMAGES} images from ${IMAGES} "
            "inputs:\n${out}${err}")
endif()

# One output per input and nothing else: a name invented by the encoder, or an
# input taken over by an output, both change what the directory holds.
file(GLOB jpgs "${CDIR}/*.jpg")
list(LENGTH jpgs jpg_n)
if(NOT jpg_n EQUAL ${IMAGES})
    message(FATAL_ERROR "${CDIR} holds ${jpg_n} .jpg outputs for ${IMAGES} inputs")
endif()

foreach(i RANGE 0 ${LAST})
    set(jpg "${CDIR}/c${i}.jpg")
    set(raw "${CDIR}/c${i}.nv12")

    if(NOT EXISTS "${raw}")
        message(FATAL_ERROR "${raw} is gone: the encode replaced its own input")
    endif()
    file(SIZE "${raw}" raw_size)
    if(NOT raw_size EQUAL ${FRAME_BYTES})
        message(FATAL_ERROR "${raw} holds ${raw_size} bytes, not ${FRAME_BYTES}: "
                "an output was written over an input")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E md5sum "${raw}"
                    OUTPUT_VARIABLE h OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^[0-9a-f]+" now_md5 "${h}")
    if(NOT now_md5 STREQUAL "${ref_md5_${i}}")
        message(FATAL_ERROR "${raw} changed bytes during the encode")
    endif()

    file(SIZE ${jpg} size)
    if(NOT size GREATER 512)
        message(FATAL_ERROR "${jpg} holds ${size} bytes: an input got no picture")
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

    # Samples, not metadata: a picture shifted by one input probes exactly as well
    # as the right one, and a name shifted by one extension is what this case is
    # about. Consecutive cars frames are far apart, so the own frame wins by ~10 dB.
    set(own_psnr -1)
    set(rival_psnr -1)
    foreach(k RANGE 0 ${LAST})
        execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                        -s 320x240 -f rawvideo -pixel_format nv12
                        -i "${CDIR}/c${k}.nv12" -i ${jpg}
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
    message("vtenc.jpeg_naming: image ${i} ok (${size} bytes, PSNR ${own_psnr} dB, "
            "best rival ${rival_psnr} dB)")
endforeach()

# -o as an output directory. ODIR does not exist yet, so this also covers hal_enc
# making it. An ignored -o would leave the outputs in BDIR beside the frames,
# which is what the two file counts check for.
execute_process(COMMAND ${HAL_ENC} -b vtenc --codec jpeg -f nv12 -i ${BDIR}
                -o ${ODIR} --quality 90
                RESULT_VARIABLE rco OUTPUT_VARIABLE outo ERROR_VARIABLE erro)
if(rco EQUAL 137 OR rco GREATER 0)
    message(FATAL_ERROR "hal_enc -o ${ODIR} exited with ${rco}\n${outo}${erro}")
endif()
if(NOT outo MATCHES "Encode total frames: ${IMAGES}")
    message(FATAL_ERROR "hal_enc with -o did not encode ${IMAGES} images:\n${outo}${erro}")
endif()
string(FIND "${outo}" "${ODIR}/c0.jpg" echoed)
if(echoed EQUAL -1)
    message(FATAL_ERROR "with a directory input -o is the output directory, but no "
            "output was named under ${ODIR}:\n${outo}")
endif()
file(GLOB o_jpgs "${ODIR}/*.jpg")
list(LENGTH o_jpgs o_n)
if(NOT o_n EQUAL ${IMAGES})
    message(FATAL_ERROR "${ODIR} holds ${o_n} .jpg outputs for ${IMAGES} inputs")
endif()
foreach(i RANGE 0 ${LAST})
    file(SIZE "${ODIR}/c${i}.jpg" osz)
    if(NOT osz GREATER 512)
        message(FATAL_ERROR "${ODIR}/c${i}.jpg holds ${osz} bytes")
    endif()
endforeach()
file(GLOB b_extra "${BDIR}/*.jpg")
list(LENGTH b_extra b_n)
if(NOT b_n EQUAL 0)
    message(FATAL_ERROR "the -o run wrote ${b_n} outputs into ${BDIR}, which holds "
            "only raw frames -- -o was ignored")
endif()

# Single-file mode runs on the same default: the codestream takes the codec's
# extension, the raw input keeps its own. Last, because it reuses c0.jpg.
execute_process(COMMAND ${HAL_ENC} -b vtenc --codec jpeg -f nv12
                -i ${CDIR}/c0.nv12 --quality 90
                RESULT_VARIABLE rcs1 OUTPUT_VARIABLE outs ERROR_VARIABLE errs)
if(rcs1 EQUAL 137 OR rcs1 GREATER 0)
    message("SKIP vtenc.jpeg_naming: single-file run exited with ${rcs1}\n${outs}${errs}")
    return()
endif()
if(NOT outs MATCHES "Encode total frames: 1")
    message(FATAL_ERROR "single-file encode of one frame gave:\n${outs}${errs}")
endif()
file(SIZE "${CDIR}/c0.nv12" single_raw_size)
if(NOT single_raw_size EQUAL ${FRAME_BYTES})
    message(FATAL_ERROR "${CDIR}/c0.nv12 holds ${single_raw_size} bytes after a "
            "single-file run with no -o: the default output landed on the input")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E md5sum "${CDIR}/c0.nv12"
                OUTPUT_VARIABLE h1 OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "^[0-9a-f]+" single_md5 "${h1}")
if(NOT single_md5 STREQUAL "${ref_md5_0}")
    message(FATAL_ERROR "the single-file run changed its own input")
endif()
message("vtenc.jpeg_naming: inputs intact, outputs named by codec "
        "(${IMAGES} in ${CDIR}, ${o_n} in ${ODIR}, 1 by default for a single file)")
