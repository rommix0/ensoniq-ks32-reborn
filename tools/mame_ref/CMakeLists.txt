# MAME reference harness (opt-in: -DENS_BUILD_MAME_REF=ON). Compiles MAME's own ES5505/ES5510
# code (vendor/, BSD-3-Clause, pinned in PINNED_COMMIT.txt) against a minimal framework shim
# (shim/) and drives it from a trace. Links only the chip-free trace parser, never our cores.
# See docs/mame-ref.md.

# The vendored files are third-party code: build them with warnings off. The project-wide
# warning flags are taken off this directory and put back on our own target (main.cpp + shim).
get_directory_property(ens_warning_flags COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

add_library(mame_ref_vendor STATIC vendor/es5506.cpp vendor/es5510.cpp)
target_include_directories(mame_ref_vendor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shim)
target_include_directories(mame_ref_vendor SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
if(MSVC)
  target_compile_options(mame_ref_vendor PRIVATE /W0)
  target_compile_definitions(mame_ref_vendor PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(mame_ref_vendor PRIVATE -w)
endif()

add_executable(mame_ref main.cpp)
target_compile_options(mame_ref PRIVATE ${ens_warning_flags})
target_link_libraries(mame_ref PRIVATE mame_ref_vendor ens_trace_parse)
target_compile_definitions(mame_ref PRIVATE _CRT_SECURE_NO_WARNINGS)  # fopen on MSVC (C4996)
