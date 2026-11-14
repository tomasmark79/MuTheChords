#pragma once
#include <MuTheChordsLib/ChordEngine.hpp>
#include <MuTheChordsLib/DisplayMailbox.hpp>
#include <MuTheChordsLib/HarmonySync.hpp>
#include <MuTheChordsLib/MidiDiagnostics.hpp>
#include <MuTheChordsLib/PatternLibrary.hpp>
#include <MuTheChordsLib/PerformanceEngine.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

class MuTheChordsProcessor final : public juce::AudioProcessor,
                                   public muthechords::MidiDiagnosticsSource,
                                   private juce::AudioProcessorParameter::Listener,
                                   private juce::Timer {
private:
  muthechords::PatternLibrary library;
  std::vector<std::string> categories;

public:
  explicit MuTheChordsProcessor(std::string harmonyChannel = {});
  ~MuTheChordsProcessor() override;
  void prepareToPlay(double, int) override;
  void releaseResources() override;
  void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  bool isBusesLayoutSupported(const BusesLayout &) const override;
  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }
  const juce::String getName() const override { return "MuTheChords"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override;
  void setStateInformation(const void *, int) override;
  juce::AudioProcessorParameter *getBypassParameter() const override {
    return parameters.getParameter("bypass");
  }
  void requestStop() { stopRequested.store(true); }
  void resetToDefaults();
  int savedEditorWidth() const { return editorWidth.load(); }
  void rememberEditorWidth(int width) {
    const int next = juce::jlimit(570, 1140, width);
    if (editorWidth.exchange(next) != next)
      updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
  }
  bool takeEditorSizeRestore() { return editorSizeRestored.exchange(false); }
  void previewOn(int note) { previewRequest.store(note); }
  void previewOff() { previewRequest.store(-2); }
  bool outputNote(int note) const { return sounding[static_cast<size_t>(note)].load(); }
  int inputNote() const { return lastInput.load(); }
  int inputPitchBend() const { return lastPitchBend.load(); }
  int inputModWheel() const { return lastModWheel.load(); }
  muthechords::MidiDiagnostics &midiDiagnostics() override { return diagnostics; }
  bool inputOutside() const { return lastOutside.load(); }
  struct DisplayFrame {
    muthechords::ChordSnapshot chord;
    std::array<bool, 128> notes{};
  };
  bool readDisplaySnapshot(DisplayFrame &frame) { return chordDisplay.read(frame); }
  bool readChordSnapshot(muthechords::ChordSnapshot &snapshot) {
    DisplayFrame frame;
    if (!chordDisplay.read(frame)) return false;
    snapshot = frame.chord;
    return true;
  }
  const std::vector<muthechords::Pattern> &patternList() const { return library.patterns(); }
  void requestPerformancePreview(bool enabled);
  int selectedPatternIndex() const {
    return library.patterns().empty() ? -1 : selectedPattern.load();
  }
  bool hasSoundingOutput() const { return anyOutput.load(); }
  juce::Result exportProfile(const juce::File &);
  juce::Result importProfile(const juce::File &);
  bool performancePreview() const { return previewActive.load(); }
  bool performanceRunning() const { return running.load(); }
  bool transportPlaying() const { return hostPlaying.load(); }
  double hostTempo() const { return knownTempo.load(); }
  double performancePhase() const { return phase.load(); }
  std::pair<int, int> loopRegion() const;
  void setLoopRegion(int start, int end);
  juce::AudioProcessorValueTreeState parameters;

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout
      makeParameters(const std::vector<muthechords::Pattern> &);
  muthechords::Settings readSettings() const;
  void parameterChanged(const juce::String &, float);
  void parameterValueChanged(int, float) override;
  void parameterGestureChanged(int, bool) override {}
  void timerCallback() override;
  int categoryForPattern(int) const;
  void restoreState(juce::ValueTree, bool fromProfile = false);
  muthechords::HarmonyState currentHarmony() const;
  void applySharedHarmony(const muthechords::HarmonyState &);
  void updateHarmonySync();
  muthechords::HarmonySync harmonyBus;
  std::array<std::atomic<int>, 2> harmonyEcho{};
  std::atomic<bool> harmonyDirty{false}, rejoinHarmony{false}, publishImportedHarmony{false};
  juce::ValueTree savedState();
  std::atomic<int> selectedPattern{0}, echoPattern{-1}, echoCategory{-1}, echoPlayback{-1},
      reportedPlayback{-1};
  std::atomic<int> echoLoopStart{-1}, echoLoopEnd{-1};
  std::unique_ptr<std::atomic<uint64_t>[]> loopRegions;
  std::atomic<bool> restoring{false}, anyOutput{false};
  muthechords::Engine engine, harmonyBuilder;
  muthechords::PerformanceEngine performance;
  muthechords::PerformanceSettings readPerformanceSettings() const;
  muthechords::ChordSnapshot buildHarmony(int note, int channel, const muthechords::Settings &,
                                          bool preview = false);
  void emit(muthechords::Event, int offset);
  void stopAll(bool clearControllers = false);
  void handleNoteOn(int channel, int note, int velocity, double beat,
                    const muthechords::Settings &);
  void handleNoteOff(int channel, int note, const muthechords::Settings &);
  bool pedalDown(int channel, const muthechords::Settings &) const;
  std::array<std::array<unsigned int, 128>, 16> outputOwners{}, passed{};
  std::array<int, 16> rawPedals{};
  std::array<bool, 16> pedalSeen{};
  std::atomic<int> performancePreviewRequest{-1};
  std::atomic<bool> previewActive{false}, hostPlaying{false}, running{false};
  std::atomic<double> knownTempo{0}, phase{0};
  double sampleRate = 48000, stoppedBeat = 0, expectedHostBeat = 0;
  bool hadHostPosition = false;
  int triggerNote = -1, triggerChannel = 1, lastPerformanceInput = -1;
  bool performanceKeyHeld = false;
  int eventOffset = 0;
  int releasedForRestart = -1;
  juce::MidiBuffer output;
  std::atomic<bool> stopRequested{false};
  std::atomic<int> previewRequest{-1}, lastInput{-1};
  std::atomic<int> lastPitchBend{-1}, lastModWheel{-1};
  muthechords::MidiDiagnostics diagnostics;
  const juce::File diagnosticFile =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("MuTheChords-midi-" + juce::Uuid().toString() + ".json");
  uint32_t diagnosticRevision = 0;
  void writeMidiDiagnostics();
  void monitorOutput(const juce::MidiBuffer &midi);
  void monitorController(const juce::MidiMessage &message) {
    if (message.isPitchWheel()) {
      lastPitchBend.store(message.getPitchWheelValue(), std::memory_order_relaxed);
      diagnostics.input.store(
          muthechords::MidiDiagnostics::pack(message.getChannel(), message.getPitchWheelValue()),
          std::memory_order_relaxed);
      diagnostics.inputCount.fetch_add(1, std::memory_order_relaxed);
    } else if (message.isController() && message.getControllerNumber() == 1)
      lastModWheel.store(message.getControllerValue(), std::memory_order_relaxed);
  }
  std::atomic<bool> lastOutside{false};
  std::array<std::atomic<bool>, 128> sounding{};
  int previewHeld = -1;
  bool wasPlaying = false;
  bool userBypassed = false;
  std::array<std::array<unsigned int, 128>, 16> bypassHeld{};
  std::atomic<bool> stateRestored{false};
  std::atomic<bool> resetPerformanceInput{false};
  std::atomic<int> editorWidth{760};
  std::atomic<bool> editorSizeRestored{false};
  muthechords::DisplayMailbox<DisplayFrame> chordDisplay;
};

using ChordInstrumentProcessor = MuTheChordsProcessor;
