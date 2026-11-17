#include "PluginProcessor.h"
#include <MuTheChordsLib/NoteNames.hpp>
#include <iostream>
#include <stdexcept>

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void set(ChordInstrumentProcessor &p, const char *id, float value) {
  auto *param = p.parameters.getParameter(id);
  param->setValueNotifyingHost(param->convertTo0to1(value));
}
int main(int argc, char **argv) {
  try {
    juce::ScopedJuceInitialiser_GUI juce;
    ChordInstrumentProcessor p;
    p.prepareToPlay(48000, 512);
    require(p.parameters.getRawParameterValue("chord")->load() == 1,
            "fresh processor defaults to Triad");
    require(p.parameters.getRawParameterValue("low")->load() == 36 &&
                p.parameters.getRawParameterValue("high")->load() == 59,
            "fresh processor C1-B2 input range");
    require(muthechords::noteNameStd(36) == "C1" && muthechords::noteNameStd(59) == "B2" &&
                muthechords::noteNameStd(60) == "C3",
            "Bitwig octave labels");
    auto *key = p.parameters.getParameter("root");
    require(key->getNumSteps() == 12, "Key retains twelve automation steps");
    for (int i = 0; i < 12; ++i) {
      const float normalized = static_cast<float>(i) / 11;
      require(key->getText(normalized, 32) == muthechords::pitchNames[i],
              "Key exposes pitch names");
      require(key->convertFrom0to1(normalized) == i, "Key retains automation values");
    }
    auto *scaleParameter = p.parameters.getParameter("scale");
    require(scaleParameter->getNumSteps() == muthechords::scaleCount, "all scales exposed to host");
    for (int i = 0; i < muthechords::scaleCount; ++i)
      require(scaleParameter->getText(static_cast<float>(i) / (muthechords::scaleCount - 1), 128) ==
                  muthechords::scaleNames[static_cast<size_t>(i)],
              "scale host text matches catalog");
    auto oldState = p.parameters.copyState();
    oldState.getChildWithProperty("id", "root").setProperty("value", 9, nullptr);
    p.parameters.replaceState(oldState);
    require(p.parameters.getRawParameterValue("root")->load() == 9,
            "numeric project state retains Key");
    set(p, "root", 0);
    {
      // VST3 asks the editor which parameter belongs to the component under the
      // mouse.
      std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
      std::vector<juce::Component *> components;
      const auto collect = [&](auto &&self, juce::Component &parent) -> void {
        for (auto *child : parent.getChildren()) {
          components.push_back(child);
          self(self, *child);
        }
      };
      collect(collect, *editor);
      for (auto *child : components) {
        if (auto *combo = dynamic_cast<juce::ComboBox *>(child)) {
          auto &look = combo->getLookAndFeel();
          require(look.getPopupMenuFont() == look.getComboBoxFont(*combo),
                  "open menu uses the same font as its combo box");
          require(std::abs(look.getComboBoxFont(*combo).getHeight() - 13.0f) < 1e-4f,
                  "all combo boxes use 13pt font");
          for (int i = 0; i < combo->getNumItems(); ++i)
            require(juce::GlyphArrangement::getStringWidthInt(look.getComboBoxFont(*combo),
                                                              combo->getItemText(i)) <=
                        combo->getWidth() - 30,
                    ("combo item fits without squishing: " + combo->getTitle() + " / " +
                     combo->getItemText(i))
                        .toRawUTF8());
        }
        if (auto *label = dynamic_cast<juce::Label *>(child))
          require(label->getMinimumHorizontalScale() == 1.0f,
                  "labels never horizontally squish text");
      }
      const std::array<std::pair<const char *, const char *>, 23> controls{
          {{"Key", "root"},
           {"Scale", "scale"},
           {"Chord", "chord"},
           {"Voicing", "voicingPreset"},
           {"Transpose", "transpose"},
           {"From", "low"},
           {"To", "high"},
           {"Mapping", "mapping"},
           {"Outside scale", "outside"},
           {"Mode", "mode"},
           {"Strum", "strum"},
           {"Pedal", "pedal"},
           {"Alt Chord", "alt"},
           {"Polarity", "polarity"},
           {"Note spelling", "spelling"},
           {"Category", "perfCategory"},
           {"Pattern", "perfPattern"},
           {"Rate", "perfRate"},
           {"Division", "perfDivision"},
           {"Voices", "perfVoices"},
           {"Passing notes", "perfPassing"},
           {"Chord change", "perfChange"},
           {"Note transition", "perfTransition"}}};
      for (const auto &[title, id] : controls) {
        juce::ComboBox *found = nullptr;
        for (auto *child : components)
          if (auto *combo = dynamic_cast<juce::ComboBox *>(child))
            if (combo->getTitle() == title) found = combo;
        require(found != nullptr, "parameter control exists");
        const int index = p.parameters.getParameter(id)->getParameterIndex();
        require(editor->getControlParameterIndex(*found) == index,
                "VST3 control-to-parameter mapping");
        if (found->getNumChildComponents() > 0)
          require(editor->getControlParameterIndex(*found->getChildComponent(0)) == index,
                  "VST3 combo label mapping");
      }
      juce::ComboBox *voicing = nullptr;
      for (auto *child : components)
        if (auto *c = dynamic_cast<juce::ComboBox *>(child))
          if (c->getTitle() == "Voicing") voicing = c;
      require(voicing && voicing->getNumItems() == muthechords::voicingCount,
              "all voicings appear in combined selector");
      voicing->setSelectedId(muthechords::bassOctave + 1, juce::sendNotificationSync);
      require(p.parameters.getRawParameterValue("voicingPreset")->load() ==
                  muthechords::bassOctave - muthechords::sixthInversion,
              "selector chooses extended voicing");
      voicing->setSelectedId(muthechords::secondInversion + 1, juce::sendNotificationSync);
      require(p.parameters.getRawParameterValue("voicingPreset")->load() == 0 &&
                  p.parameters.getRawParameterValue("inversion")->load() == 2,
              "selector retains original inversion parameter");
      voicing->setSelectedId(1, juce::sendNotificationSync);
      for (auto *child : components) {
        if (child->getComponentID() == "performanceGraph")
          require(editor->getControlParameterIndex(*child) ==
                      p.parameters.getParameter("perfPreview")->getParameterIndex(),
                  "graph playback host parameter mapping");
        if (child->getTitle() == "Previous pattern" || child->getTitle() == "Next pattern")
          require(editor->getControlParameterIndex(*child) ==
                      p.parameters.getParameter("perfPattern")->getParameterIndex(),
                  "pattern arrow host mapping");
      }
      for (auto *child : components)
        if (auto *button = dynamic_cast<juce::Button *>(child)) {
          const auto text = button->getButtonText();
          const char *id = text == "Sync"         ? "harmonySync"
                           : text == "Full range" ? "full"
                           : text == "Latch"      ? "latch"
                           : text == "Bypass"     ? "bypass"
                                                  : nullptr;
          if (id)
            require(editor->getControlParameterIndex(*button) ==
                        p.parameters.getParameter(id)->getParameterIndex(),
                    "VST3 button parameter mapping");
        }
    }
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    {
      ChordInstrumentProcessor monitor;
      monitor.prepareToPlay(48000, 512);
      require(monitor.inputPitchBend() == -1 && monitor.inputModWheel() == -1,
              "controllers start without a received value");
      for (int mode = 0; mode < 2; ++mode)
        for (int bypass = 0; bypass < 2; ++bypass) {
          set(monitor, "mode", static_cast<float>(mode));
          set(monitor, "bypass", static_cast<float>(bypass));
          midi.clear();
          midi.addEvent(juce::MidiMessage::pitchWheel(2, 1234 + mode * 100 + bypass), 17);
          midi.addEvent(juce::MidiMessage::controllerEvent(2, 1, 40 + mode * 2 + bypass), 31);
          monitor.processBlock(audio, midi);
          require(monitor.inputPitchBend() == 1234 + mode * 100 + bypass &&
                      monitor.inputModWheel() == 40 + mode * 2 + bypass,
                  "controller indicators reflect MIDI input");
          const auto &diagnostic = monitor.midiDiagnostics();
          require(diagnostic.input.load() ==
                          muthechords::MidiDiagnostics::pack(2, 1234 + mode * 100 + bypass) &&
                      diagnostic.output.load() == diagnostic.input.load(),
                  "input and output diagnostics retain the real pitch-bend "
                  "channel and value");
          bool bend = false, mod = false;
          for (const auto event : midi) {
            const auto m = event.getMessage();
            bend |= m.isPitchWheel() && m.getPitchWheelValue() == 1234 + mode * 100 + bypass &&
                    m.getChannel() == 2 && event.samplePosition == 17;
            mod |= m.isController() && m.getControllerNumber() == 1 &&
                   m.getControllerValue() == 40 + mode * 2 + bypass && m.getChannel() == 2 &&
                   event.samplePosition == 31;
          }
          require(bend && mod, "controller monitoring preserves MIDI output and timing");
        }
      midi.clear();
    }
    {
      const auto channel = juce::Uuid().toString().toStdString();
      ChordInstrumentProcessor original(channel);
      std::unique_ptr<juce::AudioProcessorEditor> editor(original.createEditor());
      editor->setSize(950, 900);
      for (int pass = 0; pass < 2; ++pass) {
        for (auto *base : original.getParameters())
          if (auto *parameter = dynamic_cast<juce::RangedAudioParameter *>(base)) {
            const auto id = parameter->paramID;
            if (id == "perfPreview" || id == "perfCategory" || id == "perfPattern" ||
                id == "harmonySync")
              continue;
            parameter->setValueNotifyingHost(pass == 0 ? 1.0f : 0.0f);
          }
        if (original.patternList().size() > 3) {
          int selected = 3;
          if (pass == 1)
            for (size_t i = 0; i < original.patternList().size(); ++i)
              if (original.patternList()[i].category != original.patternList().front().category) {
                selected = static_cast<int>(i);
                break;
              }
          set(original, "perfPattern", static_cast<float>(selected));
        }
        set(original, "harmonySync", static_cast<float>(pass));
        juce::Thread::sleep(65);
        juce::Timer::callPendingTimersSynchronously();
        juce::MemoryBlock state;
        original.getStateInformation(state);
        ChordInstrumentProcessor loaded(channel);
        loaded.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        loaded.prepareToPlay(44100, 256);
        juce::Thread::sleep(65);
        juce::Timer::callPendingTimersSynchronously();
        for (auto *base : original.getParameters())
          if (auto *parameter = dynamic_cast<juce::RangedAudioParameter *>(base)) {
            if (parameter->paramID == "perfPreview") continue;
            require(std::abs(parameter->getValue() -
                             loaded.parameters.getParameter(parameter->paramID)->getValue()) <
                        1e-6f,
                    ("persistent parameter restores: " + parameter->paramID).toRawUTF8());
          }
        require(loaded.selectedPatternIndex() == original.selectedPatternIndex(),
                "effective pattern restores");
        require(!loaded.performancePreview() && !loaded.performanceRunning(),
                "playback remains stopped after restore");
        std::unique_ptr<juce::AudioProcessorEditor> reopened(loaded.createEditor());
        require(reopened->getWidth() == 950 && reopened->getHeight() == 900,
                "editor size restores with project");
        reopened->setSize(760, 720);
        loaded.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        for (int attempt = 0; attempt < 30 && reopened->getWidth() != 950; ++attempt) {
          juce::Thread::sleep(20);
          juce::Timer::callPendingTimersSynchronously();
        }
        require(reopened->getWidth() == 950 && reopened->getHeight() == 900,
                "state restore resizes an already open editor");
      }
    }
    {
      ChordInstrumentProcessor b;
      b.prepareToPlay(48000, 512);
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 5);
      b.processBlock(audio, midi);
      require(midi.getNumEvents() == 3, "normal chord starts before bypass");
      set(b, "bypass", 1);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(2, 61, static_cast<juce::uint8>(77)), 17);
      midi.addEvent(juce::MidiMessage::controllerEvent(2, 74, 51), 25);
      b.processBlock(audio, midi);
      int offs = 0, passed = 0;
      for (auto e : midi) {
        auto m = e.getMessage();
        if (m.isNoteOff() && m.getChannel() == 1) ++offs;
        if (m.isNoteOn() && m.getChannel() == 2 && m.getNoteNumber() == 61 &&
            e.samplePosition == 17)
          ++passed;
        if (m.isController() && m.getControllerNumber() == 74 && e.samplePosition == 25) ++passed;
      }
      require(offs == 3 && passed == 2, "bypass stops generated chord and passes MIDI unchanged");
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(3, 67, static_cast<juce::uint8>(50)), 31);
      b.processBlock(audio, midi);
      require(midi.getNumEvents() == 1 && midi.getFirstEventTime() == 31,
              "bypass stays transparent");
      juce::MemoryBlock state;
      b.getStateInformation(state);
      ChordInstrumentProcessor restored;
      restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
      require(restored.parameters.getRawParameterValue("bypass")->load() == 1,
              "bypass persists in project state");
      set(b, "bypass", 0);
      midi.clear();
      b.processBlock(audio, midi);
      offs = 0;
      for (auto e : midi)
        if (e.getMessage().isNoteOff()) ++offs;
      require(offs == 2, "disabling bypass releases notes held during bypass");
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 5);
      b.processBlock(audio, midi);
      require(midi.getNumEvents() == 3, "processing resumes after bypass");
    }
    {
      const auto channel = juce::Uuid().toString().toStdString();
      ChordInstrumentProcessor reset(channel), peer(channel);
      reset.prepareToPlay(48000, 512);
      set(reset, "root", 9);
      set(reset, "scale", 1);
      set(reset, "chord", 3);
      set(reset, "transpose", 12);
      set(reset, "latch", 1);
      set(reset, "strum", 2);
      set(reset, "perfVoices", 2);
      if (reset.patternList().size() > 3) set(reset, "perfPattern", 3);
      set(reset, "harmonySync", 1);
      set(peer, "harmonySync", 1);
      midi.addEvent(juce::MidiMessage::pitchWheel(1, 4096), 0);
      midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 77), 0);
      midi.addEvent(juce::MidiMessage::noteOn(1, 45, static_cast<juce::uint8>(90)), 0);
      reset.processBlock(audio, midi);
      require(reset.hasSoundingOutput() && reset.inputPitchBend() == 4096 &&
                  reset.inputModWheel() == 77,
              "reset test starts a sounding chord and controller state");
      std::unique_ptr<juce::AudioProcessorEditor> resetEditor(reset.createEditor());
      juce::Button *octUp = nullptr, *resetButton = nullptr;
      juce::Component *previewKeyboard = nullptr;
      const auto findButtons = [&](auto &&self, juce::Component &parent) -> void {
        for (auto *child : parent.getChildren()) {
          if (child->getTitle().startsWith("Preview keyboard")) previewKeyboard = child;
          if (auto *b = dynamic_cast<juce::Button *>(child)) {
            if (b->getTitle() == "Keyboard octave up") octUp = b;
            if (b->getButtonText() == "Reset") resetButton = b;
          }
          self(self, *child);
        }
      };
      findButtons(findButtons, *resetEditor);
      require(octUp && resetButton && previewKeyboard, "octave and reset buttons exist");
      octUp->onClick();
      require(previewKeyboard->getTitle() == "Preview keyboard C2 to B4",
              "octave up shifts keyboard range");
      resetButton->onClick();
      require(previewKeyboard->getTitle() == "Preview keyboard C1 to B3",
              "Reset restores default keyboard octave");
      require(reset.inputPitchBend() == -1 && reset.inputModWheel() == -1 &&
                  reset.inputNote() == -1,
              "Reset clears controller and input indicators");
      for (auto *parameter : reset.getParameters())
        require(std::abs(parameter->getValue() - parameter->getDefaultValue()) < 1e-6f,
                "reset restores every default parameter");
      require(reset.selectedPatternIndex() == (reset.patternList().empty() ? -1 : 0),
              "reset selects default pattern");
      juce::Timer::callPendingTimersSynchronously();
      midi.clear();
      reset.processBlock(audio, midi);
      require(!reset.hasSoundingOutput() && midi.getNumEvents() > 0,
              "reset sends note-offs and stops playback");
      require(peer.parameters.getRawParameterValue("root")->load() == 9 &&
                  peer.parameters.getRawParameterValue("scale")->load() == 1,
              "reset does not change other synced instances");
      set(peer, "scale", 43);
      juce::Thread::sleep(60);
      juce::Timer::callPendingTimersSynchronously();
      set(reset, "harmonySync", 1);
      require(reset.parameters.getRawParameterValue("scale")->load() == 43,
              "new scale reaches another synced processor");
      reset.releaseResources();
    }
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(93)), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 211);
    p.processBlock(audio, midi);
    int ons = 0, offs = 0;
    for (auto event : midi) {
      auto m = event.getMessage();
      if (m.isNoteOn()) {
        ++ons;
        require(event.samplePosition == 17 && m.getVelocity() == 93,
                "sample/velocity preservation");
      }
      if (m.isNoteOff()) {
        ++offs;
        require(event.samplePosition == 211, "note-off sample");
      }
    }
    require(ons == 3 && offs == 3, "triad MIDI output");
    {
      ChordInstrumentProcessor labels;
      labels.prepareToPlay(48000, 512);
      set(labels, "latch", 1);
      set(labels, "chord", 2);
      set(labels, "inversion", 1);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(80)), 0);
      labels.processBlock(audio, midi);
      muthechords::ChordSnapshot snapshot;
      require(labels.readChordSnapshot(snapshot) && snapshot.root == 38 &&
                  snapshot.notes[0] == 41 && snapshot.count == 4,
              "coherent display snapshot");
      set(labels, "scale", 1);
      set(labels, "root", 2);
      set(labels, "transpose", 12);
      set(labels, "spelling", 1);
      midi.clear();
      labels.processBlock(audio, midi);
      require(labels.readChordSnapshot(snapshot) && snapshot.root == 38 && snapshot.key == 0 &&
                  snapshot.scale == 0,
              "display preserves triggered context after parameter edits");
      labels.requestStop();
      midi.clear();
      labels.processBlock(audio, midi);
      require(labels.readChordSnapshot(snapshot) && snapshot.count == 0,
              "display clears after stop");
      // Closed GUI: a stale triad contained in the next extended chord must not
      // be accepted as current.
      set(labels, "root", 0);
      set(labels, "scale", 0);
      set(labels, "transpose", 0);
      set(labels, "chord", 1);
      set(labels, "inversion", 0);
      set(labels, "full", 1);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(80)), 0);
      labels.processBlock(audio, midi);
      for (int i = 0; i < 32; ++i) {
        midi.clear();
        labels.processBlock(audio, midi);
      }
      set(labels, "chord", 3);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 65, static_cast<juce::uint8>(80)), 0);
      labels.processBlock(audio, midi);
      ChordInstrumentProcessor::DisplayFrame frame;
      require(labels.readDisplaySnapshot(frame) && frame.chord.root == 65 &&
                  frame.chord.count == 5 && frame.notes[65],
              "closed GUI receives latest complete frame");
      labels.releaseResources();
      require(labels.readDisplaySnapshot(frame) && frame.chord.count == 0 && !frame.notes[65] &&
                  !labels.outputNote(65),
              "releaseResources clears display");
      juce::MemoryBlock saved;
      labels.getStateInformation(saved);
      ChordInstrumentProcessor loaded;
      loaded.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
      require(loaded.parameters.getRawParameterValue("spelling")->load() == 1,
              "spelling saved in project");
      labels.releaseResources();
    }
    {
      ChordInstrumentProcessor range;
      range.prepareToPlay(48000, 512);
      set(range, "transpose", 12);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(80)), 0);
      range.processBlock(audio, midi);
      require(midi.getNumEvents() == 1 && midi.getFirstEventTime() == 0,
              "C3 input passes above range");
      require((*midi.begin()).getMessage().getNoteNumber() == 60, "solo note is not transposed");
      range.requestStop();
      midi.clear();
      range.processBlock(audio, midi);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(80)), 0);
      range.processBlock(audio, midi);
      require(midi.getNumEvents() == 3 && range.outputNote(48) && range.outputNote(52) &&
                  range.outputNote(55),
              "C1 input is processed before output transpose");
      range.requestStop();
      midi.clear();
      range.processBlock(audio, midi);
      set(range, "pedal", 2);
      midi.clear();
      midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(80)), 1);
      range.processBlock(audio, midi);
      require(range.outputNote(59), "default reversed pedal selects Alt seventh at CC64 zero");
      midi.clear();
      midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
      range.processBlock(audio, midi);
      require(range.outputNote(59), "pedal release keeps current chord");
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(80)), 0);
      range.processBlock(audio, midi);
      require(range.outputNote(50) && range.outputNote(53) && range.outputNote(57) &&
                  !range.outputNote(60),
              "released reversed pedal selects Triad on next trigger");
      range.releaseResources();
    }
    set(p, "transpose", 12);
    set(p, "latch", 1);
    set(p, "scale", 1);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    ChordInstrumentProcessor restored;
    restored.prepareToPlay(44100, 512);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.parameters.getRawParameterValue("transpose")->load() == 12, "saved transpose");
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 39, static_cast<juce::uint8>(75)), 5);
    restored.processBlock(audio, midi);
    require(midi.getNumEvents() == 3 && restored.outputNote(51), "restored Dorian octave chord");
    set(restored, "mode", 1);
    midi.clear();
    restored.processBlock(audio, midi);
    require(midi.getNumEvents() == 3 && !restored.outputNote(51), "mode reset note-offs");
    juce::MemoryBlock performanceState;
    restored.getStateInformation(performanceState);
    p.setStateInformation(performanceState.getData(), static_cast<int>(performanceState.getSize()));
    require(p.parameters.getRawParameterValue("mode")->load() == 1, "mode persisted");
    set(restored, "mode", 0);
    set(restored, "latch", 0);
    set(restored, "pedal", 2);
    set(restored, "polarity", 0);
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(88)), 3);
    restored.processBlock(audio, midi);
    require(midi.getNumEvents() == 4, "pedal selects seventh and is consumed");
    restored.requestStop();
    midi.clear();
    restored.processBlock(audio, midi);
    require(midi.getNumEvents() == 4, "stop emits note-offs");
    {
      ChordInstrumentProcessor strum;
      strum.prepareToPlay(48000, 512);
      set(strum, "strum", 1);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 10);
      strum.processBlock(audio, midi);
      std::vector<std::pair<int, int>> notesAndOffsets;
      for (auto event : midi)
        if (event.getMessage().isNoteOn())
          notesAndOffsets.emplace_back(event.getMessage().getNoteNumber(), event.samplePosition);
      require(notesAndOffsets.size() == 3 && notesAndOffsets[0].first == 36 &&
                  notesAndOffsets[1].first == 40 && notesAndOffsets[2].first == 43 &&
                  notesAndOffsets[0].second == 10 &&
                  notesAndOffsets[1].second > notesAndOffsets[0].second &&
                  notesAndOffsets[2].second > notesAndOffsets[1].second,
              "Strum Up spreads chord attacks from low to high");
      strum.requestStop();
      midi.clear();
      strum.processBlock(audio, midi);
      set(strum, "strum", 3);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 10);
      strum.processBlock(audio, midi);
      notesAndOffsets.clear();
      for (auto event : midi)
        if (event.getMessage().isNoteOn())
          notesAndOffsets.emplace_back(event.getMessage().getNoteNumber(), event.samplePosition);
      require(notesAndOffsets.size() == 3 && notesAndOffsets[0].first == 43 &&
                  notesAndOffsets[0].second == 10 && notesAndOffsets[1].first == 40 &&
                  notesAndOffsets[2].first == 36 &&
                  notesAndOffsets[2].second > notesAndOffsets[1].second,
              "Strum Down spreads chord attacks from high to low");
      strum.releaseResources();
    }
    {
      ChordInstrumentProcessor strum;
      strum.prepareToPlay(48000, 512);
      set(strum, "strum", 1);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 10);
      midi.addEvent(juce::MidiMessage::noteOff(1, 36), 100);
      strum.processBlock(audio, midi);
      require(midi.getNumEvents() == 2, "short tap cancels strum voices before their onset");
      auto event = midi.begin();
      require((*event).getMessage().isNoteOn() && (*event).getMessage().getNoteNumber() == 36 &&
                  (*event).samplePosition == 10,
              "short tap starts the first strum voice");
      ++event;
      require((*event).getMessage().isNoteOff() && (*event).getMessage().getNoteNumber() == 36 &&
                  (*event).samplePosition == 100,
              "short tap releases only the voice that sounded");
    }
    {
      ChordInstrumentProcessor strum;
      strum.prepareToPlay(48000, 128);
      set(strum, "strum", 1);
      juce::AudioBuffer<float> shortBlock(2, 128);
      std::vector<std::pair<int, int>> attacks;
      int blockStart = 0;
      for (int blockSize : {128, 64, 128, 128, 128}) {
        shortBlock.setSize(2, blockSize);
        midi.clear();
        if (blockStart == 0)
          midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 10);
        strum.processBlock(shortBlock, midi);
        for (auto event : midi) {
          const auto message = event.getMessage();
          require(message.isNoteOn(), "held strum emits only note-ons");
          attacks.emplace_back(message.getNoteNumber(), blockStart + event.samplePosition);
        }
        blockStart += blockSize;
      }
      require(attacks == std::vector<std::pair<int, int>>{{36, 10}, {40, 250}, {43, 490}},
              "strum preserves exact onset spacing across changing block lengths");
      strum.requestStop();
      midi.clear();
      strum.processBlock(shortBlock, midi);
      require(midi.getNumEvents() == 3, "stop releases the three sounding strum voices");
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 120);
      strum.processBlock(shortBlock, midi);
      require(midi.getNumEvents() == 1, "only the first strum voice starts near the block end");
      set(strum, "bypass", 1);
      midi.clear();
      strum.processBlock(shortBlock, midi);
      require(midi.getNumEvents() == 1 && (*midi.begin()).getMessage().isNoteOff(),
              "bypass releases the strum voice that sounded");
      set(strum, "bypass", 0);
      for (int block = 0; block < 4; ++block) {
        midi.clear();
        strum.processBlock(shortBlock, midi);
        require(midi.isEmpty(), "bypass discards pending strum voices");
      }
    }
    if (argc > 1) {
      set(p, "mode", 0);
      set(p, "strum", 0);
      set(p, "transpose", 0);
      set(p, "scale", 0);
      midi.clear();
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 0);
      p.processBlock(audio, midi);
      std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
      for (int i = 0; i < 5; ++i)
        juce::Timer::callPendingTimersSynchronously();
      editor->setScaleFactor(2.0f);
      auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
      const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
      destination.deleteFile();
      juce::FileOutputStream file(destination);
      require(file.openedOk(), "screenshot destination");
      juce::PNGImageFormat format;
      require(format.writeImageToStream(image, file), "editor screenshot");
    }
    p.releaseResources();
    restored.releaseResources();
    std::cout << "Processor MIDI timing, velocity, state, mode, pedal and Stop "
                 "checks passed.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
