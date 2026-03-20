#include "SettingsPanel.h"

SettingsPanel::SettingsPanel(ThemePreset current, ThemeChanged cb,
                             CloseRequested closeCb)
    : onThemeChanged(std::move(cb)), onClose(std::move(closeCb)) {
  title.setText("Settings", juce::dontSendNotification);
  title.setJustificationType(juce::Justification::centredLeft);
  title.setFont(juce::FontOptions(16.0f).withKerningFactor(0.04f));
  addAndMakeVisible(title);

  themeLabel.setText("Skin", juce::dontSendNotification);
  themeLabel.setJustificationType(juce::Justification::centredLeft);
  themeLabel.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(themeLabel);

  themeBox.addItem("Default", static_cast<int>(ThemePreset::Default));
  themeBox.addItem("Spotify", static_cast<int>(ThemePreset::Spotify));
  themeBox.addItem("Ableton", static_cast<int>(ThemePreset::Ableton));
  themeBox.setSelectedId(static_cast<int>(current));
  themeBox.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(themeBox);

  closeButton.setButtonText("CLOSE");
  closeButton.setClickingTogglesState(false);
  closeButton.setViewportIgnoreDragFlag(true);
  closeButton.onClick = [this] { closeSelf(); };
  addAndMakeVisible(closeButton);

  themeBox.onChange = [this] {
    const auto id = themeBox.getSelectedId();
    if (id <= 0 || !onThemeChanged)
      return;
    onThemeChanged(static_cast<ThemePreset>(id));
  };
}

void SettingsPanel::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(b, 6.0f);
  g.setColour(UITheme::cardBorder);
  g.drawRoundedRectangle(b, 6.0f, 1.0f);
}

void SettingsPanel::resized() {
  auto area = getLocalBounds().reduced(16);
  title.setBounds(area.removeFromTop(28));
  area.removeFromTop(10);
  auto row = area.removeFromTop(26);
  themeLabel.setBounds(row.removeFromLeft(60));
  themeBox.setBounds(row);
  area.removeFromTop(14);
  closeButton.setBounds(area.removeFromTop(28).removeFromRight(90));
}

void SettingsPanel::refreshThemeColours() {
  title.setColour(juce::Label::textColourId, UITheme::textPrimary);
  themeLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  themeBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  themeBox.setColour(juce::ComboBox::outlineColourId,
                     UITheme::cardBorder.withAlpha(0.65f));
  themeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  themeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  closeButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  closeButton.setColour(juce::TextButton::buttonOnColourId,
                        UITheme::cardBgPressed);
  closeButton.setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  closeButton.setColour(juce::TextButton::textColourOnId,
                        UITheme::textPrimary);
  repaint();
}

void SettingsPanel::closeSelf() {
  if (onClose)
    onClose();
  if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
    dw->exitModalState(0);
    dw->setVisible(false);
  }
}
