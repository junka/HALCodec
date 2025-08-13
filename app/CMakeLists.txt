cmake_minimum_required(VERSION 3.7)

add_executable(hal_enc hal_enc.cc)

add_executable(decinfo decinfo.cc)

add_executable(hal_dec hal_dec.cc)

target_include_directories(hal_enc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../NVEnc/layers)
target_link_libraries(hal_enc PRIVATE nvenc_layers)

target_include_directories(decinfo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../NVEnc/layers)
target_link_libraries(decinfo PRIVATE nvenc_layers)

target_include_directories(hal_dec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../NVEnc/layers)
target_link_libraries(hal_dec PRIVATE nvenc_layers)