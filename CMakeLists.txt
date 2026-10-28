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

set(ENS_M68K_TESTS_DIR "${CMAKE_SOURCE_DIR}/testdata/680x0/68000/m68t" CACHE PATH
  "Converted Tom Harte 68000 tests (.m68t, see docs/m68k.md); m68k_suite skips when absent")
set(ENS_ROM_DIR "${CMAKE_SOURCE_DIR}/ks-32" CACHE PATH "KS-32 ROM directory for ROM-dependent tests")

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

# Motorola 68000 core (Plan 5). Later tasks add the instruction-family sources to this list.
add_library(ens_m68k STATIC src/m68k/m68k_decode.cpp src/m68k/m68k_cpu.cpp src/m68k/m68k_ea.cpp
  src/m68k/m68k_ops_data.cpp src/m68k/m68k_alu.cpp src/m68k/m68k_ops_arith.cpp
  src/m68k/m68k_ops_logic.cpp src/m68k/m68k_ops_flow.cpp src/m68k/m68k_dis.cpp)
target_link_libraries(ens_m68k PUBLIC ens_common)

# MC68681 DUART (Plan 6): the KS-32's serial and timer chip.
add_library(ens_duart STATIC src/duart/duart.cpp)
target_link_libraries(ens_duart PUBLIC ens_common)

# Tom Harte's per-instruction tests (.m68t reader and runner); the data itself is not in the repo.
add_library(ens_m68k_harte STATIC src/m68k/harte_format.cpp)
target_link_libraries(ens_m68k_harte PUBLIC ens_m68k)

# KS-32 board: wave ROM layout, OTIS + ESP + delay DRAM and the frame routing.
add_library(ens_ks32 STATIC src/ks32/wave_roms.cpp src/ks32/board.cpp)
target_link_libraries(ens_ks32 PUBLIC ens_otis ens_esp)

# Pure player layer (no OS code). Later tasks add sources to this list.
add_library(ens_player STATIC src/player/resampler.cpp src/player/json.cpp src/player/sample_map.cpp
  src/player/effects.cpp src/player/voices.cpp src/player/esp_loader.cpp src/player/synth.cpp
  src/player/smf.cpp src/player/wav.cpp)
target_link_libraries(ens_player PUBLIC ens_common ens_ks32)

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

# KS-32 MIDI test player: offline render and waves; live play (WinMM MIDI in, WASAPI out) on Windows.
add_executable(ks32play tools/ks32play/main.cpp tools/ks32play/files.cpp)
target_link_libraries(ks32play PRIVATE ens_player)
if(WIN32)
  target_sources(ks32play PRIVATE tools/ks32play/midi_in_win.cpp tools/ks32play/audio_out_wasapi.cpp)
  target_link_libraries(ks32play PRIVATE winmm ole32 avrt)
endif()
target_compile_definitions(ks32play PRIVATE _CRT_SECURE_NO_WARNINGS)

# 68000 per-instruction suite runner (tools/m68k_harte/convert_harte.py makes the .m68t files).
add_executable(m68k_harte tools/m68k_harte/main.cpp)
target_link_libraries(m68k_harte PRIVATE ens_m68k_harte)
target_compile_definitions(m68k_harte PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME m68k_suite COMMAND m68k_harte ${ENS_M68K_TESTS_DIR})

# MAME reference harness (Task 12): vendored MAME chip code, built only on request.
if(ENS_BUILD_MAME_REF)
  add_subdirectory(tools/mame_ref)
endif()
