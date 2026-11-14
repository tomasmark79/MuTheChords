#pragma once

#include <MuTheChordsLib/MidiDiagnostics.hpp>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>

namespace muthechords {

  // Transparent observer: forwards the exact VST3 event unchanged and records
  // whether the host accepted it. It never creates or transforms pitch events.
  class Vst3MidiOutputMonitor final : public Steinberg::Vst::IEventList {
    Steinberg::Vst::IEventList *destination = nullptr;
    MidiDiagnostics *diagnostics = nullptr;

  public:
    void setDestination(Steinberg::Vst::IEventList &list, MidiDiagnostics *data) {
      destination = &list;
      diagnostics = data;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void **object) override {
      *object = nullptr;
      return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getEventCount() override {
      return destination ? destination->getEventCount() : 0;
    }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                           Steinberg::Vst::Event &event) override {
      return destination ? destination->getEvent(index, event) : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event &event) override {
      if (destination == nullptr) {
        return Steinberg::kResultFalse;
      }
      const bool bend = event.type == Steinberg::Vst::Event::kLegacyMIDICCOutEvent &&
                        event.midiCCOut.controlNumber == Steinberg::Vst::kPitchBend;
      const int packed = bend ? MidiDiagnostics::pack(event.midiCCOut.channel + 1,
                                                      (event.midiCCOut.value & 127) |
                                                          ((event.midiCCOut.value2 & 127) << 7))
                              : -1;
      const auto result = destination->addEvent(event);
      if (bend && diagnostics) {
        diagnostics->host.store(packed, std::memory_order_relaxed);
        diagnostics->hostResult.store(result, std::memory_order_relaxed);
        diagnostics->hostCount.fetch_add(1, std::memory_order_relaxed);
      }
      return result;
    }
  };

} // namespace muthechords
