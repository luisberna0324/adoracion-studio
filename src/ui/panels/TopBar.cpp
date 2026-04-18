#include "TopBar.h"

#include "../../PluginProcessor.h"
#include "../../data/Localization.h"

#include <cmath>
#include <iterator>

namespace {
struct NoteOption {
  const char *label;
  int semitone;
};

constexpr NoteOption kNoteOptions[] = {{"Ab", 8}, {"A", 9},  {"Bb", 10},
                                       {"B", 11}, {"C", 0},  {"Db", 1},
                                       {"D", 2},  {"Eb", 3}, {"E", 4},
                                       {"F", 5},  {"Gb", 6}, {"G", 7}};

int normalizeSemitone(int semitone) {
  const int mod = semitone % 12;
  return mod < 0 ? mod + 12 : mod;
}

juce::String noteLabelForSemitone(int semitone) {
  const int normalized = normalizeSemitone(semitone);
  for (const auto &opt : kNoteOptions) {
    if (opt.semitone == normalized)
      return opt.label;
  }
  return "C";
}

bool parseKeySemitone(const juce::String &keyText, int &semitoneOut) {
  auto token = keyText.trim();
  if (token.isEmpty())
    return false;
  token = token.upToFirstOccurrenceOf(" ", false, false).trim();
  if (token.isEmpty())
    return false;

  const auto letter = token.substring(0, 1).toUpperCase();
  juce::String accidental;
  if (token.length() > 1) {
    const auto second = token.substring(1, 2);
    if (second == "#" || second == "b" || second == "B")
      accidental = (second == "B") ? "b" : second;
  }

  const auto normalized = (letter + accidental).toUpperCase();
  if (normalized == "C")
    semitoneOut = 0;
  else if (normalized == "C#" || normalized == "DB")
    semitoneOut = 1;
  else if (normalized == "D")
    semitoneOut = 2;
  else if (normalized == "D#" || normalized == "EB")
    semitoneOut = 3;
  else if (normalized == "E")
    semitoneOut = 4;
  else if (normalized == "F")
    semitoneOut = 5;
  else if (normalized == "F#" || normalized == "GB")
    semitoneOut = 6;
  else if (normalized == "G")
    semitoneOut = 7;
  else if (normalized == "G#" || normalized == "AB")
    semitoneOut = 8;
  else if (normalized == "A")
    semitoneOut = 9;
  else if (normalized == "A#" || normalized == "BB")
    semitoneOut = 10;
  else if (normalized == "B")
    semitoneOut = 11;
  else
    return false;

  return true;
}

int semitoneDelta(int from, int to) {
  int delta = normalizeSemitone(to) - normalizeSemitone(from);
  if (delta > 6)
    delta -= 12;
  else if (delta < -6)
    delta += 12;
  return delta;
}
} // namespace

TopBar::IconButton::IconButton(juce::String name, IconGlyph g, bool primary)
    : juce::Button(std::move(name)), glyph(g), isPrimary(primary) {}

void TopBar::IconButton::setGlyph(IconGlyph g) {
  glyph = g;
  repaint();
}

void TopBar::IconButton::setBadgeText(juce::String badge) {
  badgeText = std::move(badge);
  repaint();
}

void TopBar::IconButton::setPulseAmount(float amount, bool downbeat) {
  pulseAmount = juce::jlimit(0.0f, 1.0f, amount);
  pulseDownbeat = downbeat;
  repaint();
}

void TopBar::IconButton::paintButton(juce::Graphics &gr,
                                     bool isMouseOverButton,
                                     bool isButtonDown) {
  auto b = getLocalBounds().toFloat();
  juce::Colour fill = isPrimary ? UITheme::accent : UITheme::cardBg;
  if (isMouseOverButton)
    fill = fill.brighter(0.06f);
  if (isButtonDown || getToggleState())
    fill = isPrimary ? UITheme::accentSoft : UITheme::cardBgPressed;
  if (pulseAmount > 0.001f) {
    const auto pulseColour =
        pulseDownbeat ? UITheme::accentSoft : UITheme::accent;
    fill = fill.interpolatedWith(pulseColour, 0.25f + (pulseAmount * 0.35f));
  }

  gr.setColour(fill);
  gr.fillRoundedRectangle(b, 6.0f);
  gr.setColour(UITheme::cardBorder.withAlpha(0.72f));
  gr.drawRoundedRectangle(b, 6.0f, 1.0f);
  drawIconGlyph(gr, b, glyph,
                isPrimary ? UITheme::onAccent : UITheme::textPrimary);

  if (badgeText.isNotEmpty()) {
    gr.setColour(isPrimary ? UITheme::onAccent.withAlpha(0.92f)
                           : UITheme::textSecondary.withAlpha(0.88f));
    gr.setFont(juce::FontOptions(9.0f).withKerningFactor(0.04f));
    gr.drawFittedText(badgeText, getLocalBounds().withTrimmedTop(24),
                      juce::Justification::centredTop, 1);
  }
}

void TopBar::ClickableLabel::mouseDown(const juce::MouseEvent &) {
  if (onClick)
    onClick();
}

TopBar::TopBar(MTControlsAudioProcessor &processorRef)
    : prevButton("prev", IconGlyph::Prev),
      seekBackButton("seekBack", IconGlyph::SeekBack),
      playButton("play", IconGlyph::Play, true),
      stopButton("stop", IconGlyph::Stop),
      seekForwardButton("seekForward", IconGlyph::SeekForward),
      nextButton("next", IconGlyph::Next, true),
      fadeButton("fade", IconGlyph::FadeOut),
      windowModeButton("windowMode", IconGlyph::WindowMaximize),
      settingsButton("settings", IconGlyph::Settings),
      processor(processorRef) {
  for (auto *btn :
       {&prevButton, &seekBackButton, &playButton, &stopButton,
        &seekForwardButton, &nextButton, &fadeButton, &windowModeButton,
        &settingsButton}) {
    addAndMakeVisible(*btn);
  }

  configureDisplay(timeDisplay, "00:00 / 00:00");
  configureDisplay(keyDisplay, L10n::tr("Tono", "Key"));
  activeKeyLabel = L10n::tr("Tono", "Key");
  applyResponsiveTypography();
  seekBackButton.setBadgeText("-5s");
  seekForwardButton.setBadgeText("+5s");
  seekBackButton.setTooltip(
      L10n::tr("Retroceder 5 segundos", "Seek back 5 seconds"));
  seekForwardButton.setTooltip(
      L10n::tr("Avanzar 5 segundos", "Seek forward 5 seconds"));
  nextButton.setTooltip(L10n::tr("Siguiente cancion", "Next song"));
  timeDisplay.onClick = [this] { showSongWindowPopover(); };
  keyDisplay.onClick = [this] { showKeyPopover(); };
  configureMetronomeControls();

  prevButton.onClick = [this] {
    clearFadeIfActive();
    if (onPrevSong)
      onPrevSong();
  };
  seekBackButton.onClick = [this] {
    clearFadeIfActive();
    if (onSeekRelative)
      onSeekRelative(-5.0);
  };
  seekForwardButton.onClick = [this] {
    clearFadeIfActive();
    if (onSeekRelative)
      onSeekRelative(5.0);
  };
  nextButton.onClick = [this] {
    clearFadeIfActive();
    if (onNextSong)
      onNextSong();
  };
  fadeButton.onClick = [this] {
    const bool shouldFadeOut = !processor.isFadeActive();
    if (onFadeToggle)
      onFadeToggle(shouldFadeOut);
  };
  playButton.onClick = [this] {
    clearFadeIfActive();
    if (onPlayPause)
      onPlayPause();
    refreshFromProviders();
  };
  stopButton.onClick = [this] {
    clearFadeIfActive();
    if (onStop)
      onStop();
    refreshFromProviders();
  };
  windowModeButton.onClick = [this] {
    clearFadeIfActive();
    toggleTopLevelWindowMaximised(this);
    const bool maxed = isTopLevelWindowMaximised(this);
    windowModeButton.setGlyph(maxed ? IconGlyph::WindowRestore
                                    : IconGlyph::WindowMaximize);
    lastTopLevelMaximised = maxed;
  };
  settingsButton.onClick = [this] {
    clearFadeIfActive();
    if (onSettingsWithAnchor)
      onSettingsWithAnchor(settingsButton.getBounds(), this);
    else if (onSettings)
      onSettings();
  };
  bpmSegmentButton.onClick = [this] { showMetronomePopover(); };
  metroToggleButton.onClick = [this] {
    processor.setMetronomeEnabled(metroToggleButton.getToggleState());
  };
  startTimerHz(20);
}

TopBar::~TopBar() { stopTimer(); }

void TopBar::setTransportCallbacks(
    std::function<void()> playPauseCallback,
    std::function<void()> stopCallback,
    std::function<void()> prevSongCallback,
    std::function<void()> nextSongCallback,
    std::function<void(bool)> fadeToggleCallback,
    std::function<void(double)> seekRelativeCallback) {
  onPlayPause = std::move(playPauseCallback);
  onStop = std::move(stopCallback);
  onPrevSong = std::move(prevSongCallback);
  onNextSong = std::move(nextSongCallback);
  onFadeToggle = std::move(fadeToggleCallback);
  onSeekRelative = std::move(seekRelativeCallback);
}

void TopBar::setTransportProviders(std::function<bool()> playingProviderFn,
                                   std::function<double()> positionProviderFn,
                                   std::function<double()> lengthProviderFn,
                                   std::function<juce::String()> keyProviderFn) {
  playingProvider = std::move(playingProviderFn);
  positionProvider = std::move(positionProviderFn);
  lengthProvider = std::move(lengthProviderFn);
  keyProvider = std::move(keyProviderFn);
  refreshFromProviders();
}

void TopBar::setOnSettings(std::function<void()> cb) {
  onSettings = std::move(cb);
}

void TopBar::setOnSettingsWithAnchor(
    std::function<void(juce::Rectangle<int>, juce::Component *)> cb) {
  onSettingsWithAnchor = std::move(cb);
}

void TopBar::setResponsiveProfile(const ResponsiveLayoutProfile profile) {
  if (responsiveProfile == profile)
    return;
  responsiveProfile = profile;
  applyResponsiveTypography();
  resized();
  repaint();
}

void TopBar::refreshThemeColours() {
  timeDisplay.setColour(juce::Label::textColourId, UITheme::textPrimary);
  timeDisplay.setColour(juce::Label::backgroundColourId, UITheme::cardBg);
  timeDisplay.setColour(juce::Label::outlineColourId,
                        UITheme::cardBorder.withAlpha(0.55f));
  keyDisplay.setColour(juce::Label::textColourId, UITheme::textPrimary);
  keyDisplay.setColour(juce::Label::backgroundColourId, UITheme::cardBg);
  keyDisplay.setColour(juce::Label::outlineColourId,
                       UITheme::cardBorder.withAlpha(0.55f));
  bpmSegmentButton.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  bpmSegmentButton.setColour(juce::TextButton::buttonOnColourId,
                             UITheme::accentSoft);
  bpmSegmentButton.setColour(juce::TextButton::textColourOffId,
                             UITheme::onAccent);
  bpmSegmentButton.setColour(juce::TextButton::textColourOnId,
                             UITheme::onAccent);
  repaint();
}

void TopBar::paint(juce::Graphics &g) {
  const auto b = getLocalBounds().toFloat();
  drawStudioBackground(g, b);
  g.setColour(UITheme::panelDark.withAlpha(0.10f));
  g.fillRect(b);
  g.setColour(UITheme::dividerSoft);
  g.drawLine(0.0f, static_cast<float>(getHeight()) - 0.5f,
             static_cast<float>(getWidth()),
             static_cast<float>(getHeight()) - 0.5f, 1.0f);
}

void TopBar::resized() {
  int y = 10;
  int square = 44;
  int gap = 8;
  int keyWidth = 72;
  int metroTotalW = 110;
  int metroIconW = 44;
  int timeWidth = 120;

  if (responsiveProfile == ResponsiveLayoutProfile::Compact) {
    y = 8;
    square = 38;
    gap = 6;
    keyWidth = 64;
    metroTotalW = 92;
    metroIconW = 38;
    timeWidth = 96;
  } else if (responsiveProfile == ResponsiveLayoutProfile::Large) {
    y = 10;
    square = 46;
    gap = 8;
    keyWidth = 76;
    metroTotalW = 114;
    metroIconW = 46;
    timeWidth = 128;
  }

  int x = 12;

  auto placeSquare = [&](juce::Component &c) {
    c.setBounds(x, y - 2, square, square);
    x += square + gap;
  };

  placeSquare(prevButton);
  placeSquare(seekBackButton);
  placeSquare(playButton);
  placeSquare(stopButton);
  placeSquare(seekForwardButton);
  placeSquare(nextButton);
  placeSquare(fadeButton);

  const int rightPadding = 12;
  const int fixedButtonsW = (2 * square) + gap;
  const int transportW = x;
  const int availableDynamicW =
      getWidth() - rightPadding - transportW - fixedButtonsW - 8;

  const int minTimeW = responsiveProfile == ResponsiveLayoutProfile::Compact ? 72 : 88;
  const int minKeyW = responsiveProfile == ResponsiveLayoutProfile::Compact ? 52 : 60;
  const int metroCoreW = metroTotalW;
  bool showTime = true;
  bool showKey = true;
  bool showBpmText = true;

  int wantedDynamicW = timeWidth + gap + metroCoreW + gap + keyWidth;
  if (availableDynamicW < wantedDynamicW) {
    const int overflow = wantedDynamicW - availableDynamicW;
    const int timeShrink = juce::jmin(overflow, timeWidth - minTimeW);
    timeWidth -= timeShrink;
    wantedDynamicW -= timeShrink;
  }
  if (availableDynamicW < wantedDynamicW) {
    const int overflow = wantedDynamicW - availableDynamicW;
    const int keyShrink = juce::jmin(overflow, keyWidth - minKeyW);
    keyWidth -= keyShrink;
    wantedDynamicW -= keyShrink;
  }
  if (availableDynamicW < wantedDynamicW) {
    showTime = false;
    wantedDynamicW -= (timeWidth + gap);
  }
  if (availableDynamicW < wantedDynamicW) {
    showKey = false;
    wantedDynamicW -= (keyWidth + gap);
  }
  if (availableDynamicW < wantedDynamicW) {
    showBpmText = false;
    wantedDynamicW -= (metroTotalW - metroIconW);
  }

  timeDisplay.setVisible(showTime);
  keyDisplay.setVisible(showKey);
  bpmSegmentButton.setVisible(showBpmText);

  int rightX = getWidth() - 12;
  settingsButton.setBounds(rightX - square, y - 2, square, square);
  rightX -= square + gap;
  windowModeButton.setBounds(rightX - square, y - 2, square, square);
  rightX -= square + gap;

  if (showKey) {
    keyDisplay.setBounds(rightX - keyWidth, y, keyWidth, 40);
    rightX -= keyWidth + gap;
  } else {
    keyDisplay.setBounds({});
  }

  const int metroY = y - 2;
  const int metroTextW = showBpmText ? (metroTotalW - metroIconW) : 0;
  const int metroUsedW = showBpmText ? metroTotalW : metroIconW;
  if (showBpmText)
    bpmSegmentButton.setBounds(rightX - metroTotalW, metroY, metroTextW, 44);
  else
    bpmSegmentButton.setBounds({});
  metroToggleButton.setBounds(rightX - metroIconW, metroY, metroIconW, 44);
  rightX -= metroUsedW + gap;
  if (showTime)
    timeDisplay.setBounds(rightX - timeWidth, y, timeWidth, 40);
  else
    timeDisplay.setBounds({});

  if (showTime && timeDisplay.getX() < x + 8) {
    const int shift = (x + 8) - timeDisplay.getX();
    for (auto *c :
         {static_cast<juce::Component *>(&timeDisplay),
          static_cast<juce::Component *>(&bpmSegmentButton),
          static_cast<juce::Component *>(&metroToggleButton),
          static_cast<juce::Component *>(&keyDisplay),
          static_cast<juce::Component *>(&windowModeButton),
          static_cast<juce::Component *>(&settingsButton)}) {
      c->setTopLeftPosition(c->getX() + shift, c->getY());
    }
  }
}

void TopBar::clearFadeIfActive() {
  if (!processor.isFadeActive())
    return;
  if (onFadeToggle)
    onFadeToggle(false);
}

void TopBar::applyResponsiveTypography() {
  float displayFont = 13.0f;
  float keyFont = 15.0f;
  if (responsiveProfile == ResponsiveLayoutProfile::Compact) {
    displayFont = 11.5f;
    keyFont = 13.0f;
  } else if (responsiveProfile == ResponsiveLayoutProfile::Large) {
    displayFont = 13.5f;
    keyFont = 15.5f;
  }
  timeDisplay.setFont(juce::FontOptions(displayFont));
  keyDisplay.setFont(juce::FontOptions(keyFont).withStyle("Bold"));
}

void TopBar::configureDisplay(juce::Label &label, const juce::String &text) {
  label.setText(text, juce::dontSendNotification);
  label.setJustificationType(juce::Justification::centred);
  label.setFont(juce::FontOptions(13.0f));
  label.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  label.setColour(juce::Label::textColourId, UITheme::textPrimary);
  label.setColour(juce::Label::backgroundColourId, UITheme::cardBg);
  label.setColour(juce::Label::outlineColourId,
                  UITheme::cardBorder.withAlpha(0.55f));
  addAndMakeVisible(label);
}

void TopBar::configureMetronomeControls() {
  const auto arrow =
      juce::String::charToString(static_cast<juce::juce_wchar>(0x25BC));
  bpmSegmentButton.setButtonText(juce::String("120 ") + arrow);
  bpmSegmentButton.setViewportIgnoreDragFlag(true);
  bpmSegmentButton.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  bpmSegmentButton.setColour(juce::TextButton::buttonOnColourId,
                             UITheme::accentSoft);
  bpmSegmentButton.setColour(juce::TextButton::textColourOffId,
                             UITheme::onAccent);
  bpmSegmentButton.setColour(juce::TextButton::textColourOnId,
                             UITheme::onAccent);
  bpmSegmentButton.setConnectedEdges(juce::Button::ConnectedOnRight);
  addAndMakeVisible(bpmSegmentButton);

  metroToggleButton.setClickingTogglesState(true);
  metroToggleButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
  addAndMakeVisible(metroToggleButton);
}

void TopBar::timerCallback() {
  refreshFromProviders();

  const bool maxed = isTopLevelWindowMaximised(this);
  if (maxed != lastTopLevelMaximised) {
    lastTopLevelMaximised = maxed;
    windowModeButton.setGlyph(maxed ? IconGlyph::WindowRestore
                                    : IconGlyph::WindowMaximize);
  }

  const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
  if (lastTickTime == 0.0)
    lastTickTime = now;
  const double dt = now - lastTickTime;
  lastTickTime = now;

  const bool metronomeEnabled = processor.isMetronomeEnabled();
  if (metronomeEnabled) {
    const double bpm = processor.getMetronomeBpm();
    if (bpm > 0.0) {
      const bool transportPlaying = playingProvider ? playingProvider() : false;
      double beatPosition = 0.0;
      if (transportPlaying) {
        const double currentPos =
            positionProvider ? juce::jmax(0.0, positionProvider()) : 0.0;
        beatPosition = (currentPos * bpm) / 60.0;
        beatCounter = beatPosition;
      } else {
        if (!lastMetronomeEnabled && beatCounter <= 0.0)
          beatCounter = 0.0;
        beatCounter += (dt * bpm) / 60.0;
        beatPosition = beatCounter;
      }
      const int beatIndex = static_cast<int>(std::floor(beatPosition));
      if (beatIndex != lastBeatIndex) {
        lastBeatIndex = beatIndex;
        lastBeatWasDownbeat = (beatIndex % juce::jmax(1, beatsPerBar)) == 0;
        beatFlash = 1.0f;
      }
    }
  } else {
    beatCounter = 0.0;
    lastBeatIndex = -1;
    beatFlash = 0.0f;
  }
  lastMetronomeEnabled = metronomeEnabled;

  beatFlash = juce::jmax(0.0f, beatFlash - static_cast<float>(dt * 1.6));
  metroToggleButton.setToggleState(processor.isMetronomeEnabled(),
                                   juce::dontSendNotification);
  metroToggleButton.setPulseAmount(
      processor.isMetronomeEnabled() ? beatFlash : 0.0f,
      lastBeatWasDownbeat);
}

void TopBar::updateBpmDisplay() {
  const int bpm = static_cast<int>(std::round(processor.getMetronomeBpm()));
  const auto arrow =
      juce::String::charToString(static_cast<juce::juce_wchar>(0x25BC));
  bpmSegmentButton.setButtonText(
      (bpm > 0 ? juce::String(bpm) : "120") + " " + arrow);
}

void TopBar::applyTimeSignature(const juce::String &sigText) {
  timeSignature = sigText.trim();
  const int numerator = timeSignature.upToFirstOccurrenceOf("/", false, false)
                            .getIntValue();
  beatsPerBar = numerator > 0 ? numerator : 4;
  processor.setMetronomeBeatsPerBar(beatsPerBar);
  beatCounter = 0.0;
  lastBeatIndex = -1;
  lastBeatWasDownbeat = true;
}

void TopBar::setBpmAbsolute(double bpm) {
  if (bpm <= 0.0)
    return;
  processor.setMetronomeBpm(bpm);
  updateBpmDisplay();
}

void TopBar::adjustBpm(double delta) {
  const auto mods = juce::ModifierKeys::getCurrentModifiers();
  const double step = mods.isShiftDown() ? (delta < 0.0 ? -5.0 : 5.0) : delta;
  setBpmAbsolute(processor.getMetronomeBpm() + step);
}

void TopBar::registerTap() {
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

void TopBar::showMetronomePopover() {
  class MetronomePopover final : public juce::Component {
  public:
    MetronomePopover(std::function<void()> onTapCb,
                     std::function<void(const juce::String &)> onTimeSigCb,
                     std::function<void(double)> onSetBpmCb,
                     std::function<void(double)> onAdjustBpmCb,
                     std::function<double()> bpmProviderCb,
                     const juce::String &timeSig)
        : onTap(std::move(onTapCb)), onTimeSig(std::move(onTimeSigCb)),
          onSetBpm(std::move(onSetBpmCb)),
          onAdjustBpm(std::move(onAdjustBpmCb)),
          bpmProvider(std::move(bpmProviderCb)) {
      tapButton.setButtonText("TAP");
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
        if (onTimeSig)
          onTimeSig(timeSigBox.getText().trim());
      };
      addAndMakeVisible(timeSigBox);

      minusButton.setButtonText("-");
      plusButton.setButtonText("+");
      minusButton.onClick = [this] {
        if (onAdjustBpm)
          onAdjustBpm(-1.0);
        syncBpmEditor();
      };
      plusButton.onClick = [this] {
        if (onAdjustBpm)
          onAdjustBpm(1.0);
        syncBpmEditor();
      };
      addAndMakeVisible(minusButton);
      addAndMakeVisible(plusButton);

      bpmEditor.setInputRestrictions(4, "0123456789.");
      bpmEditor.setJustification(juce::Justification::centred);
      bpmEditor.onReturnKey = [this] { submitBpmEditor(); };
      bpmEditor.onFocusLost = [this] { submitBpmEditor(); };
      addAndMakeVisible(bpmEditor);
      applyTheme();
      syncBpmEditor();
    }

    void paint(juce::Graphics &g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(b, 6.0f);
      g.setColour(UITheme::cardBorder);
      g.drawRoundedRectangle(b, 6.0f, 1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(10);
      tapButton.setBounds(area.removeFromTop(32));
      area.removeFromTop(8);
      timeSigBox.setBounds(area.removeFromTop(26));
      area.removeFromTop(8);
      auto row = area.removeFromTop(30);
      minusButton.setBounds(row.removeFromLeft(28));
      plusButton.setBounds(row.removeFromRight(28));
      bpmEditor.setBounds(row.reduced(6, 0));
    }

  private:
    juce::TextButton tapButton, minusButton, plusButton;
    juce::ComboBox timeSigBox;
    juce::TextEditor bpmEditor;
    std::function<void()> onTap;
    std::function<void(const juce::String &)> onTimeSig;
    std::function<void(double)> onSetBpm;
    std::function<void(double)> onAdjustBpm;
    std::function<double()> bpmProvider;

    void applyTheme() {
      for (auto *btn : {&tapButton, &minusButton, &plusButton}) {
        btn->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
        btn->setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
        btn->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
      }
      timeSigBox.setColour(juce::ComboBox::backgroundColourId,
                           UITheme::cardBgHover);
      timeSigBox.setColour(juce::ComboBox::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.65f));
      timeSigBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
      timeSigBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
      bpmEditor.setColour(juce::TextEditor::backgroundColourId,
                          UITheme::cardBgHover);
      bpmEditor.setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
      bpmEditor.setColour(juce::TextEditor::outlineColourId,
                          UITheme::cardBorder.withAlpha(0.7f));
    }

    void syncBpmEditor() {
      if (!bpmProvider)
        return;
      bpmEditor.setText(
          juce::String(static_cast<int>(std::round(bpmProvider()))),
          juce::dontSendNotification);
    }

    void submitBpmEditor() {
      if (!onSetBpm)
        return;
      const double bpm = bpmEditor.getText().trim().getDoubleValue();
      if (bpm > 0.0)
        onSetBpm(bpm);
      syncBpmEditor();
    }
  };

  auto panel = std::make_unique<MetronomePopover>(
      [this] { registerTap(); },
      [this](const juce::String &sig) { applyTimeSignature(sig); },
      [this](double bpm) { setBpmAbsolute(bpm); },
      [this](double delta) { adjustBpm(delta); },
      [this] { return processor.getMetronomeBpm(); }, timeSignature);
  panel->setSize(180, 120);
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor = parentComp->getLocalArea(this, bpmSegmentButton.getBounds());
  auto &callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
                                                         anchor, parentComp);
  callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void TopBar::showKeyPopover() {
  class NoteChip final : public juce::Button {
  public:
    explicit NoteChip(const juce::String &labelText)
        : juce::Button(labelText), label(labelText) {}

    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override {
      auto b = getLocalBounds().toFloat();
      auto fill = getToggleState() ? UITheme::accent : UITheme::cardBgHover;
      if (isMouseOverButton)
        fill = fill.brighter(0.06f);
      if (isButtonDown)
        fill = fill.darker(0.05f);
      g.setColour(fill);
      const float radius = b.getHeight() * 0.5f;
      g.fillRoundedRectangle(b, radius);
      g.setColour(UITheme::cardBorder.withAlpha(0.6f));
      g.drawRoundedRectangle(b, radius, 1.0f);
      g.setColour(getToggleState() ? UITheme::onAccent : UITheme::textPrimary);
      g.setFont(juce::FontOptions(12.0f));
      g.drawFittedText(label, getLocalBounds(), juce::Justification::centred,
                       1);
    }

  private:
    juce::String label;
  };

  class KeyPopover final : public juce::Component {
  public:
    KeyPopover(int activeSemitone, std::function<void(int)> onSelectFn)
        : onSelect(std::move(onSelectFn)) {
      for (size_t i = 0; i < std::size(kNoteOptions); ++i) {
        auto chip =
            std::make_unique<NoteChip>(kNoteOptions[i].label);
        chip->setClickingTogglesState(true);
        const bool isActive = kNoteOptions[i].semitone == activeSemitone;
        chip->setToggleState(isActive, juce::dontSendNotification);
        chip->onClick = [this, i] {
          setActiveIndex(static_cast<int>(i));
          if (onSelect)
            onSelect(kNoteOptions[i].semitone);
        };
        addAndMakeVisible(*chip);
        chips.push_back(std::move(chip));
      }
    }

    void paint(juce::Graphics &g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(b, 8.0f);
      g.setColour(UITheme::cardBorder);
      g.drawRoundedRectangle(b, 8.0f, 1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(12, 10);
      const int count = static_cast<int>(chips.size());
      const int gap = 6;
      const int chipH = 28;
      const int totalGap = gap * (count - 1);
      const int chipW =
          juce::jmax(28, (area.getWidth() - totalGap) / count);
      int x = area.getX();
      const int y = area.getCentreY() - (chipH / 2);
      for (auto &chip : chips) {
        chip->setBounds(x, y, chipW, chipH);
        x += chipW + gap;
      }
    }

  private:
    std::vector<std::unique_ptr<NoteChip>> chips;
    std::function<void(int)> onSelect;

    void setActiveIndex(int index) {
      for (int i = 0; i < static_cast<int>(chips.size()); ++i) {
        chips[static_cast<size_t>(i)]->setToggleState(
            i == index, juce::dontSendNotification);
      }
    }
  };

  if (!processor.hasLoadedSong())
    return;

  auto panel = std::make_unique<KeyPopover>(
      activeKeySemitone, [this](int semitone) {
        activeKeySemitone = normalizeSemitone(semitone);
        activeKeyLabel = noteLabelForSemitone(activeKeySemitone);
        keyDisplay.setText(activeKeyLabel, juce::dontSendNotification);
        processor.setPitchSemiTones(
            semitoneDelta(baseKeySemitone, activeKeySemitone));
      });

  panel->setSize(480, 56);
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor = parentComp->getLocalArea(this, keyDisplay.getBounds());
  auto &callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
                                                         anchor, parentComp);
  callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void TopBar::showSongWindowPopover() {
  class SongWindowPopover final : public juce::Component {
  public:
    SongWindowPopover(MTControlsAudioProcessor &processorRef,
                      const bool readOnlyArrangement)
        : processor(processorRef), arrangementReadOnly(readOnlyArrangement) {
      configureRow(startLabel, startMinus, startValue, startPlus,
                   L10n::tr("Inicio", "Start"));
      configureRow(endLabel, endMinus, endValue, endPlus,
                   L10n::tr("Fin", "End"));
      configureRow(fadeLabel, fadeMinus, fadeValue, fadePlus,
                   L10n::tr("Salida", "Fade out"));

      startMinus.onClick = [this] { adjustStart(-stepSeconds()); };
      startPlus.onClick = [this] { adjustStart(stepSeconds()); };
      endMinus.onClick = [this] { adjustEnd(-stepSeconds()); };
      endPlus.onClick = [this] { adjustEnd(stepSeconds()); };
      fadeMinus.onClick = [this] { adjustFade(-stepSeconds()); };
      fadePlus.onClick = [this] { adjustFade(stepSeconds()); };

      for (auto *btn : {&startMinus, &startPlus, &endMinus, &endPlus,
                        &fadeMinus, &fadePlus}) {
        btn->setEnabled(!arrangementReadOnly);
      }

      if (arrangementReadOnly) {
        arrangementInfoLabel.setText(
            L10n::tr("Arreglo personalizado activo: Inicio/Fin/Salida bloqueados",
                     "Custom arrangement active: Start/End/Fade locked"),
            juce::dontSendNotification);
        arrangementInfoLabel.setJustificationType(juce::Justification::centred);
        arrangementInfoLabel.setFont(juce::FontOptions(11.0f));
        addAndMakeVisible(arrangementInfoLabel);
      }

      refreshValues();
      applyTheme();
    }

    void refreshValues() {
      startValue.setText(formatTimeSeconds(processor.getSongStartSeconds()),
                         juce::dontSendNotification);
      endValue.setText(formatTimeSeconds(processor.getSongEndSeconds()),
                       juce::dontSendNotification);
      const int fadeSecs =
          static_cast<int>(std::round(processor.getSongFadeOutSeconds()));
      fadeValue.setText(juce::String(fadeSecs) + " " + L10n::tr("seg", "sec"),
                        juce::dontSendNotification);
    }

    void applyTheme() {
      for (auto *label : {&startLabel, &endLabel, &fadeLabel}) {
        label->setColour(juce::Label::textColourId, UITheme::textSecondary);
        label->setFont(juce::FontOptions(11.0f));
      }
      for (auto *value : {&startValue, &endValue, &fadeValue}) {
        value->setColour(juce::Label::textColourId, UITheme::textPrimary);
        value->setColour(juce::Label::backgroundColourId,
                         UITheme::cardBgHover);
        value->setColour(juce::Label::outlineColourId,
                         UITheme::cardBorder.withAlpha(0.65f));
        value->setJustificationType(juce::Justification::centred);
        value->setFont(juce::FontOptions(12.0f));
      }
      for (auto *btn : {&startMinus, &startPlus, &endMinus, &endPlus,
                        &fadeMinus, &fadePlus}) {
        btn->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
        btn->setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
        btn->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
        btn->setViewportIgnoreDragFlag(true);
      }
      if (arrangementReadOnly) {
        arrangementInfoLabel.setColour(juce::Label::textColourId,
                                       UITheme::accent);
      }
    }

    void paint(juce::Graphics &g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(b, 8.0f);
      g.setColour(UITheme::cardBorder);
      g.drawRoundedRectangle(b, 8.0f, 1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(12);
      const int rowH = 28;
      layoutRow(area.removeFromTop(rowH), startLabel, startMinus, startValue,
                startPlus);
      area.removeFromTop(8);
      layoutRow(area.removeFromTop(rowH), endLabel, endMinus, endValue,
                endPlus);
      area.removeFromTop(8);
      layoutRow(area.removeFromTop(rowH), fadeLabel, fadeMinus, fadeValue,
                fadePlus);
      if (arrangementReadOnly) {
        area.removeFromTop(6);
        arrangementInfoLabel.setBounds(area.removeFromTop(18));
      }
    }

  private:
    MTControlsAudioProcessor &processor;
    bool arrangementReadOnly = false;
    juce::Label startLabel, endLabel, fadeLabel;
    juce::TextButton startMinus, startPlus, endMinus, endPlus, fadeMinus,
        fadePlus;
    juce::Label startValue, endValue, fadeValue;
    juce::Label arrangementInfoLabel;

    void configureRow(juce::Label &label, juce::TextButton &minus,
                      juce::Label &value, juce::TextButton &plus,
                      const juce::String &text) {
      label.setText(text, juce::dontSendNotification);
      addAndMakeVisible(label);

      minus.setButtonText("-");
      plus.setButtonText("+");
      addAndMakeVisible(minus);
      addAndMakeVisible(plus);

      addAndMakeVisible(value);
    }

    void layoutRow(juce::Rectangle<int> row, juce::Label &label,
                   juce::TextButton &minus, juce::Label &value,
                   juce::TextButton &plus) {
      const int labelW = 60;
      const int btnW = 28;
      label.setBounds(row.removeFromLeft(labelW));
      minus.setBounds(row.removeFromLeft(btnW));
      plus.setBounds(row.removeFromRight(btnW));
      value.setBounds(row);
    }

    double stepSeconds() const {
      const auto mods = juce::ModifierKeys::getCurrentModifiers();
      return mods.isShiftDown() ? 5.0 : 1.0;
    }

    void adjustStart(double delta) {
      processor.setSongStartSeconds(processor.getSongStartSeconds() + delta);
      refreshValues();
    }

    void adjustEnd(double delta) {
      processor.setSongEndSeconds(processor.getSongEndSeconds() + delta);
      refreshValues();
    }

    void adjustFade(double delta) {
      processor.setSongFadeOutSeconds(processor.getSongFadeOutSeconds() + delta);
      refreshValues();
    }
  };

  if (!processor.hasLoadedSong())
    return;
  const bool arrangementReadOnly = processor.isCustomArrangementActive();
  auto panel =
      std::make_unique<SongWindowPopover>(processor, arrangementReadOnly);
  panel->setSize(230, arrangementReadOnly ? 156 : 128);
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor = parentComp->getLocalArea(this, timeDisplay.getBounds());
  auto &callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
                                                         anchor, parentComp);
  callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void TopBar::refreshFromProviders() {
  const double pos = positionProvider ? positionProvider() : 0.0;
  const double len = lengthProvider ? lengthProvider() : 0.0;
  timeDisplay.setText(formatTimeSeconds(pos) + " / " +
                          formatTimeSeconds(len),
                      juce::dontSendNotification);

  updateBpmDisplay();

  const bool songLoaded = processor.hasLoadedSong();
  if (!songLoaded) {
    if (hasKey || lastSongTitle.isNotEmpty() || lastKeyToken.isNotEmpty()) {
      hasKey = false;
      lastSongTitle.clear();
      lastKeyToken.clear();
      baseKeySemitone = 0;
      activeKeySemitone = 0;
      activeKeyLabel = L10n::tr("Tono", "Key");
      processor.setPitchSemiTones(0);
    }
    keyDisplay.setText(L10n::tr("Tono", "Key"), juce::dontSendNotification);
  } else {
    const auto keyText = keyProvider ? keyProvider().trim() : juce::String{};
    const auto songTitle = processor.getLoadedSongTitle();
    const bool songChanged = songTitle != lastSongTitle;
    if (songChanged || !hasKey) {
      lastSongTitle = songTitle;
      lastKeyToken = keyText;
      int semitone = 0;
      if (parseKeySemitone(keyText, semitone)) {
        baseKeySemitone = semitone;
        activeKeySemitone = semitone;
        activeKeyLabel = noteLabelForSemitone(semitone);
      } else {
        baseKeySemitone = 0;
        activeKeySemitone = 0;
        activeKeyLabel = keyText.isNotEmpty() ? keyText : L10n::tr("Tono", "Key");
      }
      hasKey = true;
      processor.setPitchSemiTones(0);
    } else if (keyText != lastKeyToken) {
      // Metadata key text may refresh asynchronously; keep user-selected
      // transposition and only refresh labels/base reference.
      const int currentDelta =
          semitoneDelta(baseKeySemitone, activeKeySemitone);
      lastKeyToken = keyText;
      int semitone = 0;
      if (parseKeySemitone(keyText, semitone)) {
        baseKeySemitone = semitone;
        activeKeySemitone = normalizeSemitone(baseKeySemitone + currentDelta);
        activeKeyLabel = noteLabelForSemitone(activeKeySemitone);
      } else {
        baseKeySemitone = 0;
        activeKeySemitone = normalizeSemitone(currentDelta);
        activeKeyLabel = keyText.isNotEmpty() ? keyText
                                              : L10n::tr("Tono", "Key");
      }
    }

    if (activeKeyLabel.isEmpty())
      activeKeyLabel = L10n::tr("Tono", "Key");
    keyDisplay.setText(activeKeyLabel, juce::dontSendNotification);
    processor.setPitchSemiTones(
        semitoneDelta(baseKeySemitone, activeKeySemitone));
  }

  const bool playing = playingProvider ? playingProvider() : false;
  playButton.setGlyph(playing ? IconGlyph::Pause : IconGlyph::Play);
  const bool fadeOutActive = processor.isFadeActive();
  fadeButton.setGlyph(fadeOutActive ? IconGlyph::FadeIn : IconGlyph::FadeOut);
  fadeButton.setToggleState(fadeOutActive, juce::dontSendNotification);
  metroToggleButton.setToggleState(processor.isMetronomeEnabled(),
                                   juce::dontSendNotification);
}
