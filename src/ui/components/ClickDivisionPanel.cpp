#include "ClickDivisionPanel.h"

#include "../UiCommon.h"

#include <cmath>
#include <utility>

namespace {
constexpr float cardCornerRadius = 4.0f;
const juce::Colour selectedDivisionColour{0xFFC8943A};

juce::Colour inactiveButtonColour() {
  return UITheme::panelDark.contrasting(0.04f);
}
} // namespace

ClickDivisionPanel::DivisionButton::DivisionButton(
    const juce::String &buttonLabel, int value)
    : juce::Button(buttonLabel), label(buttonLabel), divisionValue(value) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ClickDivisionPanel::DivisionButton::setSelected(const bool shouldSelect) {
  if (selected == shouldSelect)
    return;
  selected = shouldSelect;
  repaint();
}

void ClickDivisionPanel::DivisionButton::paintButton(juce::Graphics &g,
                                                     bool isMouseOverButton,
                                                     bool isButtonDown) {
  auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  auto fill = selected ? selectedDivisionColour : inactiveButtonColour();
  if (isMouseOverButton)
    fill = selected ? fill.brighter(0.04f) : UITheme::cardBgHover;
  if (isButtonDown)
    fill = fill.darker(0.10f);

  const float radius = 3.0f;
  g.setColour(juce::Colours::black.withAlpha(0.28f));
  g.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), radius);
  g.setColour(fill);
  g.fillRoundedRectangle(bounds, radius);
  g.setColour(selected ? selectedDivisionColour.brighter(0.20f)
                       : UITheme::cardBorder);
  g.drawRoundedRectangle(bounds, radius, 1.0f);

  g.setColour(selected ? UITheme::onAccent : UITheme::textPrimary);
  g.setFont(makeAppFont(juce::jlimit(10.5f, 13.0f,
                                     bounds.getHeight() * 0.48f),
                        selected ? AppFontWeight::Bold
                                 : AppFontWeight::SemiBold));
  g.drawFittedText(label, bounds.toNearestInt(), juce::Justification::centred,
                   1);
}

ClickDivisionPanel::ClickDivisionPanel(ChannelStripLookAndFeel &lnf,
                                       StereoVUMeter::LevelProvider levelProvider)
    : lookAndFeel(lnf) {
  btn14 = std::make_unique<DivisionButton>("1/4", 1);
  btn18 = std::make_unique<DivisionButton>("1/8", 2);
  btn116 = std::make_unique<DivisionButton>("1/16", 4);
  volumeKnob = std::make_unique<juce::Slider>();
  meter = std::make_unique<StereoVUMeter>(
      std::move(levelProvider), [this] { return muted; });
  soloButton = std::make_unique<juce::TextButton>("S");
  muteButton = std::make_unique<juce::TextButton>("M");

  for (auto *button : {btn14.get(), btn18.get(), btn116.get()}) {
    button->onClick = [this, division = button->getDivisionValue()] {
      selectDivision(division);
    };
    addAndMakeVisible(button);
  }

  volumeKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  volumeKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volumeKnob->setRange(0.0, 1.0);
  volumeKnob->setValue(1.0, juce::dontSendNotification);
  volumeKnob->setLookAndFeel(&lookAndFeel);
  volumeKnob->onValueChange = [this] {
    if (suppressVolumeCallback)
      return;
    if (onVolumeChanged)
      onVolumeChanged(static_cast<float>(volumeKnob->getValue()));
  };
  addAndMakeVisible(*volumeKnob);
  addAndMakeVisible(*meter);

  soloButton->setClickingTogglesState(true);
  soloButton->setComponentID("soloButton");
  soloButton->setLookAndFeel(&lookAndFeel);
  soloButton->setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  soloButton->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  soloButton->onClick = [this] {
    soloed = soloButton->getToggleState();
    updateButtonStates();
    if (onSoloChanged)
      onSoloChanged(soloed);
  };
  addAndMakeVisible(*soloButton);

  muteButton->setClickingTogglesState(true);
  muteButton->setComponentID("muteButton");
  muteButton->setLookAndFeel(&lookAndFeel);
  muteButton->setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  muteButton->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  muteButton->onClick = [this] {
    muted = muteButton->getToggleState();
    updateButtonStates();
    if (onMuteChanged)
      onMuteChanged(muted);
  };
  addAndMakeVisible(*muteButton);
  updateButtonStates();
}

ClickDivisionPanel::~ClickDivisionPanel() {
  if (volumeKnob != nullptr)
    volumeKnob->setLookAndFeel(nullptr);
  if (soloButton != nullptr)
    soloButton->setLookAndFeel(nullptr);
  if (muteButton != nullptr)
    muteButton->setLookAndFeel(nullptr);
}

void ClickDivisionPanel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  g.setColour(UITheme::cardBgClickGuide);
  g.fillRoundedRectangle(bounds, cardCornerRadius);
  g.setColour(UITheme::cardBorder);
  g.drawRoundedRectangle(bounds.reduced(0.5f), cardCornerRadius, 1.0f);

  const int bandH = juce::jmax(12, juce::roundToInt(16.0f * layoutScale));
  auto footer = getLocalBounds().removeFromBottom(bandH).toFloat();
  g.setColour(UITheme::accent);
  g.fillRect(footer);

  g.setColour(UITheme::onAccent);
  g.setFont(makeAppFont(juce::jlimit(9.5f, 12.0f,
                                     static_cast<float>(bandH) * 0.70f),
                        AppFontWeight::Bold));
  g.drawFittedText("DIV", footer.toNearestInt(), juce::Justification::centred,
                   1);

  if (volumeKnob != nullptr) {
    const auto knobBounds = volumeKnob->getBounds();
    const int labelW = knobBounds.getWidth() + 10;
    const int labelH = 12;
    const int labelX = knobBounds.getCentreX() - (labelW / 2);
    const int labelY = knobBounds.getBottom() + 2;
    g.setColour(UITheme::textSecondary);
    g.setFont(makeAppFont(7.5f, AppFontWeight::Bold));
    g.drawFittedText("VOLUME",
                     juce::Rectangle<int>(labelX, labelY, labelW, labelH),
                     juce::Justification::centred, 1);
  }
}

void ClickDivisionPanel::resized() {
  const int bandH = juce::jmax(12, juce::roundToInt(16.0f * layoutScale));
  const int sidePad = juce::jmax(6, juce::roundToInt(8.0f * layoutScale));
  const int gap = juce::jmax(5, juce::roundToInt(6.0f * layoutScale));
  const int buttonH = juce::jmax(18, juce::roundToInt(22.0f * layoutScale));
  const int meterW = meter->getLayout().getBlockWidth();
  const int meterX = getWidth() - sidePad - meterW;
  const int buttonW = juce::jlimit(
      30, juce::jmax(30, juce::roundToInt(54.0f * layoutScale)),
      juce::jmax(30, meterX - sidePad - gap));
  const int x = sidePad;

  const int rowH = juce::jmax(18, juce::roundToInt(22.0f * layoutScale));
  const int rowW = juce::jlimit(
      54, juce::jmax(54, juce::roundToInt(65.0f * layoutScale)),
      juce::jmax(54, getWidth() - (sidePad * 2)));
  const int rowX = (getWidth() - rowW) / 2;
  const int rowY =
      getHeight() - bandH - juce::jmax(6, juce::roundToInt(6.0f * layoutScale)) -
      rowH;

  const int knobSize = juce::jmax(22, juce::roundToInt(34.0f * layoutScale));
  const int knobX = x + ((buttonW - knobSize) / 2);
  const int knobY = juce::jmax(6, juce::roundToInt(8.0f * layoutScale));
  const int knobLabelH = 14;
  volumeKnob->setBounds(knobX, knobY, knobSize, knobSize);

  const int buttonsTopLimit = knobY + knobSize + knobLabelH +
                              juce::jmax(8, juce::roundToInt(10.0f * layoutScale));
  const int buttonsBottomLimit =
      rowY - juce::jmax(10, juce::roundToInt(12.0f * layoutScale));
  const int buttonsTotalH = (buttonH * 3) + (gap * 2);
  int y = buttonsTopLimit;
  if (buttonsBottomLimit > buttonsTopLimit + buttonsTotalH)
    y += (buttonsBottomLimit - buttonsTopLimit - buttonsTotalH) / 2;
  if (y + buttonsTotalH > buttonsBottomLimit)
    y = juce::jmax(knobY + knobSize + knobLabelH + gap,
                   buttonsBottomLimit - buttonsTotalH);

  btn14->setBounds(x, y, buttonW, buttonH);
  y += buttonH + gap;
  btn18->setBounds(x, y, buttonW, buttonH);
  y += buttonH + gap;
  btn116->setBounds(x, y, buttonW, buttonH);

  const int meterTop = juce::jmax(8, juce::roundToInt(8.0f * layoutScale));
  const int meterBottom =
      rowY - juce::jmax(10, juce::roundToInt(10.0f * layoutScale));
  meter->setBounds(meterX, meterTop, meterW,
                   juce::jmax(12, meterBottom - meterTop));

  const int buttonGap = juce::jmax(2, juce::roundToInt(3.0f * layoutScale));
  const int soloW = (rowW - buttonGap) / 2;
  soloButton->setBounds(rowX, rowY, soloW, rowH);
  muteButton->setBounds(rowX + soloW + buttonGap, rowY,
                        rowW - soloW - buttonGap, rowH);
}

void ClickDivisionPanel::setDivision(int division) {
  division = normalizeDivision(division);
  if (division == currentDivision)
    return;
  currentDivision = division;
  updateButtonStates();
}

int ClickDivisionPanel::getDivision() const { return currentDivision; }

void ClickDivisionPanel::setVolume(const float volume) {
  suppressVolumeCallback = true;
  volumeKnob->setValue(juce::jlimit(0.0f, 1.0f, volume),
                       juce::dontSendNotification);
  suppressVolumeCallback = false;
}

void ClickDivisionPanel::setPowerOn(const bool shouldBeOn) {
  if (powerOn == shouldBeOn)
    return;
  powerOn = shouldBeOn;
  updateButtonStates();
}

void ClickDivisionPanel::setMuteState(const bool shouldBeMuted) {
  if (muted == shouldBeMuted)
    return;
  muted = shouldBeMuted;
  updateButtonStates();
}

void ClickDivisionPanel::setSoloState(const bool shouldBeSoloed) {
  if (soloed == shouldBeSoloed)
    return;
  soloed = shouldBeSoloed;
  updateButtonStates();
}

void ClickDivisionPanel::setOnDivisionChanged(std::function<void(int)> cb) {
  onDivisionChanged = std::move(cb);
}

void ClickDivisionPanel::setOnVolumeChanged(std::function<void(float)> cb) {
  onVolumeChanged = std::move(cb);
}

void ClickDivisionPanel::setOnPowerChanged(std::function<void(bool)> cb) {
  onPowerChanged = std::move(cb);
}

void ClickDivisionPanel::setOnMuteChanged(std::function<void(bool)> cb) {
  onMuteChanged = std::move(cb);
}

void ClickDivisionPanel::setOnSoloChanged(std::function<void(bool)> cb) {
  onSoloChanged = std::move(cb);
}

void ClickDivisionPanel::setLayoutScale(float scale) {
  const float nextScale = juce::jlimit(0.82f, 1.4f, scale);
  if (std::abs(nextScale - layoutScale) < 0.001f)
    return;
  layoutScale = nextScale;
  resized();
  repaint();
}

int ClickDivisionPanel::normalizeDivision(const int division) noexcept {
  return division >= 4 ? 4 : (division >= 2 ? 2 : 1);
}

void ClickDivisionPanel::selectDivision(int division) {
  division = normalizeDivision(division);
  if (division == currentDivision)
    return;
  currentDivision = division;
  updateButtonStates();
  if (onDivisionChanged)
    onDivisionChanged(division);
}

void ClickDivisionPanel::updateButtonStates() {
  btn14->setSelected(currentDivision == 1);
  btn18->setSelected(currentDivision == 2);
  btn116->setSelected(currentDivision == 4);
  soloButton->setToggleState(soloed, juce::dontSendNotification);
  muteButton->setToggleState(muted, juce::dontSendNotification);
  repaint();
}
