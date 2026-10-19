# Adding a test file is a one-line change: append it to ENS_TEST_SOURCES.
set(ENS_TEST_SOURCES
  main.cpp
  test_bits.cpp
  test_otis_regs.cpp
  test_otis_addr.cpp
  test_otis_signal.cpp
  test_esp_alu.cpp
)

add_executable(ens_tests ${ENS_TEST_SOURCES})
target_link_libraries(ens_tests PRIVATE ens_otis ens_esp ens_common)
add_test(NAME ens_tests COMMAND ens_tests)
