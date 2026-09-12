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

# Vendor layers self-register through static initializers (HALCODEC_CONNECT).
# On ELF, ld drops shared libs whose symbols are unreferenced by default, so
# they must be force-loaded or registration never runs. macOS loads all linked
# dylibs and does not need the flag.
set(LINK_LAYERS "")
if(APPLE)
    list(APPEND LINK_LAYERS vtbox_layers)
else()
    list(APPEND LINK_LAYERS
            -Wl,--no-as-needed
            nvenc_layers
            nvjpeg_layers
            -Wl,--as-needed)
    if(AMF_SDK_DIR)
        list(APPEND LINK_LAYERS -Wl,--no-as-needed amf_layers -Wl,--as-needed)
    endif()
    if(QSV_SDK_DIR)
        list(APPEND LINK_LAYERS -Wl,--no-as-needed qsv_layers -Wl,--as-needed)
    endif()
endif()

target_link_libraries(hal_enc PUBLIC ${LINK_LAYERS} Boost::program_options)

target_link_libraries(codecinfo PUBLIC ${LINK_LAYERS} Boost::program_options)

target_link_libraries(hal_dec PUBLIC ${LINK_LAYERS} Boost::program_options)
