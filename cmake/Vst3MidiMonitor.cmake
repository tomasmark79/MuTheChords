# Generate a transparent output observer; leave the pinned JUCE checkout intact.
set(chord_vst_wrapper "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${chord_vst_wrapper}")
file(READ "${chord_vst_wrapper}" chord_vst_source)
function(chord_vst_replace before after)
  string(FIND "${chord_vst_source}" "${before}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "JUCE VST3 wrapper changed; review the MIDI output observer")
  endif()
  string(REPLACE "${before}" "${after}" chord_vst_source "${chord_vst_source}")
  set(chord_vst_source "${chord_vst_source}" PARENT_SCOPE)
endfunction()
chord_vst_replace(
  "#include <juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h>"
  "#include <juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h>\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Vst3MidiOutputMonitor.h\"")
chord_vst_replace(
  "MidiEventList::pluginToHostEventList (*data.outputEvents, midiBuffer);"
  "{\n            auto* source = dynamic_cast<muthechords::MidiDiagnosticsSource*> (&getPluginInstance());\n            chordMidiOutputMonitor.setDestination (*data.outputEvents, source != nullptr ? &source->midiDiagnostics() : nullptr);\n            MidiEventList::pluginToHostEventList (chordMidiOutputMonitor, midiBuffer);\n        }")
chord_vst_replace("    MidiBuffer midiBuffer;" "    MidiBuffer midiBuffer;\n    muthechords::Vst3MidiOutputMonitor chordMidiOutputMonitor;")
set(chord_vst_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/MuTheChordsVST3.cpp")
file(GENERATE OUTPUT "${chord_vst_generated}" CONTENT "${chord_vst_source}")
get_target_property(chord_vst_sources juce_audio_plugin_client_VST3 INTERFACE_SOURCES)
string(REPLACE "${chord_vst_wrapper}" "${chord_vst_generated}" chord_vst_sources_patched "${chord_vst_sources}")
if(chord_vst_sources STREQUAL chord_vst_sources_patched)
  message(FATAL_ERROR "Could not select the observed JUCE VST3 wrapper")
endif()
set_property(TARGET juce_audio_plugin_client_VST3 PROPERTY INTERFACE_SOURCES "${chord_vst_sources_patched}")
