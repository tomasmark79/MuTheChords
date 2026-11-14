#pragma once

#include <MuTheChordsLib/ChordSnapshot.hpp>
#include <string>
#include <vector>

namespace muthechords {

  struct PatternNote {
    double beat = 0;
    double duration = 0;
    int note = 60;
    int velocity = 100;
  };

  struct Pattern {
    std::string id;
    std::string name;
    std::string category;
    std::string group;
    double length = 4;
    int numerator = 4;
    int denominator = 4;
    int sourceRoot = 60;
    std::vector<PatternNote> events;
  };

  struct PerformanceSettings {
    double rate = 1;
    int division = 0;
    int voices = 0;
    int passing = 0;
    int change = 0;
    int transition = 0;
    int playback = 0;
    int loopStart = 0;
    int loopEnd = 0; // zero-based start, exclusive end; 0 end means full length
  };

} // namespace muthechords
