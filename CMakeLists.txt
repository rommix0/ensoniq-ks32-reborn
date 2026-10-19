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

add_subdirectory(tests)

# Command-line tools (file I/O allowed here, not in src/).
add_executable(esp_dis tools/esp_dis/main.cpp)
target_link_libraries(esp_dis PRIVATE ens_esp ens_common)
