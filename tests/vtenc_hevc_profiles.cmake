# vtenc_hevc_profiles.cmake — the three HEVC profiles VideoToolbox offers on this
# hardware that the encoder used to have no name for: Main 4:2:2 10, Monochrome
# and Monochrome 10. Each is asserted through ffprobe's pix_fmt, which ffmpeg
# derives from the SPS's chroma_format_idc and bit depths -- that is the only way
# to tell a real 4:2:2 stream from a session that quietly resampled the input,
# and the byte size of the output cannot tell them apart.
#
# The refusals are the other half of the case. VideoToolbox accepts a mismatched
# profile and source pool at VTCompressionSessionCreate and at EncodeFrame, and
# reports it only from the compression callback -- with zero frames handed back.
# So "the session opened" proves nothing, and the encoder has to refuse the
# combinations itself: 8-bit input with a 10-bit profile (measured: produces a
# profile_idc=2 stream at 8-bit precision), P210 input with anything but
# main422-10 (Main10 halves the chroma, the mono profiles fail outright),
# main-mono-10 without a 10-bit source, and a misspelled profile name -- which
# used to fall through to Main.
#
# Skips when ffmpeg/ffprobe are missing or the encoder backend cannot load.

if(NOT HAL_ENC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_ENC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)
set(NV12_3 "${WORKDIR}/hevcprof_src_320x240_nv12.yuv")
set(P010_3 "${WORKDIR}/hevcprof_src_320x240_p010le.yuv")
set(P210_3 "${WORKDIR}/hevcprof_src_320x240_p210le.yuv")
foreach(pair "${NV12_3}:nv12" "${P010_3}:p010le" "${P210_3}:p210le")
    string(REPLACE ":" ";" parts "${pair}")
    list(GET parts 0 raw)
    list(GET parts 1 ff_fmt)
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                    -f rawvideo -pixel_format nv12 -video_size 320x240
                    -framerate 25 -i ${INPUT_NV12} -frames:v ${FRAMES}
                    -pix_fmt ${ff_fmt} -f rawvideo -y ${raw}
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message("SKIP vtenc.hevc_profiles: ffmpeg cannot make a ${ff_fmt} source")
        return()
    endif()
endforeach()

# HAL format | ffmpeg raw format | profile | the pix_fmt ffprobe must report.
# Both p210 and p010 inputs are expected to come out 4:2:2 10-bit: the source's
# chroma is upsampled by ffmpeg on the way in for the second one.
set(CASES
    "p210|p210le|main422-10|yuv422p10le"
    "p010|p010le|main422-10|yuv422p10le"
    "nv12|nv12|main-mono|gray"
    "p010|p010le|main-mono-10|gray10le")

foreach(case ${CASES})
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 fmt)
    list(GET parts 1 ff_fmt)
    list(GET parts 2 profile)
    list(GET parts 3 want_fmt)
    if(fmt STREQUAL "nv12")
        set(src "${NV12_3}")
    elseif(fmt STREQUAL "p010")
        set(src "${P010_3}")
    else()
        set(src "${P210_3}")
    endif()
    set(stream "${WORKDIR}/hevcprof_${profile}_${fmt}.h265")

    execute_process(COMMAND ${HAL_ENC} -b vtenc --codec hevc -f ${fmt}
                    --profile ${profile} -i ${src} -o ${stream} --bitrate 2000
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 137 OR rc GREATER 0)
        message("SKIP vtenc.hevc_profiles: hal_enc exited with ${rc}\n${out}${err}")
        return()
    endif()
    if(NOT out MATCHES "Encode total frames: ${FRAMES}")
        message(FATAL_ERROR "${profile} from ${fmt} did not encode ${FRAMES} "
                "frames:\n${out}${err}")
    endif()

    # The claim under test: the subsampling and depth the SPS declares.
    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error
                    -select_streams v -show_entries stream=pix_fmt,profile
                    -of default=noprint_wrappers=1 ${stream}
                    RESULT_VARIABLE rc2 OUTPUT_VARIABLE meta ERROR_QUIET)
    if(NOT rc2 EQUAL 0)
        message(FATAL_ERROR "ffprobe cannot read ${stream}")
    endif()
    if(NOT meta MATCHES "pix_fmt=${want_fmt}")
        message(FATAL_ERROR "profile ${profile} from ${fmt} input produced ${meta}"
                            " -- expected pix_fmt ${want_fmt}. A session that "
                            "resampled the source instead of encoding it looks "
                            "exactly like success here.")
    endif()

    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error -count_packets
                    -select_streams v -show_entries stream=nb_read_packets
                    -of default=noprint_wrappers=1 ${stream}
                    RESULT_VARIABLE rc3 OUTPUT_VARIABLE counted ERROR_QUIET)
    string(REGEX MATCH "nb_read_packets=([0-9]+)" _m "${counted}")
    if(NOT CMAKE_MATCH_1 STREQUAL "${FRAMES}")
        message(FATAL_ERROR "${stream} holds ${CMAKE_MATCH_1} packets, expected "
                "${FRAMES}:\n${counted}")
    endif()

    # Metadata cannot see a mis-copied plane, so compare samples: both sides into
    # the coder's own output format, then psnr. The floor is low on purpose -- HAL
    # declares full range while ffmpeg reads the raw side as limited range, which
    # costs several dB on content that is otherwise a clean round trip.
    execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                    -s 320x240 -r 25 -f rawvideo -pixel_format ${ff_fmt} -i ${src}
                    -f hevc -i ${stream}
                    -lavfi "[0:v]format=${want_fmt},settb=AVTB[a];[1:v]format=${want_fmt}[b];[a][b]psnr"
                    -f null -
                    RESULT_VARIABLE rc4 ERROR_VARIABLE psnr_out OUTPUT_QUIET)
    if(NOT rc4 EQUAL 0)
        message(FATAL_ERROR "psnr comparison failed: ${psnr_out}")
    endif()
    string(REGEX MATCH "average:([0-9]+)" _m "${psnr_out}")
    set(PSNR "${CMAKE_MATCH_1}")
    if(NOT PSNR GREATER 20)
        message(FATAL_ERROR "${profile} from ${fmt}: PSNR ${PSNR} dB against its "
                "own source -- a wrong plane pitch or a dropped chroma plane "
                "lands here\n${psnr_out}")
    endif()
    file(SIZE ${stream} stream_bytes)
    message("vtenc.hevc_profiles ${profile} from ${fmt}: ${FRAMES} frames, "
            "${stream_bytes} bytes, ${want_fmt}, PSNR ${PSNR} dB")
endforeach()

# --- the refusals -----------------------------------------------------------
# Each of these opens a session, answers EncodeFrame with noErr and hands back
# nothing (or a stream whose SPS contradicts the request), so the encoder has to
# say no before creating the session.
set(REFUSALS
    "p210|main10|P210 input keeps its 4:2:2"
    "nv12|main10|needs a 10-bit input"
    "nv12|main42210|unknown HEVC profile"
    "nv12|main-mono-10|needs a 10-bit input")
foreach(refusal ${REFUSALS})
    string(REPLACE "|" ";" parts "${refusal}")
    list(GET parts 0 fmt)
    list(GET parts 1 profile)
    list(GET parts 2 why)
    if(fmt STREQUAL "p210")
        set(src "${P210_3}")
    else()
        set(src "${NV12_3}")
    endif()
    execute_process(COMMAND ${HAL_ENC} -b vtenc --codec hevc -f ${fmt}
                    --profile ${profile} -i ${src}
                    -o "${WORKDIR}/hevcprof_refused.h265" --bitrate 2000
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 137)
        message("SKIP vtenc.hevc_profiles: backend could not load")
        return()
    endif()
    if(rc EQUAL 0)
        message(FATAL_ERROR "${fmt} input with profile ${profile} was accepted; "
                "it should be refused (${why}).")
    endif()
    if(NOT err MATCHES "${why}")
        message(FATAL_ERROR "refusal for ${fmt}/${profile} did not say why "
                "(${why}): ${err}")
    endif()
    message("vtenc.hevc_profiles ${fmt}/${profile} refused: ${why}")
endforeach()
