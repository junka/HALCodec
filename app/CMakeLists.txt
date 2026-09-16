cmake_minimum_required(VERSION 3.10)

# Set policy CMP0167 to NEW to suppress warnings about FindBoost removal
if(POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(hal_enc hal_enc.cc)

add_executable(codecinfo codecinfo.cc)

add_executable(hal_dec hal_dec.cc parse_cli.h)

# Backends are loaded at runtime via dlopen (see src/plugin_loader.h) instead of
# link-time binding. Link-time binding forces the dynamic loader to resolve every
# backend's NEEDED entries (libnvidia-encode.so.1, libnvjpeg.so.12, ...) at
# process start, which makes the executable unusable on machines lacking those
# driver libraries. With dlopen, a backend whose deps are missing is simply
# skipped and the rest still work.
#
# macOS is the exception: VideoToolbox is a system framework always present on
# Apple platforms, so vtbox_layers stays link-time bound there.
if(APPLE)
    target_link_libraries(hal_enc PUBLIC vtbox_layers halcodec_core Boost::program_options)
    target_link_libraries(codecinfo PUBLIC vtbox_layers halcodec_core Boost::program_options)
    target_link_libraries(hal_dec PUBLIC vtbox_layers halcodec_core Boost::program_options)
else()
    target_link_libraries(hal_enc PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})
    target_link_libraries(codecinfo PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})
    target_link_libraries(hal_dec PUBLIC halcodec_core Boost::program_options ${CMAKE_DL_LIBS})

    # Stage backend .so's next to the executables so plugin_loader's ExeDir()
    # search finds them in the build tree. Each exe depends on the backends so
    # a backend rebuild triggers the exe's POST_BUILD copy.
    set(_halcodec_backends "")
    if(TARGET nvenc_layers)
        list(APPEND _halcodec_backends nvenc_layers)
    endif()
    if(TARGET nvjpeg_layers)
        list(APPEND _halcodec_backends nvjpeg_layers)
    endif()
    if(TARGET amf_layers)
        list(APPEND _halcodec_backends amf_layers)
    endif()
    if(TARGET qsv_layers)
        list(APPEND _halcodec_backends qsv_layers)
    endif()
    foreach(_tgt hal_enc codecinfo hal_dec)
        foreach(_layer ${_halcodec_backends})
            add_dependencies(${_tgt} ${_layer})
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${_layer}> $<TARGET_FILE_DIR:${_tgt}>)
        endforeach()
    endforeach()
endif()
