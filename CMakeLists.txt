cmake_minimum_required(VERSION 3.22)
project(MuTheChords VERSION 0.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)

option(MUTHECHORDS_BUILD_PLUGIN "Build JUCE/CLAP plugin formats" ON)
option(CHORD_EMBED_PRIVATE_PATTERNS "Embed a local private pattern library in plugin binaries" OFF)
set(CHORD_PRIVATE_PATTERN_LIBRARY "" CACHE FILEPATH "Private library JSON to embed (never committed)")

# Collect MuTheChordsLib sources (NixonCpp Utils + domain engine)
set(MUTHECHORDS_LIB_SOURCES
  src/lib/MuTheChordsLib.cpp
  src/lib/PatternLibrary.cpp
  src/lib/HarmonySync.cpp
  src/lib/Utils/UtilsFactory.cpp
  src/lib/Utils/Assets/AssetManager.cpp
  src/lib/Utils/Filesystem/DirectoryManager.cpp
  src/lib/Utils/Filesystem/FileReader.cpp
  src/lib/Utils/Filesystem/FileWriter.cpp
  src/lib/Utils/Filesystem/PathResolver.cpp
  src/lib/Utils/Json/CustomStringsLoader.cpp
  src/lib/Utils/Json/JsonSerializer.cpp
  src/lib/Utils/Logger/LoggerFactory.cpp
  src/lib/Utils/Platform/EmscriptenPlatformInfo.cpp
  src/lib/Utils/Platform/PlatformInfoFactory.cpp
  src/lib/Utils/Platform/UnixPlatformInfo.cpp
  src/lib/Utils/Platform/WindowsPlatformInfo.cpp
  src/lib/Utils/String/StringFormatter.cpp
)

set(MUTHECHORDS_HEADER_INCLUDE_DIRS "")
if(DEFINED ENV{NIX_HEADER_INCLUDE_DIRS})
  set(MUTHECHORDS_HEADER_INCLUDE_DIRS $ENV{NIX_HEADER_INCLUDE_DIRS})
endif()

if(MUTHECHORDS_BUILD_PLUGIN)
  add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
  include(cmake/Vst3MidiMonitor.cmake)
  add_subdirectory(third_party/clap-juce-extensions EXCLUDE_FROM_ALL)

  set(MUTHECHORDS_AUTO_MANIFEST TRUE)
  if(CMAKE_CROSSCOMPILING)
    set(MUTHECHORDS_AUTO_MANIFEST FALSE)
  endif()

  juce_add_plugin(MuTheChords
    COMPANY_NAME "DotName"
    BUNDLE_ID "com.dotname.muthechords"
    PLUGIN_MANUFACTURER_CODE Dotn PLUGIN_CODE Mtch
    PRODUCT_NAME "MuTheChords"
    FORMATS VST3
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${MUTHECHORDS_AUTO_MANIFEST}
    VST3_CATEGORIES Instrument Tools)

  target_sources(MuTheChords PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/plugin/ProfileState.cpp
    ${MUTHECHORDS_LIB_SOURCES}
  )

  target_include_directories(MuTheChords PUBLIC
    include
    src/lib
    src/plugin
    ${MUTHECHORDS_HEADER_INCLUDE_DIRS}
  )

  target_compile_definitions(MuTheChords PUBLIC
    FMT_HEADER_ONLY=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_XINPUT=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

  target_link_libraries(MuTheChords PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)

  if(CHORD_EMBED_PRIVATE_PATTERNS)
    if(NOT EXISTS "${CHORD_PRIVATE_PATTERN_LIBRARY}")
      message(FATAL_ERROR "Set CHORD_PRIVATE_PATTERN_LIBRARY to the local private library JSON")
    endif()
    juce_add_binary_data(ChordPrivatePatterns NAMESPACE chord_private_patterns
      HEADER_NAME ChordPrivatePatterns.h SOURCES "${CHORD_PRIVATE_PATTERN_LIBRARY}")
    target_link_libraries(MuTheChords PRIVATE ChordPrivatePatterns)
    target_compile_definitions(MuTheChords PRIVATE CHORD_EMBED_PRIVATE_PATTERNS=1)
  endif()

  clap_juce_extensions_plugin(TARGET MuTheChords
    CLAP_ID "com.dotname.muthechords" CLAP_FEATURES note-effect)

  if(BUILD_TESTING)
    add_executable(muthechords_vst_midi_monitor_tests tests/VstMidiMonitorTests.cpp)
    target_include_directories(muthechords_vst_midi_monitor_tests PRIVATE
      include src/lib src/plugin
      third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK)
    add_test(NAME muthechords_vst_midi_monitor COMMAND muthechords_vst_midi_monitor_tests)

    add_executable(muthechords_performance_processor_tests tests/PerformanceProcessorTests.cpp)
    target_include_directories(muthechords_performance_processor_tests PRIVATE include src/lib src/plugin ${MUTHECHORDS_HEADER_INCLUDE_DIRS})
    target_link_libraries(muthechords_performance_processor_tests PRIVATE MuTheChords juce::juce_audio_utils)
    add_test(NAME muthechords_performance_processor COMMAND muthechords_performance_processor_tests)

    add_executable(muthechords_profile_tests tests/ProfileStateTests.cpp src/plugin/ProfileState.cpp)
    target_include_directories(muthechords_profile_tests PRIVATE include src/lib src/plugin ${MUTHECHORDS_HEADER_INCLUDE_DIRS})
    target_compile_definitions(muthechords_profile_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(muthechords_profile_tests PRIVATE juce::juce_audio_processors)
    add_test(NAME muthechords_profile COMMAND muthechords_profile_tests)

    add_executable(muthechords_processor_tests tests/ProcessorTests.cpp)
    target_include_directories(muthechords_processor_tests PRIVATE include src/lib src/plugin ${MUTHECHORDS_HEADER_INCLUDE_DIRS})
    target_link_libraries(muthechords_processor_tests PRIVATE MuTheChords juce::juce_audio_utils)
    add_test(NAME muthechords_processor COMMAND muthechords_processor_tests)

    add_executable(muthechords_clap_smoke tests/ClapSmoke.cpp)
    target_include_directories(muthechords_clap_smoke PRIVATE third_party/clap-juce-extensions/clap-libs/clap/include)
    target_link_libraries(muthechords_clap_smoke PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME muthechords_clap COMMAND muthechords_clap_smoke $<TARGET_FILE:MuTheChords_CLAP>)

    add_executable(muthechords_vst_smoke tests/VstSmoke.cpp)
    target_include_directories(muthechords_vst_smoke PRIVATE third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK)
    target_compile_definitions(muthechords_vst_smoke PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_XINPUT=0)
    target_link_libraries(muthechords_vst_smoke PRIVATE juce::juce_audio_utils)
    add_test(NAME muthechords_vst3 COMMAND muthechords_vst_smoke $<GENEX_EVAL:$<TARGET_PROPERTY:MuTheChords_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>)

    set(bitwig_test_host "${CMAKE_CURRENT_BINARY_DIR}/Bitwig Studio/BitwigOutputTest${CMAKE_EXECUTABLE_SUFFIX}")
    add_custom_command(TARGET muthechords_vst_smoke POST_BUILD
      BYPRODUCTS "${bitwig_test_host}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Bitwig Studio"
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:muthechords_vst_smoke> "${bitwig_test_host}")
    add_test(NAME muthechords_vst3_bitwig_output COMMAND "${bitwig_test_host}" $<GENEX_EVAL:$<TARGET_PROPERTY:MuTheChords_VST3,JUCE_PLUGIN_ARTEFACT_FILE>> --bitwig-output)
  endif()
endif()
