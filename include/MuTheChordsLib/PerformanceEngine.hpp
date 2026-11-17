#pragma once

#include <MuTheChordsLib/ChordEngine.hpp>
#include <MuTheChordsLib/PerformanceTypes.hpp>
#include <array>
#include <cmath>
#include <limits>

namespace muthechords {

  // Playback is allocation-free for patterns prepared by PatternLibrary.
  // Prepare standalone patterns before calling configure on the audio thread.
  // Pattern storage must remain valid until another pattern is configured.
  // Host positions are quarter-note PPQ; startBeat is the beat at sample zero.
  class PerformanceEngine {
  public:
    using Callback = void (*)(void *, Event, int);

    void setOutput(void *context, Callback fn) {
      destination = context;
      callback = fn;
    }

    void configure(const Pattern *next, PerformanceSettings settings) {
      if (!std::isfinite(settings.rate) || settings.rate <= 0) {
        settings.rate = 1;
      }
      settings.rate = std::clamp(settings.rate, 0.0625, 16.0);
      settings.division = std::clamp(settings.division, 0, 1);
      settings.voices = std::clamp(settings.voices, 0, 2);
      settings.passing = std::clamp(settings.passing, 0, 1);
      settings.change = std::clamp(settings.change, 0, 2);
      settings.transition = std::clamp(settings.transition, 0, 2);
      settings.playback = std::clamp(settings.playback, 0, 2);
      settings.loopStart = std::max(0, settings.loopStart);
      settings.loopEnd = std::max(0, settings.loopEnd);
      if (next == pattern && settings.rate == config.rate && settings.division == config.division &&
          settings.voices == config.voices && settings.passing == config.passing &&
          settings.change == config.change && settings.transition == config.transition &&
          settings.playback == config.playback && settings.loopStart == config.loopStart &&
          settings.loopEnd == config.loopEnd) {
        return;
      }
      if (next != pattern && next && !next->playback) {
        fallbackPlayback = next->preparePlayback();
      }
      stop();
      pattern = next;
      config = settings;
      if (usable()) {
        const double start = regionStart(), end = regionEnd();
        loopStartBeat = start;
        loopLength = std::max(epsilon, end - start);
        const auto &order = playback().eventOrder;
        const auto beforeBeat = [&](size_t i, double beat) {
          return pattern->events[i].beat < beat;
        };
        const auto first = std::lower_bound(order.begin(), order.end(), start, beforeBeat);
        firstEvent = static_cast<size_t>(first - order.begin());
        endEvent = static_cast<size_t>(std::lower_bound(first, order.end(), end, beforeBeat) -
                                       order.begin());
      }
    }

    void trigger(ChordSnapshot chord, int channel, double hostBeat, bool quantize,
                 double boundaryTolerance = 0, int inputVelocity = 100) {
      if (!usable() || chord.count <= 0 || !std::isfinite(hostBeat)) {
        return;
      }
      chord.count = std::clamp(chord.count, 0, 8);
      channel = std::clamp(channel, 1, 16);
      inputVelocity = std::clamp(inputVelocity, 1, 127);
      if (!active) {
        harmony = chord;
        outputChannel = channel;
        triggerVelocity = inputVelocity;
        active = true;
        origin = quantize ? nextQuarter(hostBeat, boundaryTolerance) : hostBeat;
        nextEvent = firstEvent;
        loopCycle = 0;
      } else if (config.change == 2) {
        pendingChord = chord;
        pendingChannel = channel;
        pendingVelocity = inputVelocity;
        pendingBeat = nextQuarter(hostBeat, boundaryTolerance);
        pending = true;
      } else {
        changeHarmony(chord, channel, inputVelocity);
        if (config.change == 1) {
          origin = hostBeat;
          nextEvent = firstEvent;
          loopCycle = 0;
        }
      }
    }

    void release() { stop(); }

    void resetChannel(int channel) {
      if (channel < 1 || channel > 16) {
        return;
      }
      for (auto &n : notes) {
        if (n.used && n.channel == channel) {
          drop(n);
        }
      }
      if (pending && pendingChannel == channel) {
        pending = false;
      }
      if (active && outputChannel == channel) {
        active = false;
        pending = false;
        harmony = {};
        lastPhase = 0;
      }
    }

    void stop() {
      for (auto &n : notes) {
        if (n.used) {
          drop(n);
        }
      }
      active = false;
      pending = false;
      harmony = {};
      lastPhase = 0;
    }

    // Half-open sample range. Call even for an empty range to set the
    // immediate-event offset.
    void advance(double startBeat, double beatsPerSample, int fromSample, int toSample) {
      sampleCursor = std::max(0, fromSample);
      if (!std::isfinite(startBeat) || !std::isfinite(beatsPerSample) || beatsPerSample <= 0 ||
          toSample < fromSample) {
        return;
      }
      const double endBeat = startBeat + toSample * beatsPerSample;
      while (active || liveCount > 0) {
        double next = active ? nextBeat() : std::numeric_limits<double>::infinity();
        const double finish = origin + loopLength / speed();
        if (active && config.playback == 1) {
          next = std::min(next, finish);
        }
        for (const auto &n : notes) {
          if (n.used) {
            next = std::min(next, n.offBeat);
          }
        }
        if (pending) {
          next = std::min(next, pendingBeat);
        }
        if (!std::isfinite(next) || next >= endBeat - epsilon) {
          break;
        }
        const double relative = (next - startBeat) / beatsPerSample;
        int sample = fromSample;
        if (relative > fromSample) {
          sample = static_cast<int>(std::min<double>(toSample, std::ceil(relative - 1e-7)));
        }
        if (sample >= toSample) {
          break;
        }
        sampleCursor = sample;
        const double now = startBeat + sample * beatsPerSample;
        // Note-offs precede every note-on at the same sample, including harmony
        // changes.
        for (auto &n : notes) {
          if (n.used && n.offBeat <= now + epsilon) {
            drop(n);
          }
        }
        if (active && config.playback == 1 && finish <= now + epsilon) {
          stop();
          break;
        }
        if (pending && pendingBeat <= now + epsilon) {
          changeHarmony(pendingChord, pendingChannel, pendingVelocity);
          pending = false;
        }
        // Usually one position; handles several events rounding to the same
        // sample.
        for (; active;) {
          const double beat = nextBeat();
          if (!std::isfinite(beat) || beat > now + epsilon) {
            break;
          }
          if (liveCount < notes.size()) {
            const double cycleEnd = origin + (loopCycle + 1) * loopLength / speed();
            play(pattern->events[playback().eventOrder[nextEvent]], beat, cycleEnd,
                 now + beatsPerSample);
          }
          if (++nextEvent == endEvent) {
            nextEvent = firstEvent;
            ++loopCycle;
          }
        }
      }
      sampleCursor = std::max(0, toSample);
      if (active && usable()) {
        const double p = std::max(0.0, (endBeat - origin) * speed());
        lastPhase = (loopStartBeat + std::fmod(p, loopLength)) / pattern->length;
      }
    }

    [[nodiscard]] const ChordSnapshot &snapshot() const { return harmony; }

    [[nodiscard]] bool isSounding(int note) const {
      if (note < 0 || note > 127) {
        return false;
      }
      for (const auto &channel : owners) {
        if (channel[static_cast<size_t>(note)] > 0) {
          return true;
        }
      }
      return false;
    }

    [[nodiscard]] double phase() const { return lastPhase; }
    [[nodiscard]] bool isActive() const { return active; }
    [[nodiscard]] int currentChannel() const { return outputChannel; }
    [[nodiscard]] int currentVelocity() const { return triggerVelocity; }

  private:
    static constexpr double epsilon = 1e-9;

    struct ActiveNote {
      double offBeat = 0;
      int note = 0, channel = 1;
      bool used = false;
    };

    // Exhaustion drops a new voice instead of stealing or allocating; old notes
    // still terminate.
    std::array<ActiveNote, 2048> notes{};
    std::array<std::array<unsigned int, 128>, 16> owners{};
    const Pattern *pattern = nullptr;
    PatternPlayback fallbackPlayback;
    PerformanceSettings config{};
    ChordSnapshot harmony{}, pendingChord{};
    double origin = 0, loopStartBeat = 0, loopLength = 1, pendingBeat = 0, lastPhase = 0;
    size_t firstEvent = 0, endEvent = 0, nextEvent = 0, loopCycle = 0;
    int outputChannel = 1, pendingChannel = 1, triggerVelocity = 100, pendingVelocity = 100,
        sampleCursor = 0;
    unsigned int liveCount = 0;
    bool active = false, pending = false;
    void *destination = nullptr;
    Callback callback = nullptr;

    [[nodiscard]] double speed() const { return config.rate * (config.division == 1 ? 1.5 : 1.0); }

    [[nodiscard]] bool usable() const {
      return pattern && std::isfinite(pattern->length) && pattern->length > epsilon &&
             !pattern->events.empty();
    }

    [[nodiscard]] double regionStart() const {
      const double bar = pattern->numerator * 4.0 / std::max(1, pattern->denominator);
      const double lastStart = std::max(0.0, (std::ceil(pattern->length / bar) - 1) * bar);
      return std::min(config.loopStart * bar, lastStart);
    }

    [[nodiscard]] double regionEnd() const {
      const double bar = pattern->numerator * 4.0 / std::max(1, pattern->denominator);
      const double end = std::min(pattern->length, config.loopEnd * bar);
      return config.loopEnd > 0 && end > regionStart() ? end : pattern->length;
    }

    static double nextQuarter(double beat, double tolerance = 0) {
      // Grid MIDI events round to whole samples. Do not postpone a beat by a
      // full quarter when its input timestamp is at most one sample late.
      const double nearest = std::round(beat);
      const double window = std::isfinite(tolerance) ? std::max(0.0, tolerance) : 0;
      return std::abs(beat - nearest) <= window + epsilon ? nearest : std::ceil(beat - epsilon);
    }

    [[nodiscard]] const PatternPlayback &playback() const {
      return pattern->playback ? *pattern->playback : fallbackPlayback;
    }

    [[nodiscard]] double nextBeat() const {
      if (firstEvent == endEvent) {
        return std::numeric_limits<double>::infinity();
      }
      const auto &event = pattern->events[playback().eventOrder[nextEvent]];
      return origin + (loopCycle * loopLength + (event.beat - loopStartBeat)) / speed();
    }

    void emit(bool on, int channel, int note, int velocity) {
      if (callback) {
        callback(destination, {on, channel, note, velocity}, sampleCursor);
      }
    }

    void drop(ActiveNote &n) {
      auto &count = owners[static_cast<size_t>(n.channel - 1)][static_cast<size_t>(n.note)];
      if (count > 0 && --count == 0) {
        emit(false, n.channel, n.note, 0);
      }
      n.used = false;
      --liveCount;
    }

    void add(int note, int velocity, double offBeat) {
      if (note < 0 || note > 127 || liveCount == notes.size()) {
        return;
      }
      for (auto &n : notes) {
        if (!n.used) {
          n = {offBeat, note, outputChannel, true};
          ++liveCount;
          if (owners[static_cast<size_t>(outputChannel - 1)][static_cast<size_t>(note)]++ == 0) {
            emit(true, outputChannel, note, std::clamp(velocity, 1, 127));
          }
          return;
        }
      }
    }

    void changeHarmony(const ChordSnapshot &chord, int channel, int velocity = 100) {
      if (config.transition != 2) {
        for (auto &n : notes) {
          if (n.used) {
            bool common = false;
            if (config.transition == 1 && n.channel == channel) {
              for (int i = 0; i < chord.count; ++i) {
                if ((chord.notes[static_cast<size_t>(i)] - n.note) % 12 == 0) {
                  common = true;
                }
              }
            }
            if (!common) {
              drop(n);
            }
          }
        }
      }
      harmony = chord;
      outputChannel = channel;
      triggerVelocity = std::clamp(velocity, 1, 127);
      pending = false;
    }

    static int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

    [[nodiscard]] int scaleVelocity(int eventVelocity) const {
      return std::clamp((std::clamp(eventVelocity, 1, 127) * triggerVelocity + 50) / 100, 1, 127);
    }

    void play(const PatternNote &e, double beat, double cycleEnd, double earliestOffBeat) {
      const auto &source = playback();
      const int delta = std::clamp(e.note, 0, 127) - source.sourceAnchor;
      const int octave = floorDiv(delta, 12), pc = delta - octave * 12;
      // A sub-sample duration must still produce a terminating note-off after its
      // note-on.
      const double off = std::max(std::min(beat + e.duration / speed(), cycleEnd), earliestOffBeat);
      const int velocity = scaleVelocity(e.velocity);
      const int sourceThird = source.sourceMinor ? 3 : 4;
      if (config.passing == 1 && pc != 0 && pc != sourceThird && pc != 7) {
        // Snap to the inferred source scale, then move within the destination
        // scale.
        constexpr std::array<int, 7> major{0, 2, 4, 5, 7, 9, 11};
        constexpr std::array<int, 7> minor{0, 2, 3, 5, 7, 8, 10};
        const auto &sourceScale = source.sourceMinor ? minor : major;
        int degree = 0;
        for (int i = 1; i < 7; ++i) {
          if (std::abs(sourceScale[static_cast<size_t>(i)] - pc) <
              std::abs(sourceScale[static_cast<size_t>(degree)] - pc)) {
            degree = i;
          }
        }
        const auto &scale =
            scales[static_cast<size_t>(std::clamp(harmony.scale, 0, scaleCount - 1))];
        const int start = std::clamp(harmony.degree, 0, scale.size() - 1),
                  destinationDegree = start + degree;
        add(harmony.root + octave * 12 +
                scale[static_cast<size_t>(destinationDegree % scale.size())] +
                12 * (destinationDegree / scale.size()) - scale[static_cast<size_t>(start)],
            velocity, off);
        return;
      }
      // Deterministic rank heuristic, not a reconstruction of proprietary voice
      // leading: Auto spreads three source ranks across the chord; All fills each
      // rank's bucket; Pattern preserves source rank, wrapping through the
      // destination chord.
      int rank = 0;
      if (std::abs(pc - sourceThird) < std::abs(pc)) {
        rank = 1;
      }
      if (std::abs(pc - 7) < std::abs(pc - (rank == 0 ? 0 : sourceThird))) {
        rank = 2;
      }
      const int count = harmony.count;
      int first = config.voices == 2 ? rank : rank * count / 3;
      int last = config.voices == 1 ? std::max(first + 1, (rank + 1) * count / 3) : first + 1;
      for (int i = first; i < last; ++i) {
        add(harmony.notes[static_cast<size_t>(i % count)] + 12 * (octave + i / count), velocity,
            off);
      }
    }
  };

} // namespace muthechords
