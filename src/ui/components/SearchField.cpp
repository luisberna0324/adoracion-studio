#include "SearchField.h"

SearchField::SearchField() {
  textEditor.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
  textEditor.setLookAndFeel(&roundedLookAndFeel);
  textEditor.onTextChange = [this] {
    if (onTextChange)
      onTextChange(textEditor.getText());
  };

  refreshThemeColours();
  addAndMakeVisible(textEditor);
}

void SearchField::RoundedLookAndFeel::fillTextEditorBackground(
    juce::Graphics &g, const int width, const int height, juce::TextEditor &editor) {
  const auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                              static_cast<float>(width),
                                              static_cast<float>(height));
  g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
  g.fillRoundedRectangle(bounds, 9.0f);
}

void SearchField::RoundedLookAndFeel::drawTextEditorOutline(
    juce::Graphics &g, const int width, const int height, juce::TextEditor &editor) {
  const auto bounds = juce::Rectangle<float>(0.5f, 0.5f,
                                              static_cast<float>(width - 1),
                                              static_cast<float>(height - 1));
  const auto colour = editor.hasKeyboardFocus(true)
                          ? editor.findColour(juce::TextEditor::focusedOutlineColourId)
                          : editor.findColour(juce::TextEditor::outlineColourId);
  g.setColour(colour);
  g.drawRoundedRectangle(bounds, 9.0f, 1.0f);
}

void SearchField::setPlaceholderText(const juce::String &text) {
  textEditor.setTextToShowWhenEmpty(text, UITheme::textSecondary.withAlpha(0.8f));
}

void SearchField::setText(const juce::String &text,
                          const juce::NotificationType notification) {
  textEditor.setText(text, notification);
}

juce::String SearchField::getText() const { return textEditor.getText(); }

void SearchField::clear() { textEditor.clear(); }

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

  const auto centre = juce::Point<float>(b.getRight() - 18.0f, b.getCentreY());
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
  textEditor.setIndents(iconInsetX + iconSizePx + 10, 0);
}

void SearchField::timerCallback() {
  loadingFrame = (loadingFrame + 1) % 12;
  repaint();
}
