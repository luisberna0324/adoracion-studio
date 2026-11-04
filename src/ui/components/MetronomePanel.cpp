#include "MetronomePanel.h"
#include "../../data/TempoValue.h"

#include <cmath>
#include <memory>
#include <utility>

MetronomePanel::BpmSegmentButton::BpmSegmentButton()
    : juce::Button("metroBpmSegment") {}

void MetronomePanel::BpmSegmentButton::setBpmText(juce::String newText) {
  bpmText = std::move(newText);
  repaint();
}

void MetronomePanel::BpmSegmentButton::paintButton(juce::Graphics &g,
                                                   bool isMouseOverButton,
                                                   bool isButtonDown) {
  auto b = getLocalBounds().toFloat();
  juce::Colour fill = UITheme::accent;
  if (isMouseOverButton)
    fill = fill.brighter(0.06f);
  if (isButtonDown)
    fill = UITheme::accentSoft;

  juce::Path leftSegment;
  leftSegment.addRoundedRectangle(b.getX(), b.getY(), b.getWidth(),
                                  b.getHeight(), 8.0f, 8.0f, true, false, true,
                                  false);
  g.setColour(fill);
  g.fillPath(leftSegment);
  g.setColour(UITheme::cardBorder.withAlpha(0.7f));
  g.strokePath(leftSegment, juce::PathStrokeType(1.0f));

  auto textArea = getLocalBounds().reduced(8, 2);
  textArea.removeFromBottom(8);
  g.setColour(UITheme::onAccent);
  g.setFont(juce::FontOptions(17.0f).withKerningFactor(0.06f));
  g.drawFittedText(bpmText, textArea, juce::Justification::centred, 1);

  const float cx = b.getCentreX();
  const float cy = b.getBottom() - 7.0f;
  juce::Path caret;
  caret.startNewSubPath(cx - 4.0f, cy - 2.0f);
  caret.lineTo(cx, cy + 2.0f);
  caret.lineTo(cx + 4.0f, cy - 2.0f);
  g.strokePath(caret, juce::PathStrokeType(1.6f));
}

MetronomePanel::MetronomeIconButton::MetronomeIconButton()
    : juce::Button("metroIconSegment") {
  setClickingTogglesState(true);
}

void MetronomePanel::MetronomeIconButton::setBeatPulse(float intensity,
                                                       bool downbeat) {
  pulse = juce::jlimit(0.0f, 1.0f, intensity);
  isDownbeat = downbeat;
  repaint();
}

void MetronomePanel::MetronomeIconButton::paintButton(juce::Graphics &g,
                                                      bool isMouseOverButton,
                                                      bool isButtonDown) {
  auto b = getLocalBounds().toFloat();
  const bool enabled = getToggleState();

  juce::Colour fill = enabled ? UITheme::accent : UITheme::cardBgHover;
  if (enabled && pulse > 0.001f) {
    const auto flashColour =
        isDownbeat ? UITheme::accentSoft : UITheme::onAccent;
    fill = fill.interpolatedWith(flashColour, 0.18f + (pulse * 0.42f));
  }
  if (isMouseOverButton)
    fill = fill.brighter(0.06f);
  if (isButtonDown)
    fill = fill.darker(0.06f);

  juce::Path rightSegment;
  rightSegment.addRoundedRectangle(b.getX(), b.getY(), b.getWidth(),
                                   b.getHeight(), 8.0f, 8.0f, false, true,
                                   false, true);
  g.setColour(fill);
  g.fillPath(rightSegment);
  g.setColour(UITheme::cardBorder.withAlpha(0.7f));
  g.strokePath(rightSegment, juce::PathStrokeType(1.0f));

  const auto iconArea =
      b.reduced(b.getWidth() * 0.26f, b.getHeight() * 0.2f);
  juce::Path body;
  body.startNewSubPath(iconArea.getX() + 1.0f, iconArea.getBottom());
  body.lineTo(iconArea.getCentreX(), iconArea.getY() + 2.0f);
  body.lineTo(iconArea.getRight() - 1.0f, iconArea.getBottom());
  body.closeSubPath();

  const auto iconColour = enabled ? UITheme::onAccent : UITheme::textPrimary;
  g.setColour(iconColour);
  g.strokePath(body, juce::PathStrokeType(1.8f));

  g.drawLine(iconArea.getCentreX(), iconArea.getY() + 7.0f,
             iconArea.getCentreX(), iconArea.getBottom() - 3.0f, 1.5f);
  g.drawLine(iconArea.getCentreX(), iconArea.getCentreY() + 1.0f,
             iconArea.getRight() - 3.0f, iconArea.getY() + 5.0f, 1.5f);
}

MetronomePanel::MetronomePopover::MetronomePopover(
    const juce::String &timeSig, TapCallback onTapCb,
    TimeSignatureCallback onTimeSigCb, BpmSetCallback onBpmSetCb,
    BpmAdjustCallback onBpmAdjustCb, BpmProvider bpmProviderCb)
    : onTap(std::move(onTapCb)),
      onTimeSignatureChanged(std::move(onTimeSigCb)),
      onBpmSet(std::move(onBpmSetCb)),
      onBpmAdjust(std::move(onBpmAdjustCb)),
      bpmProvider(std::move(bpmProviderCb)) {
  tapButton.setButtonText("TAP");
  tapButton.setViewportIgnoreDragFlag(true);
  tapButton.onClick = [this] {
    if (onTap)
      onTap();
    syncBpmEditor();
  };
  addAndMakeVisible(tapButton);

  timeSigBox.addItem("2/4", 1);
  timeSigBox.addItem("3/4", 2);
  timeSigBox.addItem("4/4", 3);
  timeSigBox.addItem("5/4", 4);
  timeSigBox.addItem("6/8", 5);
  timeSigBox.setText(timeSig, juce::dontSendNotification);
  timeSigBox.onChange = [this] {
    if (onTimeSignatureChanged)
      onTimeSignatureChanged(timeSigBox.getText().trim());
  };
  addAndMakeVisible(timeSigBox);

  minusButton.setButtonText("-");
  plusButton.setButtonText("+");
  minusButton.setViewportIgnoreDragFlag(true);
  plusButton.setViewportIgnoreDragFlag(true);
  minusButton.onClick = [this] {
    if (onBpmAdjust)
      onBpmAdjust(-1.0);
    syncBpmEditor();
  };
  plusButton.onClick = [this] {
    if (onBpmAdjust)
      onBpmAdjust(1.0);
    syncBpmEditor();
  };
  addAndMakeVisible(minusButton);
  addAndMakeVisible(plusButton);

  bpmEditor.setInputRestrictions(10, "0123456789.,");
  bpmEditor.setKeyboardType(juce::TextInputTarget::decimalKeyboard);
  bpmEditor.setJustification(juce::Justification::centred);
  bpmEditor.onReturnKey = [this] { submitBpmEditor(); };
  bpmEditor.onFocusLost = [this] { submitBpmEditor(); };
  addAndMakeVisible(bpmEditor);

  syncBpmEditor();
}

void MetronomePanel::MetronomePopover::refreshThemeColours() {
  tapButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  tapButton.setColour(juce::TextButton::buttonOnColourId,
                      UITheme::cardBgPressed);
  tapButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  tapButton.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);

  minusButton.setColour(juce::TextButton::buttonColourId,
                        UITheme::cardBgHover);
  minusButton.setColour(juce::TextButton::buttonOnColourId,
                        UITheme::cardBgPressed);
  minusButton.setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  plusButton.setColour(juce::TextButton::buttonColourId,
                       UITheme::cardBgHover);
  plusButton.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
  plusButton.setColour(juce::TextButton::textColourOffId,
                       UITheme::textPrimary);

  applyComboBoxTheme(timeSigBox, UITheme::cardBgHover,
                     UITheme::cardBorder.withAlpha(0.65f));
  applyTextEditorTheme(bpmEditor, UITheme::cardBgHover,
                       UITheme::cardBorder.withAlpha(0.7f));
  bpmEditor.setJustification(juce::Justification::centred);
  repaint();
}

void MetronomePanel::MetronomePopover::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(b, 6.0f);
  g.setColour(UITheme::cardBorder);
  g.drawRoundedRectangle(b, 6.0f, 1.0f);
}

void MetronomePanel::MetronomePopover::resized() {
  auto area = getLocalBounds().reduced(10);
  const int tapH = 32;
  tapButton.setBounds(area.removeFromTop(tapH));
  area.removeFromTop(8);

  timeSigBox.setBounds(area.removeFromTop(26));
  area.removeFromTop(8);

  auto bpmRow = area.removeFromTop(30);
  minusButton.setBounds(bpmRow.removeFromLeft(28));
  plusButton.setBounds(bpmRow.removeFromRight(28));
  bpmEditor.setBounds(bpmRow.reduced(6, 0));
}

void MetronomePanel::MetronomePopover::syncBpmEditor() {
  if (!bpmProvider)
    return;
  bpmEditor.setText(TempoValue::formatBpm(bpmProvider()),
                    juce::dontSendNotification);
}

void MetronomePanel::MetronomePopover::submitBpmEditor() {
  const auto text = bpmEditor.getText().trim();
  if (!onBpmSet)
    return;
  double bpm = 0.0;
  if (TempoValue::parseBpm(text, bpm))
    onBpmSet(bpm);
  syncBpmEditor();
}

MetronomePanel::MetronomePanel(MTControlsAudioProcessor &processorRef)
    : processor(processorRef) {
  bpmButton.setViewportIgnoreDragFlag(true);
  iconButton.setViewportIgnoreDragFlag(true);

  bpmButton.onClick = [this] { showPopover(); };
  iconButton.onClick = [this] {
    processor.setMetronomeEnabled(iconButton.getToggleState());
  };

  addAndMakeVisible(bpmButton);
  addAndMakeVisible(iconButton);

  applyTimeSignature("4/4");
  refreshFromProcessor();
  refreshThemeColours();
  startTimerHz(20);
}

void MetronomePanel::setSongBpm(double bpm) {
  if (!std::isfinite(bpm) || bpm <= 0.0)
    return;
  manualOverride = false;
  processor.setPlaybackTempoBpm(bpm);
  updateBpmDisplay();
}

void MetronomePanel::refreshThemeColours() { repaint(); }

void MetronomePanel::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat().reduced(4.0f, 6.0f);
  const float separatorX = b.getX() + (b.getWidth() * 0.64f);
  g.setColour(UITheme::cardBorder.withAlpha(0.5f));
  g.drawLine(separatorX, b.getY() + 4.0f, separatorX, b.getBottom() - 4.0f,
             1.0f);
}

void MetronomePanel::resized() {
  auto area = getLocalBounds().reduced(4, 6);
  const int segmentW = static_cast<int>(std::round(area.getWidth() * 0.64f));
  bpmButton.setBounds(area.removeFromLeft(segmentW));
  iconButton.setBounds(area);
}

void MetronomePanel::showPopover() {
  auto panel = std::make_unique<MetronomePopover>(
      timeSignature, [this] { registerTap(); },
      [this](const juce::String &sig) { applyTimeSignature(sig); },
      [this](double bpm) { setBpmAbsolute(bpm); },
      [this](double delta) { adjustBpm(delta); },
      [this] { return processor.getPlaybackTempoBpm(); });
  panel->setSize(230, 120);
  panel->refreshThemeColours();

  presentActionPopover(std::move(panel), bpmButton.getBounds(), this);
}

void MetronomePanel::refreshFromProcessor() {
  updateBpmDisplay();
  iconButton.setToggleState(processor.isMetronomeEnabled(),
                            juce::dontSendNotification);
}

void MetronomePanel::updateBpmDisplay() {
  bpmButton.setBpmText(TempoValue::formatBpm(processor.getPlaybackTempoBpm()));
}

void MetronomePanel::setBpmAbsolute(double bpm) {
  if (!std::isfinite(bpm) || bpm <= 0.0)
    return;
  manualOverride = true;
  processor.setPlaybackTempoBpm(bpm);
  updateBpmDisplay();
}

void MetronomePanel::adjustBpm(double delta) {
  const auto mods = juce::ModifierKeys::getCurrentModifiers();
  const double step = mods.isShiftDown() ? (delta < 0.0 ? -5.0 : 5.0) : delta;
  setBpmAbsolute(processor.getPlaybackTempoBpm() + step);
}

void MetronomePanel::registerTap() {
  const double now = juce::Time::getMillisecondCounterHiRes();
  if (!tapTimes.empty() && (now - tapTimes.back()) > 2000.0)
    tapTimes.clear();
  tapTimes.push_back(now);
  while (tapTimes.size() > 6)
    tapTimes.pop_front();

  if (tapTimes.size() < 2)
    return;

  double sum = 0.0;
  for (size_t i = 1; i < tapTimes.size(); ++i)
    sum += (tapTimes[i] - tapTimes[i - 1]);
  const double avgMs = sum / static_cast<double>(tapTimes.size() - 1);
  if (avgMs <= 0.0)
    return;

  setBpmAbsolute(60000.0 / avgMs);
}

void MetronomePanel::applyTimeSignature(const juce::String &sigText) {
  timeSignature = sigText.trim();
  if (timeSignature.isEmpty())
    timeSignature = "4/4";
  beatsPerBar = timeSignatureBeatsPerBar(timeSignature);
  processor.setMetronomeBeatsPerBar(beatsPerBar);
  beatCounter = 0.0;
  lastBeatIndex = -1;
  lastBeatWasDownbeat = true;
}

void MetronomePanel::timerCallback() {
  const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
  if (lastTickTime == 0.0)
    lastTickTime = now;
  const double dt = now - lastTickTime;
  lastTickTime = now;

  refreshFromProcessor();

  if (processor.isMetronomeEnabled()) {
    const double bpm = processor.getMetronomeBpm();
    if (bpm > 0.0) {
      beatCounter += dt * (bpm / 60.0);
      const int beatIndex = static_cast<int>(std::floor(beatCounter));
      if (beatIndex != lastBeatIndex) {
        lastBeatIndex = beatIndex;
        lastBeatWasDownbeat =
            (beatIndex % juce::jmax(1, beatsPerBar)) == 0;
        beatFlash = 1.0f;
      }
    }
  } else {
    beatCounter = 0.0;
    lastBeatIndex = -1;
    beatFlash = 0.0f;
  }

  beatFlash = juce::jmax(0.0f, beatFlash - 0.08f);
  const float pulse = processor.isMetronomeEnabled() ? beatFlash : 0.0f;
  iconButton.setBeatPulse(pulse, lastBeatWasDownbeat);
}
