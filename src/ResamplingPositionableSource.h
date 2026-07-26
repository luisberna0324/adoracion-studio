#pragma once

#include <JuceHeader.h>
#include <atomic>

class ResamplingPositionableSource : public juce::PositionableAudioSource {
public:
  ResamplingPositionableSource(juce::PositionableAudioSource *inputSource,
                               double inputSampleRate,
                               int numChannels = 2);

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void releaseResources() override;
  void getNextAudioBlock(const juce::AudioSourceChannelInfo &info) override;

  void setNextReadPosition(juce::int64 newPosition) override;
  juce::int64 getNextReadPosition() const override;
  juce::int64 getTotalLength() const override;
  bool isLooping() const override;
  void setLooping(bool shouldLoop) override;

  // ---- Benchmark: AAC decode + resample instrumentation ---------------
  // The background TimeSliceThread worker calls getNextAudioBlock() to
  // pull decoded + resampled audio from the reader. That is where the
  // AAC decode (or WAV/FLAC/OGG decode) actually happens. The audio
  // thread samples these atomics to learn how long the decode+resample
  // path takes and how long it has been since the last call.
  int64_t getLastDecodeStartTick() const noexcept {
    return lastDecodeStartTick.load(std::memory_order_relaxed);
  }
  int64_t getLastDecodeDurationTicks() const noexcept {
    return lastDecodeDurationTicks.load(std::memory_order_relaxed);
  }
  int64_t getPreviousDecodeStartTick() const noexcept {
    return previousDecodeStartTick.load(std::memory_order_relaxed);
  }
  uint64_t getDecodeCallCount() const noexcept {
    return decodeCallCount.load(std::memory_order_relaxed);
  }

private:
  double getResampleRatio() const noexcept;

  juce::PositionableAudioSource *input = nullptr;
  juce::ResamplingAudioSource resampler;
  double sourceSampleRate = 0.0;
  double outputSampleRate = 0.0;
  std::atomic<juce::int64> outputPosition{0};

  // ---- Benchmark: decode / resample timing ----------------------------
  // Written by the background worker (sole writer), read by the audio
  // thread (sole reader). Relaxed ordering — we are sampling
  // approximate diagnostics, not synchronising state.
  std::atomic<int64_t> lastDecodeStartTick{0};
  std::atomic<int64_t> previousDecodeStartTick{0};
  std::atomic<int64_t> lastDecodeDurationTicks{0};
  std::atomic<uint64_t> decodeCallCount{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResamplingPositionableSource)
};
