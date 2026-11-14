#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
  }
  void set(ChordInstrumentProcessor &p, const char *id, float value) {
    auto *param = p.parameters.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
  }
  struct PlayHead : juce::AudioPlayHead {
    double beat = 0, bpm = 120;
    bool playing = true, validPosition = true;
    juce::Optional<PositionInfo> getPosition() const override {
      PositionInfo p;
      p.setBpm(bpm);
      if (validPosition) p.setPpqPosition(beat);
      p.setIsPlaying(playing);
      return p;
    }
  };
  struct Rig {
    ChordInstrumentProcessor p;
    PlayHead host;
    juce::AudioBuffer<float> audio{2, 512};
    juce::MidiBuffer midi;
    explicit Rig(std::string channel = {}) : p(std::move(channel)) {
      p.setPlayHead(&host);
      p.prepareToPlay(48000, 512);
      set(p, "mode", 1);
      set(p, "full", 1);
    }
    void block() {
      p.processBlock(audio, midi);
      if (host.playing) host.beat += 512 * host.bpm / (60 * 48000);
    }
    void empty() {
      midi.clear();
      block();
    }
    void on(int n, int offset = 0) {
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, n, static_cast<juce::uint8>(40)), offset);
      block();
    }
    void off(int n, int offset = 0) {
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOff(1, n), offset);
      block();
    }
    int ons() {
      int count = 0;
      for (auto e : midi)
        if (e.getMessage().isNoteOn()) ++count;
      return count;
    }
  };
} // namespace
int main(int argc, char **argv) {
  try {
    juce::ScopedJuceInitialiser_GUI gui;
    Rig r;
    if (r.p.patternList().empty()) {
      std::cout << "No personal library: processor performance checks skipped "
                   "(run with CHORD_PATTERN_LIBRARY).\n";
      return 0;
    }
    // Choose a source event at beat zero with a normal short phrase.
    int index = -1;
    for (size_t i = 0; i < r.p.patternList().size(); ++i) {
      const auto &pattern = r.p.patternList()[i];
      if (pattern.length <= 4 &&
          std::any_of(pattern.events.begin(), pattern.events.end(),
                      [](const auto &e) { return e.beat == 0 && e.duration > 0; })) {
        index = static_cast<int>(i);
        break;
      }
    }
    require(index >= 0, "pattern starting on beat zero exists");
    set(r.p, "perfPattern", static_cast<float>(index));
    for (int keyCount : {1, 4}) {
      Rig t;
      t.host.playing = false;
      int passage = index;
      for (size_t i = 0; i < t.p.patternList().size(); ++i)
        if (t.p.patternList()[i].category == "Passages") {
          passage = static_cast<int>(i);
          break;
        }
      set(t.p, "perfPattern", static_cast<float>(passage));
      set(t.p, "pedal", 0);
      set(t.p, "perfChange", 2);
      t.p.setLoopRegion(0, 2);
      const std::array<int, 4> notes{36, 40, 43, 47};
      for (int i = 0; i < keyCount; ++i)
        t.midi.addEvent(juce::MidiMessage::noteOn(1, notes[static_cast<size_t>(i)],
                                                  static_cast<juce::uint8>(80)),
                        0);
      t.block();
      for (int i = 0; i < 10; ++i) {
        juce::Thread::sleep(20);
        juce::Timer::callPendingTimersSynchronously();
      }
      require(t.p.parameters.getRawParameterValue("perfPreview")->load() < 0.5f,
              "keyboard playback does not activate Preview parameter");
      set(t.p, "perfPreview", 0);
      t.empty();
      require(t.p.performanceRunning(), "host echo does not stop held keyboard performance");
      t.midi.clear();
      for (int i = 0; i < keyCount; ++i)
        t.midi.addEvent(juce::MidiMessage::noteOff(1, notes[static_cast<size_t>(i)]), 0);
      t.block();
      require(!t.p.performanceRunning() && !t.p.performancePreview(),
              "keyboard release stops without activating Preview");
      set(t.p, "perfPreview", 0);
      t.empty();
      require(!t.p.performanceRunning() && !t.p.performancePreview(),
              "late host echo after release must not start preview");
      t.p.requestPerformancePreview(true);
      t.empty();
      require(t.p.performancePreview(), "explicit Preview remains available");
      t.p.requestPerformancePreview(false);
      t.empty();
      require(!t.p.performanceRunning(), "explicit Preview still stops");
    }
    {
      // A multi-key chord released under sustain stops when the pedal rises,
      // for either supported polarity and for different note-off orders.
      for (int polarity : {0, 1})
        for (int reverse : {0, 1}) {
          Rig t;
          set(t.p, "perfPattern", static_cast<float>(index));
          set(t.p, "perfDawSync", 0);
          set(t.p, "pedal", 1);
          set(t.p, "polarity", static_cast<float>(polarity));
          const int down = polarity ? 0 : 127, up = polarity ? 127 : 0;
          t.midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, down), 0);
          for (int note : {36, 40, 43, 47})
            t.midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(90)), 10);
          t.block();
          require(t.p.performanceRunning(), "pedal chord starts pattern");
          t.midi.clear();
          const std::array<int, 4> notes =
              reverse ? std::array<int, 4>{47, 43, 40, 36} : std::array<int, 4>{36, 40, 43, 47};
          for (int note : notes)
            t.midi.addEvent(juce::MidiMessage::noteOff(1, note), 10);
          t.block();
          require(t.p.performanceRunning(), "Hold pedal retains pattern after releasing keys");
          t.midi.clear();
          t.midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, up), 12);
          t.block();
          require(!t.p.performanceRunning() && !t.p.hasSoundingOutput(),
                  "releasing Hold pedal ends multi-key pattern");
        }
    }
    {
      Rig t;
      set(t.p, "perfPattern", static_cast<float>(index));
      set(t.p, "perfDawSync", 0);
      set(t.p, "perfPlayback", 2);
      set(t.p, "perfChange", 0);
      set(t.p, "latch", 1);
      t.on(36);
      t.off(36);
      require(t.p.performanceRunning(), "continuous playback survives released key");
      const double phase = t.p.performancePhase();
      t.on(38);
      require(t.p.performanceRunning() && t.p.performancePhase() > phase,
              "continuous chord change preserves phase");
      t.off(38);
      t.on(38);
      require(t.p.performanceRunning(), "repeated input does not toggle continuous playback");
      t.p.requestStop();
      t.empty();
      require(!t.p.performanceRunning(), "Stop ends continuous playback");
    }
    {
      Rig t;
      set(t.p, "perfPattern", static_cast<float>(index));
      set(t.p, "perfDawSync", 0);
      set(t.p, "perfPlayback", 2);
      set(t.p, "perfChange", 0);
      for (int note : {36, 38, 40, 41}) {
        t.on(note);
        t.off(note);
        require(t.p.performanceRunning(),
                "continuous pattern runs through four separated input notes");
      }
      t.host.playing = false;
      t.empty();
      require(!t.p.performanceRunning(), "transport stop ends continuous playback");
    }
    {
      Rig t;
      set(t.p, "perfPattern", static_cast<float>(index));
      set(t.p, "perfDawSync", 0);
      set(t.p, "perfPlayback", 1);
      t.on(36);
      t.off(36);
      require(t.p.performanceRunning(), "one pass continues after release");
      for (int i = 0; i < 450; ++i)
        t.empty();
      require(!t.p.performanceRunning() && !t.p.hasSoundingOutput(),
              "one pass completes and releases notes");
    }
    {
      Rig t;
      set(t.p, "perfPattern", static_cast<float>(index));
      t.p.setLoopRegion(1, 4);
      int other = index == 0 ? 1 : 0;
      set(t.p, "perfPattern", static_cast<float>(other));
      t.p.setLoopRegion(2, 5);
      juce::MemoryBlock saved;
      t.p.getStateInformation(saved);
      ChordInstrumentProcessor loaded;
      loaded.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
      require(loaded.loopRegion() == std::make_pair(2, 5),
              "current loop region restores with project");
      set(loaded, "perfPattern", static_cast<float>(index));
      require(loaded.loopRegion() == std::make_pair(1, 4),
              "other pattern loop region restores with project");
      const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("chord-loop-profile", ".chordprofile");
      require(t.p.exportProfile(file).wasOk(), "export profile with loop regions");
      ChordInstrumentProcessor imported;
      require(imported.importProfile(file).wasOk(), "import profile with loop regions");
      require(imported.loopRegion() == std::make_pair(2, 5),
              "current loop region restores with profile");
      set(imported, "perfPattern", static_cast<float>(index));
      require(imported.loopRegion() == std::make_pair(1, 4),
              "other pattern loop region restores with profile");
      auto oldProfile = juce::JSON::parse(file.loadFileAsString());
      auto *oldValues = oldProfile["parameters"].getDynamicObject();
      for (const char *id : {"perfPlayback", "perfLoopStart", "perfLoopEnd", "voicingPreset"})
        oldValues->removeProperty(id);
      oldProfile.getDynamicObject()->removeProperty("loopRegions");
      require(file.replaceWithText(juce::JSON::toString(oldProfile)),
              "write previous profile format");
      require(imported.importProfile(file).wasOk() &&
                  imported.loopRegion() == std::make_pair(0, 0) &&
                  imported.parameters.getRawParameterValue("perfPlayback")->load() == 0,
              "old profiles restore with Held and Full defaults");
      file.deleteFile();
    }
    {
      // A bass phrase whose estimated centre differs from its first tone must
      // still start on the input root through the real processor and library.
      int candidate = -1;
      for (size_t i = 0; i < r.p.patternList().size(); ++i) {
        const auto &p = r.p.patternList()[i];
        if (p.category != "Bass" || p.events.empty()) continue;
        const auto e =
            std::min_element(p.events.begin(), p.events.end(),
                             [](const auto &a, const auto &b) { return a.beat < b.beat; });
        if (e->beat < .02 && (e->note - p.sourceRoot) % 12 != 0) {
          candidate = static_cast<int>(i);
          break;
        }
      }
      if (candidate >= 0) {
        Rig t;
        set(t.p, "root", 9);
        set(t.p, "perfPattern", static_cast<float>(candidate));
        set(t.p, "perfDawSync", 0);
        t.on(38);
        bool found = false;
        for (auto e : t.midi)
          if (e.getMessage().isNoteOn()) {
            require(e.getMessage().getNoteNumber() == 38, "bass phrase starts on D for D input");
            found = true;
            break;
          }
        require(found, "bass phrase plays its first note");
      }
    }
    {
      Rig t;
      set(t.p, "perfPattern", static_cast<float>(index));
      set(t.p, "perfChange", 1);
      set(t.p, "perfTransition", 2);
      t.host.beat = -74.0 / 24000;
      t.on(36);
      for (int i = 0; i < 46; ++i)
        t.empty();
      t.midi.clear();
      t.midi.addEvent(juce::MidiMessage::noteOff(1, 36), 12);
      t.midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(80)), 12);
      t.block();
      bool first = false;
      for (auto e : t.midi)
        if (e.getMessage().isNoteOn() && e.samplePosition == 12) first = true;
      require(first, "first pattern attack after near-grid chord change");
      t.off(38);
      set(t.p, "perfChange", 0);
      t.empty();
      t.host.beat = 2.5;
      t.on(36, 17);
      require(t.ons() == 0, "fresh off-grid trigger still waits for the next beat");
    }
    {
      // Chords -> lowest-note filter -> Performance must match a direct held
      // note.
      int mono = -1;
      for (size_t i = 0; i < r.p.patternList().size(); ++i) {
        const auto &pattern = r.p.patternList()[i];
        if (pattern.events.size() < 2 || pattern.events.front().beat != 0) continue;
        bool separate = true;
        for (size_t j = 1; j < pattern.events.size(); ++j)
          if (pattern.events[j].beat <= pattern.events[j - 1].beat) separate = false;
        if (separate) {
          mono = static_cast<int>(i);
          break;
        }
      }
      require(mono >= 0, "monophonic diagnostic pattern exists");
      for (bool synced : {false, true}) {
        const auto channel = juce::Uuid().toString().toStdString();
        Rig source(channel), chained(channel), direct(channel + "-direct");
        set(source.p, "mode", 0);
        set(chained.p, "perfPattern", static_cast<float>(mono));
        set(direct.p, "perfPattern", static_cast<float>(mono));
        if (synced) {
          set(source.p, "harmonySync", 1);
          set(chained.p, "harmonySync", 1);
          juce::Thread::sleep(65);
          juce::Timer::callPendingTimersSynchronously();
        }
        int played = 0, late = 0;
        for (int block = 0; block < 400; ++block) {
          source.midi.clear();
          if (block == 0)
            source.midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 0);
          if (block == 399) source.midi.addEvent(juce::MidiMessage::noteOff(1, 36), 127);
          source.block();
          if (block == 0)
            require(source.ons() == 3,
                    "upstream Chords generates a complete triad before filtering");
          chained.midi.clear();
          for (auto item : source.midi)
            if (item.getMessage().isNoteOnOrOff() && item.getMessage().getNoteNumber() == 36)
              chained.midi.addEvent(item.getMessage(), item.samplePosition);
          direct.midi.clear();
          if (block == 0)
            direct.midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 0);
          if (block == 399) direct.midi.addEvent(juce::MidiMessage::noteOff(1, 36), 127);
          chained.block();
          direct.block();
          auto expected = direct.midi.begin();
          int lastSample = -1;
          for (auto actual : chained.midi) {
            require(expected != direct.midi.end(), "chained Performance has no extra MIDI");
            const auto message = actual.getMessage(), reference = (*expected).getMessage();
            require(actual.samplePosition == (*expected).samplePosition &&
                        message.getRawDataSize() == reference.getRawDataSize() &&
                        std::equal(message.getRawData(),
                                   message.getRawData() + message.getRawDataSize(),
                                   reference.getRawData()),
                    "chained Performance matches standalone MIDI");
            ++expected;
            if (actual.getMessage().isNoteOn()) {
              require(lastSample != actual.samplePosition,
                      "monophonic Performance must not emit a whole chord per "
                      "event");
              lastSample = actual.samplePosition;
              ++played;
              if (block > 0) ++late;
            }
          }
          require(expected == direct.midi.end(), "chained Performance has no missing MIDI");
        }
        require(played > 1 && late > 0, "chained Performance advances beyond its first event");
        require(!chained.p.performanceRunning() && !chained.p.hasSoundingOutput(),
                "filtered trigger release ends chained Performance");
        std::cout << "Chords -> lowest note -> Performance, Sync " << (synced ? "on" : "off")
                  << ": " << played << " separate note-ons, identical to standalone.\n";
      }
    }
    {
      Rig unsynced;
      set(unsynced.p, "perfPattern", static_cast<float>(index));
      set(unsynced.p, "perfDawSync", 0);
      unsynced.host.beat = .5;
      unsynced.host.bpm = 110;
      unsynced.on(38, 17);
      bool first = false;
      for (auto e : unsynced.midi)
        if (e.getMessage().isNoteOn() && e.samplePosition == 17) first = true;
      require(first, "DAW Sync off starts an off-grid input immediately");
      double next = 1e9;
      for (const auto &e : unsynced.p.patternList()[static_cast<size_t>(index)].events)
        if (e.beat > 0) next = std::min(next, e.beat);
      if (next < .5) {
        int actual = -1;
        for (int i = 0; i < 35; ++i) {
          unsynced.empty();
          for (auto e : unsynced.midi)
            if (e.getMessage().isNoteOn() && actual < 0) actual = (i + 1) * 512 + e.samplePosition;
        }
        require(actual == 17 + static_cast<int>(std::ceil(next * 60 * 48000 / 110 - 1e-7)),
                "DAW Sync off retains host tempo");
      }
    }
    r.host.beat = .5;
    r.on(38, 17);
    require(r.ons() == 0, "initial trigger waits for next DAW beat");
    bool sounded = false;
    for (int i = 0; i < 30; ++i) {
      r.empty();
      for (auto e : r.midi) {
        auto m = e.getMessage();
        if (m.isNoteOn()) {
          sounded = true;
          require(m.getNoteNumber() % 12 == 2 || m.getNoteNumber() % 12 == 5 ||
                      m.getNoteNumber() % 12 == 9,
                  "D harmony stays D-F-A in C Major");
        }
      }
    }
    require(sounded, "quantized performance eventually sounds");
    r.off(38, 17);
    for (auto e : r.midi)
      if (e.getMessage().isNoteOff()) require(e.samplePosition == 17, "release is immediate");
    for (int i = 0; i < 50; ++i) {
      r.empty();
      require(r.ons() == 0, "release cancels future pattern events");
    }
    r.host.playing = false;
    r.host.bpm = 110;
    r.empty();
    r.on(38, 17);
    require(r.ons() > 0, "held key starts Performance immediately with stopped DAW");
    for (auto e : r.midi)
      if (e.getMessage().isNoteOn()) {
        require(e.samplePosition == 17, "stopped trigger preserves input sample offset");
        const int pc = e.getMessage().getNoteNumber() % 12;
        require(pc == 2 || pc == 5 || pc == 9, "stopped D trigger uses D-F-A");
      }
    require(!r.p.performancePreview(), "held-key playback does not enable Preview");
    int firstRepeat = -1;
    for (int i = 0; i < 35; ++i) {
      r.empty();
      for (auto e : r.midi)
        if (e.getMessage().isNoteOn() && firstRepeat < 0)
          firstRepeat = (i + 1) * 512 + e.samplePosition;
    }
    require(firstRepeat > 0, "held key keeps pattern advancing with frozen host PPQ");
    double nextBeat = 1e9;
    for (const auto &e : r.p.patternList()[static_cast<size_t>(index)].events)
      if (e.beat > 0) nextBeat = std::min(nextBeat, e.beat);
    require(firstRepeat == 17 + static_cast<int>(std::ceil(nextBeat * 60 * 48000 / 110 - 1e-7)),
            "stopped playback follows 110 BPM host tempo exactly");
    r.off(38, 29);
    int stoppedOffs = 0;
    for (auto e : r.midi)
      if (e.getMessage().isNoteOff()) {
        ++stoppedOffs;
        require(e.samplePosition == 29, "stopped-key release is immediate");
      }
    require(stoppedOffs > 0, "release terminates sounding stopped-transport note");
    for (int i = 0; i < 50; ++i) {
      r.empty();
      require(r.ons() == 0, "stopped-key release cancels pending events");
    }
    set(r.p, "latch", 1);
    r.on(38);
    r.off(38);
    bool stoppedLatch = false;
    for (int i = 0; i < 450; ++i) {
      r.empty();
      stoppedLatch |= r.ons() > 0;
    }
    require(stoppedLatch, "Latch also repeats with stopped transport");
    require(r.p.performanceRunning() && !r.p.performancePreview(),
            "latched loop is active without explicit Preview");
    r.on(38);
    require(!r.p.performanceRunning(), "same trigger toggles stopped Latch off");
    set(r.p, "latch", 0);
    r.empty();
    set(r.p, "root", 9);
    r.on(45);
    for (auto e : r.midi)
      if (e.getMessage().isNoteOn()) {
        const int pc = e.getMessage().getNoteNumber() % 12;
        require(pc == 9 || pc == 1 || pc == 4, "stopped playback follows A Major root");
      }
    r.off(45);
    set(r.p, "root", 0);
    r.empty();
    r.p.requestPerformancePreview(true);
    r.empty();
    require(r.p.performancePreview(), "explicit preview runs while stopped");
    bool previewSound = r.ons() > 0;
    for (int i = 0; i < 10; ++i) {
      r.empty();
      previewSound |= r.ons() > 0;
    }
    require(previewSound, "preview emits real MIDI at host tempo");
    r.on(38);
    r.off(38);
    require(r.p.performancePreview() && r.p.performanceRunning(),
            "clicked Preview keeps playing after MIDI key release");
    muthechords::ChordSnapshot previewChord;
    require(r.p.readChordSnapshot(previewChord) && previewChord.root == 38,
            "clicked Preview follows MIDI root changes");
    r.host.playing = true;
    r.empty();
    require(!r.p.performancePreview(), "DAW takes priority over preview");
    for (int i = 0; i < 10; ++i) {
      r.empty();
      require(r.ons() == 0, "DAW takeover does not auto-restart preview harmony");
    }
    set(r.p, "latch", 1);
    r.host.beat = std::ceil(r.host.beat);
    r.on(36);
    r.off(36);
    bool latched = false;
    for (int i = 0; i < 450; ++i) {
      r.empty();
      latched |= r.ons() > 0;
    }
    require(latched, "latch repeats after release");
    r.host.beat += 20;
    r.empty();
    for (int i = 0; i < 10; ++i) {
      r.empty();
      require(r.ons() == 0, "seek clears latched scheduler");
    }
    r.p.requestStop();
    r.empty();
    r.host.playing = false;
    r.empty();
    r.p.requestPerformancePreview(true);
    r.p.requestStop();
    r.empty();
    require(!r.p.performancePreview() && r.ons() == 0, "Stop cancels queued Preview");
    r.p.requestPerformancePreview(true);
    r.empty();
    require(r.p.performancePreview(), "preview restarted explicitly");
    r.host.playing = true;
    r.empty();
    r.host.beat = std::ceil(r.host.beat);
    r.on(36);
    r.host.validPosition = false;
    r.empty();
    for (int n = 0; n < 128; ++n)
      require(!r.p.outputNote(n), "lost PPQ releases all notes");
    r.host.validPosition = true;
    r.empty();
    r.host.beat = std::ceil(r.host.beat);
    r.on(36);
    r.midi.clear();
    r.p.processBlockBypassed(r.audio, r.midi);
    for (int n = 0; n < 128; ++n)
      require(!r.p.outputNote(n), "bypass clears output");
    set(r.p, "perfRate", 4);
    set(r.p, "perfDivision", 1);
    set(r.p, "perfTransition", 2);
    juce::MemoryBlock saved;
    r.p.getStateInformation(saved);
    ChordInstrumentProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(restored.parameters.getRawParameterValue("perfRate")->load() == 4 &&
                restored.parameters.getRawParameterValue("perfDivision")->load() == 1 &&
                restored.parameters.getRawParameterValue("perfTransition")->load() == 2,
            "performance controls saved in project");
    require(!restored.performancePreview(), "preview never restored as active");
    require(r.p.parameters.getParameter("spelling")->getParameterIndex() == 15,
            "existing parameter indexes unchanged");
    require(r.p.parameters.getParameter("perfPattern")->getParameterIndex() == 16,
            "performance parameters appended");
    r.host.playing = false;
    r.empty();
    {
      std::vector<std::string> categories;
      int otherCategory = -1;
      for (size_t i = 0; i < r.p.patternList().size(); ++i) {
        const auto &category = r.p.patternList()[i].category;
        if (std::find(categories.begin(), categories.end(), category) == categories.end())
          categories.push_back(category);
        if (categories.size() == 2 && otherCategory < 0) otherCategory = static_cast<int>(i);
      }
      set(r.p, "perfCategory", 1);
      require(r.p.selectedPatternIndex() == otherCategory,
              "host Category chooses its first pattern without an open editor");
      set(r.p, "perfPattern", static_cast<float>(index));
      require(r.p.selectedPatternIndex() == index,
              "host Pattern immediately overrides category selection");
      for (int i = 0; i < 30 && r.p.parameters.getRawParameterValue("perfPreview")->load() > .5f;
           ++i) {
        juce::Thread::sleep(20);
        juce::Timer::callPendingTimersSynchronously();
      }
      set(r.p, "perfPreview", 1);
      r.empty();
      require(r.p.performancePreview() && r.p.performanceRunning(),
              "host playback parameter starts audition");
      set(r.p, "perfPreview", 0);
      r.empty();
      require(!r.p.performanceRunning() && !r.p.hasSoundingOutput(),
              "host playback parameter stops all output");
      const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("chord-profile-runtime", ".chordprofile");
      struct Cleanup {
        juce::File file;
        ~Cleanup() { file.deleteFile(); }
      } cleanup{file};
      juce::MemoryBlock xmlState;
      r.p.getStateInformation(xmlState);
      r.p.setStateInformation(xmlState.getData(), static_cast<int>(xmlState.getSize()));
      r.empty();
      require(r.p.exportProfile(file).wasOk(),
              "external profile export succeeds after host XML restore");
      const auto profile = juce::JSON::parse(file.loadFileAsString());
      require(profile["patternId"].toString() ==
                  juce::String(r.p.patternList()[static_cast<size_t>(index)].id),
              "profile uses effective stable pattern ID");
      require(!profile["parameters"].hasProperty("perfPreview"),
              "profile excludes running playback");
      set(r.p, "root", 9);
      set(r.p, "transpose", 12);
      set(r.p, "perfRate", 0);
      set(r.p, "perfCategory", 1);
      r.on(45);
      require(r.p.importProfile(file).wasOk(), "external profile import succeeds");
      r.empty();
      require(r.p.parameters.getRawParameterValue("root")->load() == 0 &&
                  r.p.parameters.getRawParameterValue("transpose")->load() == 0 &&
                  r.p.parameters.getRawParameterValue("perfRate")->load() == 4,
              "external profile restores Harmony and Performance");
      require(r.p.selectedPatternIndex() == index && !r.p.performanceRunning() &&
                  !r.p.hasSoundingOutput(),
              "profile restores pattern and terminates previous output");
      const auto before = r.p.parameters.copyState().toXmlString();
      file.replaceWithText("{invalid");
      require(r.p.importProfile(file).failed(), "invalid external profile rejected");
      require(r.p.parameters.copyState().toXmlString() == before,
              "invalid import leaves state unchanged");
    }
    {
      std::unique_ptr<juce::AudioProcessorEditor> editor(r.p.createEditor());
      require(editor->isResizable() && editor->getHeight() <= 640,
              "compact resizable editor fits the reported display");
      std::vector<juce::Component *> components;
      const auto collect = [&](auto &&self, juce::Component &parent) -> void {
        for (auto *child : parent.getChildren()) {
          components.push_back(child);
          self(self, *child);
        }
      };
      collect(collect, *editor);
      juce::Component *graph = nullptr;
      for (auto *component : components) {
        if (component->getComponentID() == "performanceGraph") graph = component;
        if (auto *button = dynamic_cast<juce::TextButton *>(component))
          require(button->getButtonText() != "Preview", "no separate Preview button");
        if (auto *label = dynamic_cast<juce::Label *>(component))
          require(!label->getText().contains("BPM") && !label->getText().contains("DAW stopped"),
                  "no transport or tempo text");
      }
      require(graph != nullptr, "interactive pattern graph exists");
      juce::Button *previous = nullptr;
      juce::Button *next = nullptr;
      juce::Button *stop = nullptr;
      for (auto *component : components)
        if (auto *button = dynamic_cast<juce::Button *>(component)) {
          if (button->getTitle() == "Previous pattern") previous = button;
          if (button->getTitle() == "Next pattern") next = button;
          if (button->getButtonText() == "Stop") stop = button;
        }
      require(previous && next && stop && stop->isVisible(),
              "pattern arrows and Performance Stop are visible");
      const int originalPattern = r.p.selectedPatternIndex();
      next->onClick();
      require(r.p.selectedPatternIndex() == originalPattern + 1,
              "mouse next arrow changes pattern");
      previous->onClick();
      require(r.p.selectedPatternIndex() == originalPattern,
              "mouse previous arrow changes pattern");
      require(editor->getControlParameterIndex(*previous) ==
                  r.p.parameters.getParameter("perfPattern")->getParameterIndex(),
              "arrows identify Pattern for Remote Controls");
      require(editor->getControlParameterIndex(*graph) ==
                  r.p.parameters.getParameter("perfPreview")->getParameterIndex(),
              "graph identifies playback parameter");
      int longPattern = -1;
      for (size_t i = 0; i < r.p.patternList().size(); ++i) {
        const auto &p = r.p.patternList()[i];
        if (p.length > p.numerator * 4.0 / p.denominator) {
          longPattern = static_cast<int>(i);
          break;
        }
      }
      if (longPattern >= 0) {
        set(r.p, "perfPattern", static_cast<float>(longPattern));
        for (int attempt = 0; attempt < 15; ++attempt) {
          juce::Thread::sleep(20);
          juce::Timer::callPendingTimersSynchronously();
        }
        require(graph->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),
                "graph keyboard adjusts loop start");
        require(r.p.loopRegion().first == 1, "keyboard loop start changes one bar");
        require(graph->keyPressed(juce::KeyPress(juce::KeyPress::homeKey)),
                "graph Home resets loop");
        require(r.p.loopRegion() == std::make_pair(0, 0), "keyboard restores full pattern");
        set(r.p, "perfPattern", static_cast<float>(originalPattern));
        for (int attempt = 0; attempt < 15; ++attempt) {
          juce::Thread::sleep(20);
          juce::Timer::callPendingTimersSynchronously();
        }
      }
      auto accessibility = graph->createAccessibilityHandler();
      require(accessibility &&
                  accessibility->getActions().invoke(juce::AccessibilityActionType::press),
              "graph exposes Preview action");
      r.empty();
      require(r.p.performancePreview(), "graph action starts Preview");
      r.on(38);
      r.off(38);
      require(r.p.performancePreview() && r.p.performanceRunning(),
              "graph Preview follows keyboard without stopping on release");
      require(accessibility->getActions().invoke(juce::AccessibilityActionType::press),
              "graph exposes Stop action");
      r.empty();
      require(!r.p.performanceRunning(), "graph stops active Preview");
      require(graph->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)), "Space activates graph");
      r.empty();
      require(r.p.performancePreview(), "Space starts Preview");
      graph->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
      r.empty();
      require(r.p.performancePreview(), "key auto-repeat does not stop Preview");
      graph->keyStateChanged(false);
      graph->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
      r.empty();
      require(!r.p.performanceRunning(), "Enter stops Preview after key release");
      graph->keyStateChanged(false);
      r.on(38);
      r.off(38);
      require(r.p.performanceRunning() && !r.p.performancePreview(),
              "Latch starts loop independently of clicked Preview");
      accessibility->getActions().invoke(juce::AccessibilityActionType::press);
      r.empty();
      require(!r.p.performanceRunning(), "graph stops keyboard-latched playback");
      set(r.p, "full", 0);
      r.on(72);
      require(!r.p.performanceRunning() && r.p.hasSoundingOutput(),
              "solo pass-through can sound without pattern playback");
      accessibility->getActions().invoke(juce::AccessibilityActionType::press);
      r.empty();
      require(!r.p.hasSoundingOutput() && !r.p.performanceRunning(),
              "graph stops all plugin output including solo-only notes");
      r.off(72);
      set(r.p, "full", 1);
      r.empty();
      for (const auto size :
           std::array<std::pair<int, int>, 3>{{{760, 640}, {570, 480}, {900, 550}}}) {
        editor->setSize(size.first, size.second);
        for (const float dpi : {1.0f, 1.5f, 2.0f}) {
          editor->setScaleFactor(dpi);
          for (auto *component : components) {
            bool visible = true;
            for (auto *c = component; c && c != editor.get(); c = c->getParentComponent())
              visible &= c->isVisible();
            if (!visible || (!dynamic_cast<juce::ComboBox *>(component) &&
                             !dynamic_cast<juce::Button *>(component) && component != graph))
              continue;
            const auto bounds = editor->getLocalArea(component, component->getLocalBounds());
            require(editor->getLocalBounds().expanded(1).contains(bounds),
                    "scaled controls stay inside the editor");
          }
        }
      }
      if (argc > 1) {
        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        const auto capture = [&](const juce::File &path, int width, int height, float dpi) {
          editor->setSize(width, height);
          editor->setScaleFactor(dpi);
          const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, dpi);
          require(image.getWidth() == juce::roundToInt(width * dpi) &&
                      image.getHeight() == juce::roundToInt(height * dpi),
                  "DPI snapshot dimensions");
          juce::FileOutputStream file(path);
          require(file.openedOk(), "performance screenshot destination");
          juce::PNGImageFormat format;
          require(format.writeImageToStream(image, file), "performance editor screenshot");
        };
        capture(destination, 760, 640, 1);
        capture(destination.getSiblingFile(destination.getFileNameWithoutExtension() + "-150.png"),
                760, 640, 1.5f);
        capture(destination.getSiblingFile(destination.getFileNameWithoutExtension() +
                                           "-small-200.png"),
                570, 480, 2);
      }
    }
    {
      const auto channel = juce::Uuid().toString().toStdString();
      ChordInstrumentProcessor first(channel), second(channel), individual(channel);
      const auto tick = [] {
        juce::Thread::sleep(65);
        juce::Timer::callPendingTimersSynchronously();
      };
      set(first, "root", 9);
      set(first, "scale", 1);
      set(first, "chord", 3);
      set(first, "inversion", 2);
      set(first, "transpose", 12);
      set(first, "harmonySync", 1);
      tick();
      set(second, "root", 2);
      set(second, "alt", 5);
      set(second, "perfRate", 6);
      set(second, "latch", 1);
      set(second, "harmonySync", 1);
      tick();
      for (const auto *id : {"root", "scale"})
        require(first.parameters.getRawParameterValue(id)->load() ==
                    second.parameters.getRawParameterValue(id)->load(),
                "Sync adopts Key and Scale");
      require(second.parameters.getRawParameterValue("chord")->load() == 1 &&
                  second.parameters.getRawParameterValue("inversion")->load() == 0 &&
                  second.parameters.getRawParameterValue("transpose")->load() == 0,
              "joining does not adopt chord, inversion or transpose");
      require(individual.parameters.getRawParameterValue("root")->load() == 0,
              "unsynced instance remains independent");
      require(second.parameters.getRawParameterValue("alt")->load() == 5 &&
                  first.parameters.getRawParameterValue("alt")->load() == 2 &&
                  second.parameters.getRawParameterValue("perfRate")->load() == 6 &&
                  first.parameters.getRawParameterValue("perfRate")->load() == 3 &&
                  first.parameters.getRawParameterValue("latch")->load() == 0,
              "Sync does not share other settings");
      set(second, "root", 5);
      set(second, "scale", 4);
      tick();
      tick();
      require(first.parameters.getRawParameterValue("root")->load() == 5 &&
                  first.parameters.getRawParameterValue("scale")->load() == 4,
              "Harmony Sync publishes from either instance");
      set(second, "chord", 4);
      set(second, "inversion", 3);
      set(second, "transpose", -12);
      tick();
      tick();
      require(first.parameters.getRawParameterValue("chord")->load() == 3 &&
                  first.parameters.getRawParameterValue("inversion")->load() == 2 &&
                  first.parameters.getRawParameterValue("transpose")->load() == 12,
              "edits to chord, inversion and transpose remain independent");
      set(second, "harmonySync", 0);
      tick();
      set(second, "root", 2);
      set(first, "root", 7);
      tick();
      tick();
      require(second.parameters.getRawParameterValue("root")->load() == 2,
              "Sync off restores independent editing");
      set(second, "harmonySync", 1);
      tick();
      require(second.parameters.getRawParameterValue("root")->load() == 7,
              "rejoining adopts current shared harmony");
      juce::MemoryBlock syncedState;
      second.getStateInformation(syncedState);
      ChordInstrumentProcessor restoredSync(channel);
      restoredSync.setStateInformation(syncedState.getData(),
                                       static_cast<int>(syncedState.getSize()));
      tick();
      require(restoredSync.parameters.getRawParameterValue("harmonySync")->load() == 1 &&
                  restoredSync.parameters.getRawParameterValue("root")->load() == 7,
              "Sync flag persists and restored instance joins current group");
      set(first, "harmonySync", 0);
      set(second, "harmonySync", 0);
      set(restoredSync, "harmonySync", 0);
      tick();
    }
    r.p.releaseResources();
    std::cout << "Performance processor transport, timing, harmony, preview, "
                 "latch, seek and state checks passed.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
