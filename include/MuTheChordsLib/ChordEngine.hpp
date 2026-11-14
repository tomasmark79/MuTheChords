#pragma once

#include <MuTheChordsLib/ChordSnapshot.hpp>
#include <MuTheChordsLib/Scales.hpp>
#include <MuTheChordsLib/Voicing.hpp>
#include <algorithm>
#include <array>

namespace muthechords {

  struct Settings {
    int root = 0, scale = 0, chord = 1, inversion = 0, transpose = 0;
    int low = 36, high = 59, mapping = 0, outside = 0, pedal = 0, alt = 2, mode = 0;
    bool full = false, latch = false, pedalReversed = false;
  };

  struct Event {
    bool on;
    int channel, note, velocity;
  };

  class Engine {
  public:
    using Callback = void (*)(void *, Event);

    void setOutput(void *context, Callback fn) {
      destination = context;
      callback = fn;
    }

    [[nodiscard]] const Settings &settings() const { return config; }
    [[nodiscard]] const ChordSnapshot &snapshot() const { return soundingChord; }

    void configure(Settings next) {
      next.root = std::clamp(next.root, 0, 11);
      next.scale = std::clamp(next.scale, 0, scaleCount - 1);
      next.chord = std::clamp(next.chord, 0, 5);
      next.alt = std::clamp(next.alt, 0, 5);
      next.inversion = std::clamp(next.inversion, 0, voicingCount - 1);
      next.transpose = std::clamp(next.transpose, -36, 36);
      next.low = std::clamp(next.low, 0, 127);
      next.high = std::clamp(next.high, next.low, 127);
      next.mapping = std::clamp(next.mapping, 0, 1);
      next.outside = std::clamp(next.outside, 0, 4);
      next.pedal = std::clamp(next.pedal, 0, 2);
      next.mode = std::clamp(next.mode, 0, 1);
      if (next.mode != config.mode) {
        reset();
      } else if (config.latch && !next.latch) {
        stopChord();
      }
      const bool polarityChanged = next.pedalReversed != config.pedalReversed;
      config = next;
      if (polarityChanged) {
        for (int c = 1; c <= 16; ++c) {
          if (pedalSeen[static_cast<size_t>(c - 1)]) {
            pedal(c, rawPedals[static_cast<size_t>(c - 1)]);
          }
        }
      }
    }

    [[nodiscard]] bool inRange(int n) const {
      return config.full || (n >= config.low && n <= config.high);
    }

    [[nodiscard]] bool inScale(int n) const {
      const auto &scale = scales[static_cast<size_t>(config.scale)];
      const int pc = ((n - config.root) % 12 + 12) % 12;
      return std::find(scale.begin(), scale.end(), pc) != scale.end();
    }

    [[nodiscard]] bool validInput(int n) const {
      if (!inRange(n) || config.mapping == 0) {
        return inScale(n);
      }
      return std::find(white.begin(), white.end(), n % 12) != white.end();
    }

    [[nodiscard]] bool isSounding(int n) const {
      for (const auto &channel : owners) {
        if (channel[static_cast<size_t>(n)] > 0) {
          return true;
        }
      }
      return false;
    }

    void noteOn(int channel, int n, int velocity) {
      if (!valid(channel, n) || config.mode != 0) {
        return;
      }
      if (velocity <= 0) {
        noteOff(channel, n);
        return;
      }
      if (!inRange(n)) {
        passOn(channel, n, velocity);
        return;
      }
      const int mapped = map(n);
      if (mapped < 0) {
        if (config.outside == 1) {
          stopChord();
          passOn(channel, n, velocity);
        }
        return;
      }
      if (config.latch && trigger == n && triggerChannel == channel) {
        stopChord();
        return;
      }
      stopChord();
      int degree = 0, base = mapped;
      const auto &scale = scales[static_cast<size_t>(config.scale)];
      if (config.mapping == 1) {
        const int inputDegree =
            static_cast<int>(std::find(white.begin(), white.end(), mapped % 12) - white.begin());
        degree = inputDegree % scale.size();
        base = (mapped / 12) * 12 + config.root + scale[static_cast<size_t>(degree)] +
               12 * (inputDegree / scale.size());
      } else {
        degree = static_cast<int>(
            std::find(scale.begin(), scale.end(), (mapped - config.root + 120) % 12) -
            scale.begin());
      }
      const int type = std::clamp(
          config.pedal == 2 && pedals[static_cast<size_t>(channel - 1)] ? config.alt : config.chord,
          0, 5);
      int count = type + 2;
      std::array<int, 8> notes{};
      for (int i = 0; i < count; ++i) {
        int d = degree + 2 * i;
        notes[static_cast<size_t>(i)] = base - scale[static_cast<size_t>(degree)] +
                                        scale[static_cast<size_t>(d % scale.size())] +
                                        12 * (d / scale.size());
      }
      if (config.inversion <= sixthInversion) {
        const int inversion = std::min(config.inversion, count - 1);
        for (int i = 0; i < inversion; ++i) {
          notes[static_cast<size_t>(i)] += 12;
        }
      } else {
        switch (config.inversion) {
        case bassDown: notes[0] -= 12; break;
        case topUp: notes[static_cast<size_t>(count - 1)] += 12; break;
        case openVoicing: notes[1] += 12; break;
        case wideVoicing:
          notes[0] -= 12;
          notes[1] += 12;
          break;
        case dropTwo: notes[static_cast<size_t>(count - 2)] -= 12; break;
        case bassOctave: notes[static_cast<size_t>(count++)] = notes[0] - 12; break;
        case octaveUp:
          for (int i = 0; i < count; ++i) {
            notes[static_cast<size_t>(i)] += 12;
          }
          break;
        case octaveDown:
          for (int i = 0; i < count; ++i) {
            notes[static_cast<size_t>(i)] -= 12;
          }
          break;
        default: break;
        }
      }
      std::sort(notes.begin(), notes.begin() + count);
      trigger = n;
      triggerChannel = channel;
      keyHeld = true;
      chordHold = config.pedal == 1;
      for (int i = 0; i < count; ++i) {
        const int out = notes[static_cast<size_t>(i)] + config.transpose;
        if (out < 0 || out > 127) {
          continue;
        }
        if (chordCount > 0 && chordNotes[static_cast<size_t>(chordCount - 1)] == out) {
          continue;
        }
        chordNotes[static_cast<size_t>(chordCount++)] = out;
        acquire(channel, out, velocity);
      }
      soundingChord = {};
      soundingChord.root = base + config.transpose;
      soundingChord.key = (config.root + config.transpose + 48) % 12;
      soundingChord.scale = config.scale;
      soundingChord.degree = degree;
      soundingChord.count = chordCount;
      soundingChord.intendedCount = count;
      soundingChord.notes = chordNotes;
    }

    void noteOff(int channel, int n) {
      if (!valid(channel, n)) {
        return;
      }
      auto &count = passed[static_cast<size_t>(channel - 1)][static_cast<size_t>(n)];
      if (count > 0) {
        --count;
        release(channel, n);
      }
      if (trigger == n && triggerChannel == channel) {
        keyHeld = false;
        if (!config.latch && !(chordHold && pedals[static_cast<size_t>(channel - 1)])) {
          stopChord();
        }
      }
    }

    void pedal(int channel, int value) {
      if (channel < 1 || channel > 16) {
        return;
      }
      const auto c = static_cast<size_t>(channel - 1);
      rawPedals[c] = value;
      pedalSeen[c] = true;
      const bool down = config.pedalReversed ? value < 64 : value >= 64;
      pedals[c] = down;
      if (!down && chordHold && !config.latch && !keyHeld && triggerChannel == channel) {
        stopChord();
      }
    }

    void resetControllers(int channel) {
      if (channel < 1 || channel > 16) {
        return;
      }
      const auto c = static_cast<size_t>(channel - 1);
      pedals[c] = false;
      pedalSeen[c] = false;
      if (chordHold && !config.latch && !keyHeld && triggerChannel == channel) {
        stopChord();
      }
    }

    void stopChord() {
      for (int i = 0; i < chordCount; ++i) {
        release(triggerChannel, chordNotes[static_cast<size_t>(i)]);
      }
      chordCount = 0;
      trigger = -1;
      triggerChannel = 1;
      keyHeld = false;
      chordHold = false;
      soundingChord = {};
    }

    void resetChannel(int channel) {
      if (channel < 1 || channel > 16) {
        return;
      }
      if (trigger >= 0 && triggerChannel == channel) {
        stopChord();
      }
      const auto c = static_cast<size_t>(channel - 1);
      for (int n = 0; n < 128; ++n) {
        auto &owner = owners[c][static_cast<size_t>(n)];
        if (owner > 0) {
          emit({false, channel, n, 0});
        }
        owner = 0;
      }
      passed[c] = {};
    }

    void reset(bool clearControllers = false) {
      for (int c = 1; c <= 16; ++c) {
        resetChannel(c);
      }
      if (clearControllers) {
        pedals = {};
        pedalSeen = {};
        rawPedals = {};
      }
    }

  private:
    inline static constexpr std::array<int, 7> white{0, 2, 4, 5, 7, 9, 11};
    Settings config;
    std::array<std::array<unsigned int, 128>, 16> owners{}, passed{};
    std::array<bool, 16> pedals{};
    std::array<bool, 16> pedalSeen{};
    std::array<int, 16> rawPedals{};
    std::array<int, 8> chordNotes{};
    ChordSnapshot soundingChord;
    int trigger = -1, triggerChannel = 1, chordCount = 0;
    bool keyHeld = false, chordHold = false;
    void *destination = nullptr;
    Callback callback = nullptr;

    static bool valid(int c, int n) { return c >= 1 && c <= 16 && n >= 0 && n < 128; }

    void emit(Event e) {
      if (callback) {
        callback(destination, e);
      }
    }

    void acquire(int c, int n, int v) {
      if (owners[static_cast<size_t>(c - 1)][static_cast<size_t>(n)]++ == 0) {
        emit({true, c, n, v});
      }
    }

    void release(int c, int n) {
      auto &owner = owners[static_cast<size_t>(c - 1)][static_cast<size_t>(n)];
      if (owner > 0 && --owner == 0) {
        emit({false, c, n, 0});
      }
    }

    void passOn(int c, int n, int v) {
      ++passed[static_cast<size_t>(c - 1)][static_cast<size_t>(n)];
      acquire(c, n, v);
    }

    [[nodiscard]] int map(int n) const {
      if (validInput(n)) {
        return n;
      }
      if (config.outside < 2) {
        return -1;
      }
      if (config.outside == 2) {
        for (int q = n - 1; q >= 0; --q) {
          if (validMapped(q)) {
            return q;
          }
        }
      }
      if (config.outside == 3) {
        for (int q = n + 1; q < 128; ++q) {
          if (validMapped(q)) {
            return q;
          }
        }
      }
      if (config.outside == 4) {
        for (int d = 1; d < 128; ++d) {
          if (n - d >= 0 && validMapped(n - d)) {
            return n - d;
          }
          if (n + d < 128 && validMapped(n + d)) {
            return n + d;
          }
        }
      }
      return -1;
    }

    [[nodiscard]] bool validMapped(int n) const {
      return config.mapping == 0 ? inScale(n)
                                 : std::find(white.begin(), white.end(), n % 12) != white.end();
    }
  };

} // namespace muthechords
