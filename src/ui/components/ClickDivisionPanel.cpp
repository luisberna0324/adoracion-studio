#include "ClickDivisionPanel.h"

#include "../UiCommon.h"

#include <cmath>
#include <utility>

namespace {
constexpr float cardCornerRadius = 4.0f;

float midiLearnBlinkStrength() {
  const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
  const double pulse =
      0.5 + (0.5 * std::sin(seconds * juce::MathConstants<double>::twoPi * 2.4));
  return static_cast<float>(juce::jmap(pulse, 0.36, 1.0));
}

float midiLearnSoftBlinkStrength() {
  const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
  const double pulse =
      0.5 + (0.5 * std::sin(seconds * juce::MathConstants<double>::twoPi * 1.1));
  return static_cast<float>(juce::jmap(pulse, 0.82, 1.0));
}

juce::String withMidiAssignmentTooltip(const juce::String &baseText,
                                       const juce::String &midiCode) {
  const auto trimmed = midiCode.trim();
  if (trimmed.isEmpty())
    return baseText;
  return baseText + " | MIDI: " + trimmed;
}
} // namespace

void ClickDivisionPanel::LearnAwareButton::mouseDown(
    const juce::MouseEvent &event) {
  juce::TextButton::mouseDown(event);
  if (onPointerDown)
    onPointerDown();
}

ClickDivisionPanel::DivisionButton::DivisionButton(
    const juce::String &buttonLabel, int value)
    : LearnAwareButton(buttonLabel), divisionValue(value) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setClickingTogglesState(true);
  setButtonText(buttonLabel);
}

void ClickDivisionPanel::DivisionButton::setSelected(const bool shouldSelect) {
  if (getToggleState() == shouldSelect)
    return;
  setToggleState(shouldSelect, juce::dontSendNotification);
}

ClickDivisionPanel::ClickDivisionPanel(ChannelStripLookAndFeel &lnf,
                                       StereoVUMeter::LevelProvider levelProvider)
    : lookAndFeel(lnf) {
  btn14 = std::make_unique<DivisionButton>("1/4", 1);
  btn18 = std::make_unique<DivisionButton>("1/8", 2);
  btn116 = std::make_unique<DivisionButton>("1/16", 4);
  volumeKnob = std::make_unique<LearnAwareSlider>();
  meter = std::make_unique<StereoVUMeter>(
      std::move(levelProvider), [this] { return muted; }, false,
      StereoVUMeter::Response::percussive);
  soloButton = std::make_unique<LearnAwareButton>("S");
  muteButton = std::make_unique<LearnAwareButton>("M");

  for (auto *button : {btn14.get(), btn18.get(), btn116.get()}) {
    button->setViewportIgnoreDragFlag(true);
    button->setLookAndFeel(&lookAndFeel);
    button->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    button->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
    button->setColour(juce::TextButton::buttonColourId, UITheme::cardBg);
    button->setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);
    button->onClick = [this] {
      if (midiLearnSelectable) {
        updateButtonStates();
        return;
      }
      updateDivisionMaskFromButtons();
    };
    addAndMakeVisible(button);
  }
  btn14->onPointerDown = [this] { selectMidiLearnTarget(midiQuarterTargetId); };
  btn18->onPointerDown = [this] { selectMidiLearnTarget(midiEighthTargetId); };
  btn116->onPointerDown = [this] { selectMidiLearnTarget(midiSixteenthTargetId); };

  volumeKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  volumeKnob->setViewportIgnoreDragFlag(true);
  volumeKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volumeKnob->setRange(0.0, 1.0);
  volumeKnob->setValue(1.0, juce::dontSendNotification);
  volumeKnob->setLookAndFeel(&lookAndFeel);
  volumeKnob->setMouseDragSensitivity(170);
  volumeKnob->onValueChange = [this] {
    if (suppressVolumeCallback)
      return;
    if (onVolumeChanged)
      onVolumeChanged(static_cast<float>(volumeKnob->getValue()));
  };
  volumeKnob->onDragEnd = [this] {
    if (suppressVolumeCallback)
      return;
    if (onVolumeDragEnded)
      onVolumeDragEnded(static_cast<float>(volumeKnob->getValue()));
  };
  addAndMakeVisible(*volumeKnob);
  addAndMakeVisible(*meter);

  soloButton->setClickingTogglesState(true);
  soloButton->setViewportIgnoreDragFlag(true);
  soloButton->setComponentID("soloButton");
  soloButton->setLookAndFeel(&lookAndFeel);
  soloButton->setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  soloButton->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  soloButton->onPointerDown = [this] { selectMidiLearnTarget(midiSoloTargetId); };
  soloButton->onClick = [this] {
    if (midiLearnSelectable) {
      updateButtonStates();
      return;
    }
    soloed = soloButton->getToggleState();
    updateButtonStates();
    if (onSoloChanged)
      onSoloChanged(soloed);
  };
  addAndMakeVisible(*soloButton);

  muteButton->setClickingTogglesState(true);
  muteButton->setViewportIgnoreDragFlag(true);
  muteButton->setComponentID("muteButton");
  muteButton->setLookAndFeel(&lookAndFeel);
  muteButton->setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  muteButton->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  muteButton->onPointerDown = [this] { selectMidiLearnTarget(midiMuteTargetId); };
  muteButton->onClick = [this] {
    if (midiLearnSelectable) {
      updateButtonStates();
      return;
    }
    muted = muteButton->getToggleState();
    updateButtonStates();
    if (onMuteChanged)
      onMuteChanged(muted);
  };
  addAndMakeVisible(*muteButton);
  refreshThemeColours();
  updateButtonStates();
}

ClickDivisionPanel::~ClickDivisionPanel() {
  if (btn14 != nullptr)
    btn14->setLookAndFeel(nullptr);
  if (btn18 != nullptr)
    btn18->setLookAndFeel(nullptr);
  if (btn116 != nullptr)
    btn116->setLookAndFeel(nullptr);
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
  auto footerArea = getLocalBounds();
  footerArea.removeFromBottom(bottomContentInset);
  auto footer = footerArea.removeFromBottom(bandH).toFloat();
  g.setColour(UITheme::accent);
  g.fillRect(footer);

  g.setColour(UITheme::onAccent);
  g.setFont(makeAppFont(juce::jlimit(9.5f, 12.0f,
                                     static_cast<float>(bandH) * 0.70f),
                        AppFontWeight::Bold));
  g.drawFittedText("CLICK DYN", footer.toNearestInt(),
                   juce::Justification::centred, 1);

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
      getHeight() - bottomContentInset - bandH -
      juce::jmax(6, juce::roundToInt(6.0f * layoutScale)) -
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

void ClickDivisionPanel::setVolume(const float volume) {
  if (volumeKnob->isMouseButtonDown()) return;
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

void ClickDivisionPanel::setOnVolumeDragEnded(std::function<void(float)> cb) {
  onVolumeDragEnded = std::move(cb);
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

void ClickDivisionPanel::setMidiLearnTargetIds(
    const int quarterTargetId, const int eighthTargetId,
    const int sixteenthTargetId, const int muteTargetId,
    const int soloTargetId) {
  midiQuarterTargetId = quarterTargetId;
  midiEighthTargetId = eighthTargetId;
  midiSixteenthTargetId = sixteenthTargetId;
  midiMuteTargetId = muteTargetId;
  midiSoloTargetId = soloTargetId;
  refreshMidiLearnTooltips();
  repaint();
}

void ClickDivisionPanel::setMidiLearnSelectable(const bool selectable) {
  midiLearnSelectable = selectable;
  if (!midiLearnSelectable)
    midiLearnPendingTargetId = -1;
  updateMidiLearnBlinkTimer();
  repaint();
}

void ClickDivisionPanel::setMidiLearnPendingTarget(const int targetId) {
  midiLearnPendingTargetId = hasMidiLearnTarget(targetId) ? targetId : -1;
  updateMidiLearnBlinkTimer();
  refreshMidiLearnTooltips();
  repaint();
}

void ClickDivisionPanel::setMidiLearnBindingLabelProvider(
    std::function<juce::String(int)> cb) {
  midiLearnBindingLabelProvider = std::move(cb);
  refreshMidiLearnTooltips();
  updateMidiLearnBlinkTimer();
  repaint();
}

void ClickDivisionPanel::setOnMidiLearnTargetSelected(
    std::function<void(int)> cb) {
  onMidiLearnTargetSelected = std::move(cb);
}

void ClickDivisionPanel::setLayoutScale(float scale) {
  const float nextScale = juce::jlimit(0.82f, 1.4f, scale);
  if (std::abs(nextScale - layoutScale) < 0.001f)
    return;
  layoutScale = nextScale;
  resized();
  repaint();
}

void ClickDivisionPanel::setBottomContentInset(const int inset) {
  const int nextInset = juce::jmax(0, inset);
  if (bottomContentInset == nextInset)
    return;
  bottomContentInset = nextInset;
  resized();
  repaint();
}

void ClickDivisionPanel::refreshThemeColours() {
  if (btn14 != nullptr) {
    btn14->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    btn14->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
    btn14->setColour(juce::TextButton::buttonColourId, UITheme::cardBg);
    btn14->setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);
  }
  if (btn18 != nullptr) {
    btn18->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    btn18->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
    btn18->setColour(juce::TextButton::buttonColourId, UITheme::cardBg);
    btn18->setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);
  }
  if (btn116 != nullptr) {
    btn116->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    btn116->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
    btn116->setColour(juce::TextButton::buttonColourId, UITheme::cardBg);
    btn116->setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);
  }
  if (soloButton != nullptr) {
    soloButton->setColour(juce::TextButton::buttonColourId,
                          UITheme::cardBgHover);
    soloButton->setColour(juce::TextButton::buttonOnColourId,
                          UITheme::accentSoft);
    soloButton->setColour(juce::TextButton::textColourOffId,
                          UITheme::textPrimary);
    soloButton->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  }
  if (muteButton != nullptr) {
    muteButton->setColour(juce::TextButton::buttonColourId,
                          UITheme::cardBgHover);
    muteButton->setColour(juce::TextButton::buttonOnColourId,
                          UITheme::accentMuted);
    muteButton->setColour(juce::TextButton::textColourOffId,
                          UITheme::textPrimary);
    muteButton->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  }
  repaint();
}

bool ClickDivisionPanel::hasMidiLearnTarget(const int targetId) const noexcept {
  return targetId >= 0 &&
         (targetId == midiQuarterTargetId || targetId == midiEighthTargetId ||
          targetId == midiSixteenthTargetId || targetId == midiMuteTargetId ||
          targetId == midiSoloTargetId);
}

void ClickDivisionPanel::selectMidiLearnTarget(const int targetId) {
  if (!midiLearnSelectable || !hasMidiLearnTarget(targetId))
    return;
  midiLearnPendingTargetId = targetId;
  if (onMidiLearnTargetSelected)
    onMidiLearnTargetSelected(targetId);
  updateMidiLearnBlinkTimer();
  repaint();
}

void ClickDivisionPanel::updateMidiLearnBlinkTimer() {
  if (midiLearnSelectable) {
    if (!isTimerRunning())
      startTimerHz(24);
  } else if (isTimerRunning()) {
    stopTimer();
  }
}

void ClickDivisionPanel::refreshMidiLearnTooltips() {
  auto tooltipForTarget = [this](const int targetId, const juce::String &baseText) {
    if (targetId < 0 || midiLearnBindingLabelProvider == nullptr)
      return baseText;
    return withMidiAssignmentTooltip(baseText,
                                     midiLearnBindingLabelProvider(targetId));
  };

  if (btn14 != nullptr)
    btn14->setTooltip(tooltipForTarget(midiQuarterTargetId, "CLICK DYN 1/4"));
  if (btn18 != nullptr)
    btn18->setTooltip(tooltipForTarget(midiEighthTargetId, "CLICK DYN 1/8"));
  if (btn116 != nullptr)
    btn116->setTooltip(
        tooltipForTarget(midiSixteenthTargetId, "CLICK DYN 1/16"));
  if (muteButton != nullptr)
    muteButton->setTooltip(tooltipForTarget(midiMuteTargetId, "CLICK DYN Mute"));
  if (soloButton != nullptr)
    soloButton->setTooltip(tooltipForTarget(midiSoloTargetId, "CLICK DYN Solo"));
}

void ClickDivisionPanel::drawMidiLearnFrame(juce::Graphics &g,
                                            juce::Component &component,
                                            const int targetId) {
  if (!midiLearnSelectable || targetId < 0 || !component.isVisible())
    return;

  const auto selectableColour = juce::Colour::fromRGB(78, 170, 255);
  const auto pendingColour = juce::Colour::fromRGB(255, 196, 92);
  const auto frameBounds = component.getBounds().toFloat().reduced(1.0f);
  const bool pending = midiLearnPendingTargetId == targetId;
  const float blink =
      pending ? midiLearnBlinkStrength() : midiLearnSoftBlinkStrength();
  const auto frameColour = pending ? pendingColour : selectableColour;
  g.setColour(frameColour.withAlpha(pending ? (0.20f + (0.16f * blink))
                                            : (0.14f + (0.06f * blink))));
  g.fillRoundedRectangle(frameBounds, 5.0f);
  g.setColour(frameColour.withAlpha(pending ? (0.54f + (0.40f * blink))
                                            : (0.52f + (0.20f * blink))));
  g.drawRoundedRectangle(frameBounds, 5.0f,
                         pending ? (1.4f + (0.8f * blink))
                                 : (1.0f + (0.3f * blink)));
}

void ClickDivisionPanel::paintOverChildren(juce::Graphics &g) {
  drawMidiLearnFrame(g, *btn14, midiQuarterTargetId);
  drawMidiLearnFrame(g, *btn18, midiEighthTargetId);
  drawMidiLearnFrame(g, *btn116, midiSixteenthTargetId);
  drawMidiLearnFrame(g, *muteButton, midiMuteTargetId);
  drawMidiLearnFrame(g, *soloButton, midiSoloTargetId);
}

void ClickDivisionPanel::timerCallback() {
  repaint();
}

int ClickDivisionPanel::normalizeDivisionMask(const int divisionMask) noexcept {
  return divisionMask & 0x07;
}

void ClickDivisionPanel::updateDivisionMaskFromButtons() {
  int nextMask = 0;
  if (btn14 != nullptr && btn14->getToggleState())
    nextMask |= btn14->getDivisionValue();
  if (btn18 != nullptr && btn18->getToggleState())
    nextMask |= btn18->getDivisionValue();
  if (btn116 != nullptr && btn116->getToggleState())
    nextMask |= btn116->getDivisionValue();

  nextMask = normalizeDivisionMask(nextMask);
  if (nextMask == currentDivisionMask)
    return;
  currentDivisionMask = nextMask;
  updateButtonStates();
  if (onDivisionChanged)
    onDivisionChanged(currentDivisionMask);
}

void ClickDivisionPanel::setDivision(const int divisionMask) {
  const int normalized = normalizeDivisionMask(divisionMask);
  if (normalized == currentDivisionMask)
    return;
  currentDivisionMask = normalized;
  updateButtonStates();
}

int ClickDivisionPanel::getDivision() const { return currentDivisionMask; }

void ClickDivisionPanel::updateButtonStates() {
  btn14->setSelected((currentDivisionMask & btn14->getDivisionValue()) != 0);
  btn18->setSelected((currentDivisionMask & btn18->getDivisionValue()) != 0);
  btn116->setSelected((currentDivisionMask & btn116->getDivisionValue()) != 0);
  soloButton->setToggleState(soloed, juce::dontSendNotification);
  muteButton->setToggleState(muted, juce::dontSendNotification);
  repaint();
}
