# halcodec_test_prelude.cmake — the argument guard every integration case opens
# with, in one place.
#
# `ctest` configures these scripts from tests/CMakeLists.txt and each one used to
# spell out the same `if(NOT A OR NOT B ...) message(FATAL_ERROR "...")` block by
# hand. The blocks were correct but identical in shape and drifting in wording,
# so a case could be added with a guard that named the wrong variables -- and a
# guard is the one thing in a test that no other test can fail on: a script that
# silently mis-checks its inputs just runs with empty paths and reports a skip
# that looks like 'no backend here' rather than a bug in the harness.
#
#   include(${CMAKE_CURRENT_LIST_DIR}/halcodec_test_prelude.cmake)
#   halcodec_require(HAL_DEC FFMPEG INPUT_NV12 WORKDIR)
#
# Requires: each named variable to be set and non-empty. FATAL_ERROR otherwise,
# because arriving without a variable means tests/CMakeLists.txt was changed and
# the case was misconfigured -- not that the machine lacks a backend, which is
# what the in-body `SKIP` paths are for.

if(NOT COMMAND halcodec_require)
    # The message names the caller so a mis-registered case says which script it
    # is: CMAKE_CURRENT_LIST_FILE inside the macro is this prelude, not the
    # includer, so the includer passes its own name in.
    macro(halcodec_require)
        set(_halcodec_missing "")
        foreach(_halcodec_var IN LISTS ARGN)
            if(NOT ${_halcodec_var})
                list(APPEND _halcodec_missing ${_halcodec_var})
            endif()
        endforeach()
        if(_halcodec_missing)
            string(REPLACE ";" ", " _halcodec_missing "${_halcodec_missing}")
            message(FATAL_ERROR
                "${CMAKE_CURRENT_LIST_FILE}: ${_halcodec_missing} must be set. "
                "These come from tests/CMakeLists.txt; see its add_test() entry "
                "for this case.")
        endif()
    endmacro()
endif()

# --- PSNR gates ----------------------------------------------------------------
#
# Eight integration cases compare decoded/encoded samples the same way -- run
# ffmpeg's psnr filter, fish `average:` out of its stderr, gate the number
# against a floor -- and three of them re-run that inside a loop to prove a
# picture landed in the file named after its own input frame. The copies had
# drifted into three different `average:` regexes, and a failure message that
# names the wrong variable is the one bug no other test can catch. Two macros
# cover every site:
#
#   halcodec_psnr_average(PSNR "vtdec.jpeg vs ffmpeg"
#       -s 320x240 -f rawvideo -pixel_format nv12 -i ${FF_REF}
#       -s 320x240 -f rawvideo -pixel_format nv12 -i ${HAL_OUT})
#   halcodec_psnr_gate(PSNR 45 "vtdec.jpeg"
#       "hal_dec and ffmpeg disagree -- check the picture size and the plane copy")
#
# halcodec_psnr_average runs <ffmpegArgs...> after `-hide_banner -loglevel
# info` and before the default two-input psnr graph; set HALCODEC_PSNR_GRAPH
# first to replace the graph (the 10-bit profile cases need format= filters
# on both branches). It sets <outVar> to the reported average and
# FATAL_ERRORs if ffmpeg exits non-zero or reports no average.
# halcodec_psnr_gate FATAL_ERRORs unless <psnrVar> is greater than <floor>,
# with <hint> naming what to check and ffmpeg's full psnr output appended --
# the average line alone rarely says enough to debug a plane-copy bug.

if(NOT COMMAND halcodec_psnr_average)
    macro(halcodec_psnr_average outVar label)
        set(_halcodec_graph "[0:v]settb=AVTB[a];[a][1:v]psnr")
        if(HALCODEC_PSNR_GRAPH)
            set(_halcodec_graph "${HALCODEC_PSNR_GRAPH}")
        endif()
        execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                        ${ARGN}
                        -lavfi "${_halcodec_graph}" -f null -
                        RESULT_VARIABLE _halcodec_rc
                        ERROR_VARIABLE _halcodec_err OUTPUT_QUIET)
        if(NOT _halcodec_rc EQUAL 0)
            message(FATAL_ERROR
                "psnr comparison failed for ${label}: ${_halcodec_err}")
        endif()
        string(REGEX MATCH "average:([0-9]+\\.?[0-9]*)" _halcodec_m "${_halcodec_err}")
        if(NOT CMAKE_MATCH_1)
            message(FATAL_ERROR "no psnr average for ${label}: ${_halcodec_err}")
        endif()
        set(${outVar} "${CMAKE_MATCH_1}")
    endmacro()

    # Reads _halcodec_err left behind by a preceding halcodec_psnr_average so
    # the gate can show the whole psnr output, not just the average.
    macro(halcodec_psnr_gate psnrVar floor label hint)
        if(NOT ${psnrVar} GREATER ${floor})
            message(FATAL_ERROR
                "${label}: PSNR ${${psnrVar}} dB is not above ${floor}"
                " -- ${hint}\n${_halcodec_err}")
        endif()
    endmacro()
endif()

# --- capability gate -------------------------------------------------------------
#
# Integration cases whose assertions depend on what a decode or an encode
# actually produces (a byte-exact P210 frame, say) gate on the backend's
# structured capability answer instead of hardcoding platform knowledge:
# codecinfo -b <backend> --decode-caps <codec> asks the backend to state, per
# input class, which pixel formats its decode really yields, and --encode-caps
# which raw formats a hardware encoder on this machine really takes (measured on
# some machines, VideoToolbox decodes an HEVC Main 4:2:2 10 stream into 8-bit
# NV16 -- the kind of silent downgrade only a real decode exposes, and the reason
# the answer has to come from the machine rather than from a table in the test).
# Two macros cover every site:
#
#   halcodec_decode_caps(CODECINFO vtbox hevc HEVC_CAPS)
#   halcodec_caps_supports(HEVC_CAPS 10 422 P210 WANTP210)
#   if(WANTP210 STREQUAL "NO")
#       message("SKIP ...: ${HEVC_CAPS_JSON}")
#       return()
#   endif()
#
# halcodec_encode_caps(CODECINFO vtbox hevc HEVC_ENC) is the same shape for the
# encode side, where the format matched is one the encoder accepts.
#
# Each query macro sets <capsVar> to available, unknown or error (plus
# <capsVar>_JSON with the single-line answer and <capsVar>_KEY naming the format
# list field when available). Only "available" is a claim: unknown covers every
# honest cannot-answer (the codec has no probe, the data path is a stub), error is
# codecinfo itself failing -- a gate treats both as "no answer" and the case keeps
# its historical hard-run behavior, so backends that never adopted the structured
# query fail their tests exactly as before.
# halcodec_caps_supports scans the JSON for an entry claiming <bitDepth>/<chroma>
# with <format> among its formats and sets <outVar> to YES, NO or UNKNOWN
# (UNKNOWN = no answer available). Pass the depth/chroma as literals.

if(NOT COMMAND halcodec_decode_caps)
    macro(halcodec_decode_caps codecinfoExe backend codec capsVar)
        set(${capsVar} unknown)
        set(${capsVar}_KEY outputFormats)
        # No -DCODECINFO, or an empty one, is simply no answer available: the
        # gate keeps the case's historical hard-run behavior.
        if(DEFINED ${codecinfoExe} AND ${codecinfoExe})
            execute_process(COMMAND ${${codecinfoExe}} -b ${backend}
                            --decode-caps ${codec} --json
                            RESULT_VARIABLE _caps_rc
                            OUTPUT_VARIABLE _caps_out
                            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT _caps_rc EQUAL 0)
                set(${capsVar} error)
            elseif(_caps_out MATCHES "\"status\":\"available\"")
                set(${capsVar} available)
                set(${capsVar}_JSON "${_caps_out}")
            endif()
        endif()
    endmacro()

    macro(halcodec_encode_caps codecinfoExe backend codec capsVar)
        set(${capsVar} unknown)
        set(${capsVar}_KEY inputFormats)
        # No -DCODECINFO, or an empty one, is simply no answer available: the
        # gate keeps the case's historical hard-run behavior.
        if(DEFINED ${codecinfoExe} AND ${codecinfoExe})
            execute_process(COMMAND ${${codecinfoExe}} -b ${backend}
                            --encode-caps ${codec} --json
                            RESULT_VARIABLE _caps_rc
                            OUTPUT_VARIABLE _caps_out
                            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT _caps_rc EQUAL 0)
                set(${capsVar} error)
            elseif(_caps_out MATCHES "\"status\":\"available\"")
                set(${capsVar} available)
                set(${capsVar}_JSON "${_caps_out}")
            endif()
        endif()
    endmacro()

    macro(halcodec_caps_supports capsVar bitDepth chroma format outVar)
        set(${outVar} UNKNOWN)
        if("${${capsVar}}" STREQUAL "available")
            set(${outVar} NO)
            # string(REGEX MATCH <regex> <output> <input>) -- regex first, the
            # order the psnr matcher above uses too. The field name comes from the
            # query that filled this answer, so one matcher serves both sides.
            string(REGEX MATCHALL
                "\\{\"profile\":\"[^\"]*\",\"bitDepth\":${bitDepth},\"chroma\":${chroma},\"${${capsVar}_KEY}\":\\[[^]]*\"${format}\"[^]]*\\]\\}"
                _caps_entry
                "${${capsVar}_JSON}")
            if(_caps_entry)
                set(${outVar} YES)
            endif()
        endif()
    endmacro()
endif()
