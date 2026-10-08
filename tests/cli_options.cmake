# cli_options.cmake -- every app built from app/parse_cli.h has to answer `-c`.
#
# boost.program_options registers short options at parse time, so two long options
# claiming `-c` compiles clean and only fails when someone types it: every app died
# with "Error: option '-c' is ambiguous and matches '--codec', and '--colorspace'"
# and exit 1, which made `hal_dec -c hevc` unusable even though `--codec hevc` was
# fine. The clash is visible in the help text before anyone types it, so the first
# check below scans the letters --help lists rather than probing only -c: a future
# clash is caught even for an option nobody thought to test.
#
# Matching peels one hit at a time out of the help text and keeps the letters in a
# sentinel-wrapped scalar. CMake's REGEX MATCHALL joins its hits with an escaped
# `\;` that no later list() or string(REPLACE) turns back into a list, so counting
# matches through a list variable silently reports one element for eight.

if(NOT APPS)
    message("SKIP cli.options: no app binaries were passed")
    return()
endif()

foreach(app ${APPS})
    get_filename_component(name ${app} NAME)
    if(NOT EXISTS ${app})
        message("SKIP cli.options: ${app} is missing")
        return()
    endif()

    execute_process(COMMAND ${app} --help
                    OUTPUT_VARIABLE help ERROR_VARIABLE help_err
                    RESULT_VARIABLE rc OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${name} --help exited with ${rc}:\n${help}${help_err}")
    endif()

    # boost prints an option that has a short form as "  -c [ --codec ] arg".
    set(rest "${help}")
    set(seen "")
    set(dups "")
    set(found 0)
    # while(1) rather than while(TRUE): on CMake >= 3.28 with CMP0130 unset,
    # the bare word TRUE is diagnosed as a non-boolean variable name and the
    # loop body never runs, so the scan reports zero short options. 1 is
    # always treated as true regardless of policy.
    while(1)
        string(REGEX MATCH "-[A-Za-z] \\[ --" hit "${rest}")
        if(NOT hit)
            break()
        endif()
        string(SUBSTRING "${hit}" 1 1 letter)
        string(FIND "${seen}" "-${letter}-" again)
        if(again GREATER -1)
            set(dups "${dups} -${letter}")
        else()
            set(seen "${seen}-${letter}-")
        endif()
        math(EXPR found "${found} + 1")
        string(FIND "${rest}" "${hit}" at)
        string(LENGTH "${hit}" hit_len)
        math(EXPR next "${at} + ${hit_len}")
        string(SUBSTRING "${rest}" ${next} -1 rest)
    endwhile()

    # An empty scan would pass the duplicate check vacuously, so finding nothing is
    # its own failure -- and -c has to be in the list to be worth giving to.
    if(found LESS 4)
        message(FATAL_ERROR "${name} --help yielded ${found} short options "
                "(${seen}): the scan no longer matches boost's help format, so the "
                "duplicate check would pass vacuously")
    endif()
    string(FIND "${seen}" "-c-" has_c)
    if(has_c LESS 0)
        message(FATAL_ERROR "${name} --help lists no -c at all, so --codec has lost "
                "the short form this test exists to keep: ${seen}")
    endif()
    if(dups)
        message(FATAL_ERROR "${name} advertises ${found} short options in ${seen} "
                "but ${dups} is claimed more than once: boost refuses every "
                "duplicated letter at parse time.")
    endif()
    message("cli.options: ${name} advertises ${seen} with no repeats")

    # The symptom itself, not just the table it comes from.
    execute_process(COMMAND ${app} -c h264 --help
                    OUTPUT_VARIABLE out ERROR_VARIABLE err
                    RESULT_VARIABLE rc2 OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc2 EQUAL 0)
        message(FATAL_ERROR "${name} -c exited with ${rc2}:\n${out}${err}")
    endif()
    if(out MATCHES "ambiguous" OR err MATCHES "ambiguous")
        message(FATAL_ERROR "${name} -c is ambiguous:\n${out}${err}")
    endif()

    # --colorspace is the option whose short form was dropped to free -c. It stays
    # long-only on purpose, so the pair still has to parse together.
    execute_process(COMMAND ${app} --codec hevc --colorspace 444 --help
                    OUTPUT_VARIABLE out2 ERROR_VARIABLE err2
                    RESULT_VARIABLE rc3 OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc3 EQUAL 0)
        message(FATAL_ERROR "${name} rejected --codec together with --colorspace "
                "(exit ${rc3}):\n${out2}${err2}")
    endif()
endforeach()
