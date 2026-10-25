#include "PracticePanel.h"
#include "TimelineRulerStyle.h"

#include "../../data/Localization.h"
#include "../UiCommon.h"

#include <algorithm>
#include <cmath>

namespace {
double waveformLength(const PracticePanel::Sources &sources) {
  const double length = sources.lengthSeconds ? sources.lengthSeconds() : 0.0;
  return std::isfinite(length) ? juce::jmax(0.0, length) : 0.0;
}

void paintInsetWaveformFrame(juce::Graphics &g, juce::Rectangle<float> card) {
  const juce::Graphics::ScopedSaveState saved(g);
  juce::Path outline;
  outline.addRoundedRectangle(card, 16.0f);
  g.reduceClipRegion(outline);
  const float depth = juce::jmin(9.0f, juce::jmin(card.getWidth(), card.getHeight()) * 0.1f);
  const auto shade = [&](juce::Rectangle<float> edge, juce::Point<float> from,
                         juce::Point<float> to, float opacity) {
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(opacity), from,
        juce::Colours::transparentBlack, to, false));
    g.fillRect(edge);
  };
  // Four narrow gradients give the stationary frame depth without blurring
  // or rebuilding an offscreen image as the waveform moves beneath it.
  shade(card.withWidth(depth), {card.getX(), card.getCentreY()},
        {card.getX() + depth, card.getCentreY()}, 0.24f);
  shade(card.withLeft(card.getRight() - depth), {card.getRight(), card.getCentreY()},
        {card.getRight() - depth, card.getCentreY()}, 0.24f);
  shade(card.withHeight(depth), {card.getCentreX(), card.getY()},
        {card.getCentreX(), card.getY() + depth}, 0.28f);
  shade(card.withTop(card.getBottom() - depth), {card.getCentreX(), card.getBottom()},
        {card.getCentreX(), card.getBottom() - depth}, 0.12f);
  g.setColour(UITheme::cardBorder);
  g.drawRoundedRectangle(card.reduced(0.5f), 16.0f, 1.0f);
  g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.18f),
      card.getCentreX(), card.getY(), juce::Colours::white.withAlpha(0.07f),
      card.getCentreX(), card.getBottom(), false));
  g.drawRoundedRectangle(card.reduced(1.0f), 15.0f, 1.0f);
}

// Draw before the opaque card, so every inward pixel is covered by the
// unchanged card/waveform. Fixed vector strokes need no blur buffer, animation,
// timer, or reduced-motion exception; the halo is always static.
void paintListeningGlow(juce::Graphics &g, juce::Rectangle<float> card, int mode) {
  if (mode != 1 && mode != 2)
    return;
  const juce::Graphics::ScopedSaveState saved(g);
  const float radius = 16.0f;
  const float left = card.getX(), right = card.getRight();
  const float edge = mode == 1 ? card.getY() : card.getBottom();
  const float direction = mode == 1 ? 1.0f : -1.0f;
  const float middle = card.getCentreY();
  juce::Path outline;
  outline.startNewSubPath(left, middle);
  outline.lineTo(left, edge + direction * radius);
  outline.quadraticTo(left, edge, left + radius, edge);
  outline.lineTo(right - radius, edge);
  outline.quadraticTo(right, edge, right, edge + direction * radius);
  outline.lineTo(right, middle);
  const auto stroke = [&](float width, float opacity) {
    g.setGradientFill(juce::ColourGradient(UITheme::accent.withAlpha(opacity),
        card.getCentreX(), edge, UITheme::accent.withAlpha(0.0f),
        card.getCentreX(), middle, false));
    g.strokePath(outline, juce::PathStrokeType(width,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  };
  stroke(10.0f, 0.035f);
  stroke(6.0f, 0.065f);
  stroke(3.0f, 0.16f);
  stroke(1.5f, 0.70f);
}

juce::String loopErrorMessage(const juce::String &error) {
  if (error == "discontinuous-arrangement")
    return L10n::tr("A–B cruza un corte.\nElige un tramo continuo.",
                    "A–B crosses a cut.\nChoose a continuous part.");
  if (error == "too-short")
    return L10n::tr("Separa un poco más las marcas A y B.", "Move A and B a little farther apart.");
  if (error == "buffer-not-ready")
    return L10n::tr("Loop detenido.\nPulsa Play para continuar.",
                    "Loop stopped.\nPress Play to continue.");
  return L10n::tr("No se pudo activar este loop.", "Could not activate this loop.");
}
} // namespace

PracticePanel::PracticePanel(Sources nextSources)
    : sources(std::move(nextSources)),
      closeButton("practiceClose", IconGlyph::Close),
      playPauseButton("practicePlay", SvgIconName::Play, true),
      stopButton("practiceStop", SvgIconName::Square),
      guideButton(L10n::tr("Guía", "Guide")),
      metronomeButton(L10n::tr("Metrónomo", "Metronome")),
      listeningHelpButton("?"),
      loopStartButton("A"), loopEndButton("B"),
      loopToggleButton("practiceLoop", SvgIconName::Repeat),
      loopClearButton("practiceLoopClear", SvgIconName::Trash) {
  setOpaque(true);
  setInterceptsMouseClicks(true, true);
  setWantsKeyboardFocus(true);

  closeButton.setTitle(L10n::tr("Cerrar ensayo", "Close practice"));
  playPauseButton.setTitle(L10n::tr("Reproducir o pausar", "Play or pause"));
  stopButton.setTitle(L10n::tr("Detener reproducción", "Stop playback"));
  listeningHelpButton.setTitle(L10n::tr("Abrir recorrido de Ensayo",
                                       "Open Practice walkthrough"));
  listeningHelpButton.setTooltip(L10n::tr("Aprende Ensayo paso a paso", "Learn Practice step by step"));
  for (auto *button : {&guideButton, &metronomeButton}) {
    // Native Button accessibility exposes a checkable state, while clicks wait
    // for the session's actual state instead of toggling optimistically.
    button->setToggleable(true);
    button->setClickingTogglesState(false);
    button->setWantsKeyboardFocus(true);
  }
  guideButton.setExplicitFocusOrder(4);
  metronomeButton.setExplicitFocusOrder(5);
  listeningHelpButton.setExplicitFocusOrder(6);
  closeButton.setSubtleHighlight(true);
  loopStartButton.setTitle(L10n::tr("Marcar A: inicio del loop", "Mark A: loop start"));
  loopEndButton.setTitle(L10n::tr("Marcar B: final e iniciar loop", "Mark B: end and start looping"));
  loopToggleButton.setTitle(L10n::tr("Activar o desactivar loop A–B", "Enable or disable A–B loop"));
  loopClearButton.setTitle(L10n::tr("Borrar región A–B", "Clear A–B region"));
  loopClearButton.setTooltip(L10n::tr("Borra las marcas A y B y desactiva su repetición.",
                                     "Clear A and B and stop repeating the region."));
  for (auto *button : {static_cast<juce::Button *>(&closeButton),
                       static_cast<juce::Button *>(&playPauseButton),
                       static_cast<juce::Button *>(&stopButton),
                       static_cast<juce::Button *>(&guideButton),
                       static_cast<juce::Button *>(&metronomeButton),
                       static_cast<juce::Button *>(&listeningHelpButton),
                       static_cast<juce::Button *>(&loopStartButton),
                       static_cast<juce::Button *>(&loopEndButton),
                       static_cast<juce::Button *>(&loopToggleButton),
                       static_cast<juce::Button *>(&loopClearButton)}) {
    addAndMakeVisible(*button);
  }
  addAndMakeVisible(pitchSelector);
  addAndMakeVisible(tempoSelector);
  addAndMakeVisible(instrumentSelector);
  instrumentSelector.onValueChange = [this] { selectInstrumentFromWheel(); };
  instrumentSelector.onPointerEvent = [this](const juce::MouseEvent &event,
      PracticeInstrumentSelector::PointerPhase phase) {
    return routeInstrumentPointer(event, phase);
  };
  closeButton.onClick = [this] {
    if (sources.close)
      sources.close();
  };
  playPauseButton.onClick = [this] {
    cancelScrub(false);
    pendingSeek = false;
    handoffOffsetSeconds = 0.0;
    if (sources.playPause)
      sources.playPause();
  };
  stopButton.onClick = [this] {
    cancelScrub(false);
    pendingSeek = false;
    handoffOffsetSeconds = 0.0;
    waveformPressed = false;
    barFeedbackUntilMs = 0;
    accumulatedBarSteps = 0;
    if (sources.stop)
      sources.stop();
  };
  guideButton.onClick = [this] { toggleAccompaniment(true); };
  metronomeButton.onClick = [this] { toggleAccompaniment(false); };
  listeningHelpButton.onClick = [this] {
    if (!listeningHelpButton.isGestureRelease())
      showListeningHelp();
  };
  pitchSelector.onValueChange = [this] {
    const auto songId = sources.songId ? sources.songId() : juce::String{};
    const bool remote = sources.isRemote && sources.isRemote();
    if (songId != displayedSongId || remote != displayedWasRemote) {
      auto safe = juce::Component::SafePointer<PracticePanel>(this);
      refreshWaveform();
      if (safe != nullptr)
        updatePracticeControls();
      return;
    }
    const int current = pitchPending ? requestedPitch :
        sources.pitchSemitones ? sources.pitchSemitones() : 0;
    changePitch(juce::roundToInt(pitchSelector.getValue()) - current);
  };
  tempoSelector.onValueChange = [this] {
    const auto songId = sources.songId ? sources.songId() : juce::String{};
    const bool remote = sources.isRemote && sources.isRemote();
    if (songId != displayedSongId || remote != displayedWasRemote) {
      auto safe = juce::Component::SafePointer<PracticePanel>(this);
      refreshWaveform();
      if (safe != nullptr)
        updatePracticeControls();
      return;
    }
    changeTempo(tempoSelector.getValue());
  };
  loopStartButton.onClick = [this] { markLoopStart(); };
  loopEndButton.onClick = [this] { markLoopEnd(); };
  loopToggleButton.onClick = [this] { togglePracticeLoop(); };
  loopClearButton.onClick = [this] { clearPracticeRegion(); };
  refreshWaveform();
  updatePracticeControls();
#if !JUCE_ANDROID
  playbackVBlankAttachment = juce::VBlankAttachment(
      this, [this](double timestamp) { updateAnimationFrame(timestamp, true); });
  startTimerHz(30);
#else
  startTimerHz(60);
#endif
}

PracticePanel::~PracticePanel() {
  stopTimer();
  playbackVBlankAttachment = {};
  // Invalidate before calling providers: a provider may synchronously flush a
  // retained delivery while its scrub/loop/listening state is being released.
  cancelWaveformRequest();
  cancelScrub(true);
  if (sources.releaseLoop && sources.ownsLoop && sources.ownsLoop())
    sources.releaseLoop();
  if (listeningActivatedHere && sources.finishListening)
    sources.finishListening();
}

juce::Rectangle<int> PracticePanel::safeContentBounds() const {
  auto bounds = getLocalBounds();
  if (const auto *display = juce::Desktop::getInstance().getDisplays()
                                .getDisplayForRect(getScreenBounds()))
    bounds = display->safeAreaInsets.subtractedFrom(bounds);
  return bounds.reduced(18, 12);
}

PracticePanel::WaveformLayout PracticePanel::getWaveformLayout() const {
  WaveformLayout layout;
  auto area = waveformBounds.reduced(0, 20);
  area.removeFromTop(54);
  layout.hints = area.removeFromTop(30).reduced(18, 0);
  layout.ruler = area.removeFromTop(24);
  layout.help = area.removeFromBottom(44).reduced(18, 0);
  // Reserve matching gutters on BOTH sides, independent of icon visibility.
  // The hint centre is the card centre, never the remaining area left of '?'.
  layout.helpText = layout.help.reduced(48, 0);
  layout.times = area.removeFromBottom(28).reduced(18, 0);
  layout.plot = area.reduced(0, juce::jmin(18, area.getHeight() / 8));
  return layout;
}

void PracticePanel::resized() {
  auto area = safeContentBounds();
  auto header = area.removeFromTop(64);
  closeButton.setBounds(header.removeFromRight(44).withSizeKeepingCentre(44, 44));

  auto tools = area.removeFromTop(74);
  tools.removeFromTop(22);
  tools.setHeight(44);
  pitchSelector.setBounds(tools.removeFromLeft((tools.getWidth() - 12) / 2));
  tools.removeFromLeft(12);
  tempoSelector.setBounds(tools);

  auto dock = area.removeFromBottom(120);
  auto accompanimentRow = dock.removeFromTop(44);
  const int accompanimentGap = 12;
  const int accompanimentWidth = (accompanimentRow.getWidth() - accompanimentGap) / 2;
  guideButton.setBounds(accompanimentRow.removeFromLeft(accompanimentWidth));
  accompanimentRow.removeFromLeft(accompanimentGap);
  metronomeButton.setBounds(accompanimentRow.removeFromLeft(accompanimentWidth));
  dock.removeFromTop(8);
  auto controls = dock;
  const bool compact = controls.getWidth() < 328;
  const int gap = compact ? 4 : 8;
  const int width = compact ? 44 : 56;
  auto buttons = juce::Rectangle<int>(width * 2 + 44 * 4 + gap * 5, 56)
                     .withCentre(controls.getCentre());
  playPauseButton.setBounds(buttons.removeFromLeft(width).withSizeKeepingCentre(width, width));
  buttons.removeFromLeft(gap);
  stopButton.setBounds(buttons.removeFromLeft(width).withSizeKeepingCentre(width, width));
  for (auto *button : {static_cast<juce::Button *>(&loopStartButton),
                       static_cast<juce::Button *>(&loopEndButton),
                       static_cast<juce::Button *>(&loopToggleButton),
                       static_cast<juce::Button *>(&loopClearButton)}) {
    buttons.removeFromLeft(gap);
    button->setBounds(buttons.removeFromLeft(44).withSizeKeepingCentre(44, 44));
  }

  area.removeFromTop(8);
  area.removeFromBottom(18);
  waveformBounds = area;
  instrumentHeaderBounds = waveformBounds.reduced(18, 12).removeFromTop(54);
  const auto selectorBounds = instrumentHeaderBounds.withTrimmedTop(10);
  if (instrumentSelector.getBounds() != selectorBounds)
    cancelInstrumentGesture();
  instrumentSelector.setBounds(selectorBounds);
  listeningHelpButton.setBounds(getWaveformLayout().help.removeFromRight(44));
}

void PracticePanel::GestureAwareButton::paintButton(juce::Graphics &g,
                                                    bool over, bool down) {
  auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  const auto fill = getToggleState() ? UITheme::accentMuted
                   : down ? UITheme::cardBgHover : UITheme::cardBg;
  g.setColour(over ? fill.brighter(0.04f) : fill);
  g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);
  g.setColour(UITheme::cardBorder.withAlpha(0.65f));
  g.drawRoundedRectangle(bounds, bounds.getHeight() * 0.5f, 1.0f);
  g.setColour(UITheme::textPrimary.withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
  const bool twoLines = getButtonText().containsChar('\n');
  g.setFont(makeAppFont(twoLines ? 11.0f : 13.0f, AppFontWeight::Medium));
  g.drawFittedText(getButtonText(), getLocalBounds().reduced(twoLines ? 3 : 8, 2),
                   juce::Justification::centred, twoLines ? 2 : 1);
}

void PracticePanel::AccompanimentButton::paintButton(juce::Graphics &g,
                                                       bool over, bool down) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  auto fill = getToggleState() ? UITheme::panelDark.interpolatedWith(UITheme::accent, 0.24f)
                              : UITheme::panelDark;
  if (isEnabled() && (over || down))
    fill = fill.brighter(down ? 0.10f : 0.04f);
  g.setColour(fill);
  g.fillRoundedRectangle(bounds, 12.0f);
  g.setColour(hasKeyboardFocus(true) ? UITheme::accent
      : getToggleState() ? UITheme::accent.withAlpha(0.65f)
                         : UITheme::textSecondary.withAlpha(0.22f));
  g.drawRoundedRectangle(bounds, 12.0f, hasKeyboardFocus(true) ? 2.0f : 1.0f);
  // The check/minus and inline ON/OFF remain legible without colour. Keep the
  // authoritative toggle state while a request is pending; availability and
  // confirmation details remain in the accessible description and tooltip.
  const auto ink = UITheme::textPrimary.withMultipliedAlpha(isEnabled() || pending ? 1.0f : 0.5f);
  auto text = getLocalBounds().reduced(10, 3);
  const auto icon = text.removeFromLeft(18).toFloat().withSizeKeepingCentre(12.0f, 12.0f);
  drawIconGlyph(g, icon, getToggleState() ? IconGlyph::Check : IconGlyph::Remove, ink, 1.5f);
  text.removeFromLeft(4);
  g.setColour(ink);
  g.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  g.drawFittedText(getButtonText() + " • " + (getToggleState() ? "ON" : "OFF"),
      text, juce::Justification::centredLeft, 1, 0.75f);
}

void PracticePanel::updateAccompanimentControls() {
  const auto now = juce::Time::getMillisecondCounter();
  const auto song = sources.songId ? sources.songId() : juce::String{};
  const bool remote = sources.isRemote && sources.isRemote();
  const bool canControl = sources.canControl && sources.canControl();
  const auto update = [&](AccompanimentButton &button, AccompanimentControl &control,
      const std::function<AccompanimentState()> &read, bool writable) {
    auto state = read ? read() : AccompanimentState{};
    const bool available = canControl && state.available && writable;
    const bool contextMatches = available && state.identity == control.requestedIdentity &&
        song == control.requestedSongId && remote == control.requestedRemote;
    if (control.request.reconcile(state.enabled, contextMatches, now))
      showPracticeFeedback(button.getButtonText() +
          L10n::tr(": cambio sin confirmar. Intenta de nuevo.", ": change not confirmed. Try again."));
    control.state = std::move(state);
    if (button.pending != control.request.pending) {
      button.pending = control.request.pending;
      button.repaint();
    }
    const bool checked = available && control.state.enabled;
    if (button.getToggleState() != checked) {
      // JUCE may ignore toggle state changes on a disabled button. Sync the
      // authoritative state first, including the first acknowledgement frame.
      button.setEnabled(true);
      button.setToggleState(checked, juce::dontSendNotification);
    }
    button.setEnabled(available && !control.request.pending);
    const auto reason = !canControl ? L10n::tr("Sin conexión o reproducción disponible.",
                                               "No connection or playback available.")
        : control.state.unavailableReason.isNotEmpty() ? control.state.unavailableReason
        : L10n::tr("Esta canción no tiene esta fuente disponible.",
                   "This song has no available source for this control.");
    const auto description = button.getButtonText() + ": " +
        (!available ? reason : control.request.pending
            ? L10n::tr("Confirmando cambio; estado actual: ", "Confirming change; current state: ") +
                (control.state.enabled ? L10n::tr("encendido", "on") : L10n::tr("apagado", "off"))
            : control.state.enabled ? L10n::tr("Encendido", "On") : L10n::tr("Apagado", "Off"));
    if (button.getDescription() != description) {
      button.setTitle(description);
      button.setDescription(description);
      button.setTooltip(description);
    }
  };
  update(guideButton, guideControl, sources.guideState, static_cast<bool>(sources.setGuideEnabled));
  update(metronomeButton, metronomeControl, sources.metronomeState,
         static_cast<bool>(sources.setMetronomeEnabled));
}

void PracticePanel::toggleAccompaniment(bool guide) {
  auto &button = guide ? guideButton : metronomeButton;
  auto &control = guide ? guideControl : metronomeControl;
  if (button.gestureRelease)
    return;
  const auto identityBeforeClick = control.state.identity;
  const auto song = sources.songId ? sources.songId() : juce::String{};
  const bool remote = sources.isRemote && sources.isRemote();
  // A source/song change may arrive between UI ticks. Refresh without acting
  // on the replacement source from a tap intended for the old one.
  updateAccompanimentControls();
  if (!button.isEnabled() || identityBeforeClick != control.state.identity ||
      song != displayedSongId || remote != displayedWasRemote)
    return;
  control.requestedIdentity = control.state.identity;
  control.requestedSongId = song;
  control.requestedRemote = remote;
  control.request.begin(!control.state.enabled, juce::Time::getMillisecondCounter());
  const auto write = guide ? sources.setGuideEnabled : sources.setMetronomeEnabled;
  const bool desired = control.request.requested;
  auto safe = juce::Component::SafePointer<PracticePanel>(this);
  write(desired);
  if (safe != nullptr)
    updateAccompanimentControls();
}

int PracticePanel::getSourceDisplayListeningMode() const {
  return sources.displayListeningMode ? sources.displayListeningMode()
      : sources.listeningMode ? sources.listeningMode() : 0;
}

juce::String PracticePanel::getSourceDisplayListeningInstrument() const {
  return sources.displayListeningInstrumentId ? sources.displayListeningInstrumentId()
      : sources.listeningInstrumentId ? sources.listeningInstrumentId() : juce::String{};
}

bool PracticePanel::isListeningChangePending() const {
  const int intendedMode = sources.listeningMode ? sources.listeningMode() : 0;
  const auto intendedTarget = sources.listeningInstrumentId
      ? sources.listeningInstrumentId() : juce::String{};
  return intendedMode != getSourceDisplayListeningMode() ||
      (intendedMode != 0 && intendedTarget != getSourceDisplayListeningInstrument());
}

int PracticePanel::getVisibleListeningMode() const {
  if (!sources.canControl || !sources.canControl() || selectedInstrumentId.isEmpty() ||
      getSourceDisplayListeningInstrument() != selectedInstrumentId ||
      (sources.songId ? sources.songId() : juce::String{}) != displayedSongId ||
      (sources.isRemote && sources.isRemote()) != displayedWasRemote)
    return 0;
  const int mode = getSourceDisplayListeningMode();
  return mode == 1 || mode == 2 ? mode : 0;
}

juce::String PracticePanel::listeningContextText() const {
  const int mode = getSourceDisplayListeningMode();
  if (mode == 0)
    return L10n::tr("Mezcla normal", "Normal mix");
  const auto target = getSourceDisplayListeningInstrument();
  const auto found = std::find_if(instruments.begin(), instruments.end(),
      [&target](const Instrument &instrument) { return instrument.id == target; });
  const auto name = found != instruments.end() ? found->name
      : L10n::tr("otro instrumento", "another instrument");
  return (mode == 1 ? L10n::tr("Enfoque: ", "Focus: ") : L10n::tr("Reducido: ", "Reduced: ")) + name;
}

void PracticePanel::restoreListeningMix() {
  if (!sources.canControl || !sources.canControl() || !sources.setListeningMode ||
      (sources.songId ? sources.songId() : juce::String{}) != displayedSongId ||
      (sources.isRemote && sources.isRemote()) != displayedWasRemote)
    return;
  const int mode = sources.listeningMode ? sources.listeningMode() : 0;
  if (mode == 0)
    return;
  listeningActivatedHere = false;
  requestedListeningMode = 0;
  const auto reset = sources.setListeningMode;
  auto safe = juce::Component::SafePointer<PracticePanel>(this);
  reset({}, 0);
  if (safe != nullptr)
    repaint(waveformBounds.expanded(6));
}

void PracticePanel::showListeningHelp() {
  // Help belongs to the editor's existing learning flow. Copy before invoking:
  // opening the guide may synchronously change or destroy this panel.
  const auto show = sources.showHelp;
  if (show)
    show();
}

juce::String PracticePanel::currentPracticeFeedback() const {
  const auto now = juce::Time::getMillisecondCounter();
  if (practiceFeedbackUntilMs != 0 && static_cast<juce::int32>(practiceFeedbackUntilMs - now) > 0)
    return practiceFeedback;
  if (markingLoop)
    return "A " + formatTime(loopStartSeconds) + L10n::tr(" · Pulsa B", " · Tap B");
  if (loopStartSeconds >= 0.0 && loopEndSeconds >= 0.0)
    return (loopToggleButton.getToggleState() ? L10n::tr("Loop · ", "Loop · ") : "A–B · ") +
           formatTime(loopStartSeconds) + "–" + formatTime(loopEndSeconds);
  return {};
}

juce::String PracticePanel::formatTime(double seconds) {
  const int total = juce::jmax(0, static_cast<int>(std::floor(seconds)));
  return juce::String::formatted("%02d:%02d", total / 60, total % 60);
}

double PracticePanel::getMinimumLoopDuration() const {
  const double duration = sources.minimumLoopDuration ? sources.minimumLoopDuration() : 0.25;
  return std::isfinite(duration) ? juce::jmax(0.25, duration) : 0.25;
}

double PracticePanel::getVisibleSpanSeconds() const {
  const double length = sources.lengthSeconds ? sources.lengthSeconds() : 0.0;
  return juce::jmin(juce::jmax(0.0, length), viewport.getSpan());
}

void PracticePanel::showPracticeFeedback(juce::String message) {
  practiceFeedback = std::move(message);
  practiceFeedbackUntilMs = juce::Time::getMillisecondCounter() + 3000;
  setDescription(practiceFeedback);
  repaint(getWaveformLayout().hints);
}

void PracticePanel::changePitch(const int delta) {
  if (!sources.canControl || !sources.canControl() || !sources.setPitchSemitones)
    return;
  const int previous = pitchPending ? requestedPitch :
      sources.pitchSemitones ? sources.pitchSemitones() : 0;
  requestedPitch = juce::jlimit(-12, 12, previous + delta);
  pitchPending = true;
  pitchRequestedAtMs = juce::Time::getMillisecondCounter();
  sources.setPitchSemitones(requestedPitch);
  updatePracticeControls();
}

void PracticePanel::changeTempo(double bpm) {
  if (!sources.canControl || !sources.canControl() || !sources.setTempoBpm || !std::isfinite(bpm))
    return;
  requestedTempo = juce::jlimit(tempoSelector.getMinimum(), tempoSelector.getMaximum(), bpm);
  tempoPending = true;
  tempoRequestedAtMs = juce::Time::getMillisecondCounter();
  sources.setTempoBpm(requestedTempo);
  updatePracticeControls();
}

double PracticePanel::getLoopMarkPosition(bool isEnd) const {
  const double position = clampSeekPosition(getDisplayedPositionSeconds());
  return sources.snapLoopPosition ? sources.snapLoopPosition(position, isEnd) : position;
}

void PracticePanel::clearLoopSelection() {
  loopStartSeconds = loopEndSeconds = -1.0;
  markingLoop = loopActivatedHere = loopPending = false;
  practiceFeedbackUntilMs = 0;
  lastLoopError.clear();
  suppressClearedLoop = false;
  clearedLoopRange = {};
  clearedLoopIdentity.clear();
}

void PracticePanel::releaseOwnedLoop() {
  if (!sources.ownsLoop || !sources.ownsLoop() || !sources.releaseLoop)
    return;
  const auto identity = sources.loopIdentity ? sources.loopIdentity() : juce::String{};
  if (identity.isNotEmpty())
    releasedLoopIdentities.addIfNotAlreadyThere(identity);
  sources.releaseLoop();
}

void PracticePanel::clearPracticeRegion() {
  const auto songId = sources.songId ? sources.songId() : juce::String{};
  const bool remote = sources.isRemote && sources.isRemote();
  if (songId != displayedSongId || remote != displayedWasRemote) {
    auto safe = juce::Component::SafePointer<PracticePanel>(this);
    refreshWaveform();
    if (safe != nullptr)
      updatePracticeControls();
    return;
  }
  if (!sources.canControl || !sources.canControl() || scrubActive || pinchLatched || loopStartSeconds < 0.0)
    return;
  const bool active = sources.isLoopActive && sources.isLoopActive();
  const bool owned = sources.ownsLoop && sources.ownsLoop();
  const auto identity = sources.loopIdentity ? sources.loopIdentity() : juce::String{};
  if (active && !owned && !releasedLoopIdentities.contains(identity))
    return;
  const auto previousRange = juce::Range<double>(loopStartSeconds,
      juce::jmax(loopStartSeconds, loopEndSeconds));
  releaseOwnedLoop();
  clearLoopSelection();
  // Do not redraw the just-deleted region from an older remote snapshot.
  suppressClearedLoop = owned && !previousRange.isEmpty();
  clearedLoopRange = previousRange;
  clearedLoopIdentity = identity;
  updatePracticeControls();
  repaint(waveformBounds);
  repaint(getWaveformLayout().hints);
}

void PracticePanel::markLoopStart() {
  if (!sources.canControl || !sources.canControl() || scrubActive || pinchLatched)
    return;
  const double position = getLoopMarkPosition(false);
  if (!std::isfinite(position)) {
    showPracticeFeedback(L10n::tr("No hay un pulso válido para A en esta ventana.",
                                  "There is no valid pulse for A in this window."));
    return;
  }
  releaseOwnedLoop();
  clearLoopSelection();
  loopStartSeconds = position;
  markingLoop = true;
  if (sources.prepareLoop)
    sources.prepareLoop(position);
  updatePracticeControls();
  repaint(waveformBounds);
  repaint(getWaveformLayout().hints);
}

bool PracticePanel::activateMarkedLoop() {
  if (!sources.canControl || !sources.canControl() || !sources.setLoopRange ||
      loopStartSeconds < 0.0 ||
      loopEndSeconds - loopStartSeconds < getMinimumLoopDuration())
    return false;
  if (!sources.setLoopRange(loopStartSeconds, loopEndSeconds)) {
    showPracticeFeedback(loopErrorMessage(sources.loopError ? sources.loopError() : juce::String{}));
    return false;
  }
  lastLoopError.clear();
  practiceFeedback.clear();
  practiceFeedbackUntilMs = 0;
  loopActivatedHere = true;
  loopPending = true;
  loopRequestedAtMs = juce::Time::getMillisecondCounter();
  pendingSeek = false;
  handoffOffsetSeconds = 0.0;
  playbackClock.clear();
  return true;
}

void PracticePanel::markLoopEnd() {
  if (!markingLoop || !sources.canControl || !sources.canControl() ||
      scrubActive || pinchLatched)
    return;
  const double end = getLoopMarkPosition(true);
  if (!std::isfinite(end) || end - loopStartSeconds < getMinimumLoopDuration()) {
    showPracticeFeedback(L10n::tr("Marca B en un pulso posterior a A.", "Mark B on a pulse after A."));
    return;
  }
  loopEndSeconds = end;
  if (activateMarkedLoop())
    markingLoop = false;
  updatePracticeControls();
  repaint(waveformBounds);
  repaint(getWaveformLayout().hints);
}

void PracticePanel::togglePracticeLoop() {
  if (!sources.canControl || !sources.canControl() || scrubActive || pinchLatched || markingLoop)
    return;
  if (loopActivatedHere && sources.ownsLoop && sources.ownsLoop()) {
    releaseOwnedLoop();
    loopActivatedHere = loopPending = false;
  } else {
    activateMarkedLoop();
  }
  updatePracticeControls();
  repaint(waveformBounds);
  repaint(getWaveformLayout().hints);
}

void PracticePanel::updatePracticeControls() {
  updateAccompanimentControls();
  const double oldStart = loopStartSeconds;
  const double oldEnd = loopEndSeconds;
  const bool oldActive = loopToggleButton.getToggleState();
  const auto now = juce::Time::getMillisecondCounter();
  const bool enabled = sources.canControl && sources.canControl();
  const int actualPitch = sources.pitchSemitones ? sources.pitchSemitones() : 0;
  if (!enabled || (pitchPending && (actualPitch == requestedPitch || now - pitchRequestedAtMs >= 2000)))
    pitchPending = false;
  const int pitch = pitchPending ? requestedPitch : actualPitch;
  pitchSelector.setEnabled(enabled && static_cast<bool>(sources.setPitchSemitones));
  pitchSelector.setOriginalKey(sources.songKey ? sources.songKey() : juce::String{});
  pitchSelector.setSessionPitch(pitch);

  const double originalTempo = sources.songBpm ? sources.songBpm() : 120.0;
  const double actualTempo = sources.tempoBpm ? sources.tempoBpm() : originalTempo;
  if (!enabled || (tempoPending && (std::abs(actualTempo - requestedTempo) < 0.001 ||
                                    now - tempoRequestedAtMs >= 2000)))
    tempoPending = false;
  tempoSelector.setEnabled(enabled && static_cast<bool>(sources.setTempoBpm));
  tempoSelector.setOriginalTempo(originalTempo);
  tempoSelector.setSessionTempo(tempoPending ? requestedTempo : actualTempo);

  const bool active = sources.isLoopActive && sources.isLoopActive();
  const bool owned = sources.ownsLoop && sources.ownsLoop();
  const auto range = sources.loopRange ? sources.loopRange() : juce::Range<double>{};
  const auto identity = sources.loopIdentity ? sources.loopIdentity() : juce::String{};
  // Other snapshots may precede the deleted command's acknowledgement. Keep
  // its identity until a new selection/context instead of forgetting it when
  // an older (or another client's) loop is received.
  const bool isClearedSnapshot = releasedLoopIdentities.contains(identity) || (suppressClearedLoop &&
      (clearedLoopIdentity.isNotEmpty()
          ? identity == clearedLoopIdentity : range == clearedLoopRange));
  const auto error = sources.loopError ? sources.loopError() : juce::String{};
  if (loopPending && sources.loopRequestRejected && sources.loopRequestRejected()) {
    loopPending = loopActivatedHere = false;
    lastLoopError = error;
    showPracticeFeedback(loopErrorMessage(error));
  }
  if (loopActivatedHere && !active && !loopPending &&
      (error == "buffer-not-ready" || error == "too-short") && error != lastLoopError) {
    lastLoopError = error;
    showPracticeFeedback(loopErrorMessage(error));
  }
  if (loopPending && ((active && owned) || now - loopRequestedAtMs >= 2000))
    loopPending = false;
  if (loopActivatedHere && !loopPending && !owned)
    loopActivatedHere = false;
  if (!markingLoop && active && !owned && !loopPending && !range.isEmpty() && !isClearedSnapshot) {
    loopStartSeconds = range.getStart();
    loopEndSeconds = range.getEnd();
  }
  const bool resting = !scrubActive && !pinchLatched;
  const auto syncToggle = [](juce::Button &button, bool selected, bool available) {
    // This JUCE build ignores setToggleState while disabled. Update selection
    // first so deleting a loop also clears its disabled buttons' highlights.
    if (button.getToggleState() != selected) {
      button.setEnabled(true);
      button.setToggleState(selected, juce::dontSendNotification);
    }
    button.setEnabled(available);
  };
  syncToggle(loopStartButton, loopStartSeconds >= 0.0,
      enabled && resting && static_cast<bool>(sources.setLoopRange));
  syncToggle(loopEndButton, !markingLoop && loopEndSeconds >= 0.0,
      enabled && resting && markingLoop &&
          getLoopMarkPosition(true) - loopStartSeconds >= getMinimumLoopDuration());
  const bool canClear = enabled && resting && loopStartSeconds >= 0.0 &&
      (!active || owned || isClearedSnapshot);
  loopClearButton.setEnabled(canClear);
  loopClearButton.setAlpha(canClear ? 1.0f : 0.35f);
  syncToggle(loopToggleButton, !markingLoop && (loopPending ||
      (active && !isClearedSnapshot && std::abs(range.getStart() - loopStartSeconds) < 0.001 &&
       std::abs(range.getEnd() - loopEndSeconds) < 0.001)),
      enabled && resting && !markingLoop &&
          loopEndSeconds - loopStartSeconds >= getMinimumLoopDuration() &&
          (!active || owned || loopPending || isClearedSnapshot));
  if (getDescription() != currentPracticeFeedback())
    setDescription(currentPracticeFeedback());
  if (oldStart != loopStartSeconds || oldEnd != loopEndSeconds || oldActive != loopToggleButton.getToggleState()) {
    repaint(waveformBounds);
    repaint(getWaveformLayout().hints);
  }
}

void PracticePanel::applyZoom(const double scaleFactor) {
  viewport.setLength(sources.lengthSeconds ? sources.lengthSeconds() : 0.0);
  if (viewport.zoomBy(scaleFactor))
    repaint(waveformBounds);
}

bool PracticePanel::handleTouchDown(const int index, const juce::Point<float> position) {
  if (!waveformBounds.toFloat().contains(position))
    return false;
  touchPositions[index] = position;
  if (touchPositions.size() < 2)
    return pinchLatched;
  if (!pinchLatched) {
    cancelScrub(true);
    instrumentSelector.cancelDrag();
    waveformPressed = instrumentSelectorPressed = false;
    dragAxis = DragAxis::Undecided;
    lastTapSide = -1;
    barFeedbackUntilMs = 0;
    accumulatedBarSteps = 0;
    pinchLatched = true;
    updatePracticeControls();
  }
  {
    const auto first = touchPositions.begin();
    pinchInitialDistance = juce::jmax(1.0f, first->second.getDistanceFrom(std::next(first)->second));
    pinchInitialSpan = getVisibleSpanSeconds();
  }
  return true;
}

bool PracticePanel::handleTouchMove(const int index, const juce::Point<float> position) {
  const auto touch = touchPositions.find(index);
  if (touch != touchPositions.end())
    touch->second = position;
  if (!pinchLatched)
    return false;
  if (touchPositions.size() >= 2) {
    const auto first = touchPositions.begin();
    const double distance = juce::jmax(1.0f, first->second.getDistanceFrom(std::next(first)->second));
    viewport.setLength(sources.lengthSeconds ? sources.lengthSeconds() : 0.0);
    if (viewport.setSpan(pinchInitialSpan * pinchInitialDistance / distance))
      repaint(waveformBounds);
  }
  return true;
}

bool PracticePanel::handleTouchUp(const int index) {
  touchPositions.erase(index);
  const bool consumed = pinchLatched;
  if (touchPositions.empty()) {
    pinchLatched = false;
    updatePracticeControls();
  } else if (pinchLatched && touchPositions.size() >= 2) {
    const auto first = touchPositions.begin();
    pinchInitialDistance = juce::jmax(1.0f, first->second.getDistanceFrom(std::next(first)->second));
    pinchInitialSpan = getVisibleSpanSeconds();
  }
  return consumed;
}

void PracticePanel::mouseMagnify(const juce::MouseEvent &event, const float scaleFactor) {
  if (waveformBounds.toFloat().contains(event.getEventRelativeTo(this).position))
    applyZoom(scaleFactor);
}

double PracticePanel::clampSeekPosition(const double seconds) const {
  const double length = sources.lengthSeconds ? sources.lengthSeconds() : 0.0;
  auto range = sources.seekRange ? sources.seekRange()
                                : juce::Range<double>(0.0, length);
  const double start = juce::jlimit(0.0, juce::jmax(0.0, length), range.getStart());
  const double end = juce::jlimit(start, juce::jmax(start, length), range.getEnd());
  return juce::jlimit(start, end, seconds);
}

double PracticePanel::getDisplayedPositionSeconds() const {
  if (scrubActive && canFinishWaveformGesture())
    return visualPositionSeconds;
  if (pendingSeek && canFinishWaveformGesture()) {
    const double elapsed =
        (juce::Time::getMillisecondCounter() - pendingSeekVisualAtMs) / 1000.0;
    const double rate = sources.playbackRate ? sources.playbackRate() : 1.0;
    return clampSeekPosition(
        pendingSeekPositionSeconds +
        (sources.isPlaying && sources.isPlaying() ? elapsed * rate : 0.0));
  }
  const double length = sources.lengthSeconds ? sources.lengthSeconds() : 0.0;
  const double source = sources.positionSeconds ? sources.positionSeconds() : 0.0;
  if (playbackClock.isValid() && sources.isPlaying && sources.isPlaying()) {
    const auto loop = sources.isLoopActive && sources.isLoopActive() && sources.loopRange
        ? sources.loopRange() : juce::Range<double>{};
    return loop.isEmpty() ? juce::jlimit(0.0, juce::jmax(0.0, length), playbackClock.position())
                         : juce::jlimit(loop.getStart(), loop.getEnd(), playbackClock.position());
  }
  const double offset = handoffOffsetSeconds != 0.0 && canFinishWaveformGesture()
      ? handoffOffsetSeconds * std::exp(
            -static_cast<double>(juce::Time::getMillisecondCounter() -
                                 handoffOffsetAtMs) / 75.0)
      : 0.0;
  return juce::jlimit(0.0, juce::jmax(0.0, length), source + offset);
}

double PracticePanel::getBarDurationSeconds() const {
  double bpm = sources.songBpm ? sources.songBpm() : 120.0;
  if (!std::isfinite(bpm) || bpm <= 0.0)
    bpm = 120.0;
  const auto meter = sources.timeSignature ? sources.timeSignature().trim()
                                          : juce::String{};
  const int slash = meter.indexOfChar('/');
  int numerator = (slash > 0 ? meter.substring(0, slash) : meter).getIntValue();
  int denominator = slash > 0 ? meter.substring(slash + 1).getIntValue() : 4;
  if (numerator <= 0)
    numerator = 4;
  if (denominator <= 0)
    denominator = 4;
  // Position is in source seconds. Playback tempo changes how quickly this
  // distance sounds, rather than changing the source distance of a bar.
  return (60.0 / bpm) * (numerator * 4.0 / denominator);
}

bool PracticePanel::canFinishWaveformGesture() const {
  return sources.canControl && sources.canControl() &&
         gestureSongId == (sources.songId ? sources.songId() : juce::String{}) &&
         gestureWasRemote == (sources.isRemote && sources.isRemote());
}

void PracticePanel::commitSeek(const double seconds) {
  if (!sources.seekToSeconds || !canFinishWaveformGesture())
    return;
  beginPendingSeek(seconds, juce::Time::getMillisecondCounter());
  previewPositionSeconds = pendingSeekTargetSeconds;
  sources.seekToSeconds(pendingSeekTargetSeconds);
  repaint();
}

void PracticePanel::beginPendingSeek(const double target, const juce::uint32 now) {
  pendingSeekTargetSeconds = clampSeekPosition(target);
  pendingSeekPositionSeconds = pendingSeekTargetSeconds;
  pendingSeekAtMs = pendingSeekVisualAtMs = now;
  pendingSeekMatchingFrames = 0;
  pendingScrubFinish = false;
  handoffOffsetSeconds = 0.0;
  playbackClock.clear();
  lastClockSourceSeconds = -1.0;
  // The audio callback can still publish its pre-seek clock for one block.
  // Both local and remote views retain ownership until the new clock settles.
  pendingSeek = true;
}

void PracticePanel::updatePendingSeek(const juce::uint32 now) {
  if (!pendingSeek)
    return;
  if (!canFinishWaveformGesture() ||
      (pendingScrubFinish && sources.scrubFinishRejected &&
       sources.scrubFinishRejected())) {
    pendingSeek = false;
    handoffOffsetSeconds = 0.0;
    return;
  }
  const double elapsed = (now - pendingSeekVisualAtMs) / 1000.0;
  const double rate = sources.playbackRate ? sources.playbackRate() : 1.0;
  if (sources.isPlaying && sources.isPlaying())
    pendingSeekPositionSeconds = clampSeekPosition(
        pendingSeekPositionSeconds + elapsed * rate);
  pendingSeekVisualAtMs = now;
  if (!sources.positionSeconds)
    return;
  const double position = sources.positionSeconds();
  const double tolerance = juce::jmax(0.03, dragSecondsPerPixel * 2.0);
  pendingSeekMatchingFrames =
      std::abs(position - pendingSeekPositionSeconds) <= tolerance
          ? pendingSeekMatchingFrames + 1 : 0;
  if ((now - pendingSeekAtMs >= 120 && pendingSeekMatchingFrames >= 2) ||
      now - pendingSeekAtMs >= 1500) {
    handoffOffsetSeconds = pendingSeekPositionSeconds - position;
    handoffOffsetAtMs = now;
    pendingSeek = false;
  }
}

void PracticePanel::cancelScrub(const bool restorePlayback) {
  if (scrubActive && sources.cancelScrub)
    sources.cancelScrub(restorePlayback);
  scrubActive = false;
  scrubSettling = false;
  motionSamples.clear();
  handoffOffsetSeconds = 0.0;
  playbackClock.clear();
  lastClockSourceSeconds = -1.0;
}

void PracticePanel::recordScrubMotion(const double x, const juce::int64 timeMs) {
  if (!motionSamples.empty()) {
    if (timeMs < motionSamples.back().timeMs)
      return;
    if (x != motionSamples.back().x)
      lastMotionAtMs = timeMs;
    if (timeMs == motionSamples.back().timeMs) {
      motionSamples.back().x = x;
      return;
    }
  }
  motionSamples.push_back({x, timeMs});
  const auto cutoff = timeMs - 100;
  while (motionSamples.size() > 2 && motionSamples[1].timeMs <= cutoff)
    motionSamples.erase(motionSamples.begin());
}

double PracticePanel::getScrubReleaseVelocityPixelsPerSecond() const {
  if (motionSamples.size() < 2 ||
      motionSamples.back().timeMs - lastMotionAtMs >= 100)
    return 0.0;
  const auto &last = motionSamples.back();
  auto first = motionSamples.front();
  const auto cutoff = last.timeMs - 100;
  if (first.timeMs < cutoff) {
    const auto &next = motionSamples[1];
    const double blend = static_cast<double>(cutoff - first.timeMs) /
                         (next.timeMs - first.timeMs);
    first.x += (next.x - first.x) * blend;
    first.timeMs = cutoff;
  }
  const double elapsed = (last.timeMs - first.timeMs) / 1000.0;
  if (elapsed <= 0.0)
    return 0.0;
  return juce::jlimit(-2400.0, 2400.0, (last.x - first.x) / elapsed);
}

void PracticePanel::settleScrub(const bool withMomentum) {
  if (!scrubActive || !canFinishWaveformGesture()) {
    cancelScrub(false);
    return;
  }
  settleOriginSeconds = visualPositionSeconds;
  settleStartedMs = juce::Time::getMillisecondCounter();
  settleDurationMs = 180;
  constexpr double friction = 4.8;
  const double velocity = withMomentum
      ? getScrubReleaseVelocityPixelsPerSecond() : 0.0;
  if (std::abs(velocity) >= 40.0) {
    const double duration = juce::jlimit(
        0.18, 1.6, std::log(std::abs(velocity) / 12.0) / friction);
    settleDurationMs = static_cast<juce::uint32>(std::round(duration * 1000.0));
    const double tail = std::exp(-friction * duration);
    const double travelPixels = velocity *
        ((1.0 - tail) / friction - duration * tail);
    const double unconstrainedTarget =
        previewPositionSeconds - travelPixels * dragSecondsPerPixel;
    previewPositionSeconds = clampSeekPosition(unconstrainedTarget);
    const double requestedTravel = std::abs(unconstrainedTarget - settleOriginSeconds);
    if (requestedTravel > 0.0001) {
      const double fraction =
          std::abs(previewPositionSeconds - settleOriginSeconds) / requestedTravel;
      settleDurationMs = static_cast<juce::uint32>(juce::jmax(
          180.0, std::round(settleDurationMs * fraction)));
    }
  }
  if (std::abs(previewPositionSeconds - settleOriginSeconds) < 0.0001)
    settleDurationMs = 180;
  scrubSettling = true;
}

void PracticePanel::updateScrubAnimation(const juce::uint32 now) {
  if (!scrubActive)
    return;
  if (!canFinishWaveformGesture()) {
    cancelScrub(false);
    return;
  }
  if (scrubSettling) {
    const double progress = juce::jmin(
        1.0, static_cast<double>(now - settleStartedMs) / settleDurationMs);
    // Analytic friction is independent of timer/frame rate. Removing the
    // terminal velocity makes the curve meet the playback handoff at rest.
    const double duration = settleDurationMs / 1000.0;
    const double tail = std::exp(-4.8 * duration);
    const double eased =
        (1.0 - std::exp(-4.8 * duration * progress) -
         4.8 * duration * progress * tail) /
        (1.0 - tail - 4.8 * duration * tail);
    visualPositionSeconds = clampSeekPosition(
        settleOriginSeconds + (previewPositionSeconds - settleOriginSeconds) * eased);
    if (progress >= 1.0) {
      const double target = clampSeekPosition(previewPositionSeconds);
      beginPendingSeek(target, now);
      pendingScrubFinish = true;
      bool accepted = true;
      if (sources.finishScrub)
        accepted = sources.finishScrub(target);
      else if (sources.seekToSeconds)
        sources.seekToSeconds(target);
      scrubActive = false;
      scrubSettling = false;
      if (!accepted) {
        pendingSeek = false;
        handoffOffsetSeconds = 0.0;
      }
    }
  } else {
    const double elapsed = juce::jmin(0.1, (now - scrubVisualAtMs) / 1000.0);
    const double blend = 1.0 - std::exp(-elapsed / 0.035);
    visualPositionSeconds = clampSeekPosition(
        visualPositionSeconds + (previewPositionSeconds - visualPositionSeconds) * blend);
  }
  scrubVisualAtMs = now;
}

void PracticePanel::showBarFeedback(const int direction,
                                   const double previousPosition,
                                   const double target) {
  const auto now = juce::Time::getMillisecondCounter();
  const bool continuing = barFeedbackUntilMs != 0 &&
      static_cast<juce::int32>(barFeedbackUntilMs - now) > 0 &&
      ((accumulatedBarSteps < 0) == (direction < 0));
  if (!continuing)
    accumulatedBarSteps = 0;
  if (std::abs(target - previousPosition) > 0.0001)
    accumulatedBarSteps += direction;
  if (accumulatedBarSteps == 0)
    return;
  const int count = std::abs(accumulatedBarSteps);
  barFeedback = (accumulatedBarSteps < 0 ? juce::String::fromUTF8("−") : "+") +
                juce::String(count) + " " +
                (count == 1 ? L10n::tr("compás", "bar")
                            : L10n::tr("compases", "bars"));
  barFeedbackSide = direction < 0 ? 0 : 1;
  barFeedbackUntilMs = now + 1500;
}

void PracticePanel::paint(juce::Graphics &g) {
  g.fillAll(UITheme::panelDark);
  auto content = safeContentBounds();
  auto header = content.removeFromTop(64);
  header.removeFromRight(56);
  const auto headingFont = makeAppFont(23.0f, AppFontWeight::SemiBold);
  const float baseline = header.toFloat().getCentreY() +
                         (headingFont.getAscent() - headingFont.getDescent()) * 0.5f;
  juce::GlyphArrangement heading;
  heading.addLineOfText(headingFont, L10n::tr("Ensayo", "Practice"),
                        static_cast<float>(header.getX()), baseline);
  g.setColour(UITheme::textPrimary);
  heading.draw(g);

  const float songX = heading.getBoundingBox(0, -1, true).getRight() + 12.0f;
  const auto title = sources.title ? sources.title().trim() : juce::String{};
  juce::GlyphArrangement songTitle;
  songTitle.addCurtailedLineOfText(makeAppFont(14.0f),
      title.isNotEmpty() ? title : L10n::tr("Sin canción", "No song"),
      songX, baseline, juce::jmax(0.0f, header.getRight() - songX), true);
  g.setColour(UITheme::textSecondary);
  songTitle.draw(g);

  g.setColour(UITheme::textSecondary);
  g.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  const auto drawSelectorTitle = [&](const juce::Component &selector,
                                     const juce::String &text) {
    const auto bounds = selector.getBounds().withY(selector.getY() - 22)
                           .withHeight(18).reduced(12, 0);
    g.drawFittedText(text, bounds, juce::Justification::centredLeft, 1);
  };
  drawSelectorTitle(pitchSelector, L10n::tr("Tono", "Key"));
  drawSelectorTitle(tempoSelector, L10n::tr("Tempo", "Tempo"));

  const int mode = getSourceDisplayListeningMode();
  const auto targetId = getSourceDisplayListeningInstrument();
  const bool selectedIsTarget = targetId == selectedInstrumentId;
  if (waveformBounds.isEmpty())
    return;
  const auto card = waveformBounds.toFloat();
  paintListeningGlow(g, card, getVisibleListeningMode());
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(card, 16.0f);

  auto picker = instrumentHeaderBounds;
  g.setColour(UITheme::textSecondary);
  g.fillRoundedRectangle(static_cast<float>(picker.getCentreX() - 18),
                         static_cast<float>(picker.getY()), 36.0f, 4.0f, 2.0f);

  const double length = sources.lengthSeconds ? sources.lengthSeconds() : 0.0;
  const double current = getDisplayedPositionSeconds();
  const auto layout = getWaveformLayout();
  const auto wave = layout.plot;
  auto tapHints = layout.hints;
  const int hintWidth = tapHints.getWidth() / 2;
  const auto feedback = currentPracticeFeedback();
  g.setFont(makeAppFont(11.0f));
  if (feedback.isNotEmpty()) {
    g.setColour(UITheme::textPrimary);
    g.drawFittedText(feedback, tapHints, juce::Justification::centred, 2);
  } else {
    g.setColour(UITheme::textSecondary.withMultipliedAlpha(0.55f));
    g.drawFittedText(L10n::tr("−1 compás · Doble toque", "−1 bar · Double tap"),
                     tapHints.removeFromLeft(hintWidth),
                     juce::Justification::centred, 1);
    g.drawFittedText(L10n::tr("+1 compás · Doble toque", "+1 bar · Double tap"),
                     tapHints, juce::Justification::centred, 1);
  }
  const auto ruler = layout.ruler;
  g.setColour(UITheme::textSecondary.withMultipliedAlpha(0.75f));
  g.setFont(makeAppFont(11.0f));
  const auto help = layout.helpText;
  const auto listeningHint = isListeningChangePending()
      ? L10n::tr("Confirmando mezcla…", "Confirming mix…")
      : selectedIsTarget && mode == 1
      ? L10n::tr("Enfoque · ↓ Normal", "Focus · ↓ Normal")
      : selectedIsTarget && mode == 2
          ? L10n::tr("Reducido · ↑ Normal", "Reduced · ↑ Normal")
          : mode != 0 ? (mode == 1 ? L10n::tr("Enfoque en otra pista", "Focus on another track")
                                   : L10n::tr("Otra pista reducida", "Another track reduced"))
          : L10n::tr("↑ Enfocar · ↓ Reducir", "↑ Focus · ↓ Reduce");
  g.drawFittedText(L10n::tr("↔ Recorrer · Pellizca: zoom\n",
                           "↔ Browse · Pinch: zoom\n") + listeningHint,
                   help, juce::Justification::centred, 2);
  auto times = layout.times;
  g.setColour(UITheme::textSecondary);
  g.setFont(makeAppFont(13.0f));
  g.drawText(formatTime(current), times.removeFromLeft(90),
             juce::Justification::centredLeft);
  g.drawText(formatTime(length), times.removeFromRight(90),
             juce::Justification::centredRight);
  g.setFont(makeAppFont(11.0f));
  g.drawText(juce::String(viewport.getSpan() > 0.0 ? 12.0 / viewport.getSpan() : 1.0, 1) + "×",
             times, juce::Justification::centred);

  const double span = getVisibleSpanSeconds();
  const auto timeToX = [&](const double seconds) {
    return static_cast<float>(wave.getCentreX() +
        (seconds - current) * wave.getWidth() / juce::jmax(0.001, span));
  };
  if (span > 0.0 && ruler.getWidth() > 0) {
    const auto visible = juce::Range<double>(current - span * 0.5, current + span * 0.5);
    const auto marks = sources.timelineMarks ? sources.timelineMarks(visible, static_cast<float>(ruler.getWidth()))
        : PracticeTimelineRuler::build(visible, static_cast<float>(ruler.getWidth()),
              sources.songBpm ? sources.songBpm() : 120.0,
              sources.timeSignature ? sources.timeSignature() : "4/4");
    drawMusicalTimelineRuler(g, ruler.toFloat(), visible, length, marks);
  }

  const auto drawBarFeedback = [&] {
    if (barFeedbackUntilMs == 0 ||
        static_cast<juce::int32>(barFeedbackUntilMs -
                                juce::Time::getMillisecondCounter()) <= 0)
      return;
    const juce::Graphics::ScopedSaveState savedState(g);
    auto badge = juce::Rectangle<float>(120.0f, 34.0f).withCentre(
        {card.getX() + card.getWidth() * (barFeedbackSide == 0 ? 0.25f : 0.75f),
         card.getY() + 89.0f});
    g.setColour(UITheme::panelDark.withAlpha(0.9f));
    g.fillRoundedRectangle(badge, 17.0f);
    g.setColour(UITheme::accent);
    g.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
    g.drawText(barFeedback, badge, juce::Justification::centred);
  };

  if (length <= 0.0 || waveformBins.empty()) {
    g.drawFittedText(
        length <= 0.0
            ? L10n::tr("Carga una canción para comenzar a ensayar.",
                       "Load a song to start practicing.")
            : instruments.empty()
                  ? L10n::tr("Esta canción no tiene instrumentos.",
                             "This song has no instruments.")
                  : sources.isRemote && sources.isRemote()
                        ? L10n::tr("Onda individual no disponible en remoto.",
                                   "Individual waveform unavailable remotely.")
                        : L10n::tr("Preparando onda…", "Preparing waveform…"),
        wave.reduced(18, 0), juce::Justification::centred, 2);
    paintInsetWaveformFrame(g, card);
    drawBarFeedback();
    return;
  }

  if (wave.getWidth() <= 0 || wave.getHeight() <= 0) {
    paintInsetWaveformFrame(g, card);
    return;
  }
  const float middle = static_cast<float>(wave.getCentreY());
  waveformGeometry.configure(length, juce::jmin(2.0, length), static_cast<float>(wave.getWidth()),
                             static_cast<float>(wave.getHeight()));
  g.saveState();
  g.reduceClipRegion(wave);
  g.setColour(UITheme::cardBorder.withAlpha(0.75f));
  g.drawHorizontalLine(static_cast<int>(middle), static_cast<float>(wave.getX()),
                       static_cast<float>(wave.getRight()));
  if (loopStartSeconds >= 0.0) {
    const double end = markingLoop ? juce::jmax(loopStartSeconds, current) : loopEndSeconds;
    if (end >= loopStartSeconds) {
      const auto a = timeToX(loopStartSeconds);
      const auto b = timeToX(end);
      g.setColour(UITheme::accent.withAlpha(loopToggleButton.getToggleState() || markingLoop ? 0.16f : 0.07f));
      g.fillRect(juce::Rectangle<float>(a, static_cast<float>(wave.getY()), b - a,
                                        static_cast<float>(wave.getHeight())));
    }
  }
  g.setColour(UITheme::accentSoft);
  waveformGeometry.paint(g, wave.toFloat(), current, span);
  g.restoreState();
  paintInsetWaveformFrame(g, card);
  g.saveState();
  g.reduceClipRegion(wave);
  const float playheadX = static_cast<float>(wave.getCentreX());
  g.setColour(UITheme::textPrimary);
  g.drawVerticalLine(static_cast<int>(playheadX), static_cast<float>(wave.getY()),
                     static_cast<float>(wave.getBottom()));
  if (loopStartSeconds >= 0.0) {
    const auto drawMarker = [&](double seconds, const juce::String &label) {
      const float x = timeToX(seconds);
      if (x < wave.getX() || x > wave.getRight())
        return;
      g.setColour(UITheme::accent);
      g.drawLine(x, static_cast<float>(wave.getY()), x, static_cast<float>(wave.getBottom()), 1.5f);
      const float badgeX = juce::jlimit(static_cast<float>(wave.getX() + 2),
          static_cast<float>(juce::jmax(wave.getX() + 2, wave.getRight() - 22)), x - 10.0f);
      auto badge = juce::Rectangle<float>(badgeX, static_cast<float>(wave.getY()), 20.0f, 20.0f);
      g.fillRoundedRectangle(badge, 4.0f);
      g.setColour(UITheme::onAccent);
      g.setFont(makeAppFont(11.0f, AppFontWeight::SemiBold));
      g.drawText(label, badge, juce::Justification::centred);
    };
    drawMarker(loopStartSeconds, "A");
    if (!markingLoop && loopEndSeconds >= 0.0)
      drawMarker(loopEndSeconds, "B");
  }
  g.restoreState();
  drawBarFeedback();
}

void PracticePanel::refreshWaveform(const juce::String &wheelSelectionId) {
  const auto songId = sources.songId ? sources.songId() : juce::String{};
  const bool remote = sources.isRemote && sources.isRemote();
  const auto revision = sources.waveformRevision ? sources.waveformRevision()
                                                  : juce::String{};
  const double length = waveformLength(sources);
  const bool contextChanged = songId != displayedSongId ||
                              remote != displayedWasRemote;
  const bool waveformChanged = contextChanged ||
      revision != displayedWaveformRevision ||
      length != displayedWaveformLengthSeconds;
  viewport.setLength(length);
  if (waveformChanged) {
    cancelWaveformRequest();
    displayedSongId = songId;
    displayedWasRemote = remote;
    displayedWaveformRevision = revision;
    displayedWaveformLengthSeconds = length;
    waveformBins.clear();
    waveformGeometry.clear();
    waveformCache.clear();
    repaint(waveformBounds);
  }
  if (contextChanged) {
    cancelScrub(false);
    viewport.reset();
    touchPositions.clear();
    pinchLatched = false;
    pitchPending = false;
    pitchSelector.cancelDrag();
    tempoPending = false;
    guideControl.request.pending = false;
    metronomeControl.request.pending = false;
    tempoSelector.cancelDrag();
    cancelInstrumentGesture();
    clearLoopSelection();
    releasedLoopIdentities.clear();
    selectedInstrumentId.clear();
    instruments.clear();
    listeningActivatedHere = false;
    requestedListeningMode = 0;
    pendingSeek = false;
    waveformPressed = false;
    dragAxis = DragAxis::Undecided;
    lastTapSide = -1;
    barFeedbackUntilMs = 0;
    accumulatedBarSteps = 0;
  }
  auto latest = songId.isNotEmpty() && sources.instruments
                    ? sources.instruments()
                    : std::vector<Instrument>{};
  bool changed = latest.size() != instruments.size();
  if (!changed)
    for (size_t i = 0; i < latest.size(); ++i)
      if (latest[i].id != instruments[i].id ||
          latest[i].name != instruments[i].name) {
        changed = true;
        break;
      }
  if (changed) {
    cancelInstrumentGesture();
    instruments = std::move(latest);
    for (auto cached = waveformCache.begin(); cached != waveformCache.end();) {
      const auto found = std::find_if(instruments.begin(), instruments.end(),
          [&](const Instrument &entry) { return entry.id == cached->first; });
      if (found == instruments.end())
        cached = waveformCache.erase(cached);
      else
        ++cached;
    }
    const auto found = std::find_if(
        instruments.begin(), instruments.end(), [this](const Instrument &entry) {
          return entry.id == selectedInstrumentId;
        });
    if (found == instruments.end()) {
      cancelWaveformRequest();
      selectedInstrumentId.clear();
      waveformBins.clear();
      waveformGeometry.clear();
      repaint(waveformBounds);
    }
  }
  const bool canSelect = sources.canControl && sources.canControl();
  const bool preserveWheelSelection = canSelect && !contextChanged &&
      std::any_of(instruments.begin(), instruments.end(),
          [&wheelSelectionId](const Instrument &entry) { return entry.id == wheelSelectionId; });
  instrumentSelector.setInstruments(instruments,
      preserveWheelSelection ? wheelSelectionId : selectedInstrumentId);
  instrumentSelector.setEnabled(!instruments.empty() && canSelect);
  if (selectedInstrumentId.isEmpty() && !instruments.empty())
    selectInstrument(0);
  else if (waveformChanged && selectedInstrumentId.isNotEmpty() &&
           !(preserveWheelSelection && wheelSelectionId != selectedInstrumentId)) {
    // A committed wheel candidate will request its own fresh bins below in
    // selectInstrumentFromWheel; reloading the old ID here would reset preview.
    const auto selected = std::find_if(instruments.begin(), instruments.end(),
        [this](const Instrument &entry) {
          return entry.id == selectedInstrumentId;
        });
    if (selected != instruments.end())
      selectInstrument(static_cast<int>(selected - instruments.begin()), true);
  }
}

void PracticePanel::cancelWaveformRequest() {
  ++waveformRequestGeneration;
  if (activeWaveformCancellation)
    activeWaveformCancellation->store(true, std::memory_order_relaxed);
  activeWaveformCancellation.reset();
}

bool PracticePanel::waveformContextMatchesSources() const {
  return displayedSongId == (sources.songId ? sources.songId() : juce::String{}) &&
      displayedWasRemote == (sources.isRemote && sources.isRemote()) &&
      displayedWaveformRevision == (sources.waveformRevision
          ? sources.waveformRevision() : juce::String{}) &&
      displayedWaveformLengthSeconds == waveformLength(sources);
}

void PracticePanel::selectInstrument(const int index, const bool reloadWaveform) {
  if (!juce::isPositiveAndBelow(index, static_cast<int>(instruments.size())))
    return;
  const auto id = instruments[static_cast<size_t>(index)].id;
  if (!waveformContextMatchesSources()) {
    // A wheel selection can precede the next 500 ms probe. Never use a cache
    // entry from the previous content revision in that interval.
    auto safe = juce::Component::SafePointer<PracticePanel>(this);
    refreshWaveform();
    if (safe == nullptr)
      return;
    const auto found = std::find_if(instruments.begin(), instruments.end(),
        [&id](const Instrument &entry) { return entry.id == id; });
    if (found != instruments.end())
      selectInstrument(static_cast<int>(found - instruments.begin()));
    return;
  }
  const bool selectionChanged = id != selectedInstrumentId;
  if (!selectionChanged && !reloadWaveform)
    return;
  // Advance even for a cache hit: a cancelled provider may still deliver.
  cancelWaveformRequest();
  const auto generation = waveformRequestGeneration;
  selectedInstrumentId = id;
  if (instrumentSelector.getSelectedInstrumentId() != id)
    cancelInstrumentGesture();
  instrumentSelector.setInstruments(instruments, selectedInstrumentId);
  instrumentSelector.setEnabled(sources.canControl && sources.canControl());
  auto safe = juce::Component::SafePointer<PracticePanel>(this);
  if (selectionChanged && listeningActivatedHere && requestedListeningMode != 0) {
    if (sources.setListeningMode && sources.canControl && sources.canControl() &&
        sources.canRetargetListening && sources.canRetargetListening()) {
      const auto retarget = sources.setListeningMode;
      retarget(id, requestedListeningMode);
      if (safe == nullptr || waveformRequestGeneration != generation)
        return;
    } else {
      listeningActivatedHere = false;
      requestedListeningMode = 0;
    }
  }
  waveformBins.clear();
  waveformGeometry.clear();
  const auto cached = waveformCache.find(id);
  if (cached != waveformCache.end()) {
    waveformBins = cached->second;
    waveformGeometry.setBins(waveformBins);
    repaint();
    return;
  }
  const auto songId = displayedSongId;
  const auto revision = displayedWaveformRevision;
  const bool remote = displayedWasRemote;
  const double length = displayedWaveformLengthSeconds;
  repaint();
  if (sources.requestInstrumentWaveform) {
    activeWaveformCancellation = std::make_shared<std::atomic<bool>>(false);
    const auto cancellation = activeWaveformCancellation;
    // Keep the provider alive if it delivers synchronously and re-enters or
    // closes the panel. All request state is installed before invoking it.
    const auto request = sources.requestInstrumentWaveform;
    request(id, cancellation,
        [safe, cancellation, generation, songId, revision, remote, length, id]
        (std::vector<float> bins) mutable {
          if (cancellation->load(std::memory_order_relaxed) || safe == nullptr ||
              safe->waveformRequestGeneration != generation ||
              safe->selectedInstrumentId != id ||
              safe->displayedSongId != songId ||
              safe->displayedWasRemote != remote ||
              safe->displayedWaveformRevision != revision ||
              safe->displayedWaveformLengthSeconds != length ||
              !safe->waveformContextMatchesSources())
            return;
          // Check the live providers before touching either cache or UI. A
          // source edit may arrive before refreshWaveform's next timer probe.
          safe->waveformCache[id] = bins;
          safe->waveformBins = std::move(bins);
          safe->waveformGeometry.setBins(safe->waveformBins);
          safe->repaint();
        });
  }
}

void PracticePanel::applyListeningGesture(const juce::Point<float> &movement) {
  if (!waveformPressed || dragAxis != DragAxis::Vertical || listeningSwipeHandled ||
      selectedInstrumentId.isEmpty() || !sources.setListeningMode ||
      !canFinishWaveformGesture() ||
      std::abs(movement.y) < 44 ||
      static_cast<float>(std::abs(movement.y)) <=
          static_cast<float>(std::abs(movement.x)) * 1.2f)
    return;
  listeningSwipeHandled = true;
  const int mode = sources.listeningMode ? sources.listeningMode() : 0;
  const auto targetId = sources.listeningInstrumentId
      ? sources.listeningInstrumentId() : juce::String{};
  const int currentMode = targetId == selectedInstrumentId ? mode : 0;
  // Mode IDs are protocol values, not the vertical ordering: low, normal, focus.
  const int level = currentMode == 1 ? 1 : currentMode == 2 ? -1 : 0;
  const int nextLevel = juce::jlimit(-1, 1, level + (movement.y < 0 ? 1 : -1));
  if (nextLevel == level)
    return;
  requestedListeningMode = nextLevel > 0 ? 1 : nextLevel < 0 ? 2 : 0;
  sources.setListeningMode(requestedListeningMode == 0 ? juce::String{}
                                                      : selectedInstrumentId,
                           requestedListeningMode);
  listeningActivatedHere = requestedListeningMode != 0;
  repaint();
}

void PracticePanel::updateWaveformGesture(const juce::Point<float> position,
                                        const juce::int64 eventTimeMs) {
  if (!waveformPressed || !canFinishWaveformGesture())
    return;
  recordScrubMotion(position.x, eventTimeMs);
  const auto movement = position - dragStart;
  if (dragAxis == DragAxis::Undecided) {
    if (std::abs(movement.x) >= 12 &&
        std::abs(movement.x) > std::abs(movement.y) * 1.2f)
      dragAxis = DragAxis::Horizontal;
    else if (std::abs(movement.y) >= 12 &&
             std::abs(movement.y) > std::abs(movement.x) * 1.2f)
      dragAxis = DragAxis::Vertical;
    if (dragAxis != DragAxis::Undecided) {
      lastTapSide = -1;
      barFeedbackUntilMs = 0;
      accumulatedBarSteps = 0;
    }
  }
  if (dragAxis == DragAxis::Horizontal) {
    if (instrumentSelectorPressed)
      return;
    if (!scrubActive) {
      scrubActive = true;
      visualPositionSeconds = dragOriginSeconds;
      scrubVisualAtMs = juce::Time::getMillisecondCounter();
      pendingSeek = false;
      handoffOffsetSeconds = 0.0;
      if (sources.beginScrub)
        sources.beginScrub();
    }
    previewPositionSeconds = clampSeekPosition(
        dragOriginSeconds - movement.x * dragSecondsPerPixel);
    updateScrubAnimation(juce::Time::getMillisecondCounter());
    repaint(waveformBounds);
  } else if (dragAxis == DragAxis::Vertical) {
    applyListeningGesture(movement);
  }
}

void PracticePanel::cancelInstrumentGesture() {
  instrumentSelector.cancelDrag();
  if (instrumentSelectorPressed) {
    instrumentSelectorPressed = false;
    waveformPressed = false;
    dragAxis = DragAxis::Undecided;
    lastTapSide = -1;
  }
}

void PracticePanel::selectInstrumentFromWheel() {
  // Capture the stable ID before refreshing; an index may now identify a
  // different stem after a same-song reorder between the 500 ms probes.
  const auto id = instrumentSelector.getSelectedInstrumentId();
  const auto songId = displayedSongId;
  const bool remote = displayedWasRemote;
  const juce::Component::SafePointer<PracticePanel> safe(this);
  refreshWaveform(id);
  if (safe == nullptr || songId != displayedSongId || remote != displayedWasRemote ||
      !sources.canControl || !sources.canControl())
    return;
  const auto found = std::find_if(instruments.begin(), instruments.end(),
      [&id](const Instrument &entry) { return entry.id == id; });
  if (found != instruments.end())
    selectInstrument(static_cast<int>(found - instruments.begin()));
}

bool PracticePanel::routeInstrumentPointer(const juce::MouseEvent &event,
    PracticeInstrumentSelector::PointerPhase phase) {
  const juce::Component::SafePointer<PracticePanel> safe(this);
  if (phase == PracticeInstrumentSelector::PointerPhase::down) {
    mouseDown(event);
    return safe == nullptr || pinchLatched || !instrumentSelectorPressed ||
           !canFinishWaveformGesture();
  }
  // The wheel acquires a horizontal gesture at its own six-point threshold.
  // Preserve that owner if the finger later turns vertically.
  if (instrumentSelectorPressed && instrumentSelector.isHorizontalDragActive())
    dragAxis = DragAxis::Horizontal;
  mouseDrag(event);
  if (safe == nullptr)
    return true;
  // Capture before mouseUp clears its axis and the last pinch contact. Running
  // mouseDrag first also resolves a last/coalesced vertical movement on release.
  const bool consumed = pinchLatched || dragAxis == DragAxis::Vertical ||
                        !instrumentSelectorPressed || !canFinishWaveformGesture();
  if (phase == PracticeInstrumentSelector::PointerPhase::up)
    mouseUp(event);
  return consumed;
}

void PracticePanel::mouseDown(const juce::MouseEvent &event) {
  const auto songId = sources.songId ? sources.songId() : juce::String{};
  const bool remote = sources.isRemote && sources.isRemote();
  if (songId != displayedSongId || remote != displayedWasRemote) {
    auto safe = juce::Component::SafePointer<PracticePanel>(this);
    refreshWaveform();
    if (safe == nullptr)
      return;
  }
  if (gestureSongId != songId || gestureWasRemote != remote) {
    cancelScrub(false);
    lastTapSide = -1;
    pendingSeek = false;
    barFeedbackUntilMs = 0;
    accumulatedBarSteps = 0;
  }
  const auto position = event.getEventRelativeTo(this).position;
  if (event.source.isTouch() && handleTouchDown(event.source.getIndex(), position))
    return;
  updateScrubAnimation(juce::Time::getMillisecondCounter());
  if (scrubActive && scrubSettling) {
    scrubSettling = false;
    previewPositionSeconds = visualPositionSeconds;
  }
  instrumentSelectorPressed = event.eventComponent == &instrumentSelector;
  waveformPressed = event.eventComponent != &listeningHelpButton &&
                    waveformBounds.toFloat().contains(position);
  dragAxis = DragAxis::Undecided;
  gestureSongId = songId;
  gestureWasRemote = remote;
  dragOriginSeconds = getDisplayedPositionSeconds();
  previewPositionSeconds = dragOriginSeconds;
  dragSecondsPerPixel = getVisibleSpanSeconds() /
                        juce::jmax(1, getWaveformLayout().plot.getWidth());
  listeningSwipeHandled = false;
  dragStart = position;
  motionSamples.clear();
  lastMotionAtMs = event.eventTime.toMilliseconds();
  recordScrubMotion(position.x, lastMotionAtMs);
}

void PracticePanel::mouseDrag(const juce::MouseEvent &event) {
  if (event.source.isTouch() && handleTouchMove(event.source.getIndex(), event.getEventRelativeTo(this).position))
    return;
  updateWaveformGesture(event.getEventRelativeTo(this).position,
                        event.eventTime.toMilliseconds());
}

void PracticePanel::mouseUp(const juce::MouseEvent &event) {
  if (event.source.isTouch() && handleTouchUp(event.source.getIndex()))
    return;
  const auto position = event.getEventRelativeTo(this).position;
  updateWaveformGesture(position, event.eventTime.toMilliseconds());
  if (waveformPressed && !instrumentSelectorPressed && dragAxis == DragAxis::Horizontal) {
    if (canFinishWaveformGesture())
      settleScrub(true);
    else {
      cancelScrub(false);
      pendingSeek = false;
    }
  } else if (waveformPressed && !instrumentSelectorPressed && dragAxis == DragAxis::Undecided &&
             waveformBounds.toFloat().contains(position) &&
             event.getDistanceFromDragStart() < 12 &&
             !event.mouseWasDraggedSinceMouseDown() &&
             event.eventComponent != &listeningHelpButton) {
    const int side = position.x < waveformBounds.getCentreX() ? 0 : 1;
    const bool doubleTap = event.getNumberOfClicks() >= 2 &&
                           event.getNumberOfClicks() % 2 == 0 && lastTapSide == side;
    if (doubleTap && canFinishWaveformGesture()) {
      const int direction = side == 0 ? -1 : 1;
      const double previous = scrubActive ? previewPositionSeconds
                                         : getDisplayedPositionSeconds();
      const double target = clampSeekPosition(previous + direction * getBarDurationSeconds());
      if (scrubActive) {
        previewPositionSeconds = target;
        settleScrub();
      } else {
        commitSeek(target);
      }
      showBarFeedback(direction, previous, target);
      lastTapSide = -1;
    } else {
      lastTapSide = side;
    }
  } else {
    lastTapSide = -1;
  }
  if (scrubActive && !scrubSettling)
    settleScrub();
  instrumentSelectorPressed = false;
  waveformPressed = false;
  listeningSwipeHandled = false;
  dragAxis = DragAxis::Undecided;
}

void PracticePanel::updateAnimationFrame(const double frameTimestampSec,
                                        const bool fromVBlank) {
  if (getPeer() != nullptr && !isShowing())
    return;
  const auto now = juce::Time::getMillisecondCounter();
  if (fromVBlank)
    lastVBlankAtMs = now;
  if (animationUsesVBlank != fromVBlank) {
    // CADisplayLink and the fallback timer have different timestamp origins.
    playbackClock.clear();
    animationUsesVBlank = fromVBlank;
  }
  updateScrubAnimation(now);
  updatePendingSeek(now);
  if (scrubActive || pendingSeek) {
    playbackClock.clear();
    lastClockSourceSeconds = -1.0;
  } else {
    const double source = sources.positionSeconds ? sources.positionSeconds() : 0.0;
    const auto loop = sources.isLoopActive && sources.isLoopActive() && sources.loopRange
        ? sources.loopRange() : juce::Range<double>{};
    // A short A/B wrap is intentional even when it is smaller than the normal
    // 250 ms discontinuity threshold used to reject clock/packet jitter.
    if (!loop.isEmpty() && lastClockSourceSeconds >= 0.0 &&
        lastClockSourceSeconds - source > juce::jmin(0.2, loop.getLength() * 0.5))
      playbackClock.clear();
    lastClockSourceSeconds = source;
    const double offset = canFinishWaveformGesture()
        ? handoffOffsetSeconds * std::exp(
              -static_cast<double>(now - handoffOffsetAtMs) / 75.0) : 0.0;
    const double rate = sources.playbackRate ? sources.playbackRate() : 1.0;
    playbackClock.update(source + offset,
                         sources.isPlaying && sources.isPlaying() &&
                             sources.canControl && sources.canControl(),
                         rate, frameTimestampSec);
  }
  const double presented = getDisplayedPositionSeconds();
  if (std::abs(presented - lastPresentedPositionSeconds) > 0.000001) {
    lastPresentedPositionSeconds = presented;
    repaint(waveformBounds);
  }
}

void PracticePanel::timerCallback() {
  const auto now = juce::Time::getMillisecondCounter();
  if (now >= nextWaveformProbeMs) {
    auto safe = juce::Component::SafePointer<PracticePanel>(this);
    refreshWaveform();
    if (safe == nullptr)
      return;
    nextWaveformProbeMs = now + 500;
  }
  const bool enabled = sources.canControl && sources.canControl();
  playPauseButton.setEnabled(enabled);
  stopButton.setEnabled(enabled);
  instrumentSelector.setEnabled(enabled && !instruments.empty());
  const int mode = getSourceDisplayListeningMode();
  const auto listeningInstrument = getSourceDisplayListeningInstrument();
  const bool listeningPending = isListeningChangePending();
  if (mode != displayedListeningMode || enabled != displayedCanControl ||
      listeningPending != displayedListeningPending ||
      listeningInstrument != displayedListeningInstrument) {
    displayedCanControl = enabled;
    displayedListeningPending = listeningPending;
    displayedListeningMode = mode;
    displayedListeningInstrument = listeningInstrument;
    repaint(getWaveformLayout().hints);
    repaint(waveformBounds.expanded(6));
    listeningHelpButton.setDescription(listeningContextText() +
        (listeningPending ? L10n::tr(" · Confirmando cambio", " · Confirming change") : juce::String{}));
  }
  if (!enabled) {
    cancelInstrumentGesture();
    cancelScrub(false);
    listeningActivatedHere = false;
    requestedListeningMode = 0;
    pendingSeek = false;
    waveformPressed = false;
    lastTapSide = -1;
    barFeedbackUntilMs = 0;
    accumulatedBarSteps = 0;
    touchPositions.clear();
    pinchLatched = false;
    clearLoopSelection();
    pitchPending = false;
    tempoPending = false;
  }
  updatePracticeControls();
  if (practiceFeedbackUntilMs != 0 && static_cast<juce::int32>(now - practiceFeedbackUntilMs) >= 0) {
    practiceFeedbackUntilMs = 0;
    setDescription(currentPracticeFeedback());
    repaint(getWaveformLayout().hints);
  }
#if JUCE_ANDROID
  updateAnimationFrame(juce::Time::getMillisecondCounterHiRes() * 0.001);
#else
  if (lastVBlankAtMs == 0 || now - lastVBlankAtMs > 100)
    updateAnimationFrame(juce::Time::getMillisecondCounterHiRes() * 0.001);
#endif
  const bool playing = sources.isPlaying && sources.isPlaying();
  const auto playLabel = playing
                             ? L10n::tr("Pausar", "Pause")
                             : L10n::tr("Reproducir", "Play");
  if (playPauseButton.getButtonText() != playLabel)
    playPauseButton.setButtonText(playLabel);
  playPauseButton.setIcon(playing ? SvgIconName::Pause : SvgIconName::Play);
  if (playPauseButton.getToggleState() != playing)
    playPauseButton.setToggleState(playing, juce::dontSendNotification);
  const bool feedback = barFeedbackUntilMs != 0 &&
      static_cast<juce::int32>(barFeedbackUntilMs - now) > 0;
  if (feedback != displayedBarFeedback) {
    displayedBarFeedback = feedback;
    repaint(waveformBounds);
  }
}

bool PracticePanel::keyPressed(const juce::KeyPress &key) {
  if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R') {
    if (!key.getModifiers().isAnyModifierKeyDown()) {
      restoreListeningMix();
      return true;
    }
  }
  if (key == juce::KeyPress::escapeKey) {
    if (sources.close)
      sources.close();
    return true;
  }
  return false;
}
