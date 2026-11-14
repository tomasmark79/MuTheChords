#pragma once

#include <atomic>
#include <cstdint>

namespace muthechords {

  // A single atomic value keeps the MIDI channel and its 14-bit value together.
  struct MidiDiagnostics {
    static int pack(int channel, int value) { return (channel << 14) | value; }
    static int channel(int packed) { return packed < 0 ? -1 : packed >> 14; }
    static int value(int packed) { return packed < 0 ? -1 : packed & 16383; }
    std::atomic<int> input{-1}, output{-1}, host{-1}, noteChannel{-1}, hostResult{-1};
    std::atomic<uint32_t> inputCount{0}, outputCount{0}, hostCount{0};
  };

  struct MidiDiagnosticsSource {
    virtual ~MidiDiagnosticsSource() = default;
    virtual MidiDiagnostics &midiDiagnostics() = 0;
  };

} // namespace muthechords
