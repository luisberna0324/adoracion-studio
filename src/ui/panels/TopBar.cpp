#include "TopBar.h"

#include "../../PluginProcessor.h"
#include "../../data/Localization.h"
#include "../../data/SongLibraryStore.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

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

juce::Colour colourfulAccentFromStableId(const juce::String &id) {
  if (id.isEmpty())
    return UITheme::accent;
  const auto hash = std::hash<std::string>{}(id.toStdString());
  const float hue = static_cast<float>(hash % 360) / 360.0f;
  const float saturation =
      0.78f + (static_cast<float>((hash >> 9) % 16) / 100.0f);
  const float brightness =
      0.88f + (static_cast<float>((hash >> 15) % 10) / 100.0f);
  return juce::Colour::fromHSV(juce::jlimit(0.0f, 1.0f, hue),
                               juce::jlimit(0.0f, 1.0f, saturation),
                               juce::jlimit(0.0f, 1.0f, brightness), 1.0f);
}

juce::Path roundedControlPath(juce::Rectangle<float> bounds,
                              const int connectedEdges,
                              const float cornerRadius) {
  const bool connectedLeft =
      (connectedEdges & juce::Button::ConnectedOnLeft) != 0;
  const bool connectedRight =
      (connectedEdges & juce::Button::ConnectedOnRight) != 0;
  const bool connectedTop =
      (connectedEdges & juce::Button::ConnectedOnTop) != 0;
  const bool connectedBottom =
      (connectedEdges & juce::Button::ConnectedOnBottom) != 0;

  if (connectedLeft)
    bounds.setX(0.0f);
  if (connectedRight)
    bounds.setRight(bounds.getRight() + 0.5f);
  if (connectedTop)
    bounds.setY(0.0f);
  if (connectedBottom)
    bounds.setBottom(bounds.getBottom() + 0.5f);

  juce::Path path;
  path.addRoundedRectangle(
      bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
      cornerRadius, cornerRadius, !connectedLeft && !connectedTop,
      !connectedRight && !connectedTop, !connectedLeft && !connectedBottom,
      !connectedRight && !connectedBottom);
  return path;
}

void setBoundsIfChanged(juce::Component &component,
                        const juce::Rectangle<int> &bounds) {
  if (component.getBounds() != bounds)
    component.setBounds(bounds);
}

void setVisibleIfChanged(juce::Component &component, const bool visible) {
  if (component.isVisible() != visible)
    component.setVisible(visible);
}

bool setLabelTextIfChanged(juce::Label &label, const juce::String &text) {
  if (label.getText() == text)
    return false;
  label.setText(text, juce::dontSendNotification);
  return true;
}

bool setButtonTextIfChanged(juce::Button &button, const juce::String &text) {
  if (button.getButtonText() == text)
    return false;
  button.setButtonText(text);
  return true;
}
} // namespace

TopBar::IconButton::IconButton(juce::String name, SvgIconName iconToUse,
                               bool primary)
    : juce::Button(std::move(name)), icon(iconToUse), isPrimary(primary) {}

void TopBar::IconButton::setIcon(SvgIconName iconToUse) {
  if (icon == iconToUse)
    return;
  icon = iconToUse;
  repaint();
}

void TopBar::IconButton::setBadgeText(juce::String badge) {
  if (badgeText == badge)
    return;
  badgeText = std::move(badge);
  repaint();
}

void TopBar::IconButton::setPulseAmount(float amount, bool downbeat) {
  const float nextAmount = juce::jlimit(0.0f, 1.0f, amount);
  if (std::abs(pulseAmount - nextAmount) < 0.01f &&
      pulseDownbeat == downbeat)
    return;
  pulseAmount = nextAmount;
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
  const auto connectedEdges = getConnectedEdgeFlags();
  const auto controlPath = roundedControlPath(b, connectedEdges, 10.0f);
  const bool active = isButtonDown || getToggleState();

  auto fill = UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.20f);
  if (active && !isPrimary)
    fill = fill.brighter(0.08f);
  else if (isMouseOverButton && !isPrimary)
    fill = fill.brighter(0.05f);

  if (pulseAmount > 0.001f) {
    const auto pulseColour =
        pulseDownbeat ? UITheme::accentSoft : UITheme::accent;
    fill = fill.interpolatedWith(pulseColour, 0.14f + (pulseAmount * 0.24f));
  }

  if (isPrimary) {
    gr.setColour(UITheme::accentSoft.withAlpha(active ? 0.26f : 0.18f));
    gr.strokePath(controlPath, juce::PathStrokeType(3.0f));
  }

  gr.setColour(juce::Colours::black.withAlpha(isPrimary ? 0.18f : 0.20f));
  auto shadowPath = controlPath;
  shadowPath.applyTransform(
      juce::AffineTransform::translation(0.0f, isPrimary ? 1.5f : 1.0f));
  gr.fillPath(shadowPath);

  if (isPrimary) {
    auto primaryTop = UITheme::accentSoft.brighter(isMouseOverButton ? 0.04f : 0.0f);
    if (active)
      primaryTop = primaryTop.brighter(0.05f);
    juce::ColourGradient primaryFill(primaryTop, b.getCentreX(), b.getY(),
                                     UITheme::accent, b.getCentreX(),
                                     b.getBottom(), false);
    gr.setGradientFill(primaryFill);
  } else {
    juce::ColourGradient secondaryFill(fill.brighter(0.025f), b.getCentreX(),
                                       b.getY(), fill.darker(0.035f),
                                       b.getCentreX(), b.getBottom(), false);
    gr.setGradientFill(secondaryFill);
  }
  gr.fillPath(controlPath);

  const auto border =
      isPrimary ? UITheme::accentSoft.withAlpha(active ? 0.88f : 0.70f)
                : UITheme::cardBorder.brighter(isMouseOverButton ? 0.10f : 0.02f)
                      .withAlpha(active ? 0.90f : 0.72f);
  gr.setColour(border);
  gr.strokePath(controlPath,
                juce::PathStrokeType(isPrimary ? 1.25f : 1.0f));

  const bool hasBadge = badgeText.isNotEmpty();
  const auto iconColour = isPrimary ? UITheme::onAccent : UITheme::textPrimary;
  const float iconSize = hasBadge ? 18.0f : 21.0f;
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

void TopBar::RoundedLabel::paint(juce::Graphics &g) {
  const auto b = getLocalBounds().toFloat().reduced(0.5f);
  constexpr float cornerRadius = 7.0f;

  g.setColour(findColour(juce::Label::backgroundColourId));
  g.fillRoundedRectangle(b, cornerRadius);

  g.setColour(juce::Colours::white.withAlpha(0.08f));
  g.drawHorizontalLine(static_cast<int>(b.getY() + 1.0f), b.getX() + 8.0f,
                       b.getRight() - 8.0f);

  g.setColour(findColour(juce::Label::outlineColourId));
  g.drawRoundedRectangle(b, cornerRadius, 1.0f);

  g.setColour(findColour(juce::Label::textColourId)
                  .withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
  g.setFont(getFont());
  g.drawFittedText(getText(), getLocalBounds().reduced(5, 2),
                   getJustificationType(), 2, 0.7f);
}

void TopBar::ClickableLabel::mouseDown(const juce::MouseEvent &) {
  if (onClick)
    onClick();
}

TopBar::BpmSegmentButton::BpmSegmentButton()
    : juce::Button("bpmSegment") {}

void TopBar::BpmSegmentButton::paintButton(juce::Graphics &g,
                                           bool isMouseOverButton,
                                           bool isButtonDown) {
  auto b = getLocalBounds().toFloat().reduced(0.5f);
  const auto path = roundedControlPath(b, getConnectedEdgeFlags(), 10.0f);
  auto fill = UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.20f);
  if (isButtonDown)
    fill = fill.brighter(0.08f);
  if (isMouseOverButton && !isButtonDown)
    fill = fill.brighter(0.05f);

  g.setColour(juce::Colours::black.withAlpha(0.20f));
  auto shadowPath = path;
  shadowPath.applyTransform(juce::AffineTransform::translation(0.0f, 1.0f));
  g.fillPath(shadowPath);

  juce::ColourGradient controlFill(fill.brighter(0.025f), b.getCentreX(),
                                   b.getY(), fill.darker(0.035f),
                                   b.getCentreX(), b.getBottom(), false);
  g.setGradientFill(controlFill);
  g.fillPath(path);
  g.setColour(UITheme::cardBorder.withAlpha(0.72f));
  g.strokePath(path, juce::PathStrokeType(1.0f));

  g.setColour(UITheme::accentSoft.withAlpha(isButtonDown ? 1.0f : 0.92f));
  g.setFont(makeAppFont(15.0f, AppFontWeight::Medium));
  g.drawFittedText(getButtonText(), getLocalBounds().reduced(8, 2),
                   juce::Justification::centred, 1);
}

TopBar::TopBar(MTControlsAudioProcessor &processorRef)
    : prevButton("prev", SvgIconName::SkipBack),
      seekBackButton("seekBack", SvgIconName::Rewind),
      playButton("play", SvgIconName::Play, true),
      stopButton("stop", SvgIconName::Square),
      seekForwardButton("seekForward", SvgIconName::FastForward),
      nextButton("next", SvgIconName::SkipForward),
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
  systemUsageDisplay.setComponentID("systemUsage");
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
  setBaseTooltip(windowModeButton,
                 L10n::tr("Maximizar / restaurar", "Maximize / restore"));
  setBaseTooltip(settingsButton, L10n::tr("Configuración", "Settings"));
  setBaseTooltip(midiLearnButton, L10n::tr("MIDI Learn", "MIDI Learn"));
  setBaseTooltip(timeDisplay,
                 L10n::tr("Ventana de canción", "Song window"));
  setBaseTooltip(keyDisplay, L10n::tr("Tono", "Key"));
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
  metroToggleButton.onPointerDown = [this, armLearnTarget] {
    if (midiLearnActive)
      armLearnTarget(processor.midiTargetMetronomeEnabled);
  };

  timeDisplay.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetUiSongWindowPopover);
      return;
    }
    showSongWindowPopover();
  };
  keyDisplay.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetUiKeyPopover);
      return;
    }
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
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetUiWindowModeToggle);
      return;
    }
    toggleWindowMode();
  };
  settingsButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetUiSettingsToggle);
      return;
    }
    openSettingsPanel();
  };
  midiLearnButton.setClickingTogglesState(true);
  midiLearnButton.onClick = [this] {
    midiLearnActive = midiLearnButton.getToggleState();
    if (!midiLearnActive)
      midiLearnPendingTargetId = -1;
    midiLearnButton.setBadgeText(midiLearnActive ? "ON" : juce::String{});
    refreshMidiLearnTooltips();
    if (onMidiLearnModeChanged)
      onMidiLearnModeChanged(midiLearnActive);
    repaint();
  };
  bpmSegmentButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetUiMetronomePopover);
      return;
    }
    showMetronomePopover();
  };
  metroToggleButton.onClick = [this, armLearnTarget] {
    if (midiLearnActive) {
      armLearnTarget(processor.midiTargetMetronomeEnabled);
      metroToggleButton.setToggleState(processor.isMetronomeEnabled(),
                                       juce::dontSendNotification);
      return;
    }
    const bool enabled = metroToggleButton.getToggleState();
    processor.setMetronomeEnabled(enabled);
    if (onMetronomeEnabledChanged)
      onMetronomeEnabledChanged(enabled);
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
                                   std::function<juce::String()> songIdProviderFn,
                                   std::function<const ImportedSongData *()>
                                       songProviderFn) {
  playingProvider = std::move(playingProviderFn);
  positionProvider = std::move(positionProviderFn);
  lengthProvider = std::move(lengthProviderFn);
  keyProvider = std::move(keyProviderFn);
  songIdProvider = std::move(songIdProviderFn);
  songProvider = std::move(songProviderFn);
  refreshFromProviders();
}

void TopBar::setOnSettings(std::function<void()> cb) {
  onSettings = std::move(cb);
}

void TopBar::setOnSettingsWithAnchor(
    std::function<void(juce::Rectangle<int>, juce::Component *)> cb) {
  onSettingsWithAnchor = std::move(cb);
}

void TopBar::setOnMetronomeEnabledChanged(std::function<void(bool)> cb) {
  onMetronomeEnabledChanged = std::move(cb);
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
  midiLearnButton.setBadgeText(midiLearnActive ? "ON" : juce::String{});
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
  setLabelTextIfChanged(systemUsageDisplay, nextText);
}

void TopBar::setBaseTooltip(juce::Button &component, const juce::String &text) {
  component.getProperties().set(kMidiBaseTooltipProperty, text);
  component.setTooltip(text);
}

void TopBar::setBaseTooltip(juce::Label &component, const juce::String &text) {
  component.getProperties().set(kMidiBaseTooltipProperty, text);
  component.setTooltip(text);
}

void TopBar::refreshMidiLearnTooltips() {
  auto midiLabelForTarget = [this](const int targetId) {
    if (targetId < 0 || midiLearnBindingLabelProvider == nullptr)
      return juce::String{};
    return midiLearnBindingLabelProvider(targetId).trim();
  };

  auto apply = [this](auto &component, const int targetId) {
    const auto baseText =
        component.getProperties()
            .getWithDefault(kMidiBaseTooltipProperty, juce::var())
            .toString();
    if (targetId < 0 || midiLearnBindingLabelProvider == nullptr) {
      component.setTooltip(baseText);
      return;
    }
    const auto midiCode = midiLearnBindingLabelProvider(targetId).trim();
    component.setTooltip(withMidiAssignmentTooltip(baseText, midiCode));
  };

  apply(prevButton, processor.midiTargetTransportPrevSong);
  apply(seekBackButton, processor.midiTargetTransportSeekBack);
  apply(playButton, processor.midiTargetTransportPlayPause);
  apply(stopButton, processor.midiTargetTransportStop);
  apply(seekForwardButton, processor.midiTargetTransportSeekForward);
  apply(nextButton, processor.midiTargetTransportNextSong);
  apply(fadeButton, processor.midiTargetTransportFadeToggle);
  apply(timeDisplay, processor.midiTargetUiSongWindowPopover);
  apply(bpmSegmentButton, processor.midiTargetUiMetronomePopover);
  apply(metroToggleButton, processor.midiTargetMetronomeEnabled);
  apply(keyDisplay, processor.midiTargetUiKeyPopover);
  apply(midiLearnButton, -1);
  apply(windowModeButton, processor.midiTargetUiWindowModeToggle);
  apply(settingsButton, processor.midiTargetUiSettingsToggle);

  auto applyBadge = [this, midiLabelForTarget](IconButton &button,
                                               const int targetId) {
    button.setBadgeText(midiLearnActive ? midiLabelForTarget(targetId)
                                        : juce::String{});
  };

  applyBadge(prevButton, processor.midiTargetTransportPrevSong);
  applyBadge(seekBackButton, processor.midiTargetTransportSeekBack);
  applyBadge(playButton, processor.midiTargetTransportPlayPause);
  applyBadge(stopButton, processor.midiTargetTransportStop);
  applyBadge(seekForwardButton, processor.midiTargetTransportSeekForward);
  applyBadge(nextButton, processor.midiTargetTransportNextSong);
  applyBadge(fadeButton, processor.midiTargetTransportFadeToggle);
  applyBadge(metroToggleButton, processor.midiTargetMetronomeEnabled);
  applyBadge(windowModeButton, processor.midiTargetUiWindowModeToggle);
  applyBadge(settingsButton, processor.midiTargetUiSettingsToggle);
  midiLearnButton.setBadgeText(midiLearnActive ? "ON" : juce::String{});
}

void TopBar::triggerWindowModeToggleFromMidi() { toggleWindowMode(); }

void TopBar::triggerSettingsFromMidi() { openSettingsPanel(); }

void TopBar::triggerSongWindowPopoverFromMidi() { showSongWindowPopover(); }

void TopBar::triggerKeyPopoverFromMidi() { showKeyPopover(); }

void TopBar::triggerMetronomePopoverFromMidi() {
  showMetronomePopover();
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
  systemUsageDisplay.setColour(juce::Label::textColourId,
                               UITheme::textSecondary.withAlpha(0.92f));
  systemUsageDisplay.setColour(
      juce::Label::backgroundColourId,
      UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.28f));
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

  if (!songInfoBounds.isEmpty()) {
    g.setColour(UITheme::dividerSoft.withAlpha(0.58f));
    const float separatorX = static_cast<float>(songInfoBounds.getX() - 8);
    g.drawLine(separatorX, 12.0f, separatorX,
               static_cast<float>(getHeight()) - 12.0f, 1.0f);
  }
}

void TopBar::paintOverChildren(juce::Graphics &g) {
  if (midiLearnActive) {
    const auto selectableColour = juce::Colour::fromRGB(78, 170, 255);
    const auto pendingColour = juce::Colour::fromRGB(255, 196, 92);
    auto drawLearnFrame = [this, &g, selectableColour,
                           pendingColour](juce::Component &comp,
                                          const int targetId) {
      if (targetId < 0 || !comp.isVisible() || comp.getBounds().isEmpty())
        return;
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
    drawLearnFrame(timeDisplay, processor.midiTargetUiSongWindowPopover);
    drawLearnFrame(bpmSegmentButton, processor.midiTargetUiMetronomePopover);
    drawLearnFrame(metroToggleButton, processor.midiTargetMetronomeEnabled);
    drawLearnFrame(keyDisplay, processor.midiTargetUiKeyPopover);
    drawLearnFrame(windowModeButton, processor.midiTargetUiWindowModeToggle);
    drawLearnFrame(settingsButton, processor.midiTargetUiSettingsToggle);

  }

  paintSongInfoCard(g);
}

void TopBar::resized() {
  int y = 10;
  int square = 40;
  int gap = 8;
  int keyWidth = 72;
  int metroTotalW = 102;
  int metroIconW = 40;
  int timeWidth = 116;
  int usageWidth = 78;
  int minUsageWidth = 68;
  int minCardW = 158;
  int maxCardW = 286;
  songCardHeight = 42;
  songArtworkSize = 32;

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
    minCardW = 148;
    maxCardW = 260;
    songCardHeight = 40;
    songArtworkSize = 30;
  } else if (responsiveProfile == ResponsiveLayoutProfile::Large) {
    y = 10;
    square = 44;
    gap = 8;
    keyWidth = 76;
    metroTotalW = 108;
    metroIconW = 44;
    timeWidth = 124;
    usageWidth = 88;
    minUsageWidth = 76;
    minCardW = 176;
    maxCardW = 325;
    songCardHeight = 44;
    songArtworkSize = 34;
  }

  constexpr int transportButtonCount = 7;
  const int leftPadding = 12;
  const int rightPadding = 12;
  const int transportControlsW =
      (transportButtonCount * square) + ((transportButtonCount - 1) * gap);

  juce::FlexBox transportRow;
  transportRow.flexDirection = juce::FlexBox::Direction::row;
  transportRow.alignItems = juce::FlexBox::AlignItems::stretch;
  auto addTransportButton = [&](juce::Component &component,
                                const bool last = false) {
    transportRow.items.add(
        juce::FlexItem(component)
            .withWidth(static_cast<float>(square))
            .withHeight(static_cast<float>(square))
            .withMargin(juce::FlexItem::Margin(
                0.0f, last ? 0.0f : static_cast<float>(gap), 0.0f, 0.0f)));
  };
  addTransportButton(prevButton);
  addTransportButton(seekBackButton);
  addTransportButton(playButton);
  addTransportButton(stopButton);
  addTransportButton(seekForwardButton);
  addTransportButton(nextButton);
  addTransportButton(fadeButton, true);
  transportRow.performLayout(juce::Rectangle<float>(
      static_cast<float>(leftPadding), static_cast<float>(y - 2),
      static_cast<float>(transportControlsW), static_cast<float>(square)));

  const int transportW = leftPadding + transportControlsW + gap;
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
  bool showBpmText = true;
  auto layoutRightSide = [&](const bool usageVisible,
                             const bool bpmVisible) -> int {
    const int metronomeW = bpmVisible ? metroTotalW : metroIconW;
    int controlsW = timeWidth + gap + metronomeW + gap + keyWidth + gap +
                    square + gap;
    if (showWindowButton)
      controlsW += square + gap;
    controlsW += square;
    if (usageVisible)
      controlsW += gap + usageWidth;

    const int leftX = getWidth() - rightPadding - controlsW;

    setVisibleIfChanged(timeDisplay, true);
    setVisibleIfChanged(keyDisplay, true);
    setVisibleIfChanged(bpmSegmentButton, bpmVisible);
    setVisibleIfChanged(systemUsageDisplay, usageVisible);
    setVisibleIfChanged(windowModeButton, showWindowButton);

    if (!bpmVisible)
      setBoundsIfChanged(bpmSegmentButton, {});
    if (!usageVisible)
      setBoundsIfChanged(systemUsageDisplay, {});
    if (!showWindowButton)
      setBoundsIfChanged(windowModeButton, {});

    juce::FlexBox rightRow;
    rightRow.flexDirection = juce::FlexBox::Direction::row;
    rightRow.alignItems = juce::FlexBox::AlignItems::stretch;

    auto addItem = [&](juce::Component &component, const int width,
                       const bool last = false) {
      rightRow.items.add(
          juce::FlexItem(component)
              .withWidth(static_cast<float>(width))
              .withHeight(static_cast<float>(square))
              .withMargin(juce::FlexItem::Margin(
                  0.0f, last ? 0.0f : static_cast<float>(gap), 0.0f, 0.0f)));
    };

    addItem(timeDisplay, timeWidth);
    if (bpmVisible) {
      rightRow.items.add(
          juce::FlexItem(bpmSegmentButton)
              .withWidth(static_cast<float>(metroTotalW - metroIconW))
              .withHeight(static_cast<float>(square)));
      rightRow.items.add(
          juce::FlexItem(metroToggleButton)
              .withWidth(static_cast<float>(metroIconW))
              .withHeight(static_cast<float>(square))
              .withMargin(juce::FlexItem::Margin(
                  0.0f, static_cast<float>(gap), 0.0f, 0.0f)));
    } else {
      addItem(metroToggleButton, metroIconW);
    }
    addItem(keyDisplay, keyWidth);
    addItem(midiLearnButton, square);
    if (showWindowButton)
      addItem(windowModeButton, square);
    addItem(settingsButton, square, !usageVisible);
    if (usageVisible)
      addItem(systemUsageDisplay, usageWidth, true);

    rightRow.performLayout(juce::Rectangle<float>(
        static_cast<float>(leftX), static_cast<float>(y - 2),
        static_cast<float>(controlsW), static_cast<float>(square)));

    return leftX - gap;
  };

  int rightX = 0;
  for (int attempt = 0; attempt < 3; ++attempt) {
    rightX = layoutRightSide(showSystemUsage, showBpmText);
    const int centerW = rightX - (transportW + 16);
    if (centerW >= minCardW)
      break;
    if (showBpmText) {
      showBpmText = false;
      continue;
    }
    if (showSystemUsage) {
      showSystemUsage = false;
      continue;
    }
    break;
  }

  rightX = layoutRightSide(showSystemUsage, showBpmText);

  const int cardLeft = transportW + 8;
  const int cardRight = rightX - 8;
  const int centerW = cardRight - cardLeft;
  if (centerW > 0) {
    const auto titleText = songTitleText.isNotEmpty()
                               ? songTitleText
                               : L10n::tr("Sin canción", "No song loaded");
    const auto artistText = songArtistText.isNotEmpty()
                                ? songArtistText
                                : L10n::tr("Selecciona una canción",
                                           "Select a song");
    const auto titleFont = makeAppFont(songTitleFontSize, AppFontWeight::SemiBold);
    const auto artistFont = makeAppFont(songArtistFontSize, AppFontWeight::Medium);
    auto measureText = [](const juce::Font &font, const juce::String &text) {
      juce::GlyphArrangement glyphs;
      glyphs.addLineOfText(font, text, 0.0f, 0.0f);
      return static_cast<int>(
          std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
    };
    const int measuredTextW = juce::jmax(measureText(titleFont, titleText),
                                         measureText(artistFont, artistText));
    const int contentW = songArtworkSize + 11 + measuredTextW + 18;
    const int preferredCardW =
        juce::jlimit(minCardW, juce::jmin(maxCardW, contentW + contentW / 3),
                     contentW);
    int cardW = preferredCardW;
    if (centerW >= minCardW)
      cardW = juce::jmin(preferredCardW, centerW - 8);
    else
      cardW = juce::jmax(0, centerW - 8);
    const int cardX = cardLeft;
    const int cardY = juce::jmax(4, (getHeight() - songCardHeight) / 2);
    songInfoBounds = juce::Rectangle<int>(cardX, cardY, cardW, songCardHeight);
  } else {
    songInfoBounds = {};
  }
}

void TopBar::mouseDown(const juce::MouseEvent &event) {
  juce::ignoreUnused(event);
}

void TopBar::mouseDoubleClick(const juce::MouseEvent &event) {
  if (!event.mods.isLeftButtonDown())
    return;
  toggleWindowMode();
}

juce::Rectangle<int> TopBar::getSongInfoAnchorBounds() const {
  if (!songInfoBounds.isEmpty())
    return songInfoBounds;
  if (!timeDisplay.getBounds().isEmpty())
    return timeDisplay.getBounds();
  if (!keyDisplay.getBounds().isEmpty())
    return keyDisplay.getBounds();
  return getLocalBounds().withSizeKeepingCentre(1, 1);
}

void TopBar::paintSongInfoCard(juce::Graphics &g) {
  if (songInfoBounds.isEmpty())
    return;

  auto cardBounds = songInfoBounds.toFloat().reduced(0.5f);
  juce::ColourGradient cardFill(
      UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.38f),
      cardBounds.getX(), cardBounds.getY(),
      UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.26f),
      cardBounds.getRight(), cardBounds.getBottom(), false);
  g.setGradientFill(cardFill);
  g.fillRoundedRectangle(cardBounds, 9.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.72f));
  g.drawRoundedRectangle(songInfoBounds.toFloat().reduced(0.5f), 9.0f, 1.0f);

  auto content = songInfoBounds.reduced(6, 5);
  if (content.isEmpty())
    return;
  const int artSize =
      juce::jmin(songArtworkSize, juce::jmin(content.getHeight(), content.getWidth()));
  auto artworkArea = juce::Rectangle<int>(
      content.getX(),
      content.getY() + juce::jmax(0, (content.getHeight() - artSize) / 2),
      artSize, artSize);

  const auto artworkFloat = artworkArea.toFloat();
  if (songArtwork.isValid()) {
    juce::Graphics::ScopedSaveState saveState(g);
    juce::Path clip;
    clip.addRoundedRectangle(artworkFloat, 5.0f);
    g.reduceClipRegion(clip);
    g.drawImageWithin(songArtwork, artworkArea.getX(), artworkArea.getY(),
                      artworkArea.getWidth(), artworkArea.getHeight(),
                      juce::RectanglePlacement::fillDestination);
  } else {
    const auto placeholder = songTitleText.trim().isNotEmpty()
                                 ? songTitleText.trim().substring(0, 1).toUpperCase()
                                 : "?";
    g.setColour(songAccentColour.withAlpha(0.28f));
    g.fillRoundedRectangle(artworkFloat, 5.0f);
    g.setColour(songAccentColour.withAlpha(0.96f));
    g.setFont(makeAppFont(songTitleFontSize + 1.5f, AppFontWeight::SemiBold));
    g.drawFittedText(placeholder, artworkArea, juce::Justification::centred, 1);
  }
  g.setColour(juce::Colours::white.withAlpha(0.12f));
  g.drawRoundedRectangle(artworkFloat.reduced(0.5f), 5.0f, 1.0f);

  const int textX = artworkArea.getRight() + 11;
  auto textArea = juce::Rectangle<int>(textX, content.getY(),
                                       juce::jmax(0, content.getRight() - textX),
                                       content.getHeight());
  if (textArea.isEmpty())
    return;

  const int titleH = responsiveProfile == ResponsiveLayoutProfile::Compact ? 14 : 16;
  const int artistH = responsiveProfile == ResponsiveLayoutProfile::Compact ? 11 : 12;
  const int lineGap = responsiveProfile == ResponsiveLayoutProfile::Compact ? 1 : 2;
  const int totalTextH = titleH + artistH + lineGap;
  const int textY = textArea.getY() +
                    juce::jmax(0, (textArea.getHeight() - totalTextH) / 2);

  auto titleRect = juce::Rectangle<int>(textArea.getX(), textY, textArea.getWidth(),
                                        titleH);
  auto artistRect =
      juce::Rectangle<int>(textArea.getX(), titleRect.getBottom() + lineGap,
                           textArea.getWidth(), artistH);

  const auto titleText = songTitleText.isNotEmpty()
                             ? songTitleText
                             : L10n::tr("Sin canción", "No song loaded");
  const auto artistText = songArtistText.isNotEmpty()
                              ? songArtistText
                              : L10n::tr("Selecciona una canción",
                                         "Select a song");

  g.setColour(UITheme::textPrimary);
  g.setFont(makeAppFont(songTitleFontSize, AppFontWeight::SemiBold));
  g.drawFittedText(titleText, titleRect, juce::Justification::centredLeft, 1);

  g.setColour(UITheme::textSecondary.withAlpha(0.95f));
  g.setFont(makeAppFont(songArtistFontSize, AppFontWeight::Medium));
  g.drawFittedText(artistText, artistRect, juce::Justification::centredLeft, 1);
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
  songTitleFontSize = 14.4f;
  songArtistFontSize = 10.8f;
  if (responsiveProfile == ResponsiveLayoutProfile::Compact) {
    displayFont = 11.5f;
    keyFont = 13.0f;
    usageFont = 11.5f;
    songTitleFontSize = 13.3f;
    songArtistFontSize = 10.2f;
  } else if (responsiveProfile == ResponsiveLayoutProfile::Large) {
    displayFont = 13.5f;
    keyFont = 15.5f;
    usageFont = 13.0f;
    songTitleFontSize = 15.4f;
    songArtistFontSize = 11.4f;
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
  setBaseTooltip(bpmSegmentButton,
                 L10n::tr(u8"Tempo y compás", "Tempo and meter"));
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
  setButtonTextIfChanged(bpmSegmentButton, bpm > 0 ? juce::String(bpm) : "120");
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
  const auto songId = songIdProvider ? songIdProvider() : juce::String{};
  if (songId.isNotEmpty())
    SongLibraryStore::updateTempoBpmForActiveSetlist(
        songId, processor.getPlaybackTempoBpm());
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

      meterLabel.setText(L10n::tr(u8"Compás", "Meter"),
                         juce::dontSendNotification);
      bpmLabel.setText("BPM", juce::dontSendNotification);
      for (auto *label : {&meterLabel, &bpmLabel}) {
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(makeAppFont(11.0f, AppFontWeight::SemiBold));
        addAndMakeVisible(label);
      }

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
      minusButton.setConnectedEdges(juce::Button::ConnectedOnRight);
      plusButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
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
      g.setColour(UITheme::accent.withAlpha(0.18f));
      g.fillRoundedRectangle(b.removeFromTop(4.0f), 2.0f);
      g.setColour(UITheme::cardBorder);
      g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f,
                             1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(10);
      auto topRow = area.removeFromTop(32);
      tapButton.setBounds(topRow.removeFromLeft(58));
      topRow.removeFromLeft(10);
      meterLabel.setBounds(topRow.removeFromLeft(56));
      timeSigBox.setBounds(topRow);

      area.removeFromTop(10);
      auto row = area.removeFromTop(30);
      bpmLabel.setBounds(row.removeFromLeft(44));
      row.removeFromLeft(6);
      minusButton.setBounds(row.removeFromLeft(28));
      plusButton.setBounds(row.removeFromRight(28));
      bpmEditor.setBounds(row.reduced(6, 0));
    }

  private:
    juce::TextButton tapButton, minusButton, plusButton;
    juce::ComboBox timeSigBox;
    juce::TextEditor bpmEditor;
    juce::Label meterLabel, bpmLabel;
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
      tapButton.setColour(juce::TextButton::buttonColourId,
                          UITheme::accent.withAlpha(0.92f));
      tapButton.setColour(juce::TextButton::textColourOffId,
                          UITheme::onAccent);
      for (auto *label : {&meterLabel, &bpmLabel})
        label->setColour(juce::Label::textColourId, UITheme::textSecondary);
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
  panel->setSize(230, 92);
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
        activePitchDelta = semitoneDelta(baseKeySemitone, activeKeySemitone);
        processor.setPitchSemiTones(activePitchDelta);
        const auto songId = songIdProvider ? songIdProvider() : juce::String{};
        if (songId.isNotEmpty())
          SongLibraryStore::updatePitchSemitonesForActiveSetlist(songId, activePitchDelta);
        refreshFromProviders();
      });

  panel->setSize(480, 56);
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto keyAnchor =
      keyDisplay.getBounds().isEmpty() ? getSongInfoAnchorBounds()
                                       : keyDisplay.getBounds();
  const auto anchor = parentComp->getLocalArea(this, keyAnchor);
  auto &callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
                                                         anchor, parentComp);
  callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void TopBar::showSongWindowPopover() {
  class SongWindowPopover final : public juce::Component {
  public:
    SongWindowPopover(MTControlsAudioProcessor &processorRef,
                      const bool customArrangement)
        : processor(processorRef),
          customArrangementActive(customArrangement) {
      configureRow(startLabel, startMinus, startEditor, startPlus,
                   L10n::tr("Inicio", "Start"));
      configureRow(endLabel, endMinus, endEditor, endPlus,
                   L10n::tr("Fin", "End"));
      configureRow(fadeLabel, fadeMinus, fadeValue, fadePlus,
                   L10n::tr("Salida", "Fade out"));

      // For Start, "-" trims time from the beginning and "+" restores it.
      startMinus.onClick = [this] { adjustStart(stepSeconds()); };
      startPlus.onClick = [this] { adjustStart(-stepSeconds()); };
      endMinus.onClick = [this] { adjustEnd(-stepSeconds()); };
      endPlus.onClick = [this] { adjustEnd(stepSeconds()); };
      fadeMinus.onClick = [this] { adjustFade(-stepSeconds()); };
      fadePlus.onClick = [this] { adjustFade(stepSeconds()); };
      startEditor.onReturnKey = [this] { submitStartEditor(); };
      startEditor.onFocusLost = [this] { submitStartEditor(); };
      endEditor.onReturnKey = [this] { submitEndEditor(); };
      endEditor.onFocusLost = [this] { submitEndEditor(); };

      for (auto *btn : {&startMinus, &startPlus, &endMinus, &endPlus,
                        &fadeMinus, &fadePlus})
        btn->setEnabled(true);

      if (customArrangementActive) {
        arrangementInfoLabel.setText(
            L10n::tr("Arreglo personalizado: Inicio/Fin delimitan la reproducción",
                     "Custom arrangement: Start/End limit playback"),
            juce::dontSendNotification);
        arrangementInfoLabel.setJustificationType(juce::Justification::centred);
        arrangementInfoLabel.setFont(juce::FontOptions(11.0f));
        addAndMakeVisible(arrangementInfoLabel);
      }

      refreshValues();
      applyTheme();
    }

    void refreshValues() {
      const double start = processor.getSongStartSeconds();
      const double end = processor.getSongEndSeconds();
      startValue.setText(formatTimeSeconds(start),
                         juce::dontSendNotification);
      endValue.setText(formatTimeSeconds(end),
                       juce::dontSendNotification);
      startEditor.setText(formatTimeSeconds(start),
                          juce::dontSendNotification);
      endEditor.setText(formatTimeSeconds(end),
                        juce::dontSendNotification);
      const int fadeSecs =
          static_cast<int>(std::round(processor.getSongFadeOutSeconds()));
      fadeValue.setText(juce::String(fadeSecs) + " " + L10n::tr("seg", "sec"),
                        juce::dontSendNotification);
      updateButtonStates(start, end);
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
      if (customArrangementActive) {
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
      if (customArrangementActive) {
        area.removeFromTop(6);
        arrangementInfoLabel.setBounds(area.removeFromTop(18));
      }
    }

  private:
    MTControlsAudioProcessor &processor;
    bool customArrangementActive = false;
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

    void updateButtonStates(const double start, const double end) {
      constexpr double epsilon = 0.0001;
      const double length = juce::jmax(0.0, processor.getSongLengthSeconds());
      const double fade = processor.getSongFadeOutSeconds();
      const double windowLength = juce::jmax(0.0, end - start);

      startPlus.setEnabled(start > epsilon);
      startMinus.setEnabled(start < end - epsilon);
      endMinus.setEnabled(end > start + epsilon);
      endPlus.setEnabled(end < length - epsilon);
      fadeMinus.setEnabled(fade > epsilon);
      fadePlus.setEnabled(fade < windowLength - epsilon);
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
  const bool customArrangementActive = processor.isCustomArrangementActive();
  auto panel =
      std::make_unique<SongWindowPopover>(processor, customArrangementActive);
  panel->setSize(230, customArrangementActive ? 156 : 128);
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto timeAnchor =
      timeDisplay.getBounds().isEmpty() ? getSongInfoAnchorBounds()
                                        : timeDisplay.getBounds();
  const auto anchor = parentComp->getLocalArea(this, timeAnchor);
  auto &callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
                                                         anchor, parentComp);
  callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void TopBar::refreshFromProviders() {
  bool songCardNeedsRepaint = false;
  auto assignSongText = [&songCardNeedsRepaint](juce::String &target,
                                                juce::String value) {
    if (target == value)
      return;
    target = std::move(value);
    songCardNeedsRepaint = true;
  };
  auto assignSongAccent = [&songCardNeedsRepaint](juce::Colour &target,
                                                  juce::Colour value) {
    if (target == value)
      return;
    target = value;
    songCardNeedsRepaint = true;
  };

  const double pos = positionProvider ? positionProvider() : 0.0;
  const double len = lengthProvider ? lengthProvider() : 0.0;
  setLabelTextIfChanged(timeDisplay, formatTimeSeconds(pos) + " / " +
                                         formatTimeSeconds(len));

  updateBpmDisplay();

  const bool songLoaded = processor.hasLoadedSong();
  const ImportedSongData *loadedSong = songProvider ? songProvider() : nullptr;
  const auto songIdFromProvider =
      songIdProvider ? songIdProvider().trim() : juce::String{};
  const auto activeSongId =
      loadedSong != nullptr && loadedSong->id.isNotEmpty()
          ? loadedSong->id.trim()
          : songIdFromProvider;
  const auto activeSongIdentity =
      loadedSong != nullptr ? playbackIdentityForSong(*loadedSong)
                            : activeSongId + "|legacy";

  if (!songLoaded) {
    if (hasKey || lastKeyToken.isNotEmpty() || lastSongIdentity.isNotEmpty()) {
      hasKey = false;
      lastSongIdentity.clear();
      lastArtworkPath.clear();
      if (songArtwork.isValid()) {
        songArtwork = {};
        songCardNeedsRepaint = true;
      }
      lastKeyToken.clear();
      baseKeySemitone = 0;
      activeKeySemitone = 0;
      activeKeyLabel = L10n::tr("Tono", "Key");
      processor.setPitchSemiTones(0);
    }
    assignSongText(songTitleText, L10n::tr("Sin canción", "No song loaded"));
    assignSongText(songArtistText,
                   L10n::tr("Selecciona una canción", "Select a song"));
    assignSongAccent(songAccentColour, UITheme::accentMuted);
    setLabelTextIfChanged(keyDisplay, L10n::tr("Tono", "Key"));
  } else {
    const auto keyText =
        loadedSong != nullptr && loadedSong->key.trim().isNotEmpty()
            ? loadedSong->key.trim()
            : (keyProvider ? keyProvider().trim() : juce::String{});
    const auto songTitle =
        loadedSong != nullptr && loadedSong->title.trim().isNotEmpty()
            ? loadedSong->title.trim()
            : processor.getLoadedSongTitle().trim();
    const auto songArtist =
        loadedSong != nullptr && loadedSong->artist.trim().isNotEmpty()
            ? loadedSong->artist.trim()
            : L10n::tr("Sin artista", "Unknown artist");

    const bool songChanged = activeSongIdentity != lastSongIdentity;
    if (songChanged || !hasKey) {
      lastSongIdentity = activeSongIdentity;
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
      activePitchDelta =
          juce::jlimit(-12, 12, processor.getPitchSemiTones());
      activeKeySemitone = normalizeSemitone(baseKeySemitone + activePitchDelta);
      activeKeyLabel = noteLabelForSemitone(activeKeySemitone);
      processor.setPitchSemiTones(activePitchDelta);
      updateBpmDisplay();
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
        activeKeyLabel = keyText.isNotEmpty() ? keyText : L10n::tr("Tono", "Key");
      }
    }

    if (activeKeyLabel.isEmpty())
      activeKeyLabel = L10n::tr("Tono", "Key");
    activePitchDelta = semitoneDelta(baseKeySemitone, activeKeySemitone);
    processor.setPitchSemiTones(activePitchDelta);
    setLabelTextIfChanged(keyDisplay, activeKeyLabel);

    assignSongText(songTitleText,
                   songTitle.isNotEmpty()
                       ? songTitle
                       : L10n::tr("Canción sin título", "Untitled Song"));
    assignSongText(songArtistText, songArtist);
    assignSongAccent(songAccentColour, colourfulAccentFromStableId(activeSongId));

    if (loadedSong != nullptr) {
      const auto artworkPath = loadedSong->artworkPath.trim();
      if (artworkPath != lastArtworkPath) {
        lastArtworkPath = artworkPath;
        songArtwork = {};
        songCardNeedsRepaint = true;
        if (artworkPath.isNotEmpty()) {
          const juce::File artworkFile(artworkPath);
          if (artworkFile.existsAsFile())
            songArtwork = juce::ImageFileFormat::loadFrom(artworkFile);
        }
      }
    } else {
      if (lastArtworkPath.isNotEmpty() || songArtwork.isValid()) {
        lastArtworkPath.clear();
        songArtwork = {};
        songCardNeedsRepaint = true;
      }
    }
  }

  const bool playing = playingProvider ? playingProvider() : false;
  playButton.setIcon(playing ? SvgIconName::Pause : SvgIconName::Play);
  if (playButton.getToggleState() != playing)
    playButton.setToggleState(playing, juce::dontSendNotification);
  const bool stopped = !playing && pos <= 0.05;
  if (stopButton.getToggleState() != stopped)
    stopButton.setToggleState(stopped, juce::dontSendNotification);
  const bool fadeOutActive = processor.isFadeActive();
  fadeButton.setIcon(fadeOutActive ? SvgIconName::FadeIn
                                   : SvgIconName::FadeOut);
  if (fadeButton.getToggleState() != fadeOutActive)
    fadeButton.setToggleState(fadeOutActive, juce::dontSendNotification);
  if (!midiLearnActive)
    fadeButton.setBadgeText(fadeOutActive ? "ON" : juce::String{});
  const bool metronomeEnabled = processor.isMetronomeEnabled();
  if (metroToggleButton.getToggleState() != metronomeEnabled)
    metroToggleButton.setToggleState(metronomeEnabled,
                                     juce::dontSendNotification);
  if (!midiLearnActive)
    metroToggleButton.setBadgeText(metronomeEnabled ? "ON" : juce::String{});

  if (songCardNeedsRepaint)
    repaint(songInfoBounds.expanded(2));
}
