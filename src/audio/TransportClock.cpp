#include "TransportClock.h"

#include <algorithm>
#include <cmath>

namespace playworship::audio {

void TransportClock::prepare(const double sampleRate) noexcept {
  if (std::isfinite(sampleRate) && sampleRate > 0.0)
    sampleRateHz.store(sampleRate, std::memory_order_relaxed);
}

void TransportClock::reset(const Frame frame) noexcept {
  positionFrames.store(std::max<Frame>(0, frame), std::memory_order_relaxed);
  state.store(static_cast<int>(State::stopped), std::memory_order_release);
}

void TransportClock::play() noexcept {
  state.store(static_cast<int>(State::playing), std::memory_order_release);
}

void TransportClock::pause() noexcept {
  state.store(static_cast<int>(State::paused), std::memory_order_release);
}

void TransportClock::stop(const Frame returnFrame) noexcept {
  positionFrames.store(std::max<Frame>(0, returnFrame),
                       std::memory_order_relaxed);
  state.store(static_cast<int>(State::stopped), std::memory_order_release);
}

void TransportClock::seek(const Frame frame) noexcept {
  positionFrames.store(std::max<Frame>(0, frame), std::memory_order_release);
}

void TransportClock::advance(const int frameCount) noexcept {
  if (frameCount <= 0 || !isPlaying())
    return;
  positionFrames.fetch_add(static_cast<Frame>(frameCount),
                           std::memory_order_relaxed);
}

TransportClock::State TransportClock::getState() const noexcept {
  return static_cast<State>(state.load(std::memory_order_acquire));
}

bool TransportClock::isPlaying() const noexcept {
  return getState() == State::playing;
}

TransportClock::Frame TransportClock::getPositionFrames() const noexcept {
  return positionFrames.load(std::memory_order_acquire);
}

double TransportClock::getPositionSeconds() const noexcept {
  return framesToSeconds(getPositionFrames(), getSampleRate());
}

double TransportClock::getSampleRate() const noexcept {
  return sampleRateHz.load(std::memory_order_relaxed);
}

TransportClock::Frame
TransportClock::secondsToFrames(const double seconds,
                                const double sampleRate) noexcept {
  if (!std::isfinite(seconds) || !std::isfinite(sampleRate) ||
      seconds <= 0.0 || sampleRate <= 0.0) {
    return 0;
  }
  return static_cast<Frame>(std::llround(seconds * sampleRate));
}

double TransportClock::framesToSeconds(const Frame frames,
                                       const double sampleRate) noexcept {
  if (frames <= 0 || !std::isfinite(sampleRate) || sampleRate <= 0.0)
    return 0.0;
  return static_cast<double>(frames) / sampleRate;
}

} // namespace playworship::audio
