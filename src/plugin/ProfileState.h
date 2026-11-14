#pragma once

#include <MuTheChordsLib/PerformanceTypes.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

namespace muthechords {

  // External profiles contain settings and a stable pattern reference, never
  // playback state or library content. Version 2 values are numeric, including
  // after XML host restore. Decode validates everything before returning a copy;
  // the caller decides when to replace the live parameter state.
  class ProfileState {
  public:
    static juce::String encode(juce::AudioProcessorValueTreeState &, const std::vector<Pattern> &,
                               int selectedPattern, const juce::ValueTree &loopRegions = {});
    static juce::Result decode(const juce::String &, juce::AudioProcessorValueTreeState &,
                               const std::vector<Pattern> &, juce::ValueTree &validatedState);
  };

} // namespace muthechords
