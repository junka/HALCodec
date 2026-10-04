# vtenc_prores.cmake — encodes raw P210 with the vtbox ProRes encoder and checks
# what that coder owes. ProRes samples are the codestream itself, so an emitted
# packet has to stand up to the same reading a decoder will give it: every frame
# opens with a 32-bit count of its own bytes (the frames must therefore tile the
# output exactly), the frame header has to carry the dimensions the input was
# given, and the 4:2:2/4:4:4 flag has to follow the profile that was asked for.
# The bitrate tiers are the encoder's only quality dial, so they are asserted to be
# ordered rather than merely accepted, and the knobs ProRes has no use for are
# asserted to be refused rather than silently dropped -- the bulk session property
# setter used here accepts keys the single setter rejects, so a knob handed over
# would vanish without an error. Finally the stream is decoded back with HAL's own
# decoder and scored against the frames that went in (~59 dB for this content; a
# wrong plane pitch or a 6-bit rescale collapses it below 15 dB). Skips when
# ffmpeg or a backend is missing.

if(NOT HAL_ENC OR NOT HAL_DEC OR NOT FFMPEG OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_ENC, HAL_DEC, FFMPEG, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)
set(FRAME_BYTES 307200)   # 320x240 P210: 4:2:2, 16-bit storage (4 bytes/pixel)
math(EXPR WANT_BYTES "${FRAMES} * ${FRAME_BYTES}")

# hal_enc takes dimensions from a WxH in the file name, so the staged input carries
# one. ffmpeg's p210le is left-aligned in 16 bits like the pool the encoder feeds,
# which is why no shift shows up anywhere in this path.
set(P210 "${WORKDIR}/prores_320x240_p210.yuv")
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12} -frames:v ${FRAMES} -pix_fmt p210le -f rawvideo
                -y ${P210}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtenc.prores: ffmpeg cannot produce p210le (rc=${rc})")
    return()
endif()
file(SIZE ${P210} src_size)
if(NOT src_size EQUAL WANT_BYTES)
    message("SKIP vtenc.prores: staged ${src_size} bytes, expected ${WANT_BYTES}")
    return()
endif()

# --- structure: the encoder's own output has to be what a decoder reads --------
set(STD "${WORKDIR}/penc_standard.prores")
execute_process(COMMAND ${HAL_ENC} -b vtenc --codec prores --profile standard
                -f p210 -i ${P210} -o ${STD}
                RESULT_VARIABLE rc2 OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc2 EQUAL 137 OR rc2 GREATER 0)
    message("SKIP vtenc.prores: hal_enc exited with ${rc2}\n${out}${err}")
    return()
endif()
if(NOT out MATCHES "Encode total frames: ${FRAMES}")
    message(FATAL_ERROR "hal_enc did not encode ${FRAMES} frames:\n${out}${err}")
endif()

# The frame walk is the parser the decoder itself uses: it asserts the byte counts
# tile the file, that every header carries 'icpf' behind the documented constant
# field, and that the picture is the 320x240 4:2:2 one that was fed.
if(PRORESDESC_TEST)
    execute_process(COMMAND ${PRORESDESC_TEST} ${STD}
                    RESULT_VARIABLE rc3 OUTPUT_VARIABLE sout ERROR_VARIABLE serr)
    if(NOT rc3 EQUAL 0)
        message(FATAL_ERROR "the encoder's own output does not parse as ${FRAMES} "
                "whole ProRes frames:\n${sout}${serr}")
    endif()
    message("vtenc.prores: stream walks as ${FRAMES} tiling frames")
else()
    message("vtenc.prores: skipped the frame walk (no PRORESDESC_TEST)")
endif()

# --- tiers: byte total rises with the tier, and only 4:4:4 sets the flag --------
set(TIERS proxy lt standard hq 4444 xq)
set(SIZES "")
set(PREV -1)
set(PREVTIER "")
foreach(tier ${TIERS})
    set(TOUT "${WORKDIR}/penc_${tier}.prores")
    execute_process(COMMAND ${HAL_ENC} -b vtenc --codec prores --profile ${tier}
                    -f p210 -i ${P210} -o ${TOUT}
                    RESULT_VARIABLE rct OUTPUT_VARIABLE outt ERROR_VARIABLE errt)
    if(rct EQUAL 137 OR rct GREATER 0)
        message("SKIP vtenc.prores: tier ${tier} exited with ${rct}\n${outt}${errt}")
        return()
    endif()
    file(SIZE ${TOUT} tsize)
    if(NOT tsize GREATER 0)
        message(FATAL_ERROR "tier ${tier} produced a ${tsize}-byte stream")
    endif()
    if(tier STREQUAL "4444" OR tier STREQUAL "xq")
        set(WANT_FLAG "0001")
    else()
        set(WANT_FLAG "0000")
    endif()
    file(READ ${TOUT} flag OFFSET 10 LIMIT 2 HEX)
    string(TOUPPER "${flag}" flag)
    if(NOT flag STREQUAL WANT_FLAG)
        message(FATAL_ERROR "tier ${tier} wrote subsampling flag ${flag}, expected "
                "${WANT_FLAG} (only a 4:4:4 tier sets it)")
    endif()
    # A tier is a bitrate tier, so each step up has to spend bytes or the profile
    # never reached the session.
    if(NOT tsize GREATER PREV)
        message(FATAL_ERROR "tier ${tier} gave ${tsize} bytes, not more than the "
                "cheaper ${PREVTIER} (${PREV}): the profile is not reaching the "
                "encoder")
    endif()
    set(PREV ${tsize})
    set(PREVTIER ${tier})
    list(APPEND SIZES ${tsize})
endforeach()
message("vtenc.prores: six tiers strictly ordered (${SIZES})")

# --- round trip through HAL's own decoder --------------------------------------
set(RT "${WORKDIR}/penc_roundtrip.p210")
execute_process(COMMAND ${HAL_DEC} -b vtbox -i ${STD} -o ${RT}
                RESULT_VARIABLE rc4 OUTPUT_VARIABLE outr ERROR_VARIABLE errr)
if(rc4 EQUAL 137 OR rc4 GREATER 0)
    message(FATAL_ERROR "the encoder's stream could not be decoded by this same "
            "build (exit ${rc4}):\n${outr}${errr}")
endif()
if(NOT outr MATCHES "Decode total frames: ${FRAMES}")
    message(FATAL_ERROR "round trip frames:\n${outr}${errr}")
endif()
file(SIZE ${RT} rt_size)
if(NOT rt_size EQUAL WANT_BYTES)
    message(FATAL_ERROR "round trip gave ${rt_size} bytes, expected ${WANT_BYTES} -- "
            "the picture that came back is not the one that went in")
endif()
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                -s 320x240 -f rawvideo -pixel_format p210le -i ${P210}
                -s 320x240 -f rawvideo -pixel_format p210le -i ${RT}
                -lavfi "[0:v]settb=AVTB[a];[a][1:v]psnr" -f null -
                RESULT_VARIABLE rc5 ERROR_VARIABLE psnr_out OUTPUT_QUIET)
if(NOT rc5 EQUAL 0)
    message(FATAL_ERROR "psnr comparison failed: ${psnr_out}")
endif()
string(REGEX MATCH "average:([0-9]+)" _m "${psnr_out}")
set(PSNR "${CMAKE_MATCH_1}")
if(NOT PSNR GREATER 40)
    message(FATAL_ERROR "the decoded frames do not match what was encoded (PSNR "
            "${PSNR} dB) -- check the plane copy into the source buffer and the "
            "sample handed over:\n${psnr_out}")
endif()
message("vtenc.prores: round trip ${FRAMES} frames, ${rt_size} bytes, PSNR ${PSNR} dB")

# --- knobs ProRes has no use for must be refused, not swallowed -----------------
set(KNOB_OUT "${WORKDIR}/penc_knob.prores")
foreach(knob "--bitrate;2000" "--maxbitrate;2000" "--quality;60" "--gop;10"
             "--lowdelay")
    execute_process(COMMAND ${HAL_ENC} -b vtenc --codec prores -f p210
                    -i ${P210} -o ${KNOB_OUT} ${knob}
                    RESULT_VARIABLE rck OUTPUT_VARIABLE okout ERROR_VARIABLE okerr)
    file(REMOVE ${KNOB_OUT})
    if(rck EQUAL 0)
        message(FATAL_ERROR "${knob} was accepted: ProRes has no such dial, and the "
                "session would have dropped the request without a word")
    endif()
    if(NOT okerr MATCHES "ProRes takes none of")
        message(FATAL_ERROR "${knob} refused without saying why:\n${okout}${okerr}")
    endif()
endforeach()
message("vtenc.prores: bitrate/quality/gop/low-delay refused for ProRes")

execute_process(COMMAND ${HAL_ENC} -b vtenc --codec prores --profile bogus -f p210
                -i ${P210} -o "${WORKDIR}/penc_bogus.prores"
                RESULT_VARIABLE rcb OUTPUT_VARIABLE bout ERROR_VARIABLE berr)
if(rcb EQUAL 0)
    message(FATAL_ERROR "--profile bogus was accepted as a ProRes tier")
endif()
if(NOT berr MATCHES "unknown ProRes flavour")
    message(FATAL_ERROR "unknown tier refused without saying why:\n${bout}${berr}")
endif()
message("vtenc.prores: unknown ProRes tier refused by name")
