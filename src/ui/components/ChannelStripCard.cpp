#include "ChannelStripCard.h"

#include <utility>

ChannelStripCard::ChannelStripCard(
    const juce::String &channelName, ChannelStripLookAndFeel &lookAndFeelRef,
    StereoVUMeter::LevelProvider levelProvider, bool isCompact)
    : layout(isCompact ? Layout::createCompact() : Layout{}),
      lookAndFeel(lookAndFeelRef), name(channelName),
      meter(
          std::move(levelProvider),
          [this] { return muteButton.getToggleState(); }, isCompact),
      cardBgColour(UITheme::cardBg), channelBandColour(UITheme::accent),
      channelBandTextColour(UITheme::onAccent) {
  volSlider.setSliderStyle(juce::Slider::LinearVertical);
  volSlider.setViewportIgnoreDragFlag(true);
  volSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volSlider.setRange(0.0, 1.0);
  volSlider.setValue(1.0);
  volSlider.setLookAndFeel(&lookAndFeel);
  addAndMakeVisible(volSlider);
  addAndMakeVisible(meter);

  configureKnob(gainKnob, 1.0);
  configureLabel(gainLabel, "GAIN", 7.5f, 0.08f, true);

  soloButton.setButtonText("S");
  soloButton.setClickingTogglesState(true);
  soloButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  soloButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::textPrimary.darker(0.18f));
  soloButton.setLookAndFeel(&lookAndFeel);
  addAndMakeVisible(soloButton);

  muteButton.setButtonText("M");
  muteButton.setClickingTogglesState(true);
  muteButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  muteButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  muteButton.setLookAndFeel(&lookAndFeel);
  muteButton.setComponentID("muteButton");
  addAndMakeVisible(muteButton);

  volSlider.onValueChange = [this] {
    if (onVolumeChanged)
      onVolumeChanged(static_cast<float>(volSlider.getValue()));
  };
  gainKnob.onValueChange = [this] {
    if (onGainChanged)
      onGainChanged(static_cast<float>(gainKnob.getValue()));
  };
  muteButton.onClick = [this] {
    if (onMuteChanged)
      onMuteChanged(muteButton.getToggleState());
  };
  soloButton.onClick = [this] {
    if (onSoloChanged)
      onSoloChanged(soloButton.getToggleState());
  };
}

ChannelStripCard::~ChannelStripCard() { clearLookAndFeelFromControls(); }

void ChannelStripCard::paint(juce::Graphics &g) {
  const auto card = getLocalBounds().toFloat();
  g.setColour(cardBgColour);
  g.fillRoundedRectangle(card, 4.0f);
  g.setColour(UITheme::cardBorder);
  g.drawRoundedRectangle(card, 4.0f, 1.0f);

  const auto band = juce::Rectangle<float>(
      0.0f, static_cast<float>(getHeight() - layout.channelBandHeight),
      static_cast<float>(getWidth()),
      static_cast<float>(layout.channelBandHeight));
  g.setColour(channelBandColour);
  g.fillRect(band);

  g.setColour(channelBandTextColour);
  g.setFont(juce::FontOptions(8.0f).withKerningFactor(0.08f));
  g.drawFittedText(name, band.toNearestInt(), juce::Justification::centred, 1);
}

void ChannelStripCard::resized() {
  const int buttonsY = getHeight() - layout.channelBandHeight -
                       layout.buttonToBandGap - layout.soloHeight;

  // Calculate total width of Fader + Gap + Meter
  const int totalGroupWidth = layout.faderWidth + layout.faderMeterGap +
                              meter.getLayout().getBlockWidth();

  // Center whole group horizontally
  const int groupX = (getWidth() - totalGroupWidth) / 2;
  const int faderX = groupX;
  const int meterX = faderX + layout.faderWidth + layout.faderMeterGap;

  const int knobX = faderX + (layout.faderWidth - layout.knobSize) / 2;
  const int knobY = layout.knobTop;
  gainKnob.setBounds(knobX, knobY, layout.knobSize, layout.knobSize);
  const int labelW = layout.knobSize + 10;
  const int labelH = 12;
  const int labelX = faderX + (layout.faderWidth - labelW) / 2;
  const int labelY = knobY + layout.knobSize + 2;
  gainLabel.setBounds(labelX, labelY, labelW, labelH);

  // Vertically, position the fader just above the buttons
  const int mainVolBottomSpacing = 12;
  const int componentBottomY = buttonsY - mainVolBottomSpacing;

  const int minFaderY = labelY + labelH + 6;
  const int faderY =
      juce::jmax(minFaderY, componentBottomY - layout.faderHeight);
  const int faderH = juce::jmax(12, componentBottomY - faderY);
  volSlider.setBounds(faderX, faderY, layout.faderWidth, faderH);

  // Make the VU meter span from near the top of the strip down to just
  // above the mute/solo buttons.
  const int meterTop = layout.knobTop;
  const int meterBottom = buttonsY - 10;
  const int meterHeight = juce::jmax(12, meterBottom - meterTop);
  meter.setBounds(meterX, meterTop, meter.getLayout().getBlockWidth(),
                  meterHeight);

  const int buttonsX = (getWidth() - layout.soloWidth) / 2;
  if (soloVisible) {
    const int buttonW = (layout.soloWidth - layout.buttonGap) / 2;
    soloButton.setBounds(buttonsX, buttonsY, buttonW, layout.soloHeight);
    muteButton.setBounds(buttonsX + buttonW + layout.buttonGap, buttonsY,
                         layout.soloWidth - buttonW - layout.buttonGap,
                         layout.soloHeight);
  } else {
    soloButton.setBounds({});
    muteButton.setBounds(buttonsX, buttonsY, layout.soloWidth,
                         layout.soloHeight);
  }
}

const juce::String &ChannelStripCard::getChannelName() const { return name; }

void ChannelStripCard::setVolume(float volume) {
  volSlider.setValue(volume, juce::dontSendNotification);
}

void ChannelStripCard::setGain(float gain) {
  gainKnob.setValue(gain, juce::dontSendNotification);
}

void ChannelStripCard::setMuteState(bool muted) {
  muteButton.setToggleState(muted, juce::dontSendNotification);
}

void ChannelStripCard::setSoloState(bool soloed) {
  soloButton.setToggleState(soloed, juce::dontSendNotification);
}

void ChannelStripCard::setSoloEnabled(bool enabled) {
  soloButton.setEnabled(enabled);
}

void ChannelStripCard::setSoloVisible(bool visible) {
  soloVisible = visible;
  soloButton.setVisible(soloVisible);
}

void ChannelStripCard::setGainVisible(bool visible) {
  gainVisible = visible;
  gainKnob.setVisible(gainVisible);
  gainLabel.setVisible(gainVisible);
}

void ChannelStripCard::setMuteAlignedToFader(bool aligned) {
  muteAlignedToFader = aligned;
  resized();
}

void ChannelStripCard::setChannelBandColours(juce::Colour bandColour,
                                             juce::Colour textColour) {
  channelBandColour = bandColour;
  channelBandTextColour = textColour;
  repaint();
}

void ChannelStripCard::setCardBgColour(juce::Colour colour) {
  cardBgColour = colour;
  repaint();
}

void ChannelStripCard::refreshThemeColours() {
  volLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  gainLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);

  soloButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  soloButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::textPrimary.darker(0.18f));
  soloButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBg);
  soloButton.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
  soloButton.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);

  muteButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  muteButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  muteButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBg);
  muteButton.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);

  repaint();
}

void ChannelStripCard::setOnVolumeChanged(std::function<void(float)> cb) {
  onVolumeChanged = std::move(cb);
}

void ChannelStripCard::setOnGainChanged(std::function<void(float)> cb) {
  onGainChanged = std::move(cb);
}

void ChannelStripCard::setOnMuteChanged(std::function<void(bool)> cb) {
  onMuteChanged = std::move(cb);
}

void ChannelStripCard::setOnSoloChanged(std::function<void(bool)> cb) {
  onSoloChanged = std::move(cb);
}

void ChannelStripCard::configureLabel(juce::Label &label,
                                      const juce::String &text, float size,
                                      float kerning, bool uppercase) {
  label.setText(uppercase ? text.toUpperCase() : text,
                juce::dontSendNotification);
  label.setJustificationType(juce::Justification::centred);
  label.setFont(juce::FontOptions(size).withKerningFactor(kerning));
  label.setColour(juce::Label::textColourId, UITheme::textPrimary);
  addAndMakeVisible(label);
}

void ChannelStripCard::configureKnob(juce::Slider &slider, double value) {
  slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  slider.setViewportIgnoreDragFlag(true);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setRange(0.0, 2.0, 0.0);
  slider.setValue(value, juce::dontSendNotification);
  slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                             juce::MathConstants<float>::pi * 2.8f, true);
  slider.setLookAndFeel(&lookAndFeel);
  slider.setMouseDragSensitivity(170);
  slider.setDoubleClickReturnValue(true, value);
  addAndMakeVisible(slider);
}

void ChannelStripCard::clearLookAndFeelFromControls() {
  volSlider.setLookAndFeel(nullptr);
  gainKnob.setLookAndFeel(nullptr);
  gainLabel.setLookAndFeel(nullptr);
  soloButton.setLookAndFeel(nullptr);
  muteButton.setLookAndFeel(nullptr);
}
