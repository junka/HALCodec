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
