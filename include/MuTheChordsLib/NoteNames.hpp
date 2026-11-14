#pragma once

#include <array>
#include <string>

namespace muthechords {

  inline constexpr std::array<const char *, 12> pitchNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                           "F#", "G",  "G#", "A",  "A#", "B"};

  [[nodiscard]] inline std::string noteNameStd(int note) {
    const int pc = ((note % 12) + 12) % 12;
    return std::string(pitchNames[static_cast<std::size_t>(pc)]) + std::to_string(note / 12 - 2);
  }

} // namespace muthechords
