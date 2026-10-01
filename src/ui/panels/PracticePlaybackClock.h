#pragma once

#include <algorithm>
#include <cmath>

// Presentation only: source seconds still belong to the audio/server clock.
// Advance from frame deltas and absorb block/packet quantisation gradually,
// while preserving deliberate transport discontinuities immediately.
class PracticePlaybackClock {
public:
  void reset(const double positionSeconds, const double frameTimestampSec) {
    visualPosition = finitePosition(positionSeconds);
    lastObservedPosition = visualPosition;
    lastFrameTimestamp = finiteTimestamp(frameTimestampSec, 0.0);
    lastSourceChangeTimestamp = lastFrameTimestamp;
    initialized = true;
    previouslyPlaying = false;
  }

  double update(const double observedSeconds, const bool playing,
                const double playbackRate, const double frameTimestampSec) {
    const double observed = std::isfinite(observedSeconds)
        ? finitePosition(observedSeconds) : lastObservedPosition;
    const double timestamp = finiteTimestamp(frameTimestampSec, lastFrameTimestamp);
    if (!initialized || !playing || !previouslyPlaying) {
      reset(observed, timestamp);
      previouslyPlaying = playing;
      return visualPosition;
    }

    const double now = std::max(lastFrameTimestamp, timestamp);
    const double elapsed = std::min(maximumPredictionSeconds,
                                    now - lastFrameTimestamp);
    const double rate = std::isfinite(playbackRate) && playbackRate >= 0.0
        ? playbackRate : 1.0;
    const bool sourceChanged =
        std::abs(observed - lastObservedPosition) > sourceChangeEpsilon;
    const double predicted = visualPosition + elapsed * rate;

    // An unchanged source is a stall, not a backward seek to its old sample.
    // Conversely, a fresh position far from prediction is a seek/loop/jump.
    if (sourceChanged && std::abs(observed - predicted) >= discontinuitySeconds) {
      reset(observed, now);
      previouslyPlaying = true;
      return visualPosition;
    }

    if (sourceChanged) {
      lastObservedPosition = observed;
      lastSourceChangeTimestamp = now;
    }

    // Stop predicting when neither blocks nor remote estimates advance. This
    // retains the last presented position rather than creeping indefinitely.
    const double remainingPrediction = std::max(
        0.0, maximumPredictionSeconds -
                 (lastFrameTimestamp - lastSourceChangeTimestamp));
    const double advance = std::min(elapsed, remainingPrediction);
    double next = visualPosition + advance * rate;
    const double error = observed - next;
    const double deadZone = quantizationToleranceSeconds * std::max(1.0, rate);
    const double effectiveError = std::copysign(
        std::max(0.0, std::abs(error) - deadZone), error);
    const double correction =
        1.0 - std::exp(-elapsed / correctionTimeSeconds);
    next += effectiveError * correction;

    // Small stale packets must never move a continuously playing wave back.
    visualPosition = std::max(visualPosition, finitePosition(next));
    lastFrameTimestamp = now;
    return visualPosition;
  }

  double position() const { return visualPosition; }
  bool isValid() const { return initialized; }

  void clear() {
    visualPosition = lastObservedPosition = 0.0;
    lastFrameTimestamp = lastSourceChangeTimestamp = 0.0;
    initialized = previouslyPlaying = false;
  }

private:
  static double finitePosition(const double value) {
    return std::isfinite(value) ? std::max(0.0, value) : 0.0;
  }

  static double finiteTimestamp(const double value, const double fallback) {
    return std::isfinite(value) ? value : fallback;
  }

  static constexpr double correctionTimeSeconds = 0.09;
  static constexpr double quantizationToleranceSeconds = 0.01;
  static constexpr double maximumPredictionSeconds = 0.25;
  static constexpr double discontinuitySeconds = 0.25;
  static constexpr double sourceChangeEpsilon = 0.000001;
  double visualPosition = 0.0;
  double lastObservedPosition = 0.0;
  double lastFrameTimestamp = 0.0;
  double lastSourceChangeTimestamp = 0.0;
  bool initialized = false;
  bool previouslyPlaying = false;
};
