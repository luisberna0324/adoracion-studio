#include "MixerFaderPanel.h"

#include "../../PluginEditor.h"
#include "../../PluginProcessor.h"

MixerFaderPanel::MixerFaderPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                                 MTControlsAudioProcessor &processorRef)
    : lookAndFeel(lookAndFeelRef), processor(processorRef) {
  masterStrip = std::make_unique<ChannelStripCard>(
      "MASTER", lookAndFeel,
      [this] {
        float l = 0.0f;
        float r = 0.0f;
        processor.getOutputMeterLevels(l, r);
        return std::pair<float, float>{l, r};
      },
      false);
  masterStrip->setCardBgColour(UITheme::cardBgMaster);
  masterStrip->setChannelBandColours(UITheme::accentMaster, UITheme::onAccent);
  masterStrip->setSoloEnabled(false);
  masterStrip->setSoloVisible(false);
  masterStrip->setGainVisible(true);
  masterStrip->setMuteAlignedToFader(true);
  masterStrip->setVolume(processor.getMasterVolume());
  masterStrip->setGain(processor.getMasterGain());
  masterStrip->setMuteState(processor.isMasterMuted());
  masterStrip->setOnVolumeChanged([this](float value) {
    processor.setMasterVolume(value);
  });
  masterStrip->setOnGainChanged([this](float value) {
    processor.setMasterGain(value);
  });
  masterStrip->setOnMuteChanged([this](bool muted) {
    processor.setMasterMute(muted);
  });
  addAndMakeVisible(*masterStrip);

  configureTitleLabel(titleLabel, "ALL FADER");
  configureTitleLabel(masterLabel, "MASTER LEVEL");
  configureButton(upButton, "VOL +");
  configureButton(downButton, "VOL -");

  upButton.onClick = [this] {
    if (onAdjust)
      onAdjust(step);
  };
  downButton.onClick = [this] {
    if (onAdjust)
      onAdjust(-step);
  };

  addAndMakeVisible(titleLabel);
  addAndMakeVisible(masterLabel);
  addAndMakeVisible(upButton);
  addAndMakeVisible(downButton);
  refreshThemeColours();
}

MixerFaderPanel::~MixerFaderPanel() { clearLookAndFeel(); }

void MixerFaderPanel::setOnAdjust(AdjustCallback cb) {
  onAdjust = std::move(cb);
}

void MixerFaderPanel::setSongBpm(double) {}

void MixerFaderPanel::refreshThemeColours() {
  configureTitleLabel(titleLabel, "ALL FADER");
  configureTitleLabel(masterLabel, "MASTER LEVEL");
  configureButton(upButton, "VOL +");
  configureButton(downButton, "VOL -");

  if (masterStrip) {
    masterStrip->setCardBgColour(UITheme::cardBgMaster);
    masterStrip->setChannelBandColours(UITheme::accentMaster,
                                       UITheme::onAccent);
    masterStrip->refreshThemeColours();
  }
  repaint();
}

void MixerFaderPanel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBgMaster.withAlpha(0.9f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.7f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  auto drawSection = [&g](const juce::Rectangle<int> &section) {
    if (section.isEmpty())
      return;
    auto b = section.toFloat().reduced(2.0f, 2.0f);
    juce::ColourGradient grad(UITheme::cardBg.withAlpha(0.28f),
                              b.getCentreX(), b.getY(),
                              UITheme::cardBgHover.withAlpha(0.38f),
                              b.getCentreX(), b.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.25f));
    g.drawLine(b.getX() + 6.0f, b.getY(), b.getRight() - 6.0f, b.getY(), 1.0f);
  };

  drawSection(allFadersSection);
  drawSection(masterSection);
}

void MixerFaderPanel::resized() {
  auto area = getLocalBounds().reduced(8, 8);
  const auto stripLayout = ChannelStripCard::Layout{};
  const int stripW = stripLayout.width;
  const int stripH = stripLayout.height;

  const int titleH = 18;
  const int buttonH = 22;
  const int gap = 4;
  const int sectionGap = 10;

  const int sectionW = stripW;
  const int sectionX = area.getX() + (area.getWidth() - sectionW) / 2;
  int y = area.getY() + 4;
  const int allTop = y;

  titleLabel.setBounds(sectionX, y, sectionW, titleH);
  y += titleH + gap;

  const int buttonW = sectionW;
  upButton.setBounds(sectionX, y, buttonW, buttonH);
  y += buttonH + gap;
  downButton.setBounds(sectionX, y, buttonW, buttonH);
  y += buttonH + sectionGap;
  const int allBottom = y - (sectionGap / 2);

  const int masterTitleH = titleH;
  const int masterTitleGap = gap;
  const int masterTop =
      area.getBottom() - stripH - masterTitleH - masterTitleGap;
  masterLabel.setBounds(sectionX, masterTop, sectionW, masterTitleH);

  const int stripX = area.getX() + (area.getWidth() - stripW) / 2;
  const int stripY = masterTop + masterTitleH + masterTitleGap;
  masterStrip->setBounds(stripX, stripY, stripW, stripH);

  allFadersSection = juce::Rectangle<int>(area.getX(), allTop, area.getWidth(),
                                         juce::jmax(0, allBottom - allTop));
  masterSection = juce::Rectangle<int>(
      area.getX(), masterTop, area.getWidth(),
      juce::jmax(0, area.getBottom() - masterTop));
}

void MixerFaderPanel::configureLabel(juce::Label &label,
                                     const juce::String &text) {
  label.setText(text.toUpperCase(), juce::dontSendNotification);
  label.setJustificationType(juce::Justification::centred);
  label.setFont(juce::FontOptions(9.0f).withKerningFactor(0.10f));
  label.setColour(juce::Label::textColourId, UITheme::textSecondary);
}

void MixerFaderPanel::configureTitleLabel(juce::Label &label,
                                          const juce::String &text) {
  label.setText(text.toUpperCase(), juce::dontSendNotification);
  label.setJustificationType(juce::Justification::centred);
  label.setFont(juce::FontOptions(11.0f).withKerningFactor(0.12f));
  label.setColour(juce::Label::textColourId, UITheme::onAccent);
  label.setColour(juce::Label::backgroundColourId, UITheme::accentSoft);
  label.setColour(juce::Label::outlineColourId,
                  UITheme::accent.withAlpha(0.6f));
}

void MixerFaderPanel::configureButton(juce::TextButton &button,
                                      const juce::String &text) {
  button.setButtonText(text);
  button.setViewportIgnoreDragFlag(true);
  button.setLookAndFeel(&lookAndFeel);
  button.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  button.setColour(juce::TextButton::buttonOnColourId,
                   UITheme::cardBgPressed);
  button.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  button.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
}

void MixerFaderPanel::clearLookAndFeel() {
  upButton.setLookAndFeel(nullptr);
  downButton.setLookAndFeel(nullptr);
}
