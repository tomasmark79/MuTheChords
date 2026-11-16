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
  void drawTooltip(juce::Graphics &g, const juce::String &text, int width, int height) override {
    const auto bounds =
        juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.fillAll(panel.withAlpha(0.75f));
    g.setColour(line);
    g.drawRect(bounds.reduced(0.5f), 1.0f);

    juce::AttributedString s;
    s.setJustification(juce::Justification::centred);
    s.append(text, juce::FontOptions(13.0f), muted);

    juce::TextLayout tl;
    tl.createLayout(s, bounds.getWidth() - 8.0f);
    tl.draw(g, bounds.reduced(4.0f));
  }
  void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override {
    // Leave a dedicated arrow gutter; Label draws ellipses within the text
    // area without horizontally squishing glyphs.
    label.setBounds(8, 1, juce::jmax(0, box.getWidth() - 30), box.getHeight() - 2);
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setFont(getComboBoxFont(box));
    label.setMinimumHorizontalScale(1.0f);
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
    const float x = static_cast<float>(width) - 14.0f, y = static_cast<float>(height) / 2.0f;
    arrow.startNewSubPath(x - 3.0f, y - 2.0f);
    arrow.lineTo(x, y + 1.0f);
    arrow.lineTo(x + 3.0f, y - 2.0f);
    g.setColour(muted);
    g.strokePath(arrow, juce::PathStrokeType(1.4f));
  }
  void drawButtonBackground(juce::Graphics &g, juce::Button &button, const juce::Colour &,
                            bool highlighted, bool) override {
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float alpha = button.isEnabled() ? 1.0f : 0.45f;
    g.setColour((button.getToggleState() ? panel : control).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour((button.isEnabled() && (highlighted || button.hasKeyboardFocus(true))
                     ? accent
                     : controlOutline)
                    .withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
  }
  void drawButtonText(juce::Graphics &g, juce::TextButton &button, bool, bool) override {
    const auto colour = button.getToggleState() ? accent : foreground;
    g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 1),
                     juce::Justification::centred, 1, 1.0f);
  }
  void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button,
                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
    const float fontSize = 13.0f;
    const float tickWidth = fontSize * 1.1f;
    drawTickBox(g, button, 4.0f, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
                tickWidth, tickWidth, button.getToggleState(), button.isEnabled(),
                shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    g.setColour(button.findColour(juce::ToggleButton::textColourId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
    g.setFont(juce::FontOptions(fontSize));
    const juce::Rectangle<int> textBounds(juce::roundToInt(4.0f + tickWidth + 6.0f), 0,
                                          button.getWidth() - juce::roundToInt(tickWidth + 12.0f),
                                          button.getHeight());
    g.drawFittedText(button.getButtonText(), textBounds, juce::Justification::centredLeft, 1, 1.0f);
  }
};

class MuTheChordsEditor::PreviewKeyboard final : public juce::Component {
public:
  explicit PreviewKeyboard(MuTheChordsEditor &editor) : owner(editor) {
    updateTitle();
    setDescription("Click and hold a key to preview the current harmony.");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  ~PreviewKeyboard() override { release(); }
  int baseNote() const { return base; }
  void resetOctave() {
    release();
    if (base != 36) {
      base = 36;
      updateTitle();
      repaint();
    }
  }
  void shiftOctave(int delta) {
    const int next = juce::jlimit(0, 84, base + delta * 12);
    if (next == base) return;
    release();
    base = next;
    updateTitle();
    repaint();
  }
  void paint(juce::Graphics &g) override {
    for (int pass = 0; pass < 2; ++pass)
      for (int note = base; note < base + 36; ++note) {
        const bool black = blackKey(note);
        if (black != (pass == 1)) continue;
        auto bounds = keyBounds(note);
        const bool full = owner.value("full") > 0.5f;
        const bool inRange = full || (note >= owner.value("low") && note <= owner.value("high"));
        const bool scaleTone = isScaleTone(note);
        const bool activeKey =
            inRange ? (owner.value("mapping") > 0.5f ? !black : scaleTone) : scaleTone;
        const bool input = owner.processor.inputNote() == note;
        const bool invalid = input && owner.processor.inputOutside();
        const bool sounding = owner.processor.outputNote(note);
        juce::Colour fill = black ? juce::Colour(0xff284260) : juce::Colour(0xff2a4564);
        if (sounding) fill = accent;
        if (input) fill = juce::Colour(0xffd58f91);
        if (invalid) fill = juce::Colour(0xff737b80);
        const auto rect = bounds.reduced(1.0f);
        g.setColour(fill);
        g.fillRoundedRectangle(rect, 3.0f);
        const juce::Colour outline = input && !invalid ? juce::Colour(0xffb8666d)
                                     : sounding        ? juce::Colour(0xffdff6d8)
                                     : activeKey ? (inRange ? accent : juce::Colour(0xff93b3cf))
                                                 : line.withAlpha(0.32f);
        g.setColour(outline);
        g.drawRoundedRectangle(rect, 3.0f, (activeKey || sounding || input) ? 1.6f : 1.0f);
        if (invalid) {
          g.setColour(juce::Colours::white);
          g.setFont(juce::FontOptions(13.0f));
          g.drawText("x", bounds.withY(8.0f).withHeight(16.0f), juce::Justification::centred);
        }
        if (!black) {
          g.setColour(sounding || (input && !invalid) ? juce::Colour(0xff172b43)
                      : activeKey ? (inRange ? accent : foreground.withAlpha(0.85f))
                                  : muted.withAlpha(0.35f));
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
  void updateTitle() {
    setTitle("Preview keyboard " + noteName(base) + " to " + noteName(base + 35));
  }
  bool isScaleTone(int note) const {
    const int pc = (note - static_cast<int>(owner.value("root")) + 120) % 12;
    const auto index = static_cast<size_t>(
        juce::jlimit(0, muthechords::scaleCount - 1, static_cast<int>(owner.value("scale"))));
    const auto &intervals = muthechords::scales[index];
    return std::find(intervals.begin(), intervals.end(), pc) != intervals.end();
  }
  juce::Rectangle<float> keyBounds(int note) const {
    int whites = 0;
    for (int n = base; n < note; ++n)
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
      for (int note = base; note < base + 36; ++note)
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
  int base = 36, held = -1;
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
  void enablementChanged() override {
    if (!isEnabled()) {
      draggingRegion = false;
      activationKeyDown = false;
    }
    setMouseCursor(isEnabled() ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::NormalCursor);
    repaint();
  }
  void paint(juce::Graphics &g) override {
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    g.setColour(graphBackground.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour((isEnabled() && hasKeyboardFocus(true) ? accent : line).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f,
                           isEnabled() && hasKeyboardFocus(true) ? 2.0f : 1.0f);
    const auto *source = selected();
    if (source == nullptr || source->events.empty()) {
      g.setColour(muted.withMultipliedAlpha(alpha));
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
    g.setColour(background.withAlpha(.55f * alpha));
    g.fillRect(area.withRight(left));
    g.fillRect(area.withLeft(right));
    g.setColour(accent.withAlpha(.35f * alpha));
    g.fillRect(juce::Rectangle<float>(left, 1, right - left, 14));
    g.setColour(accent.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(left - 3, 1, 6, 14, 3);
    g.fillRoundedRectangle(right - 3, 1, 6, 14, 3);
    g.setFont(juce::FontOptions(9.0f));
    const int stride = juce::jmax(1, static_cast<int>(std::ceil(end / bar / 16.0)));
    for (int b = 0; b * bar <= end; b += stride) {
      const float x = area.getX() + static_cast<float>(b * bar / end) * area.getWidth();
      g.setColour(juce::Colour(0xff7890a5).withMultipliedAlpha(alpha));
      g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
      if (x + 35 < getWidth() - 132) {
        g.setColour(muted.withMultipliedAlpha(alpha));
        g.drawText(juce::String(b + 1), juce::roundToInt(x + 3), 2, 32, 12,
                   juce::Justification::left);
      }
    }
    if (end <= 64)
      for (int beat = 1; beat < end; ++beat) {
        g.setColour(juce::Colour(0xff3c5972).withMultipliedAlpha(alpha));
        const float x = area.getX() + static_cast<float>(beat / end) * area.getWidth();
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
      }
    for (int note = limits.first; note <= limits.second; ++note)
      if (note % 12 == 0) {
        const float y = area.getBottom() -
                        (static_cast<float>(note - limits.first) + 0.5f) /
                            static_cast<float>(limits.second - limits.first + 1) * area.getHeight();
        g.setColour(line.withMultipliedAlpha(alpha));
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
      }
    g.setColour(muted.withMultipliedAlpha(alpha));
    g.drawText(noteName(limits.second), 3, 16, 34, 12, juce::Justification::left);
    g.drawText(noteName(limits.first), 3, getHeight() - 17, 34, 12, juce::Justification::left);
    for (const auto &event : source->events) {
      g.setColour(
          accent.withAlpha((0.3f + 0.7f * juce::jlimit(0, 127, event.velocity) / 127.0f) * alpha));
      g.fillRoundedRectangle(noteBounds(event), 1.5f);
    }
    if (owner.processor.performanceRunning()) {
      g.setColour(juce::Colour(0xfff0ce8b).withMultipliedAlpha(alpha));
      const float x =
          area.getX() +
          static_cast<float>(juce::jlimit(0.0, 1.0, owner.processor.performancePhase())) *
              area.getWidth();
      g.drawLine(x, area.getY(), x, area.getBottom(), 1.5f);
    }
  }
  void mouseDown(const juce::MouseEvent &event) override {
    if (!isEnabled() || !event.mods.isLeftButtonDown()) return;
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
    if (isEnabled() && draggingRegion) moveHandle(event.position.x);
  }
  void mouseUp(const juce::MouseEvent &) override { draggingRegion = false; }
  bool keyPressed(const juce::KeyPress &key) override {
    if (!isEnabled()) return false;
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
  addCombo(mode, modeLabel, "Mode", {"Chords", "Performance"}, "mode");
  addCombo(strum, strumLabel, "Strum", {"Off", "Up Fast", "Up Slow", "Down Fast", "Down Slow"},
           "strum");
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
    entry.second->setBorderSize(juce::BorderSize<int>(0));
    entry.second->setFont(juce::FontOptions(11.0f));
    entry.second->setMinimumHorizontalScale(1.0f);
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
  loopInfo.setBorderSize(juce::BorderSize<int>(0));
  loopInfo.setColour(juce::Label::textColourId, muted);
  loopInfo.setFont(juce::FontOptions(11.0f));
  loopInfo.setMinimumHorizontalScale(1.0f);
  loopInfo.setJustificationType(juce::Justification::centredRight);
  for (auto *button : {&resetDefaults, &harmonySync, &importProfile, &exportProfile})
    content.addAndMakeVisible(button);
  resetDefaults.setTooltip("Reset all settings to their defaults and stop "
                           "playback. Sync will turn off.");
  resetDefaults.onClick = [this] {
    keyboard->resetOctave();
    displayedPattern = -1;
    chordHistory.clear();
    chordTrail.setText({}, juce::dontSendNotification);
    chordTrail.setTooltip({});
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
  syncInfo.setBorderSize(juce::BorderSize<int>(0));
  syncInfo.setColour(juce::Label::textColourId, muted);
  syncInfo.setFont(juce::FontOptions(11.0f));
  syncInfo.setMinimumHorizontalScale(1.0f);
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
  content.addAndMakeVisible(stop);
  full.setComponentID("full");
  latch.setComponentID("latch");
  stop.onClick = [this] { processor.requestStop(); };
  stop.setTooltip("Stop sounding notes and release held chords.");
  mode.setTooltip("Chords plays block harmony; Performance drives rhythmic patterns.");
  strum.setTooltip("Spread chord note attacks across a few milliseconds in Chords mode.");
  transpose.setTooltip("Shift generated chords in semitones. Applies on the next trigger.");
  mapping.setTooltip("Absolute: scale notes trigger chords. Relative: white "
                     "keys select scale degrees.");
  keyboard = std::make_unique<PreviewKeyboard>(*this);
  content.addAndMakeVisible(*keyboard);
  keyboardDown.setButtonText(juce::String::fromUTF8("−"));
  keyboardUp.setButtonText("+");
  keyboardDown.setTitle("Keyboard octave down");
  keyboardUp.setTitle("Keyboard octave up");
  keyboardDown.setName("Keyboard octave down");
  keyboardUp.setName("Keyboard octave up");
  for (auto *button : {&keyboardDown, &keyboardUp}) {
    content.addAndMakeVisible(button);
    button->setTooltip(button->getTitle());
    button->setWantsKeyboardFocus(true);
  }
  keyboardDown.onClick = [this] {
    keyboard->shiftOctave(-1);
    timerCallback();
  };
  keyboardUp.onClick = [this] {
    keyboard->shiftOctave(1);
    timerCallback();
  };
  patternGraph = std::make_unique<PatternGraph>(*this);
  content.addAndMakeVisible(*patternGraph);
  content.addAndMakeVisible(compactButton);
  compactButton.setTooltip("Toggle compact output-only view");
  compactButton.onClick = [this] { setCompactMode(!compactMode); };
  for (auto *label : {&outputTitle, &outputNotes, &chordTrail, &patternSummary}) {
    content.addAndMakeVisible(label);
    label->setBorderSize(juce::BorderSize<int>(0));
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, foreground);
    label->setMinimumHorizontalScale(1.0f);
  }
  outputTitle.setFont(juce::FontOptions(19.0f));
  outputNotes.setFont(juce::FontOptions(13.0f));
  chordTrail.setFont(juce::FontOptions(11.5f));
  outputTitle.setMinimumHorizontalScale(1.0f);
  outputNotes.setColour(juce::Label::textColourId, accent);
  chordTrail.setColour(juce::Label::textColourId, foreground.withAlpha(0.65f));
  patternSummary.setFont(juce::FontOptions(11.0f));
  patternSummary.setJustificationType(juce::Justification::centredLeft);
  patternSummary.setColour(juce::Label::textColourId, muted);
  setResizable(true, true);
  setResizeLimits(570, 540, 1140, 1080);
  getConstrainer()->setFixedAspectRatio(760.0 / 720.0);
  setSize(initialWidth, juce::roundToInt(initialWidth * 720.0 / 760.0));
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
  label.setBorderSize(juce::BorderSize<int>(0));
  label.setFont(juce::FontOptions(11.0f));
  label.setMinimumHorizontalScale(1.0f);
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
  previousPattern.setEnabled(performanceMode && adjacentPattern(-1) >= 0);
  nextPattern.setEnabled(performanceMode && adjacentPattern(1) >= 0);
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
    setSize(width, juce::roundToInt(width * 720.0 / 760.0));
  }
  syncInfo.setVisible(value("harmonySync") > 0.5f);
  performanceMode = value("mode") > 0.5f;
  strum.setEnabled(!performanceMode);
  strumLabel.setEnabled(!performanceMode);
  dawSync.setEnabled(performanceMode);
  keyboardDown.setEnabled(keyboard->baseNote() > 0);
  keyboardUp.setEnabled(keyboard->baseNote() < 84);
  latch.setEnabled(!performanceMode || value("perfPlayback") < .5f);
  latch.setTooltip(performanceMode && value("perfPlayback") > .5f
                       ? "Latch applies in Held playback mode."
                       : "Keep playing after key release; press the same key to stop.");
  const bool useAlt = value("pedal") > 1.5f;
  alt.setVisible(useAlt);
  altLabel.setVisible(useAlt);
  low.setEnabled(value("full") < 0.5f);
  high.setEnabled(value("full") < 0.5f);
  for (juce::Component *component : std::initializer_list<juce::Component *>{
           &rate, &rateLabel, &division, &divisionLabel, &voices, &voicesLabel, &passing,
           &passingLabel, &change, &changeLabel, &transition, &transitionLabel, &playback,
           &playbackLabel, &fullLoop, &loopInfo, &patternSummary, patternGraph.get()})
    component->setEnabled(performanceMode);
  syncPatternSelectors();
  {
    const auto region = processor.loopRegion();
    loopInfo.setText(region.second > region.first
                         ? "Loop: " + juce::String(region.first + 1) + juce::String::fromUTF8("–") +
                               juce::String(region.second)
                         : "Loop: Full",
                     juce::dontSendNotification);
  }
  const bool hasPatterns = !processor.patternList().empty();
  category.setEnabled(performanceMode && hasPatterns);
  categoryLabel.setEnabled(performanceMode && hasPatterns);
  pattern.setEnabled(performanceMode && hasPatterns);
  patternLabel.setEnabled(performanceMode && hasPatterns);
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
    title = "";
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
    if (complete) {
      const auto chordSymbol =
          juce::String::fromUTF8(muthechords::chordName(currentChord, notation).c_str());
      title = chordSymbol + (solo ? " + solo" : "");
      if (chordSymbol.isNotEmpty() &&
          (chordHistory.isEmpty() ||
           chordHistory.getReference(chordHistory.size() - 1) != chordSymbol)) {
        chordHistory.add(chordSymbol);
        while (chordHistory.size() > 8)
          chordHistory.remove(0);
        const auto trailText =
            chordHistory.joinIntoString(juce::String::fromUTF8(" \xe2\x86\x92 "));
        chordTrail.setText(trailText, juce::dontSendNotification);
        chordTrail.setTooltip(trailText);
      }
    }
  }
  for (auto *box :
       {&root, &scale, &chord, &low, &high, &outside, &mode, &strum, &pedal, &alt, &polarity})
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
void MuTheChordsEditor::setCompactMode(bool compact) {
  compactMode = compact;
  compactButton.setButtonText(compactMode ? "Full" : "Mini");
  for (auto *child : content.getChildren())
    child->setVisible(!compactMode || child == &outputTitle || child == &outputNotes ||
                      child == &chordTrail || child == &compactButton);
  const double logicalHeight = compactMode ? 76.0 : 720.0;
  const int currentWidth = std::max(570, getWidth());
  getConstrainer()->setFixedAspectRatio(760.0 / logicalHeight);
  if (compactMode)
    setResizeLimits(570, 57, 1140, 114);
  else
    setResizeLimits(570, 540, 1140, 1080);
  setSize(currentWidth, juce::roundToInt(currentWidth * logicalHeight / 760.0));
  resized();
  repaint();
}

void MuTheChordsEditor::paint(juce::Graphics &g) {
  g.fillAll(background);
  const float logicalHeight = compactMode ? 76.0f : 720.0f;
  const float zoom = std::min(static_cast<float>(getWidth()) / 760.0f,
                              static_cast<float>(getHeight()) / logicalHeight);
  g.addTransform(juce::AffineTransform::scale(zoom).translated(
      (getWidth() - 760.0f * zoom) * 0.5f, (getHeight() - logicalHeight * zoom) * 0.5f));
  if (compactMode) {
    drawGroup(g, {16, 6, 728, 64}, "Output", juce::Colour(0xff294b69));
    return;
  }
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
  drawGroup(g, {16, 129, 728, 70}, "Keyboard range");
  drawGroup(g, {16, 213, 728, 70}, "Play");
  drawGroup(g, {16, 297, 728, 160}, "Pattern");
  drawGroup(g, {16, 471, 728, 105}, "Performance");
  drawGroup(g, {16, 588, 728, 60}, "Output", juce::Colour(0xff294b69));
  g.setFont(juce::FontOptions(10.0f));
  g.setColour(muted);
  const auto wheel = [&](float x, const char *name, int value, int maximum) {
    g.setColour(muted);
    g.setFont(juce::FontOptions(9.0f));
    g.drawText(name, juce::Rectangle<float>(x - 10.0f, 651.0f, 32.0f, 10.0f),
               juce::Justification::centred);
    const juce::Rectangle<float> track(x, 662.0f, 12.0f, 45.0f);
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
  wheel(21.0f, "BEND", processor.inputPitchBend(), 16383);
  wheel(46.0f, "MOD", processor.inputModWheel(), 127);
  g.setColour(muted);
  g.setFont(juce::FontOptions(9.0f));
  g.drawText("OCT", juce::Rectangle<float>(63.0f, 651.0f, 28.0f, 10.0f),
             juce::Justification::centred);
}
void MuTheChordsEditor::resized() {
  if (getWidth() >= 570) processor.rememberEditorWidth(getWidth());
  // Keep one logical coordinate system. JUCE applies the host's DPI transform
  // to the whole editor independently of this user-controlled size adjustment.
  const float logicalHeight = compactMode ? 76.0f : 720.0f;
  const float zoom = std::min(static_cast<float>(getWidth()) / 760.0f,
                              static_cast<float>(getHeight()) / logicalHeight);
  content.setBounds(0, 0, 760, static_cast<int>(logicalHeight));
  content.setTransform(juce::AffineTransform::scale(zoom).translated(
      (getWidth() - 760.0f * zoom) * 0.5f, (getHeight() - logicalHeight * zoom) * 0.5f));
  if (compactMode) {
    compactButton.setBounds(29, 14, 64, 24);
    outputTitle.setBounds(118, 10, 524, 22);
    outputNotes.setBounds(118, 33, 524, 16);
    chordTrail.setBounds(29, 52, 702, 15);
    return;
  }
  auto field = [](juce::ComboBox &box, juce::Label &label, int x, int y, int width) {
    label.setBounds(x, y, width, 14);
    box.setBounds(x, y + 16, width, 28);
  };
  field(root, rootLabel, 29, 59, 81);
  field(scale, scaleLabel, 120, 59, 211);
  field(chord, chordLabel, 341, 59, 118);
  field(inversion, inversionLabel, 469, 59, 118);
  field(transpose, transposeLabel, 597, 59, 134);
  resetDefaults.setBounds(212, 5, 68, 26);
  syncInfo.setBounds(286, 5, 134, 26);
  harmonySync.setBounds(426, 5, 72, 26);
  importProfile.setBounds(504, 5, 74, 26);
  exportProfile.setBounds(584, 5, 74, 26);
  bypass.setBounds(664, 5, 67, 26);
  field(low, lowLabel, 29, 140, 100);
  field(high, highLabel, 139, 140, 100);
  full.setBounds(249, 156, 107, 28);
  field(mapping, mappingLabel, 368, 140, 172);
  field(outside, outsideLabel, 550, 140, 181);
  field(mode, modeLabel, 29, 224, 114);
  field(strum, strumLabel, 151, 224, 108);
  field(pedal, pedalLabel, 267, 224, 124);
  field(polarity, polarityLabel, 399, 224, 96);
  field(alt, altLabel, 503, 224, 72);
  latch.setBounds(583, 240, 62, 28);
  stop.setBounds(653, 240, 78, 28);
  field(category, categoryLabel, 29, 307, 195);
  field(pattern, patternLabel, 232, 307, 338);
  previousPattern.setBounds(578, 323, 28, 28);
  nextPattern.setBounds(610, 323, 28, 28);
  dawSync.setBounds(646, 323, 85, 28);
  patternGraph->setBounds(29, 357, 702, 76);
  patternSummary.setBounds(29, 434, 520, 20);
  loopInfo.setBounds(552, 434, 105, 20);
  fullLoop.setBounds(667, 434, 64, 20);
  field(rate, rateLabel, 29, 478, 166);
  field(division, divisionLabel, 207, 478, 166);
  field(voices, voicesLabel, 385, 478, 166);
  field(passing, passingLabel, 563, 478, 168);
  field(change, changeLabel, 29, 525, 226);
  field(transition, transitionLabel, 267, 525, 226);
  field(playback, playbackLabel, 505, 525, 226);
  compactButton.setBounds(29, 598, 64, 28);
  outputTitle.setBounds(118, 593, 524, 20);
  outputNotes.setBounds(118, 613, 524, 15);
  chordTrail.setBounds(29, 630, 702, 15);
  spelling.setBounds(644, 598, 87, 28);
  keyboardUp.setBounds(66, 662, 22, 21);
  keyboardDown.setBounds(66, 686, 22, 21);
  keyboard->setBounds(96, 652, 648, 60);
}
