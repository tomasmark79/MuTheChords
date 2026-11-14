#include <iostream>
#include <juce_audio_processors/juce_audio_processors.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <stdexcept>

// Inspect the actual VST3 output, without JUCE's MIDI decoder on the receiving
// side.
struct RawEvents final : Steinberg::Vst::IEventList {
  std::vector<Steinberg::Vst::Event> events;
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void **object) override {
    *object = nullptr;
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::int32 PLUGIN_API getEventCount() override {
    return static_cast<Steinberg::int32>(events.size());
  }
  Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 i, Steinberg::Vst::Event &e) override {
    if (i < 0 || static_cast<size_t>(i) >= events.size()) return Steinberg::kInvalidArgument;
    e = events[static_cast<size_t>(i)];
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event &e) override {
    events.push_back(e);
    return Steinberg::kResultOk;
  }
};
void require(bool v, const char *message) {
  if (!v) throw std::runtime_error(message);
}
void checkRawMidiOutput(juce::AudioPluginInstance &plugin, juce::AudioPluginInstance &receiver) {
  using namespace Steinberg;
  struct Visitor final : juce::ExtensionsVisitor {
    Vst::IAudioProcessor *processor = nullptr;
    void visitVST3Client(const VST3Client &client) override {
      client.getIComponentPtr()->queryInterface(Vst::IAudioProcessor::iid,
                                                reinterpret_cast<void **>(&processor));
    }
  } source, destination;
  plugin.getExtensions(source);
  receiver.getExtensions(destination);
  require(source.processor && destination.processor, "raw VST3 processor interfaces");
  float left[512]{}, right[512]{};
  float *channels[]{left, right};
  Vst::AudioBusBuffers audio{};
  audio.numChannels = 2;
  audio.channelBuffers32 = channels;
  Vst::ProcessData data{};
  data.symbolicSampleSize = Vst::kSample32;
  data.numSamples = 512;
  data.numOutputs = 1;
  data.outputs = &audio;
  for (int channel : {0, 1, 15}) {
    RawEvents input, output, next;
    Vst::Event note{};
    note.type = Vst::Event::kNoteOnEvent;
    note.sampleOffset = 17;
    note.noteOn.channel = static_cast<int16>(channel);
    note.noteOn.pitch = 36;
    note.noteOn.velocity = .8f;
    note.noteOn.noteId = 10;
    input.events.push_back(note);
    Vst::Event bend{};
    bend.type = Vst::Event::kLegacyMIDICCOutEvent;
    bend.sampleOffset = 64;
    bend.midiCCOut.channel = static_cast<int8>(channel);
    bend.midiCCOut.controlNumber = 129;
    bend.midiCCOut.value = 57;
    bend.midiCCOut.value2 = 96;
    input.events.push_back(bend);
    Vst::Event mod = bend;
    mod.sampleOffset = 96;
    mod.midiCCOut.controlNumber = 1;
    mod.midiCCOut.value = 87;
    mod.midiCCOut.value2 = 0;
    input.events.push_back(mod);
    Vst::Event off{};
    off.type = Vst::Event::kNoteOffEvent;
    off.sampleOffset = 211;
    off.noteOff.channel = static_cast<int16>(channel);
    off.noteOff.pitch = 36;
    off.noteOff.noteId = 10;
    input.events.push_back(off);
    data.inputEvents = &input;
    data.outputEvents = &output;
    require(source.processor->process(data) == kResultOk, "first raw VST3 process");
    data.inputEvents = &output;
    data.outputEvents = &next;
    require(destination.processor->process(data) == kResultOk, "second raw VST3 process");
    for (const auto *list : {&output, &next}) {
      int bends = 0, mods = 0, ons = 0;
      for (const auto &e : list->events) {
        require(e.type != Vst::Event::kNoteExpressionValueEvent, "no per-note pitch conversion");
        if (e.type == Vst::Event::kNoteOnEvent) {
          ++ons;
          require(e.noteOn.channel == channel && e.noteOn.noteId == -1 && e.noteOn.tuning == 0,
                  "chord output retains its channel without per-note tuning");
        }
        if (e.type == Vst::Event::kLegacyMIDICCOutEvent && e.midiCCOut.controlNumber == 129) {
          ++bends;
          require(e.sampleOffset == 64 && e.midiCCOut.channel == channel &&
                      e.midiCCOut.value == 57 && e.midiCCOut.value2 == 96,
                  "original pitch bend reaches next VST3 with exact channel, "
                  "value and time");
        }
        if (e.type == Vst::Event::kLegacyMIDICCOutEvent && e.midiCCOut.controlNumber == 1) {
          ++mods;
          require(e.sampleOffset == 96 && e.midiCCOut.channel == channel && e.midiCCOut.value == 87,
                  "mod wheel retains channel, value and time");
        }
      }
      require(ons >= 3 && bends == 1 && mods == 1,
              "chord, pitch bend and mod wheel reach both VST3 outputs");
    }
  }
  source.processor->release();
  destination.processor->release();
}
struct TestPlayhead : juce::AudioPlayHead {
  double beat = 0, bpm = 120;
  bool playing = true;
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo p;
    p.setPpqPosition(beat);
    p.setBpm(bpm);
    p.setIsPlaying(playing);
    return p;
  }
};
int main(int argc, char **argv) {
  try {
    juce::ScopedJuceInitialiser_GUI initialise;
    require(argc == 2 || argc == 3, "Supply VST3 bundle path");
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(
        types, juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]).getFullPathName());
    require(types.size() == 1, "VST3 scan");
    juce::String error;
    auto plugin = format.createInstanceFromDescription(*types[0], 48000, 512, error);
    if (!plugin) throw std::runtime_error(error.toStdString());
    const std::array<const char *, 32> names{{"Key",
                                              "Scale",
                                              "Chord",
                                              "Inversion",
                                              "Transpose",
                                              "From",
                                              "To",
                                              "Full range",
                                              "Mapping",
                                              "Outside scale",
                                              "Pedal",
                                              "Alt Chord",
                                              "Latch",
                                              "Mode",
                                              "Pedal polarity",
                                              "Note spelling",
                                              "Pattern",
                                              "Rate",
                                              "Division",
                                              "Voices",
                                              "Passing notes",
                                              "Chord change",
                                              "Note transition",
                                              "Category",
                                              "Performance playback",
                                              "Harmony Sync",
                                              "Bypass",
                                              "DAW Sync",
                                              "Playback",
                                              "Loop start",
                                              "Loop end",
                                              "Voicing"}};
    for (auto *name : names) {
      juce::AudioProcessorParameter *found = nullptr;
      for (auto *parameter : plugin->getParameters())
        if (parameter->getName(128) == name) found = parameter;
      require(found != nullptr, "VST3 expected parameter exposed");
      require(found->isAutomatable(), "VST3 parameter automatable");
    }
    require(std::count_if(plugin->getParameters().begin(), plugin->getParameters().end(),
                          [](auto *p) { return p->getName(128) == "Bypass"; }) == 1,
            "VST3 has one shared host/plugin Bypass parameter");
    for (auto *parameter : plugin->getParameters())
      if (parameter->getName(128) == "Key") {
        const std::array<const char *, 12> pitches{
            {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
        require(parameter->getNumSteps() == 12, "VST3 Key steps");
        for (int i = 0; i < 12; ++i)
          require(parameter->getText(static_cast<float>(i) / 11, 32) ==
                      pitches[static_cast<size_t>(i)],
                  "VST3 Key pitch text");
      }
    for (auto *parameter : plugin->getParameters())
      if (parameter->getName(128) == "Scale") {
        require(parameter->getNumSteps() == 44, "VST3 exposes all scale choices");
        require(parameter->getText(1.0f, 128) == "Iwato", "VST3 exposes final new scale");
      }
    for (auto *parameter : plugin->getParameters())
      if (parameter->getName(128) == "Inversion")
        require(parameter->getText(1.0f, 32) == "6",
                "original inversion automation retains 0–6 range");
    plugin->setPlayConfigDetails(0, 2, 48000, 512);
    plugin->prepareToPlay(48000, 512);
    if (argc == 3) {
      auto receiver = format.createInstanceFromDescription(*types[0], 48000, 512, error);
      require(receiver != nullptr, "second VST3 instance");
      receiver->setPlayConfigDetails(0, 2, 48000, 512);
      receiver->prepareToPlay(48000, 512);
      checkRawMidiOutput(*plugin, *receiver);
      receiver->releaseResources();
      plugin->releaseResources();
      std::cout << "Two VST3 instances preserve raw pitch bend and modulation "
                   "on channels 1, 2 and 16.\n";
      return 0;
    }
    juce::AudioBuffer<float> audio(2, 512);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 211);
    plugin->processBlock(audio, midi);
    int on = 0, off = 0;
    for (auto item : midi) {
      const auto m = item.getMessage();
      if (m.isNoteOn()) {
        ++on;
        require(item.samplePosition == 17, "VST3 note-on sample");
      }
      if (m.isNoteOff()) {
        ++off;
        require(item.samplePosition == 211, "VST3 note-off sample");
      }
      if (m.isNoteOnOrOff())
        require(m.getNoteNumber() == 36 || m.getNoteNumber() == 40 || m.getNoteNumber() == 43,
                "VST3 chord notes");
    }
    require(on == 3 && off == 3, "VST3 triad output");
    auto setParameter = [&](const char *name, float value) {
      for (auto *p : plugin->getParameters())
        if (p->getName(128) == name) {
          p->setValueNotifyingHost(value);
          return;
        }
      throw std::runtime_error("VST3 parameter missing");
    };
    auto checkPitchBend = [&] {
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 1);
      for (int channel : {1, 2, 16})
        midi.addEvent(juce::MidiMessage::pitchWheel(channel, 12345), channel * 10);
      midi.addEvent(juce::MidiMessage::noteOff(1, 36), 200);
      plugin->processBlock(audio, midi);
      int count = 0;
      for (auto event : midi) {
        const auto message = event.getMessage();
        if (message.isPitchWheel()) {
          ++count;
          require(message.getPitchWheelValue() == 12345, "VST3 preserves pitch bend value");
          require(event.samplePosition == message.getChannel() * 10,
                  "VST3 preserves pitch bend channel and timing");
        }
      }
      require(count == 3, "VST3 forwards pitch bend on every MIDI channel");
    };
    checkPitchBend();
    setParameter("Bypass", 1);
    checkPitchBend();
    setParameter("Bypass", 0);
    {
      juce::AudioProcessorParameter *pattern = nullptr;
      for (auto *p : plugin->getParameters())
        if (p->getName(128) == "Pattern") pattern = p;
      require(pattern && pattern->getNumSteps() > 11, "pattern choices for project restore");
      const float wanted = 10.0f / static_cast<float>(pattern->getNumSteps() - 1);
      setParameter("Pattern", wanted);
      juce::MemoryBlock saved;
      plugin->getStateInformation(saved);
      auto reloaded = format.createInstanceFromDescription(*types[0], 48000, 512, error);
      require(reloaded != nullptr, "recreate VST3 for project restore");
      reloaded->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
      juce::AudioProcessorParameter *actual = nullptr;
      for (auto *p : reloaded->getParameters())
        if (p->getName(128) == "Pattern") actual = p;
      require(actual && std::abs(actual->getValue() - wanted) < .0001f,
              "VST3 project restores selected pattern");
      setParameter("Pattern", 0);
    }
    {
      juce::MemoryBlock baseline;
      plugin->getStateInformation(baseline);
      for (auto *parameter : plugin->getParameters()) {
        const auto name = parameter->getName(128);
        if (std::none_of(names.begin(), names.end(), [&](const char *own) { return name == own; }))
          continue;
        // Playback is transient; cross-instance Sync is covered by an isolated
        // processor bus so this host test cannot join a user's active group.
        if (name == "Performance playback" || name == "Harmony Sync") continue;
        parameter->setValueNotifyingHost(parameter->getValue() < .5f ? 1.0f : 0.0f);
        const float expected = parameter->getValue();
        juce::MemoryBlock state;
        plugin->getStateInformation(state);
        auto loaded = format.createInstanceFromDescription(*types[0], 48000, 512, error);
        require(loaded != nullptr, "recreate VST3 for full state audit");
        loaded->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        loaded->prepareToPlay(48000, 512);
        bool found = false;
        for (auto *restored : loaded->getParameters())
          if (restored->getName(128) == name) {
            require(std::abs(restored->getValue() - expected) < 1e-6f,
                    ("VST3 project restores " + name).toRawUTF8());
            found = true;
          }
        require(found, "restored VST3 parameter exists");
        loaded->releaseResources();
        plugin->setStateInformation(baseline.getData(), static_cast<int>(baseline.getSize()));
      }
    }
    setParameter("Pedal", 1.0f); // Chord Switch; default Reversed polarity.
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 211);
    plugin->processBlock(audio, midi);
    on = off = 0;
    bool seventh = false;
    for (auto item : midi) {
      const auto m = item.getMessage();
      if (m.isNoteOn()) {
        ++on;
        if (m.getNoteNumber() == 47) seventh = true;
      }
      if (m.isNoteOff()) ++off;
    }
    require(on == 4 && off == 4 && seventh, "VST3 reversed pedal selects Alt chord");
    setParameter("Transpose", 48.0f / 72.0f); // +12 semitones.
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(90)), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 211);
    plugin->processBlock(audio, midi);
    require(midi.getNumEvents() == 2, "VST3 input C3 outside default range");
    for (auto item : midi)
      require(item.getMessage().getNoteNumber() == 60, "VST3 solo bypasses transposition");
    // Exercise Performance through the actual VST3 wrapper with host transport.
    TestPlayhead playhead;
    plugin->setPlayHead(&playhead);
    setParameter("Transpose", .5f);
    setParameter("Pedal", 0);
    setParameter("Mode", 1);
    checkPitchBend();
    midi.clear();
    plugin->processBlock(audio, midi);
    playhead.beat += 512.0 / 24000;
    auto *patternParam = static_cast<juce::AudioProcessorParameter *>(nullptr);
    for (auto *parameter : plugin->getParameters())
      if (parameter->getName(128) == "Pattern") patternParam = parameter;
    require(patternParam && patternParam->getNumSteps() > 1000,
            "VST3 external pattern library discovered");
    setParameter("Latch", 1);
    playhead.beat = 1;
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(40)), 0);
    plugin->processBlock(audio, midi);
    bool performed = false;
    for (int block = 0; block < 500; ++block) {
      for (auto item : midi)
        if (item.getMessage().isNoteOn()) {
          const int pc = item.getMessage().getNoteNumber() % 12;
          require(pc == 2 || pc == 5 || pc == 9, "VST3 Performance uses diatonic D-F-A");
          performed = true;
        }
      playhead.beat += 512.0 / 24000;
      midi.clear();
      plugin->processBlock(audio, midi);
    }
    require(performed, "VST3 Performance emits pattern MIDI");
    playhead.playing = false;
    midi.clear();
    plugin->processBlock(audio, midi);
    for (int block = 0; block < 16; ++block) {
      midi.clear();
      plugin->processBlock(audio, midi);
      for (auto item : midi)
        require(!item.getMessage().isNoteOn(), "VST3 transport stop cancels patterns");
    }
    setParameter("Latch", 0);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(40)), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 38), 211);
    plugin->processBlock(audio, midi);
    bool stoppedPlayed = false, stoppedReleased = false;
    for (auto item : midi) {
      const auto m = item.getMessage();
      if (m.isNoteOn()) {
        stoppedPlayed = true;
        const int pc = m.getNoteNumber() % 12;
        require(pc == 2 || pc == 5 || pc == 9, "VST3 stopped-key harmony");
        require(item.samplePosition == 17, "VST3 stopped-key immediate start");
      }
      if (m.isNoteOff()) {
        stoppedReleased = true;
        require(item.samplePosition == 211, "VST3 stopped-key immediate release");
      }
    }
    require(stoppedPlayed && stoppedReleased,
            "VST3 held-key Performance works without transport or Preview");
    setParameter("Performance playback", 1);
    midi.clear();
    plugin->processBlock(audio, midi);
    bool remotePreview = false;
    for (auto item : midi)
      remotePreview |= item.getMessage().isNoteOn();
    require(remotePreview, "VST3 Remote Control playback starts audition");
    setParameter("Performance playback", 0);
    midi.clear();
    plugin->processBlock(audio, midi);
    for (int block = 0; block < 16; ++block) {
      midi.clear();
      plugin->processBlock(audio, midi);
      for (auto item : midi)
        require(!item.getMessage().isNoteOn(), "VST3 Remote Control playback stops future events");
    }
    // Clip grid positions round upward to integer samples at nonintegral
    // tempos.
    playhead.bpm = 110;
    playhead.playing = true;
    setParameter("Latch", 0);
    const double beatPerSample = 110.0 / (60 * 48000);
    for (const double drift : {0.0, .25, .999}) {
      playhead.beat = 8 + drift * beatPerSample - 17 * beatPerSample;
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(40)), 17);
      midi.addEvent(juce::MidiMessage::noteOff(1, 38), 211);
      plugin->processBlock(audio, midi);
      bool firstBeat = false;
      for (auto item : midi)
        if (item.getMessage().isNoteOn()) {
          firstBeat = true;
          require(item.samplePosition == 17, "VST3 rounded clip note starts at input sample");
        }
      require(firstBeat, "VST3 rounded on-grid clip note must not miss first beat");
    }
    playhead.beat = 8.5 - 17 * beatPerSample;
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(40)), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 38), 211);
    plugin->processBlock(audio, midi);
    for (auto item : midi)
      require(!item.getMessage().isNoteOn(),
              "VST3 genuinely offbeat input still waits for next beat");
    playhead.beat = 32 - 10 * beatPerSample;
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 12);
    midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(90)), 12);
    setParameter("Chord change", .5f);
    plugin->processBlock(audio, midi);
    bool restarted = false;
    for (auto item : midi)
      if (item.getMessage().isNoteOn() && item.samplePosition == 12) restarted = true;
    require(restarted, "VST3 Restart plays first attack on a same-sample note "
                       "transition after the beat");
    setParameter("DAW Sync", 0);
    playhead.beat = 40.5 - 17 * beatPerSample;
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 17);
    plugin->processBlock(audio, midi);
    bool unsynced = false;
    for (auto item : midi)
      if (item.getMessage().isNoteOn() && item.samplePosition == 17) unsynced = true;
    require(unsynced, "VST3 DAW Sync off starts an off-grid trigger immediately");
    plugin->releaseResources();
    plugin.reset();
    std::cout << "VST3 exposes all 32 automatable settings and passes chord timing, "
                 "transport sync and held-key Performance while stopped.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
