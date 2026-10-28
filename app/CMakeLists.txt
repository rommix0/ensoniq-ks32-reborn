# The KS-32 JUCE app (Plan 7): Standalone and VST3 around ens::app::Engine.
# JUCE is fetched into the build directory (<build>/_deps), never into the source tree.
enable_language(C)  # JUCE compiles C sources; the default builds stay C++ only
include(FetchContent)
FetchContent_Declare(juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE
  GIT_TAG 8.0.16-security  # ruling R6: the security release of 8.0.15
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_plugin(KS32
  PRODUCT_NAME "KS-32"
  VERSION 0.7.0
  BUNDLE_ID com.ks32emulator.ks32
  COMPANY_NAME "KS-32 Emulator"
  PLUGIN_MANUFACTURER_CODE Kemu
  PLUGIN_CODE Ks32
  FORMATS Standalone VST3
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE)

target_sources(KS32 PRIVATE
  source/Processor.cpp source/PanelEditor.cpp source/AppSettings.cpp source/RomFolder.cpp source/MemoryFile.cpp
  source/SettingsPage.cpp)
target_compile_definitions(KS32 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(KS32
  PRIVATE ens_app juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The checks that need JUCE but no window of their own: the ROM folder, the memory file and the
# processor's memory-file guards (dummy files only), and the built VST3 loaded through JUCE's VST3
# host (ruling R8; needs the ROMs). The processor is compiled in as a plain class (no wrapper).
juce_add_console_app(KS32_AppTests PRODUCT_NAME "KS32_AppTests" VERSION 0.7.0)
target_sources(KS32_AppTests PRIVATE
  tests/app_tests.cpp source/RomFolder.cpp source/MemoryFile.cpp source/AppSettings.cpp
  source/Processor.cpp source/PanelEditor.cpp source/SettingsPage.cpp)
target_compile_definitions(KS32_AppTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1
  JucePlugin_Name="KS-32" JucePlugin_Build_Standalone=0
  KS32_ROM_DIR="${ENS_ROM_DIR}"
  KS32_VST3_PATH="$<GENEX_EVAL:$<TARGET_PROPERTY:KS32_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
add_dependencies(KS32_AppTests KS32_VST3)
target_link_libraries(KS32_AppTests
  PRIVATE ens_app juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME app_tests COMMAND KS32_AppTests)
