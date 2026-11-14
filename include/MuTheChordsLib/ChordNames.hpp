#pragma once

#include <MuTheChordsLib/ChordEngine.hpp>
#include <MuTheChordsLib/ChordSnapshot.hpp>
#include <array>
#include <cstdlib>
#include <string>

namespace muthechords {

  enum class Spelling { Auto = 0, Flats = 1, Sharps = 2 };

  namespace naming {

    inline int pc(int n) { return (n % 12 + 12) % 12; }

    inline constexpr std::array<int, 7> natural{0, 2, 4, 5, 7, 9, 11};
    inline constexpr std::array<char, 7> letters{'C', 'D', 'E', 'F', 'G', 'A', 'B'};

    inline int accidental(int pitch, int letter) {
      int result = pc(pitch - natural[static_cast<size_t>(letter)]);
      return result > 6 ? result - 12 : result;
    }

    struct WrittenPitch {
      int letter = 0, accidental = 0;
    };

    inline std::array<WrittenPitch, 12> spellings(const ChordSnapshot &s, Spelling preference) {
      std::array<WrittenPitch, 12> result{};
      const std::array<int, 12> sharpLetters{0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
      const std::array<int, 12> flatLetters{0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
      bool flats = preference == Spelling::Flats;
      int tonic = 0;
      const auto &selectedScale =
          scales[static_cast<size_t>(std::clamp(s.scale, 0, scaleCount - 1))];
      if (preference == Spelling::Auto && selectedScale.size() == 7) {
        const auto &scale = selectedScale;
        // Resolve equal-cost signatures with familiar tonic spellings (F# rather
        // than Gb).
        const std::array<int, 12> majorPreferred{0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};
        const std::array<int, 12> minorPreferred{0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6};
        const auto &preferred =
            (s.scale < 9 ? s.scale >= 5 : scale[2] == 3) ? minorPreferred : majorPreferred;
        int best = 100000;
        for (int letter = 0; letter < 7; ++letter) {
          int cost = 0, sum = 0;
          for (int degree = 0; degree < 7; ++degree) {
            const int a =
                accidental(s.key + scale[static_cast<size_t>(degree)], (letter + degree) % 7);
            cost += std::abs(a) + (std::abs(a) > 1 ? 4 * std::abs(a) : 0);
            sum += a;
          }
          cost = cost * 10 + (letter == preferred[static_cast<size_t>(pc(s.key))] ? 0 : 1);
          if (cost < best) {
            best = cost;
            tonic = letter;
            flats = sum < 0;
          }
        }
      }
      const auto &fallback = flats ? flatLetters : sharpLetters;
      for (int pitch = 0; pitch < 12; ++pitch) {
        const int letter = fallback[static_cast<size_t>(pitch)];
        result[static_cast<size_t>(pitch)] = {letter, accidental(pitch, letter)};
      }
      if (preference == Spelling::Auto && selectedScale.size() == 7) {
        const auto &scale = selectedScale;
        for (int d = 0; d < 7; ++d) {
          const int pitch = pc(s.key + scale[static_cast<size_t>(d)]), letter = (tonic + d) % 7;
          result[static_cast<size_t>(pitch)] = {letter, accidental(pitch, letter)};
        }
      }
      return result;
    }

    inline std::string written(WrittenPitch p) {
      std::string name(1, letters[static_cast<size_t>(p.letter)]);
      name.append(static_cast<size_t>(std::abs(p.accidental)), p.accidental < 0 ? 'b' : '#');
      return name;
    }

  } // namespace naming

  // Octaves follow the instrument convention: MIDI 60 is C3. Compute them from
  // the written letter, so enharmonic B# and Cb cross the octave boundary
  // correctly.
  [[nodiscard]] inline std::string pitchName(int midi, const ChordSnapshot &snapshot,
                                             Spelling spelling, bool includeOctave = true) {
    const auto p = naming::spellings(snapshot, spelling)[static_cast<size_t>(naming::pc(midi))];
    auto result = naming::written(p);
    if (includeOctave) {
      const int n = midi - naming::natural[static_cast<size_t>(p.letter)] - p.accidental;
      const int octave = (n >= 0 ? n / 12 : (n - 11) / 12) - 2;
      result += std::to_string(octave);
    }
    return result;
  }

  [[nodiscard]] inline std::string chordName(const ChordSnapshot &s,
                                             Spelling spelling = Spelling::Auto) {
    const int soundingCount = std::clamp(s.count, 0, 8);
    if (soundingCount == 0) {
      return "";
    }
    const auto pitches = [&]() {
      std::string result;
      for (int i = 0; i < soundingCount; ++i) {
        if (i) {
          result += " + ";
        }
        result += pitchName(s.notes[static_cast<size_t>(i)], s, spelling);
      }
      return result;
    };
    if (soundingCount < 3 || soundingCount != s.intendedCount || s.root < 0) {
      return pitches();
    }
    std::array<bool, 12> actual{}, expected{};
    int count = soundingCount, rootDoublings = 0;
    for (int i = 0; i < soundingCount; ++i) {
      const auto interval =
          static_cast<size_t>(naming::pc(s.notes[static_cast<size_t>(i)] - s.root));
      if (actual[interval]) {
        if (interval != 0 || ++rootDoublings > 1) {
          return pitches();
        }
        --count;
      }
      actual[interval] = true;
    }
    if (count < 3 || count > 7) {
      return pitches();
    }
    const auto &scale = scales[static_cast<size_t>(std::clamp(s.scale, 0, scaleCount - 1))];
    if (scale.size() != 7) {
      return pitches();
    }
    const int degree = std::clamp(s.degree, 0, 6);
    std::array<int, 7> intervals{};
    for (int i = 0; i < count; ++i) {
      intervals[static_cast<size_t>(i)] = naming::pc(
          scale[static_cast<size_t>((degree + 2 * i) % 7)] - scale[static_cast<size_t>(degree)]);
      expected[static_cast<size_t>(intervals[static_cast<size_t>(i)])] = true;
    }
    if (actual != expected) {
      return pitches();
    }
    const int third = intervals[1], fifth = intervals[2];
    std::string suffix;
    if (count == 3) {
      if (third == 4 && fifth == 7) {
        suffix = "";
      } else if (third == 3 && fifth == 7) {
        suffix = "m";
      } else if (third == 3 && fifth == 6) {
        suffix = "dim";
      } else if (third == 4 && fifth == 8) {
        suffix = "aug";
      } else {
        return pitches();
      }
    } else {
      const int seventh = intervals[3];
      const std::string extension = count == 4 ? "7" : count == 5 ? "9" : count == 6 ? "11" : "13";
      if (third == 4 && fifth == 7 && seventh == 11) {
        suffix = "maj" + extension;
      } else if (third == 4 && fifth == 7 && seventh == 10) {
        suffix = extension;
      } else if (third == 3 && fifth == 7 && seventh == 10) {
        suffix = "m" + extension;
      } else if (third == 3 && fifth == 7 && seventh == 11) {
        suffix = "m(maj" + extension + ")";
      } else if (third == 3 && fifth == 6 && seventh == 10) {
        suffix = "m" + extension + "b5";
      } else if (third == 3 && fifth == 6 && seventh == 9 && count == 4) {
        suffix = "dim7";
      } else {
        return pitches();
      }
      std::string alterations;
      for (int i = 4; i < count; ++i) {
        const int interval = intervals[static_cast<size_t>(i)];
        const int natural = i == 4 ? 2 : i == 5 ? 5 : 9;
        if (interval == natural) {
          continue;
        }
        if (interval != natural - 1 && interval != natural + 1) {
          return pitches();
        }
        if (!alterations.empty()) {
          alterations += ",";
        }
        alterations += (interval < natural ? "b" : "#") + std::string(i == 4   ? "9"
                                                                      : i == 5 ? "11"
                                                                               : "13");
      }
      // An altered ninth replaces the ninth implied by a five-note symbol.
      if (count == 5 && !alterations.empty()) {
        const auto position = suffix.find('9');
        if (position != std::string::npos) {
          suffix[position] = '7';
        }
      }
      if (!alterations.empty()) {
        suffix += "(" + alterations + ")";
      }
    }
    std::string result = pitchName(s.root, s, spelling, false) + suffix;
    const int bass = *std::min_element(s.notes.begin(), s.notes.begin() + soundingCount);
    if (naming::pc(bass) != naming::pc(s.root)) {
      result += "/" + pitchName(bass, s, spelling, false);
    }
    return result;
  }

} // namespace muthechords
