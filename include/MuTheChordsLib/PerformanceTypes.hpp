#pragma once

#include <MuTheChordsLib/ChordSnapshot.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace muthechords {

  struct PatternNote {
    double beat = 0;
    double duration = 0;
    int note = 60;
    int velocity = 100;
  };

  struct PatternPlayback {
    std::vector<size_t> eventOrder;
    int sourceAnchor = 0;
    bool sourceMinor = false;
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
    std::optional<PatternPlayback> playback;

    [[nodiscard]] PatternPlayback preparePlayback() const {
      PatternPlayback result;
      result.sourceAnchor = sourceRoot;
      result.eventOrder.reserve(events.size());
      double earliestBass = std::numeric_limits<double>::infinity();
      for (size_t i = 0; i < events.size(); ++i) {
        const auto &event = events[i];
        if (!std::isfinite(event.beat) || !std::isfinite(event.duration) || event.beat < 0 ||
            event.beat >= length || event.duration <= 0) {
          continue;
        }
        result.eventOrder.push_back(i);
        // Anchor bass phrases to their first sounding note.
        if (category == "Bass" && event.note >= 0 && event.note <= 127) {
          if (event.beat < earliestBass) {
            earliestBass = event.beat;
            result.sourceAnchor = event.note;
          } else if (event.beat == earliestBass) {
            result.sourceAnchor = std::min(result.sourceAnchor, event.note);
          }
        }
      }
      std::stable_sort(result.eventOrder.begin(), result.eventOrder.end(),
                       [&](size_t a, size_t b) { return events[a].beat < events[b].beat; });

      // Source files carry a root but no scale. Infer the scale from weighted
      // minor-third and major-third evidence.
      double minorWeight = 0, majorWeight = 0;
      for (const auto &event : events) {
        if (!std::isfinite(event.duration) || event.duration <= 0) {
          continue;
        }
        const int pitchClass = ((event.note - result.sourceAnchor) % 12 + 12) % 12;
        const double weight = event.duration * std::clamp(event.velocity, 1, 127);
        if (pitchClass == 3) {
          minorWeight += weight;
        } else if (pitchClass == 4) {
          majorWeight += weight;
        }
      }
      result.sourceMinor = minorWeight > majorWeight;
      return result;
    }
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
