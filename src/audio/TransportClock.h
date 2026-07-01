#pragma once

#include <atomic>
#include <cstdint>

namespace playworship::audio {

class TransportClock {
public:
  using Frame = std::int64_t;

  enum class State : int { stopped = 0, playing, paused };

  void prepare(double sampleRate) noexcept;
  void reset(Frame frame = 0) noexcept;

  void play() noexcept;
  void pause() noexcept;
  void stop(Frame returnFrame = 0) noexcept;
  void seek(Frame frame) noexcept;
  void advance(int frameCount) noexcept;

  State getState() const noexcept;
  bool isPlaying() const noexcept;
  Frame getPositionFrames() const noexcept;
  double getPositionSeconds() const noexcept;
  double getSampleRate() const noexcept;

  static Frame secondsToFrames(double seconds, double sampleRate) noexcept;
  static double framesToSeconds(Frame frames, double sampleRate) noexcept;

private:
  std::atomic<double> sampleRateHz{44100.0};
  std::atomic<Frame> positionFrames{0};
  std::atomic<int> state{static_cast<int>(State::stopped)};
};

} // namespace playworship::audio
