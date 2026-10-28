#include "ui/panels/PracticePanel.h"
#include "ui/UiCommon.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

void runPracticeWaveformGeometryTests();
void runPracticePlaybackClockTests();
void runPracticeViewportTests();
void runPracticeLoopTests();

namespace {
void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectNear(const double actual, const double expected,
                const char *message) {
  if (!std::isfinite(actual) || !std::isfinite(expected) ||
      std::abs(actual - expected) > 0.00001) {
    std::cerr << "FAILED: " << message << " (actual " << actual
              << ", expected " << expected << ")\n";
    std::exit(1);
  }
}

void pumpFor(const int milliseconds) {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

juce::MouseEvent fractionalPointerEvent(juce::Component &component,
                                       const juce::Point<float> position,
                                       const juce::Point<float> start,
                                       const int clicks = 1,
                                       const bool dragged = false,
                                       const juce::int64 eventTimeMs = 1000000) {
  // Default events share a timestamp so ordinary drag regressions do not
  // accidentally become flicks because the test runner took a millisecond.
  const juce::Time now(eventTimeMs);
  return {juce::Desktop::getInstance().getMainMouseSource(), position,
          juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
          &component, &component, now, start, now, clicks, dragged};
}

juce::MouseEvent pointerEvent(juce::Component &component,
                             const juce::Point<int> position,
                             const juce::Point<int> start,
                             const int clicks = 1,
                             const bool dragged = false,
                             const juce::int64 eventTimeMs = 1000000) {
  return fractionalPointerEvent(component, position.toFloat(), start.toFloat(),
                                clicks, dragged, eventTimeMs);
}
} // namespace

// Access presentation state while every gesture follows the real event handlers.
struct PracticePanelGestureTestAccess {
  static juce::Rectangle<int> card(const PracticePanel &panel) {
    return panel.waveformBounds;
  }

  static juce::Rectangle<int> header(const PracticePanel &panel) {
    return panel.instrumentHeaderBounds;
  }

  static juce::Rectangle<int> waveformPlot(const PracticePanel &panel) {
    return panel.getWaveformLayout().plot;
  }

  static juce::Rectangle<int> waveformRuler(const PracticePanel &panel) {
    return panel.getWaveformLayout().ruler;
  }

  static double displayedPosition(const PracticePanel &panel) {
    return panel.getDisplayedPositionSeconds();
  }

  static PracticeInstrumentSelector &instrumentSelector(PracticePanel &panel) {
    return panel.instrumentSelector;
  }

  static juce::Rectangle<int> helpText(const PracticePanel &panel) {
    return panel.getWaveformLayout().helpText;
  }

  static juce::TextButton &helpButton(PracticePanel &panel) {
    return panel.listeningHelpButton;
  }

  static juce::TextButton &accompanimentButton(PracticePanel &panel, bool guide) {
    return guide ? panel.guideButton : panel.metronomeButton;
  }

  static bool accompanimentPending(const PracticePanel &panel, bool guide) {
    return (guide ? panel.guideControl : panel.metronomeControl).request.pending;
  }

  static void ageAccompanimentRequest(PracticePanel &panel, bool guide,
                                     int milliseconds) {
    auto &control = guide ? panel.guideControl : panel.metronomeControl;
    control.request.requestedAt -= static_cast<juce::uint32>(milliseconds);
  }

  static juce::Rectangle<int> hints(const PracticePanel &panel) {
    return panel.getWaveformLayout().hints;
  }

  static juce::Rectangle<int> footer(const PracticePanel &panel) {
    return panel.getWaveformLayout().help;
  }

  static juce::String practiceFeedback(const PracticePanel &panel) {
    return panel.currentPracticeFeedback();
  }

  static void expirePracticeFeedback(PracticePanel &panel) {
    panel.practiceFeedbackUntilMs = juce::Time::getMillisecondCounter() - 1;
    panel.timerCallback();
  }

  static int visibleListeningMode(const PracticePanel &panel) {
    return panel.getVisibleListeningMode();
  }

  static bool listeningChangePending(const PracticePanel &panel) {
    return panel.isListeningChangePending();
  }

  static juce::Button &stopButton(PracticePanel &panel) {
    return panel.stopButton;
  }

  static bool hasBarFeedback(const PracticePanel &panel) {
    return panel.barFeedbackUntilMs != 0 &&
           static_cast<juce::int32>(panel.barFeedbackUntilMs -
                                   juce::Time::getMillisecondCounter()) > 0;
  }

  static double targetPosition(const PracticePanel &panel) {
    return panel.previewPositionSeconds;
  }

  static bool isSettling(const PracticePanel &panel) {
    return panel.scrubActive && panel.scrubSettling;
  }

  static void refreshFromSources(PracticePanel &panel) {
    // Apply fixture source changes without depending on OS timer delivery.
    panel.timerCallback();
  }

  static void advanceAnimation(PracticePanel &panel, const int milliseconds) {
    panel.scrubVisualAtMs -= static_cast<juce::uint32>(milliseconds);
    if (panel.scrubSettling)
      panel.settleStartedMs -= static_cast<juce::uint32>(milliseconds);
    if (panel.pendingSeek) {
      panel.pendingSeekAtMs -= static_cast<juce::uint32>(milliseconds);
      panel.pendingSeekVisualAtMs -= static_cast<juce::uint32>(milliseconds);
    }
    if (panel.handoffOffsetSeconds != 0.0)
      panel.handoffOffsetAtMs -= static_cast<juce::uint32>(milliseconds);
    panel.timerCallback();
  }

  static void finishAnimation(PracticePanel &panel) {
    advanceAnimation(panel, static_cast<int>(panel.settleDurationMs) + 1);
  }

  static int settleDuration(const PracticePanel &panel) {
    return static_cast<int>(panel.settleDurationMs);
  }

  static void animateAtElapsed(PracticePanel &panel, const int milliseconds) {
    const auto now = juce::Time::getMillisecondCounter();
    panel.settleStartedMs = now - static_cast<juce::uint32>(milliseconds);
    panel.updateScrubAnimation(now);
  }

  static bool hasPendingSeek(const PracticePanel &panel) {
    return panel.pendingSeek;
  }

  static void agePendingSeek(PracticePanel &panel, const int milliseconds) {
    panel.pendingSeekAtMs -= static_cast<juce::uint32>(milliseconds);
  }

  static int accumulatedBars(const PracticePanel &panel) {
    return panel.accumulatedBarSteps;
  }

  static juce::String feedback(const PracticePanel &panel) {
    return panel.barFeedback;
  }

  static int feedbackRemainingMs(const PracticePanel &panel) {
    return static_cast<juce::int32>(panel.barFeedbackUntilMs -
                                    juce::Time::getMillisecondCounter());
  }

  static void setFeedbackRemainingMs(PracticePanel &panel, const int remaining) {
    panel.barFeedbackUntilMs =
        juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(remaining);
  }

  static juce::Button &playButton(PracticePanel &panel) {
    return panel.playPauseButton;
  }

  static juce::Button &closeButton(PracticePanel &panel) {
    return panel.closeButton;
  }

  static PracticePitchSelector &pitchSelector(PracticePanel &panel) {
    return panel.pitchSelector;
  }

  static PracticeTempoSelector &tempoSelector(PracticePanel &panel) {
    return panel.tempoSelector;
  }

  static juce::Button &loopClearButton(PracticePanel &panel) {
    return panel.loopClearButton;
  }

  static bool isTempoPending(const PracticePanel &panel) {
    return panel.tempoPending;
  }

  static juce::Button &loopStartButton(PracticePanel &panel) {
    return panel.loopStartButton;
  }

  static juce::Button &loopEndButton(PracticePanel &panel) {
    return panel.loopEndButton;
  }

  static juce::Button &loopToggleButton(PracticePanel &panel) {
    return panel.loopToggleButton;
  }

  static double visibleSpan(const PracticePanel &panel) {
    return panel.viewport.getSpan();
  }

  static void zoom(PracticePanel &panel, const double factor) {
    panel.applyZoom(factor);
  }

  static bool touchDown(PracticePanel &panel, const int index,
                        const juce::Point<float> position) {
    return panel.handleTouchDown(index, position);
  }

  static bool touchMove(PracticePanel &panel, const int index,
                        const juce::Point<float> position) {
    return panel.handleTouchMove(index, position);
  }

  static bool touchUp(PracticePanel &panel, const int index) {
    return panel.handleTouchUp(index);
  }

  static void updateControls(PracticePanel &panel) {
    panel.updatePracticeControls();
  }

  static double loopStart(const PracticePanel &panel) {
    return panel.loopStartSeconds;
  }

  static double loopEnd(const PracticePanel &panel) {
    return panel.loopEndSeconds;
  }

  static bool isMarkingLoop(const PracticePanel &panel) {
    return panel.markingLoop;
  }

  static bool isPitchPending(const PracticePanel &panel) {
    return panel.pitchPending;
  }

  static bool isLoopPending(const PracticePanel &panel) {
    return panel.loopPending;
  }

  static void updateFrame(PracticePanel &panel, const double timestampSeconds) {
    panel.updateAnimationFrame(timestampSeconds);
  }

  static void probeCurrentSong(PracticePanel &panel) {
    panel.nextWaveformProbeMs = 0;
    panel.timerCallback();
  }

  static void selectInstrument(PracticePanel &panel, const int index) {
    panel.selectInstrument(index);
  }

  static void refreshWaveform(PracticePanel &panel) {
    panel.refreshWaveform();
  }

  static const std::vector<float> &waveform(const PracticePanel &panel) {
    return panel.waveformBins;
  }

  static const std::map<juce::String, std::vector<float>> &waveformCache(
      const PracticePanel &panel) {
    return panel.waveformCache;
  }

  static juce::String selectedInstrument(const PracticePanel &panel) {
    return panel.selectedInstrumentId;
  }

  static unsigned int waveformGeneration(const PracticePanel &panel) {
    return panel.waveformRequestGeneration;
  }

  static bool scrubIsActive(const PracticePanel &panel) {
    return panel.scrubActive;
  }

  static size_t waveformGeometrySize(PracticePanel &panel) {
    panel.waveformGeometry.configure(panel.displayedWaveformLengthSeconds,
                                    panel.viewport.getSpan(), 390.0f, 200.0f);
    return panel.waveformGeometry.getBars().size();
  }
};

namespace {
struct Fixture {
  juce::String songId = "practice-song";
  juce::String waveformRevision = "revision-1";
  std::vector<PracticePanel::Instrument> availableInstruments{
      {"piano", "Piano"}, {"bass", "Bass"}};
  struct WaveformRequest {
    juce::String instrument;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::function<void(std::vector<float>)> deliver;
  };
  std::vector<WaveformRequest> waveformRequests;
  bool deferWaveforms = false;
  std::function<void(const juce::String &)> afterSynchronousWaveform;
  juce::String signature = "4/4";
  juce::String key = "C";
  double bpm = 120.0;
  double playbackRate = 1.0;
  double position = 40.0;
  double length = 120.0;
  juce::Range<double> range{0.0, 120.0};
  int closeCalls = 0;
  int helpCalls = 0;
  bool playing = false;
  bool connected = true;
  bool remote = false;
  bool acknowledgeSeekImmediately = true;
  bool scrubFinishRejected = false;
  bool scrubLeaseActive = false;
  bool resumeAfterScrub = false;
  int pitch = 0;
  bool acknowledgePitchImmediately = true;
  double tempo = 120.0;
  bool acknowledgeTempoImmediately = true;
  std::vector<double> tempoChanges;
  bool acknowledgeLoopReleaseImmediately = true;
  juce::String loopIdentity;
  int loopCommandSequence = 0;
  std::function<double(double, bool)> snapLoopPosition;
  std::vector<std::pair<double, bool>> loopSnapRequests;
  bool loopActive = false;
  bool loopOwned = false;
  bool acceptLoopRequest = true;
  bool acknowledgeLoopImmediately = true;
  bool loopRequestRejected = false;
  juce::String loopError;
  PracticePanel::AccompanimentState guideState{"guide-1", {}, true, false};
  PracticePanel::AccompanimentState metronomeState{"metronome-1", {}, true, true};
  bool acknowledgeGuideImmediately = true;
  bool acknowledgeMetronomeImmediately = true;
  std::vector<bool> guideChanges;
  std::vector<bool> metronomeChanges;
  std::function<void(bool)> afterAccompanimentWrite;
  double minimumLoopDuration = 0.25;
  juce::Range<double> activeLoopRange;
  std::vector<int> pitchChanges;
  std::vector<juce::Range<double>> loopChanges;
  std::vector<double> loopPreparations;
  int releaseLoopCalls = 0;
  int playPauseCalls = 0;
  int stopCalls = 0;
  int beginScrubCalls = 0;
  int finishScrubCalls = 0;
  int pauseCalls = 0;
  int resumeCalls = 0;
  int finishListeningCalls = 0;
  int mode = 0;
  juce::String listeningInstrument;
  bool useConfirmedListening = false;
  int confirmedListeningMode = 0;
  juce::String confirmedListeningInstrument;
  std::vector<double> seeks;
  std::vector<int> listeningChanges;
  std::vector<juce::String> listeningTargets;
  std::vector<bool> scrubCancellations;
  std::unique_ptr<PracticePanel> panel;

  explicit Fixture(const bool deferredWaveforms = false,
                   const bool provideWaveformRevision = true)
      : deferWaveforms(deferredWaveforms) {
    PracticePanel::Sources sources;
    sources.songId = [this] { return songId; };
    if (provideWaveformRevision)
      sources.waveformRevision = [this] { return waveformRevision; };
    sources.title = [] { return juce::String("Practice fixture"); };
    sources.positionSeconds = [this] { return position; };
    sources.lengthSeconds = [this] { return length; };
    sources.songBpm = [this] { return bpm; };
    sources.songKey = [this] { return key; };
    sources.timeSignature = [this] { return signature; };
    sources.playbackRate = [this] { return playbackRate; };
    sources.pitchSemitones = [this] { return pitch; };
    sources.setPitchSemitones = [this](const int semitones) {
      pitchChanges.push_back(semitones);
      if (acknowledgePitchImmediately)
        pitch = semitones;
    };
    sources.tempoBpm = [this] { return tempo; };
    sources.setTempoBpm = [this](const double next) {
      tempoChanges.push_back(next);
      if (acknowledgeTempoImmediately)
        tempo = next;
    };
    sources.loopIdentity = [this] { return loopIdentity; };
    sources.snapLoopPosition = [this](const double position, const bool isEnd) {
      loopSnapRequests.emplace_back(position, isEnd);
      return snapLoopPosition ? snapLoopPosition(position, isEnd) : position;
    };
    sources.isLoopActive = [this] { return loopActive; };
    sources.loopRange = [this] { return activeLoopRange; };
    sources.setLoopRange = [this](const double start, const double end) {
      if (!acceptLoopRequest)
        return false;
      loopChanges.emplace_back(start, end);
      loopIdentity = "loop-" + juce::String(++loopCommandSequence);
      loopOwned = true;
      if (acknowledgeLoopImmediately) {
        loopActive = true;
        activeLoopRange = {start, end};
      }
      return true;
    };
    sources.prepareLoop = [this](const double start) {
      loopPreparations.push_back(start);
      loopIdentity = "prepare-" + juce::String(++loopCommandSequence);
      loopOwned = true;
    };
    sources.releaseLoop = [this] {
      ++releaseLoopCalls;
      loopOwned = false;
      if (acknowledgeLoopReleaseImmediately) {
        loopActive = false;
        activeLoopRange = {};
        loopIdentity.clear();
      }
    };
    sources.ownsLoop = [this] { return loopOwned; };
    sources.loopRequestRejected = [this] { return loopRequestRejected; };
    sources.loopError = [this] { return loopError; };
    sources.minimumLoopDuration = [this] { return minimumLoopDuration; };
    sources.seekRange = [this] { return range; };
    sources.seekToSeconds = [this](const double seconds) {
      seeks.push_back(seconds);
      if (acknowledgeSeekImmediately)
        position = seconds;
    };
    sources.beginScrub = [this] {
      ++beginScrubCalls;
      if (scrubLeaseActive)
        return;
      scrubLeaseActive = true;
      resumeAfterScrub = playing;
      if (playing) {
        ++pauseCalls;
        playing = false;
      }
    };
    sources.finishScrub = [this](const double seconds) {
      ++finishScrubCalls;
      if (!scrubLeaseActive)
        return false;
      seeks.push_back(seconds);
      if (acknowledgeSeekImmediately)
        position = seconds;
      scrubLeaseActive = false;
      if (resumeAfterScrub) {
        ++resumeCalls;
        playing = true;
      }
      resumeAfterScrub = false;
      return true;
    };
    sources.scrubFinishRejected = [this] { return scrubFinishRejected; };
    sources.cancelScrub = [this](const bool restorePlayback) {
      scrubCancellations.push_back(restorePlayback);
      if (scrubLeaseActive && restorePlayback && resumeAfterScrub) {
        ++resumeCalls;
        playing = true;
      }
      scrubLeaseActive = false;
      resumeAfterScrub = false;
    };
    sources.isPlaying = [this] { return playing; };
    sources.canControl = [this] { return connected; };
    sources.instruments = [this] { return availableInstruments; };
    sources.requestInstrumentWaveform =
        [this](const juce::String &id,
           std::shared_ptr<std::atomic<bool>> cancelled,
           std::function<void(std::vector<float>)> deliver) {
          waveformRequests.push_back({id, std::move(cancelled), deliver});
          if (!deferWaveforms) {
            const auto afterDelivery = afterSynchronousWaveform;
            deliver(std::vector<float>(512, 0.5f));
            if (afterDelivery)
              afterDelivery(id);
          }
        };
    sources.isRemote = [this] { return remote; };
    sources.listeningMode = [this] { return mode; };
    sources.listeningInstrumentId = [this] { return listeningInstrument; };
    sources.displayListeningMode = [this] {
      return useConfirmedListening ? confirmedListeningMode : mode;
    };
    sources.displayListeningInstrumentId = [this] {
      return useConfirmedListening ? confirmedListeningInstrument : listeningInstrument;
    };
    sources.setListeningMode = [this](const juce::String &id, const int next) {
      listeningInstrument = id;
      mode = next;
      listeningChanges.push_back(next);
      listeningTargets.push_back(id);
    };
    sources.canRetargetListening = [] { return true; };
    sources.finishListening = [this] { ++finishListeningCalls; };
    sources.guideState = [this] { return guideState; };
    sources.metronomeState = [this] { return metronomeState; };
    sources.setGuideEnabled = [this](bool enabled) {
      guideChanges.push_back(enabled);
      if (acknowledgeGuideImmediately)
        guideState.enabled = enabled;
      const auto afterWrite = afterAccompanimentWrite;
      if (afterWrite)
        afterWrite(true);
    };
    sources.setMetronomeEnabled = [this](bool enabled) {
      metronomeChanges.push_back(enabled);
      if (acknowledgeMetronomeImmediately)
        metronomeState.enabled = enabled;
      const auto afterWrite = afterAccompanimentWrite;
      if (afterWrite)
        afterWrite(false);
    };
    sources.close = [this] { ++closeCalls; };
    sources.showHelp = [this] { ++helpCalls; };
    sources.playPause = [this] { ++playPauseCalls; playing = !playing; };
    sources.stop = [this] {
      ++stopCalls;
      playing = false;
      position = range.getStart();
    };
    panel = std::make_unique<PracticePanel>(std::move(sources));
    panel->setSize(390, 844);
    panel->setVisible(true);
  }

  ~Fixture() {
    juce::PopupMenu::dismissAllActiveMenus();
    panel.reset();
  }

  void deliverWaveform(const size_t requestIndex, const float value) {
    expect(requestIndex < waveformRequests.size(), "waveform request exists");
    // Retain callbacks deliberately and ignore the cancellation flag to model
    // work that completed before cancellation reached its worker.
    const auto deliver = waveformRequests[requestIndex].deliver;
    deliver(std::vector<float>(512, value));
  }

  juce::Point<int> left() const {
    const auto bounds = PracticePanelGestureTestAccess::card(*panel);
    return {bounds.getX() + bounds.getWidth() / 4, bounds.getCentreY()};
  }

  juce::Point<int> right() const {
    const auto bounds = PracticePanelGestureTestAccess::card(*panel);
    return {bounds.getX() + bounds.getWidth() * 3 / 4, bounds.getCentreY()};
  }

  juce::Point<int> centre() const {
    return PracticePanelGestureTestAccess::card(*panel).getCentre();
  }

  void down(const juce::Point<int> point, const int clicks = 1,
            const juce::int64 eventTimeMs = 1000000) {
    panel->mouseDown(pointerEvent(*panel, point, point, clicks, false, eventTimeMs));
  }

  void drag(const juce::Point<int> start, const juce::Point<int> point,
            const int clicks = 1, const juce::int64 eventTimeMs = 1000000) {
    panel->mouseDrag(pointerEvent(*panel, point, start, clicks, true, eventTimeMs));
  }

  void up(const juce::Point<int> start, const juce::Point<int> point,
          const int clicks = 1, const bool dragged = false,
          const juce::int64 eventTimeMs = 1000000) {
    panel->mouseUp(pointerEvent(*panel, point, start, clicks, dragged, eventTimeMs));
  }

  void tap(const juce::Point<int> point, const int clicks = 1) {
    down(point, clicks);
    up(point, point, clicks);
  }

  void doubleTap(const juce::Point<int> point) {
    tap(point);
    tap(point, 2);
  }

  void swipe(const juce::Point<int> start, const juce::Point<int> end) {
    down(start);
    drag(start, end);
    up(start, end, 1, true);
  }

  void finishAnimation() {
    PracticePanelGestureTestAccess::finishAnimation(*panel);
  }

  void clickButton(juce::Button &button) {
    const auto point = button.getLocalBounds().getCentre();
    const auto event = pointerEvent(button, point, point);
    static_cast<juce::Component &>(button).mouseDown(event);
    static_cast<juce::Component &>(button).mouseUp(event);
  }
};

void expectWaveform(const PracticePanel &panel, const float value,
                    const char *message) {
  const auto &bins = PracticePanelGestureTestAccess::waveform(panel);
  expect(!bins.empty() && std::all_of(bins.begin(), bins.end(),
      [value](const float bin) { return bin == value; }), message);
}

void testWaveformRevisionInvalidatesSameDuration() {
  Fixture fixture(true);
  fixture.deliverWaveform(0, 0.2f);
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
  fixture.deliverWaveform(1, 0.6f);
  expect(PracticePanelGestureTestAccess::waveformCache(*fixture.panel).size() == 2,
         "both instruments are cached before editing the arrangement");
  expect(PracticePanelGestureTestAccess::waveformGeometrySize(*fixture.panel) > 0,
         "loaded waveform has envelope geometry");

  fixture.swipe(fixture.centre(), fixture.centre().translated(0, -60));
  expect(fixture.mode == 1 && fixture.listeningInstrument == "bass",
         "the selected instrument is focused before editing");
  PracticePanelGestureTestAccess::zoom(*fixture.panel, 2.0);
  const auto span = PracticePanelGestureTestAccess::visibleSpan(*fixture.panel);
  const auto start = fixture.centre();
  fixture.down(start);
  fixture.drag(start, start.translated(60, 0));
  const auto preview = PracticePanelGestureTestAccess::targetPosition(*fixture.panel);
  const auto generation = PracticePanelGestureTestAccess::waveformGeneration(*fixture.panel);
  const auto listeningChanges = fixture.listeningChanges.size();
  const auto scrubCancellations = fixture.scrubCancellations.size();

  fixture.waveformRevision = "same-length-reordered-sections";
  PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
  expect(fixture.waveformRequests.size() == 3 &&
             fixture.waveformRequests.back().instrument == "bass",
         "same song and duration request the selected instrument again on revision change");
  expect(fixture.waveformRequests[1].cancelled->load() &&
             PracticePanelGestureTestAccess::waveformGeneration(*fixture.panel) > generation,
         "a revision change cancels and supersedes the old request");
  expect(PracticePanelGestureTestAccess::waveform(*fixture.panel).empty() &&
             PracticePanelGestureTestAccess::waveformCache(*fixture.panel).empty() &&
             PracticePanelGestureTestAccess::waveformGeometrySize(*fixture.panel) == 0,
         "revision invalidation clears bins, every cached instrument and geometry");
  expect(PracticePanelGestureTestAccess::selectedInstrument(*fixture.panel) == "bass" &&
             fixture.mode == 1 && fixture.listeningInstrument == "bass" &&
             fixture.listeningChanges.size() == listeningChanges,
         "waveform-only invalidation preserves selection and active listening without retargeting");
  expect(PracticePanelGestureTestAccess::scrubIsActive(*fixture.panel) &&
             fixture.scrubCancellations.size() == scrubCancellations,
         "waveform-only invalidation does not cancel an ongoing scrub");
  expectNear(PracticePanelGestureTestAccess::targetPosition(*fixture.panel), preview,
             "waveform-only invalidation preserves the gesture preview");
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), span,
             "waveform-only invalidation preserves zoom");
  fixture.deliverWaveform(0, 0.9f);
  fixture.deliverWaveform(1, 0.9f);
  expect(PracticePanelGestureTestAccess::waveformCache(*fixture.panel).empty(),
         "old-revision deliveries cannot repopulate any instrument cache");
  fixture.deliverWaveform(2, 0.3f);
  expectWaveform(*fixture.panel, 0.3f, "new-revision waveform is displayed");
  PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
  expect(fixture.waveformRequests.size() == 3,
         "an unchanged revision reuses its waveform without another request");
}

void testWaveformDurationInvalidationWithoutRevisionProvider() {
  Fixture fixture(true, false);
  fixture.deliverWaveform(0, 0.2f);
  fixture.length = 180.0;
  PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
  expect(fixture.waveformRequests.size() == 2 &&
             fixture.waveformRequests[0].cancelled->load() &&
             PracticePanelGestureTestAccess::waveform(*fixture.panel).empty(),
         "a changed duration invalidates the waveform even without a revision provider");
  fixture.deliverWaveform(0, 0.9f);
  expect(PracticePanelGestureTestAccess::waveformCache(*fixture.panel).empty(),
         "old-duration bins cannot enter the new cache");
  fixture.deliverWaveform(1, 0.7f);
  expectWaveform(*fixture.panel, 0.7f, "new-duration bins are accepted");
}

void testWaveformRejectsChangedLiveSourcesBeforeProbe() {
  for (int change = 0; change < 4; ++change) {
    Fixture fixture(true);
    if (change == 0)
      fixture.waveformRevision = "edited-before-probe";
    else if (change == 1)
      fixture.length += 30.0;
    else if (change == 2)
      fixture.songId = "different-song";
    else
      fixture.remote = true;

    // No refresh/timer call: the displayed identity deliberately remains old.
    fixture.deliverWaveform(0, 0.9f);
    expect(PracticePanelGestureTestAccess::waveform(*fixture.panel).empty() &&
               PracticePanelGestureTestAccess::waveformCache(*fixture.panel).empty(),
           "live revision, duration, song and local/remote changes reject delivery before the next probe");
    PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
    expect(fixture.waveformRequests.size() == 2,
           "the next probe requests the new live context");
    fixture.deliverWaveform(1, 0.4f);
    expectWaveform(*fixture.panel, 0.4f, "the current context accepts its response");
    fixture.deliverWaveform(0, 0.9f);
    expectWaveform(*fixture.panel, 0.4f, "a late prior-context response cannot replace current bins");
  }
}

void testWaveformCacheHitCancelsPendingSelection() {
  Fixture fixture(true);
  fixture.deliverWaveform(0, 0.2f);
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
  const auto generation = PracticePanelGestureTestAccess::waveformGeneration(*fixture.panel);
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 0);
  expect(fixture.waveformRequests.size() == 2 &&
             fixture.waveformRequests[1].cancelled->load() &&
             PracticePanelGestureTestAccess::waveformGeneration(*fixture.panel) > generation,
         "switching to a cached instrument cancels and advances generation without another request");
  fixture.deliverWaveform(1, 0.9f);
  expectWaveform(*fixture.panel, 0.2f, "cancelled response cannot replace a cache hit");
  expect(PracticePanelGestureTestAccess::waveformCache(*fixture.panel).count("bass") == 0,
         "cancelled response cannot poison the unselected instrument cache");
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
  expect(fixture.waveformRequests.size() == 3,
         "returning to the cancelled instrument starts a fresh request");
  fixture.deliverWaveform(1, 0.9f);
  expect(PracticePanelGestureTestAccess::waveform(*fixture.panel).empty(),
         "an older generation for the now-selected instrument is still rejected");
  fixture.deliverWaveform(2, 0.6f);
  expectWaveform(*fixture.panel, 0.6f, "the latest instrument request wins");
  fixture.deliverWaveform(0, 0.9f);
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 0);
  expectWaveform(*fixture.panel, 0.2f, "late duplicates cannot overwrite an earlier valid cache entry");
}

void testWaveformSelectionChecksRevisionBeforeCacheHit() {
  Fixture fixture(true);
  fixture.deliverWaveform(0, 0.2f);
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
  fixture.deliverWaveform(1, 0.6f);
  fixture.waveformRevision = "edited-before-menu-selection";
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 0);
  expect(PracticePanelGestureTestAccess::selectedInstrument(*fixture.panel) == "piano" &&
             PracticePanelGestureTestAccess::waveform(*fixture.panel).empty() &&
             PracticePanelGestureTestAccess::waveformCache(*fixture.panel).empty(),
         "a selection before the next probe cannot reuse a previous-revision cache entry");
  expect(fixture.waveformRequests.back().instrument == "piano",
         "a selection across revision change requests the intended instrument");
  fixture.deliverWaveform(fixture.waveformRequests.size() - 1, 0.4f);
  expectWaveform(*fixture.panel, 0.4f, "selected instrument uses only the new revision");
}

void testWaveformCachePrunesRemovedInstruments() {
  Fixture fixture(true);
  fixture.deliverWaveform(0, 0.2f);
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
  fixture.deliverWaveform(1, 0.6f);
  fixture.availableInstruments.pop_back();
  PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
  expect(PracticePanelGestureTestAccess::waveformCache(*fixture.panel).size() == 1 &&
             PracticePanelGestureTestAccess::waveformCache(*fixture.panel).count("bass") == 0,
         "retired instruments are removed from the cache");
  expectWaveform(*fixture.panel, 0.2f, "removing selection chooses the remaining cached instrument");
  fixture.deliverWaveform(1, 0.9f);
  expect(PracticePanelGestureTestAccess::waveformCache(*fixture.panel).count("bass") == 0,
         "a retired instrument cannot return through a late delivery");
  fixture.availableInstruments.clear();
  PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
  expect(PracticePanelGestureTestAccess::waveformCache(*fixture.panel).empty() &&
             PracticePanelGestureTestAccess::waveform(*fixture.panel).empty() &&
             PracticePanelGestureTestAccess::selectedInstrument(*fixture.panel).isEmpty(),
         "removing every instrument clears the selection and waveform");
}

void testWaveformSynchronousReentryAndPanelLifetime() {
  Fixture reentrant;
  reentrant.afterSynchronousWaveform = [&](const juce::String &id) {
    if (id == "bass")
      PracticePanelGestureTestAccess::selectInstrument(*reentrant.panel, 0);
  };
  PracticePanelGestureTestAccess::selectInstrument(*reentrant.panel, 1);
  expect(PracticePanelGestureTestAccess::selectedInstrument(*reentrant.panel) == "piano" &&
             reentrant.waveformRequests[1].cancelled->load(),
         "a synchronous provider can re-enter selection without restoring the older request");
  reentrant.deliverWaveform(1, 0.9f);
  expectWaveform(*reentrant.panel, 0.5f, "reentrant selection keeps its waveform");
  expect(PracticePanelGestureTestAccess::waveformCache(*reentrant.panel).at("bass").front() == 0.5f,
         "a repeated synchronous delivery cannot poison cached bins after reentry");

  Fixture destroyed(true);
  const auto cancelled = destroyed.waveformRequests[0].cancelled;
  destroyed.panel.reset();
  expect(cancelled->load(), "destroying the panel cancels outstanding waveform work");
  destroyed.deliverWaveform(0, 0.9f);

  Fixture closesSynchronously;
  closesSynchronously.afterSynchronousWaveform = [&](const juce::String &id) {
    if (id == "bass")
      closesSynchronously.panel.reset();
  };
  PracticePanelGestureTestAccess::selectInstrument(*closesSynchronously.panel, 1);
  expect(closesSynchronously.panel == nullptr &&
             closesSynchronously.waveformRequests[1].cancelled->load(),
         "a synchronous provider may close the panel without a post-request member access");
  closesSynchronously.deliverWaveform(1, 0.9f);

  Fixture closesOnProbe;
  closesOnProbe.afterSynchronousWaveform = [&](const juce::String &) {
    closesOnProbe.panel.reset();
  };
  closesOnProbe.waveformRevision = "close-during-refresh";
  PracticePanelGestureTestAccess::probeCurrentSong(*closesOnProbe.panel);
  expect(closesOnProbe.panel == nullptr &&
             closesOnProbe.waveformRequests[1].cancelled->load(),
         "a synchronous provider may close the panel during its timer refresh");
  closesOnProbe.deliverWaveform(1, 0.9f);
}

double releaseTimedSwipe(Fixture &fixture, const int travelMilliseconds,
                         const int heldMilliseconds = 0,
                         const int direction = 1) {
  constexpr juce::int64 originTime = 1000000;
  const auto start = fixture.centre();
  fixture.down(start, 1, originTime);
  for (int step = 1; step <= 6; ++step)
    fixture.drag(start, start.translated(direction * step * 10, 0), 1,
                 originTime + travelMilliseconds * step / 6);
  const auto fingerTarget =
      PracticePanelGestureTestAccess::targetPosition(*fixture.panel);
  fixture.up(start, start.translated(direction * 60, 0), 1, true,
             originTime + travelMilliseconds + heldMilliseconds);
  return fingerTarget;
}

void testVelocityBasedInertiaAndPause() {
  Fixture slow;
  slow.playing = true;
  const auto slowFingerTarget = releaseTimedSwipe(slow, 2000);
  const auto slowTarget =
      PracticePanelGestureTestAccess::targetPosition(*slow.panel);
  expectNear(slowTarget, slowFingerTarget,
             "a swipe below the velocity threshold adds no inertial travel");

  Fixture fast;
  fast.playing = true;
  const auto fastFingerTarget = releaseTimedSwipe(fast, 40);
  const auto fastTarget =
      PracticePanelGestureTestAccess::targetPosition(*fast.panel);
  const auto fastDuration =
      PracticePanelGestureTestAccess::settleDuration(*fast.panel);
  expectNear(fastFingerTarget, slowFingerTarget,
             "fast and slow swipes move the finger across the same source distance");
  expect(fastTarget < slowTarget - 1.0 &&
             fastDuration > PracticePanelGestureTestAccess::settleDuration(*slow.panel) &&
             fastDuration <= 1600,
         "a faster swipe travels farther and brakes for longer within the duration cap");
  expect(!fast.playing && fast.pauseCalls == 1 && fast.seeks.empty() &&
             fast.finishScrubCalls == 0 && fast.resumeCalls == 0,
         "release keeps the original audio paused while inertia is active");
  PracticePanelGestureTestAccess::animateAtElapsed(*fast.panel, fastDuration - 1);
  expect(!fast.playing && fast.seeks.empty() && fast.resumeCalls == 0,
         "inertial motion cannot seek or resume before it has stopped");
  fast.finishAnimation();
  expect(fast.playing && fast.seeks.size() == 1 && fast.finishScrubCalls == 1 &&
             fast.resumeCalls == 1,
         "the inertial stop commits one seek and restores the previous playback state");
  expectNear(fast.seeks.back(), fastTarget,
             "playback resumes from the complete inertial destination");
  slow.finishAnimation();

  Fixture held;
  const auto heldFingerTarget = releaseTimedSwipe(held, 40, 120);
  expectNear(PracticePanelGestureTestAccess::targetPosition(*held.panel),
             heldFingerTarget,
             "holding the finger still before release cancels the flick's momentum");
  expect(PracticePanelGestureTestAccess::settleDuration(*held.panel) ==
             PracticePanelGestureTestAccess::settleDuration(*slow.panel),
         "a held release uses the ordinary short settling duration");
  held.finishAnimation();

  Fixture cappedA;
  Fixture cappedB;
  releaseTimedSwipe(cappedA, 1);
  releaseTimedSwipe(cappedB, 2);
  expectNear(PracticePanelGestureTestAccess::targetPosition(*cappedA.panel),
             PracticePanelGestureTestAccess::targetPosition(*cappedB.panel),
             "extreme touch velocities share a finite capped inertial destination");
  expect(PracticePanelGestureTestAccess::settleDuration(*cappedA.panel) ==
             PracticePanelGestureTestAccess::settleDuration(*cappedB.panel),
         "velocity spikes cannot keep increasing the braking duration");
  cappedA.finishAnimation();
  cappedB.finishAnimation();
}

void testInertiaFrameRateAndBounds() {
  Fixture frequent;
  Fixture sparse;
  releaseTimedSwipe(frequent, 40);
  releaseTimedSwipe(sparse, 40);
  const auto frequentOrigin =
      PracticePanelGestureTestAccess::displayedPosition(*frequent.panel);
  const auto sparseOrigin =
      PracticePanelGestureTestAccess::displayedPosition(*sparse.panel);
  const auto frequentTarget =
      PracticePanelGestureTestAccess::targetPosition(*frequent.panel);
  const auto sparseTarget =
      PracticePanelGestureTestAccess::targetPosition(*sparse.panel);
  const auto duration =
      PracticePanelGestureTestAccess::settleDuration(*frequent.panel);
  expectNear(frequentTarget, sparseTarget,
             "identical touch trajectories produce the same inertial destination");
  expect(duration == PracticePanelGestureTestAccess::settleDuration(*sparse.panel),
         "identical touch trajectories produce the same braking duration");
  int frequentElapsed = 8;
  int sparseElapsed = 50;
  for (const int checkpoint : {duration / 4, duration / 2, duration * 3 / 4}) {
    for (; frequentElapsed < checkpoint; frequentElapsed += 8)
      PracticePanelGestureTestAccess::animateAtElapsed(*frequent.panel, frequentElapsed);
    for (; sparseElapsed < checkpoint; sparseElapsed += 50)
      PracticePanelGestureTestAccess::animateAtElapsed(*sparse.panel, sparseElapsed);
    PracticePanelGestureTestAccess::animateAtElapsed(*frequent.panel, checkpoint);
    PracticePanelGestureTestAccess::animateAtElapsed(*sparse.panel, checkpoint);
    const auto frequentPosition =
        PracticePanelGestureTestAccess::displayedPosition(*frequent.panel);
    const auto sparsePosition =
        PracticePanelGestureTestAccess::displayedPosition(*sparse.panel);
    // Normalise only the initial filter lag, which can differ by one real
    // millisecond before release; the coast is driven at identical timestamps.
    expectNear((frequentPosition - frequentOrigin) / (frequentTarget - frequentOrigin),
               (sparsePosition - sparseOrigin) / (sparseTarget - sparseOrigin),
               "inertial progress depends on elapsed time rather than rendered frame count");
  }
  frequent.finishAnimation();
  sparse.finishAnimation();
  expectNear(frequent.seeks.back(), sparse.seeks.back(),
             "frequent and sparse animation frames commit the same final seek");

  for (const int direction : {-1, 1}) {
    Fixture bounded;
    bounded.position = direction > 0 ? 5.0 : bounded.length - 5.0;
    releaseTimedSwipe(bounded, 40, 0, direction);
    const auto target = PracticePanelGestureTestAccess::targetPosition(*bounded.panel);
    const auto stopAt = direction > 0 ? bounded.range.getStart() : bounded.range.getEnd();
    expectNear(target, stopAt,
               "inertia clamps its destination to either end of the playback range");
    const auto boundedDuration =
        PracticePanelGestureTestAccess::settleDuration(*bounded.panel);
    auto previous = PracticePanelGestureTestAccess::displayedPosition(*bounded.panel);
    for (int elapsed = 20; elapsed < boundedDuration; elapsed += 20) {
      PracticePanelGestureTestAccess::animateAtElapsed(*bounded.panel, elapsed);
      const auto position =
          PracticePanelGestureTestAccess::displayedPosition(*bounded.panel);
      expect(bounded.range.contains(position) || position == bounded.range.getEnd(),
             "the inertial waveform remains inside the playback range");
      expect(direction > 0 ? position <= previous + 0.00001
                           : position >= previous - 0.00001,
             "inertial braking reaches the boundary without reversing or bouncing");
      previous = position;
    }
    bounded.finishAnimation();
    expectNear(bounded.seeks.back(), stopAt,
               "a bounded flick seeks exactly to its clamped destination");
  }
}

void testNewTouchInterruptsInertia() {
  Fixture fixture;
  fixture.playing = true;
  releaseTimedSwipe(fixture, 40);
  PracticePanelGestureTestAccess::animateAtElapsed(
      *fixture.panel, PracticePanelGestureTestAccess::settleDuration(*fixture.panel) / 4);
  const auto beforeTouch =
      PracticePanelGestureTestAccess::displayedPosition(*fixture.panel);
  const auto start = fixture.centre();
  fixture.down(start, 1, 1000200);
  expect(!PracticePanelGestureTestAccess::isSettling(*fixture.panel) &&
             !fixture.playing && fixture.seeks.empty() &&
             fixture.beginScrubCalls == 1 && fixture.pauseCalls == 1,
         "a new touch interrupts inertia and retains the existing transport lease");
  expect(std::abs(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel) -
                  beforeTouch) < 0.1,
         "interrupting inertia keeps the waveform at its current visual position");
  fixture.drag(start, start.translated(-24, 0), 1, 1000250);
  fixture.up(start, start.translated(-24, 0), 1, true, 1000370);
  const auto newTarget =
      PracticePanelGestureTestAccess::targetPosition(*fixture.panel);
  fixture.finishAnimation();
  expect(fixture.seeks.size() == 1 && fixture.finishScrubCalls == 1 &&
             fixture.resumeCalls == 1 && fixture.playing,
         "only the replacement gesture commits and resumes the original audio");
  expectNear(fixture.seeks.back(), newTarget,
             "interrupting a flick discards its obsolete future destination");
}

void testSeekClockHandoffAndRejection() {
  for (const bool remote : {false, true}) {
    Fixture fixture;
    fixture.remote = remote;
    fixture.acknowledgeSeekImmediately = false;
    const auto oldPosition = fixture.position;
    fixture.swipe(fixture.centre(), fixture.centre().translated(60, 0));
    fixture.finishAnimation();
    const auto target = fixture.seeks.back();
    expect(PracticePanelGestureTestAccess::hasPendingSeek(*fixture.panel),
           "both local and remote scrubs retain preview ownership after their seek");
    for (const auto sourcePosition : {oldPosition, target, oldPosition, target}) {
      fixture.position = sourcePosition;
      PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 20);
      expectNear(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel),
                 target, "an old-target-old-target source clock cannot snap the waveform back");
      expect(PracticePanelGestureTestAccess::hasPendingSeek(*fixture.panel),
             "a brief matching source sample cannot release preview ownership early");
    }
    PracticePanelGestureTestAccess::agePendingSeek(*fixture.panel, 130);
    fixture.position = target + 0.03;
    PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 1);
    expect(!PracticePanelGestureTestAccess::hasPendingSeek(*fixture.panel),
           "two nearby source samples take ownership after the minimum hold interval");
    const auto immediatelyAfter =
        PracticePanelGestureTestAccess::displayedPosition(*fixture.panel);
    expect(std::abs(immediatelyAfter - target) < 0.001,
           "the source handoff starts from the displayed preview without a hard jump");
    PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 700);
    expectNear(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel),
               fixture.position, "the residual handoff converges smoothly to the source clock");
    expect(fixture.seeks.size() == 1 && fixture.finishScrubCalls == 1,
           "source-clock handoff never resends the seek");
  }

  Fixture rejected;
  rejected.playing = true;
  rejected.swipe(rejected.centre(), rejected.centre().translated(60, 0));
  rejected.scrubLeaseActive = false;
  rejected.finishAnimation();
  expect(rejected.seeks.empty() && rejected.resumeCalls == 0 &&
             !PracticePanelGestureTestAccess::hasPendingSeek(*rejected.panel),
         "a rejected scrub finish cannot seek, resume or retain a stale preview");
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*rejected.panel),
             rejected.position, "a rejected finish returns display ownership to the source");

  Fixture rejectedRemotely;
  rejectedRemotely.remote = true;
  rejectedRemotely.acknowledgeSeekImmediately = false;
  rejectedRemotely.swipe(rejectedRemotely.centre(),
                         rejectedRemotely.centre().translated(60, 0));
  rejectedRemotely.finishAnimation();
  rejectedRemotely.scrubFinishRejected = true;
  PracticePanelGestureTestAccess::advanceAnimation(*rejectedRemotely.panel, 1);
  expect(!PracticePanelGestureTestAccess::hasPendingSeek(*rejectedRemotely.panel),
         "a delayed server rejection releases the pending scrub preview");
}

void testContextChangeClearsResidualHandoff() {
  for (const bool startsRemote : {false, true}) {
    for (const int contextChange : {0, 1, 2}) {
      Fixture fixture;
      fixture.remote = startsRemote;
      fixture.acknowledgeSeekImmediately = false;
      fixture.swipe(fixture.centre(), fixture.centre().translated(60, 0));
      fixture.finishAnimation();
      fixture.position = fixture.seeks.back() + 0.03;
      PracticePanelGestureTestAccess::agePendingSeek(*fixture.panel, 130);
      PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 1);
      PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 1);
      expect(!PracticePanelGestureTestAccess::hasPendingSeek(*fixture.panel) &&
                 std::abs(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel) -
                          fixture.position) > 0.01,
             "the regression begins with a real residual offset after a confirmed seek");

      fixture.position = 12.0;
      if (contextChange == 0)
        fixture.songId = "new-song-after-handoff";
      else if (contextChange == 1)
        fixture.remote = !startsRemote;
      else {
        fixture.connected = false;
        PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 1);
        fixture.connected = true;
      }
      fixture.down(fixture.centre());
      expectNear(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel),
                 fixture.position,
                 "song, session and connection changes clear the previous handoff offset");
      expectNear(PracticePanelGestureTestAccess::targetPosition(*fixture.panel),
                 fixture.position,
                 "a new gesture starts from its current source clock without an old residual offset");
    }
  }
}

void testBarJumpsAndTransportState() {
  for (const bool playing : {false, true}) {
    for (const auto &entry :
         std::vector<std::pair<juce::String, double>>{{"4/4", 2.0},
                                                     {"6/8", 1.5},
                                                     {"7/8", 1.75}}) {
      for (const int direction : {-1, 1}) {
        Fixture fixture;
        fixture.playing = playing;
        fixture.signature = entry.first;
        fixture.playbackRate = 0.5;
        fixture.doubleTap(direction < 0 ? fixture.left() : fixture.right());
        expect(fixture.seeks.size() == 1,
               "a double tap requests one playback jump");
        expectNear(fixture.seeks.back(), 40.0 + direction * entry.second,
                   "bar jump follows the song's original BPM and meter");
        expect(fixture.playing == playing && fixture.playPauseCalls == 0 &&
                   fixture.stopCalls == 0 && fixture.listeningChanges.empty(),
               "bar jumps preserve play/pause and the listening mix");
      }
    }
  }
}

void testDragPreviewAndSingleCommit() {
  Fixture fixture;
  fixture.playing = true;
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel),
             fixture.position,
             "an idle waveform with no handoff offset has a finite source position");
  const auto start = fixture.centre();
  fixture.down(start);
  expect(fixture.playing && fixture.beginScrubCalls == 0,
         "touching the card does not pause playback before a horizontal drag");
  fixture.drag(start, start.translated(24, 0));
  PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 32);
  const auto firstPreview =
      PracticePanelGestureTestAccess::displayedPosition(*fixture.panel);
  if (!(firstPreview < 40.0 && fixture.seeks.empty() && !fixture.playing &&
        fixture.beginScrubCalls == 1 && fixture.pauseCalls == 1))
    std::cerr << "Drag state: preview=" << firstPreview
              << " target=" << PracticePanelGestureTestAccess::targetPosition(*fixture.panel)
              << " playing=" << fixture.playing << " begins=" << fixture.beginScrubCalls
              << " pauses=" << fixture.pauseCalls << " seeks=" << fixture.seeks.size()
              << '\n';
  expect(firstPreview < 40.0 && fixture.seeks.empty() && !fixture.playing &&
             fixture.beginScrubCalls == 1 && fixture.pauseCalls == 1,
         "horizontal drag pauses once and previews earlier audio without seeking");
  fixture.drag(start, start.translated(72, 0));
  const auto target =
      PracticePanelGestureTestAccess::targetPosition(*fixture.panel);
  PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 32);
  const auto preview =
      PracticePanelGestureTestAccess::displayedPosition(*fixture.panel);
  expect(target < preview && preview < firstPreview && fixture.seeks.empty() &&
             fixture.beginScrubCalls == 1 && fixture.pauseCalls == 1,
         "waveform follows the finger smoothly while the audio stays paused");
  fixture.up(start, start.translated(72, 0), 1, true);
  expect(fixture.seeks.empty() && !fixture.playing && fixture.resumeCalls == 0 &&
             PracticePanelGestureTestAccess::isSettling(*fixture.panel),
         "release begins settling without seeking or resuming playback");
  PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 70);
  const auto midway =
      PracticePanelGestureTestAccess::displayedPosition(*fixture.panel);
  expect(target < midway && midway < preview && fixture.seeks.empty() &&
             !fixture.playing && fixture.resumeCalls == 0,
         "settling moves toward the released target while playback remains paused");
  fixture.finishAnimation();
  expect(fixture.seeks.size() == 1 && fixture.finishScrubCalls == 1 &&
             fixture.playing && fixture.resumeCalls == 1,
         "settling completion seeks once and resumes the previously playing song");
  expectNear(fixture.seeks.back(), target,
             "settling commits the finger's final target rather than the lagging preview");
  const auto before = fixture.position;
  fixture.swipe(start, start.translated(-60, 0));
  expect(fixture.seeks.size() == 1 && !fixture.playing,
         "the next horizontal drag pauses again until its own settling completes");
  fixture.finishAnimation();
  expect(fixture.seeks.size() == 2 && fixture.seeks.back() > before,
         "leftward drag advances playback after settling");
  expect(fixture.playing && fixture.playPauseCalls == 0 &&
             fixture.pauseCalls == 2 && fixture.resumeCalls == 2 &&
             fixture.stopCalls == 0 && fixture.listeningChanges.empty(),
         "horizontal scrubbing restores playback and preserves the listening mix");
}

void testRemoteSeekPreviewLifecycle() {
  Fixture remote;
  remote.remote = true;
  remote.acknowledgeSeekImmediately = false;
  const auto start = remote.centre();
  remote.down(start);
  remote.drag(start, start.translated(60, 0));
  const auto preview = PracticePanelGestureTestAccess::targetPosition(*remote.panel);
  remote.up(start, start.translated(60, 0), 1, true);
  expect(remote.seeks.empty(),
         "remote release also waits for settling before sending its seek");
  remote.finishAnimation();
  expect(remote.seeks.size() == 1 && remote.position == 40.0,
         "remote release sends one seek while the server position is still stale");
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*remote.panel),
             preview, "remote preview remains visible after releasing the finger");
  pumpFor(80);
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*remote.panel),
             preview, "stale server state cannot snap the waveform back");
  remote.position = preview + 0.05;
  // An elapsed message-pump interval does not guarantee two timer samples.
  // Confirm the handoff explicitly before aging its newly-created residual.
  PracticePanelGestureTestAccess::agePendingSeek(*remote.panel, 130);
  PracticePanelGestureTestAccess::advanceAnimation(*remote.panel, 1);
  expect(PracticePanelGestureTestAccess::hasPendingSeek(*remote.panel),
         "one matching server sample cannot release the retained preview");
  PracticePanelGestureTestAccess::advanceAnimation(*remote.panel, 1);
  expect(!PracticePanelGestureTestAccess::hasPendingSeek(*remote.panel),
         "two matching server samples confirm the handoff after the hold interval");
  const auto initialHandoff =
      PracticePanelGestureTestAccess::displayedPosition(*remote.panel);
  expect(initialHandoff >= preview && initialHandoff < remote.position,
         "confirmed server position blends smoothly with the retained preview");
  PracticePanelGestureTestAccess::advanceAnimation(*remote.panel, 700);
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*remote.panel),
             remote.position, "confirmed server position takes over after the residual settles");
  expect(remote.seeks.size() == 1,
         "receiving server confirmation does not send another seek");

  Fixture disconnected;
  disconnected.remote = true;
  disconnected.acknowledgeSeekImmediately = false;
  disconnected.swipe(disconnected.centre(),
                     disconnected.centre().translated(-60, 0));
  disconnected.finishAnimation();
  expect(PracticePanelGestureTestAccess::displayedPosition(*disconnected.panel) >
             disconnected.position,
         "a remote seek initially keeps its advanced preview");
  disconnected.connected = false;
  pumpFor(80);
  expectNear(
      PracticePanelGestureTestAccess::displayedPosition(*disconnected.panel),
      disconnected.position, "disconnect clears the unconfirmed seek preview");
  expect(disconnected.seeks.size() == 1,
         "disconnect cannot resend the unconfirmed seek");

  Fixture changedSong;
  changedSong.remote = true;
  changedSong.acknowledgeSeekImmediately = false;
  changedSong.swipe(changedSong.centre(),
                    changedSong.centre().translated(60, 0));
  changedSong.finishAnimation();
  changedSong.songId = "new-remote-song";
  changedSong.position = 12.0;
  pumpFor(600);
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*changedSong.panel),
             changedSong.position, "a new song clears the old song's seek preview");
  expect(changedSong.seeks.size() == 1,
         "changing songs cannot resend the old song's pending seek");
}

void testPausedAndInterruptedSettling() {
  for (const bool initiallyPlaying : {false, true}) {
    Fixture fixture;
    fixture.playing = initiallyPlaying;
    const auto start = fixture.centre();
    fixture.swipe(start, start.translated(60, 0));
    expect(!fixture.playing && fixture.seeks.empty() &&
               fixture.beginScrubCalls == 1 && fixture.resumeCalls == 0 &&
               fixture.pauseCalls == (initiallyPlaying ? 1 : 0),
           "scrubbing remembers whether playback was active before the gesture");
    PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 50);
    fixture.down(start);
    fixture.drag(start, start.translated(-72, 0));
    expect(!fixture.playing && fixture.seeks.empty() &&
               fixture.beginScrubCalls == 1 && fixture.resumeCalls == 0,
           "a new drag interrupts settling and retains the existing pause lease");
    const auto finalTarget =
        PracticePanelGestureTestAccess::targetPosition(*fixture.panel);
    fixture.up(start, start.translated(-72, 0), 1, true);
    fixture.finishAnimation();
    expect(fixture.finishScrubCalls == 1 && fixture.seeks.size() == 1 &&
               fixture.playing == initiallyPlaying &&
               fixture.resumeCalls == (initiallyPlaying ? 1 : 0),
           "the final settling seeks once and resumes only an originally playing song");
    expectNear(fixture.seeks.back(), finalTarget,
               "an interrupted settling commits only the new drag's target");
    fixture.finishAnimation();
    expect(fixture.finishScrubCalls == 1 && fixture.seeks.size() == 1 &&
               fixture.resumeCalls == (initiallyPlaying ? 1 : 0),
           "later timer ticks cannot seek or resume again");
  }
}

void testScrubCancellationAndExplicitTransport() {
  Fixture stopped;
  stopped.playing = true;
  stopped.swipe(stopped.centre(), stopped.centre().translated(60, 0));
  stopped.clickButton(PracticePanelGestureTestAccess::stopButton(*stopped.panel));
  stopped.finishAnimation();
  expect(stopped.stopCalls == 1 && !stopped.playing && stopped.seeks.empty() &&
             stopped.finishScrubCalls == 0 && stopped.resumeCalls == 0 &&
             stopped.scrubCancellations == std::vector<bool>({false}),
         "Stop cancels settling without a delayed seek or automatic resume");

  Fixture explicitPlay;
  explicitPlay.playing = true;
  explicitPlay.swipe(explicitPlay.centre(),
                     explicitPlay.centre().translated(60, 0));
  explicitPlay.clickButton(
      PracticePanelGestureTestAccess::playButton(*explicitPlay.panel));
  explicitPlay.finishAnimation();
  expect(explicitPlay.playing && explicitPlay.playPauseCalls == 1 &&
             explicitPlay.seeks.empty() && explicitPlay.finishScrubCalls == 0 &&
             explicitPlay.resumeCalls == 0 &&
             explicitPlay.scrubCancellations == std::vector<bool>({false}),
         "explicit Play takes over transport and cancels the pending automatic resume");

  for (const int cancellation : {0, 1, 2}) {
    Fixture fixture;
    fixture.playing = true;
    fixture.swipe(fixture.centre(), fixture.centre().translated(60, 0));
    if (cancellation == 0)
      fixture.connected = false;
    else if (cancellation == 1)
      fixture.songId = "changed-while-settling";
    else
      fixture.remote = true;
    fixture.finishAnimation();
    expect(!fixture.playing && fixture.seeks.empty() &&
               fixture.finishScrubCalls == 0 && fixture.resumeCalls == 0 &&
               fixture.scrubCancellations == std::vector<bool>({false}),
           "disconnect, song change and session change cancel settling without resuming");
  }

  for (const bool initiallyPlaying : {false, true}) {
    Fixture closed;
    closed.playing = initiallyPlaying;
    closed.swipe(closed.centre(), closed.centre().translated(60, 0));
    closed.panel.reset();
    expect(closed.playing == initiallyPlaying && closed.seeks.empty() &&
               closed.finishScrubCalls == 0 &&
               closed.resumeCalls == (initiallyPlaying ? 1 : 0) &&
               closed.scrubCancellations == std::vector<bool>({true}),
           "closing practice releases its pause without committing an unfinished seek");
  }
}

void testAccumulatedBarFeedback() {
  Fixture repeated;
  repeated.doubleTap(repeated.right());
  expect(PracticePanelGestureTestAccess::accumulatedBars(*repeated.panel) == 1 &&
             PracticePanelGestureTestAccess::feedback(*repeated.panel).startsWith("+1"),
         "the first forward bar jump displays one positive bar");
  PracticePanelGestureTestAccess::setFeedbackRemainingMs(*repeated.panel, 100);
  repeated.doubleTap(repeated.right());
  expect(PracticePanelGestureTestAccess::accumulatedBars(*repeated.panel) == 2 &&
             PracticePanelGestureTestAccess::feedback(*repeated.panel).startsWith("+2") &&
             PracticePanelGestureTestAccess::feedbackRemainingMs(*repeated.panel) >= 1400,
         "same-side jumps accumulate and renew feedback for 1500 ms from the last jump");
  expectNear(repeated.position, 44.0,
             "two forward double taps still request two distinct one-bar seeks");
  repeated.doubleTap(repeated.left());
  expect(PracticePanelGestureTestAccess::accumulatedBars(*repeated.panel) == -1 &&
             PracticePanelGestureTestAccess::feedback(*repeated.panel).startsWith(
                 juce::String::fromUTF8("−1")),
         "changing direction starts a new negative feedback count");
  repeated.doubleTap(repeated.left());
  expect(PracticePanelGestureTestAccess::accumulatedBars(*repeated.panel) == -2 &&
             PracticePanelGestureTestAccess::feedback(*repeated.panel).startsWith(
                 juce::String::fromUTF8("−2")),
         "backward bar jumps accumulate in their own direction");
  PracticePanelGestureTestAccess::setFeedbackRemainingMs(*repeated.panel, -1);
  expect(!PracticePanelGestureTestAccess::hasBarFeedback(*repeated.panel),
         "bar feedback disappears once its last-jump timeout expires");
  repeated.doubleTap(repeated.left());
  expect(PracticePanelGestureTestAccess::accumulatedBars(*repeated.panel) == -1,
         "the next jump after timeout starts a new count");

  for (const bool backwards : {false, true}) {
    Fixture bounded;
    bounded.position = backwards ? 0.1 : bounded.length - 0.1;
    bounded.doubleTap(backwards ? bounded.left() : bounded.right());
    bounded.doubleTap(backwards ? bounded.left() : bounded.right());
    expect(PracticePanelGestureTestAccess::accumulatedBars(*bounded.panel) ==
               (backwards ? -1 : 1),
           "a jump blocked by the playback range cannot inflate its feedback count");
  }

  for (const int reset : {0, 1, 2, 3}) {
    Fixture fixture;
    fixture.doubleTap(fixture.right());
    fixture.doubleTap(fixture.right());
    if (reset == 0)
      fixture.swipe(fixture.centre(), fixture.centre().translated(60, 0));
    else if (reset == 1) {
      fixture.songId = "feedback-new-song";
      PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
    } else if (reset == 2)
      fixture.clickButton(PracticePanelGestureTestAccess::stopButton(*fixture.panel));
    else {
      fixture.connected = false;
      PracticePanelGestureTestAccess::advanceAnimation(*fixture.panel, 1);
    }
    expect(!PracticePanelGestureTestAccess::hasBarFeedback(*fixture.panel) &&
               PracticePanelGestureTestAccess::accumulatedBars(*fixture.panel) == 0,
           "drag, song change, Stop and disconnect reset accumulated bar feedback");
  }
}

void testHelpButtonTapAndDrag() {
  Fixture tap;
  tap.mode = 1;
  tap.listeningInstrument = "piano";
  PracticePanelGestureTestAccess::refreshFromSources(*tap.panel);
  auto &tapButton = PracticePanelGestureTestAccess::helpButton(*tap.panel);
  expect(tapButton.isVisible() && tapButton.isEnabled() &&
             tapButton.getWidth() == 44 && tapButton.getHeight() == 44 &&
             tapButton.getButtonText() == "?",
         "the footer exposes a real 44-point help button");
  const auto point = tapButton.getLocalBounds().getCentre();
  const auto event = pointerEvent(tapButton, point, point);
  static_cast<juce::Component &>(tapButton).mouseDown(event);
  tap.panel->mouseDown(event);
  static_cast<juce::Component &>(tapButton).mouseUp(event);
  tap.panel->mouseUp(event);
  expect(tap.helpCalls == 1,
         "tapping help requests the existing learning flow once");
  pumpFor(10);
  expect(tap.mode == 1 && tap.listeningChanges.empty() && tap.seeks.empty(),
         "opening or dismissing help does not restore the mix or seek");
  const auto second = pointerEvent(tapButton, point, point, 2);
  static_cast<juce::Component &>(tapButton).mouseDown(second);
  tap.panel->mouseDown(second);
  static_cast<juce::Component &>(tapButton).mouseUp(second);
  tap.panel->mouseUp(second);
  expect(tap.helpCalls == 2 && tap.seeks.empty() &&
             tap.listeningChanges.empty(),
         "a second help tap cannot leak into the waveform double-tap seek");
  pumpFor(10);

  for (const juce::Point<int> movement : {juce::Point<int>{14, 0},
                                         juce::Point<int>{0, 14},
                                         juce::Point<int>{0, -60}}) {
    Fixture drag;
    drag.mode = 1;
    drag.listeningInstrument = "piano";
    drag.playing = true;
    PracticePanelGestureTestAccess::refreshFromSources(*drag.panel);
    auto &button = PracticePanelGestureTestAccess::helpButton(*drag.panel);
    const auto start = button.getLocalBounds().getCentre();
    const auto end = start + movement;
    const auto down = pointerEvent(button, start, start);
    const auto release = pointerEvent(button, end, start, 1, true);
    static_cast<juce::Component &>(button).mouseDown(down);
    drag.panel->mouseDown(down);
    drag.panel->mouseDrag(release);
    // Exercise JUCE's actual button-before-listener release ordering.
    static_cast<juce::Component &>(button).mouseUp(release);
    drag.panel->mouseUp(release);
    drag.finishAnimation();
    expect(drag.helpCalls == 0 && drag.mode == 1 &&
               drag.listeningChanges.empty() && drag.seeks.empty() &&
               drag.beginScrubCalls == 0 && drag.pauseCalls == 0 && drag.playing,
           "dragging help opens no menu and changes neither mix nor transport");
  }

  Fixture footer;
  const auto area = PracticePanelGestureTestAccess::footer(*footer.panel);
  const auto left = juce::Point<int>(area.getX() + 20, area.getCentreY());
  footer.doubleTap(left);
  expect(footer.seeks.size() == 1 && footer.listeningChanges.empty(),
         "double tapping the footer outside help preserves the one-bar gesture");
  expectNear(footer.seeks.back(), 38.0,
             "left footer double tap moves exactly one bar backwards");
}

void testHelpCallbackLifetime() {
  // The integration callback may synchronously replace/destroy Practice.
  PracticePanel::Sources sources;
  std::unique_ptr<PracticePanel> panel;
  int calls = 0;
  sources.showHelp = [&] { ++calls; panel.reset(); };
  panel = std::make_unique<PracticePanel>(std::move(sources));
  const auto click = PracticePanelGestureTestAccess::helpButton(*panel).onClick;
  click();
  expect(calls == 1 && panel == nullptr, "help callback tolerates synchronous panel destruction");
}

void testRestoreGestureTargetsActiveInstrument() {
  for (const bool remote : {false, true})
    for (const int mode : {1, 2}) {
      Fixture fixture;
      fixture.remote = remote;
      fixture.mode = mode;
      fixture.listeningInstrument = "bass";
      PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
      PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
      const auto start = fixture.centre();
      fixture.swipe(start, start.translated(0, mode == 1 ? 60 : -60));
      expect(fixture.mode == 0 && fixture.listeningInstrument.isEmpty() &&
                 fixture.listeningChanges == std::vector<int>({0}),
             "selecting the active target then the opposite gesture restores in local and remote contexts");
      expect(fixture.seeks.empty() && fixture.guideChanges.empty() && fixture.metronomeChanges.empty(),
             "gesture restoration preserves position and accompaniment");
    }
}

void testRestoreKeyboardAndContext() {
  for (const int mode : {1, 2}) {
    Fixture fixture;
    fixture.mode = mode;
    fixture.listeningInstrument = "bass";
    PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
    const auto key = juce::KeyPress('r', juce::ModifierKeys{}, 'r');
    expect(fixture.panel->keyPressed(key) && fixture.mode == 0 &&
               fixture.listeningInstrument.isEmpty() &&
               fixture.listeningChanges == std::vector<int>({0}),
           "R restores an active mix even when another instrument is selected");
    expect(fixture.panel->keyPressed(key) && fixture.listeningChanges.size() == 1,
           "R in the normal mix does not send a redundant reset");
    expect(fixture.seeks.empty() && fixture.guideChanges.empty() &&
               fixture.metronomeChanges.empty() && fixture.playPauseCalls == 0,
           "Restore affects only the temporary listening mix");
  }
  for (const int context : {0, 1, 2, 3}) {
    Fixture fixture;
    fixture.mode = 1;
    fixture.listeningInstrument = "piano";
    if (context == 0)
      fixture.connected = false;
    else if (context == 1)
      fixture.songId = "replacement-before-keypress";
    else if (context == 2)
      fixture.remote = true;
    const auto modifiers = context == 3
        ? juce::ModifierKeys(juce::ModifierKeys::ctrlModifier) : juce::ModifierKeys{};
    fixture.panel->keyPressed(juce::KeyPress('r', modifiers, 'r'));
    expect(fixture.listeningChanges.empty(),
           "disconnected, stale-context and modified R cannot reset a different mix");
  }
}

void testSharedTransportLayout() {
  for (const int width : {320, 390, 768}) {
    Fixture fixture;
    fixture.panel->setSize(width, 844);
    const auto card = PracticePanelGestureTestAccess::card(*fixture.panel);
    const auto close = PracticePanelGestureTestAccess::closeButton(*fixture.panel).getBounds();
    const auto play = PracticePanelGestureTestAccess::playButton(*fixture.panel).getBounds();
    const auto stop = PracticePanelGestureTestAccess::stopButton(*fixture.panel).getBounds();
    const auto help = PracticePanelGestureTestAccess::helpButton(*fixture.panel).getBounds();
    const auto guide = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, true).getBounds();
    const auto metronome = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, false).getBounds();
    const int transportSize = width == 320 ? 44 : 56;
    expect(close.getWidth() == 44 && close.getHeight() == 44 &&
               help.getWidth() == 44 && help.getHeight() == 44 &&
               card.contains(help) &&
               PracticePanelGestureTestAccess::footer(*fixture.panel).contains(help),
           "Close and the in-card help keep 44-point targets on narrow and wide layouts");
    expect(play.getWidth() == transportSize && play.getHeight() == transportSize &&
               stop.getWidth() == transportSize && stop.getHeight() == transportSize &&
               play.getCentreY() == stop.getCentreY(),
           "shared Play and Stop retain matching responsive transport targets");
    const auto clear = PracticePanelGestureTestAccess::loopClearButton(*fixture.panel).getBounds();
    expect(stop.getX() - play.getRight() == (width == 320 ? 4 : 8) &&
               std::abs(play.getUnion(clear).getCentreX() - card.getCentreX()) <= 1,
           "the six-control transport row stays centered with consistent spacing");
    expect(guide.getHeight() == 44 && metronome.getHeight() == 44 &&
               guide.getWidth() == metronome.getWidth() && guide.getWidth() >= 132 &&
               guide.getY() == metronome.getY() && metronome.getX() - guide.getRight() == 12 &&
               !card.intersects(guide) && !card.intersects(metronome) &&
               guide.getBottom() <= play.getY() && metronome.getBottom() <= play.getY(),
           "equal-width Guide and Metronome occupy the sole row above transport");
    const auto hints = PracticePanelGestureTestAccess::hints(*fixture.panel);
    expect(card.contains(hints) && hints.getBottom() <=
               PracticePanelGestureTestAccess::waveformRuler(*fixture.panel).getY(),
           "A/B feedback remains in the existing hint strip above the ruler");
    fixture.mode = 1;
    fixture.listeningInstrument = "piano";
    PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
    fixture.panel->resized();
    expect(PracticePanelGestureTestAccess::card(*fixture.panel) == card &&
               PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, true).getBounds() == guide &&
               PracticePanelGestureTestAccess::helpButton(*fixture.panel).getBounds() == help,
           "activating a listening mode adds no status row and changes no geometry");
  }
}

void testAccompanimentIndependentControls() {
  Fixture fixture;
  auto &guide = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, true);
  auto &metronome = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, false);
  expect(guide.isEnabled() && metronome.isEnabled() &&
             !guide.getToggleState() && metronome.getToggleState(),
         "Guide and Metronome start from independent authoritative source states");
  expect(guide.isToggleable() && metronome.isToggleable() &&
             !guide.getClickingTogglesState() && !metronome.getClickingTogglesState() &&
             guide.getTitle().isNotEmpty() && metronome.getTitle().isNotEmpty() &&
             guide.getDescription().isNotEmpty() && metronome.getDescription().isNotEmpty(),
         "accompaniment buttons expose real checkable state and named accessibility descriptions");
  fixture.clickButton(guide);
  expect(fixture.guideChanges == std::vector<bool>({true}) &&
             fixture.metronomeChanges.empty() && guide.getToggleState() &&
             metronome.getToggleState(),
         "turning Guide on leaves the active Metronome untouched");
  fixture.clickButton(metronome);
  expect(fixture.guideChanges == std::vector<bool>({true}) &&
             fixture.metronomeChanges == std::vector<bool>({false}) &&
             guide.getToggleState() && !metronome.getToggleState(),
         "turning Metronome off leaves Guide on");
  fixture.mode = 2;
  fixture.listeningInstrument = "piano";
  PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
  fixture.clickButton(guide);
  expect(fixture.mode == 2 && fixture.listeningInstrument == "piano" &&
             fixture.listeningChanges.empty() && fixture.seeks.empty() &&
             fixture.loopChanges.empty() && fixture.pitchChanges.empty() &&
             fixture.tempoChanges.empty() && fixture.playPauseCalls == 0 &&
             fixture.stopCalls == 0,
         "accompaniment toggles do not modify listening, A/B, pitch, tempo or transport");
  fixture.metronomeState.enabled = true;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  expect(metronome.getToggleState() && !guide.getToggleState(),
         "changes from another controller are reflected without another command");
}

void testAccompanimentAsyncConfirmationAndTimeout() {
  for (const bool isGuide : {false, true}) {
    Fixture fixture;
    fixture.remote = true;
    fixture.acknowledgeGuideImmediately = false;
    fixture.acknowledgeMetronomeImmediately = false;
    PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
    auto &button = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, isGuide);
    auto &other = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, !isGuide);
    auto &state = isGuide ? fixture.guideState : fixture.metronomeState;
    auto &changes = isGuide ? fixture.guideChanges : fixture.metronomeChanges;
    const bool before = state.enabled;
    fixture.clickButton(button);
    expect(changes == std::vector<bool>({!before}) && !button.isEnabled() &&
               other.isEnabled() && button.getToggleState() == before &&
               PracticePanelGestureTestAccess::accompanimentPending(*fixture.panel, isGuide),
           "a remote request disables only its own button without optimistic checked state");
    expect(button.getDescription().containsIgnoreCase("confirm"),
           "pending remote control describes confirmation and its current actual state");
    // Also exercise the callback guard, rather than relying only on disabled hit testing.
    button.onClick();
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    expect(changes.size() == 1 && button.getToggleState() == before && !button.isEnabled(),
           "rapid repeat activation and a stale snapshot cannot queue duplicate or optimistic requests");
    state.enabled = !before;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    expect(button.isEnabled() && button.getToggleState() == !before &&
               !PracticePanelGestureTestAccess::accompanimentPending(*fixture.panel, isGuide),
           "the first confirming snapshot updates checked and releases pending immediately");
    fixture.clickButton(button);
    expect(!button.isEnabled() && button.getToggleState() == !before && changes.size() == 2,
           "the opposite request also keeps the last confirmed audible state");
    PracticePanelGestureTestAccess::ageAccompanimentRequest(*fixture.panel, isGuide, 2001);
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    expect(button.isEnabled() && button.getToggleState() == !before &&
               !PracticePanelGestureTestAccess::accompanimentPending(*fixture.panel, isGuide) &&
               PracticePanelGestureTestAccess::practiceFeedback(*fixture.panel).isNotEmpty(),
           "two-second timeout releases the button with actual state and existing-strip feedback");
    fixture.clickButton(button);
    expect(changes.size() == 3 && !button.isEnabled(),
           "an unconfirmed remote request can be retried after its timeout");
  }

  Fixture concurrent;
  concurrent.acknowledgeGuideImmediately = false;
  concurrent.acknowledgeMetronomeImmediately = false;
  auto &guide = PracticePanelGestureTestAccess::accompanimentButton(*concurrent.panel, true);
  auto &metronome = PracticePanelGestureTestAccess::accompanimentButton(*concurrent.panel, false);
  concurrent.clickButton(guide);
  concurrent.clickButton(metronome);
  expect(concurrent.guideChanges == std::vector<bool>({true}) &&
             concurrent.metronomeChanges == std::vector<bool>({false}) &&
             !guide.isEnabled() && !metronome.isEnabled(),
         "both controls may await their own independent confirmations");
  concurrent.guideState.enabled = true;
  PracticePanelGestureTestAccess::updateControls(*concurrent.panel);
  expect(guide.isEnabled() && guide.getToggleState() && !metronome.isEnabled() &&
             metronome.getToggleState(),
         "confirming Guide cannot acknowledge or change a pending Metronome");
}

void testAccompanimentContextAndUnavailable() {
  for (const bool isGuide : {false, true}) {
    for (const int context : {0, 1, 2, 3, 4}) {
      Fixture fixture;
      fixture.acknowledgeGuideImmediately = false;
      fixture.acknowledgeMetronomeImmediately = false;
      auto &button = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, isGuide);
      auto &state = isGuide ? fixture.guideState : fixture.metronomeState;
      auto &changes = isGuide ? fixture.guideChanges : fixture.metronomeChanges;
      fixture.clickButton(button);
      expect(PracticePanelGestureTestAccess::accompanimentPending(*fixture.panel, isGuide),
             "context regression starts with an actual pending accompaniment command");
      if (context == 0)
        state.identity = "replacement-source";
      else if (context == 1)
        fixture.songId = "replacement-song";
      else if (context == 2)
        fixture.remote = true;
      else if (context == 3)
        fixture.connected = false;
      else {
        state.available = false;
        state.unavailableReason = "Source deliberately unavailable";
      }
      PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
      expect(!PracticePanelGestureTestAccess::accompanimentPending(*fixture.panel, isGuide) &&
                 changes.size() == 1 &&
                 PracticePanelGestureTestAccess::practiceFeedback(*fixture.panel).isEmpty(),
             "source, song, session, disconnect or unavailable cancels pending without timeout error");
      if (context >= 3) {
        expect(!button.isEnabled() && !button.getToggleState() &&
                   button.getDescription().isNotEmpty(),
               "disconnected or unavailable accompaniment has no stale checked indicator");
        button.onClick();
        expect(changes.size() == 1,
               "a disabled or stale accompaniment callback cannot send another command");
        if (context == 4)
          expect(button.getDescription().contains(state.unavailableReason),
                 "source-specific unavailability is exposed to accessibility and tooltips");
        fixture.connected = true;
        state.available = true;
        PracticePanelGestureTestAccess::updateControls(*fixture.panel);
      }
      expect(button.isEnabled() && button.getToggleState() == state.enabled,
             "valid replacement or reconnection immediately resumes the authoritative source state");
    }

    for (const int changedBeforeClick : {0, 1, 2, 3}) {
      Fixture fixture;
      auto &button = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, isGuide);
      auto &state = isGuide ? fixture.guideState : fixture.metronomeState;
      auto &changes = isGuide ? fixture.guideChanges : fixture.metronomeChanges;
      if (changedBeforeClick == 0)
        state.identity = "source-changed-between-polls";
      else if (changedBeforeClick == 1)
        fixture.songId = "song-changed-between-polls";
      else if (changedBeforeClick == 2)
        fixture.remote = true;
      else
        fixture.connected = false;
      fixture.clickButton(button);
      expect(changes.empty(),
             "a tap intended for an old source cannot write after a live context change before polling");
    }
  }
}

void testAccompanimentDragAndCallbackLifetime() {
  for (const bool isGuide : {false, true}) {
    for (const juce::Point<int> motion : {juce::Point<int>{16, 0}, juce::Point<int>{0, 14}}) {
      Fixture fixture;
      auto &button = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, isGuide);
      const auto start = button.getLocalBounds().getCentre();
      const auto release = pointerEvent(button, start + motion, start, 1, true);
      static_cast<juce::Component &>(button).mouseDown(pointerEvent(button, start, start));
      fixture.panel->mouseDown(pointerEvent(button, start, start));
      fixture.panel->mouseDrag(release);
      static_cast<juce::Component &>(button).mouseUp(release);
      fixture.panel->mouseUp(release);
      expect(fixture.guideChanges.empty() && fixture.metronomeChanges.empty() &&
                 fixture.seeks.empty() && fixture.listeningChanges.empty() &&
                 fixture.beginScrubCalls == 0,
             "dragging within an accompaniment target cannot toggle, scrub or change listening");
    }
    Fixture closing;
    auto &button = PracticePanelGestureTestAccess::accompanimentButton(*closing.panel, isGuide);
    closing.afterAccompanimentWrite = [&](bool) { closing.panel.reset(); };
    // Use the real button release: it must not write to gestureRelease after
    // onClick synchronously destroys the panel and this child button.
    closing.clickButton(button);
    expect(closing.panel == nullptr &&
               (isGuide ? closing.guideChanges : closing.metronomeChanges).size() == 1,
           "a synchronous accompaniment provider may close the panel safely");
  }
}

juce::Image renderPracticePanel(PracticePanel &panel) {
  juce::Image image(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
  juce::Graphics g(image);
  panel.paint(g);
  return image;
}

int differentPixels(const juce::Image &a, const juce::Image &b,
                    juce::Rectangle<int> area) {
  expect(a.getBounds().contains(area) && b.getBounds().contains(area),
         "pixel comparison region lies inside both renders");
  int count = 0;
  for (int y = area.getY(); y < area.getBottom(); ++y)
    for (int x = area.getX(); x < area.getRight(); ++x)
      if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
        ++count;
  return count;
}

void testListeningGlowPixelsAndRemoteTarget() {
  for (const int width : {320, 390}) {
    for (const bool remote : {false, true}) {
      Fixture fixture;
      fixture.remote = remote;
      PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
      fixture.panel->setSize(width, 844);
      const auto card = PracticePanelGestureTestAccess::card(*fixture.panel);
      const auto top = juce::Rectangle<int>(card.getX() + 24, card.getY() - 6,
                                             card.getWidth() - 48, 6);
      const auto bottom = juce::Rectangle<int>(card.getX() + 24, card.getBottom(),
                                                card.getWidth() - 48, 6);
      const auto plot = PracticePanelGestureTestAccess::waveformPlot(*fixture.panel);
      const auto ruler = PracticePanelGestureTestAccess::waveformRuler(*fixture.panel);
      const auto originalBins = PracticePanelGestureTestAccess::waveform(*fixture.panel);
      const auto normal = renderPracticePanel(*fixture.panel);
      for (const int mode : {1, 2}) {
        fixture.mode = mode;
        fixture.listeningInstrument = "piano";
        PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
        expect(PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == mode,
               "selected local and remote target use the confirmed listening state");
        const auto active = renderPracticePanel(*fixture.panel);
        expect(differentPixels(normal, active, mode == 1 ? top : bottom) > 0 &&
                   differentPixels(normal, active, mode == 1 ? bottom : top) == 0,
               "focus changes only the top exterior and reduced only the bottom exterior");
        expect(differentPixels(normal, active, plot) == 0 &&
                   differentPixels(normal, active, ruler) == 0 &&
                   PracticePanelGestureTestAccess::waveform(*fixture.panel) == originalBins,
               "the listening glow cannot recolour, obscure or alter the waveform and ruler");
        PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
        const auto later = renderPracticePanel(*fixture.panel);
        expect(differentPixels(active, later, top) == 0 &&
                   differentPixels(active, later, bottom) == 0,
               "the exterior halo stays static across presentation refreshes");
        fixture.listeningInstrument = "bass";
        const auto other = renderPracticePanel(*fixture.panel);
        expect(PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 0 &&
                   differentPixels(normal, other, top) == 0 &&
                   differentPixels(normal, other, bottom) == 0,
               "another instrument's listening state never lights the selected card");
        fixture.listeningInstrument = "piano";
        fixture.connected = false;
        const auto disconnected = renderPracticePanel(*fixture.panel);
        expect(PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 0 &&
                   differentPixels(normal, disconnected, top) == 0 &&
                   differentPixels(normal, disconnected, bottom) == 0,
               "a stale active snapshot cannot leave a halo after disconnection");
        fixture.connected = true;
      }
      fixture.mode = 0;
      fixture.listeningInstrument.clear();
      const auto restored = renderPracticePanel(*fixture.panel);
      expect(differentPixels(normal, restored, top) == 0 &&
                 differentPixels(normal, restored, bottom) == 0,
             "returning to normal fully removes both exterior indicators");
      fixture.mode = 1;
      fixture.listeningInstrument = "piano";
      fixture.songId = "changed-before-paint";
      expect(PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 0,
             "a live song mismatch hides the old halo before the next context probe");
      fixture.songId = "practice-song";
      fixture.remote = !remote;
      expect(PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 0,
             "a live local/remote mismatch hides the old halo before the next context probe");
    }
  }
}

void testRemoteListeningGlowWaitsForConfirmation() {
  Fixture fixture;
  fixture.remote = true;
  fixture.useConfirmedListening = true;
  PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
  const auto card = PracticePanelGestureTestAccess::card(*fixture.panel);
  const auto top = juce::Rectangle<int>(card.getX() + 24, card.getY() - 6,
                                         card.getWidth() - 48, 6);
  const auto bottom = juce::Rectangle<int>(card.getX() + 24, card.getBottom(),
                                            card.getWidth() - 48, 6);
  const auto normal = renderPracticePanel(*fixture.panel);
  fixture.swipe(fixture.centre(), fixture.centre().translated(0, -60));
  expect(fixture.mode == 1 &&
             PracticePanelGestureTestAccess::listeningChangePending(*fixture.panel) &&
             PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 0 &&
             differentPixels(normal, renderPracticePanel(*fixture.panel), top) == 0,
         "a remote focus preview shows pending intent but no unconfirmed halo");
  // A rejected request rolls its optimistic session value back to the snapshot.
  fixture.mode = 0;
  fixture.listeningInstrument.clear();
  PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
  expect(!PracticePanelGestureTestAccess::listeningChangePending(*fixture.panel) &&
             differentPixels(normal, renderPracticePanel(*fixture.panel), top) == 0,
         "rejected focus stays at the confirmed normal appearance");
  fixture.swipe(fixture.centre(), fixture.centre().translated(0, -60));
  fixture.confirmedListeningMode = 1;
  fixture.confirmedListeningInstrument = "piano";
  PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
  expect(!PracticePanelGestureTestAccess::listeningChangePending(*fixture.panel) &&
             PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 1 &&
             differentPixels(normal, renderPracticePanel(*fixture.panel), top) > 0,
         "the confirming focus snapshot turns on the selected instrument's top halo");
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
  expect(fixture.listeningInstrument == "bass" &&
             PracticePanelGestureTestAccess::listeningChangePending(*fixture.panel) &&
             PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 0 &&
             differentPixels(normal, renderPracticePanel(*fixture.panel), top) == 0,
         "a pending retarget cannot apply Piano's confirmed halo to the selected Bass");
  fixture.confirmedListeningInstrument = "bass";
  PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
  expect(PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 1 &&
             differentPixels(normal, renderPracticePanel(*fixture.panel), top) > 0,
         "acknowledging the retarget lights Bass only when it is the real target");
  fixture.panel->keyPressed(juce::KeyPress('r', juce::ModifierKeys{}, 'r'));
  expect(PracticePanelGestureTestAccess::listeningChangePending(*fixture.panel) &&
             PracticePanelGestureTestAccess::visibleListeningMode(*fixture.panel) == 1,
         "pending Restore retains the last confirmed audible mode until its acknowledgement");
  fixture.confirmedListeningMode = 0;
  fixture.confirmedListeningInstrument.clear();
  PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
  expect(!PracticePanelGestureTestAccess::listeningChangePending(*fixture.panel) &&
             differentPixels(normal, renderPracticePanel(*fixture.panel), top) == 0 &&
             differentPixels(normal, renderPracticePanel(*fixture.panel), bottom) == 0,
         "confirmed normal clears both halo edges after Restore");

  Fixture rapid;
  rapid.remote = true;
  rapid.useConfirmedListening = true;
  PracticePanelGestureTestAccess::probeCurrentSong(*rapid.panel);
  rapid.swipe(rapid.centre(), rapid.centre().translated(0, -60));
  rapid.swipe(rapid.centre(), rapid.centre().translated(0, 60));
  rapid.swipe(rapid.centre(), rapid.centre().translated(0, 60));
  expect(rapid.listeningChanges == std::vector<int>({1, 0, 2}) &&
             PracticePanelGestureTestAccess::visibleListeningMode(*rapid.panel) == 0,
         "rapid remote swipes still use optimistic stepping without lighting an unconfirmed mode");
  rapid.confirmedListeningMode = 2;
  rapid.confirmedListeningInstrument = "piano";
  PracticePanelGestureTestAccess::refreshFromSources(*rapid.panel);
  expect(PracticePanelGestureTestAccess::visibleListeningMode(*rapid.panel) == 2,
         "the confirmed reduced snapshot activates only its bottom-halo mode");
}

void testLoopFeedbackUsesExistingHints() {
  Fixture fixture;
  const auto card = PracticePanelGestureTestAccess::card(*fixture.panel);
  const auto hints = PracticePanelGestureTestAccess::hints(*fixture.panel);
  const auto guide = PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, true).getBounds();
  const auto initial = renderPracticePanel(*fixture.panel);
  fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
  expect(PracticePanelGestureTestAccess::practiceFeedback(*fixture.panel).startsWith("A "),
         "A draft supplies feedback to the existing hint strip");
  const auto marked = renderPracticePanel(*fixture.panel);
  expect(differentPixels(initial, marked, hints) > 0 &&
             fixture.panel->getDescription() ==
                 PracticePanelGestureTestAccess::practiceFeedback(*fixture.panel),
         "A feedback is both painted in existing hints and exposed accessibly");
  fixture.position += 1.0;
  fixture.acknowledgeLoopImmediately = false;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  fixture.clickButton(PracticePanelGestureTestAccess::loopEndButton(*fixture.panel));
  fixture.loopError = "discontinuous-arrangement";
  fixture.loopRequestRejected = true;
  fixture.loopOwned = false;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  const auto error = PracticePanelGestureTestAccess::practiceFeedback(*fixture.panel);
  expect(error.contains("A") && error.containsChar('\n') &&
             fixture.panel->getDescription() == error,
         "the two-line A/B rejection is visible and accessible without a separate status row");
  const auto rejected = renderPracticePanel(*fixture.panel);
  expect(differentPixels(marked, rejected, hints) > 0 &&
             PracticePanelGestureTestAccess::card(*fixture.panel) == card &&
             PracticePanelGestureTestAccess::hints(*fixture.panel) == hints &&
             PracticePanelGestureTestAccess::accompanimentButton(*fixture.panel, true).getBounds() == guide,
         "A/B error changes the existing hints without moving card or accompaniment controls");
  fixture.loopRequestRejected = false;
  PracticePanelGestureTestAccess::expirePracticeFeedback(*fixture.panel);
  expect(PracticePanelGestureTestAccess::practiceFeedback(*fixture.panel) != error,
         "expired errors yield back to the retained A/B status");
}

void testFractionalDragCoordinates() {
  Fixture fixture;
  const auto start = fixture.centre().toFloat().translated(0.25f, 0.25f);
  fixture.panel->mouseDown(fractionalPointerEvent(*fixture.panel, start, start));
  const auto firstPoint = start.translated(12.25f, 0.0f);
  fixture.panel->mouseDrag(
      fractionalPointerEvent(*fixture.panel, firstPoint, start, 1, true));
  const auto firstTarget =
      PracticePanelGestureTestAccess::targetPosition(*fixture.panel);
  const auto secondPoint = start.translated(12.75f, 0.0f);
  fixture.panel->mouseDrag(
      fractionalPointerEvent(*fixture.panel, secondPoint, start, 1, true));
  const auto secondTarget =
      PracticePanelGestureTestAccess::targetPosition(*fixture.panel);
  expect(secondTarget < firstTarget && fixture.beginScrubCalls == 1 &&
             fixture.seeks.empty(),
         "a half-point movement after horizontal axis lock updates the raw preview");
  expectNear(firstTarget - secondTarget,
             (fixture.position - firstTarget) * 0.5 / 12.25,
             "fractional movement remains proportional without integer coordinate steps");
  fixture.panel->mouseUp(
      fractionalPointerEvent(*fixture.panel, secondPoint, start, 1, true));
  fixture.finishAnimation();
  expect(fixture.seeks.size() == 1,
         "fractional pointer movements still produce one completed scrub seek");
  expectNear(fixture.seeks.back(), secondTarget,
             "the completed seek retains the fractional final target");
}

void testFullWidthWaveformDragTimeMapping() {
  for (const auto [width, zoom] : {std::pair<int, double>{390, 1.0}, {320, 2.0}}) {
    Fixture fixture;
    fixture.panel->setSize(width, 844);
    PracticePanelGestureTestAccess::zoom(*fixture.panel, zoom);
    const auto card = PracticePanelGestureTestAccess::card(*fixture.panel);
    const auto plot = PracticePanelGestureTestAccess::waveformPlot(*fixture.panel);
    const auto ruler = PracticePanelGestureTestAccess::waveformRuler(*fixture.panel);
    expect(plot.getX() == card.getX() && plot.getRight() == card.getRight() &&
               ruler.getX() == plot.getX() && ruler.getRight() == plot.getRight(),
           "waveform and ruler share the card's complete horizontal timeline after resize");

    const auto start = plot.toFloat().getCentre();
    const auto finish = start.translated(static_cast<float>(card.getWidth()) * 0.25f, 0.0f);
    const double expected = fixture.position -
        PracticePanelGestureTestAccess::visibleSpan(*fixture.panel) * 0.25;
    constexpr juce::int64 eventTime = 1000000;
    fixture.panel->mouseDown(fractionalPointerEvent(*fixture.panel, start, start,
                                                   1, false, eventTime));
    fixture.panel->mouseDrag(fractionalPointerEvent(*fixture.panel, finish, start,
                                                   1, true, eventTime + 40));
    expectNear(PracticePanelGestureTestAccess::targetPosition(*fixture.panel), expected,
               "dragging a quarter of the visible card seeks a quarter of its source-time span");
    expect(fixture.seeks.empty(), "full-width dragging still previews without seeking during contact");

    // Holding the last location removes momentum, isolating the exact mapping
    // between the visible timeline and the source position committed on release.
    fixture.panel->mouseUp(fractionalPointerEvent(*fixture.panel, finish, start,
                                                 1, true, eventTime + 160));
    expectNear(PracticePanelGestureTestAccess::targetPosition(*fixture.panel), expected,
               "a held release preserves the full-width target without adding inertia");
    fixture.finishAnimation();
    expect(fixture.seeks.size() == 1,
           "full-width scrubbing commits exactly one seek after visual settling");
    expectNear(fixture.seeks.back(), expected,
               "the committed seek matches the full-width timeline at both sizes and zoom levels");
  }
}

void testPinchLatchAndScrubCancellation() {
  Fixture fixture;
  fixture.playing = true;
  const auto centre = fixture.centre().toFloat();
  const auto first = centre.translated(-40.0f, 0.0f);
  const auto second = centre.translated(40.0f, 0.0f);
  expect(!PracticePanelGestureTestAccess::touchDown(*fixture.panel, 0, first),
         "the first touch remains eligible for ordinary waveform gestures");
  fixture.panel->mouseDown(fractionalPointerEvent(*fixture.panel, first, first));
  expect(PracticePanelGestureTestAccess::touchDown(*fixture.panel, 1, second),
         "the second touch captures the waveform as a pinch");
  expect(PracticePanelGestureTestAccess::touchMove(
             *fixture.panel, 1, centre.translated(120.0f, 0.0f)),
         "moving the second finger is consumed by zoom");
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 6.0,
             "doubling the pinch distance halves the visible source duration");
  expect(PracticePanelGestureTestAccess::touchUp(*fixture.panel, 1),
         "lifting the second finger still consumes its release");
  expect(PracticePanelGestureTestAccess::touchMove(
             *fixture.panel, 0, first.translated(60.0f, -80.0f)),
         "one remaining finger cannot restart seeking or listening after a pinch");
  expect(PracticePanelGestureTestAccess::touchUp(*fixture.panel, 0),
         "the final finger release is consumed before clearing the pinch latch");
  fixture.finishAnimation();
  expect(fixture.playing && fixture.beginScrubCalls == 0 && fixture.pauseCalls == 0 &&
             fixture.seeks.empty() && fixture.listeningChanges.empty() &&
             !PracticePanelGestureTestAccess::hasBarFeedback(*fixture.panel),
         "a pinch changes neither transport nor mix and produces no tap, menu or seek");
  fixture.doubleTap(fixture.right());
  expect(fixture.seeks.size() == 1,
         "ordinary gestures resume only after every pinch finger has lifted");

  Fixture scrubbing;
  scrubbing.playing = true;
  const auto start = scrubbing.centre();
  PracticePanelGestureTestAccess::touchDown(*scrubbing.panel, 0, start.toFloat());
  scrubbing.down(start);
  scrubbing.drag(start, start.translated(60, 0));
  expect(!scrubbing.playing && scrubbing.pauseCalls == 1,
         "the cancellation regression begins with a real active scrub pause");
  expect(PracticePanelGestureTestAccess::touchDown(
             *scrubbing.panel, 1, start.toFloat().translated(-60.0f, 0.0f)),
         "a second finger takes over an existing one-finger scrub");
  expect(scrubbing.playing && scrubbing.resumeCalls == 1 &&
             scrubbing.scrubCancellations == std::vector<bool>({true}) &&
             scrubbing.seeks.empty(),
         "pinching cancels unfinished scrubbing and restores its original playback state");
  PracticePanelGestureTestAccess::touchUp(*scrubbing.panel, 1);
  PracticePanelGestureTestAccess::touchUp(*scrubbing.panel, 0);
  scrubbing.finishAnimation();
  expect(scrubbing.seeks.empty() && scrubbing.finishScrubCalls == 0 &&
             scrubbing.resumeCalls == 1,
         "a cancelled scrub cannot settle or resume a second time after zoom");
}

void testZoomLimitsAndSeekScale() {
  Fixture fixture;
  fixture.playing = true;
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 12.0,
             "practice opens with twelve source seconds visible");
  PracticePanelGestureTestAccess::zoom(*fixture.panel, 100.0);
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 2.0,
             "zoom in stops at the two-second minimum");
  PracticePanelGestureTestAccess::zoom(*fixture.panel, 0.001);
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 48.0,
             "zoom out stops at the forty-eight-second maximum");
  for (const auto invalid : {0.0, -1.0,
                             std::numeric_limits<double>::quiet_NaN()})
    PracticePanelGestureTestAccess::zoom(*fixture.panel, invalid);
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 48.0,
             "invalid magnification factors leave the zoom unchanged");
  expect(fixture.playing && fixture.seeks.empty() && fixture.beginScrubCalls == 0 &&
             fixture.pauseCalls == 0 && fixture.listeningChanges.empty(),
         "zoom alone preserves playback and the instrument mix");

  Fixture normal;
  Fixture enlarged;
  PracticePanelGestureTestAccess::zoom(*enlarged.panel, 2.0);
  normal.down(normal.centre());
  normal.drag(normal.centre(), normal.centre().translated(60, 0));
  enlarged.down(enlarged.centre());
  enlarged.drag(enlarged.centre(), enlarged.centre().translated(60, 0));
  const double normalTravel =
      normal.position - PracticePanelGestureTestAccess::targetPosition(*normal.panel);
  const double enlargedTravel =
      enlarged.position - PracticePanelGestureTestAccess::targetPosition(*enlarged.panel);
  expectNear(enlargedTravel * 2.0, normalTravel,
             "the same finger movement seeks half as far when the wave is twice as large");
  expect(normalTravel > 0.0 && normal.seeks.empty() && enlarged.seeks.empty(),
         "zoom-scaled dragging continues to preview without committing a seek");
}

void testPinchRebaselinesFingerMembership() {
  Fixture fixture;
  fixture.playing = true;
  const auto centre = fixture.centre().toFloat();
  const auto first = centre.translated(-40.0f, 0.0f);
  const auto second = centre.translated(40.0f, 0.0f);
  PracticePanelGestureTestAccess::touchDown(*fixture.panel, 1, first);
  fixture.panel->mouseDown(fractionalPointerEvent(*fixture.panel, first, first));
  PracticePanelGestureTestAccess::touchDown(*fixture.panel, 2, second);
  const auto spread = centre.translated(120.0f, 0.0f);
  PracticePanelGestureTestAccess::touchMove(*fixture.panel, 2, spread);
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 6.0,
             "the original fingers establish an enlarged six-second viewport");
  const auto third = centre.translated(20.0f, 0.0f);
  expect(PracticePanelGestureTestAccess::touchDown(*fixture.panel, 0, third),
         "adding the lower-index third finger remains within the pinch");
  PracticePanelGestureTestAccess::touchMove(*fixture.panel, 0, third);
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 6.0,
             "changing the tracked finger pair without movement cannot jump zoom");
  PracticePanelGestureTestAccess::touchMove(
      *fixture.panel, 0, centre.translated(80.0f, 0.0f));
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 3.0,
             "the replacement finger pair zooms relative to its current distance");
  PracticePanelGestureTestAccess::touchUp(*fixture.panel, 0);
  PracticePanelGestureTestAccess::touchMove(*fixture.panel, 2, spread);
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 3.0,
             "removing the third finger also reanchors the original pair without a jump");
  PracticePanelGestureTestAccess::touchMove(*fixture.panel, 2, second);
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*fixture.panel), 6.0,
             "the remaining pair continues smoothly from the reanchored zoom");
  PracticePanelGestureTestAccess::touchUp(*fixture.panel, 2);
  expect(PracticePanelGestureTestAccess::touchMove(*fixture.panel, 1, first),
         "the last remaining finger stays latched after the three-finger pinch");
  PracticePanelGestureTestAccess::touchUp(*fixture.panel, 1);
  expect(fixture.playing && fixture.pauseCalls == 0 && fixture.seeks.empty() &&
             fixture.listeningChanges.empty(),
         "finger membership changes alter zoom without playback or listening commands");
}

void testPitchControlsAndRemoteOptimism() {
  Fixture fixture;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  auto &selector = PracticePanelGestureTestAccess::pitchSelector(*fixture.panel);
  for (int press = 0; press < 13; ++press)
    selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expect(fixture.pitch == 12 && fixture.pitchChanges.size() == 12 &&
             selector.getValue() == 12.0,
         "pitch advances one semitone per key press and stops at plus twelve");
  for (int semitone = 0; semitone < 12; ++semitone)
    expect(fixture.pitchChanges[static_cast<size_t>(semitone)] == semitone + 1,
           "rapid local pitch changes cannot skip or combine semitone steps");
  for (int press = 0; press < 25; ++press)
    selector.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
  expect(fixture.pitch == -12 && fixture.pitchChanges.size() == 36 &&
             selector.getValue() == -12.0,
         "pitch descends through zero and stops at minus twelve");
  selector.keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
  expect(fixture.pitch == 0 && fixture.pitchChanges.back() == 0 &&
             selector.getValue() == 0.0,
         "Home restores the original key");
  expect(fixture.seeks.empty() && fixture.listeningChanges.empty() &&
             fixture.beginScrubCalls == 0 && fixture.playPauseCalls == 0,
         "pitch controls preserve position, playback intent and the listening mix");

  Fixture remote;
  remote.remote = true;
  remote.acknowledgePitchImmediately = false;
  PracticePanelGestureTestAccess::probeCurrentSong(*remote.panel);
  auto &remoteSelector = PracticePanelGestureTestAccess::pitchSelector(*remote.panel);
  for (int press = 0; press < 3; ++press)
    remoteSelector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expect(remote.pitch == 0 && remote.pitchChanges == std::vector<int>({1, 2, 3}) &&
             remoteSelector.getValue() == 3.0 &&
             PracticePanelGestureTestAccess::isPitchPending(*remote.panel),
         "remote rapid changes build on the requested pitch while server state is stale");
  remote.pitch = 1;
  PracticePanelGestureTestAccess::updateControls(*remote.panel);
  remoteSelector.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
  expect(remote.pitchChanges.back() == 2 && remoteSelector.getValue() == 2.0,
         "an older server acknowledgment cannot replace the latest optimistic pitch");
  remoteSelector.keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
  expect(remote.pitchChanges.back() == 0 && remoteSelector.getValue() == 0.0,
         "reset to original pitch uses the latest remote requested value");
  remote.pitch = 0;
  PracticePanelGestureTestAccess::updateControls(*remote.panel);
  expect(!PracticePanelGestureTestAccess::isPitchPending(*remote.panel),
         "the final server acknowledgment returns pitch presentation to the source");
}

void testPitchSelectorDragAndNames() {
  Fixture fixture;
  fixture.playing = true;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  auto &selector = PracticePanelGestureTestAccess::pitchSelector(*fixture.panel);
  expect(selector.getTextFromValue(-1) == "B" &&
             selector.getTextFromValue(0) == "C" &&
             selector.getTextFromValue(1) == "C#" &&
             selector.getTextFromValue(2) == "D",
         "pitch names follow the song key across the B to C octave boundary");
  selector.setOriginalKey("C#");
  expect(selector.getTextFromValue(0) == "C#" &&
             selector.getTextFromValue(1) == "D" &&
             selector.getTextFromValue(-2) == "B",
         "sharp original keys transpose by chromatic semitones");
  selector.setOriginalKey({});
  expect(selector.getTextFromValue(0) == "Original" &&
             selector.getTextFromValue(1).contains("+1") &&
             selector.getTextFromValue(-1).contains("-1"),
         "missing song key retains truthful original and semitone labels");
  selector.setOriginalKey("Cm");
  expect(selector.getTextFromValue(1) == "C#m", "minor mode is preserved");
  selector.setOriginalKey("CMaj");
  expect(selector.getTextFromValue(1) == "C#", "major suffix is not mistaken for minor");
  selector.setOriginalKey("C");

  const auto start = selector.getLocalBounds().getCentre();
  const auto finish = start.translated(-95, 0);
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseDrag(pointerEvent(selector, finish, start, 1, true));
  expect(fixture.pitchChanges.empty(),
         "drag previews notes without repeatedly changing the audio engine");
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  selector.mouseUp(pointerEvent(selector, finish, start, 1, true));
  expect(fixture.pitchChanges == std::vector<int>({2}) &&
             selector.getValue() == 2.0,
         "release snaps a fractional drag to one exact note in one command");
  pumpFor(80);
  expect(fixture.pitchChanges == std::vector<int>({2}) &&
             selector.getValue() == 2.0,
         "a settled pitch selection cannot overshoot or issue later changes");

  const auto lower = start.translated(44, 0);
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseDrag(pointerEvent(selector, lower, start, 1, true));
  selector.mouseUp(pointerEvent(selector, lower, start, 1, true));
  expect(fixture.pitchChanges == std::vector<int>({2, 1}),
         "dragging right lowers the pitch by one note");

  for (const int side : {-1, 1}) {
    pumpFor(180);
    const auto point = start.translated(44 * side, 0);
    selector.mouseDown(pointerEvent(selector, point, point));
    selector.mouseUp(pointerEvent(selector, point, point));
    expect(fixture.pitch == (side < 0 ? 0 : 1),
           "tapping either visible adjacent note selects that note");
  }
  pumpFor(180);
  const auto countBeforeCentreTap = fixture.pitchChanges.size();
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseUp(pointerEvent(selector, start, start));
  expect(fixture.pitchChanges.size() == countBeforeCentreTap && fixture.pitch == 1,
         "tapping the highlighted note leaves the selection unchanged");
  selector.mouseDoubleClick(pointerEvent(selector, start, start, 2));
  expect(fixture.pitch == 0 && selector.getValue() == 0.0,
         "double tapping the selector restores the original song key");
  expect(fixture.position == 40.0 && fixture.playing && fixture.seeks.empty() &&
             fixture.listeningChanges.empty() && fixture.beginScrubCalls == 0 &&
             fixture.playPauseCalls == 0,
         "pitch dragging never triggers waveform scrubbing or mix gestures");

  Fixture animating;
  auto &movingSelector = PracticePanelGestureTestAccess::pitchSelector(*animating.panel);
  PracticePanelGestureTestAccess::updateControls(*animating.panel);
  movingSelector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  const auto visibleSharp = movingSelector.getLocalBounds().getCentre().translated(44, 0);
  movingSelector.mouseDown(pointerEvent(movingSelector, visibleSharp, visibleSharp));
  movingSelector.mouseUp(pointerEvent(movingSelector, visibleSharp, visibleSharp));
  expect(animating.pitchChanges == std::vector<int>({1}),
         "tapping during the centering animation selects the visible note, not the future slot");

  Fixture coalesced;
  auto &coalescedSelector = PracticePanelGestureTestAccess::pitchSelector(*coalesced.panel);
  const auto origin = coalescedSelector.getLocalBounds().getCentre();
  coalescedSelector.mouseDown(pointerEvent(coalescedSelector, origin, origin));
  coalescedSelector.mouseUp(pointerEvent(coalescedSelector, origin.translated(-88, 0), origin));
  expect(coalesced.pitchChanges == std::vector<int>({2}),
         "release consumes the final drag position even when intermediate motion is coalesced");
}

void testPitchSelectorCancelledDrag() {
  for (const int context : {0, 1, 2, 3}) {
    Fixture fixture;
    auto &selector = PracticePanelGestureTestAccess::pitchSelector(*fixture.panel);
    const auto start = selector.getLocalBounds().getCentre();
    const auto finish = start.translated(-88, 0);
    selector.mouseDown(pointerEvent(selector, start, start));
    selector.mouseDrag(pointerEvent(selector, finish, start, 1, true));
    if (context == 0)
      fixture.songId = "song-after-pitch-preview";
    else if (context == 1)
      fixture.remote = true;
    else if (context == 2)
      fixture.connected = false;
    else
      selector.cancelDrag();
    if (context < 3)
      PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
    selector.mouseUp(pointerEvent(selector, finish, start, 1, true));
    expect(fixture.pitchChanges.empty() && selector.getValue() == 0.0,
           "context changes and cancelled drags discard the uncommitted note");
    expect(fixture.seeks.empty() && fixture.listeningChanges.empty(),
           "cancelling pitch input cannot affect waveform or listening state");
  }
}

void testTempoSelectorDragAndFractionalOriginal() {
  Fixture fixture;
  // JUCE exposes native accessibility only when a window handle exists.
  // Provide a hidden peer, as in the instrument-wheel accessibility fixture.
  fixture.panel->setVisible(false);
  fixture.panel->addToDesktop(juce::ComponentPeer::windowIsTemporary);
  fixture.playing = true;
  fixture.bpm = fixture.tempo = 120.5;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  auto &selector = PracticePanelGestureTestAccess::tempoSelector(*fixture.panel);
  expectNear(selector.getValue(), 120.5,
             "opening the tempo selector preserves fractional source BPM");
  expect(fixture.tempoChanges.empty() && selector.getTextFromValue(120.0) == "120 BPM" &&
             selector.getTextFromValue(120.5) == "120.5 BPM" &&
             selector.getTextFromValue(120.125) == "120.125 BPM",
         "tempo synchronization is silent and accessible values include BPM");
  expect(selector.getItemSpacing() > 44.0f,
         "fractional tempo labels receive room instead of clipping their decimals");
  auto *accessibility = selector.getAccessibilityHandler();
  expect(accessibility != nullptr && accessibility->getValueInterface() != nullptr &&
             accessibility->getValueInterface()->getCurrentValueAsString() == "120.5 BPM",
         "native accessibility announces the exact fractional tempo");
  pumpFor(180);
  const auto start = selector.getLocalBounds().getCentre();
  const auto finish = start.translated(juce::roundToInt(-2.0f * selector.getItemSpacing()), 0);
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseDrag(pointerEvent(selector, finish, start, 1, true));
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  expect(fixture.tempoChanges.empty() && selector.getValue() == 120.5,
         "tempo drag previews without repeatedly changing the audio engine");
  selector.mouseUp(pointerEvent(selector, finish, start, 1, true));
  expect(fixture.tempoChanges.size() == 1,
         "tempo release submits one snapped change");
  expectNear(fixture.tempo, 122.5,
             "two tempo wheel steps preserve the fractional BPM origin");
  pumpFor(180);
  const auto lower = start.translated(juce::roundToInt(selector.getItemSpacing()), 0);
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseUp(pointerEvent(selector, lower, start));
  expect(fixture.tempoChanges.size() == 2,
         "coalesced tempo movement is committed once at mouse release");
  expectNear(fixture.tempo, 121.5, "a one-item rightward drag lowers tempo by one BPM");
  selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expectNear(fixture.tempo, 122.5, "tempo keyboard input increases by exactly one BPM");
  selector.mouseDoubleClick(pointerEvent(selector, start, start, 2));
  expectNear(fixture.tempo, 120.5, "double tapping restores the exact fractional original tempo");
  selector.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
  selector.keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
  expectNear(fixture.tempo, 120.5, "Home also restores the exact original tempo");
  pumpFor(180);
  const auto right = start.translated(juce::roundToInt(selector.getItemSpacing()), 0);
  selector.mouseDown(pointerEvent(selector, right, right));
  selector.mouseUp(pointerEvent(selector, right, right));
  expectNear(fixture.tempo, 121.5, "tapping the adjacent tempo value selects one BPM higher");
  pumpFor(180);
  const auto changes = fixture.tempoChanges.size();
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseUp(pointerEvent(selector, start, start));
  expect(fixture.tempoChanges.size() == changes,
         "tapping the centered tempo does not emit a redundant audio change");
  expect(fixture.position == 40.0 && fixture.playing && fixture.seeks.empty() &&
             fixture.pitchChanges.empty() && fixture.listeningChanges.empty() &&
             fixture.playPauseCalls == 0 && fixture.beginScrubCalls == 0,
         "tempo gestures preserve transport position, pitch, playback intent and listening mix");
}

void testTempoLimitsAndRemoteOptimism() {
  for (const double original : {20.0, 125.0, 200.0}) {
    Fixture fixture;
    fixture.bpm = fixture.tempo = original;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    auto &selector = PracticePanelGestureTestAccess::tempoSelector(*fixture.panel);
    const double minimum = juce::jmax(20.0, original * 0.75);
    const double maximum = juce::jmin(300.0, original * 2.0);
    expectNear(selector.getMinimum(), minimum, "tempo minimum matches the audio engine");
    expectNear(selector.getMaximum(), maximum, "tempo maximum matches the audio engine");
    expect(fixture.tempoChanges.empty(), "configuring song tempo limits does not modify playback");
    pumpFor(180);
    const auto start = selector.getLocalBounds().getCentre();
    selector.mouseDown(pointerEvent(selector, start, start));
    selector.mouseUp(pointerEvent(selector, start.translated(20000, 0), start));
    expectNear(fixture.tempo, minimum, "tempo dragging reaches the exact minimum, including fractional bounds");
    const auto countAtMinimum = fixture.tempoChanges.size();
    selector.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    expect(fixture.tempoChanges.size() == countAtMinimum, "tempo cannot decrease beyond its lower limit");
    pumpFor(180);
    selector.mouseDown(pointerEvent(selector, start, start));
    selector.mouseUp(pointerEvent(selector, start.translated(-20000, 0), start));
    expectNear(fixture.tempo, maximum, "tempo dragging reaches the engine upper limit");
    const auto countAtMaximum = fixture.tempoChanges.size();
    selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    expect(fixture.tempoChanges.size() == countAtMaximum, "tempo cannot increase beyond its upper limit");
  }

  Fixture remote;
  remote.remote = true;
  remote.acknowledgeTempoImmediately = false;
  PracticePanelGestureTestAccess::probeCurrentSong(*remote.panel);
  auto &selector = PracticePanelGestureTestAccess::tempoSelector(*remote.panel);
  for (int step = 0; step < 3; ++step)
    selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expect(remote.tempo == 120.0 && remote.tempoChanges == std::vector<double>({121.0, 122.0, 123.0}) &&
             selector.getValue() == 123.0 && PracticePanelGestureTestAccess::isTempoPending(*remote.panel),
         "rapid remote tempo changes accumulate while the server snapshot is stale");
  remote.tempo = 121.0;
  PracticePanelGestureTestAccess::updateControls(*remote.panel);
  selector.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
  expectNear(remote.tempoChanges.back(), 122.0,
             "an older tempo acknowledgment cannot replace the latest optimistic selection");
  selector.keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
  expectNear(remote.tempoChanges.back(), 120.0, "remote reset targets the original tempo");
  remote.tempo = 120.0;
  PracticePanelGestureTestAccess::updateControls(*remote.panel);
  expect(!PracticePanelGestureTestAccess::isTempoPending(*remote.panel),
         "the final tempo acknowledgment returns presentation to the source");
}

void testTempoGestureCancellation() {
  for (const int context : {0, 1, 2, 3, 4}) {
    Fixture fixture;
    auto &selector = PracticePanelGestureTestAccess::tempoSelector(*fixture.panel);
    const auto start = selector.getLocalBounds().getCentre();
    const auto finish = start.translated(-88, 0);
    selector.mouseDown(pointerEvent(selector, start, start));
    selector.mouseDrag(pointerEvent(selector, finish, start, 1, true));
    if (context == 0)
      fixture.songId = "new-song-after-tempo-preview";
    else if (context == 1)
      fixture.remote = true;
    else if (context == 2)
      fixture.connected = false;
    else if (context == 3)
      selector.cancelDrag();
    else
      fixture.tempo = 135.25;
    if (context < 3)
      PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
    else if (context == 4)
      PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    selector.mouseUp(pointerEvent(selector, finish, start, 1, true));
    expect(fixture.tempoChanges.empty(),
           "context changes and authoritative tempo updates cancel uncommitted dragging");
    expectNear(selector.getValue(), fixture.tempo,
               "cancelled tempo input retains the current authoritative source value");
    expect(fixture.seeks.empty() && fixture.listeningChanges.empty(),
           "cancelling tempo input cannot alter waveform or listening state");
  }
}

void testDeletePracticeRegion() {
  for (const int state : {0, 1, 2}) {
    Fixture fixture;
    fixture.playing = true;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    auto &clear = PracticePanelGestureTestAccess::loopClearButton(*fixture.panel);
    expect(!clear.isEnabled(), "Delete starts disabled when no region exists");
    fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
    if (state > 0) {
      fixture.position += 1.0;
      PracticePanelGestureTestAccess::updateControls(*fixture.panel);
      fixture.clickButton(PracticePanelGestureTestAccess::loopEndButton(*fixture.panel));
    }
    if (state == 2)
      fixture.clickButton(PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel));
    expect(clear.isEnabled(), "Delete is available for an A draft, active loop or inactive A/B region");
    fixture.clickButton(clear);
    expect(PracticePanelGestureTestAccess::loopStart(*fixture.panel) < 0.0 &&
               PracticePanelGestureTestAccess::loopEnd(*fixture.panel) < 0.0 &&
               !PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel) &&
               !PracticePanelGestureTestAccess::isLoopPending(*fixture.panel) &&
               !fixture.loopActive && !fixture.loopOwned && fixture.releaseLoopCalls == 1,
           "Delete clears both markers and releases only the remaining owned loop or prefetch");
    expect(!clear.isEnabled(), "deleted regions disable Delete");
    expect(!PracticePanelGestureTestAccess::loopStartButton(*fixture.panel).getToggleState(),
           "deleted regions deselect A");
    expect(!PracticePanelGestureTestAccess::loopEndButton(*fixture.panel).isEnabled(),
           "deleted regions disable B");
    expect(!PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel).isEnabled(),
           "deleted regions disable Repeat");
    expect(!PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel).getToggleState(),
           "deleted regions deselect Repeat");
    expect(fixture.playing && fixture.playPauseCalls == 0 && fixture.stopCalls == 0 &&
               fixture.seeks.empty() && fixture.pitchChanges.empty() && fixture.tempoChanges.empty() &&
               fixture.listeningChanges.empty(),
           "clearing A/B preserves playback and the song's other controls");
    fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
    expect(PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel) && clear.isEnabled(),
           "a fresh A can be marked immediately after deleting a previous region");
  }
}

void testRemoteDeletedRegionCannotReappear() {
  for (const bool initiallyConfirmed : {false, true}) {
    Fixture fixture;
    fixture.remote = true;
    fixture.acknowledgeLoopImmediately = initiallyConfirmed;
    fixture.acknowledgeLoopReleaseImmediately = false;
    PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
    fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
    fixture.position += 1.0;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    fixture.clickButton(PracticePanelGestureTestAccess::loopEndButton(*fixture.panel));
    const auto deletedRange = fixture.loopChanges.back();
    const auto deletedIdentity = fixture.loopIdentity;
    fixture.clickButton(PracticePanelGestureTestAccess::loopClearButton(*fixture.panel));
    expect(fixture.releaseLoopCalls == 1 && !fixture.loopOwned,
           "Delete releases an active or pending remote loop once");
    fixture.loopActive = false;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    fixture.loopActive = true;
    fixture.activeLoopRange = deletedRange;
    fixture.loopIdentity = deletedIdentity;
    for (int snapshot = 0; snapshot < 3; ++snapshot)
      PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    expect(PracticePanelGestureTestAccess::loopStart(*fixture.panel) < 0.0 &&
               PracticePanelGestureTestAccess::loopEnd(*fixture.panel) < 0.0 &&
               !PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel).getToggleState(),
           "delayed snapshots cannot resurrect an active or pending A/B region after Delete");
    fixture.loopIdentity = "new-controller-loop";
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    expectNear(PracticePanelGestureTestAccess::loopStart(*fixture.panel), deletedRange.getStart(),
               "a genuinely new remote loop is visible even when its boundaries match the deleted region");
    expectNear(PracticePanelGestureTestAccess::loopEnd(*fixture.panel), deletedRange.getEnd(),
               "new loop identity restores the authoritative B marker");
    auto &clear = PracticePanelGestureTestAccess::loopClearButton(*fixture.panel);
    expect(!clear.isEnabled(), "the panel cannot delete a loop owned by another controller");
    fixture.clickButton(clear);
    expect(fixture.releaseLoopCalls == 1 && fixture.loopActive,
           "disabled Delete preserves a newer foreign loop");
  }
}

void testDeletingNewDraftDoesNotRevivePreviousLoop() {
  Fixture fixture;
  fixture.remote = true;
  PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
  fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
  fixture.position += 1.0;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  fixture.clickButton(PracticePanelGestureTestAccess::loopEndButton(*fixture.panel));
  const auto oldIdentity = fixture.loopIdentity;
  const auto oldRange = fixture.activeLoopRange;
  fixture.position += 1.0;
  fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
  expect(PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel),
         "a new A draft replaces the previous owned loop");
  fixture.clickButton(PracticePanelGestureTestAccess::loopClearButton(*fixture.panel));
  expect(fixture.releaseLoopCalls == 2,
         "replacing the old loop and deleting the new A each release only their own session");
  fixture.loopActive = true;
  fixture.loopOwned = false;
  fixture.loopIdentity = oldIdentity;
  fixture.activeLoopRange = oldRange;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  expect(PracticePanelGestureTestAccess::loopStart(*fixture.panel) < 0.0 &&
             PracticePanelGestureTestAccess::loopEnd(*fixture.panel) < 0.0 &&
             !PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel).getToggleState(),
         "a late snapshot of an older replaced loop cannot reappear after deleting a newer A draft");
}

void testLoopMarksUseSharedSnapping() {
  Fixture fixture;
  fixture.snapLoopPosition = [](const double position, bool) {
    return std::round(position * 2.0) / 2.0;
  };
  fixture.position = 40.26;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
  expectNear(PracticePanelGestureTestAccess::loopStart(*fixture.panel), 40.5,
             "A uses the pulse returned by the shared timeline snap callback");
  expectNear(fixture.loopPreparations.back(), 40.5,
             "A prefetch uses the snapped position rather than the unsnapped playhead");
  fixture.position = 40.74;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  auto &end = PracticePanelGestureTestAccess::loopEndButton(*fixture.panel);
  expect(!end.isEnabled(), "B remains disabled while snapping would select the same pulse as A");
  fixture.position = 40.76;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  fixture.clickButton(end);
  expect(fixture.loopChanges.size() == 1 && fixture.loopChanges.back() == juce::Range<double>(40.5, 41.0),
         "B activates the exact snapped A/B range from the shared tempo map");
  bool sawStart = false;
  bool sawEnd = false;
  for (const auto &[position, isEnd] : fixture.loopSnapRequests) {
    sawStart = sawStart || (!isEnd && std::abs(position - 40.26) < 0.00001);
    sawEnd = sawEnd || (isEnd && std::abs(position - 40.76) < 0.00001);
  }
  expect(sawStart && sawEnd, "snap callbacks identify A and B and receive their exact source positions");

  Fixture bounded;
  bounded.range = {0.0, 10.4};
  bounded.position = 10.3;
  bounded.snapLoopPosition = [&bounded](const double position, const bool isEnd) {
    const auto permitted = isEnd ? bounded.range : juce::Range<double>(
        bounded.range.getStart(),
        PracticeTimelineRuler::snapToPulse(bounded.range.getEnd(), bounded.bpm,
            bounded.signature, {}, bounded.range) - bounded.minimumLoopDuration);
    return PracticeTimelineRuler::snapToPulse(position, bounded.bpm, bounded.signature,
                                             {}, permitted);
  };
  bounded.clickButton(PracticePanelGestureTestAccess::loopStartButton(*bounded.panel));
  expectNear(PracticePanelGestureTestAccess::loopStart(*bounded.panel), 9.5,
             "A near an unaligned end leaves room for a valid later snapped B");
  bounded.position = 10.3;
  PracticePanelGestureTestAccess::updateControls(*bounded.panel);
  auto &boundedEnd = PracticePanelGestureTestAccess::loopEndButton(*bounded.panel);
  expect(boundedEnd.isEnabled(), "a valid ending pulse remains available after bounded A snapping");
  bounded.clickButton(boundedEnd);
  expect(bounded.loopChanges.size() == 1 &&
             bounded.loopChanges.back() == juce::Range<double>(9.5, 10.0),
         "the completed region ends on a pulse inside an unaligned playback window");

  Fixture unavailable;
  unavailable.snapLoopPosition = [](double, bool) {
    return std::numeric_limits<double>::quiet_NaN();
  };
  unavailable.clickButton(PracticePanelGestureTestAccess::loopStartButton(*unavailable.panel));
  expect(unavailable.loopPreparations.empty() &&
             PracticePanelGestureTestAccess::loopStart(*unavailable.panel) < 0.0,
         "an unavailable snap point cannot create an invalid A marker");
  unavailable.snapLoopPosition = [](const double position, const bool isEnd) {
    return isEnd ? std::numeric_limits<double>::quiet_NaN() : position;
  };
  unavailable.clickButton(PracticePanelGestureTestAccess::loopStartButton(*unavailable.panel));
  unavailable.position += 2.0;
  PracticePanelGestureTestAccess::updateControls(*unavailable.panel);
  expect(!PracticePanelGestureTestAccess::loopEndButton(*unavailable.panel).isEnabled() &&
             unavailable.loopChanges.empty(),
         "B stays unavailable when no valid pulse exists in its permitted window");
}

void testLoopMarkingActivationAndRelease() {
  Fixture fixture;
  fixture.position = 40.123456;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  auto &a = PracticePanelGestureTestAccess::loopStartButton(*fixture.panel);
  auto &b = PracticePanelGestureTestAccess::loopEndButton(*fixture.panel);
  auto &toggle = PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel);
  expect(!b.isEnabled() && !toggle.isEnabled(),
         "B and the loop toggle remain disabled until a valid A/B draft exists");
  fixture.clickButton(a);
  expectNear(PracticePanelGestureTestAccess::loopStart(*fixture.panel), fixture.position,
             "A preserves the exact current source time without rounding to seconds");
  expect(PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel) &&
             fixture.loopPreparations.size() == 1 && fixture.loopOwned &&
             !fixture.loopActive && fixture.loopChanges.empty() && !b.isEnabled(),
         "A prepares its source and starts an owned draft without activating an incomplete loop");
  expectNear(fixture.loopPreparations.back(), fixture.position,
             "A preloads the exact marked source position without altering playback");
  fixture.position -= 1.0;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  fixture.clickButton(b);
  expect(!b.isEnabled() && fixture.loopChanges.empty(),
         "B before A cannot activate a reversed loop");
  fixture.position = PracticePanelGestureTestAccess::loopStart(*fixture.panel) + 0.249;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  expect(!b.isEnabled(), "B shorter than a quarter second remains disabled");
  fixture.position = PracticePanelGestureTestAccess::loopStart(*fixture.panel) + 0.25;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  expect(b.isEnabled(), "B accepts the exact minimum loop duration");
  fixture.clickButton(b);
  expect(fixture.loopChanges.size() == 1 && fixture.loopActive && fixture.loopOwned &&
             toggle.getToggleState() &&
             !PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel),
         "a valid B completes A/B and activates the loop exactly once");
  const auto markedRange = fixture.loopChanges.back();
  fixture.clickButton(toggle);
  expect(!fixture.loopActive && fixture.releaseLoopCalls == 1 &&
             !toggle.getToggleState() && toggle.isEnabled(),
         "the loop control releases playback repetition while preserving a valid draft");
  expectNear(PracticePanelGestureTestAccess::loopStart(*fixture.panel), markedRange.getStart(),
             "switching loop off preserves marker A");
  expectNear(PracticePanelGestureTestAccess::loopEnd(*fixture.panel), markedRange.getEnd(),
             "switching loop off preserves marker B");
  fixture.clickButton(toggle);
  expect(fixture.loopActive && fixture.loopChanges.size() == 2 &&
             fixture.loopChanges.back() == markedRange,
         "the preserved markers can reactivate the same loop without remarking A/B");
  fixture.position = markedRange.getStart() + 0.1;
  fixture.clickButton(a);
  expect(fixture.releaseLoopCalls == 2 && !fixture.loopActive &&
             PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel) &&
             fixture.loopPreparations.size() == 2 &&
             PracticePanelGestureTestAccess::loopEnd(*fixture.panel) < 0.0 &&
             !b.isEnabled() && !toggle.isEnabled(),
         "marking a new A releases the owned loop and discards the previous B");
  expect(fixture.seeks.empty() && fixture.listeningChanges.empty() &&
             fixture.beginScrubCalls == 0 && fixture.playPauseCalls == 0,
         "A/B controls leave transport and the instrument mix to the shared session");
}

void testLoopCloseOwnershipAndRejection() {
  for (const bool ownsAtClose : {false, true}) {
    Fixture fixture;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
    fixture.position += 1.0;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    fixture.clickButton(PracticePanelGestureTestAccess::loopEndButton(*fixture.panel));
    fixture.loopOwned = ownsAtClose;
    fixture.panel.reset();
    expect(fixture.releaseLoopCalls == (ownsAtClose ? 1 : 0),
           "closing practice releases only a loop still owned by that panel");
    expect(fixture.loopActive == !ownsAtClose,
           "closing a panel cannot release an A/B loop now owned by another controller");
  }

  for (const bool ownsDraft : {false, true}) {
    Fixture draft;
    PracticePanelGestureTestAccess::updateControls(*draft.panel);
    draft.clickButton(PracticePanelGestureTestAccess::loopStartButton(*draft.panel));
    expect(draft.loopPreparations.size() == 1 && draft.loopChanges.empty() &&
               !draft.loopActive,
           "the close regression starts with a prepared A and no active loop");
    draft.loopOwned = ownsDraft;
    draft.panel.reset();
    expect(draft.releaseLoopCalls == (ownsDraft ? 1 : 0),
           "closing a prepared A draft releases only its own prefetch ownership");
  }

  Fixture buffered;
  buffered.minimumLoopDuration = 0.75;
  PracticePanelGestureTestAccess::updateControls(*buffered.panel);
  buffered.clickButton(PracticePanelGestureTestAccess::loopStartButton(*buffered.panel));
  buffered.position += 0.749;
  PracticePanelGestureTestAccess::updateControls(*buffered.panel);
  auto &bufferedEnd = PracticePanelGestureTestAccess::loopEndButton(*buffered.panel);
  expect(!bufferedEnd.isEnabled(),
         "B uses the larger duration required by the actual callback buffer");
  buffered.position = PracticePanelGestureTestAccess::loopStart(*buffered.panel) + 0.75;
  PracticePanelGestureTestAccess::updateControls(*buffered.panel);
  buffered.clickButton(bufferedEnd);
  expect(buffered.loopActive && buffered.loopChanges.size() == 1,
         "the dynamic minimum loop duration is accepted at its exact boundary");

  Fixture rejected;
  rejected.remote = true;
  rejected.acknowledgeLoopImmediately = false;
  PracticePanelGestureTestAccess::updateControls(*rejected.panel);
  rejected.clickButton(PracticePanelGestureTestAccess::loopStartButton(*rejected.panel));
  rejected.position += 1.0;
  PracticePanelGestureTestAccess::updateControls(*rejected.panel);
  rejected.clickButton(PracticePanelGestureTestAccess::loopEndButton(*rejected.panel));
  expect(PracticePanelGestureTestAccess::isLoopPending(*rejected.panel) &&
             PracticePanelGestureTestAccess::loopToggleButton(*rejected.panel).getToggleState(),
         "an accepted remote A/B request is shown while its server state is pending");
  rejected.loopRequestRejected = true;
  rejected.loopOwned = false;
  PracticePanelGestureTestAccess::updateControls(*rejected.panel);
  expect(!PracticePanelGestureTestAccess::isLoopPending(*rejected.panel) &&
             !PracticePanelGestureTestAccess::loopToggleButton(*rejected.panel).getToggleState(),
         "server rejection removes the optimistic loop state without a false active indicator");
  rejected.panel.reset();
  expect(rejected.releaseLoopCalls == 0,
         "a rejected remote loop cannot later emit an owned release at close");
}

void testPracticeControlsResetWithContext() {
  for (const int context : {0, 1, 2}) {
    Fixture fixture;
    fixture.acknowledgePitchImmediately = false;
    fixture.acknowledgeTempoImmediately = false;
    PracticePanelGestureTestAccess::updateControls(*fixture.panel);
    fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
    PracticePanelGestureTestAccess::pitchSelector(*fixture.panel).keyPressed(
        juce::KeyPress(juce::KeyPress::rightKey));
    PracticePanelGestureTestAccess::tempoSelector(*fixture.panel).keyPressed(
        juce::KeyPress(juce::KeyPress::rightKey));
    expect(PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel) &&
               PracticePanelGestureTestAccess::isPitchPending(*fixture.panel) &&
               PracticePanelGestureTestAccess::isTempoPending(*fixture.panel),
           "context reset starts with an A draft and pending pitch and tempo requests");
    if (context == 0)
      fixture.songId = "new-song-after-practice-controls";
    else if (context == 1)
      fixture.remote = true;
    else
      fixture.connected = false;
    PracticePanelGestureTestAccess::probeCurrentSong(*fixture.panel);
    expect(PracticePanelGestureTestAccess::loopStart(*fixture.panel) < 0.0 &&
               PracticePanelGestureTestAccess::loopEnd(*fixture.panel) < 0.0 &&
               !PracticePanelGestureTestAccess::isMarkingLoop(*fixture.panel) &&
               !PracticePanelGestureTestAccess::isPitchPending(*fixture.panel) &&
               !PracticePanelGestureTestAccess::isTempoPending(*fixture.panel) &&
               !PracticePanelGestureTestAccess::loopEndButton(*fixture.panel).isEnabled() &&
               !PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel).isEnabled(),
           "song, session and connection changes discard old drafts and optimistic control state");
    if (context == 2) {
      PracticePanelGestureTestAccess::pitchSelector(*fixture.panel).keyPressed(
          juce::KeyPress(juce::KeyPress::rightKey));
      PracticePanelGestureTestAccess::tempoSelector(*fixture.panel).keyPressed(
          juce::KeyPress(juce::KeyPress::rightKey));
      fixture.clickButton(PracticePanelGestureTestAccess::loopStartButton(*fixture.panel));
      expect(fixture.pitchChanges.size() == 1 && fixture.tempoChanges.size() == 1 &&
                 fixture.loopChanges.empty() &&
                 PracticePanelGestureTestAccess::loopStart(*fixture.panel) < 0.0,
             "disconnected controls cannot request pitch or tempo or start a new loop draft");
    }
  }
}

void testPracticeToolLayoutOnNarrowPhones() {
  for (const int width : {320, 393, 768}) {
    Fixture fixture;
    fixture.panel->setSize(width, 844);
    auto &pitch = PracticePanelGestureTestAccess::pitchSelector(*fixture.panel);
    auto &tempo = PracticePanelGestureTestAccess::tempoSelector(*fixture.panel);
    expect(pitch.getWidth() >= 132 && tempo.getWidth() >= 132 &&
               pitch.getHeight() >= 44 && tempo.getHeight() >= 44 &&
               pitch.getY() == tempo.getY() && pitch.getRight() < tempo.getX(),
           "pitch and tempo share one row with readable values and full-height touch targets");
    const std::vector<juce::Component *> dock{
        &PracticePanelGestureTestAccess::playButton(*fixture.panel),
        &PracticePanelGestureTestAccess::stopButton(*fixture.panel),
        &PracticePanelGestureTestAccess::loopStartButton(*fixture.panel),
        &PracticePanelGestureTestAccess::loopEndButton(*fixture.panel),
        &PracticePanelGestureTestAccess::loopToggleButton(*fixture.panel),
        &PracticePanelGestureTestAccess::loopClearButton(*fixture.panel)};
    const auto card = PracticePanelGestureTestAccess::card(*fixture.panel);
    for (size_t index = 0; index < dock.size(); ++index) {
      const auto bounds = dock[index]->getBounds();
      expect(bounds.getWidth() >= 44 && bounds.getHeight() >= 44 &&
                 fixture.panel->getLocalBounds().contains(bounds) &&
                 bounds.getY() >= card.getBottom() &&
                 bounds.getCentreY() == dock.front()->getBounds().getCentreY(),
             "Play, Stop, A, B, Repeat and Delete share one row below the waveform");
      if (index > 0)
        expect(dock[index - 1]->getRight() < bounds.getX(),
               "the six transport controls follow the requested order without overlap");
    }
    expect(card.getY() > pitch.getBottom() && card.getY() > tempo.getBottom() &&
               fixture.panel->getLocalBounds().contains(pitch.getBounds()) &&
               fixture.panel->getLocalBounds().contains(tempo.getBounds()),
           "both spinners remain above the waveform within the panel bounds");
  }
}

void testShortLoopClockWrapsImmediately() {
  Fixture fixture;
  fixture.playing = true;
  fixture.loopActive = true;
  fixture.activeLoopRange = {40.0, 40.25};
  double frameTime = 2000.0;
  fixture.position = 40.22;
  PracticePanelGestureTestAccess::updateFrame(*fixture.panel, frameTime);
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel), 40.22,
             "a quarter-second loop starts from its authoritative source time");
  for (int cycle = 0; cycle < 3; ++cycle) {
    fixture.position = 40.01;
    frameTime += 0.016;
    PracticePanelGestureTestAccess::updateFrame(*fixture.panel, frameTime);
    expectNear(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel), 40.01,
               "a short A/B wrap is presented immediately instead of holding the previous B");
    fixture.position = 40.22;
    frameTime += 0.21;
    PracticePanelGestureTestAccess::updateFrame(*fixture.panel, frameTime);
    for (int frame = 0; frame < 20; ++frame) {
      frameTime += 0.016;
      PracticePanelGestureTestAccess::updateFrame(*fixture.panel, frameTime);
      const auto displayed = PracticePanelGestureTestAccess::displayedPosition(*fixture.panel);
      expect(std::isfinite(displayed) && displayed >= 40.0 && displayed <= 40.25,
             "short-loop prediction never presents a waveform position beyond A/B");
    }
  }
  expect(fixture.seeks.empty() && fixture.playPauseCalls == 0 && fixture.pauseCalls == 0,
         "presenting natural A/B wraps sends no extra transport commands");
}

void testStopClearsRemoteSeekPreview() {
  Fixture fixture;
  fixture.remote = true;
  fixture.playing = true;
  fixture.acknowledgeSeekImmediately = false;
  fixture.doubleTap(fixture.right());
  expect(fixture.seeks.size() == 1 && fixture.position == 40.0 &&
             PracticePanelGestureTestAccess::displayedPosition(*fixture.panel) >
                 fixture.position &&
             PracticePanelGestureTestAccess::hasBarFeedback(*fixture.panel),
         "a remote bar jump retains preview and feedback while awaiting the server");
  auto &stopButton =
      PracticePanelGestureTestAccess::stopButton(*fixture.panel);
  const auto centre = stopButton.getLocalBounds().getCentre();
  const auto event = pointerEvent(stopButton, centre, centre);
  static_cast<juce::Component &>(stopButton).mouseDown(event);
  static_cast<juce::Component &>(stopButton).mouseUp(event);
  expect(fixture.stopCalls == 1 && !fixture.playing && fixture.seeks.size() == 1,
         "the actual Stop button stops once without resending the pending seek");
  expectNear(PracticePanelGestureTestAccess::displayedPosition(*fixture.panel),
             fixture.range.getStart(),
             "Stop immediately displays the stopped position instead of the pending seek");
  expect(!PracticePanelGestureTestAccess::hasBarFeedback(*fixture.panel),
         "Stop clears feedback left by the pending bar jump");
}

void testSeekBounds() {
  for (const bool activeLoop : {false, true}) {
    const auto range = activeLoop ? juce::Range<double>(20.0, 28.0)
                                  : juce::Range<double>(0.0, 120.0);
    const auto low = range.getStart();
    const auto high = range.getEnd();
    Fixture backwards;
    backwards.range = range;
    backwards.position = low + 0.1;
    backwards.doubleTap(backwards.left());
    expectNear(backwards.seeks.back(), low,
               "backward bar jump clamps to the available range start");
    Fixture forwards;
    forwards.range = range;
    forwards.position = high - 0.1;
    forwards.doubleTap(forwards.right());
    expectNear(forwards.seeks.back(), high,
               "forward bar jump clamps to the available range end");
    Fixture dragBackwards;
    dragBackwards.range = range;
    dragBackwards.position = (low + high) * 0.5;
    dragBackwards.swipe(dragBackwards.centre(),
                       dragBackwards.centre().translated(10000, 0));
    dragBackwards.finishAnimation();
    expectNear(dragBackwards.seeks.back(), low,
               "rightward drag respects the song or active loop start");
    Fixture dragForwards;
    dragForwards.range = range;
    dragForwards.position = (low + high) * 0.5;
    dragForwards.swipe(dragForwards.centre(),
                      dragForwards.centre().translated(-10000, 0));
    dragForwards.finishAnimation();
    expectNear(dragForwards.seeks.back(), high,
               "leftward drag respects the song or active loop end");
  }
}

void testGestureAxisAndWholeCardListening() {
  for (const double fraction : {0.05, 0.5, 0.95}) {
    Fixture fixture;
    fixture.playing = true;
    const auto bounds = PracticePanelGestureTestAccess::card(*fixture.panel);
    const juce::Point<int> start(
        bounds.getCentreX(),
        bounds.getY() + static_cast<int>(bounds.getHeight() * fraction));
    fixture.swipe(start, start.translated(0, -60));
    expect(fixture.mode == 1,
           "swiping up from the original mix focuses the selected instrument");
    fixture.swipe(start, start.translated(0, 60));
    expect(fixture.mode == 0 && fixture.listeningInstrument.isEmpty(),
           "swiping down from focus restores the original mix and clears its target");
    fixture.swipe(start, start.translated(0, 60));
    expect(fixture.mode == 2,
           "swiping down from the original mix reduces the selected instrument");
    fixture.swipe(start, start.translated(0, -60));
    expect(fixture.mode == 0 && fixture.listeningInstrument.isEmpty(),
           "swiping up from reduced returns to the original mix before focus");
    fixture.swipe(start, start.translated(0, -60));
    expect(fixture.listeningChanges == std::vector<int>({1, 0, 2, 0, 1}) &&
               fixture.seeks.empty() && fixture.playing &&
               fixture.beginScrubCalls == 0 && fixture.pauseCalls == 0,
           "the ordered three-state listening gesture works across the whole card without pausing audio");
  }

  Fixture fixture;
  const auto start = fixture.centre();
  fixture.down(start);
  fixture.drag(start, start.translated(12, 0));
  fixture.drag(start, start.translated(28, -110));
  fixture.up(start, start.translated(28, -110), 1, true);
  fixture.finishAnimation();
  expect(fixture.seeks.size() == 1 && fixture.listeningChanges.empty(),
         "horizontal axis lock prevents later diagonal motion changing volume");

  Fixture vertical;
  const auto verticalStart = vertical.centre();
  vertical.down(verticalStart);
  vertical.drag(verticalStart, verticalStart.translated(0, -60));
  vertical.drag(verticalStart, verticalStart.translated(110, -64));
  vertical.up(verticalStart, verticalStart.translated(110, -64), 1, true);
  expect(vertical.listeningChanges == std::vector<int>({1}) &&
             vertical.seeks.empty(),
         "vertical axis lock prevents later horizontal motion seeking");

  Fixture small;
  small.swipe(small.centre(), small.centre().translated(8, 0));
  expect(small.seeks.empty() && small.listeningChanges.empty() &&
             small.beginScrubCalls == 0,
         "small finger motion remains a tap instead of a scrub");

  Fixture diagonal;
  diagonal.swipe(diagonal.centre(), diagonal.centre().translated(60, 60));
  expect(diagonal.seeks.empty() && diagonal.listeningChanges.empty() &&
             diagonal.beginScrubCalls == 0,
         "ambiguous diagonal movement does not commit either gesture");
}

void testListeningGestureBoundsAndSingleStep() {
  for (const int initialMode : {1, 2}) {
    Fixture fixture;
    fixture.mode = initialMode;
    fixture.listeningInstrument = "piano";
    const auto start = fixture.centre();
    const int outward = initialMode == 1 ? -1 : 1;
    fixture.swipe(start, start.translated(0, outward * 60));
    fixture.swipe(start, start.translated(0, outward * 80));
    expect(fixture.listeningChanges.empty() && fixture.mode == initialMode,
           "swiping past a confirmed listening-state extreme sends no duplicate command");

    fixture.down(start);
    fixture.drag(start, start.translated(0, -outward * 60));
    fixture.drag(start, start.translated(0, -outward * 150));
    fixture.drag(start, start.translated(0, outward * 80));
    fixture.up(start, start.translated(0, outward * 80), 1, true);
    expect(fixture.listeningChanges == std::vector<int>({0}) &&
               fixture.listeningInstrument.isEmpty() && fixture.seeks.empty(),
           "one vertical gesture steps to centre only once despite more distance or reversal");
  }

  Fixture blocked;
  blocked.mode = 1;
  blocked.listeningInstrument = "piano";
  const auto start = blocked.centre();
  blocked.down(start);
  blocked.drag(start, start.translated(0, -70));
  blocked.drag(start, start.translated(0, 80));
  blocked.up(start, start.translated(0, 80), 1, true);
  expect(blocked.listeningChanges.empty() && blocked.mode == 1,
         "an outward gesture at the limit is consumed even if its finger later reverses");
}

void testListeningStateSelectionAndRestore() {
  Fixture fixture;
  const auto start = fixture.centre();
  fixture.swipe(start, start.translated(0, -60));
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 1);
  expect(fixture.listeningChanges == std::vector<int>({1, 1}) &&
             fixture.listeningTargets ==
                 std::vector<juce::String>({"piano", "bass"}),
         "changing instrument while focused preserves focus and retargets the selected instrument");
  fixture.swipe(fixture.centre(), fixture.centre().translated(0, 60));
  PracticePanelGestureTestAccess::selectInstrument(*fixture.panel, 0);
  expect(fixture.listeningChanges == std::vector<int>({1, 1, 0}) &&
             fixture.listeningInstrument.isEmpty(),
         "changing instrument after returning to centre keeps the normal mix without retargeting");
  fixture.swipe(fixture.centre(), fixture.centre().translated(0, 60));
  expect(fixture.listeningChanges.back() == 2 &&
             fixture.listeningTargets.back() == "piano",
         "the next downward swipe reduces the newly selected instrument");
  PracticePanelGestureTestAccess::refreshFromSources(*fixture.panel);
  fixture.panel->keyPressed(juce::KeyPress('r', juce::ModifierKeys{}, 'r'));
  expect(fixture.mode == 0 && fixture.listeningInstrument.isEmpty(),
         "Restore returns the reduced listening state to the normal centre");
  fixture.swipe(fixture.centre(), fixture.centre().translated(0, -60));
  expect(fixture.listeningChanges == std::vector<int>({1, 1, 0, 2, 0, 1}),
         "a new upward swipe after Restore starts from centre rather than a stale reduced state");

  for (const int otherMode : {1, 2}) {
    Fixture otherInstrument;
    otherInstrument.mode = otherMode;
    otherInstrument.listeningInstrument = "bass";
    otherInstrument.swipe(otherInstrument.centre(),
                          otherInstrument.centre().translated(0, 60));
    expect(otherInstrument.listeningChanges == std::vector<int>({2}) &&
               otherInstrument.listeningTargets.back() == "piano",
           "a different instrument's listening state does not shift the selected instrument's centre");
  }
}

void testCancelledAndDisconnectedGestures() {
  Fixture disconnected;
  disconnected.connected = false;
  disconnected.doubleTap(disconnected.left());
  disconnected.swipe(disconnected.centre(),
                     disconnected.centre().translated(80, 0));
  disconnected.swipe(disconnected.centre(),
                     disconnected.centre().translated(0, -60));
  expect(disconnected.seeks.empty() && disconnected.listeningChanges.empty(),
         "disconnected clients cannot send playback or listening commands");

  Fixture lostConnection;
  const auto start = lostConnection.centre();
  lostConnection.down(start);
  lostConnection.drag(start, start.translated(60, 0));
  lostConnection.connected = false;
  lostConnection.up(start, start.translated(60, 0), 1, true);
  expect(lostConnection.seeks.empty(),
         "losing connection before release cancels the pending seek");

  Fixture changedSong;
  const auto songStart = changedSong.centre();
  changedSong.down(songStart);
  changedSong.drag(songStart, songStart.translated(60, 0));
  changedSong.songId = "new-song";
  changedSong.up(songStart, songStart.translated(60, 0), 1, true);
  expect(changedSong.seeks.empty() && changedSong.listeningChanges.empty(),
         "a song change before release cancels the old song's gesture");

  Fixture changedBeforeListening;
  const auto listeningStart = changedBeforeListening.centre();
  changedBeforeListening.down(listeningStart);
  changedBeforeListening.songId = "new-song";
  changedBeforeListening.drag(listeningStart,
                             listeningStart.translated(0, -60));
  changedBeforeListening.up(listeningStart,
                           listeningStart.translated(0, -60), 1, true);
  expect(changedBeforeListening.seeks.empty() &&
             changedBeforeListening.listeningChanges.empty(),
         "a song change also cancels a pending vertical listening gesture");
}

void testDoubleTapDisambiguation() {
  Fixture halves;
  halves.tap(halves.left());
  halves.tap(halves.right(), 2);
  expect(halves.seeks.empty(),
         "taps in opposite halves do not become a one-bar jump");

  Fixture changedSong;
  changedSong.tap(changedSong.left());
  changedSong.songId = "new-song";
  changedSong.tap(changedSong.left(), 2);
  expect(changedSong.seeks.empty(),
         "a tap from a previous song cannot complete a double tap");

  for (const bool startsRemote : {false, true}) {
    Fixture changedMode;
    changedMode.remote = startsRemote;
    changedMode.tap(changedMode.left());
    changedMode.remote = !startsRemote;
    changedMode.tap(changedMode.left(), 2);
    expect(changedMode.seeks.empty(),
           "changing local/remote mode between taps cancels the double tap");
  }

  Fixture afterDrag;
  const auto start = afterDrag.centre();
  afterDrag.swipe(start, start.translated(80, 0));
  afterDrag.finishAnimation();
  afterDrag.tap(afterDrag.left(), 2);
  expect(afterDrag.seeks.size() == 1,
         "a click count following a drag cannot trigger a double-tap jump");

  Fixture header;
  auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*header.panel);
  const auto point = selector.getLocalBounds().getCentre();
  selector.mouseDown(pointerEvent(selector, point, point));
  selector.mouseUp(pointerEvent(selector, point, point));
  selector.mouseDown(pointerEvent(selector, point, point, 2));
  selector.mouseUp(pointerEvent(selector, point, point, 2));
  selector.mouseDoubleClick(pointerEvent(selector, point, point, 2));
  expect(header.seeks.empty(), "instrument taps never become waveform bar jumps");
  pumpFor(juce::MouseEvent::getDoubleClickTimeout() + 100);
  expect(!juce::PopupMenu::dismissAllActiveMenus(),
         "instrument selection no longer schedules a popup");
}

void testInstrumentWheelSelectionAndRetarget() {
  for (const bool revisionChanged : {false, true})
  for (const bool coalesced : {false, true}) {
    Fixture fixture;
    fixture.playing = true;
    auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*fixture.panel);
    const auto start = selector.getLocalBounds().toFloat().getCentre();
    const auto end = start.translated(-selector.getItemSpacing() * 0.75f, 0.0f);
    const auto requests = fixture.waveformRequests.size();
    selector.mouseDown(fractionalPointerEvent(selector, start, start));
    if (!coalesced) {
      selector.mouseDrag(fractionalPointerEvent(selector, end, start, 1, true));
      expect(selector.getSelectedInstrumentId() == "piano" &&
                 fixture.waveformRequests.size() == requests,
             "instrument wheel previews without changing the committed stem or fetching bins");
    }
    if (revisionChanged)
      fixture.waveformRevision = "edited-during-instrument-drag";
    selector.mouseUp(fractionalPointerEvent(selector, end, start, 1, true));
    expect(selector.getSelectedInstrumentId() == "bass" &&
               PracticePanelGestureTestAccess::selectedInstrument(*fixture.panel) == "bass" &&
               fixture.waveformRequests.size() == requests + 1,
           "normal and coalesced wheel releases select the actual instrument exactly once");
    expect(fixture.seeks.empty() && fixture.beginScrubCalls == 0 &&
               fixture.pauseCalls == 0 && fixture.playing && fixture.listeningChanges.empty(),
           "scrolling instruments in normal mix changes no playback, seek or listening state");
  }
  for (const int mode : {1, 2}) {
    Fixture fixture;
    fixture.swipe(fixture.centre(), fixture.centre().translated(0, mode == 1 ? -60 : 60));
    const auto changes = fixture.listeningChanges.size();
    auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*fixture.panel);
    selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    expect(fixture.mode == mode && fixture.listeningInstrument == "bass" &&
               fixture.listeningChanges.size() == changes + 1,
           "the existing focus/reduce mode retargets exactly once to the selected stable ID");
  }
}

void testInstrumentWheelVerticalAndAxisOwnership() {
  for (const bool coalesced : {false, true}) {
    Fixture fixture;
    auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*fixture.panel);
    const auto start = selector.getLocalBounds().getCentre();
    for (const int dy : {-60, 60, 60, -60}) {
      const auto end = start.translated(0, dy);
      selector.mouseDown(pointerEvent(selector, start, start));
      if (!coalesced)
        selector.mouseDrag(pointerEvent(selector, end, start, 1, true));
      selector.mouseUp(pointerEvent(selector, end, start, 1, true));
    }
    expect(fixture.listeningChanges == std::vector<int>({1, 0, 2, 0}) &&
               selector.getSelectedInstrumentId() == "piano" && fixture.seeks.empty() &&
               fixture.beginScrubCalls == 0,
           "vertical gestures from the wheel retain all three listening levels without selection or seeks");
  }
  Fixture horizontalFirst;
  auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*horizontalFirst.panel);
  const auto start = selector.getLocalBounds().getCentre();
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseDrag(pointerEvent(selector, start.translated(-8, 0), start, 1, true));
  selector.mouseUp(pointerEvent(selector, start.translated(-8, -60), start, 1, true));
  expect(horizontalFirst.listeningChanges.empty() && horizontalFirst.seeks.empty(),
         "a six-point horizontal wheel gesture cannot turn into a vertical listening gesture");

  Fixture verticalFirst;
  auto &vertical = PracticePanelGestureTestAccess::instrumentSelector(*verticalFirst.panel);
  const auto origin = vertical.getLocalBounds().getCentre();
  vertical.mouseDown(pointerEvent(vertical, origin, origin));
  vertical.mouseDrag(pointerEvent(vertical, origin.translated(0, -60), origin, 1, true));
  vertical.mouseUp(pointerEvent(vertical, origin.translated(-180, -60), origin, 1, true));
  expect(verticalFirst.listeningChanges == std::vector<int>({1}) &&
             vertical.getSelectedInstrumentId() == "piano" && verticalFirst.seeks.empty(),
         "a vertical listening gesture consumes the entire wheel gesture even after turning sideways");
}

void testInstrumentWheelListAndContextChanges() {
  Fixture reordered;
  auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*reordered.panel);
  std::reverse(reordered.availableInstruments.begin(), reordered.availableInstruments.end());
  // No timer probe: the visible index one still means Bass when the key arrives.
  selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expect(selector.getSelectedInstrumentId() == "bass" && selector.getValue() == 0.0 &&
             PracticePanelGestureTestAccess::selectedInstrument(*reordered.panel) == "bass",
         "reordering sources between probes maps the wheel's selected ID instead of a stale index");
  reordered.availableInstruments.erase(reordered.availableInstruments.begin());
  PracticePanelGestureTestAccess::refreshWaveform(*reordered.panel);
  expect(selector.getSelectedInstrumentId() == "piano", "removing the selection shows the remaining instrument");
  reordered.availableInstruments.clear();
  PracticePanelGestureTestAccess::refreshWaveform(*reordered.panel);
  expect(!selector.isEnabled() && selector.getSelectedInstrumentId().isEmpty(),
         "an empty instrument list disables the wheel and clears its identity");
  reordered.availableInstruments = {{"single", "Only instrument"}};
  PracticePanelGestureTestAccess::refreshWaveform(*reordered.panel);
  selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expect(selector.getValue() == 0.0 && selector.getSelectedInstrumentId() == "single",
         "a single-instrument wheel stays in range");

  for (int change = 0; change < 5; ++change) {
    Fixture fixture;
    auto &wheel = PracticePanelGestureTestAccess::instrumentSelector(*fixture.panel);
    const auto start = wheel.getLocalBounds().toFloat().getCentre();
    const auto end = start.translated(-wheel.getItemSpacing(), 0.0f);
    wheel.mouseDown(fractionalPointerEvent(wheel, start, start));
    wheel.mouseDrag(fractionalPointerEvent(wheel, end, start, 1, true));
    if (change == 0) fixture.songId = "new-song";
    if (change == 1) fixture.remote = true;
    if (change == 2) fixture.connected = false;
    if (change == 3) {
      std::reverse(fixture.availableInstruments.begin(), fixture.availableInstruments.end());
      PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
    }
    if (change == 4) fixture.panel->setSize(320, 844);
    wheel.mouseUp(fractionalPointerEvent(wheel, end, start, 1, true));
    expect(PracticePanelGestureTestAccess::selectedInstrument(*fixture.panel) == "piano" &&
               fixture.seeks.empty() && fixture.listeningChanges.empty(),
           "song/mode/disconnect/list/resize changes cancel the previous wheel gesture");
  }
}

void testInstrumentWheelPinchAndMagnify() {
  for (const bool selectorFirst : {false, true}) {
    for (const bool selectorReleasedFirst : {false, true}) {
      Fixture fixture;
      auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*fixture.panel);
      const auto point = selector.getLocalBounds().getCentre();
      const auto header = selector.getBounds().toFloat().getCentre();
      const auto wave = fixture.centre().toFloat();
      if (!selectorFirst)
        PracticePanelGestureTestAccess::touchDown(*fixture.panel, 1, wave);
      PracticePanelGestureTestAccess::touchDown(*fixture.panel, 0, header);
      selector.mouseDown(pointerEvent(selector, point, point));
      if (selectorFirst)
        PracticePanelGestureTestAccess::touchDown(*fixture.panel, 1, wave);
      const int firstUp = selectorReleasedFirst ? 0 : 1;
      expect(PracticePanelGestureTestAccess::touchUp(*fixture.panel, firstUp),
             "first pinch release is consumed with a finger on the instrument wheel");
      selector.mouseDrag(pointerEvent(selector, point.translated(-180, 0), point, 1, true));
      selector.mouseUp(pointerEvent(selector, point.translated(-180, 0), point, 1, true));
      expect(PracticePanelGestureTestAccess::touchUp(*fixture.panel, 1 - firstUp),
             "last pinch release clears the latch after consuming the gesture");
      expect(selector.getSelectedInstrumentId() == "piano" && fixture.seeks.empty() &&
                 fixture.listeningChanges.empty(),
             "pinch contacts in either order cannot leak an instrument selection or playback gesture");
      selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
      expect(selector.getSelectedInstrumentId() == "bass", "selection works again after the pinch");
    }
  }
  Fixture magnified;
  auto &wheel = PracticePanelGestureTestAccess::instrumentSelector(*magnified.panel);
  const auto point = wheel.getLocalBounds().getCentre();
  wheel.mouseDown(pointerEvent(wheel, point, point));
  wheel.mouseMagnify(pointerEvent(wheel, point, point), 2.0f);
  wheel.mouseUp(pointerEvent(wheel, point.translated(-180, 0), point, 1, true));
  expectNear(PracticePanelGestureTestAccess::visibleSpan(*magnified.panel), 6.0,
             "trackpad magnification over the wheel zooms the shared waveform");
  expect(wheel.getSelectedInstrumentId() == "piano" && magnified.seeks.empty(),
         "magnification cancels the pending wheel selection");
}

void testInstrumentWheelLongNamesLayoutAndLifetime() {
  for (const int width : {320, 390, 768}) {
    Fixture fixture;
    // JUCE creates native accessibility handlers only for a component with a
    // window handle. Keep this fixture hidden while providing that peer.
    fixture.panel->setVisible(false);
    fixture.panel->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    const juce::String longName = "Guitarra electrica principal con efectos y ambiente de concierto";
    fixture.availableInstruments[0].name = longName;
    PracticePanelGestureTestAccess::refreshWaveform(*fixture.panel);
    fixture.panel->setSize(width, 844);
    auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*fixture.panel);
    expect(selector.getHeight() == 44 && selector.getItemSpacing() > 44.0f &&
               PracticePanelGestureTestAccess::card(*fixture.panel).contains(selector.getBounds()),
           "instrument wheel stays in the card header with wide labels and a 44-point touch target");
    auto *handler = selector.getAccessibilityHandler();
    expect(selector.getTextFromValue(0.0) == longName && handler != nullptr &&
               handler->getValueInterface() != nullptr &&
               handler->getValueInterface()->getCurrentValueAsString() == longName,
           "the full instrument name remains available to the native Slider accessibility value interface");
    juce::Image image(juce::Image::ARGB, selector.getWidth(), selector.getHeight(), true);
    juce::Graphics g(image);
    selector.paint(g);
    expectNear(PracticePanelGestureTestAccess::pitchSelector(*fixture.panel).getItemSpacing(), 44.0,
               "instrument label widths do not alter pitch spacing");
    expectNear(PracticePanelGestureTestAccess::tempoSelector(*fixture.panel).getItemSpacing(), 44.0,
               "instrument label widths do not alter tempo spacing");
  }
  Fixture closing;
  closing.afterSynchronousWaveform = [&](const juce::String &) { closing.panel.reset(); };
  auto &selector = PracticePanelGestureTestAccess::instrumentSelector(*closing.panel);
  selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expect(closing.panel == nullptr && closing.waveformRequests.back().cancelled->load(),
         "a waveform provider may destroy the panel during the wheel's synchronous callback");
}

} // namespace

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  juce::MouseEvent::setDoubleClickTimeout(250);
  testWaveformRevisionInvalidatesSameDuration();
  testWaveformDurationInvalidationWithoutRevisionProvider();
  testWaveformRejectsChangedLiveSourcesBeforeProbe();
  testWaveformCacheHitCancelsPendingSelection();
  testWaveformSelectionChecksRevisionBeforeCacheHit();
  testWaveformCachePrunesRemovedInstruments();
  testWaveformSynchronousReentryAndPanelLifetime();
  testBarJumpsAndTransportState();
  runPracticeWaveformGeometryTests();
  runPracticePlaybackClockTests();
  runPracticeViewportTests();
  runPracticeLoopTests();
  testDragPreviewAndSingleCommit();
  testVelocityBasedInertiaAndPause();
  testInertiaFrameRateAndBounds();
  testNewTouchInterruptsInertia();
  testSeekClockHandoffAndRejection();
  testContextChangeClearsResidualHandoff();
  testPausedAndInterruptedSettling();
  testScrubCancellationAndExplicitTransport();
  testAccumulatedBarFeedback();
  testRemoteSeekPreviewLifecycle();
  testHelpButtonTapAndDrag();
  testHelpCallbackLifetime();
  testRestoreGestureTargetsActiveInstrument();
  testRestoreKeyboardAndContext();
  testSharedTransportLayout();
  testAccompanimentIndependentControls();
  testAccompanimentAsyncConfirmationAndTimeout();
  testAccompanimentContextAndUnavailable();
  testAccompanimentDragAndCallbackLifetime();
  testListeningGlowPixelsAndRemoteTarget();
  testRemoteListeningGlowWaitsForConfirmation();
  testLoopFeedbackUsesExistingHints();
  testFractionalDragCoordinates();
  testFullWidthWaveformDragTimeMapping();
  testPinchLatchAndScrubCancellation();
  testZoomLimitsAndSeekScale();
  testPinchRebaselinesFingerMembership();
  testPitchControlsAndRemoteOptimism();
  testInstrumentWheelSelectionAndRetarget();
  testInstrumentWheelVerticalAndAxisOwnership();
  testInstrumentWheelListAndContextChanges();
  testInstrumentWheelPinchAndMagnify();
  testInstrumentWheelLongNamesLayoutAndLifetime();
  testPitchSelectorDragAndNames();
  testPitchSelectorCancelledDrag();
  testTempoSelectorDragAndFractionalOriginal();
  testTempoLimitsAndRemoteOptimism();
  testTempoGestureCancellation();
  testDeletePracticeRegion();
  testRemoteDeletedRegionCannotReappear();
  testDeletingNewDraftDoesNotRevivePreviousLoop();
  testLoopMarksUseSharedSnapping();
  testLoopMarkingActivationAndRelease();
  testLoopCloseOwnershipAndRejection();
  testPracticeControlsResetWithContext();
  testPracticeToolLayoutOnNarrowPhones();
  testShortLoopClockWrapsImmediately();
  testStopClearsRemoteSeekPreview();
  testSeekBounds();
  testGestureAxisAndWholeCardListening();
  testListeningGestureBoundsAndSingleStep();
  testListeningStateSelectionAndRestore();
  testCancelledAndDisconnectedGestures();
  testDoubleTapDisambiguation();
  std::cout << "Practice panel: gestures, listening glow, accompaniment and cancellation passed\n";
}
