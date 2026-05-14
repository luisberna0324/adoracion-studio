#include "ChannelStripCard.h"

#include <cmath>
#include <utility>

namespace {
float midiLearnBlinkStrength() {
  const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
  const double pulse = 0.5 + (0.5 * std::sin(seconds * juce::MathConstants<double>::twoPi * 2.4));
  return static_cast<float>(juce::jmap(pulse, 0.36, 1.0));
}

float midiLearnSoftBlinkStrength() {
  const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
  const double pulse = 0.5 + (0.5 * std::sin(seconds * juce::MathConstants<double>::twoPi * 1.1));
  return static_cast<float>(juce::jmap(pulse, 0.82, 1.0));
}

juce::String withMidiAssignmentTooltip(const juce::String &baseText,
                                       const juce::String &midiCode) {
  const auto trimmedCode = midiCode.trim();
  if (trimmedCode.isEmpty())
    return baseText;
  if (baseText.trim().isEmpty())
    return "MIDI: " + trimmedCode;
  return baseText + " | MIDI: " + trimmedCode;
}
} // namespace

ChannelStripCard::ChannelStripCard(
    const juce::String &channelName, ChannelStripLookAndFeel &lookAndFeelRef,
    StereoVUMeter::LevelProvider levelProvider, bool isCompact)
    : baseLayout(isCompact ? Layout::createCompact() : Layout{}),
      layout(baseLayout),
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

  volSlider.onPointerDown = [this] {
    if (!midiLearnSelectable)
      return;
    if (onMidiLearnTargetSelected && midiVolumeTargetId >= 0)
      onMidiLearnTargetSelected(midiVolumeTargetId);
  };

  configureKnob(gainKnob, 1.0);
  gainKnob.onPointerDown = [this] {
    if (!midiLearnSelectable)
      return;
    if (onMidiLearnTargetSelected && midiGainTargetId >= 0)
      onMidiLearnTargetSelected(midiGainTargetId);
  };
  configureLabel(gainLabel, "GAIN", 7.5f, 0.08f, true);

  soloButton.setButtonText("S");
  soloButton.setClickingTogglesState(true);
  soloButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  soloButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::onAccent);
  soloButton.setLookAndFeel(&lookAndFeel);
  soloButton.setComponentID("soloButton");
  addAndMakeVisible(soloButton);

  muteButton.setButtonText("M");
  muteButton.setClickingTogglesState(true);
  muteButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  muteButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  muteButton.setLookAndFeel(&lookAndFeel);
  muteButton.setComponentID("muteButton");
  addAndMakeVisible(muteButton);
  addAndMakeVisible(powerButton);
  powerButton.setVisible(false);

  volSlider.onValueChange = [this] {
    if (suppressVolumeCallback)
      return;
    if (midiLearnSelectable) {
      if (onMidiLearnTargetSelected && midiVolumeTargetId >= 0)
        onMidiLearnTargetSelected(midiVolumeTargetId);
      return;
    }
    if (onVolumeChanged)
      onVolumeChanged(static_cast<float>(volSlider.getValue()));
  };
  volSlider.onDragStart = [this] {
    if (!midiLearnSelectable)
      return;
    if (onMidiLearnTargetSelected && midiVolumeTargetId >= 0)
      onMidiLearnTargetSelected(midiVolumeTargetId);
  };
  gainKnob.onValueChange = [this] {
    if (midiLearnSelectable) {
      if (onMidiLearnTargetSelected && midiGainTargetId >= 0)
        onMidiLearnTargetSelected(midiGainTargetId);
      return;
    }
    if (onGainChanged)
      onGainChanged(static_cast<float>(gainKnob.getValue()));
  };
  gainKnob.onDragStart = [this] {
    if (!midiLearnSelectable)
      return;
    if (onMidiLearnTargetSelected && midiGainTargetId >= 0)
      onMidiLearnTargetSelected(midiGainTargetId);
  };
  muteButton.onClick = [this] {
    if (midiLearnSelectable) {
      if (onMidiLearnTargetSelected && midiMuteTargetId >= 0)
        onMidiLearnTargetSelected(midiMuteTargetId);
      return;
    }
    if (onMuteChanged)
      onMuteChanged(muteButton.getToggleState());
  };
  soloButton.onClick = [this] {
    if (midiLearnSelectable) {
      if (onMidiLearnTargetSelected && midiSoloTargetId >= 0)
        onMidiLearnTargetSelected(midiSoloTargetId);
      return;
    }
    if (onSoloChanged)
      onSoloChanged(soloButton.getToggleState());
  };
  powerButton.setOnToggled([this](bool poweredOn) {
    if (midiLearnSelectable) {
      if (onMidiLearnTargetSelected && midiPowerTargetId >= 0)
        onMidiLearnTargetSelected(midiPowerTargetId);
      powerButton.setPoweredOn(!poweredOn);
      return;
    }
    if (onPowerToggled)
      onPowerToggled(poweredOn);
  });
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
  const float bandFontSize = juce::jlimit(
      9.5f, 12.0f, static_cast<float>(layout.channelBandHeight) * 0.70f);
  g.setFont(juce::FontOptions(bandFontSize).withStyle("Bold").withKerningFactor(0.05f));
  g.drawFittedText(name, band.toNearestInt(), juce::Justification::centred, 1);
}

void ChannelStripCard::paintOverChildren(juce::Graphics &g) {
  if (!midiLearnSelectable)
    return;

  const auto selectableColour = juce::Colour::fromRGB(78, 170, 255);
  const auto pendingColour = juce::Colour::fromRGB(255, 196, 92);
  auto drawLearnState = [this, &g, selectableColour,
                         pendingColour](juce::Rectangle<float> area,
                                        const int targetId) {
    if (targetId < 0 || area.getWidth() <= 2.0f || area.getHeight() <= 2.0f)
      return;

    const bool pending = (midiLearnPendingTargetId == targetId);
    const float blink = pending ? midiLearnBlinkStrength()
                                : midiLearnSoftBlinkStrength();
    const auto frameColour = pending ? pendingColour : selectableColour;
    const auto frameBounds = area.reduced(1.0f);
    g.setColour(frameColour.withAlpha(pending ? (0.20f + (0.16f * blink))
                                              : (0.14f + (0.06f * blink))));
    g.fillRoundedRectangle(frameBounds, 7.0f);
    g.setColour(frameColour.withAlpha(pending ? (0.54f + (0.40f * blink))
                                              : (0.52f + (0.20f * blink))));
    g.drawRoundedRectangle(frameBounds, 7.0f,
                           pending ? (1.5f + (0.9f * blink))
                                   : (1.0f + (0.35f * blink)));
  };

  if (midiVolumeTargetId >= 0)
    drawLearnState(volSlider.getBounds().toFloat(), midiVolumeTargetId);
  if (midiGainTargetId >= 0 && gainKnob.isVisible())
    drawLearnState(gainKnob.getBounds().toFloat(), midiGainTargetId);
  if (midiMuteTargetId >= 0 && muteButton.isVisible())
    drawLearnState(muteButton.getBounds().toFloat(), midiMuteTargetId);
  if (midiSoloTargetId >= 0 && soloButton.isVisible())
    drawLearnState(soloButton.getBounds().toFloat(), midiSoloTargetId);
  if (midiPowerTargetId >= 0 && powerButton.isVisible())
    drawLearnState(powerButton.getBounds().toFloat(), midiPowerTargetId);
}

void ChannelStripCard::resized() {
  const bool hasButtonRow = soloVisible || muteVisible;
  const int buttonsY = hasButtonRow
                           ? getHeight() - layout.channelBandHeight -
                                 layout.buttonToBandGap - layout.soloHeight
                           : 0;

  // Calculate total width of Fader + Gap + Meter
  const int totalGroupWidth = layout.faderWidth + layout.faderMeterGap +
                              meter.getLayout().getBlockWidth();

  // Center whole group horizontally
  const int groupX = (getWidth() - totalGroupWidth) / 2;
  const int faderX = groupX;
  const int meterX = faderX + layout.faderWidth + layout.faderMeterGap;

  const int knobX = faderX + (layout.faderWidth - layout.knobSize) / 2;
  const int knobY = layout.knobTop;
  const juce::Rectangle<int> knobBounds(knobX, knobY, layout.knobSize,
                                        layout.knobSize);
  gainKnob.setBounds(knobBounds);
  powerButton.setBounds(knobBounds);
  const int labelW = layout.knobSize + 10;
  const int labelH = 12;
  const int labelX = faderX + (layout.faderWidth - labelW) / 2;
  const int labelY = knobY + layout.knobSize + 2;
  gainLabel.setBounds(labelX, labelY, labelW, labelH);

  // Vertically, position the fader just above the buttons
  const int mainVolBottomSpacing = hasButtonRow ? 12 : 2;
  const int componentBottomY =
      hasButtonRow ? (buttonsY - mainVolBottomSpacing)
                   : (getHeight() - layout.channelBandHeight -
                      layout.buttonToBandGap - mainVolBottomSpacing);

  const int minFaderY = labelY + labelH + 6;
  const int faderY =
      juce::jmax(minFaderY, componentBottomY - layout.faderHeight);
  const int faderH = juce::jmax(12, componentBottomY - faderY);
  volSlider.setBounds(faderX, faderY, layout.faderWidth, faderH);

  // Make the VU meter span from near the top of the strip down to just
  // above the mute/solo buttons.
  const int meterTop = layout.knobTop;
  const int meterBottom = hasButtonRow
                              ? (buttonsY - 10)
                              : (getHeight() - layout.channelBandHeight -
                                 layout.buttonToBandGap - 2);
  const int meterHeight = juce::jmax(12, meterBottom - meterTop);
  meter.setBounds(meterX, meterTop, meter.getLayout().getBlockWidth(),
                  meterHeight);

  if (!hasButtonRow) {
    soloButton.setBounds({});
    muteButton.setBounds({});
    return;
  }

  const int buttonsX = (getWidth() - layout.soloWidth) / 2;
  if (soloVisible && muteVisible) {
    const int buttonW = (layout.soloWidth - layout.buttonGap) / 2;
    soloButton.setBounds(buttonsX, buttonsY, buttonW, layout.soloHeight);
    muteButton.setBounds(buttonsX + buttonW + layout.buttonGap, buttonsY,
                         layout.soloWidth - buttonW - layout.buttonGap,
                         layout.soloHeight);
    return;
  }

  if (soloVisible) {
    soloButton.setBounds(buttonsX, buttonsY, layout.soloWidth,
                         layout.soloHeight);
    muteButton.setBounds({});
    return;
  }

  soloButton.setBounds({});
  muteButton.setBounds(buttonsX, buttonsY, layout.soloWidth,
                       layout.soloHeight);
}

const juce::String &ChannelStripCard::getChannelName() const { return name; }

void ChannelStripCard::setLayoutScale(float scale) {
  const float nextScale = juce::jlimit(0.82f, 1.4f, scale);
  if (std::abs(nextScale - layoutScale) < 0.001f)
    return;

  auto scaled = [nextScale](int value, int minValue) {
    return juce::jmax(minValue,
                      juce::roundToInt(static_cast<float>(value) * nextScale));
  };

  layout = baseLayout;
  layout.width = scaled(baseLayout.width, 60);
  layout.height = scaled(baseLayout.height, 120);
  layout.paddingX = scaled(baseLayout.paddingX, 6);
  layout.paddingBottom = scaled(baseLayout.paddingBottom, 6);
  layout.faderWidth = scaled(baseLayout.faderWidth, 14);
  layout.faderHeight = scaled(baseLayout.faderHeight, 72);
  layout.faderMeterGap = scaled(baseLayout.faderMeterGap, 2);
  layout.soloWidth = scaled(baseLayout.soloWidth, 30);
  layout.soloHeight = scaled(baseLayout.soloHeight, 14);
  layout.buttonGap = scaled(baseLayout.buttonGap, 1);
  layout.buttonToBandGap = scaled(baseLayout.buttonToBandGap, 2);
  layout.channelBandHeight = scaled(baseLayout.channelBandHeight, 12);
  layout.knobSize = scaled(baseLayout.knobSize, 22);
  layout.knobTop = scaled(baseLayout.knobTop, 4);
  layout.meterToKnobGap = scaled(baseLayout.meterToKnobGap, 4);

  layoutScale = nextScale;
  resized();
  repaint();
}

void ChannelStripCard::setVolume(float volume) {
  suppressVolumeCallback = true;
  volSlider.setValue(volume, juce::dontSendNotification);
  suppressVolumeCallback = false;
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
  resized();
}

void ChannelStripCard::setMuteVisible(bool visible) {
  muteVisible = visible;
  muteButton.setVisible(muteVisible);
  resized();
}

void ChannelStripCard::setGainVisible(bool visible) {
  gainVisible = visible;
  gainKnob.setVisible(gainVisible && !powerVisible);
  gainLabel.setVisible(gainVisible && !powerVisible);
}

void ChannelStripCard::setPowerVisible(bool visible) {
  powerVisible = visible;
  powerButton.setVisible(powerVisible);
  gainKnob.setVisible(gainVisible && !powerVisible);
  gainLabel.setVisible(gainVisible && !powerVisible);
  resized();
}

void ChannelStripCard::setPowerState(bool poweredOn) {
  powerButton.setPoweredOn(poweredOn);
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
  soloButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  soloButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBg);
  soloButton.setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);

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

void ChannelStripCard::setOnPowerToggled(std::function<void(bool)> cb) {
  onPowerToggled = std::move(cb);
}

void ChannelStripCard::setMidiLearnTargetIds(const int volumeTargetId,
                                             const int gainTargetId,
                                             const int muteTargetId,
                                             const int soloTargetId,
                                             const int powerTargetId) {
  midiVolumeTargetId = volumeTargetId;
  midiGainTargetId = gainTargetId;
  midiMuteTargetId = muteTargetId;
  midiSoloTargetId = soloTargetId;
  midiPowerTargetId = powerTargetId;
  refreshMidiLearnTooltips();
  repaint();
}

void ChannelStripCard::setMidiLearnSelectable(const bool selectable) {
  midiLearnSelectable = selectable;
  if (!midiLearnSelectable)
    midiLearnPendingTargetId = -1;
  updateMidiLearnBlinkTimer();
  repaint();
}

void ChannelStripCard::setMidiLearnPendingTarget(const int targetId) {
  midiLearnPendingTargetId = hasMidiLearnTarget(targetId) ? targetId : -1;
  updateMidiLearnBlinkTimer();
  refreshMidiLearnTooltips();
  repaint();
}

void ChannelStripCard::setMidiLearnBindingLabelProvider(
    std::function<juce::String(int)> cb) {
  midiLearnBindingLabelProvider = std::move(cb);
  refreshMidiLearnTooltips();
  updateMidiLearnBlinkTimer();
  repaint();
}

void ChannelStripCard::setOnMidiLearnTargetSelected(std::function<void(int)> cb) {
  onMidiLearnTargetSelected = std::move(cb);
}

bool ChannelStripCard::hasMidiLearnTarget(const int targetId) const {
  return targetId >= 0 &&
         (midiVolumeTargetId == targetId || midiGainTargetId == targetId ||
          midiMuteTargetId == targetId || midiSoloTargetId == targetId ||
          midiPowerTargetId == targetId);
}

bool ChannelStripCard::hasAnyMidiLearnTarget() const {
  return midiVolumeTargetId >= 0 || midiGainTargetId >= 0 ||
         midiMuteTargetId >= 0 || midiSoloTargetId >= 0 ||
         midiPowerTargetId >= 0;
}

void ChannelStripCard::timerCallback() {
  repaint();
}

void ChannelStripCard::updateMidiLearnBlinkTimer() {
  const bool shouldBlink = midiLearnSelectable;
  if (shouldBlink) {
    if (!isTimerRunning())
      startTimerHz(24);
  } else if (isTimerRunning()) {
    stopTimer();
  }
}

void ChannelStripCard::refreshMidiLearnTooltips() {
  auto tooltipForTarget = [this](const int targetId,
                                 const juce::String &baseText) {
    if (targetId < 0 || midiLearnBindingLabelProvider == nullptr)
      return juce::String{};
    const auto midiCode = midiLearnBindingLabelProvider(targetId).trim();
    if (midiCode.isEmpty())
      return juce::String{};
    return withMidiAssignmentTooltip(baseText, midiCode);
  };

  volSlider.setTooltip(tooltipForTarget(midiVolumeTargetId, "Volume"));
  gainKnob.setTooltip(tooltipForTarget(midiGainTargetId, "Gain"));
  muteButton.setTooltip(tooltipForTarget(midiMuteTargetId, "Mute"));
  soloButton.setTooltip(tooltipForTarget(midiSoloTargetId, "Solo"));
  powerButton.setTooltip(tooltipForTarget(midiPowerTargetId, "Power"));
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
