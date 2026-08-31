#include "SearchField.h"

SearchField::SearchField() {
  textEditor.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
  textEditor.onTextChange = [this] {
    if (onTextChange)
      onTextChange(textEditor.getText());
  };

  refreshThemeColours();
  addAndMakeVisible(textEditor);
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
}

void SearchField::resized() {
  textEditor.setBounds(getLocalBounds());
  textEditor.setIndents(iconInsetX + iconSizePx + 10, 0);
}
