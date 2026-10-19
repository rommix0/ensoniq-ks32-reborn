cmake_minimum_required(VERSION 3.20)
project(ens_chips CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

option(ENS_BUILD_MAME_REF "Build the vendored MAME reference tool" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})  # build/ens_tests

enable_testing()

# Header-only helpers shared by every chip core: #include "common/bits.h"
add_library(ens_common INTERFACE)
target_include_directories(ens_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Chip cores. Later tasks add sources to these lists.
add_library(ens_otis STATIC src/otis/otis.cpp)
target_link_libraries(ens_otis PUBLIC ens_common)

add_library(ens_esp STATIC src/esp/esp_alu.cpp src/esp/esp_mac.cpp src/esp/esp_agen.cpp
  src/esp/esp_isa.cpp src/esp/esp.cpp)
target_link_libraries(ens_esp PUBLIC ens_common)

# Trace format + runner. The parser and PCM compare are chip-free (reused by the MAME harness).
add_library(ens_trace_parse STATIC src/trace/trace_parse.cpp)
target_include_directories(ens_trace_parse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_library(ens_trace STATIC src/trace/trace.cpp)
target_link_libraries(ens_trace PUBLIC ens_trace_parse ens_otis ens_esp)
target_compile_definitions(ens_trace PRIVATE _CRT_SECURE_NO_WARNINGS)  # fopen on MSVC (C4996)

add_subdirectory(tests)

# Command-line tools (file I/O allowed here, not in src/).
add_executable(esp_dis tools/esp_dis/main.cpp)
target_link_libraries(esp_dis PRIVATE ens_esp ens_common)
target_compile_definitions(esp_dis PRIVATE _CRT_SECURE_NO_WARNINGS)  # fopen on MSVC (C4996)

# Executable targets are suffixed _cli because the ens_trace library owns that target name.
add_executable(ens_trace_cli tools/ens_trace/main.cpp)
set_target_properties(ens_trace_cli PROPERTIES OUTPUT_NAME ens_trace)
target_link_libraries(ens_trace_cli PRIVATE ens_trace)
target_compile_definitions(ens_trace_cli PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(pcm_diff tools/pcm_diff/main.cpp)
target_link_libraries(pcm_diff PRIVATE ens_trace_parse)
target_compile_definitions(pcm_diff PRIVATE _CRT_SECURE_NO_WARNINGS)

# MAME reference harness (Task 12): vendored MAME chip code, built only on request.
if(ENS_BUILD_MAME_REF)
  add_subdirectory(tools/mame_ref)
endif()
