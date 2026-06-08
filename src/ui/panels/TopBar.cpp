#include "TopBar.h"

#include "../../PluginProcessor.h"
#include "../../data/Localization.h"
#include "../../data/SongLibraryStore.h"

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

constexpr const char *kMidiBaseTooltipProperty = "midiBaseTooltip";

juce::String withMidiAssignmentTooltip(const juce::String &baseText,
                                       const juce::String &midiCode) {
  const auto trimmedCode = midiCode.trim();
  if (trimmedCode.isEmpty())
    return baseText;
  if (baseText.trim().isEmpty())
    return "MIDI: " + trimmedCode;
  return baseText + " | MIDI: " + trimmedCode;
}

constexpr bool shouldShowWindowModeButton() {
#if JucePlugin_Build_Standalone && JUCE_MAC
  return false;
#else
  return true;
#endif
}
} // namespace

TopBar::IconButton::IconButton(juce::String name, SvgIconName iconToUse,
                               bool primary)
    : juce::Button(std::move(name)), icon(iconToUse), isPrimary(primary) {}

void TopBar::IconButton::setIcon(SvgIconName iconToUse) {
  icon = iconToUse;
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

void TopBar::IconButton::mouseDown(const juce::MouseEvent &event) {
  juce::Button::mouseDown(event);
  if (onPointerDown)
    onPointerDown();
}

void TopBar::IconButton::paintButton(juce::Graphics &gr,
                                     bool isMouseOverButton,
                                     bool isButtonDown) {
  auto b = getLocalBounds().toFloat().reduced(0.5f);
  const bool active = isButtonDown || getToggleState();

  juce::Colour fill =
      isPrimary
          ? UITheme::accent
          : UITheme::cardBg.interpolatedWith(UITheme::cardBgHover, 0.28f);
  if (active)
    fill = isPrimary ? UITheme::accentSoft : UITheme::cardBgPressed;
  else if (isMouseOverButton)
    fill = fill.brighter(isPrimary ? 0.08f : 0.10f);

  if (pulseAmount > 0.001f) {
    const auto pulseColour =
        pulseDownbeat ? UITheme::accentSoft : UITheme::accent;
    fill = fill.interpolatedWith(pulseColour, 0.25f + (pulseAmount * 0.35f));
  }

  gr.setColour(juce::Colours::black.withAlpha(isPrimary ? 0.24f : 0.18f));
  gr.fillRoundedRectangle(b.translated(0.0f, isPrimary ? 2.0f : 1.4f), 7.0f);

  gr.setColour(fill);
  gr.fillRoundedRectangle(b, 7.0f);

  gr.setColour(juce::Colours::white.withAlpha(isPrimary ? 0.16f : 0.08f));
  gr.drawHorizontalLine(static_cast<int>(b.getY() + 1.0f), b.getX() + 8.0f,
                        b.getRight() - 8.0f);

  const auto border =
      isPrimary ? UITheme::accentSoft.withAlpha(active ? 0.88f : 0.70f)
                : UITheme::cardBorder.brighter(isMouseOverButton ? 0.18f : 0.08f)
                      .withAlpha(0.82f);
  gr.setColour(border);
  gr.drawRoundedRectangle(b, 7.0f, isPrimary ? 1.25f : 1.0f);

  const bool hasBadge = badgeText.isNotEmpty();
  const auto iconColour = isPrimary ? UITheme::onAccent : UITheme::textPrimary;
  const float iconSize = hasBadge ? 20.0f : 24.0f;
  auto iconArea = b.withSizeKeepingCentre(iconSize, iconSize);
  if (icon == SvgIconName::Midi)
    iconArea = b.reduced(6.0f, 14.0f);
  if (hasBadge)
    iconArea.translate(0.0f, -4.0f);

  if (!drawSvgIcon(gr, iconArea, icon, iconColour))
    drawIconGlyph(gr, iconArea, IconGlyph::More, iconColour);

  if (hasBadge) {
    gr.setColour(isPrimary ? UITheme::onAccent.withAlpha(0.92f)
                           : UITheme::textSecondary.withAlpha(0.88f));
    gr.setFont(makeAppFont(9.0f, AppFontWeight::SemiBold));
    gr.drawFittedText(badgeText, getLocalBounds().withTrimmedTop(28),
                      juce::Justification::centredTop, 1);
  }
}

void TopBar::ClickableLabel::mouseDown(const juce::MouseEvent &) {
  if (onClick)
    onClick();
}

TopBar::TopBar(MTControlsAudioProcessor &processorRef)
    : prevButton("prev", SvgIconName::SkipBack),
      seekBackButton("seekBack", SvgIconName::Rewind),
      playButton("play", SvgIconName::Play, true),
      stopButton("stop", SvgIconName::Square),
      seekForwardButton("seekForward", SvgIconName::FastForward),
      nextButton("next", SvgIconName::SkipForward, true),
      fadeButton("fade", SvgIconName::FadeOut),
      windowModeButton("windowMode", SvgIconName::Maximize),
      settingsButton("settings", SvgIconName::Settings),
      midiLearnButton("midiLearn", SvgIconName::Midi),
      processor(processorRef) {
  for (auto *btn :
       {&prevButton, &seekBackButton, &playButton, &stopButton,
        &seekForwardButton, &nextButton, &fadeButton, &windowModeButton,
        &settingsButton, &midiLearnButton}) {
    addAndMakeVisible(*btn);
  }
  windowModeButton.setVisible(shouldShowWindowModeButton());

  configureDisplay(timeDisplay, "00:00 / 00:00");
  configureDisplay(keyDisplay, L10n::tr("Tono", "Key"));
  configureDisplay(systemUsageDisplay, "CPU: 0.0%\nMEM: 0.0%");
  systemUsageDisplay.setInterceptsMouseClicks(false, false);
  systemUsageDisplay.setMouseCursor(juce::MouseCursor::NormalCursor);
  activeKeyLabel = L10n::tr("Tono", "Key");
  applyResponsiveTypography();
  setBaseTooltip(prevButton, L10n::tr(u8"Canción anterior", "Previous song"));
  setBaseTooltip(
      seekBackButton, L10n::tr("Retroceder 5 segundos", "Seek back 5 seconds"));
  setBaseTooltip(playButton, L10n::tr("Reproducir / Pausar", "Play / Pause"));
  setBaseTooltip(stopButton, L10n::tr("Detener", "Stop"));
  setBaseTooltip(
      seekForwardButton, L10n::tr("Avanzar 5 segundos", "Seek forward 5 seconds"));
  setBaseTooltip(nextButton, L10n::tr(u8"Siguiente canción", "Next song"));
  setBaseTooltip(fadeButton, L10n::tr("Fade out", "Fade out"));
  setBaseTooltip(midiLearnButton, L10n::tr("MIDI Learn", "MIDI Learn"));
  midiLearnButton.setBadgeText({});
  configureMetronomeControls();

  auto armLearnTarget = [this](const int targetId) {
    if (!midiLearnActive || targetId < 0)
      return;
    midiLearnPendingTargetId = targetId;
    if (onMidiLearnTargetSelected)
      onMidiLearnTargetSelected(targetId);
    repaint();
  };

  prevButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetTransportPrevSong);
  };
  seekBackButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetTransportSeekBack);
  };
  playButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetTransportPlayPause);
  };
  stopButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetTransportStop);
  };
  seekForwardButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetTransportSeekForward);
  };
  nextButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetTransportNextSong);
  };
  fadeButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetTransportFadeToggle);
  };

  timeDisplay.onClick = [this, armLearnTarget] {
    juce::ignoreUnused(armLearnTarget);
    showSongWindowPopover();
  };
  keyDisplay.onClick = [this, armLearnTarget] {
    juce::ignoreUnused(armLearnTarget);
    showKeyPopover();
  };

  prevButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetTransportPrevSong);
      return;
    }
    clearFadeIfActive();
    if (onPrevSong)
      onPrevSong();
  };
  seekBackButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetTransportSeekBack);
      return;
    }
    clearFadeIfActive();
    if (onSeekRelative)
      onSeekRelative(-5.0);
  };
  seekForwardButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetTransportSeekForward);
      return;
    }
    clearFadeIfActive();
    if (onSeekRelative)
      onSeekRelative(5.0);
  };
  nextButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetTransportNextSong);
      return;
    }
    clearFadeIfActive();
    if (onNextSong)
      onNextSong();
  };
  fadeButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetTransportFadeToggle);
      return;
    }
    const bool shouldFadeOut = !processor.isFadeActive();
    if (onFadeToggle)
      onFadeToggle(shouldFadeOut);
  };
  playButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetTransportPlayPause);
      return;
    }
    clearFadeIfActive();
    if (onPlayPause)
      onPlayPause();
    refreshFromProviders();
  };
  stopButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetTransportStop);
      return;
    }
    clearFadeIfActive();
    if (onStop)
      onStop();
    refreshFromProviders();
  };
  windowModeButton.onClick = [this, armLearnTarget] {
    juce::ignoreUnused(armLearnTarget);
    toggleWindowMode();
  };
  settingsButton.onClick = [this, armLearnTarget] {
    juce::ignoreUnused(armLearnTarget);
    openSettingsPanel();
  };
  midiLearnButton.setClickingTogglesState(true);
  midiLearnButton.onClick = [this] {
    midiLearnActive = midiLearnButton.getToggleState();
    if (!midiLearnActive)
      midiLearnPendingTargetId = -1;
    if (onMidiLearnModeChanged)
      onMidiLearnModeChanged(midiLearnActive);
    repaint();
  };
  bpmSegmentButton.onClick = [] {
    // Tempo popover intentionally disabled; keep tempo logic intact.
  };
  metroToggleButton.onClick = [this, armLearnTarget] {
    juce::ignoreUnused(armLearnTarget);
    processor.setMetronomeEnabled(metroToggleButton.getToggleState());
  };
  refreshMidiLearnTooltips();
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
                                   std::function<juce::String()> keyProviderFn,
                                   std::function<juce::String()> songIdProviderFn) {
  playingProvider = std::move(playingProviderFn);
  positionProvider = std::move(positionProviderFn);
  lengthProvider = std::move(lengthProviderFn);
  keyProvider = std::move(keyProviderFn);
  songIdProvider = std::move(songIdProviderFn);
  refreshFromProviders();
}

void TopBar::setOnSettings(std::function<void()> cb) {
  onSettings = std::move(cb);
}

void TopBar::setOnSettingsWithAnchor(
    std::function<void(juce::Rectangle<int>, juce::Component *)> cb) {
  onSettingsWithAnchor = std::move(cb);
}

void TopBar::setOnMidiLearnModeChanged(std::function<void(bool)> cb) {
  onMidiLearnModeChanged = std::move(cb);
}

void TopBar::setOnMidiLearnTargetSelected(std::function<void(int)> cb) {
  onMidiLearnTargetSelected = std::move(cb);
}

void TopBar::setMidiLearnBindingLabelProvider(
    std::function<juce::String(int)> cb) {
  midiLearnBindingLabelProvider = std::move(cb);
  refreshMidiLearnTooltips();
  repaint();
}

void TopBar::setMidiLearnActive(const bool active) {
  midiLearnActive = active;
  if (!midiLearnActive)
    midiLearnPendingTargetId = -1;
  midiLearnButton.setToggleState(midiLearnActive, juce::dontSendNotification);
  refreshMidiLearnTooltips();
  repaint();
}

void TopBar::setMidiLearnPendingTarget(const int targetId) {
  midiLearnPendingTargetId = targetId;
  refreshMidiLearnTooltips();
  repaint();
}

void TopBar::setResponsiveProfile(const ResponsiveLayoutProfile profile) {
  if (responsiveProfile == profile)
    return;
  responsiveProfile = profile;
  applyResponsiveTypography();
  resized();
  repaint();
}

void TopBar::setSystemUsage(const double cpuUsage01, const double memoryUsage01) {
  const double nextCpu = juce::jlimit(0.0, 1.0, cpuUsage01);
  const double nextMem = juce::jlimit(0.0, 1.0, memoryUsage01);

  const bool changed = std::abs(lastCpuUsage01 - nextCpu) > 0.001 ||
                       std::abs(lastMemoryUsage01 - nextMem) > 0.001;
  if (!changed)
    return;

  lastCpuUsage01 = nextCpu;
  lastMemoryUsage01 = nextMem;

  const auto nextText =
      "CPU: " + juce::String(nextCpu * 100.0, 1) + "%\nMEM: " +
      juce::String(nextMem * 100.0, 1) + "%";
  if (systemUsageDisplay.getText() != nextText)
    systemUsageDisplay.setText(nextText, juce::dontSendNotification);
}

void TopBar::setBaseTooltip(IconButton &button, const juce::String &text) {
  button.getProperties().set(kMidiBaseTooltipProperty, text);
  button.setTooltip(text);
}

void TopBar::refreshMidiLearnTooltips() {
  auto apply = [this](IconButton &button, const int targetId) {
    const auto baseText = button.getProperties()
                              .getWithDefault(kMidiBaseTooltipProperty, juce::var())
                              .toString();
    if (targetId < 0 || midiLearnBindingLabelProvider == nullptr) {
      button.setTooltip(baseText);
      return;
    }
    const auto midiCode = midiLearnBindingLabelProvider(targetId).trim();
    button.setTooltip(withMidiAssignmentTooltip(baseText, midiCode));
  };

  apply(prevButton, processor.midiTargetTransportPrevSong);
  apply(seekBackButton, processor.midiTargetTransportSeekBack);
  apply(playButton, processor.midiTargetTransportPlayPause);
  apply(stopButton, processor.midiTargetTransportStop);
  apply(seekForwardButton, processor.midiTargetTransportSeekForward);
  apply(nextButton, processor.midiTargetTransportNextSong);
  apply(fadeButton, processor.midiTargetTransportFadeToggle);
}

void TopBar::triggerWindowModeToggleFromMidi() { toggleWindowMode(); }

void TopBar::triggerSettingsFromMidi() { openSettingsPanel(); }

void TopBar::triggerSongWindowPopoverFromMidi() { showSongWindowPopover(); }

void TopBar::triggerKeyPopoverFromMidi() { showKeyPopover(); }

void TopBar::triggerMetronomePopoverFromMidi() {
  // Tempo popover intentionally disabled; keep tempo logic intact.
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
  systemUsageDisplay.setColour(juce::Label::textColourId, UITheme::textPrimary);
  systemUsageDisplay.setColour(juce::Label::backgroundColourId, UITheme::cardBg);
  systemUsageDisplay.setColour(juce::Label::outlineColourId,
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

void TopBar::paintOverChildren(juce::Graphics &g) {
  if (midiLearnActive) {
    const auto selectableColour = juce::Colour::fromRGB(78, 170, 255);
    const auto pendingColour = juce::Colour::fromRGB(255, 196, 92);
    auto drawLearnFrame = [this, &g, selectableColour,
                           pendingColour](juce::Component &comp,
                                          const int targetId) {
      const auto frameBounds = comp.getBounds().toFloat().reduced(1.0f);
      const bool pending = (midiLearnPendingTargetId == targetId);
      const float blink = pending ? midiLearnBlinkStrength()
                                  : midiLearnSoftBlinkStrength();
      const auto frameColour = pending ? pendingColour : selectableColour;
      g.setColour(frameColour.withAlpha(pending ? (0.20f + (0.16f * blink))
                                                : (0.14f + (0.06f * blink))));
      g.fillRoundedRectangle(frameBounds, 7.0f);
      g.setColour(frameColour.withAlpha(pending ? (0.54f + (0.40f * blink))
                                                : (0.52f + (0.20f * blink))));
      g.drawRoundedRectangle(frameBounds, 7.0f,
                             pending ? (1.5f + (0.9f * blink))
                                     : (1.0f + (0.35f * blink)));
    };
    drawLearnFrame(prevButton, processor.midiTargetTransportPrevSong);
    drawLearnFrame(seekBackButton, processor.midiTargetTransportSeekBack);
    drawLearnFrame(playButton, processor.midiTargetTransportPlayPause);
    drawLearnFrame(stopButton, processor.midiTargetTransportStop);
    drawLearnFrame(seekForwardButton, processor.midiTargetTransportSeekForward);
    drawLearnFrame(nextButton, processor.midiTargetTransportNextSong);
    drawLearnFrame(fadeButton, processor.midiTargetTransportFadeToggle);

  }
}

void TopBar::resized() {
  int y = 10;
  int square = 44;
  int gap = 8;
  int keyWidth = 72;
  int metroTotalW = 110;
  int metroIconW = 44;
  int timeWidth = 120;
  int usageWidth = 92;
  int minUsageWidth = 72;

  if (responsiveProfile == ResponsiveLayoutProfile::Compact) {
    y = 8;
    square = 38;
    gap = 6;
    keyWidth = 64;
    metroTotalW = 92;
    metroIconW = 38;
    timeWidth = 96;
    usageWidth = 84;
    minUsageWidth = 68;
  } else if (responsiveProfile == ResponsiveLayoutProfile::Large) {
    y = 10;
    square = 46;
    gap = 8;
    keyWidth = 76;
    metroTotalW = 114;
    metroIconW = 46;
    timeWidth = 128;
    usageWidth = 102;
    minUsageWidth = 80;
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

  const int transportW = x;
  const int rightPadding = 12;
  const bool showWindowButton = shouldShowWindowModeButton();
  const int fixedButtonCount = showWindowButton ? 3 : 2;
  const int minFixedButtonsW =
      (fixedButtonCount * square) + ((fixedButtonCount - 1) * gap);
  const int availableAfterTransport = getWidth() - rightPadding - transportW - 8;
  bool showSystemUsage = true;
  const int maxUsageWidth =
      availableAfterTransport - minFixedButtonsW - gap;
  if (maxUsageWidth < minUsageWidth) {
    showSystemUsage = false;
  } else {
    usageWidth = juce::jmin(usageWidth, juce::jmax(minUsageWidth, maxUsageWidth));
  }
  const int fixedButtonsW =
      minFixedButtonsW + (showSystemUsage ? (usageWidth + gap) : 0);
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
  systemUsageDisplay.setVisible(showSystemUsage);
  windowModeButton.setVisible(showWindowButton);

  int rightX = getWidth() - 12;
  if (showSystemUsage) {
    systemUsageDisplay.setBounds(rightX - usageWidth, y - 2, usageWidth, square);
    rightX -= usageWidth + gap;
  } else {
    systemUsageDisplay.setBounds({});
  }
  settingsButton.setBounds(rightX - square, y - 2, square, square);
  rightX -= square + gap;
  if (showWindowButton) {
    windowModeButton.setBounds(rightX - square, y - 2, square, square);
    rightX -= square + gap;
  } else {
    windowModeButton.setBounds({});
  }
  midiLearnButton.setBounds(rightX - square, y - 2, square, square);
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
    auto shiftIfVisible = [shift](juce::Component &comp) {
      if (!comp.isVisible())
        return;
      comp.setTopLeftPosition(comp.getX() + shift, comp.getY());
    };
    shiftIfVisible(timeDisplay);
    shiftIfVisible(bpmSegmentButton);
    shiftIfVisible(metroToggleButton);
    shiftIfVisible(keyDisplay);
    shiftIfVisible(systemUsageDisplay);
    shiftIfVisible(windowModeButton);
    shiftIfVisible(settingsButton);
  }
}

void TopBar::mouseDoubleClick(const juce::MouseEvent &event) {
  if (!event.mods.isLeftButtonDown())
    return;
  toggleWindowMode();
}

void TopBar::clearFadeIfActive() {
  if (!processor.isFadeActive())
    return;
  if (onFadeToggle)
    onFadeToggle(false);
}

void TopBar::toggleWindowMode() {
  clearFadeIfActive();
  toggleTopLevelWindowMaximised(this);
  const bool maxed = isTopLevelWindowMaximised(this);
  windowModeButton.setIcon(maxed ? SvgIconName::Copy : SvgIconName::Maximize);
  lastTopLevelMaximised = maxed;
}

void TopBar::openSettingsPanel() {
  clearFadeIfActive();
  if (onSettingsWithAnchor)
    onSettingsWithAnchor(settingsButton.getBounds(), this);
  else if (onSettings)
    onSettings();
}

void TopBar::applyResponsiveTypography() {
  float displayFont = 13.0f;
  float keyFont = 15.0f;
  float usageFont = 12.5f;
  if (responsiveProfile == ResponsiveLayoutProfile::Compact) {
    displayFont = 11.5f;
    keyFont = 13.0f;
    usageFont = 11.5f;
  } else if (responsiveProfile == ResponsiveLayoutProfile::Large) {
    displayFont = 13.5f;
    keyFont = 15.5f;
    usageFont = 13.0f;
  }
  timeDisplay.setFont(juce::FontOptions(displayFont));
  keyDisplay.setFont(juce::FontOptions(keyFont).withStyle("Bold"));
  systemUsageDisplay.setFont(makeAppFont(usageFont, AppFontWeight::SemiBold));
  systemUsageDisplay.setJustificationType(juce::Justification::centred);
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
  bpmSegmentButton.setButtonText("120");
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
  setBaseTooltip(metroToggleButton,
                 L10n::tr(u8"Iniciar / detener metrónomo", "Start / stop metronome"));
}

void TopBar::timerCallback() {
  refreshFromProviders();

  if (windowModeButton.isVisible()) {
    const bool maxed = isTopLevelWindowMaximised(this);
    if (maxed != lastTopLevelMaximised) {
      lastTopLevelMaximised = maxed;
      windowModeButton.setIcon(maxed ? SvgIconName::Copy : SvgIconName::Maximize);
    }
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
  metroToggleButton.setBadgeText(processor.isMetronomeEnabled() ? "ON"
                                                               : juce::String{});
  metroToggleButton.setPulseAmount(
      processor.isMetronomeEnabled() ? beatFlash : 0.0f,
      lastBeatWasDownbeat);

  if (midiLearnActive)
    repaint();
}

void TopBar::updateBpmDisplay() {
  const int bpm =
      static_cast<int>(std::round(processor.getPlaybackTempoBpm()));
  bpmSegmentButton.setButtonText(bpm > 0 ? juce::String(bpm) : "120");
}

void TopBar::applyTimeSignature(const juce::String &sigText) {
  timeSignature = sigText.trim();
  if (timeSignature.isEmpty())
    timeSignature = "4/4";
  beatsPerBar = timeSignatureBeatsPerBar(timeSignature);
  processor.setMetronomeBeatsPerBar(beatsPerBar);
  beatCounter = 0.0;
  lastBeatIndex = -1;
  lastBeatWasDownbeat = true;
}

void TopBar::setBpmAbsolute(double bpm) {
  if (bpm <= 0.0)
    return;
  processor.setPlaybackTempoBpm(bpm);
  updateBpmDisplay();
}

void TopBar::adjustBpm(double delta) {
  const auto mods = juce::ModifierKeys::getCurrentModifiers();
  const double step = mods.isShiftDown() ? (delta < 0.0 ? -5.0 : 5.0) : delta;
  setBpmAbsolute(processor.getPlaybackTempoBpm() + step);
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
      [this] { return processor.getPlaybackTempoBpm(); }, timeSignature);
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
        activePitchDelta = semitoneDelta(baseKeySemitone, activeKeySemitone);
        processor.setPitchSemiTones(activePitchDelta);
        const auto songId = songIdProvider ? songIdProvider() : juce::String{};
        if (songId.isNotEmpty())
          SongLibraryStore::updatePitchSemitonesForActiveSetlist(songId, activePitchDelta);
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
      configureRow(startLabel, startMinus, startEditor, startPlus,
                   L10n::tr("Inicio", "Start"));
      configureRow(endLabel, endMinus, endEditor, endPlus,
                   L10n::tr("Fin", "End"));
      configureRow(fadeLabel, fadeMinus, fadeValue, fadePlus,
                   L10n::tr("Salida", "Fade out"));

      startMinus.onClick = [this] { adjustStart(-stepSeconds()); };
      startPlus.onClick = [this] { adjustStart(stepSeconds()); };
      endMinus.onClick = [this] { adjustEnd(-stepSeconds()); };
      endPlus.onClick = [this] { adjustEnd(stepSeconds()); };
      fadeMinus.onClick = [this] { adjustFade(-stepSeconds()); };
      fadePlus.onClick = [this] { adjustFade(stepSeconds()); };
      startEditor.onReturnKey = [this] { submitStartEditor(); };
      startEditor.onFocusLost = [this] { submitStartEditor(); };
      endEditor.onReturnKey = [this] { submitEndEditor(); };
      endEditor.onFocusLost = [this] { submitEndEditor(); };

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
      startEditor.setText(formatTimeSeconds(processor.getSongStartSeconds()),
                          juce::dontSendNotification);
      endEditor.setText(formatTimeSeconds(processor.getSongEndSeconds()),
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
      for (auto *editor : {&startEditor, &endEditor}) {
        editor->setColour(juce::TextEditor::backgroundColourId,
                          UITheme::cardBgHover);
        editor->setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
        editor->setColour(juce::TextEditor::outlineColourId,
                          UITheme::cardBorder.withAlpha(0.65f));
        editor->setColour(juce::TextEditor::focusedOutlineColourId,
                          UITheme::accent.withAlpha(0.75f));
        editor->setColour(juce::CaretComponent::caretColourId,
                          UITheme::textPrimary);
        editor->setJustification(juce::Justification::centred);
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
      layoutRow(area.removeFromTop(rowH), startLabel, startMinus, startEditor,
                startPlus);
      area.removeFromTop(8);
      layoutRow(area.removeFromTop(rowH), endLabel, endMinus, endEditor,
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
    juce::TextEditor startEditor, endEditor;
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

    void configureRow(juce::Label &label, juce::TextButton &minus,
                      juce::TextEditor &editor, juce::TextButton &plus,
                      const juce::String &text) {
      label.setText(text, juce::dontSendNotification);
      addAndMakeVisible(label);

      minus.setButtonText("-");
      plus.setButtonText("+");
      addAndMakeVisible(minus);
      addAndMakeVisible(plus);

      editor.setInputRestrictions(8, "0123456789:");
      addAndMakeVisible(editor);
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

    void layoutRow(juce::Rectangle<int> row, juce::Label &label,
                   juce::TextButton &minus, juce::TextEditor &editor,
                   juce::TextButton &plus) {
      const int labelW = 60;
      const int btnW = 28;
      label.setBounds(row.removeFromLeft(labelW));
      minus.setBounds(row.removeFromLeft(btnW));
      plus.setBounds(row.removeFromRight(btnW));
      editor.setBounds(row);
    }

    double parseTimeText(const juce::String &text) const {
      const auto cleaned = text.trim();
      if (cleaned.isEmpty())
        return -1.0;
      if (!cleaned.containsChar(':'))
        return cleaned.getDoubleValue();
      const auto mins = cleaned.upToFirstOccurrenceOf(":", false, false).trim();
      const auto secs =
          cleaned.fromFirstOccurrenceOf(":", false, false).trim();
      const auto m = mins.getDoubleValue();
      const auto s = secs.getDoubleValue();
      if (m < 0.0 || s < 0.0)
        return -1.0;
      return (m * 60.0) + s;
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

    void submitStartEditor() {
      const double secs = parseTimeText(startEditor.getText());
      if (secs >= 0.0)
        processor.setSongStartSeconds(secs);
      refreshValues();
    }

    void submitEndEditor() {
      const double secs = parseTimeText(endEditor.getText());
      if (secs >= 0.0)
        processor.setSongEndSeconds(secs);
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
      activePitchDelta = 0;
      const auto songId = songIdProvider ? songIdProvider() : juce::String{};
      if (songId.isNotEmpty())
        SongLibraryStore::getActiveSetlistPitchSemitones(songId, activePitchDelta);
      activeKeySemitone = normalizeSemitone(baseKeySemitone + activePitchDelta);
      activeKeyLabel = noteLabelForSemitone(activeKeySemitone);
      processor.setPitchSemiTones(activePitchDelta);
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
    activePitchDelta = semitoneDelta(baseKeySemitone, activeKeySemitone);
    processor.setPitchSemiTones(activePitchDelta);
  }

  const bool playing = playingProvider ? playingProvider() : false;
  playButton.setIcon(playing ? SvgIconName::Pause : SvgIconName::Play);
  const bool fadeOutActive = processor.isFadeActive();
  fadeButton.setIcon(fadeOutActive ? SvgIconName::FadeIn
                                   : SvgIconName::FadeOut);
  fadeButton.setToggleState(fadeOutActive, juce::dontSendNotification);
  metroToggleButton.setToggleState(processor.isMetronomeEnabled(),
                                   juce::dontSendNotification);
  metroToggleButton.setBadgeText(processor.isMetronomeEnabled() ? "ON"
                                                               : juce::String{});
}
