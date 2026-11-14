#include "PluginEditor.h"
#include <MuTheChordsLib/ChordNames.hpp>
#include <MuTheChordsLib/NoteNames.hpp>
#include <MuTheChordsLib/PerformanceTypes.hpp>
#include <algorithm>
#include <cmath>

namespace {
  const juce::Colour background{0xff172b43}, panel{0xff213b59}, line{0xff496783};
  const juce::Colour foreground{0xfff0f5fa}, muted{0xffc2d1df}, accent{0xffbce7b0};
  const juce::Colour control{0xff304d6b}, controlOutline{0xff67819a}, graphBackground{0xff192e45};
  juce::String noteName(int note) { return juce::String(muthechords::noteNameStd(note)); }
  bool blackKey(int note) {
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
  }
  void drawGroup(juce::Graphics &g, juce::Rectangle<float> bounds, const juce::String &name,
                 juce::Colour fill = panel) {
    const bool output = name == "Output";
    if (output)
      g.setGradientFill(juce::ColourGradient(fill, bounds.getX(), bounds.getY(),
                                             juce::Colour(0xff203b58), bounds.getRight(),
                                             bounds.getBottom(), false));
    else
      g.setColour(fill);
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(output ? juce::Colour(0xff577a99) : line);
    g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
    const juce::Font captionFont(juce::FontOptions(9.0f));
    const auto captionText = name.toUpperCase();
    const float width =
        static_cast<float>(juce::GlyphArrangement::getStringWidthInt(captionFont, captionText)) +
        14.0f;
    const juce::Rectangle<float> caption{bounds.getX() + 10.0f, bounds.getY() - 9.0f, width, 16.0f};
    g.setColour(fill);
    g.fillRoundedRectangle(caption, 7.0f);
    g.setColour(line);
    g.drawRoundedRectangle(caption.reduced(0.5f), 7.0f, 1.0f);
    g.setColour(muted);
    g.setFont(captionFont);
    g.drawText(captionText, caption, juce::Justification::centred);
  }
} // namespace

class MuTheChordsEditor::PanelLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  PanelLookAndFeel() {
    setColour(juce::ComboBox::backgroundColourId, control);
    setColour(juce::ComboBox::textColourId, foreground);
    setColour(juce::ComboBox::outlineColourId, controlOutline);
    setColour(juce::ComboBox::focusedOutlineColourId, accent);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, foreground);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, control);
    setColour(juce::PopupMenu::highlightedTextColourId, foreground);
    setColour(juce::TextButton::buttonColourId, control);
    setColour(juce::TextButton::buttonOnColourId, panel);
    setColour(juce::TextButton::textColourOffId, foreground);
    setColour(juce::TextButton::textColourOnId, accent);
    setColour(juce::ToggleButton::textColourId, foreground);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ToggleButton::tickDisabledColourId, muted);
  }
  juce::Font getComboBoxFont(juce::ComboBox &) override { return juce::FontOptions(13.0f); }
  juce::Font getPopupMenuFont() override { return juce::FontOptions(13.0f); }
  void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override {
    // Leave a dedicated arrow gutter; Label draws ellipses within the text
    // area.
    label.setBounds(8, 1, juce::jmax(0, box.getWidth() - 36), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
  }
  void drawComboBox(juce::Graphics &g, int width, int height, bool, int, int, int, int,
                    juce::ComboBox &box) override {
    const auto bounds =
        juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height))
            .reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId)
                    .withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(box.isEnabled() && (box.hasKeyboardFocus(true) || box.isMouseOver(true))
                    ? accent
                    : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    juce::Path arrow;
    const float x = static_cast<float>(width) - 19.0f, y = static_cast<float>(height) / 2.0f;
    arrow.startNewSubPath(x - 3.0f, y - 2.0f);
    arrow.lineTo(x, y + 1.0f);
    arrow.lineTo(x + 3.0f, y - 2.0f);
    g.setColour(muted);
    g.strokePath(arrow, juce::PathStrokeType(1.4f));
  }
  void drawButtonBackground(juce::Graphics &g, juce::Button &button, const juce::Colour &,
                            bool highlighted, bool) override {
    const bool tab = button.getComponentID() == "mode";
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float alpha = button.isEnabled() ? 1.0f : 0.45f;
    if (!tab || button.getToggleState()) {
      g.setColour((tab ? panel : control).withMultipliedAlpha(alpha));
      g.fillRoundedRectangle(bounds, 5.0f);
    }
    if (!tab) {
      g.setColour((button.isEnabled() && (highlighted || button.hasKeyboardFocus(true))
                       ? accent
                       : controlOutline)
                      .withMultipliedAlpha(alpha));
      g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    } else if (button.hasKeyboardFocus(true)) {
      g.setColour(accent);
      g.drawRoundedRectangle(bounds.reduced(1.0f), 5.0f, 1.0f);
    }
  }
  void drawButtonText(juce::Graphics &g, juce::TextButton &button, bool, bool) override {
    const bool tab = button.getComponentID() == "mode";
    const auto colour = tab ? (button.getToggleState() ? accent : muted)
                            : (button.getToggleState() ? accent : foreground);
    g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
    g.setFont(juce::FontOptions(tab ? 12.0f : 13.0f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 1),
                     juce::Justification::centred, 1);
  }
};

class MuTheChordsEditor::PreviewKeyboard final : public juce::Component {
public:
  explicit PreviewKeyboard(MuTheChordsEditor &editor) : owner(editor) {
    setTitle("Preview keyboard C1 to B3");
    setDescription("Click and hold a key to preview the current harmony.");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  ~PreviewKeyboard() override { release(); }
  void paint(juce::Graphics &g) override {
    for (int pass = 0; pass < 2; ++pass)
      for (int note = 36; note < 72; ++note) {
        const bool black = blackKey(note);
        if (black != (pass == 1)) continue;
        auto bounds = keyBounds(note);
        const bool full = owner.value("full") > 0.5f;
        const bool inRange = full || (note >= owner.value("low") && note <= owner.value("high"));
        const bool input = owner.processor.inputNote() == note;
        const bool invalid = input && owner.processor.inputOutside();
        const bool sounding = owner.processor.outputNote(note);
        juce::Colour fill = black     ? juce::Colour(0xff242b2a)
                            : inRange ? juce::Colour(0xffc8d6c5)
                                      : juce::Colour(0xffdfe4df);
        if (sounding) fill = accent;
        if (input) fill = juce::Colour(0xffd58f91);
        if (invalid) fill = juce::Colour(0xff737b80);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds.reduced(0.5f), 3.0f);
        g.setColour(background);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
        if (inRange && !black && !invalid) {
          g.setColour(juce::Colour(0xff89b57c));
          g.fillRect(bounds.withHeight(4.0f).reduced(1.0f, 0.0f));
        }
        if (isScaleTone(note)) {
          g.setColour(black ? juce::Colour(0xff9caf94) : juce::Colour(0xff819c78));
          g.fillEllipse(bounds.getCentreX() - 2.5f, bounds.getBottom() - (black ? 12.0f : 25.0f),
                        5.0f, 5.0f);
        }
        if (invalid) {
          g.setColour(juce::Colours::white);
          g.setFont(juce::FontOptions(13.0f));
          g.drawText("x", bounds.withY(8.0f).withHeight(16.0f), juce::Justification::centred);
        }
        if (input && !invalid) {
          g.setColour(juce::Colour(0xffb8666d));
          g.drawRoundedRectangle(bounds.reduced(1.5f), 3.0f, 2.0f);
        }
        if (!black) {
          g.setColour(juce::Colour(0xff48514b));
          g.setFont(juce::FontOptions(9.0f));
          g.drawText(noteName(note), bounds.withY(bounds.getBottom() - 17.0f).withHeight(14.0f),
                     juce::Justification::centred);
        }
      }
  }
  void mouseDown(const juce::MouseEvent &e) override { playAt(e.position); }
  void mouseDrag(const juce::MouseEvent &e) override { playAt(e.position); }
  void mouseUp(const juce::MouseEvent &) override { release(); }

private:
  bool isScaleTone(int note) const {
    const int pc = (note - static_cast<int>(owner.value("root")) + 120) % 12;
    const auto index = static_cast<size_t>(
        juce::jlimit(0, muthechords::scaleCount - 1, static_cast<int>(owner.value("scale"))));
    const auto &intervals = muthechords::scales[index];
    return std::find(intervals.begin(), intervals.end(), pc) != intervals.end();
  }
  juce::Rectangle<float> keyBounds(int note) const {
    int whites = 0;
    for (int n = 36; n < note; ++n)
      if (!blackKey(n)) ++whites;
    const float width = static_cast<float>(getWidth()) / 21.0f;
    if (blackKey(note))
      return {static_cast<float>(whites) * width - width * 0.31f, 0.0f, width * 0.62f,
              static_cast<float>(getHeight()) * 0.62f};
    return {static_cast<float>(whites) * width, 0.0f, width, static_cast<float>(getHeight())};
  }
  void playAt(juce::Point<float> point) {
    int next = -1;
    for (int pass = 0; pass < 2 && next < 0; ++pass)
      for (int note = 36; note < 72; ++note)
        if (blackKey(note) == (pass == 0) && keyBounds(note).contains(point)) {
          next = note;
          break;
        }
    if (next == held) return;
    release();
    if (next >= 0) {
      held = next;
      owner.processor.previewOn(next);
      repaint();
    }
  }
  void release() {
    if (held >= 0) {
      owner.processor.previewOff();
      held = -1;
      repaint();
    }
  }
  MuTheChordsEditor &owner;
  int held = -1;
};

class MuTheChordsEditor::PatternGraph final : public juce::Component,
                                              public juce::SettableTooltipClient {
public:
  explicit PatternGraph(MuTheChordsEditor &editor) : owner(editor) {
    setTitle("Source pattern notes");
    setComponentID("performanceGraph");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip(previewHelp());
    setDescription("Pattern notes. Drag the handles above the graph to select bars. Left "
                   "and right arrows move the start; Shift plus arrows move the end; Home "
                   "selects the full pattern. Enter or Space toggles preview.");
  }
  void paint(juce::Graphics &g) override {
    g.setColour(graphBackground);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(hasKeyboardFocus(true) ? accent : line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f,
                           hasKeyboardFocus(true) ? 2.0f : 1.0f);
    const auto *source = selected();
    if (source == nullptr || source->events.empty()) {
      g.setColour(muted);
      g.setFont(juce::FontOptions(12.0f));
      g.drawText(source == nullptr ? "Local pattern library unavailable"
                                   : "This pattern has no notes",
                 getLocalBounds(), juce::Justification::centred);
      return;
    }
    updateMetrics();
    const auto area = plot();
    const auto limits = range();
    const double end = length();
    const double bar =
        static_cast<double>(source->numerator) * 4.0 / juce::jmax(1, source->denominator);
    const int bars = juce::jmax(1, static_cast<int>(std::ceil(end / bar)));
    const auto region = owner.processor.loopRegion();
    const int first = juce::jlimit(0, bars - 1, region.first),
              last = region.second > first ? juce::jlimit(first + 1, bars, region.second) : bars;
    const float left =
        area.getX() + static_cast<float>(std::min(end, first * bar) / end) * area.getWidth();
    const float right =
        area.getX() + static_cast<float>(std::min(end, last * bar) / end) * area.getWidth();
    g.setColour(background.withAlpha(.55f));
    g.fillRect(area.withRight(left));
    g.fillRect(area.withLeft(right));
    g.setColour(accent.withAlpha(.35f));
    g.fillRect(juce::Rectangle<float>(left, 1, right - left, 14));
    g.setColour(accent);
    g.fillRoundedRectangle(left - 3, 1, 6, 14, 3);
    g.fillRoundedRectangle(right - 3, 1, 6, 14, 3);
    g.setFont(juce::FontOptions(9.0f));
    const int stride = juce::jmax(1, static_cast<int>(std::ceil(end / bar / 16.0)));
    for (int b = 0; b * bar <= end; b += stride) {
      const float x = area.getX() + static_cast<float>(b * bar / end) * area.getWidth();
      g.setColour(juce::Colour(0xff7890a5));
      g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
      if (x + 35 < getWidth() - 132) {
        g.setColour(muted);
        g.drawText(juce::String(b + 1), juce::roundToInt(x + 3), 2, 32, 12,
                   juce::Justification::left);
      }
    }
    if (end <= 64)
      for (int beat = 1; beat < end; ++beat) {
        g.setColour(juce::Colour(0xff3c5972));
        const float x = area.getX() + static_cast<float>(beat / end) * area.getWidth();
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
      }
    for (int note = limits.first; note <= limits.second; ++note)
      if (note % 12 == 0) {
        const float y = area.getBottom() -
                        (static_cast<float>(note - limits.first) + 0.5f) /
                            static_cast<float>(limits.second - limits.first + 1) * area.getHeight();
        g.setColour(line);
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
      }
    g.setColour(muted);
    g.drawText(noteName(limits.second), 3, 16, 34, 12, juce::Justification::left);
    g.drawText(noteName(limits.first), 3, getHeight() - 17, 34, 12, juce::Justification::left);
    for (const auto &event : source->events) {
      g.setColour(accent.withAlpha(0.3f + 0.7f * juce::jlimit(0, 127, event.velocity) / 127.0f));
      g.fillRoundedRectangle(noteBounds(event), 1.5f);
    }
    if (owner.processor.performanceRunning()) {
      g.setColour(juce::Colour(0xfff0ce8b));
      const float x =
          area.getX() +
          static_cast<float>(juce::jlimit(0.0, 1.0, owner.processor.performancePhase())) *
              area.getWidth();
      g.drawLine(x, area.getY(), x, area.getBottom(), 1.5f);
    }
  }
  void mouseDown(const juce::MouseEvent &event) override {
    if (!event.mods.isLeftButtonDown()) return;
    grabKeyboardFocus();
    if (event.position.y < 17 && event.position.x >= plot().getX() - 4 &&
        event.position.x <= plot().getRight() + 4) {
      if (const auto *p = selected()) {
        const double bar = p->numerator * 4.0 / juce::jmax(1, p->denominator),
                     patternEnd = juce::jmax(1.0, p->length);
        const int bars = juce::jmax(1, static_cast<int>(std::ceil(patternEnd / bar)));
        const auto r = owner.processor.loopRegion();
        const int end = r.second > r.first ? r.second : bars;
        const int boundary = juce::jlimit(0, bars,
                                          juce::roundToInt((event.position.x - plot().getX()) /
                                                           plot().getWidth() * patternEnd / bar));
        draggingStart = std::abs(boundary - r.first) <= std::abs(boundary - end);
        draggingRegion = true;
        moveHandle(event.position.x);
        return;
      }
    }
    togglePreview();
  }
  void mouseDrag(const juce::MouseEvent &event) override {
    if (draggingRegion) moveHandle(event.position.x);
  }
  void mouseUp(const juce::MouseEvent &) override { draggingRegion = false; }
  bool keyPressed(const juce::KeyPress &key) override {
    if (key.isKeyCode(juce::KeyPress::homeKey)) {
      owner.processor.setLoopRegion(0, 0);
      repaint();
      return true;
    }
    if (key.isKeyCode(juce::KeyPress::leftKey) || key.isKeyCode(juce::KeyPress::rightKey)) {
      const auto *p = selected();
      if (!p) return false;
      const double bar = p->numerator * 4.0 / juce::jmax(1, p->denominator);
      const int bars = juce::jmax(1, static_cast<int>(std::ceil(juce::jmax(1.0, p->length) / bar)));
      auto r = owner.processor.loopRegion();
      if (r.second <= r.first) r.second = bars;
      const int d = key.isKeyCode(juce::KeyPress::leftKey) ? -1 : 1;
      if (key.getModifiers().isShiftDown())
        r.second = juce::jlimit(r.first + 1, bars, r.second + d);
      else
        r.first = juce::jlimit(0, r.second - 1, r.first + d);
      owner.processor.setLoopRegion(r.first, r.second);
      repaint();
      return true;
    }
    if (!key.isKeyCode(juce::KeyPress::returnKey) && !key.isKeyCode(juce::KeyPress::spaceKey))
      return false;
    if (!activationKeyDown) {
      activationKeyDown = true;
      togglePreview();
    }
    return true;
  }
  bool keyStateChanged(bool) override {
    if (!juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::returnKey) &&
        !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey))
      activationKeyDown = false;
    return false;
  }
  void focusGained(juce::Component::FocusChangeType) override { repaint(); }
  void focusLost(juce::Component::FocusChangeType) override {
    activationKeyDown = false;
    repaint();
  }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
    class Handler final : public juce::AccessibilityHandler {
    public:
      explicit Handler(PatternGraph &graph)
          : AccessibilityHandler(
                graph, juce::AccessibilityRole::button,
                juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                       [&graph] { graph.togglePreview(); })),
            owner(graph) {}
      juce::String getTitle() const override {
        return (owner.owner.processor.performanceRunning() ||
                owner.owner.processor.hasSoundingOutput())
                   ? "Stop playback"
                   : "Preview pattern";
      }
      juce::String getHelp() const override { return owner.previewHelp(); }

    private:
      PatternGraph &owner;
    };
    return std::make_unique<Handler>(*this);
  }
  void mouseMove(const juce::MouseEvent &e) override {
    if (const auto *source = selected())
      for (auto it = source->events.rbegin(); it != source->events.rend(); ++it)
        if (noteBounds(*it).expanded(1.0f).contains(e.position)) {
          setTooltip(noteName(it->note) + " / beat " + juce::String(it->beat, 2) + " / duration " +
                     juce::String(it->duration, 2) + " / velocity " + juce::String(it->velocity));
          return;
        }
    setTooltip(previewHelp());
  }

private:
  void moveHandle(float x) {
    const auto *p = selected();
    if (!p) return;
    const double bar = p->numerator * 4.0 / juce::jmax(1, p->denominator),
                 end = juce::jmax(1.0, p->length);
    const int bars = juce::jmax(1, static_cast<int>(std::ceil(end / bar)));
    const int b = juce::jlimit(
        0, bars, juce::roundToInt((x - plot().getX()) / plot().getWidth() * end / bar));
    auto r = owner.processor.loopRegion();
    r.first = juce::jlimit(0, bars - 1, r.first);
    if (r.second <= r.first || r.second > bars) r.second = bars;
    if (draggingStart)
      r.first = juce::jlimit(0, r.second - 1, b);
    else
      r.second = juce::jlimit(r.first + 1, bars, b);
    owner.processor.setLoopRegion(r.first, r.second);
    repaint();
  }
  juce::String previewHelp() const {
    if (owner.processor.performanceRunning() || owner.processor.hasSoundingOutput())
      return "Click or press Enter or Space to stop all sounding notes.";
    if (owner.processor.transportPlaying())
      return "Pattern preview is available when the DAW is stopped.";
    if (owner.processor.hostTempo() <= 0) return "Pattern preview needs a tempo from the host.";
    return "Click or press Enter or Space to start or stop pattern playback.";
  }
  void togglePreview() {
    if (owner.processor.performanceRunning() || owner.processor.hasSoundingOutput()) {
      owner.processor.requestPerformancePreview(false);
      repaint();
      return;
    }
    const auto *source = selected();
    if (source == nullptr || source->events.empty() || owner.processor.transportPlaying() ||
        owner.processor.hostTempo() <= 0)
      return;
    owner.processor.requestPerformancePreview(true);
    repaint();
  }
  bool activationKeyDown = false, draggingRegion = false, draggingStart = false;
  const muthechords::Pattern *selected() const {
    const auto &list = owner.processor.patternList();
    const int index = owner.displayedPattern;
    return juce::isPositiveAndBelow(index, static_cast<int>(list.size()))
               ? &list[static_cast<size_t>(index)]
               : nullptr;
  }
  void updateMetrics() {
    lowPitch = 127;
    highPitch = 0;
    patternLength = 1;
    if (const auto *source = selected()) {
      patternLength = juce::jmax(1.0, source->length);
      for (const auto &event : source->events) {
        lowPitch = std::min(lowPitch, event.note);
        highPitch = std::max(highPitch, event.note);
        patternLength = juce::jmax(patternLength, event.beat + event.duration);
      }
    }
    highPitch = juce::jmax(lowPitch + 1, highPitch);
  }
  std::pair<int, int> range() const { return {lowPitch, highPitch}; }
  double length() const { return patternLength; }
  juce::Rectangle<float> plot() const {
    return getLocalBounds()
        .toFloat()
        .withTrimmedLeft(40)
        .withTrimmedRight(8)
        .withTrimmedTop(17)
        .withTrimmedBottom(6);
  }
  juce::Rectangle<float> noteBounds(const muthechords::PatternNote &event) const {
    const auto area = plot();
    const auto limits = range();
    const float row = area.getHeight() / static_cast<float>(limits.second - limits.first + 1);
    return {area.getX() + static_cast<float>(event.beat / length()) * area.getWidth(),
            area.getBottom() - static_cast<float>(event.note - limits.first + 1) * row,
            juce::jmax(1.0f, static_cast<float>(event.duration / length()) * area.getWidth()),
            juce::jlimit(2.0f, 9.0f, row * 0.8f)};
  }
  MuTheChordsEditor &owner;
  int lowPitch = 60, highPitch = 72;
  double patternLength = 4;
};

MuTheChordsEditor::MuTheChordsEditor(MuTheChordsProcessor &p)
    : AudioProcessorEditor(p), processor(p) {
  const int initialWidth = processor.savedEditorWidth();
  theme = std::make_unique<PanelLookAndFeel>();
  setLookAndFeel(theme.get());
  addAndMakeVisible(content);
  content.setInterceptsMouseClicks(false, true);
  const juce::StringArray chords{"Third", "Triad", "7th", "9th", "11th", "13th"};
  juce::StringArray roots, scales, transpositions, notes;
  for (const auto *name : muthechords::pitchNames)
    roots.add(name);
  for (const auto *name : muthechords::scaleNames)
    scales.add(name);
  for (int n = -36; n <= 36; ++n)
    transpositions.add(n > 0 ? "+" + juce::String(n) : juce::String(n));
  for (int n = 0; n < 128; ++n)
    notes.add(noteName(n));
  addCombo(root, rootLabel, "Key", roots, "root");
  addCombo(scale, scaleLabel, "Scale", scales, "scale");
  addCombo(chord, chordLabel, "Chord", chords, "chord");
  juce::StringArray voicings;
  for (const auto *name : muthechords::voicingNames)
    voicings.add(name);
  addCombo(inversion, inversionLabel, "Voicing", voicings, "voicingPreset");
  inversion.onChange = [this] {
    const int selected = inversion.getSelectedId() - 1;
    if (selected < 0) return;
    auto set = [this](const char *id, int v) {
      auto *p = processor.parameters.getParameter(id);
      p->beginChangeGesture();
      p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(v)));
      p->endChangeGesture();
    };
    if (selected <= muthechords::sixthInversion) {
      set("inversion", selected);
      set("voicingPreset", 0);
    } else
      set("voicingPreset", selected - muthechords::sixthInversion);
  };
  addCombo(transpose, transposeLabel, "Transpose", transpositions, "transpose");
  addCombo(low, lowLabel, "From", notes, "low");
  addCombo(high, highLabel, "To", notes, "high");
  addCombo(mapping, mappingLabel, "Mapping", {"Absolute", "Relative"}, "mapping");
  addCombo(outside, outsideLabel, "Outside scale",
           {"Ignore", "Pass note", "Snap down", "Snap up", "Nearest"}, "outside");
  addCombo(pedal, pedalLabel, "Pedal", {"Off", "Hold", "Chord Switch"}, "pedal");
  addCombo(alt, altLabel, "Alt Chord", chords, "alt");
  addCombo(polarity, polarityLabel, "Polarity", {"Normal", "Reversed"}, "polarity");
  addCombo(spelling, spellingLabel, "Note spelling",
           {"Auto", juce::String::fromUTF8("\xe2\x99\xad"), juce::String::fromUTF8("\xe2\x99\xaf")},
           "spelling");
  addCombo(rate, rateLabel, "Rate",
           {juce::String::fromUTF8("×0.25"), juce::String::fromUTF8("×0.5"),
            juce::String::fromUTF8("×0.75"), juce::String::fromUTF8("×1"),
            juce::String::fromUTF8("×1.25"), juce::String::fromUTF8("×1.5"),
            juce::String::fromUTF8("×1.75"), juce::String::fromUTF8("×2")},
           "perfRate");
  addCombo(division, divisionLabel, "Division", {"Straight", "Triplet"}, "perfDivision");
  addCombo(voices, voicesLabel, "Voices", {"Auto", "All", "Pattern"}, "perfVoices");
  addCombo(passing, passingLabel, "Passing notes", {"Off", "Scale"}, "perfPassing");
  addCombo(change, changeLabel, "Chord change", {"Continue", "Restart", "Next beat"}, "perfChange");
  addCombo(transition, transitionLabel, "Note transition", {"Cut", "Retain common", "Let ring"},
           "perfTransition");
  addCombo(playback, playbackLabel, "Playback", {"Held", "One pass", "Continuous"}, "perfPlayback");
  for (auto entry :
       {std::make_pair(&category, &categoryLabel), std::make_pair(&pattern, &patternLabel)}) {
    content.addAndMakeVisible(entry.first);
    content.addAndMakeVisible(entry.second);
    const auto title = entry.first == &category ? "Category" : "Pattern";
    entry.first->setTitle(title);
    entry.second->setText(title, juce::dontSendNotification);
    entry.second->setFont(juce::FontOptions(11.0f));
    entry.second->setColour(juce::Label::textColourId, muted);
  }
  pattern.setComponentID("perfPattern");
  patternLabel.setComponentID("perfPattern");
  category.setComponentID("perfCategory");
  categoryLabel.setComponentID("perfCategory");
  for (const auto &source : processor.patternList())
    categories.addIfNotAlreadyThere(juce::String(source.category));
  category.addItemList(categories, 1);
  category.onChange = [this] {
    if (auto *parameter = processor.parameters.getParameter("perfCategory")) {
      parameter->beginChangeGesture();
      parameter->setValueNotifyingHost(
          parameter->convertTo0to1(static_cast<float>(category.getSelectedItemIndex())));
      parameter->endChangeGesture();
    }
    syncPatternSelectors();
  };
  previousPattern.setButtonText(juce::String::fromUTF8("↑"));
  nextPattern.setButtonText(juce::String::fromUTF8("↓"));
  previousPattern.setTitle("Previous pattern");
  nextPattern.setTitle("Next pattern");
  previousPattern.setName("Previous pattern");
  nextPattern.setName("Next pattern");
  for (auto *button : {&previousPattern, &nextPattern}) {
    content.addAndMakeVisible(button);
    button->setComponentID("perfPattern");
    button->setTooltip(button->getTitle());
    button->setWantsKeyboardFocus(true);
  }
  previousPattern.onClick = [this] {
    const int index = adjacentPattern(-1);
    if (index >= 0) selectPattern(index);
  };
  nextPattern.onClick = [this] {
    const int index = adjacentPattern(1);
    if (index >= 0) selectPattern(index);
  };
  content.addAndMakeVisible(fullLoop);
  fullLoop.setTitle("Full pattern");
  fullLoop.setTooltip("Reset the loop to the full pattern.");
  fullLoop.onClick = [this] {
    processor.setLoopRegion(0, 0);
    patternGraph->repaint();
  };
  content.addAndMakeVisible(loopInfo);
  loopInfo.setColour(juce::Label::textColourId, muted);
  loopInfo.setFont(juce::FontOptions(10.0f));
  loopInfo.setJustificationType(juce::Justification::centredRight);
  for (auto *button : {&resetDefaults, &harmonySync, &importProfile, &exportProfile})
    content.addAndMakeVisible(button);
  resetDefaults.setTooltip("Reset all settings to their defaults and stop "
                           "playback. Sync will turn off.");
  resetDefaults.onClick = [this] {
    processor.resetToDefaults();
    timerCallback();
  };
  harmonySync.setClickingTogglesState(true);
  harmonySync.setComponentID("harmonySync");
  harmonySync.setTitle("Sync");
  harmonySync.setTooltip("Share Key and Scale. All synced instances are equal: changes in any "
                         "instance are shared. The first supplies the initial values.");
  content.addAndMakeVisible(bypass);
  bypass.setClickingTogglesState(true);
  bypass.setComponentID("bypass");
  bypass.setTooltip("Pass MIDI through without processing.");
  buttonAttachments.push_back(
      std::make_unique<ButtonAttachment>(processor.parameters, "bypass", bypass));
  content.addAndMakeVisible(syncInfo);
  syncInfo.setText("All instances equal", juce::dontSendNotification);
  syncInfo.setColour(juce::Label::textColourId, muted);
  syncInfo.setFont(juce::FontOptions(11.0f));
  syncInfo.setJustificationType(juce::Justification::centredRight);
  syncInfo.setTooltip("There is no leader. Key and Scale changes in any synced "
                      "instance are shared with the others.");
  buttonAttachments.push_back(
      std::make_unique<ButtonAttachment>(processor.parameters, "harmonySync", harmonySync));
  importProfile.setTooltip("Import a MuTheChords profile (.chordprofile).");
  exportProfile.setTooltip("Export the current MuTheChords profile (.chordprofile).");
  importProfile.onClick = [this] { chooseProfile(false); };
  exportProfile.onClick = [this] { chooseProfile(true); };
  pattern.onChange = [this] {
    if (pattern.getSelectedId() > 0) selectPattern(pattern.getSelectedId() - 1);
  };
  rate.setTooltip(
      juce::String::fromUTF8("Playback speed multiplier. ×2 halves the pattern duration."));
  change.setTooltip("Continue keeps the pattern position; Restart starts "
                    "immediately; Next beat defers the chord change.");
  transition.setTooltip("Cut ends old notes; Retain common preserves shared "
                        "pitches; Let ring keeps original durations.");
  voices.setTooltip("Auto adapts to chord size; All uses every chord voice; "
                    "Pattern preserves its voice count.");
  spellingLabel.setVisible(false);
  spelling.setTooltip("Note spelling: Auto, flats or sharps. Changes names only, not MIDI.");
  for (auto *button : {&full, &latch})
    content.addAndMakeVisible(button);
  content.addAndMakeVisible(dawSync);
  dawSync.setComponentID("perfDawSync");
  dawSync.setTooltip("Align initial pattern starts to the DAW beat. Off starts "
                     "immediately; tempo still follows the DAW.");
  buttonAttachments.push_back(
      std::make_unique<ButtonAttachment>(processor.parameters, "perfDawSync", dawSync));
  buttonAttachments.push_back(
      std::make_unique<ButtonAttachment>(processor.parameters, "full", full));
  buttonAttachments.push_back(
      std::make_unique<ButtonAttachment>(processor.parameters, "latch", latch));
  for (auto *button : {&chordsTab, &performanceTab, &stop})
    content.addAndMakeVisible(button);
  full.setComponentID("full");
  latch.setComponentID("latch");
  chordsTab.setComponentID("mode");
  performanceTab.setComponentID("mode");
  chordsTab.onClick = [this] { setMode(0); };
  performanceTab.onClick = [this] { setMode(1); };
  stop.onClick = [this] { processor.requestStop(); };
  stop.setTooltip("Stop sounding notes and release held chords.");
  transpose.setTooltip("Shift generated chords in semitones. Applies on the next trigger.");
  mapping.setTooltip("Absolute: scale notes trigger chords. Relative: white "
                     "keys select scale degrees.");
  keyboard = std::make_unique<PreviewKeyboard>(*this);
  content.addAndMakeVisible(*keyboard);
  patternGraph = std::make_unique<PatternGraph>(*this);
  content.addAndMakeVisible(*patternGraph);
  for (auto *label : {&outputTitle, &outputNotes, &patternSummary}) {
    content.addAndMakeVisible(label);
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, foreground);
  }
  outputTitle.setFont(juce::FontOptions(20.0f));
  outputNotes.setFont(juce::FontOptions(13.0f));
  outputTitle.setMinimumHorizontalScale(1.0f);
  outputNotes.setColour(juce::Label::textColourId, accent);
  patternSummary.setFont(juce::FontOptions(10.0f));
  patternSummary.setJustificationType(juce::Justification::left);
  patternSummary.setColour(juce::Label::textColourId, muted);
  setResizable(true, true);
  setResizeLimits(570, 480, 1140, 960);
  getConstrainer()->setFixedAspectRatio(760.0 / 640.0);
  setSize(initialWidth, juce::roundToInt(initialWidth * 640.0 / 760.0));
  timerCallback();
  startTimerHz(25);
}

MuTheChordsEditor::~MuTheChordsEditor() {
  stopTimer();
  keyboard.reset();
  setLookAndFeel(nullptr);
}
float MuTheChordsEditor::value(const char *id) const {
  return processor.parameters.getRawParameterValue(id)->load();
}
void MuTheChordsEditor::addCombo(juce::ComboBox &box, juce::Label &label, const char *title,
                                 const juce::StringArray &items, const char *id) {
  if (juce::String(id) == "voicingPreset") {
    for (int i = 0; i < items.size(); ++i) {
      if (i == 0)
        box.addSectionHeading("Inversions");
      else if (i == muthechords::bassDown)
        box.addSectionHeading("Spread");
      else if (i == muthechords::bassOctave)
        box.addSectionHeading("Octaves");
      box.addItem(items[i], i + 1);
    }
  } else
    box.addItemList(items, 1);
  box.setTitle(title);
  box.setComponentID(id);
  label.setComponentID(id);
  content.addAndMakeVisible(box);
  label.setText(title, juce::dontSendNotification);
  label.setFont(juce::FontOptions(11.0f));
  label.setColour(juce::Label::textColourId, muted);
  content.addAndMakeVisible(label);
  if (juce::String(id) != "voicingPreset")
    comboAttachments.push_back(std::make_unique<ComboAttachment>(processor.parameters, id, box));
}
int MuTheChordsEditor::getControlParameterIndex(juce::Component &component) {
  // Hosts may hit the ComboBox's internal Label rather than the ComboBox
  // itself.
  for (auto *control = &component; control != nullptr && control != this;
       control = control->getParentComponent()) {
    const auto id =
        control == patternGraph.get() ? juce::String("perfPreview") : control->getComponentID();
    if (auto *parameter = processor.parameters.getParameter(id))
      return parameter->getParameterIndex();
  }
  return -1;
}
void MuTheChordsEditor::setMode(int mode) {
  if (auto *parameter = processor.parameters.getParameter("mode")) {
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(mode)));
    parameter->endChangeGesture();
  }
  timerCallback();
}
void MuTheChordsEditor::chooseProfile(bool exporting) {
  if (!importProfile.isEnabled() || !exportProfile.isEnabled()) return;
  importProfile.setEnabled(false);
  exportProfile.setEnabled(false);
  profileChooser = std::make_unique<juce::FileChooser>(
      exporting ? "Export profile" : "Import profile",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
          .getChildFile("MuTheChords.chordprofile"),
      "*.chordprofile", true, false, this);
  const int flags = juce::FileBrowserComponent::canSelectFiles |
                    (exporting ? juce::FileBrowserComponent::saveMode |
                                     juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode);
  profileChooser->launchAsync(flags, [safe = juce::Component::SafePointer<MuTheChordsEditor>(this),
                                      exporting](const juce::FileChooser &chooser) {
    if (safe == nullptr) return;
    const auto selectedFile = chooser.getResult();
    if (selectedFile == juce::File()) {
      safe->importProfile.setEnabled(true);
      safe->exportProfile.setEnabled(true);
      return;
    }
    const auto file = exporting ? selectedFile.withFileExtension("chordprofile") : selectedFile;
    const auto finish = [safe, exporting, file](int accepted) {
      if (safe == nullptr) return;
      safe->importProfile.setEnabled(true);
      safe->exportProfile.setEnabled(true);
      if (accepted == 0) return;
      const auto result =
          exporting ? safe->processor.exportProfile(file) : safe->processor.importProfile(file);
      if (result.failed())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               exporting ? "Could not export profile"
                                                         : "Could not import profile",
                                               result.getErrorMessage(), "OK", safe.getComponent());
      else
        safe->timerCallback();
    };
    // Native save confirms its selected path. Confirm the final profile
    // path separately if adding the extension targets a different existing
    // file.
    if (exporting && file != selectedFile && file.existsAsFile())
      juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Replace profile?",
                                         "Replace the existing profile " + file.getFileName() + "?",
                                         "Replace", "Cancel", safe.getComponent(),
                                         juce::ModalCallbackFunction::create(finish));
    else
      finish(1);
  });
}
int MuTheChordsEditor::adjacentPattern(int direction) const {
  const auto &list = processor.patternList();
  const int selected = processor.selectedPatternIndex();
  if (!juce::isPositiveAndBelow(selected, static_cast<int>(list.size()))) return -1;
  const auto &selectedCategory = list[static_cast<size_t>(selected)].category;
  for (int index = selected + direction;
       juce::isPositiveAndBelow(index, static_cast<int>(list.size())); index += direction)
    if (list[static_cast<size_t>(index)].category == selectedCategory) return index;
  return -1;
}
void MuTheChordsEditor::selectPattern(int index) {
  if (auto *parameter = processor.parameters.getParameter("perfPattern")) {
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
    parameter->endChangeGesture();
  }
  syncPatternSelectors();
}
void MuTheChordsEditor::fillPatterns(const juce::String &selectedCategory, int selectedIndex) {
  pattern.clear(juce::dontSendNotification);
  juce::String previousGroup;
  const auto &list = processor.patternList();
  for (size_t index = 0; index < list.size(); ++index) {
    const auto &source = list[index];
    if (juce::String(source.category) != selectedCategory) continue;
    const juce::String group(source.group);
    if (group != previousGroup) {
      if (group.isNotEmpty()) pattern.addSectionHeading(group);
      previousGroup = group;
    }
    pattern.addItem(juce::String(source.name), static_cast<int>(index) + 1);
  }
  pattern.setSelectedId(selectedIndex + 1, juce::dontSendNotification);
}
void MuTheChordsEditor::syncPatternSelectors() {
  const auto &list = processor.patternList();
  const int selected = processor.selectedPatternIndex();
  previousPattern.setEnabled(adjacentPattern(-1) >= 0);
  nextPattern.setEnabled(adjacentPattern(1) >= 0);
  if (displayedPattern == selected) return;
  displayedPattern = selected;
  if (!juce::isPositiveAndBelow(selected, static_cast<int>(list.size()))) return;
  const auto &source = list[static_cast<size_t>(selected)];
  category.setSelectedId(categories.indexOf(juce::String(source.category)) + 1,
                         juce::dontSendNotification);
  fillPatterns(juce::String(source.category), selected);
  int lowest = 127, highest = 0;
  double end = source.length;
  for (const auto &event : source.events) {
    lowest = std::min(lowest, event.note);
    highest = std::max(highest, event.note);
    end = std::max(end, event.beat + event.duration);
  }
  const double bars = end / (source.numerator * 4.0 / juce::jmax(1, source.denominator));
  auto summary = juce::String(source.numerator) + "/" + juce::String(source.denominator) +
                 juce::String::fromUTF8(" · ") + juce::String(bars, 2) +
                 juce::String::fromUTF8(" bars · ") +
                 juce::String(static_cast<int>(source.events.size())) + " notes";
  if (!source.events.empty())
    summary += juce::String::fromUTF8(" · ") + noteName(lowest) + juce::String::fromUTF8("–") +
               noteName(highest) + juce::String::fromUTF8(" · Source notes");
  patternSummary.setText(summary, juce::dontSendNotification);
  patternGraph->setDescription(juce::String(source.name) + ": " + summary);
}
void MuTheChordsEditor::timerCallback() {
  if (processor.takeEditorSizeRestore()) {
    const int width = processor.savedEditorWidth();
    setSize(width, juce::roundToInt(width * 640.0 / 760.0));
  }
  syncInfo.setVisible(value("harmonySync") > 0.5f);
  const bool modeChanged = performanceMode != (value("mode") > 0.5f);
  performanceMode = value("mode") > 0.5f;
  dawSync.setVisible(performanceMode);
  latch.setEnabled(!performanceMode || value("perfPlayback") < .5f);
  latch.setTooltip(performanceMode && value("perfPlayback") > .5f
                       ? "Latch applies in Held playback mode."
                       : "Keep playing after key release; press the same key to stop.");
  chordsTab.setToggleState(!performanceMode, juce::dontSendNotification);
  performanceTab.setToggleState(performanceMode, juce::dontSendNotification);
  for (juce::Component *component :
       std::initializer_list<juce::Component *>{&low, &lowLabel, &high, &highLabel, &mapping,
                                                &mappingLabel, &outside, &outsideLabel, &full})
    component->setVisible(!performanceMode);
  const bool useAlt = value("pedal") > 1.5f;
  alt.setVisible(useAlt);
  altLabel.setVisible(useAlt);
  low.setEnabled(value("full") < 0.5f);
  high.setEnabled(value("full") < 0.5f);
  for (juce::Component *component : std::initializer_list<juce::Component *>{
           &category,    &categoryLabel, &pattern,         &patternLabel,     &previousPattern,
           &nextPattern, &rate,          &rateLabel,       &division,         &divisionLabel,
           &voices,      &voicesLabel,   &passing,         &passingLabel,     &change,
           &changeLabel, &transition,    &transitionLabel, &playback,         &playbackLabel,
           &fullLoop,    &loopInfo,      &patternSummary,  patternGraph.get()})
    component->setVisible(performanceMode);
  syncPatternSelectors();
  {
    const auto region = processor.loopRegion();
    loopInfo.setText(region.second > region.first
                         ? "Loop: " + juce::String(region.first + 1) + juce::String::fromUTF8("–") +
                               juce::String(region.second)
                         : "Loop: Full",
                     juce::dontSendNotification);
  }
  category.setEnabled(!processor.patternList().empty());
  pattern.setEnabled(!processor.patternList().empty());
  if (modeChanged) resized();
  patternGraph->repaint();
  processor.readDisplaySnapshot(currentDisplay);
  currentChord = currentDisplay.chord;
  const auto notation = static_cast<muthechords::Spelling>(static_cast<int>(value("spelling")));
  // Use the sounding chord's frozen context, not settings edited after its
  // trigger.
  auto displayContext = currentChord;
  if (displayContext.count == 0) {
    displayContext.key = static_cast<int>(value("root"));
    displayContext.scale = static_cast<int>(value("scale"));
  }
  juce::StringArray sounding;
  for (int note = 0; note < 128; ++note)
    if (currentDisplay.notes[static_cast<size_t>(note)])
      sounding.add(juce::String::fromUTF8(
          muthechords::pitchName(note, displayContext, notation, true).c_str()));
  const int input = processor.inputNote();
  juce::String title = "Ready to play";
  if (!sounding.isEmpty())
    title = juce::String(performanceMode ? "Performance" : "Playing") + " / " +
            juce::String(sounding.size()) + (sounding.size() == 1 ? " note" : " notes");
  else if (performanceMode)
    title = processor.patternList().empty() ? "No patterns available" : "Ready for pattern";
  else if (input >= 0 && processor.inputOutside())
    title = "Outside scale / " + noteName(input);
  if (!sounding.isEmpty() && currentChord.count > 0) {
    bool complete = true, solo = false;
    for (int i = 0; i < currentChord.count; ++i)
      if (!currentDisplay.notes[static_cast<size_t>(currentChord.notes[static_cast<size_t>(i)])])
        complete = false;
    for (int n = 0; n < 128; ++n)
      if (currentDisplay.notes[static_cast<size_t>(n)] &&
          std::find(currentChord.notes.begin(), currentChord.notes.begin() + currentChord.count,
                    n) == currentChord.notes.begin() + currentChord.count)
        solo = true;
    if (complete)
      title += " / " +
               juce::String::fromUTF8(muthechords::chordName(currentChord, notation).c_str()) +
               (solo ? " + solo" : "");
  }
  for (auto *box : {&root, &scale, &chord, &low, &high, &outside, &pedal, &alt, &polarity})
    box->setTooltip(box->getText());
  const int maxChordType = std::max(static_cast<int>(value("chord")),
                                    value("pedal") > 1.5f ? static_cast<int>(value("alt")) : 0);
  for (int i = 1; i <= muthechords::sixthInversion; ++i)
    inversion.setItemEnabled(i + 1, i < maxChordType + 2);
  const int preset = static_cast<int>(value("voicingPreset"));
  const int voice = juce::jlimit(0, muthechords::voicingCount - 1,
                                 preset ? muthechords::sixthInversion + preset
                                        : static_cast<int>(value("inversion")));
  inversion.setSelectedId(voice + 1, juce::dontSendNotification);
  inversion.setTooltip(juce::String(muthechords::voicingNames[static_cast<size_t>(voice)]) + ": " +
                       muthechords::voicingDescriptions[static_cast<size_t>(voice)]);
  outputTitle.setText(title, juce::dontSendNotification);
  outputTitle.setTooltip(title);
  outputNotes.setText(sounding.isEmpty() ? juce::String::fromUTF8("\xe2\x80\x94")
                                         : sounding.joinIntoString("   "),
                      juce::dontSendNotification);
  keyboard->repaint();
  repaint();
}
void MuTheChordsEditor::paint(juce::Graphics &g) {
  g.fillAll(background);
  const float zoom =
      std::min(static_cast<float>(getWidth()) / 760.0f, static_cast<float>(getHeight()) / 640.0f);
  g.addTransform(juce::AffineTransform::scale(zoom).translated(
      (getWidth() - 760.0f * zoom) * 0.5f, (getHeight() - 640.0f * zoom) * 0.5f));
  const juce::Font titleFont(juce::FontOptions(18.0f, juce::Font::bold));
  g.setColour(foreground);
  g.setFont(titleFont);
  g.drawText("MuTheChords", 20, 3, 440, 30, juce::Justification::left);
  juce::GlyphArrangement titleGlyphs;
  titleGlyphs.addLineOfText(titleFont, "MuTheChords", 0, 0);
  const int versionX =
      20 + juce::roundToInt(titleGlyphs.getBoundingBox(0, -1, true).getWidth()) + 10;
  g.setColour(muted);
  g.setFont(juce::FontOptions(11.0f));
  g.drawText("v" JucePlugin_VersionString, versionX, 3, 95, 30, juce::Justification::left);
  g.setColour(line);
  g.drawHorizontalLine(36, 0.0f, 760.0f);
  drawGroup(g, {16, 50, 728, 65}, "Harmony");
  g.setColour(line);
  g.drawHorizontalLine(152, 16.0f, 744.0f);
  const auto active = performanceMode ? performanceTab.getBounds() : chordsTab.getBounds();
  g.setColour(accent);
  g.fillRect(active.getX(), 151, active.getWidth(), 2);
  if (performanceMode) {
    drawGroup(g, {16, 168, 728, 160}, "Pattern");
    drawGroup(g, {16, 339, 728, 157}, "Performance");
  } else {
    drawGroup(g, {16, 168, 728, 70}, "Keyboard range");
    drawGroup(g, {16, 257, 728, 70}, "Play");
  }
  drawGroup(g, {16, 508, 728, 48}, "Output", juce::Colour(0xff294b69));
  g.setFont(juce::FontOptions(10.0f));
  g.setColour(muted);
  const auto wheel = [&](float x, const char *name, int value, int maximum) {
    g.setColour(muted);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText(name, juce::Rectangle<float>(x - 10.0f, 571.0f, 32.0f, 10.0f),
               juce::Justification::centred);
    const juce::Rectangle<float> track(x, 582.0f, 12.0f, 45.0f);
    g.setColour(graphBackground);
    g.fillRoundedRectangle(track, 6.0f);
    g.setColour(line);
    g.drawRoundedRectangle(track.reduced(0.5f), 6.0f, 1.0f);
    const float position = value < 0 ? (maximum == 16383 ? 0.5f : 0.0f)
                                     : static_cast<float>(value) / static_cast<float>(maximum);
    const float y = track.getBottom() - 5.0f - position * (track.getHeight() - 10.0f);
    g.setColour(value < 0 ? muted.withAlpha(0.35f) : accent);
    g.fillRoundedRectangle(x + 2.0f, y - 2.0f, 8.0f, 4.0f, 2.0f);
  };
  wheel(27.0f, "BEND", processor.inputPitchBend(), 16383);
  wheel(59.0f, "MOD", processor.inputModWheel(), 127);
}
void MuTheChordsEditor::resized() {
  if (getWidth() >= 570) processor.rememberEditorWidth(getWidth());
  // Keep one logical coordinate system. JUCE applies the host's DPI transform
  // to the whole editor independently of this user-controlled size adjustment.
  const float zoom =
      std::min(static_cast<float>(getWidth()) / 760.0f, static_cast<float>(getHeight()) / 640.0f);
  content.setBounds(0, 0, 760, 640);
  content.setTransform(juce::AffineTransform::scale(zoom).translated(
      (getWidth() - 760.0f * zoom) * 0.5f, (getHeight() - 640.0f * zoom) * 0.5f));
  auto field = [](juce::ComboBox &box, juce::Label &label, int x, int y, int width) {
    label.setBounds(x, y, width, 14);
    box.setBounds(x, y + 16, width, 28);
  };
  field(root, rootLabel, 29, 59, 81);
  field(scale, scaleLabel, 120, 59, 211);
  field(chord, chordLabel, 341, 59, 118);
  field(inversion, inversionLabel, 469, 59, 118);
  field(transpose, transposeLabel, 597, 59, 134);
  resetDefaults.setBounds(222, 5, 70, 26);
  syncInfo.setBounds(310, 5, 162, 26);
  harmonySync.setBounds(482, 5, 78, 26);
  importProfile.setBounds(568, 5, 78, 26);
  exportProfile.setBounds(654, 5, 78, 26);
  chordsTab.setBounds(16, 125, 100, 27);
  performanceTab.setBounds(120, 125, 125, 27);
  bypass.setBounds(654, 121, 78, 27);
  field(low, lowLabel, 29, 179, 100);
  field(high, highLabel, 139, 179, 100);
  full.setBounds(249, 195, 107, 28);
  field(mapping, mappingLabel, 368, 179, 172);
  field(outside, outsideLabel, 550, 179, 181);
  field(pedal, pedalLabel, 29, 268, 160);
  field(polarity, polarityLabel, 199, 268, 113);
  field(alt, altLabel, 322, 268, 147);
  latch.setBounds(500, 284, 100, 28);
  stop.setBounds(621, 284, 110, 28);
  field(category, categoryLabel, 29, 178, 230);
  field(pattern, patternLabel, 271, 178, 388);
  previousPattern.setBounds(671, 194, 28, 28);
  nextPattern.setBounds(703, 194, 28, 28);
  patternGraph->setBounds(29, 228, 702, 76);
  patternSummary.setBounds(29, 306, 520, 16);
  loopInfo.setBounds(552, 306, 105, 20);
  fullLoop.setBounds(667, 306, 64, 20);
  field(rate, rateLabel, 29, 346, 166);
  field(division, divisionLabel, 207, 346, 166);
  field(voices, voicesLabel, 385, 346, 166);
  field(passing, passingLabel, 563, 346, 168);
  field(change, changeLabel, 29, 393, 226);
  field(transition, transitionLabel, 267, 393, 226);
  field(playback, playbackLabel, 505, 393, 226);
  if (performanceMode) {
    field(pedal, pedalLabel, 29, 442, 160);
    field(polarity, polarityLabel, 199, 442, 113);
    field(alt, altLabel, 322, 442, 147);
    latch.setBounds(480, 458, 75, 28);
    dawSync.setBounds(560, 458, 105, 28);
    stop.setBounds(670, 458, 61, 28);
  }
  outputTitle.setBounds(34, 516, 692, 22);
  outputNotes.setBounds(34, 538, 692, 16);
  spelling.setBounds(640, 498, 91, 18);
  keyboard->setBounds(90, 572, 654, 60);
}
