cmake_minimum_required(VERSION 3.10)

# Set policy CMP0167 to NEW to suppress warnings about FindBoost removal
if(POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(hal_enc hal_enc.cc)

add_executable(decinfo decinfo.cc)

add_executable(hal_dec hal_dec.cc parse_cli.h)

set(LINK_LAYERS -Wl,--no-as-needed
                nvenc_layers
                nvjpeg_layers
                -Wl,--as-needed)
target_include_directories(hal_enc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../NVEnc/layers)
target_link_libraries(hal_enc PUBLIC ${LINK_LAYERS})

target_include_directories(decinfo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../NVEnc/layers)
target_link_libraries(decinfo PUBLIC ${LINK_LAYERS})

target_include_directories(hal_dec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../NVEnc/layers)
target_link_libraries(hal_dec PUBLIC ${LINK_LAYERS} Boost::program_options)