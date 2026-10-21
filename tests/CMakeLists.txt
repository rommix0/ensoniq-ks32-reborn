# Adding a test file is a one-line change: append it to ENS_TEST_SOURCES.
set(ENS_TEST_SOURCES
  main.cpp
  test_bits.cpp
  test_otis_regs.cpp
  test_otis_addr.cpp
  test_otis_signal.cpp
  test_esp_alu.cpp
  test_esp_mac.cpp
  test_esp_agen.cpp
  test_esp_core.cpp
  test_esp_host.cpp
  test_esp_isa.cpp
  test_trace.cpp
  test_esp_programs.cpp
  test_ks32_board.cpp
  test_player_resampler.cpp
)

add_executable(ens_tests ${ENS_TEST_SOURCES})
target_link_libraries(ens_tests PRIVATE ens_player ens_ks32 ens_trace ens_otis ens_esp ens_common)
target_compile_definitions(ens_tests PRIVATE ENS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  _CRT_SECURE_NO_WARNINGS)
add_test(NAME ens_tests COMMAND ens_tests)
