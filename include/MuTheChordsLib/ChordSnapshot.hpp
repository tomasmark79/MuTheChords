#pragma once

#include <array>

namespace muthechords {

  // A copy of the generated chord's sounding notes and harmonic context at
  // trigger time. No strings or dynamic storage are used on the audio thread.
  struct ChordSnapshot {
    int root = -1;
    int key = 0;
    int scale = 0;
    int degree = 0;
    int count = 0;
    int intendedCount = 0;
    std::array<int, 8> notes{};
  };

} // namespace muthechords
