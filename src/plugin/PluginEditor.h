#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class MuTheChordsEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit MuTheChordsEditor(MuTheChordsProcessor &);
  ~MuTheChordsEditor() override;
  void paint(juce::Graphics &) override;
  void resized() override;
  int getControlParameterIndex(juce::Component &) override;

private:
  class PanelLookAndFeel;
  class PreviewKeyboard;
  class PatternGraph;
  class HoverComboBox : public juce::ComboBox {
    void mouseEnter(const juce::MouseEvent &) override { repaint(); }
    void mouseExit(const juce::MouseEvent &) override { repaint(); }
  };
  using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
  void timerCallback() override;
  void setMode(int);
  void selectPattern(int);
  int adjacentPattern(int direction) const;
  void chooseProfile(bool exporting);
  void syncPatternSelectors();
  void fillPatterns(const juce::String &, int);
  float value(const char *) const;
  void addCombo(juce::ComboBox &, juce::Label &, const char *, const juce::StringArray &,
                const char *);
  MuTheChordsProcessor &processor;
  juce::Component content;
  juce::TooltipWindow tooltips{this, 600};
  std::unique_ptr<PanelLookAndFeel> theme;
  std::unique_ptr<PreviewKeyboard> keyboard;
  std::unique_ptr<PatternGraph> patternGraph;
  std::unique_ptr<juce::FileChooser> profileChooser;
  HoverComboBox root, scale, chord, inversion, transpose, low, high, mapping, outside, pedal, alt,
      polarity, spelling;
  juce::Label spellingLabel;
  juce::Label rootLabel, scaleLabel, chordLabel, inversionLabel, transposeLabel;
  juce::Label lowLabel, highLabel, mappingLabel, outsideLabel, pedalLabel, altLabel, polarityLabel;
  juce::Label outputTitle, outputNotes, patternSummary;
  juce::Label loopInfo;
  HoverComboBox category, pattern, rate, division, voices, passing, change, transition, playback;
  juce::Label categoryLabel, patternLabel, rateLabel, divisionLabel, voicesLabel, passingLabel,
      changeLabel, transitionLabel, playbackLabel;
  juce::TextButton previousPattern, nextPattern;
  juce::TextButton fullLoop{"Full"};
  juce::TextButton importProfile{"Import"}, exportProfile{"Export"};
  juce::TextButton harmonySync{"Sync"};
  juce::TextButton bypass{"Bypass"};
  juce::TextButton resetDefaults{"Reset"};
  juce::Label syncInfo;
  juce::StringArray categories;
  int displayedPattern = -1;
  juce::TextButton chordsTab{"Chords"}, performanceTab{"Performance"}, stop{"Stop"};
  juce::ToggleButton full{"Full range"}, latch{"Latch"};
  juce::ToggleButton dawSync{"DAW Sync"};
  std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
  std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
  bool performanceMode = false;
  muthechords::ChordSnapshot currentChord;
  MuTheChordsProcessor::DisplayFrame currentDisplay;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MuTheChordsEditor)
};

using ChordInstrumentEditor = MuTheChordsEditor;
