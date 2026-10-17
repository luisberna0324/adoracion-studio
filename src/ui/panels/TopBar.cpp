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
#if MTCONTROLS_NATIVE_APP && JUCE_MAC
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

void TopBar::DesktopTextButton::paintButton(juce::Graphics &g, bool over, bool down) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  const auto path = roundedControlPath(bounds, getConnectedEdgeFlags(), 7.0f);
  auto fill = getToggleState() ? UITheme::accent.withAlpha(0.22f) : UITheme::cardBg;
  if (over) fill = fill.brighter(0.05f);
  if (down) fill = fill.darker(0.06f);
  g.setColour(fill.withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
  g.fillPath(path);
  g.setColour((getToggleState() ? UITheme::accentSoft : UITheme::cardBorder)
                  .withMultipliedAlpha(isEnabled() ? 0.85f : 0.4f));
  g.strokePath(path, juce::PathStrokeType(1.0f));
  g.setColour((getToggleState() ? UITheme::accentSoft : UITheme::textPrimary)
                  .withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
  g.setFont(makeAppFont(getButtonText() == "?" ? 21.0f : 12.0f, AppFontWeight::SemiBold));
  g.drawFittedText(getButtonText(), getLocalBounds().reduced(4, 2), juce::Justification::centred, 1);
}

void TopBar::BpmSegmentButton::paintButton(juce::Graphics &g,
                                           bool isMouseOverButton,
                                           bool isButtonDown) {
  auto b = getLocalBounds().toFloat().reduced(0.5f);
  const auto path = roundedControlPath(b, getConnectedEdgeFlags(), 10.0f);
  auto fill = UITheme::cardBg;
  if (isButtonDown)
    fill = fill.brighter(0.08f);
  if (isMouseOverButton && !isButtonDown)
    fill = fill.brighter(0.05f);

  g.setColour(UITheme::cardBorder.withAlpha(0.55f));
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

TopBar::TopBar(MTControlsAudioProcessor &processorRef, PlaybackSession *sessionToUse)
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
      tabletMoreButton("tabletMore", SvgIconName::MoreVertical),
      phonePadsButton("phonePads", SvgIconName::AudioWaveform),
      processor(processorRef), localSession(processorRef), session(sessionToUse ? *sessionToUse : localSession) {
  for (auto *btn :
       {&prevButton, &seekBackButton, &playButton, &stopButton,
        &seekForwardButton, &nextButton, &fadeButton, &windowModeButton,
        &settingsButton, &midiLearnButton, &tabletMoreButton,
        &phonePadsButton}) {
    addAndMakeVisible(*btn);
  }
  addChildComponent(songPickerButton);
  for (auto *button : {&waveformViewButton, &sectionsViewButton, &helpButton}) {
    addChildComponent(*button);
    button->setComponentID(button->getName());
    button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  waveformViewButton.setConnectedEdges(juce::Button::ConnectedOnRight);
  sectionsViewButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
  waveformViewButton.setToggleable(true);
  sectionsViewButton.setToggleable(true);
  waveformViewButton.onClick = [this] { selectTimelineView(false); };
  sectionsViewButton.onClick = [this] { selectTimelineView(true); };
#if !JUCE_IOS && !JUCE_ANDROID
  addChildComponent(lyricsViewButton);
  lyricsViewButton.setComponentID("timeline-lyrics");
  lyricsViewButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  lyricsViewButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
  sectionsViewButton.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
  lyricsViewButton.setToggleable(true);
  lyricsViewButton.onClick = [this] {
    if (onLyricsViewSelect && (!lyricsViewEnabled || lyricsViewEnabled()))
      onLyricsViewSelect(true);
    refreshTimelineMenu();
  };
#endif
  helpButton.setButtonText("?");
  helpButton.onClick = [this] { if (onLearningRequested) onLearningRequested(); };
  refreshTimelineMenu();
  phonePadsButton.setVisible(false);
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
  setBaseTooltip(tabletMoreButton,
                 L10n::tr("Más opciones", "More options"));
  songPickerButton.setTooltip(L10n::tr("Canciones del setlist", "Setlist songs"));
  songPickerButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setBaseTooltip(phonePadsButton, L10n::tr("Pads", "Pads"));
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
    const bool shouldFadeOut = !session.isFadeActive();
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
  tabletMoreButton.onClick = [this] { showTabletMorePopover(); };
  songPickerButton.onClick = [this] {
    if (onPhoneLibrary)
      onPhoneLibrary(songPickerButton);
  };
  phonePadsButton.onClick = [this] { triggerPhonePadsPopover(); };
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
      metroToggleButton.setToggleState(session.isMetronomeEnabled(),
                                       juce::dontSendNotification);
      return;
    }
    const bool enabled = metroToggleButton.getToggleState();
    session.setMetronomeEnabled(enabled);
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

void TopBar::setLiveModeControls(std::function<void()> toggle,
                                 std::function<bool()> state,
                                 std::function<bool()> enabled) {
  onLiveModeToggle = std::move(toggle);
  liveModeState = std::move(state);
  liveModeEnabled = std::move(enabled);
  refreshTimelineMenu();
}

void TopBar::refreshTimelineMenu() {
  const bool available = timelineControlsAvailable();
  if (available != lastTimelineControlsAvailable) {
    lastTimelineControlsAvailable = available;
    resized();
  }
  const bool sections = liveModeState && liveModeState();
  bool lyrics = false;
#if !JUCE_IOS && !JUCE_ANDROID
  lyrics = lyricsViewState && lyricsViewState();
  lyricsViewButton.setButtonText(L10n::tr("Letras", "Lyrics"));
  lyricsViewButton.setTitle(L10n::tr("Letras", "Lyrics"));
  lyricsViewButton.setTooltip(L10n::tr("Marcas independientes de letras · FreeShow", "Independent lyrics marks · FreeShow"));
  lyricsViewButton.setEnabled(true);
  lyricsViewButton.setToggleState(lyrics, juce::dontSendNotification);
  lyricsViewButton.setEnabled(available && onLyricsViewSelect && (!lyricsViewEnabled || lyricsViewEnabled()));
#endif
  const bool enabled = available && onLiveModeToggle && (!liveModeEnabled || liveModeEnabled());
  for (auto *button : {&waveformViewButton, &sectionsViewButton}) {
    const auto text = button == &waveformViewButton ? L10n::tr("Onda", "Waveform")
                                                   : L10n::tr("Secciones", "Sections");
    const bool selected = !lyrics && ((button == &sectionsViewButton) == sections);
    setButtonTextIfChanged(*button, text);
    button->setTitle(text);
    button->setTooltip(text);
    button->setDescription(text + (selected ? L10n::tr(" — seleccionada", " — selected") : juce::String{}));
    // JUCE ignores setToggleState on a disabled button. Reconcile the actual
    // view even when an editing lock has disabled the selector.
    if (button->getToggleState() != selected) {
      button->setEnabled(true);
      button->setToggleState(selected, juce::dontSendNotification);
    }
    button->setEnabled(enabled);
  }
  helpButton.setTitle(L10n::tr("Ayuda", "Help"));
  helpButton.setTooltip(L10n::tr("Aprender PlayWorship", "Learn PlayWorship"));
  if (refreshMorePopover) refreshMorePopover();
}

void TopBar::selectTimelineView(bool sections) {
  if (!timelineControlsAvailable()) return;
  if (!onLiveModeToggle || (liveModeEnabled && !liveModeEnabled())) return;
#if !JUCE_IOS && !JUCE_ANDROID
  if (onLyricsViewSelect) onLyricsViewSelect(false);
#endif
  if (sections != (liveModeState && liveModeState())) onLiveModeToggle();
  refreshTimelineMenu();
}

#if !JUCE_IOS && !JUCE_ANDROID
void TopBar::setLyricsViewControls(std::function<void(bool)> select,
                                    std::function<bool()> state,
                                    std::function<bool()> enabled) {
  onLyricsViewSelect = std::move(select);
  lyricsViewState = std::move(state);
  lyricsViewEnabled = std::move(enabled);
  refreshTimelineMenu();
}
#endif

void TopBar::setOnSettingsWithAnchor(
    std::function<void(juce::Rectangle<int>, juce::Component *)> cb) {
  onSettingsWithAnchor = std::move(cb);
}

void TopBar::setOnPhoneLibrary(std::function<void(juce::Component &)> cb) {
  onPhoneLibrary = std::move(cb);
}

void TopBar::setOnPhonePads(std::function<void(juce::Component &)> cb) {
  onPhonePads = std::move(cb);
}

void TopBar::setOnPhonePractice(std::function<void()> cb) {
  onPhonePractice = std::move(cb);
}

void TopBar::setOnLearningRequested(std::function<void()> cb) {
  onLearningRequested = std::move(cb);
}

juce::Rectangle<int> TopBar::getLearningTargetBounds(playworship::learning::Anchor anchor) const {
  using Anchor = playworship::learning::Anchor;
  const juce::Component *target = nullptr;
  switch (anchor) {
  case Anchor::SongSelection: return songInfoBounds;
  case Anchor::PlayPause: target = &playButton; break;
  case Anchor::Stop: target = &stopButton; break;
  case Anchor::Metronome: target = &metroToggleButton; break;
  case Anchor::Tempo: target = &bpmSegmentButton; break;
  case Anchor::Key: target = &keyDisplay; break;
  case Anchor::Pads: target = &phonePadsButton; break;
  case Anchor::More: target = sectionsViewButton.isVisible() ? static_cast<const juce::Component *>(&sectionsViewButton)
      : tabletMoreButton.isVisible() ? static_cast<const juce::Component *>(&tabletMoreButton) : &settingsButton; break;
  case Anchor::Settings: target = settingsButton.isVisible() ? &settingsButton : &tabletMoreButton; break;
  default: break;
  }
  return target != nullptr && target->isVisible() ? target->getBounds() : juce::Rectangle<int>{};
}

void TopBar::triggerPhonePadsPopover() {
  if (onPhonePads)
    onPhonePads(phonePadsButton);
}

void TopBar::setPhoneOverlayStates(const bool /*libraryOpen*/,
                                   const bool padsOpen) {
  phonePadsButton.setToggleState(padsOpen, juce::dontSendNotification);
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

void TopBar::setMinimalPhoneMode(const bool enabled) {
  if (minimalPhoneMode == enabled)
    return;
  minimalPhoneMode = enabled;
  applyResponsiveTypography();
  resized();
  repaint();
}

void TopBar::setPhoneSafeAreaInsets(const juce::BorderSize<int> insets) {
  if (phoneSafeAreaInsets == insets)
    return;
  phoneSafeAreaInsets = insets;
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
#if JUCE_ANDROID
  // The Android peer software-paints each frame. The studio background
  // (tiled noise + gradients) is the dominant cost here, and the bar is
  // re-laid out over the same flat colour every tick. Use a flat base so
  // the timer-driven repaints stay cheap.
  g.fillAll(UITheme::panelDark);
#else
  drawStudioBackground(g, b);
#endif
  g.setColour(UITheme::panelDark.withAlpha(0.10f));
  g.fillRect(b);
  g.setColour(UITheme::dividerSoft);
  g.drawLine(0.0f, static_cast<float>(getHeight()) - 0.5f,
             static_cast<float>(getWidth()),
             static_cast<float>(getHeight()) - 0.5f, 1.0f);

  if (!minimalPhoneMode && !songInfoBounds.isEmpty()) {
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
  const auto previousSongBounds = songInfoBounds;
  const auto contentBounds = minimalPhoneMode
                                 ? phoneSafeAreaInsets.subtractedFrom(getLocalBounds())
                                 : getLocalBounds();
  const bool compactTouchTablet =
      useTouchOptimisedControls() && !minimalPhoneMode &&
      responsiveProfile == ResponsiveLayoutProfile::Compact;
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

  if (minimalPhoneMode) {
    const int phoneWidth = contentBounds.getWidth();
    if (phoneWidth < 760) {
      square = 44;
      gap = 6;
      keyWidth = 56;
      metroTotalW = 88;
      metroIconW = 44;
      timeWidth = 72;
      minCardW = 112;
      maxCardW = 170;
      songCardHeight = 40;
      songArtworkSize = 30;
      if (phoneWidth < 620) { gap = 4; keyWidth = 44; }
    }

    bool useRegularPhoneControls = phoneWidth < 880;
#if JUCE_ANDROID
    // Android landscape windows around 900 dp can report little or no
    // horizontal safe-area inset. The large control set then consumes the
    // complete row and clips the final metronome/key action at the right edge.
    useRegularPhoneControls = phoneWidth < 940;
#endif

    if (phoneWidth >= 760 && useRegularPhoneControls) {
      square = 46;
      gap = 8;
      keyWidth = 62;
      metroTotalW = 94;
      metroIconW = 46;
      timeWidth = 78;
      minCardW = 124;
      maxCardW = 200;
      songCardHeight = 42;
      songArtworkSize = 32;
    } else if (phoneWidth >= 760) {
      square = 50;
      gap = 10;
      keyWidth = 70;
      metroTotalW = 104;
      metroIconW = 48;
      timeWidth = 86;
      minCardW = 140;
      maxCardW = 230;
      songCardHeight = 46;
      songArtworkSize = 34;
    }
  } else if (useTouchOptimisedControls()) {
#if JUCE_ANDROID
    square = 44;
    gap = 8;
    keyWidth = 76;
    metroTotalW = 108;
    metroIconW = 44;
    timeWidth = 166;
    usageWidth = 90;
    minUsageWidth = 76;
    minCardW = 176;
    maxCardW = 325;
    songCardHeight = 46;
    songArtworkSize = 36;
#else
    square = 50;
    gap = 10;
    keyWidth = 82;
    metroTotalW = 116;
    metroIconW = 48;
    timeWidth = 184;
    usageWidth = 96;
    minUsageWidth = 82;
    minCardW = 190;
    maxCardW = 360;
    songCardHeight = 54;
    songArtworkSize = 42;
#endif
    if (compactTouchTablet) {
      // Small tablets have the full tablet feature set, but their landscape
      // width cannot accommodate the large tablet timer and still preserve
      // the complete BPM/metronome control. Use the mobile-sized header
      // geometry while keeping the tablet song card and More popover.
      square = 44;
      gap = 8;
      keyWidth = 68;
      metroTotalW = 102;
      metroIconW = 44;
      timeWidth = 78;
      minCardW = 130;
      maxCardW = 250;
      songCardHeight = 44;
      songArtworkSize = 34;
    }
  }

  const bool desktopProfile = !useTouchOptimisedControls() && !minimalPhoneMode;
  if (desktopProfile && contentBounds.getWidth() < 1280) {
    square = 32;
    gap = 4;
    keyWidth = 52;
    metroTotalW = 84;
    metroIconW = 32;
    timeWidth = 80;
    minCardW = 148;
  }

  const bool minimalPhoneProfile = minimalPhoneMode;
  setVisibleIfChanged(songPickerButton, minimalPhoneProfile);
  setVisibleIfChanged(phonePadsButton, minimalPhoneProfile);
  // Phones keep seek in secondary controls, but expose the contextual
  // Fade Down/Fade Up toggle immediately after Next.
  setVisibleIfChanged(seekBackButton, !minimalPhoneProfile);
  setVisibleIfChanged(seekForwardButton, !minimalPhoneProfile);
  setVisibleIfChanged(fadeButton, true);
  const int transportButtonCount = minimalPhoneProfile ? 5 : 7;
  int horizontalPadding = 12;
  if (minimalPhoneProfile && contentBounds.getWidth() < 620) horizontalPadding = 0;
#if JUCE_IOS
  if (minimalPhoneProfile)
    horizontalPadding = contentBounds.getWidth() < 620 ? 0 : 4;
#endif
  const int leftPadding = contentBounds.getX() + horizontalPadding;
  const int rightPadding = horizontalPadding;
  const int transportControlsW =
      (transportButtonCount * square) + ((transportButtonCount - 1) * gap);
  const int controlY = contentBounds.getY() +
                      juce::jmax(0, (contentBounds.getHeight() - square) / 2);

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
  if (!minimalPhoneProfile)
    addTransportButton(seekBackButton);
  addTransportButton(playButton);
  addTransportButton(stopButton);
  if (!minimalPhoneProfile)
    addTransportButton(seekForwardButton);
  addTransportButton(nextButton);
  addTransportButton(fadeButton, true);
  transportRow.performLayout(juce::Rectangle<float>(
      static_cast<float>(leftPadding), static_cast<float>(controlY),
      static_cast<float>(transportControlsW), static_cast<float>(square)));

  const int transportW = leftPadding + transportControlsW + gap;
  const bool tabletProfile = useTouchOptimisedControls() || minimalPhoneMode;
  const bool showDesktopExtras = !tabletProfile;
  const bool showWindowButton = shouldShowWindowModeButton() && !tabletProfile;
  const bool showTabletMoreButton = tabletProfile;
  const bool compactTimeline = desktopProfile && contentBounds.getWidth() < 1280;
  const int waveformViewWidth = compactTimeline ? 50 : 62;
  const int sectionsViewWidth = compactTimeline ? 68 : 78;
#if !JUCE_IOS && !JUCE_ANDROID
  const int lyricsViewWidth = compactTimeline ? 50 : 62;
#endif
  const bool showTimeline = timelineControlsAvailable();
  int timelineWidth = showTimeline ? waveformViewWidth + sectionsViewWidth : 0;
#if !JUCE_IOS && !JUCE_ANDROID
  if (showTimeline) timelineWidth += lyricsViewWidth;
  setVisibleIfChanged(lyricsViewButton, desktopProfile && showTimeline);
  if (!desktopProfile) lyricsViewButton.setBounds({});
#endif
  setVisibleIfChanged(waveformViewButton, desktopProfile && showTimeline);
  setVisibleIfChanged(sectionsViewButton, desktopProfile && showTimeline);
  setVisibleIfChanged(helpButton, desktopProfile);
  if (!desktopProfile) {
    waveformViewButton.setBounds({}); sectionsViewButton.setBounds({}); helpButton.setBounds({});
  }
  const int fixedButtonCount = 1 + (showWindowButton ? 1 : 0) + (showDesktopExtras ? 2 : 0);
  const int minFixedButtonsW =
      (fixedButtonCount * square) + ((fixedButtonCount - 1) * gap);
  const int availableAfterTransport = contentBounds.getRight() - rightPadding - transportW - 8;
  bool showSystemUsage = !tabletProfile;
  const int maxUsageWidth =
      availableAfterTransport - minFixedButtonsW - gap;
  if (maxUsageWidth < minUsageWidth) {
    showSystemUsage = false;
  } else {
    usageWidth = juce::jmin(usageWidth, juce::jmax(minUsageWidth, maxUsageWidth));
  }
  bool showBpmText = true;
  bool showTimeDisplay = true;
  auto layoutRightSide = [&](const bool usageVisible,
                             const bool bpmVisible) -> int {
    const int metronomeW = bpmVisible ? metroTotalW : metroIconW;
    int controlsW = (showTimeDisplay ? timeWidth + gap : 0) + metronomeW + gap + keyWidth;
    if (minimalPhoneProfile)
      controlsW += square + gap;
    controlsW += gap + square;
    if (desktopProfile && showTimeline) controlsW += gap + timelineWidth;
    if (showWindowButton) controlsW += gap + square;
    if (showDesktopExtras) controlsW += 2 * (gap + square);
    if (usageVisible)
      controlsW += gap + usageWidth;

    const int leftX = contentBounds.getRight() - rightPadding - controlsW;

    setVisibleIfChanged(timeDisplay, showTimeDisplay);
    setVisibleIfChanged(midiLearnButton, showDesktopExtras);
    setVisibleIfChanged(settingsButton, showDesktopExtras);
    setVisibleIfChanged(keyDisplay, true);
    setVisibleIfChanged(bpmSegmentButton, bpmVisible);
    setVisibleIfChanged(systemUsageDisplay, usageVisible);
    setVisibleIfChanged(windowModeButton, showWindowButton);
    setVisibleIfChanged(tabletMoreButton, showTabletMoreButton);

    if (!bpmVisible)
      setBoundsIfChanged(bpmSegmentButton, {});
    if (!usageVisible)
      setBoundsIfChanged(systemUsageDisplay, {});
    if (!showWindowButton)
      setBoundsIfChanged(windowModeButton, {});
    if (!showTabletMoreButton)
      setBoundsIfChanged(tabletMoreButton, {});

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

    if (minimalPhoneProfile) {
      addItem(phonePadsButton, square);
    }
    if (showTimeDisplay) addItem(timeDisplay, timeWidth);
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
    addItem(keyDisplay, keyWidth, false);
    if (showDesktopExtras) addItem(midiLearnButton, square);
    if (showWindowButton)
      addItem(windowModeButton, square);
    if (showDesktopExtras) addItem(settingsButton, square);
    if (desktopProfile) {
      if (showTimeline) {
        rightRow.items.add(juce::FlexItem(waveformViewButton)
            .withWidth(static_cast<float>(waveformViewWidth)).withHeight(static_cast<float>(square)));
#if !JUCE_IOS && !JUCE_ANDROID
        rightRow.items.add(juce::FlexItem(sectionsViewButton)
            .withWidth(static_cast<float>(sectionsViewWidth)).withHeight(static_cast<float>(square)));
        addItem(lyricsViewButton, lyricsViewWidth);
#else
        addItem(sectionsViewButton, sectionsViewWidth);
#endif
      }
      addItem(helpButton, square, !usageVisible);
    } else {
      addItem(tabletMoreButton, square, !usageVisible);
    }
    if (usageVisible)
      addItem(systemUsageDisplay, usageWidth, true);

    rightRow.performLayout(juce::Rectangle<float>(
        static_cast<float>(leftX), static_cast<float>(controlY),
        static_cast<float>(controlsW), static_cast<float>(square)));

    return leftX - gap;
  };

  int rightX = 0;
  for (int attempt = 0; attempt < 4; ++attempt) {
    rightX = layoutRightSide(showSystemUsage, showBpmText);
    const int centerW = rightX - (transportW + 16);
    const int requiredCenterWidth = minCardW;
    if (centerW >= requiredCenterWidth)
      break;
    if (desktopProfile && showSystemUsage) {
      showSystemUsage = false;
      continue;
    }
    if (showBpmText) {
      showBpmText = false;
      continue;
    }
    if (showSystemUsage) {
      showSystemUsage = false;
      continue;
    }
    if (showTimeDisplay && minimalPhoneProfile && contentBounds.getWidth() < 700) {
      showTimeDisplay = false;
      continue;
    }
    break;
  }

  rightX = layoutRightSide(showSystemUsage, showBpmText);

  const int cardLeft = transportW + (minimalPhoneMode ? 0 : 8);
  const int cardRight = rightX - (minimalPhoneMode ? 0 : 8);
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
    if (minimalPhoneMode)
      cardW = juce::jmin(preferredCardW, centerW);
    else if (centerW >= minCardW)
      cardW = juce::jmin(preferredCardW, centerW - 8);
    else
      cardW = juce::jmax(0, centerW - 8);
    const int cardX = cardLeft;
    const int cardY = minimalPhoneMode ? controlY
                                      : juce::jmax(4, (getHeight() - songCardHeight) / 2);
    songInfoBounds = juce::Rectangle<int>(cardX, cardY, cardW,
                                         minimalPhoneMode ? square : songCardHeight);
  } else {
    songInfoBounds = {};
  }
  songPickerButton.setBounds(minimalPhoneMode ? songInfoBounds
                                             : juce::Rectangle<int>{});
  songPickerButton.setTitle(songTitleText + " — " + songArtistText);
  if (songInfoBounds != previousSongBounds)
    songTextScrollStartMs = juce::Time::getMillisecondCounterHiRes();
  if (refreshMorePopover) refreshMorePopover();
}

void TopBar::mouseDoubleClick(const juce::MouseEvent &event) {
  if (!event.mods.isLeftButtonDown())
    return;
  if (minimalPhoneMode && songInfoBounds.contains(event.getPosition()))
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
  songTextOverflows = false;
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

  const double elapsed = juce::jmax(0.0,
      (juce::Time::getMillisecondCounterHiRes() - songTextScrollStartMs) * 0.001);
  auto drawScrollingLine = [&](const juce::String &text,
                               const juce::Font &font,
                               juce::Rectangle<int> viewport,
                               juce::Colour colour) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    const auto ink = glyphs.getBoundingBox(0, -1, true);
    const float overflow = juce::jmax(
        0.0f, ink.getWidth() - static_cast<float>(viewport.getWidth()));
    float offset = 0.0f;
    if (overflow > 0.0f) {
      songTextOverflows = true;
      constexpr double pauseSeconds = 1.5;
      constexpr double pointsPerSecond = 24.0;
      const double travel = juce::jmax(0.6, overflow / pointsPerSecond);
      const double phase = std::fmod(elapsed, 2.0 * (pauseSeconds + travel));
      // Pause at either end and ease into each direction; no wrap or jump.
      const double progress = phase < pauseSeconds ? 0.0
          : phase < pauseSeconds + travel ? (phase - pauseSeconds) / travel
          : phase < 2.0 * pauseSeconds + travel ? 1.0
          : 1.0 - (phase - 2.0 * pauseSeconds - travel) / travel;
      offset = overflow * static_cast<float>(progress * progress * (3.0 - 2.0 * progress));
    }
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(viewport);
    g.setColour(colour);
    glyphs.draw(g, juce::AffineTransform::translation(
        static_cast<float>(viewport.getX()) - ink.getX() - offset,
        static_cast<float>(viewport.getCentreY()) - ink.getCentreY()));
  };
  drawScrollingLine(titleText,
                    makeAppFont(songTitleFontSize, AppFontWeight::SemiBold),
                    titleRect, UITheme::textPrimary);
  drawScrollingLine(artistText,
                    makeAppFont(songArtistFontSize, AppFontWeight::Medium),
                    artistRect, UITheme::textSecondary.withAlpha(0.95f));
}


void TopBar::clearFadeIfActive() {
  if (!session.isFadeActive())
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
  if (onSettingsWithAnchor) {
    const auto anchor = tabletMoreButton.isVisible()
                            ? tabletMoreButton.getBounds()
                            : settingsButton.getBounds();
    onSettingsWithAnchor(anchor, this);
  }
  else if (onSettings)
    onSettings();
}

std::pair<bool, bool> TopBar::morePrimaryActions() const {
  const bool desktop = !useTouchOptimisedControls() && !minimalPhoneMode;
  const auto accessible = [this](const juce::Button &button) {
    return button.isVisible() && button.isEnabled() && !button.getBounds().isEmpty() &&
           getLocalBounds().contains(button.getBounds());
  };
  return {!desktop || !accessible(midiLearnButton), !desktop || !accessible(settingsButton)};
}

std::unique_ptr<juce::Component> TopBar::createMorePopoverContent() {

  class ActionButton final : public juce::Button {
  public:
    ActionButton(juce::String titleText, const SvgIconName iconToUse,
                 std::function<void()> actionToRun)
        : juce::Button(titleText), title(std::move(titleText)),
          icon(iconToUse), action(std::move(actionToRun)) {
      onClick = [this] {
        auto actionToInvoke = action;
        if (auto *callout = findParentComponentOfClass<juce::CallOutBox>())
          callout->dismiss();
        if (actionToInvoke)
          juce::MessageManager::callAsync(std::move(actionToInvoke));
      };
    }

    void paintButton(juce::Graphics &g, const bool isMouseOverButton,
                     const bool isButtonDown) override {
      auto bounds = getLocalBounds().toFloat().reduced(0.5f);
      auto fill = getToggleState()
                      ? UITheme::accent.withAlpha(0.24f)
                      : UITheme::cardBgHover;
      if (isMouseOverButton)
        fill = fill.brighter(0.05f);
      if (isButtonDown)
        fill = fill.darker(0.06f);
      g.setColour(fill);
      g.fillRoundedRectangle(bounds, 10.0f);
      g.setColour((getToggleState() ? UITheme::accentSoft
                                    : UITheme::cardBorder)
                      .withAlpha(0.82f));
      g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

      auto content = getLocalBounds().reduced(16, 8);
      auto iconArea = content.removeFromLeft(30).toFloat().reduced(3.0f);
      drawSvgIcon(g, iconArea, icon,
                  getToggleState() ? UITheme::accentSoft
                                   : UITheme::textPrimary);
      content.removeFromLeft(10);
      if (getProperties().getWithDefault("timelineOption", false) && getToggleState()) {
        auto mark = content.removeFromRight(20).toFloat();
        juce::Path check;
        check.startNewSubPath(mark.getX() + 2, mark.getCentreY());
        check.lineTo(mark.getX() + 7, mark.getCentreY() + 5);
        check.lineTo(mark.getX() + 16, mark.getCentreY() - 6);
        g.setColour(UITheme::accentSoft);
        g.strokePath(check, juce::PathStrokeType(2.0f));
      }
      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
      g.drawFittedText(title, content, juce::Justification::centredLeft, 1);
    }

  private:
    juce::String title;
    SvgIconName icon;
    std::function<void()> action;
  };

  class MorePopover final : public juce::Component {
  public:
    MorePopover(std::function<void()> onSections,
                const bool midiActive, std::function<void()> onMidi,
                std::function<void()> onSettings,
                std::function<void()> onPractice, const bool showPractice,
                std::function<void()> onLearning,
                std::function<void()> onWindow, const bool showWindow)
        : sectionsButton(L10n::tr("Vista de secciones", "Sections view"), SvgIconName::Music, std::move(onSections)),
          midiButton(L10n::tr("MIDI Learn", "MIDI Learn"),
                     SvgIconName::Midi, std::move(onMidi)),
          settingsButton(L10n::tr("Configuración", "Settings"),
                         SvgIconName::Settings, std::move(onSettings)),
          practiceButton(L10n::tr("Ensayo", "Practice"),
                         SvgIconName::AudioWaveform, std::move(onPractice)),
          learningButton(L10n::tr("Aprender PlayWorship", "Learn PlayWorship"),
                         SvgIconName::Music, std::move(onLearning)),
          songWindowButton(L10n::tr("Ventana de canción", "Song window"),
                           SvgIconName::Clock, std::move(onWindow)),
          hasPractice(showPractice), hasWindow(showWindow) {
      sectionsButton.setComponentID("timeline-sections");
      for (auto *option : {&sectionsButton}) {
        option->setClickingTogglesState(true);
        option->getProperties().set("timelineOption", true);
        option->setTitle(option->getName());
        addAndMakeVisible(*option);
      }
      midiButton.setComponentID("midi-learn");
      midiButton.setToggleState(midiActive, juce::dontSendNotification);
      addChildComponent(midiButton);
      addChildComponent(settingsButton);
      addAndMakeVisible(learningButton);
      if (hasPractice)
        addAndMakeVisible(practiceButton);
      if (hasWindow) addAndMakeVisible(songWindowButton);
    }

    void paint(juce::Graphics &g) override {
      auto bounds = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(bounds, 8.0f);
      g.setColour(UITheme::cardBorder);
      g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f,
                             1.0f);
    }

    void updateViewSelection(bool sections, bool enabled) {
      sectionsButton.setToggleState(sections, juce::dontSendNotification);
      for (auto *option : {&sectionsButton}) {
        option->setEnabled(enabled);
        option->setDescription(option->getName() + (option->getToggleState()
            ? L10n::tr(" — seleccionada", " — selected") : juce::String{}));
      }
    }

    void updatePrimaryActions(bool showMidi, bool showSettings, bool active, bool showPractice, bool showWindow, bool showTimeline) {
      const bool layoutChanged = getWidth() == 0 || midiButton.isVisible() != showMidi || settingsButton.isVisible() != showSettings ||
                                 hasPractice != showPractice || hasWindow != showWindow || sectionsButton.isVisible() != showTimeline;
      midiButton.setToggleState(active, juce::dontSendNotification);
      if (!layoutChanged) return;
      hasPractice = showPractice; hasWindow = showWindow;
      practiceButton.setVisible(hasPractice); songWindowButton.setVisible(hasWindow);
      midiButton.setVisible(showMidi); settingsButton.setVisible(showSettings);
      sectionsButton.setVisible(showTimeline);
      const int rows = 1 + (showTimeline ? 1 : 0) + (showMidi ? 1 : 0) + (showSettings ? 1 : 0) + (hasPractice ? 1 : 0) + (hasWindow ? 1 : 0);
      setSize(getWidth() > 0 ? getWidth() : 300, rows * 48 + 8);
      resized();
    }

    void resized() override {
      auto area = getLocalBounds().reduced(10, 6);
      constexpr int rowHeight = 44;
      for (auto *button : {&sectionsButton, &midiButton, &settingsButton}) {
        if (button->isVisible()) {
          button->setBounds(area.removeFromTop(rowHeight)); area.removeFromTop(4);
        } else button->setBounds({});
      }
      if (hasPractice) {
        practiceButton.setBounds(area.removeFromTop(rowHeight));
        area.removeFromTop(4);
      }
      learningButton.setBounds(area.removeFromTop(rowHeight));
      if (hasWindow) {
        area.removeFromTop(4);
        songWindowButton.setBounds(area.removeFromTop(rowHeight));
      }
    }

  private:
    ActionButton sectionsButton;
    ActionButton midiButton;
    ActionButton settingsButton;
    ActionButton practiceButton;
    ActionButton learningButton;
    ActionButton songWindowButton;
    bool hasPractice = false;
    bool hasWindow = false;
  };

  const auto safe = juce::Component::SafePointer<TopBar>(this);
  auto panel = std::make_unique<MorePopover>(
      [safe] { if (safe) safe->selectTimelineView(!(safe->liveModeState && safe->liveModeState())); },
      midiLearnActive, [safe] { if (safe) safe->midiLearnButton.triggerClick(); },
      [safe] { if (safe) safe->openSettingsPanel(); }, [safe] {
        if (safe && safe->onPhonePractice) safe->onPhonePractice();
      }, minimalPhoneMode && onPhonePractice != nullptr, [safe] {
        if (safe && safe->onLearningRequested) safe->onLearningRequested();
      }, [safe] { if (safe) safe->showSongWindowPopover(); }, !timeDisplay.isVisible());
  const auto panelSafe = juce::Component::SafePointer<MorePopover>(panel.get());
  const bool desktopMenu = !useTouchOptimisedControls() && !minimalPhoneMode;
  refreshMorePopover = [safe, panelSafe, desktopMenu, initialized = false]() mutable {
    if (safe && panelSafe)
      panelSafe->updateViewSelection(safe->liveModeState && safe->liveModeState(),
          safe->timelineControlsAvailable() && safe->onLiveModeToggle && (!safe->liveModeEnabled || safe->liveModeEnabled()));
    const bool timelineVisibilityChanged = safe && panelSafe &&
        panelSafe->findChildWithID("timeline-sections")->isVisible() != safe->timelineControlsAvailable();
    if (!desktopMenu && initialized && !timelineVisibilityChanged) return;
    if (safe && panelSafe) {
      initialized = true;
      const auto actions = safe->morePrimaryActions();
      panelSafe->updatePrimaryActions(actions.first, actions.second, safe->midiLearnActive,
          safe->timelineControlsAvailable() && safe->minimalPhoneMode && safe->onPhonePractice != nullptr,
          safe->timelineControlsAvailable() && !safe->timeDisplay.isVisible(), safe->timelineControlsAvailable());
    }
  };
  refreshMorePopover();
  return panel;
}

void TopBar::showTabletMorePopover() {
  auto panel = createMorePopoverContent();
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto buttonAnchor =
      parentComp->getLocalArea(this, tabletMoreButton.getBounds());
  const juce::Rectangle<int> anchor(buttonAnchor.getX() - 20,
                                    buttonAnchor.getBottom() + 4,
                                    buttonAnchor.getWidth() + 20, 2);
  presentActionPopover(std::move(panel), anchor, parentComp);
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
  if (useTouchOptimisedControls()) {
    displayFont = 21.0f;
    keyFont = 17.0f;
    usageFont = 14.0f;
    songTitleFontSize = 17.0f;
    songArtistFontSize = 12.5f;
  }
  if (useTouchOptimisedControls() && !minimalPhoneMode &&
      responsiveProfile == ResponsiveLayoutProfile::Compact)
    displayFont = 14.0f;
  if (minimalPhoneMode)
    displayFont = 14.0f;
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
  if (refreshMorePopover) refreshMorePopover();
  refreshTimelineMenu();
  refreshFromProviders();
  if (songTextOverflows && isShowing())
    repaint(songInfoBounds);

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

  const bool metronomeEnabled = session.isMetronomeEnabled();
  if (metronomeEnabled) {
    const double bpm = session.getMetronomeBpm();
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
  metroToggleButton.setToggleState(session.isMetronomeEnabled(),
                                   juce::dontSendNotification);
  metroToggleButton.setBadgeText(session.isMetronomeEnabled() ? "ON"
                                                               : juce::String{});
  metroToggleButton.setPulseAmount(
      session.isMetronomeEnabled() ? beatFlash : 0.0f,
      lastBeatWasDownbeat);

  if (midiLearnActive)
    repaint();
}

void TopBar::updateBpmDisplay() {
  const int bpm =
      static_cast<int>(std::round(session.getPlaybackTempoBpm()));
  setButtonTextIfChanged(bpmSegmentButton, bpm > 0 ? juce::String(bpm) : "120");
}

void TopBar::applyTimeSignature(const juce::String &sigText) {
  timeSignature = sigText.trim();
  if (timeSignature.isEmpty())
    timeSignature = "4/4";
  beatsPerBar = timeSignatureBeatsPerBar(timeSignature);
  session.setMetronomeBeatsPerBar(beatsPerBar);
  beatCounter = 0.0;
  lastBeatIndex = -1;
  lastBeatWasDownbeat = true;
}

void TopBar::setBpmAbsolute(double bpm) {
  if (bpm <= 0.0)
    return;
  session.setPlaybackTempoBpm(bpm);
  const auto songId = songIdProvider ? songIdProvider() : juce::String{};
  if (!session.isRemote() && songId.isNotEmpty())
    SongLibraryStore::updateTempoBpmForActiveSetlist(
        songId, session.getPlaybackTempoBpm());
  updateBpmDisplay();
}

void TopBar::adjustBpm(double delta) {
  const auto mods = juce::ModifierKeys::getCurrentModifiers();
  const double step = mods.isShiftDown() ? (delta < 0.0 ? -5.0 : 5.0) : delta;
  setBpmAbsolute(session.getPlaybackTempoBpm() + step);
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
    AdaptiveComboBox timeSigBox;
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
      applyComboBoxTheme(timeSigBox, UITheme::cardBgHover,
                         UITheme::cardBorder.withAlpha(0.65f));
      applyTextEditorTheme(bpmEditor, UITheme::cardBgHover,
                           UITheme::cardBorder.withAlpha(0.7f));
      bpmEditor.setJustification(juce::Justification::centred);
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
      [this] { return session.getPlaybackTempoBpm(); }, timeSignature);
  panel->setSize(230, 92);
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor = parentComp->getLocalArea(this, bpmSegmentButton.getBounds());
  presentActionPopover(std::move(panel), anchor, parentComp);
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

  if (!session.hasLoadedSong())
    return;

  auto panel = std::make_unique<KeyPopover>(
      activeKeySemitone, [this](int semitone) {
        activeKeySemitone = normalizeSemitone(semitone);
        activeKeyLabel = noteLabelForSemitone(activeKeySemitone);
        activePitchDelta = semitoneDelta(baseKeySemitone, activeKeySemitone);
        session.setPitchSemiTones(activePitchDelta);
        const auto songId = songIdProvider ? songIdProvider() : juce::String{};
        if (!session.isRemote() && songId.isNotEmpty())
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
  presentActionPopover(std::move(panel), anchor, parentComp);
}

void TopBar::showSongWindowPopover() {
  if (!timelineControlsAvailable()) return;
  class SongWindowPopover final : public juce::Component {
  public:
    SongWindowPopover(PlaybackSession &sessionRef,
                      const bool customArrangement)
        : session(sessionRef),
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
      const double start = session.getSongStartSeconds();
      const double end = session.getSongEndSeconds();
      startValue.setText(formatTimeSeconds(start),
                         juce::dontSendNotification);
      endValue.setText(formatTimeSeconds(end),
                       juce::dontSendNotification);
      startEditor.setText(formatTimeSeconds(start),
                          juce::dontSendNotification);
      endEditor.setText(formatTimeSeconds(end),
                        juce::dontSendNotification);
      const int fadeSecs =
          static_cast<int>(std::round(session.getSongFadeOutSeconds()));
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
        applyTextEditorTheme(*editor, UITheme::cardBgHover,
                             UITheme::cardBorder.withAlpha(0.65f));
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
    PlaybackSession &session;
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
      const double length = juce::jmax(0.0, session.getSongLengthSeconds());
      const double fade = session.getSongFadeOutSeconds();
      const double windowLength = juce::jmax(0.0, end - start);

      startPlus.setEnabled(start > epsilon);
      startMinus.setEnabled(start < end - epsilon);
      endMinus.setEnabled(end > start + epsilon);
      endPlus.setEnabled(end < length - epsilon);
      fadeMinus.setEnabled(fade > epsilon);
      fadePlus.setEnabled(fade < windowLength - epsilon);
    }

    void adjustStart(double delta) {
      session.setSongStartSeconds(session.getSongStartSeconds() + delta);
      refreshValues();
    }

    void adjustEnd(double delta) {
      session.setSongEndSeconds(session.getSongEndSeconds() + delta);
      refreshValues();
    }

    void adjustFade(double delta) {
      session.setSongFadeOutSeconds(session.getSongFadeOutSeconds() + delta);
      refreshValues();
    }

    void submitStartEditor() {
      const double secs = parseTimeText(startEditor.getText());
      if (secs >= 0.0)
        session.setSongStartSeconds(secs);
      refreshValues();
    }

    void submitEndEditor() {
      const double secs = parseTimeText(endEditor.getText());
      if (secs >= 0.0)
        session.setSongEndSeconds(secs);
      refreshValues();
    }
  };

  if (!session.hasLoadedSong())
    return;
  const bool customArrangementActive = session.isCustomArrangementActive();
  auto panel =
      std::make_unique<SongWindowPopover>(session, customArrangementActive);
  panel->setSize(230, customArrangementActive ? 156 : 128);
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto timeAnchor =
      !timeDisplay.isVisible() ? tabletMoreButton.getBounds() : timeDisplay.getBounds();
  const auto anchor = parentComp->getLocalArea(this, timeAnchor);
  presentActionPopover(std::move(panel), anchor, parentComp);
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
  setLabelTextIfChanged(
      timeDisplay,
      !timelineControlsAvailable() ? formatTimeSeconds(pos)
          : (minimalPhoneMode ||
             (useTouchOptimisedControls() &&
              responsiveProfile == ResponsiveLayoutProfile::Compact))
                ? formatTimeSeconds(pos) + " /\n" + formatTimeSeconds(len)
                : formatTimeSeconds(pos) + " / " + formatTimeSeconds(len));

  updateBpmDisplay();

  const bool songLoaded = session.hasLoadedSong();
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
      lastArtworkSource.clear();
      lastArtworkModifiedTime = {};
      lastArtworkSize = -1;
      lastArtworkStatAtMs = 0;
      if (songArtwork.isValid()) {
        songArtwork = {};
        songCardNeedsRepaint = true;
      }
      lastKeyToken.clear();
      baseKeySemitone = 0;
      activeKeySemitone = 0;
      activeKeyLabel = L10n::tr("Tono", "Key");

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
            : session.getLoadedSongTitle().trim();
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
          juce::jlimit(-12, 12, session.getPitchSemiTones());
      activeKeySemitone = normalizeSemitone(baseKeySemitone + activePitchDelta);
      activeKeyLabel = noteLabelForSemitone(activeKeySemitone);

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
    // The processor/session is authoritative, including changes from another
    // controller. A repaint must not write yesterday's label back into audio.
    activePitchDelta = session.getPitchSemiTones();
    activeKeySemitone = normalizeSemitone(baseKeySemitone + activePitchDelta);
    activeKeyLabel = noteLabelForSemitone(activeKeySemitone);
    setLabelTextIfChanged(keyDisplay, activeKeyLabel);

    assignSongText(songTitleText,
                   songTitle.isNotEmpty()
                       ? songTitle
                       : L10n::tr("Canción sin título", "Untitled Song"));
    assignSongText(songArtistText, songArtist);
    assignSongAccent(songAccentColour, colourfulAccentFromStableId(activeSongId));

    if (loadedSong != nullptr) {
      const auto artworkPath = loadedSong->artworkPath.trim();
      const auto artworkSource = loadedSong->artworkSource.trim();
      const juce::File artworkFile(artworkPath);
      const bool referenceChanged = artworkPath != lastArtworkPath ||
                                    artworkSource != lastArtworkSource;
      auto modified = lastArtworkModifiedTime;
      auto size = lastArtworkSize;
      const auto now = juce::Time::getMillisecondCounter();
      if (referenceChanged || now - lastArtworkStatAtMs >= 500) {
        lastArtworkStatAtMs = now;
        const bool exists = artworkPath.isNotEmpty() && artworkFile.existsAsFile();
        modified = exists ? artworkFile.getLastModificationTime() : juce::Time{};
        size = exists ? artworkFile.getSize() : -1;
      }
      if (referenceChanged ||
          modified != lastArtworkModifiedTime || size != lastArtworkSize) {
        lastArtworkPath = artworkPath;
        lastArtworkSource = artworkSource;
        lastArtworkModifiedTime = modified;
        lastArtworkSize = size;
        songArtwork = {};
        songCardNeedsRepaint = true;
        if (artworkFile.existsAsFile())
          songArtwork = juce::ImageFileFormat::loadFrom(artworkFile);
      }
    } else {
      if (lastArtworkPath.isNotEmpty() || songArtwork.isValid()) {
        lastArtworkPath.clear();
        lastArtworkSource.clear();
        lastArtworkModifiedTime = {};
        lastArtworkSize = -1;
        lastArtworkStatAtMs = 0;
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
  const bool fadeOutActive = session.isFadeActive();
  fadeButton.setIcon(fadeOutActive ? SvgIconName::FadeIn
                                   : SvgIconName::FadeOut);
  if (fadeButton.getToggleState() != fadeOutActive)
    fadeButton.setToggleState(fadeOutActive, juce::dontSendNotification);
  if (!midiLearnActive)
    fadeButton.setBadgeText(fadeOutActive ? "ON" : juce::String{});
  const bool metronomeEnabled = session.isMetronomeEnabled();
  if (metroToggleButton.getToggleState() != metronomeEnabled)
    metroToggleButton.setToggleState(metronomeEnabled,
                                     juce::dontSendNotification);
  if (!midiLearnActive)
    metroToggleButton.setBadgeText(metronomeEnabled ? "ON" : juce::String{});

  if (songCardNeedsRepaint) {
    songTextScrollStartMs = juce::Time::getMillisecondCounterHiRes();
    const auto previousBounds = songInfoBounds;
    resized();
    repaint(previousBounds.getUnion(songInfoBounds).expanded(2));
  }
}
