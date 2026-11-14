#include "Vst3MidiOutputMonitor.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
static void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
struct Host final : IEventList {
  std::vector<Event> events;
  tresult response = kResultOk;
  tresult PLUGIN_API queryInterface(const TUID, void **object) override {
    *object = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events.size()); }
  tresult PLUGIN_API getEvent(int32 i, Event &e) override {
    if (i < 0 || static_cast<size_t>(i) >= events.size()) return kInvalidArgument;
    e = events[static_cast<size_t>(i)];
    return kResultOk;
  }
  tresult PLUGIN_API addEvent(Event &e) override {
    if (response == kResultOk) events.push_back(e);
    return response;
  }
};
int main() {
  try {
    muthechords::MidiDiagnostics data;
    muthechords::Vst3MidiOutputMonitor monitor;
    Host host;
    monitor.setDestination(host, &data);
    for (int channel = 0; channel < 16; ++channel)
      for (int value : {0, 8191, 8192, 12345, 16383}) {
        host.events.clear();
        Event e{};
        e.type = Event::kLegacyMIDICCOutEvent;
        e.sampleOffset = 37;
        e.flags = Event::kIsLive;
        e.ppqPosition = 12.5;
        e.midiCCOut.channel = static_cast<int8>(channel);
        e.midiCCOut.controlNumber = kPitchBend;
        e.midiCCOut.value = static_cast<int8>(value & 127);
        e.midiCCOut.value2 = static_cast<int8>(value >> 7);
        require(monitor.addEvent(e) == kResultOk && host.events.size() == 1,
                "one original pitch-bend event reaches the host");
        const auto &actual = host.events.front();
        require(actual.type == e.type && actual.busIndex == e.busIndex &&
                    actual.sampleOffset == 37 && actual.flags == e.flags &&
                    actual.ppqPosition == 12.5,
                "event metadata is untouched");
        require(
            actual.midiCCOut.channel == channel && actual.midiCCOut.controlNumber == kPitchBend &&
                actual.midiCCOut.value == (value & 127) && actual.midiCCOut.value2 == (value >> 7),
            "MIDI channel and both pitch bytes are untouched");
        require(data.host.load() == muthechords::MidiDiagnostics::pack(channel + 1, value) &&
                    data.hostResult.load() == kResultOk,
                "observer reports the actual channel, value and host result");
      }
    host.events.clear();
    Event note{};
    note.type = Event::kNoteOnEvent;
    note.noteOn.noteId = -1;
    note.noteOn.channel = 3;
    note.noteOn.pitch = 60;
    note.noteOn.tuning = 0;
    monitor.addEvent(note);
    require(host.events.size() == 1 && host.events[0].noteOn.noteId == -1 &&
                host.events[0].noteOn.tuning == 0,
            "monitor does not alter note IDs or tune notes");
    Event bend{};
    bend.type = Event::kLegacyMIDICCOutEvent;
    bend.midiCCOut.controlNumber = kPitchBend;
    bend.midiCCOut.value2 = 64;
    host.response = kResultFalse;
    require(monitor.addEvent(bend) == kResultFalse && data.hostResult.load() == kResultFalse,
            "host rejection is exposed rather than reported as delivery");
    std::cout << "VST3 MIDI observer preserves pitch bytes, all 16 channels, "
                 "timing and note IDs, and reports host rejection.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
