#pragma once

#include <array>

namespace muthechords {

  enum Voicing {
    rootPosition = 0,
    firstInversion,
    secondInversion,
    thirdInversion,
    fourthInversion,
    fifthInversion,
    sixthInversion,
    bassDown,
    topUp,
    openVoicing,
    wideVoicing,
    dropTwo,
    bassOctave,
    octaveUp,
    octaveDown,
    voicingCount
  };

  inline constexpr std::array<const char *, voicingCount> voicingNames{
      "Root",          "1st inversion", "2nd inversion", "3rd inversion", "4th inversion",
      "5th inversion", "6th inversion", "Bass down",     "Top up",        "Open",
      "Wide",          "Drop 2",        "Bass octave",   "Octave up",     "Octave down"};

  inline constexpr std::array<const char *, voicingCount> voicingDescriptions{
      "Stacked from the root.",
      "Move the lowest voice up an octave.",
      "Move the two lowest voices up an octave.",
      "Move the three lowest voices up an octave.",
      "Move the four lowest voices up an octave.",
      "Move the five lowest voices up an octave.",
      "Move the six lowest voices up an octave.",
      "Move the root down an octave.",
      "Move the highest voice up an octave.",
      "Move the second chord voice up an octave.",
      "Move the root down and the second chord voice up an octave.",
      "Move the second-highest voice down an octave.",
      "Add the root an octave below.",
      "Move all voices up an octave.",
      "Move all voices down an octave."};

} // namespace muthechords
