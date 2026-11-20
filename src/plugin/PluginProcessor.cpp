#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProfileState.h"
#include <MuTheChordsLib/NoteNames.hpp>
#include <cmath>
#include <limits>

using APVTS = juce::AudioProcessorValueTreeState;
namespace {
  constexpr const char *harmonyIds[]{"root", "scale"};
  std::vector<std::string> patternCategories(const std::vector<muthechords::Pattern> &patterns) {
    std::vector<std::string> result;
    for (const auto &pattern : patterns)
      if (std::find(result.begin(), result.end(), pattern.category) == result.end())
        result.push_back(pattern.category);
    if (result.empty())
      result = {"Bass",    "Melody",  "Passages",  "Performances",
                "Phrases", "Rhythms", "Sequences", "StrummedSequences"};
    return result;
  }
  void stateValue(juce::ValueTree &state, const char *id, int value) {
    auto child = state.getChildWithProperty("id", id);
    if (!child.isValid()) {
      child = juce::ValueTree("PARAM");
      child.setProperty("id", id, nullptr);
      state.addChild(child, -1, nullptr);
    }
    child.setProperty("value", value, nullptr);
  }
} // namespace
APVTS::ParameterLayout
    MuTheChordsProcessor::makeParameters(const std::vector<muthechords::Pattern> &patterns) {
  APVTS::ParameterLayout layout;
  auto integer = [&](const char *id, const char *name, int low, int high, int value) {
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{id, 1}, name, low, high,
                                                         value));
  };
  auto choice = [&](const char *id, const char *name, juce::StringArray values, int value) {
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, values,
                                                            value));
  };
  auto boolean = [&](const char *id, const char *name, bool value) {
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, value));
  };
  juce::StringArray rootNames;
  for (auto n : muthechords::pitchNames)
    rootNames.add(n);
  choice("root", "Key", rootNames, 0);
  juce::StringArray scaleNames;
  for (auto n : muthechords::scaleNames)
    scaleNames.add(n);
  choice("scale", "Scale", scaleNames, 0);
  const juce::StringArray types{"Third", "Triad", "7th", "9th", "11th", "13th"};
  choice("chord", "Chord", types, 1);
  integer("inversion", "Inversion", 0, 6, 0);
  integer("transpose", "Transpose", -36, 36, 0);
  integer("low", "From", 0, 127, 36);
  integer("high", "To", 0, 127, 59);
  boolean("full", "Full range", false);
  choice("mapping", "Mapping", {"Absolute", "Relative"}, 0);
  choice("outside", "Outside scale", {"Ignore", "Pass note", "Snap down", "Snap up", "Nearest"}, 0);
  choice("pedal", "Pedal", {"Off", "Hold", "Chord Switch"}, 0);
  choice("alt", "Alt Chord", types, 2);
  boolean("latch", "Latch", false);
  choice("mode", "Mode", {"Chords", "Performance"}, 0);
  choice("polarity", "Pedal polarity", {"Normal", "Reversed"}, 1);
  choice("spelling", "Note spelling", {"Auto", "Flats", "Sharps"}, 0);
  juce::StringArray patternNames;
  for (const auto &pattern : patterns)
    patternNames.add(juce::String(pattern.category) + " / " + juce::String(pattern.group) +
                     (pattern.group.empty() ? "" : " / ") + juce::String(pattern.name));
  if (patternNames.isEmpty()) patternNames.add("No patterns available");
  choice("perfPattern", "Pattern", patternNames, 0);
  choice("perfRate", "Rate", {"x0.25", "x0.5", "x0.75", "x1", "x1.25", "x1.5", "x1.75", "x2"}, 3);
  choice("perfDivision", "Division", {"Straight", "Triplet"}, 0);
  choice("perfVoices", "Voices", {"Auto", "All", "Pattern"}, 0);
  choice("perfPassing", "Passing notes", {"Off", "Scale"}, 0);
  choice("perfChange", "Chord change", {"Continue", "Restart", "Next beat"}, 0);
  choice("perfTransition", "Note transition", {"Cut", "Retain common", "Let ring"}, 0);
  juce::StringArray categoryNames;
  for (const auto &category : patternCategories(patterns))
    categoryNames.add(juce::String(category));
  choice("perfCategory", "Category", categoryNames, 0);
  boolean("perfPreview", "Performance playback", false);
  boolean("harmonySync", "Harmony Sync", false);
  boolean("bypass", "Bypass", false);
  boolean("perfDawSync", "DAW Sync", true);
  choice("perfPlayback", "Playback", {"Held", "One pass", "Continuous"}, 0);
  integer("perfLoopStart", "Loop start", 0, 800000, 0);
  integer("perfLoopEnd", "Loop end", 0, 800000, 0);
  juce::StringArray voicingModes{"Inversions"};
  for (int i = muthechords::bassDown; i < muthechords::voicingCount; ++i)
    voicingModes.add(muthechords::voicingNames[static_cast<size_t>(i)]);
  choice("voicingPreset", "Voicing", voicingModes, 0);
  choice("strum", "Strum", {"Off", "Up Fast", "Up Slow", "Down Fast", "Down Slow"}, 0);
  return layout;
}
MuTheChordsProcessor::MuTheChordsProcessor(std::string harmonyChannel)
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      categories(patternCategories(library.patterns())),
      parameters(*this, nullptr, "MuTheChordsState", makeParameters(library.patterns())),
      harmonyBus(std::move(harmonyChannel)) {
  loopRegions = std::make_unique<std::atomic<uint64_t>[]>(library.patterns().size());
  for (size_t i = 0; i < library.patterns().size(); ++i)
    loopRegions[i].store(0);
  for (auto &echo : harmonyEcho)
    echo.store(std::numeric_limits<int>::min());
  for (const auto *id : harmonyIds)
    parameters.getParameter(id)->addListener(this);
  for (const auto *id : {"perfCategory", "perfPattern", "perfPreview", "harmonySync",
                         "perfLoopStart", "perfLoopEnd"})
    parameters.getParameter(id)->addListener(this);
  startTimerHz(20);
}
MuTheChordsProcessor::~MuTheChordsProcessor() {
  stopTimer();
  harmonyBus.leave();
  for (const auto *id : harmonyIds)
    parameters.getParameter(id)->removeListener(this);
  for (const auto *id : {"perfCategory", "perfPattern", "perfPreview", "harmonySync",
                         "perfLoopStart", "perfLoopEnd"})
    parameters.getParameter(id)->removeListener(this);
}
int MuTheChordsProcessor::categoryForPattern(int index) const {
  if (index < 0 || index >= static_cast<int>(library.patterns().size())) return 0;
  const auto &category = library.patterns()[static_cast<size_t>(index)].category;
  const auto found = std::find(categories.begin(), categories.end(), category);
  return found == categories.end() ? 0 : static_cast<int>(found - categories.begin());
}
void MuTheChordsProcessor::parameterValueChanged(int index, float normalized) {
  if (index >= 0 && index < 2) {
    auto *parameter = parameters.getParameter(harmonyIds[index]);
    const int value = static_cast<int>(parameter->convertFrom0to1(normalized));
    if (!restoring.load() && value != harmonyEcho[static_cast<size_t>(index)].load())
      harmonyDirty.store(true);
    return;
  }
  for (const auto *id : {"perfCategory", "perfPattern", "perfPreview", "harmonySync",
                         "perfLoopStart", "perfLoopEnd"}) {
    auto *parameter = parameters.getParameter(id);
    if (parameter->getParameterIndex() == index) {
      parameterChanged(id, parameter->convertFrom0to1(normalized));
      return;
    }
  }
}
void MuTheChordsProcessor::parameterChanged(const juce::String &id, float value) {
  if (restoring.load()) return;
  const int choice = static_cast<int>(value);
  if (id == "harmonySync") {
    if (auto *manager = juce::MessageManager::getInstanceWithoutCreating())
      if (manager->isThisTheMessageThread()) updateHarmonySync();
    return;
  }
  // Listen at the parameter level so selecting an unchanged raw value can
  // override a dependent selection before its message-thread echo arrives.
  // Host callbacks only publish atomic requests. Dependent host notifications
  // happen on the message-thread timer, never recursively from an audio
  // callback.
  if (id == "perfPattern" && choice != echoPattern.load()) {
    if (choice >= 0 && choice < static_cast<int>(library.patterns().size()))
      selectedPattern.store(choice);
  } else if (id == "perfCategory" && choice != echoCategory.load()) {
    if (choice < 0 || choice >= static_cast<int>(categories.size())) return;
    if (categoryForPattern(selectedPattern.load()) == choice) return;
    for (size_t i = 0; i < library.patterns().size(); ++i)
      if (library.patterns()[i].category == categories[static_cast<size_t>(choice)]) {
        selectedPattern.store(static_cast<int>(i));
        break;
      }
  } else if (id == "perfLoopStart" || id == "perfLoopEnd") {
    if ((id == "perfLoopStart" && choice == echoLoopStart.load()) ||
        (id == "perfLoopEnd" && choice == echoLoopEnd.load()))
      return;
    const int selected = selectedPatternIndex();
    if (selected < 0) return;
    auto &region = loopRegions[static_cast<size_t>(selected)];
    auto old = region.load(), next = old;
    do {
      next = id == "perfLoopStart"
                 ? (old & 0xffffffff00000000ULL) | static_cast<uint32_t>(choice)
                 : (old & 0xffffffffULL) |
                       (static_cast<uint64_t>(static_cast<uint32_t>(choice)) << 32);
    } while (!region.compare_exchange_weak(old, next));
  } else if (id == "perfPreview" && choice != echoPlayback.load()) {
    // This parameter represents explicit Preview, not keyboard playback.
    // Hosts may echo our last published value; it is not a new command.
    if (reportedPlayback.exchange(choice) != choice)
      performancePreviewRequest.store(choice ? 1 : 0);
  }
}
void MuTheChordsProcessor::timerCallback() {
  writeMidiDiagnostics();
  if (restoring.load()) return;
  updateHarmonySync();
  const auto mirror = [&](const char *id, int value, std::atomic<int> &echo) {
    if (static_cast<int>(parameters.getRawParameterValue(id)->load()) == value) return;
    echo.store(value);
    auto *parameter = parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(value)));
    echo.store(-1);
  };
  const int selected = selectedPattern.load();
  mirror("perfPattern", selected, echoPattern);
  if (selectedPattern.load() == selected)
    mirror("perfCategory", categoryForPattern(selected), echoCategory);
  if (selectedPattern.load() == selected) {
    const auto region = loopRegion();
    mirror("perfLoopStart", region.first, echoLoopStart);
    mirror("perfLoopEnd", region.second, echoLoopEnd);
  }
  if (performancePreviewRequest.load() == -1 && !stateRestored.load()) {
    const int value =
        parameters.getRawParameterValue("mode")->load() > .5f && previewActive.load() ? 1 : 0;
    reportedPlayback.store(value);
    mirror("perfPreview", value, echoPlayback);
  }
}
void MuTheChordsProcessor::monitorOutput(const juce::MidiBuffer &midi) {
  for (const auto event : midi) {
    const auto message = event.getMessage();
    if (message.isNoteOn())
      diagnostics.noteChannel.store(message.getChannel(), std::memory_order_relaxed);
    if (message.isPitchWheel()) {
      diagnostics.output.store(
          muthechords::MidiDiagnostics::pack(message.getChannel(), message.getPitchWheelValue()),
          std::memory_order_relaxed);
      diagnostics.outputCount.fetch_add(1, std::memory_order_relaxed);
    }
  }
}
void MuTheChordsProcessor::writeMidiDiagnostics() {
  // Optional, private diagnostic capture. File I/O runs on the message thread.
  const auto enabled = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("MuTheChords-midi-trace.enabled");
  if (!enabled.existsAsFile()) return;
  const auto revision =
      diagnostics.inputCount.load() + diagnostics.outputCount.load() + diagnostics.hostCount.load();
  if (revision == diagnosticRevision) return;
  diagnosticRevision = revision;
  auto *object = new juce::DynamicObject;
  juce::var report(object);
  object->setProperty("version", JucePlugin_VersionString);
  object->setProperty("wrapper", getWrapperTypeDescription(wrapperType));
  const auto add = [&](const char *name, int packed) {
    auto *entry = new juce::DynamicObject;
    entry->setProperty("channel", muthechords::MidiDiagnostics::channel(packed));
    entry->setProperty("value", muthechords::MidiDiagnostics::value(packed));
    object->setProperty(name, juce::var(entry));
  };
  add("input", diagnostics.input.load());
  add("processorOutput", diagnostics.output.load());
  add("vst3Host", diagnostics.host.load());
  object->setProperty("noteChannel", diagnostics.noteChannel.load());
  object->setProperty("hostResult", diagnostics.hostResult.load());
  object->setProperty("inputCount", static_cast<juce::int64>(diagnostics.inputCount.load()));
  object->setProperty("outputCount", static_cast<juce::int64>(diagnostics.outputCount.load()));
  object->setProperty("hostCount", static_cast<juce::int64>(diagnostics.hostCount.load()));
  diagnosticFile.replaceWithText(juce::JSON::toString(report));
}
muthechords::HarmonyState MuTheChordsProcessor::currentHarmony() const {
  const auto value = [&](const char *id) {
    return static_cast<int>(parameters.getRawParameterValue(id)->load());
  };
  return {value("root"), value("scale")};
}
void MuTheChordsProcessor::applySharedHarmony(const muthechords::HarmonyState &state) {
  const int values[]{state.root, state.scale};
  for (size_t i = 0; i < 2; ++i) {
    auto *parameter = parameters.getParameter(harmonyIds[i]);
    if (static_cast<int>(parameter->convertFrom0to1(parameter->getValue())) == values[i]) continue;
    harmonyEcho[i].store(values[i]);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(values[i])));
    harmonyEcho[i].store(std::numeric_limits<int>::min());
  }
}
void MuTheChordsProcessor::updateHarmonySync() {
  if (parameters.getParameter("harmonySync")->getValue() < .5f) {
    harmonyBus.leave();
    harmonyDirty.store(false);
    rejoinHarmony.store(false);
    publishImportedHarmony.store(false);
    return;
  }
  if (rejoinHarmony.exchange(false)) harmonyBus.leave();
  if (!harmonyBus.joined()) {
    auto desired = currentHarmony(), state = desired;
    harmonyDirty.exchange(false);
    if (!harmonyBus.join(state)) return;
    if (publishImportedHarmony.exchange(false)) {
      if (!harmonyBus.publish(desired)) harmonyDirty.store(true);
    } else
      applySharedHarmony(state);
    return;
  }
  const bool edited = harmonyDirty.exchange(false),
             imported = publishImportedHarmony.exchange(false);
  if (edited || imported) {
    if (!harmonyBus.publish(currentHarmony())) harmonyDirty.store(true);
  } else {
    muthechords::HarmonyState state;
    if (harmonyBus.read(state) && !harmonyDirty.load()) applySharedHarmony(state);
  }
}
void MuTheChordsProcessor::requestPerformancePreview(bool enabled) {
  auto *parameter = parameters.getParameter("perfPreview");
  parameter->beginChangeGesture();
  parameter->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
  parameter->endChangeGesture();
  // The effective playback state can lead the mirrored host value by one timer
  // tick.
  performancePreviewRequest.store(enabled ? 1 : 0);
}
bool MuTheChordsProcessor::readDisplaySnapshot(DisplayFrame &frame) {
  DisplayUpdate update;
  const bool received = chordDisplay.read(update);
  const auto generation = displayResetGeneration.load();
  const bool reset = generation != lastReadDisplayResetGeneration;
  lastReadDisplayResetGeneration = generation;
  if (received && update.resetGeneration == generation) {
    frame = update.frame;
    return true;
  }
  if (reset) {
    frame = {};
    return true;
  }
  return false;
}
void MuTheChordsProcessor::resetToDefaults() {
  // This runs on the message thread. Leave the group before changing Key/Scale.
  restoring.store(true);
  harmonyBus.leave();
  for (auto *parameter : getParameters()) {
    const float target = parameter->getDefaultValue();
    if (parameter->getValue() != target) {
      parameter->beginChangeGesture();
      parameter->setValueNotifyingHost(target);
      parameter->endChangeGesture();
    }
  }
  selectedPattern.store(0);
  for (size_t i = 0; i < library.patterns().size(); ++i)
    loopRegions[i].store(0);
  harmonyDirty.store(false);
  rejoinHarmony.store(false);
  publishImportedHarmony.store(false);
  performancePreviewRequest.store(-1);
  previewRequest.store(-1);
  reportedPlayback.store(-1);
  lastPitchBend.store(-1);
  lastModWheel.store(-1);
  lastInput.store(-1);
  lastOutside.store(false);
  resetPerformanceInput.store(true);
  stateRestored.store(true);
  // Invalidate pending display updates without writing to the audio mailbox.
  displayResetGeneration.fetch_add(1);
  restoring.store(false);
  updateHostDisplay();
}
void MuTheChordsProcessor::prepareToPlay(double rate, int) {
  sampleRate = rate > 0 ? rate : 48000;
  output.ensureSize(262144);
  engine.setOutput(nullptr, nullptr);
  performance.setOutput(nullptr, nullptr);
  stopAll(true);
  previewHeld = -1;
  wasPlaying = false;
  hadHostPosition = false;
  userBypassed = false;
  bypassHeld = {};
  for (auto &n : sounding)
    n.store(false);
  lastInput.store(-1);
  chordDisplay.publish({{}, displayResetGeneration.load()});
}
void MuTheChordsProcessor::releaseResources() {
  engine.setOutput(nullptr, nullptr);
  performance.setOutput(nullptr, nullptr);
  stopAll(true);
  for (auto &n : sounding)
    n.store(false);
  lastInput.store(-1);
  chordDisplay.publish({{}, displayResetGeneration.load()});
}
bool MuTheChordsProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
  return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() ||
         layout.getMainOutputChannelSet().isDisabled();
}
muthechords::Settings MuTheChordsProcessor::readSettings() const {
  auto value = [&](const char *id) {
    return static_cast<int>(parameters.getRawParameterValue(id)->load());
  };
  muthechords::Settings s;
  s.root = value("root");
  s.scale = value("scale");
  s.chord = value("chord");
  const int preset = value("voicingPreset");
  s.inversion = preset ? muthechords::sixthInversion + preset : value("inversion");
  s.transpose = value("transpose");
  s.low = value("low");
  s.high = value("high");
  s.full = value("full") != 0;
  s.mapping = value("mapping");
  s.outside = value("outside");
  s.pedal = value("pedal");
  s.alt = value("alt");
  s.latch = value("latch") != 0;
  s.mode = value("mode");
  s.strum = value("strum");
  s.pedalReversed = value("polarity") != 0;
  return s;
}
muthechords::PerformanceSettings MuTheChordsProcessor::readPerformanceSettings() const {
  auto value = [&](const char *id) {
    return static_cast<int>(parameters.getRawParameterValue(id)->load());
  };
  constexpr double rates[]{.25, .5, .75, 1, 1.25, 1.5, 1.75, 2};
  muthechords::PerformanceSettings s;
  s.rate = rates[std::clamp(value("perfRate"), 0, 7)];
  s.division = value("perfDivision");
  s.voices = value("perfVoices");
  s.passing = value("perfPassing");
  s.change = value("perfChange");
  s.transition = value("perfTransition");
  s.playback = value("perfPlayback");
  const auto region = loopRegion();
  s.loopStart = region.first;
  s.loopEnd = region.second;
  return s;
}
std::pair<int, int> MuTheChordsProcessor::loopRegion() const {
  const int selected = selectedPatternIndex();
  if (selected < 0) return {0, 0};
  const auto region = loopRegions[static_cast<size_t>(selected)].load();
  return {static_cast<int>(region & 0xffffffffULL), static_cast<int>(region >> 32)};
}
void MuTheChordsProcessor::setLoopRegion(int start, int end) {
  start = juce::jlimit(0, 800000, start);
  end = juce::jlimit(0, 800000, end);
  auto set = [&](const char *id, int v) {
    auto *p = parameters.getParameter(id);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(v)));
    p->endChangeGesture();
  };
  set("perfLoopStart", start);
  set("perfLoopEnd", end);
}
void MuTheChordsProcessor::emit(muthechords::Event e, int offset) {
  if (e.channel < 1 || e.channel > 16 || e.note < 0 || e.note > 127) return;
  auto &count = outputOwners[static_cast<size_t>(e.channel - 1)][static_cast<size_t>(e.note)];
  if (e.on) {
    if (count++ > 0) return;
  } else {
    if (count == 0) return;
    if (--count > 0) return;
  }
  output.addEvent(
      e.on ? juce::MidiMessage::noteOn(e.channel, e.note,
                                       static_cast<juce::uint8>(std::clamp(e.velocity, 1, 127)))
           : juce::MidiMessage::noteOff(e.channel, e.note),
      offset);
}
void MuTheChordsProcessor::handleChordOutput(muthechords::Event e) {
  if (!e.on) {
    for (size_t i = 0; i < pendingStrumCount; ++i) {
      const auto &pending = pendingStrum[i].event;
      if (pending.channel == e.channel && pending.note == e.note) {
        pendingStrum[i] = pendingStrum[--pendingStrumCount];
        return;
      }
    }
  } else if (const int strum = engine.settings().strum; strum > 0 && e.voiceCount > 1) {
    const int order = strum >= 3 ? e.voiceCount - 1 - e.voiceIndex : e.voiceIndex;
    if (order > 0) {
      const double seconds = strum == 2 || strum == 4 ? 0.012 : 0.005;
      const auto stepSamples = std::max<int64_t>(1, std::llround(seconds * sampleRate));
      pendingStrum[pendingStrumCount++] = {e, eventOffset + order * stepSamples};
      return;
    }
  }
  emit(e, eventOffset);
}
void MuTheChordsProcessor::flushStrum(int endOffset) {
  size_t remaining = 0;
  for (size_t i = 0; i < pendingStrumCount; ++i) {
    const auto voice = pendingStrum[i];
    if (voice.offset < endOffset)
      emit(voice.event, static_cast<int>(voice.offset));
    else
      pendingStrum[remaining++] = voice;
  }
  pendingStrumCount = remaining;
}
void MuTheChordsProcessor::stopAll(bool clearControllers) {
  engine.reset(clearControllers);
  pendingStrumCount = 0;
  performance.stop();
  for (int c = 0; c < 16; ++c)
    for (int n = 0; n < 128; ++n) {
      auto &owner = outputOwners[static_cast<size_t>(c)][static_cast<size_t>(n)];
      if (owner) output.addEvent(juce::MidiMessage::noteOff(c + 1, n), eventOffset);
      owner = 0;
    }
  if (clearControllers) {
    previewRequest.store(-1);
    performancePreviewRequest.store(-1);
  }
  passed = {};
  triggerNote = -1;
  performanceKeyHeld = false;
  previewActive.store(false);
  running.store(false);
  anyOutput.store(false);
  phase.store(0);
  if (clearControllers) {
    rawPedals = {};
    pedalSeen = {};
  }
}
bool MuTheChordsProcessor::pedalDown(int channel, const muthechords::Settings &s) const {
  const auto c = static_cast<size_t>(channel - 1);
  return pedalSeen[c] && (s.pedalReversed ? rawPedals[c] < 64 : rawPedals[c] >= 64);
}
muthechords::ChordSnapshot MuTheChordsProcessor::buildHarmony(int note, int channel,
                                                              const muthechords::Settings &settings,
                                                              bool preview) {
  auto s = settings;
  s.mode = 0;
  s.latch = false;
  s.pedal = 0;
  if (preview) s.full = true;
  if (settings.pedal == 2 && pedalDown(channel, settings)) s.chord = settings.alt;
  harmonyBuilder.reset(true);
  harmonyBuilder.configure(s);
  harmonyBuilder.noteOn(channel, note, 100);
  return harmonyBuilder.snapshot();
}
void MuTheChordsProcessor::handleNoteOn(int channel, int note, int velocity, double beat,
                                        const muthechords::Settings &s) {
  if (s.mode == 0) {
    engine.noteOn(channel, note, velocity);
    return;
  }
  if (!engine.inRange(note) || (!engine.validInput(note) && s.outside == 1)) {
    if (engine.inRange(note)) {
      performance.stop();
      triggerNote = -1;
    }
    ++passed[static_cast<size_t>(channel - 1)][static_cast<size_t>(note)];
    emit({true, channel, note, velocity}, eventOffset);
    return;
  }
  auto harmony = buildHarmony(note, channel, s);
  if (harmony.count == 0) return;
  lastPerformanceInput = note;
  if (!previewActive.load() && s.latch && readPerformanceSettings().playback == 0 &&
      triggerNote == note && triggerChannel == channel) {
    performance.stop();
    triggerNote = -1;
    performanceKeyHeld = false;
    return;
  }
  triggerNote = note;
  triggerChannel = channel;
  performanceKeyHeld = true;
  const bool playing = hostPlaying.load();
  if ((playing && hadHostPosition) || (!playing && knownTempo.load() > 0))
    performance.trigger(harmony, channel, beat,
                        playing && parameters.getRawParameterValue("perfDawSync")->load() > .5f &&
                            releasedForRestart != eventOffset,
                        knownTempo.load() / (60 * sampleRate), velocity);
}
void MuTheChordsProcessor::handleNoteOff(int channel, int note, const muthechords::Settings &s) {
  if (s.mode == 0) {
    engine.noteOff(channel, note);
    return;
  }
  auto &count = passed[static_cast<size_t>(channel - 1)][static_cast<size_t>(note)];
  if (count) {
    --count;
    emit({false, channel, note, 0}, eventOffset);
  }
  if (triggerNote == note && triggerChannel == channel) {
    performanceKeyHeld = false;
    if (!previewActive.load() && readPerformanceSettings().playback == 0 && !s.latch &&
        !(s.pedal == 1 && pedalDown(channel, s))) {
      if (performance.isActive() && readPerformanceSettings().change == 1)
        releasedForRestart = eventOffset;
      performance.release();
      triggerNote = -1;
    }
  }
}
void MuTheChordsProcessor::processBlock(juce::AudioBuffer<float> &audio, juce::MidiBuffer &midi) {
  if (parameters.getRawParameterValue("bypass")->load() > .5f) {
    processBlockBypassed(audio, midi);
    return;
  }
  const auto displayGeneration = displayResetGeneration.load();
  juce::ScopedNoDenormals noDenormals;
  audio.clear();
  output.clear();
  eventOffset = 0;
  releasedForRestart = -1;
  if (userBypassed) {
    for (int c = 0; c < 16; ++c)
      for (int n = 0; n < 128; ++n)
        if (bypassHeld[static_cast<size_t>(c)][static_cast<size_t>(n)])
          output.addEvent(juce::MidiMessage::noteOff(c + 1, n), 0);
    bypassHeld = {};
    userBypassed = false;
  }
  engine.setOutput(this, [](void *raw, muthechords::Event e) {
    static_cast<MuTheChordsProcessor *>(raw)->handleChordOutput(e);
  });
  performance.setOutput(this, [](void *raw, muthechords::Event e, int sample) {
    static_cast<MuTheChordsProcessor *>(raw)->emit(e, sample);
  });
  bool playing = false, positionValid = false;
  double hostBeat = 0;
  if (auto *playhead = getPlayHead())
    if (auto position = playhead->getPosition()) {
      playing = position->getIsPlaying();
      if (auto bpm = position->getBpm())
        if (std::isfinite(*bpm) && *bpm > 0) knownTempo.store(*bpm);
      if (auto ppq = position->getPpqPosition())
        if (std::isfinite(*ppq)) {
          hostBeat = *ppq;
          positionValid = true;
        }
    }
  hostPlaying.store(playing);
  const double bpm = knownTempo.load(), beatsPerSample = bpm > 0 ? bpm / (60 * sampleRate) : 0;
  const auto settings = readSettings();
  const auto oldSettings = engine.settings();
  const bool changedMode = oldSettings.mode != settings.mode;
  const bool takeover = playing && !wasPlaying;
  const bool discontinuity =
      settings.mode == 1 && playing && wasPlaying && positionValid && hadHostPosition &&
      std::abs(hostBeat - expectedHostBeat) > std::max(1e-5, beatsPerSample * 2);
  const bool rebaseHeldPerformance = discontinuity && performanceKeyHeld;
  const bool stopOnDiscontinuity = discontinuity && !rebaseHeldPerformance;
  const bool transportStop = wasPlaying && !playing;
  const bool positionLost = playing && !positionValid && settings.mode == 1;
  const double startBeat = playing && positionValid ? hostBeat : stoppedBeat;
  if (rebaseHeldPerformance) performance.rebase(hostBeat - expectedHostBeat);
  performance.advance(startBeat, beatsPerSample, 0, 0);
  const bool restored = stateRestored.exchange(false);
  if (resetPerformanceInput.exchange(false)) lastPerformanceInput = -1;
  const bool requestedStop = stopRequested.exchange(false);
  if (restored || changedMode || transportStop || takeover || stopOnDiscontinuity || positionLost ||
      requestedStop) {
    stopAll(restored);
    previewHeld = -1;
    lastInput.store(-1);
    lastOutside.store(false);
    // Explicit stop/restoration/transport discontinuities also cancel queued UI
    // starts.
    if (restored || transportStop || takeover || stopOnDiscontinuity || positionLost ||
        requestedStop) {
      previewRequest.store(-1);
      performancePreviewRequest.store(-1);
    }
  }
  hadHostPosition = playing && positionValid;
  engine.configure(settings);
  const auto &patterns = library.patterns();
  const int selected = selectedPatternIndex();
  const bool wasActive = performance.isActive();
  const auto previousHarmony = performance.snapshot();
  const int previousChannel = performance.currentChannel();
  const int previousVelocity = performance.currentVelocity();
  performance.configure(selected >= 0 && selected < static_cast<int>(patterns.size())
                            ? &patterns[static_cast<size_t>(selected)]
                            : nullptr,
                        readPerformanceSettings());
  if (wasActive && !performance.isActive() && settings.mode == 1)
    performance.trigger(previousHarmony, previousChannel, startBeat, false, 0, previousVelocity);
  if (settings.mode == 1 && readPerformanceSettings().playback == 0 && !previewActive.load() &&
      ((oldSettings.latch && !settings.latch) ||
       (!performanceKeyHeld && !settings.latch && settings.pedal != 1 && oldSettings.pedal == 1))) {
    performance.stop();
    triggerNote = -1;
  }
  if (settings.mode == 1 && readPerformanceSettings().playback == 0 && !previewActive.load() &&
      oldSettings.pedalReversed != settings.pedalReversed && !performanceKeyHeld &&
      !settings.latch && settings.pedal == 1 && triggerNote >= 0 &&
      !pedalDown(triggerChannel, settings)) {
    performance.stop();
    triggerNote = -1;
  }
  const int previewAction = performancePreviewRequest.exchange(-1);
  if (previewAction == 0) {
    stopAll();
    previewHeld = -1;
    lastInput.store(-1);
    previewRequest.store(-1);
  } else if (previewAction == 1 && !playing && settings.mode == 1 && bpm > 0 && !patterns.empty()) {
    performance.stop();
    previewActive.store(false);
    triggerNote = -1;
    const int note = lastPerformanceInput >= 0 ? lastPerformanceInput
                                               : (settings.mapping == 1 ? 36 : 36 + settings.root);
    const auto harmony = buildHarmony(note, 1, settings, true);
    if (harmony.count > 0) {
      previewActive.store(true);
      performance.trigger(harmony, 1, startBeat, false);
    }
  }
  const int preview = previewRequest.exchange(-1);
  if (preview != -1) {
    if (previewHeld >= 0) handleNoteOff(1, previewHeld, settings);
    previewHeld = preview >= 0 ? preview : -1;
    if (preview >= 0) {
      handleNoteOn(1, preview, 100, startBeat, settings);
      lastInput.store(preview);
      lastOutside.store(!engine.validInput(preview));
    } else
      lastInput.store(-1);
  }
  int cursor = 0;
  for (const auto metadata : midi) {
    eventOffset = std::clamp(metadata.samplePosition, 0, std::max(0, audio.getNumSamples() - 1));
    // Input at the same sample can cancel an attack that has not sounded yet.
    flushStrum(eventOffset);
    if (settings.mode == 1 && beatsPerSample > 0 && (!playing || positionValid))
      performance.advance(startBeat, beatsPerSample, cursor, eventOffset);
    cursor = eventOffset;
    const auto message = metadata.getMessage();
    monitorController(message);
    const int channel = message.getChannel();
    const double beat = startBeat + eventOffset * beatsPerSample;
    if (message.isNoteOn()) {
      handleNoteOn(channel, message.getNoteNumber(), message.getVelocity(), beat, settings);
      lastInput.store(message.getNoteNumber());
      lastOutside.store(!engine.validInput(message.getNoteNumber()));
    } else if (message.isNoteOff()) {
      handleNoteOff(channel, message.getNoteNumber(), settings);
      if (lastInput.load() == message.getNoteNumber()) lastInput.store(-1);
    } else if (message.isController() && message.getControllerNumber() == 64) {
      rawPedals[static_cast<size_t>(channel - 1)] = message.getControllerValue();
      pedalSeen[static_cast<size_t>(channel - 1)] = true;
      engine.pedal(channel, message.getControllerValue());
      if (settings.mode == 1 && readPerformanceSettings().playback == 0 && !previewActive.load() &&
          settings.pedal == 1 && !performanceKeyHeld && !settings.latch &&
          triggerChannel == channel && !pedalDown(channel, settings)) {
        performance.release();
        triggerNote = -1;
      }
    } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
      engine.resetChannel(channel);
      if (settings.mode == 1) {
        performance.resetChannel(channel);
        if (triggerChannel == channel) {
          triggerNote = -1;
          previewActive.store(false);
        }
      }
      for (int n = 0; n < 128; ++n) {
        auto &count = passed[static_cast<size_t>(channel - 1)][static_cast<size_t>(n)];
        while (count) {
          --count;
          emit({false, channel, n, 0}, eventOffset);
        }
      }
      lastInput.store(-1);
      output.addEvent(message, eventOffset);
    } else if (message.isController() && message.getControllerNumber() == 121) {
      engine.resetControllers(channel);
      pedalSeen[static_cast<size_t>(channel - 1)] = false;
      if (settings.mode == 1 && readPerformanceSettings().playback == 0 && !previewActive.load() &&
          settings.pedal == 1 && !performanceKeyHeld && !settings.latch &&
          triggerChannel == channel) {
        performance.release();
        triggerNote = -1;
      }
      output.addEvent(message, eventOffset);
    } else
      output.addEvent(message, eventOffset);
  }
  flushStrum(audio.getNumSamples());
  for (size_t i = 0; i < pendingStrumCount; ++i)
    pendingStrum[i].offset -= audio.getNumSamples();
  if (settings.mode == 1 && beatsPerSample > 0 && (!playing || positionValid))
    performance.advance(startBeat, beatsPerSample, cursor, audio.getNumSamples());
  if (settings.mode == 1 && readPerformanceSettings().playback == 1 && !performance.isActive())
    previewActive.store(false);
  phase.store(performance.phase());
  running.store(performance.isActive());
  muthechords::ChordSnapshot snapshot =
      settings.mode == 0 ? engine.snapshot() : performance.snapshot();
  DisplayFrame display;
  display.chord = snapshot;
  bool any = false;
  for (int n = 0; n < 128; ++n) {
    bool on = false;
    for (int c = 0; c < 16; ++c)
      on |= outputOwners[static_cast<size_t>(c)][static_cast<size_t>(n)] > 0;
    any |= on;
    display.notes[static_cast<size_t>(n)] = on;
    sounding[static_cast<size_t>(n)].store(on, std::memory_order_relaxed);
  }
  anyOutput.store(any);
  chordDisplay.publish({display, displayGeneration});
  engine.setOutput(nullptr, nullptr);
  performance.setOutput(nullptr, nullptr);
  midi.clear();
  midi.addEvents(output, 0, -1, 0);
  monitorOutput(midi);
  wasPlaying = playing;
  hadHostPosition = playing && positionValid;
  expectedHostBeat = hostBeat + audio.getNumSamples() * beatsPerSample;
  if (!playing) stoppedBeat += audio.getNumSamples() * beatsPerSample;
}
void MuTheChordsProcessor::processBlockBypassed(juce::AudioBuffer<float> &audio,
                                                juce::MidiBuffer &midi) {
  if (!userBypassed) {
    userBypassed = true;
    bypassHeld = {};
  }
  for (const auto event : midi) {
    const auto m = event.getMessage();
    monitorController(m);
    const int c = m.getChannel() - 1;
    if (c < 0 || c >= 16) continue;
    if (m.isNoteOn())
      ++bypassHeld[static_cast<size_t>(c)][static_cast<size_t>(m.getNoteNumber())];
    else if (m.isNoteOff()) {
      auto &count = bypassHeld[static_cast<size_t>(c)][static_cast<size_t>(m.getNoteNumber())];
      if (count) --count;
    } else if (m.isAllNotesOff() || m.isAllSoundOff())
      bypassHeld[static_cast<size_t>(c)] = {};
  }
  audio.clear();
  output.clear();
  eventOffset = 0;
  stopAll();
  previewRequest.store(-1);
  performancePreviewRequest.store(-1);
  previewHeld = -1;
  wasPlaying = false;
  hadHostPosition = false;
  lastInput.store(-1);
  for (auto &n : sounding)
    n.store(false);
  chordDisplay.publish({{}, displayResetGeneration.load()});
  output.addEvents(midi, 0, -1, 0);
  midi.swapWith(output);
  monitorOutput(midi);
}
juce::AudioProcessorEditor *MuTheChordsProcessor::createEditor() {
  return new MuTheChordsEditor(*this);
}
juce::ValueTree MuTheChordsProcessor::savedState() {
  auto state = parameters.copyState();
  const int selected = selectedPatternIndex();
  stateValue(state, "perfPattern", std::max(0, selected));
  stateValue(state, "perfCategory", categoryForPattern(selected));
  stateValue(state, "perfPreview", 0);
  const auto current = loopRegion();
  stateValue(state, "perfLoopStart", current.first);
  stateValue(state, "perfLoopEnd", current.second);
  auto regions = state.getChildWithName("LoopRegions");
  if (regions.isValid()) state.removeChild(regions, nullptr);
  regions = juce::ValueTree("LoopRegions");
  for (size_t i = 0; i < library.patterns().size(); ++i) {
    const auto v = loopRegions[i].load();
    if (!v) continue;
    juce::ValueTree entry("Region");
    entry.setProperty("id", juce::String(library.patterns()[i].id), nullptr);
    entry.setProperty("start", static_cast<int>(v & 0xffffffffULL), nullptr);
    entry.setProperty("end", static_cast<int>(v >> 32), nullptr);
    regions.addChild(entry, -1, nullptr);
  }
  state.addChild(regions, -1, nullptr);
  if (selected >= 0)
    state.setProperty("patternId",
                      juce::String(library.patterns()[static_cast<size_t>(selected)].id), nullptr);
  else
    state.removeProperty("patternId", nullptr);
  state.setProperty("editorWidth", editorWidth.load(), nullptr);
  return state;
}
void MuTheChordsProcessor::getStateInformation(juce::MemoryBlock &destination) {
  if (auto xml = savedState().createXml()) copyXmlToBinary(*xml, destination);
}
void MuTheChordsProcessor::restoreState(juce::ValueTree state, bool fromProfile) {
  if (!fromProfile) {
    editorWidth.store(
        juce::jlimit(570, 1140, static_cast<int>(state.getProperty("editorWidth", 760))));
    editorSizeRestored.store(true);
  }
  const auto id = state.getProperty("patternId").toString().toStdString();
  int target =
      static_cast<int>(state.getChildWithProperty("id", "perfPattern").getProperty("value", 0));
  for (size_t i = 0; i < library.patterns().size(); ++i)
    if (library.patterns()[i].id == id) {
      target = static_cast<int>(i);
      break;
    }
  target = std::clamp(target, 0, std::max(0, static_cast<int>(library.patterns().size()) - 1));
  for (size_t i = 0; i < library.patterns().size(); ++i)
    loopRegions[i].store(0);
  const auto regions = state.getChildWithName("LoopRegions");
  for (const auto entry : regions) {
    const auto key = entry.getProperty("id").toString();
    const int start = juce::jlimit(0, 800000, static_cast<int>(entry.getProperty("start", 0))),
              end = juce::jlimit(0, 800000, static_cast<int>(entry.getProperty("end", 0)));
    for (size_t i = 0; i < library.patterns().size(); ++i)
      if (juce::String(library.patterns()[i].id) == key) {
        loopRegions[i].store(static_cast<uint64_t>(static_cast<uint32_t>(start)) |
                             (static_cast<uint64_t>(static_cast<uint32_t>(end)) << 32));
        break;
      }
  }
  if (!regions.isValid() && target < static_cast<int>(library.patterns().size())) {
    const auto a = static_cast<uint32_t>(static_cast<int>(
                   state.getChildWithProperty("id", "perfLoopStart").getProperty("value", 0))),
               b = static_cast<uint32_t>(static_cast<int>(
                   state.getChildWithProperty("id", "perfLoopEnd").getProperty("value", 0)));
    loopRegions[static_cast<size_t>(target)].store(static_cast<uint64_t>(a) |
                                                   (static_cast<uint64_t>(b) << 32));
  }
  const auto selectedRegion =
      library.patterns().empty() ? uint64_t{0} : loopRegions[static_cast<size_t>(target)].load();
  stateValue(state, "perfLoopStart", static_cast<int>(selectedRegion & 0xffffffffULL));
  stateValue(state, "perfLoopEnd", static_cast<int>(selectedRegion >> 32));
  stateValue(state, "perfPattern", target);
  stateValue(state, "perfCategory", categoryForPattern(target));
  stateValue(state, "perfPreview", 0);
  restoring.store(true);
  parameters.replaceState(state);
  selectedPattern.store(target);
  restoring.store(false);
  performancePreviewRequest.store(-1);
  previewRequest.store(-1);
  reportedPlayback.store(-1);
  stateRestored.store(true);
  harmonyDirty.store(false);
  publishImportedHarmony.store(fromProfile);
  rejoinHarmony.store(!fromProfile);
}
void MuTheChordsProcessor::setStateInformation(const void *data, int bytes) {
  if (auto xml = getXmlFromBinary(data, bytes))
    if (xml->hasTagName(parameters.state.getType())) restoreState(juce::ValueTree::fromXml(*xml));
}
juce::Result MuTheChordsProcessor::exportProfile(const juce::File &file) {
  const auto json =
      muthechords::ProfileState::encode(parameters, library.patterns(), selectedPatternIndex(),
                                        savedState().getChildWithName("LoopRegions"));
  juce::TemporaryFile temporary(file);
  {
    juce::FileOutputStream stream(temporary.getFile());
    if (!stream.openedOk()) return juce::Result::fail("Cannot open profile destination.");
    if (!stream.writeText(json, false, false, "\n"))
      return juce::Result::fail("Cannot write profile.");
    stream.flush();
    if (stream.getStatus().failed()) return stream.getStatus();
  }
  return temporary.overwriteTargetFileWithTemporary()
             ? juce::Result::ok()
             : juce::Result::fail("Cannot replace profile destination.");
}
juce::Result MuTheChordsProcessor::importProfile(const juce::File &file) {
  if (!file.existsAsFile() || file.getSize() > 1024 * 1024)
    return juce::Result::fail("Profile is missing or exceeds 1 MiB.");
  juce::ValueTree state;
  const auto result = muthechords::ProfileState::decode(file.loadFileAsString(), parameters,
                                                        library.patterns(), state);
  if (result.failed()) return result;
  restoreState(state, true);
  updateHostDisplay();
  return juce::Result::ok();
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new MuTheChordsProcessor(); }
