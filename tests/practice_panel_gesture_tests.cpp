#include "ui/panels/PracticePanel.h"
#include "ui/UiCommon.h"

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

  static bool hasPendingMenu(const PracticePanel &panel) {
    return panel.pendingInstrumentMenuMs != 0;
  }

  static juce::TextButton &restoreButton(PracticePanel &panel) {
    return panel.restoreMixButton;
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
};

namespace {
struct Fixture {
  juce::String songId = "practice-song";
  juce::String signature = "4/4";
  juce::String key = "C";
  double bpm = 120.0;
  double playbackRate = 1.0;
  double position = 40.0;
  double length = 120.0;
  juce::Range<double> range{0.0, 120.0};
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
  std::vector<double> seeks;
  std::vector<int> listeningChanges;
  std::vector<juce::String> listeningTargets;
  std::vector<bool> scrubCancellations;
  std::unique_ptr<PracticePanel> panel;

  Fixture() {
    PracticePanel::Sources sources;
    sources.songId = [this] { return songId; };
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
    sources.instruments = [] {
      return std::vector<PracticePanel::Instrument>{{"piano", "Piano"},
                                                    {"bass", "Bass"}};
    };
    sources.requestInstrumentWaveform =
        [](const juce::String &, std::shared_ptr<std::atomic<bool>>,
           std::function<void(std::vector<float>)> deliver) {
          deliver(std::vector<float>(512, 0.5f));
        };
    sources.isRemote = [this] { return remote; };
    sources.listeningMode = [this] { return mode; };
    sources.listeningInstrumentId = [this] { return listeningInstrument; };
    sources.setListeningMode = [this](const juce::String &id, const int next) {
      listeningInstrument = id;
      mode = next;
      listeningChanges.push_back(next);
      listeningTargets.push_back(id);
    };
    sources.canRetargetListening = [] { return true; };
    sources.finishListening = [this] { ++finishListeningCalls; };
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

void testRestoreButtonTapAndDrag() {
  Fixture tap;
  tap.mode = 1;
  tap.listeningInstrument = "piano";
  pumpFor(80);
  auto &tapButton =
      PracticePanelGestureTestAccess::restoreButton(*tap.panel);
  expect(tapButton.isVisible() && tapButton.isEnabled(),
         "an active listening mix exposes the real Restore button");
  const auto tapPosition = tapButton.getLocalBounds().getCentre();
  const auto tapEvent = pointerEvent(tapButton, tapPosition, tapPosition);
  static_cast<juce::Component &>(tapButton).mouseDown(tapEvent);
  tap.panel->mouseDown(tapEvent);
  static_cast<juce::Component &>(tapButton).mouseUp(tapEvent);
  tap.panel->mouseUp(tapEvent);
  expect(tap.mode == 0 && tap.listeningChanges == std::vector<int>({0}) &&
             tap.seeks.empty(),
         "a real Restore button tap restores the mix without seeking");

  for (const juce::Point<int> movement : {juce::Point<int>{24, 0},
                                         juce::Point<int>{0, 14}}) {
    Fixture drag;
    drag.mode = 1;
    drag.listeningInstrument = "piano";
    drag.playing = true;
    pumpFor(80);
    auto &dragButton =
        PracticePanelGestureTestAccess::restoreButton(*drag.panel);
    expect(!PracticePanelGestureTestAccess::card(*drag.panel).intersects(
               dragButton.getBounds()),
           "Restore is outside the waveform gesture surface");
    const auto start = dragButton.getLocalBounds().getCentre();
    const auto end = start + movement;
    expect(dragButton.getLocalBounds().contains(end),
           "the test drag releases inside the Restore button");
    const auto down = pointerEvent(dragButton, start, start);
    const auto release = pointerEvent(dragButton, end, start, 1, true);
    static_cast<juce::Component &>(dragButton).mouseDown(down);
    drag.panel->mouseDown(down);
    drag.panel->mouseDrag(release);
    expect(dragButton.isDown() && drag.seeks.empty() && drag.beginScrubCalls == 0,
           "dragging Restore outside the waveform cannot start scrubbing");
    // Keep JUCE's actual button-before-listener release ordering, including
    // a release inside the button that would otherwise trigger its onClick.
    static_cast<juce::Component &>(dragButton).mouseUp(release);
    drag.panel->mouseUp(release);
    drag.finishAnimation();
    expect(drag.mode == 1 && drag.listeningChanges.empty() && drag.seeks.empty() &&
               drag.beginScrubCalls == 0 && drag.pauseCalls == 0 && drag.playing,
           "a drag on Restore changes neither mix, playback position nor transport");
  }
}

void testSharedTransportLayout() {
  Fixture fixture;
  fixture.mode = 1;
  fixture.listeningInstrument = "piano";
  pumpFor(80);
  const auto close =
      PracticePanelGestureTestAccess::closeButton(*fixture.panel).getBounds();
  const auto play =
      PracticePanelGestureTestAccess::playButton(*fixture.panel).getBounds();
  const auto stop =
      PracticePanelGestureTestAccess::stopButton(*fixture.panel).getBounds();
  const auto restore =
      PracticePanelGestureTestAccess::restoreButton(*fixture.panel).getBounds();
  expect(close.getWidth() == 44 && close.getHeight() == 44,
         "practice uses the shared 44-point header close control");
  expect(play.getWidth() == 56 && play.getHeight() == 56 &&
             stop.getWidth() == 56 && stop.getHeight() == 56 &&
             play.getCentreY() == stop.getCentreY(),
         "shared Play and Stop buttons have matching 56-point transport targets");
  const auto clear = PracticePanelGestureTestAccess::loopClearButton(*fixture.panel).getBounds();
  expect(stop.getX() - play.getRight() == 8 &&
             play.getUnion(clear).getCentreX() ==
                 PracticePanelGestureTestAccess::card(*fixture.panel).getCentreX(),
         "the six-control transport row stays centered with consistent dock spacing");
  expect(!PracticePanelGestureTestAccess::card(*fixture.panel).intersects(restore) &&
             restore.getBottom() <= play.getY(),
         "the secondary Restore control sits above transport outside the waveform");
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
             !PracticePanelGestureTestAccess::hasPendingMenu(*fixture.panel) &&
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
  fixture.playing = true;
  fixture.bpm = fixture.tempo = 120.5;
  PracticePanelGestureTestAccess::updateControls(*fixture.panel);
  auto &selector = PracticePanelGestureTestAccess::tempoSelector(*fixture.panel);
  expectNear(selector.getValue(), 120.5,
             "opening the tempo selector preserves fractional source BPM");
  expect(fixture.tempoChanges.empty() && selector.getTextFromValue(120.0) == "120 BPM",
         "tempo synchronization is silent and accessible values include BPM");
  pumpFor(180);
  const auto start = selector.getLocalBounds().getCentre();
  const auto finish = start.translated(-95, 0);
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
  const auto lower = start.translated(44, 0);
  selector.mouseDown(pointerEvent(selector, start, start));
  selector.mouseUp(pointerEvent(selector, lower, start));
  expect(fixture.tempoChanges.size() == 2,
         "coalesced tempo movement is committed once at mouse release");
  expectNear(fixture.tempo, 121.5, "a 44-point rightward drag lowers tempo by one BPM");
  selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  expectNear(fixture.tempo, 122.5, "tempo keyboard input increases by exactly one BPM");
  selector.mouseDoubleClick(pointerEvent(selector, start, start, 2));
  expectNear(fixture.tempo, 120.5, "double tapping restores the exact fractional original tempo");
  selector.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
  selector.keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
  expectNear(fixture.tempo, 120.5, "Home also restores the exact original tempo");
  pumpFor(180);
  const auto right = start.translated(44, 0);
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
  pumpFor(80);
  fixture.clickButton(PracticePanelGestureTestAccess::restoreButton(*fixture.panel));
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
  const auto headerBounds =
      PracticePanelGestureTestAccess::header(*header.panel);
  const juce::Point<int> point(headerBounds.getX() + headerBounds.getWidth() / 4,
                              headerBounds.getCentreY());
  header.tap(point);
  expect(PracticePanelGestureTestAccess::hasPendingMenu(*header.panel),
         "single header tap defers the instrument menu for double-tap resolution");
  header.tap(point, 2);
  expect(header.seeks.size() == 1 &&
             !PracticePanelGestureTestAccess::hasPendingMenu(*header.panel),
         "header double tap jumps a bar and cancels the deferred menu");
  pumpFor(juce::MouseEvent::getDoubleClickTimeout() + 100);
  expect(!juce::PopupMenu::dismissAllActiveMenus(),
         "a header double tap never opens an instrument popup later");
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
  testRestoreButtonTapAndDrag();
  testSharedTransportLayout();
  testFractionalDragCoordinates();
  testFullWidthWaveformDragTimeMapping();
  testPinchLatchAndScrubCancellation();
  testZoomLimitsAndSeekScale();
  testPinchRebaselinesFingerMembership();
  testPitchControlsAndRemoteOptimism();
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
  std::cout << "Practice panel: bar jumps, scrubbing, listening gestures and cancellation passed\n";
}
