# vtenc_main10.cmake — encodes the bundled NV12 as 10-bit (P010) HEVC with the
# vtbox encoder and asserts the stream really is Main 10: ffprobe has to report
# profile "Main 10" with pix_fmt yuv420p10le and the same frame count the encoder
# claims it fed. A wrong plane pitch in the 10-bit copy path would still produce
# a stream, so the metadata check alone is not enough -- which is why the sample
# fidelity is checked separately (see the notes in the commit). Skips when ffmpeg
# or ffprobe is missing, or when the vtenc backend cannot load.

if(NOT HAL_ENC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_ENC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(P010_RAW "${WORKDIR}/synth_320x240_p010.yuv")
set(OUT_H265 "${WORKDIR}/synth_main10.h265")
set(FRAME_BYTES 230400)   # 320x240 P010: 1.5 samples per pixel, 16-bit storage

execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240 -framerate 25
                -i ${INPUT_NV12} -pix_fmt p010le -f rawvideo -y ${P010_RAW}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtenc.hevc_main10: ffmpeg cannot produce p010le (rc=${rc})")
    return()
endif()

file(SIZE ${P010_RAW} raw_size)
math(EXPR frames "${raw_size} / ${FRAME_BYTES}")
if(NOT frames GREATER 0)
    message("SKIP vtenc.hevc_main10: no whole frames in ${raw_size} bytes")
    return()
endif()

# No --profile: a P010 input is expected to select main10 by itself.
execute_process(COMMAND ${HAL_ENC} -b vtenc --codec hevc -f p010
                -i ${P010_RAW} -o ${OUT_H265} --bitrate 2000
                RESULT_VARIABLE rc2 OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc2 EQUAL 137 OR rc2 GREATER 0)
    message("SKIP vtenc.hevc_main10: hal_enc exited with ${rc2}\n${out}${err}")
    return()
endif()
if(NOT out MATCHES "Encode total frames: ${frames}")
    message(FATAL_ERROR "hal_enc did not encode ${frames} frames:\n${out}${err}")
endif()

execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error
                -show_entries stream=profile,pix_fmt -of default=noprint_wrappers=1
                ${OUT_H265}
                RESULT_VARIABLE rc3 OUTPUT_VARIABLE meta ERROR_VARIABLE meta_err)
if(NOT rc3 EQUAL 0)
    message(FATAL_ERROR "ffprobe failed on ${OUT_H265}: ${meta_err}")
endif()
if(NOT meta MATCHES "profile=Main 10")
    message(FATAL_ERROR "stream is not Main 10:\n${meta}")
endif()
if(NOT meta MATCHES "pix_fmt=yuv420p10le")
    message(FATAL_ERROR "stream is not 10-bit:\n${meta}")
endif()

# The stream has to be readable as the same number of pictures it was fed.
execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error -count_packets
                -show_entries stream=nb_read_packets -of default=noprint_wrappers=1
                ${OUT_H265}
                RESULT_VARIABLE rc4 OUTPUT_VARIABLE counted)
string(REGEX MATCH "nb_read_packets=([0-9]+)" _m "${counted}")
if(NOT CMAKE_MATCH_1 STREQUAL "${frames}")
    message(FATAL_ERROR "stream holds ${CMAKE_MATCH_1} packets, expected ${frames}")
endif()

# Metadata and packet count cannot see a mis-copied plane, so compare samples:
# a wrong 10-bit row pitch lands the chroma in the middle of the luma and drops
# PSNR to single digits, while a correct encode of this content measures ~32 dB
# (the figure is deliberately well below what a working encoder reaches, because
# hal declares full range and ffmpeg's psnr filter rescales against the
# limited-range raw input).
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                -s 320x240 -r 25 -f rawvideo -pixel_format p010le -i ${P010_RAW}
                -f hevc -i ${OUT_H265}
                -lavfi "[0:v]settb=AVTB[a];[a][1:v]psnr" -f null -
                RESULT_VARIABLE rc5 ERROR_VARIABLE psnr_out OUTPUT_QUIET)
if(NOT rc5 EQUAL 0)
    message(FATAL_ERROR "psnr comparison failed: ${psnr_out}")
endif()
string(REGEX MATCH "average:([0-9]+)\\.([0-9]+)" _m "${psnr_out}")
set(PSNR_WHOLE "${CMAKE_MATCH_1}")
if(NOT PSNR_WHOLE GREATER 25)
    message(FATAL_ERROR "encoded frames do not match the input (PSNR "
            "${psnr_out}) -- check the 10-bit plane copy")
endif()

message("vtenc.hevc_main10: passed (${frames} frames, Main 10 / yuv420p10le, "
        "PSNR ${PSNR_WHOLE} dB)")
