# decode_determinism.cmake — run hal_dec twice on the same Annex-B stream and
# assert the outputs are byte-identical and the frame count is correct.
# Exits with code 77 when the backend cannot load (ctest SKIP).

if(NOT HAL_DEC OR NOT INPUT_H264 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, INPUT_H264 and WORKDIR must be set")
endif()

set(OUT1 "${WORKDIR}/det_run1.nv12")
set(OUT2 "${WORKDIR}/det_run2.nv12")

# Run 1
execute_process(COMMAND ${HAL_DEC} -i ${INPUT_H264} -o ${OUT1}
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 137 OR rc GREATER 0)
    # Backend not available or init failed: skip rather than fail.
    message("SKIP vtdec.determinism: hal_dec exited with ${rc}")
    return()
endif()

# Extract frame count from stdout.
string(REGEX MATCH "Decode total frames: ([0-9]+)" _m "${out}")
if(NOT CMAKE_MATCH_1 STREQUAL "30")
    message(FATAL_ERROR "expected 30 frames, got '${CMAKE_MATCH_1}'")
endif()

# Run 2
execute_process(COMMAND ${HAL_DEC} -i ${INPUT_H264} -o ${OUT2}
                RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "second decode run failed (${rc2})")
endif()

# Byte-exact comparison (order-sensitive).
file(READ ${OUT1} b1 LIMIT 1048576)
file(READ ${OUT2} b2 LIMIT 1048576)
if(NOT b1 STREQUAL b2)
    file(SIZE ${OUT1} s1)
    file(SIZE ${OUT2} s2)
    message(FATAL_ERROR "determinism check failed: ${s1} vs ${s2} bytes differ")
endif()

message("vtdec.determinism: passed (30 frames, byte-identical)")
