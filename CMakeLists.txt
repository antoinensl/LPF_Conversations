cmake_minimum_required(VERSION 3.22)
project(LPFDialogue VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)   # M_PI on Windows
  add_compile_options(/utf-8)                           # sources are UTF-8 (French text)
endif()
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

option(LPFD_BUILD_PLUGIN "Build the JUCE plugin (VST3 / AU / Standalone)" ON)
option(LPFD_BUILD_TESTS "Build tests and the command-line tool" ON)
set(LPFD_JUCE_DIR "" CACHE PATH "Local JUCE checkout (otherwise downloaded)")

# ---- core (no dependencies) ----
add_library(lpfd_core STATIC core/analyzer.cpp core/engine.cpp)
target_include_directories(lpfd_core PUBLIC core)
set_target_properties(lpfd_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(lpfd_core PRIVATE -Wall -Wextra)
endif()

if(LPFD_BUILD_TESTS)
  add_executable(lpfd_tests tests/test_main.cpp)
  target_link_libraries(lpfd_tests PRIVATE lpfd_core)
  if(NOT MSVC)
    target_compile_options(lpfd_tests PRIVATE -Wno-mismatched-new-delete)
  endif()
  add_executable(lpf_dialogue_cli tools/dialogue_cli.cpp)
  target_link_libraries(lpf_dialogue_cli PRIVATE lpfd_core)
  enable_testing()
  add_test(NAME lpfd_tests COMMAND lpfd_tests)
endif()

# ---- plugin ----
if(LPFD_BUILD_PLUGIN)
  if(LPFD_JUCE_DIR)
    add_subdirectory(${LPFD_JUCE_DIR} juce EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(juce
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.4
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
  endif()

  set(LPFD_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND LPFD_FORMATS AU)
  endif()

  juce_add_plugin(LPFDialogue
    COMPANY_NAME "Life Pass Filter"
    BUNDLE_ID "com.lifepassfilter.dialogue"
    PLUGIN_MANUFACTURER_CODE Lpfl
    PLUGIN_CODE LpDg
    PRODUCT_NAME "LPF Dialogue"
    FORMATS ${LPFD_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "LPF Dialogue listens to your instrument to answer it."
    COPY_PLUGIN_AFTER_BUILD FALSE)

  juce_add_binary_data(LPFDialogueAssets SOURCES
    assets/fonts/SpaceMono-Bold.ttf
    assets/fonts/AtkinsonHyperlegible-Regular.ttf assets/fonts/AtkinsonHyperlegible-Bold.ttf)
  set_target_properties(LPFDialogueAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

  target_sources(LPFDialogue PRIVATE plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/Ui.cpp)
  target_compile_definitions(LPFDialogue PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(LPFDialogue
    PRIVATE lpfd_core LPFDialogueAssets juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)
endif()
