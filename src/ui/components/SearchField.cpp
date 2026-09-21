#include "SearchField.h"

SearchField::SearchField() {
  textEditor.setMultiLine(false);
  textEditor.setReturnKeyStartsNewLine(false);
  textEditor.setScrollbarsShown(false);
  textEditor.setJustification(juce::Justification::centredLeft);
  textEditor.onTextChange = [this] {
    updateTrailingControl();
    if (onTextChange)
      onTextChange(textEditor.getText());
  };
  clearButton.setTooltip(TRANS("Clear search"));
  clearButton.onClick = [this] {
    textEditor.clear();
    textEditor.grabKeyboardFocus();
  };

  refreshThemeColours();
  addAndMakeVisible(textEditor);
  addAndMakeVisible(clearButton);
  updateTrailingControl();
}

SearchField::ClearButton::ClearButton() : juce::Button(TRANS("Clear search")) {
  setWantsKeyboardFocus(false);
}

void SearchField::ClearButton::paintButton(juce::Graphics &g,
                                           const bool isMouseOverButton,
                                           const bool isButtonDown) {
  auto area = getLocalBounds().toFloat().reduced(
      useTouchOptimisedControls() ? 10.0f : 8.0f);
  auto colour = UITheme::textSecondary.withAlpha(
      isButtonDown ? 1.0f : (isMouseOverButton ? 0.9f : 0.7f));
  drawIconGlyph(g, area, IconGlyph::Close, colour, 1.55f);
}

void SearchField::setPlaceholderText(const juce::String &text) {
  textEditor.setTextToShowWhenEmpty(text, UITheme::textSecondary.withAlpha(0.8f));
}

void SearchField::setText(const juce::String &text,
                          const juce::NotificationType notification) {
  textEditor.setText(text, notification);
  updateTrailingControl();
}

juce::String SearchField::getText() const { return textEditor.getText(); }

void SearchField::clear() {
  textEditor.clear();
  updateTrailingControl();
}

void SearchField::setIconGlyph(const IconGlyph glyph) {
  iconGlyph = glyph;
  repaint();
}

void SearchField::setLoading(const bool isLoading) {
  if (loading == isLoading)
    return;
  loading = isLoading;
  if (loading)
    startTimerHz(12);
  else
    stopTimer();
  updateTrailingControl();
  repaint();
}

void SearchField::setOnTextChange(
    std::function<void(const juce::String &)> cb) {
  onTextChange = std::move(cb);
}

void SearchField::refreshThemeColours() {
  applyTextEditorTheme(textEditor,
                       UITheme::panelDark.interpolatedWith(UITheme::cardBg,
                                                           0.28f),
                       UITheme::cardBorder.withAlpha(0.48f));
}

juce::TextEditor &SearchField::editor() { return textEditor; }

const juce::TextEditor &SearchField::editor() const { return textEditor; }

void SearchField::paintOverChildren(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  const auto iconSize = static_cast<float>(iconSizePx);
  auto icon = juce::Rectangle<float>(b.getX() + static_cast<float>(iconInsetX),
                                     b.getCentreY() - iconSize * 0.5f,
                                     iconSize, iconSize);
  drawIconGlyph(g, icon, iconGlyph, UITheme::textSecondary.withAlpha(0.76f),
                1.35f);

  if (!loading)
    return;

  const auto centre = juce::Point<float>(
      b.getRight() - static_cast<float>(useTouchOptimisedControls() ? 22 : 18),
      b.getCentreY());
  g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.28f));
  g.fillEllipse(centre.x - 12.0f, centre.y - 12.0f, 24.0f, 24.0f);
  for (int i = 0; i < 12; ++i) {
    const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 12.0f;
    const auto phase = (i + loadingFrame) % 12;
    const float alpha = 0.18f + 0.82f * static_cast<float>(phase) / 11.0f;
    const auto inner = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * 4.0f;
    const auto outer = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * 8.0f;
    g.setColour(UITheme::accent.withAlpha(alpha));
    g.drawLine(inner.x, inner.y, outer.x, outer.y, 1.8f);
  }
}

void SearchField::resized() {
  textEditor.setBounds(getLocalBounds());
  const int trailingWidth = useTouchOptimisedControls() ? 44 : 36;
  clearButton.setBounds(getLocalBounds().removeFromRight(trailingWidth));
  textEditor.setIndents(iconInsetX + iconSizePx + 10,
                        (loading || clearButton.isVisible()) ? trailingWidth : 10);
}

void SearchField::updateTrailingControl() {
  clearButton.setVisible(!loading && textEditor.getText().isNotEmpty());
  resized();
}

void SearchField::timerCallback() {
  loadingFrame = (loadingFrame + 1) % 12;
  repaint();
}
