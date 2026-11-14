#pragma once

#include <array>
#include <cstddef>

namespace muthechords {

  // Pitch classes in each scale, ascending from the tonic.
  struct Scale {
    int count;
    std::array<int, 12> pitches;

    [[nodiscard]] constexpr int size() const noexcept { return count; }
    [[nodiscard]] constexpr const int *begin() const noexcept { return pitches.data(); }
    [[nodiscard]] constexpr const int *end() const noexcept { return pitches.data() + count; }
    [[nodiscard]] constexpr int operator[](std::size_t index) const noexcept {
      return pitches[index];
    }
  };

  inline constexpr std::array<const char *, 44> scaleNames{{"Major",
                                                            "Dorian",
                                                            "Phrygian",
                                                            "Lydian",
                                                            "Mixolydian",
                                                            "Minor",
                                                            "Locrian",
                                                            "Harmonic Minor",
                                                            "Melodic Minor",
                                                            "Harmonic Major",
                                                            "Major Pentatonic",
                                                            "Minor Pentatonic",
                                                            "Egyptian Pentatonic",
                                                            "Minor Blues",
                                                            "Major Blues",
                                                            "Kansas City Blues",
                                                            "Lydian Dominant",
                                                            "Phrygian Dominant",
                                                            "Mixolydian b6",
                                                            "Dorian b2",
                                                            "Bebop Dominant",
                                                            "Bebop Major",
                                                            "Bebop Minor",
                                                            "Hungarian Minor",
                                                            "Double Harmonic Major",
                                                            "Persian",
                                                            "Gypsy Minor",
                                                            "Neapolitan Minor",
                                                            "Neapolitan Major",
                                                            "Jewish (Ahava Raba)",
                                                            "Hirajoshi",
                                                            "In Sen",
                                                            "Arabic (Hijaz)",
                                                            "Spanish 8-Tone",
                                                            "Whole Tone",
                                                            "Diminished",
                                                            "Octatonic",
                                                            "Altered",
                                                            "Chromatic",
                                                            "Lydian Augmented",
                                                            "Whole-Half Diminished",
                                                            "Ukrainian Dorian",
                                                            "Prometheus",
                                                            "Iwato"}};

  inline constexpr std::array<Scale, 44> scales{{
      {7, {{0, 2, 4, 5, 7, 9, 11}}},                  // Major
      {7, {{0, 2, 3, 5, 7, 9, 10}}},                  // Dorian
      {7, {{0, 1, 3, 5, 7, 8, 10}}},                  // Phrygian
      {7, {{0, 2, 4, 6, 7, 9, 11}}},                  // Lydian
      {7, {{0, 2, 4, 5, 7, 9, 10}}},                  // Mixolydian
      {7, {{0, 2, 3, 5, 7, 8, 10}}},                  // Aeolian (Natural Minor)
      {7, {{0, 1, 3, 5, 6, 8, 10}}},                  // Locrian
      {7, {{0, 2, 3, 5, 7, 8, 11}}},                  // Harmonic Minor
      {7, {{0, 2, 3, 5, 7, 9, 11}}},                  // Melodic Minor
      {7, {{0, 2, 4, 5, 7, 8, 11}}},                  // Harmonic Major
      {5, {{0, 2, 4, 7, 9}}},                         // Major Pentatonic
      {5, {{0, 3, 5, 7, 10}}},                        // Minor Pentatonic
      {5, {{0, 2, 5, 7, 10}}},                        // Egyptian Pentatonic
      {6, {{0, 3, 5, 6, 7, 10}}},                     // Minor Blues
      {6, {{0, 2, 3, 4, 7, 9}}},                      // Major Blues
      {5, {{0, 2, 5, 7, 8}}},                         // Kansas City Blues
      {7, {{0, 2, 4, 6, 7, 9, 10}}},                  // Lydian Dominant
      {7, {{0, 1, 4, 5, 7, 8, 10}}},                  // Phrygian Dominant
      {7, {{0, 2, 4, 5, 7, 8, 10}}},                  // Mixolydian b6
      {7, {{0, 1, 3, 5, 7, 9, 10}}},                  // Dorian b2
      {8, {{0, 2, 4, 5, 7, 9, 10, 11}}},              // Bebop Dominant
      {8, {{0, 2, 4, 5, 7, 8, 9, 11}}},               // Bebop Major
      {8, {{0, 2, 3, 5, 7, 8, 9, 11}}},               // Bebop Minor
      {7, {{0, 2, 3, 6, 7, 8, 11}}},                  // Hungarian Minor
      {7, {{0, 1, 4, 5, 7, 8, 11}}},                  // Double Harmonic Major
      {7, {{0, 1, 4, 5, 6, 8, 11}}},                  // Persian
      {7, {{0, 2, 3, 6, 7, 8, 10}}},                  // Gypsy Minor
      {7, {{0, 1, 3, 5, 7, 8, 11}}},                  // Neapolitan Minor
      {7, {{0, 1, 3, 5, 7, 9, 11}}},                  // Neapolitan Major
      {7, {{0, 1, 4, 5, 7, 8, 10}}},                  // Jewish (Ahava Raba)
      {5, {{0, 2, 3, 7, 8}}},                         // Hirajoshi
      {5, {{0, 1, 5, 7, 8}}},                         // In Sen
      {7, {{0, 1, 4, 5, 7, 8, 10}}},                  // Arabic (Hijaz)
      {8, {{0, 1, 3, 4, 5, 6, 8, 10}}},               // Spanish 8-Tone
      {6, {{0, 2, 4, 6, 8, 10}}},                     // Whole Tone
      {8, {{0, 1, 3, 4, 6, 7, 9, 10}}},               // Diminished
      {8, {{0, 2, 3, 5, 6, 8, 9, 11}}},               // Octatonic
      {7, {{0, 1, 3, 4, 6, 8, 10}}},                  // Altered
      {12, {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}}}, // Chromatic
      {7, {{0, 2, 4, 6, 8, 9, 11}}},                  // Lydian Augmented
      {8, {{0, 2, 3, 5, 6, 8, 9, 11}}},               // Whole-Half Diminished
      {7, {{0, 2, 3, 6, 7, 9, 10}}},                  // Ukrainian Dorian
      {6, {{0, 2, 4, 6, 9, 10}}},                     // Prometheus
      {5, {{0, 1, 5, 6, 10}}},                        // Iwato
  }};

  inline constexpr int scaleCount = static_cast<int>(scales.size());

} // namespace muthechords
