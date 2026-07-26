#include "ResamplingPositionableSource.h"
#include <cmath>

ResamplingPositionableSource::ResamplingPositionableSource(
    juce::PositionableAudioSource *inputSource, double inputSampleRate,
    int numChannels)
    : input(inputSource), resampler(inputSource, false, numChannels),
      sourceSampleRate(inputSampleRate) {}

double ResamplingPositionableSource::getResampleRatio() const noexcept {
  const double outRate =
      outputSampleRate > 0.0 ? outputSampleRate : sourceSampleRate;
  if (sourceSampleRate <= 0.0 || outRate <= 0.0)
    return 1.0;
  return sourceSampleRate / outRate;
}

void ResamplingPositionableSource::prepareToPlay(int samplesPerBlockExpected,
                                                 double sampleRate) {
  outputSampleRate = sampleRate > 0.0 ? sampleRate : sourceSampleRate;
  resampler.setResamplingRatio(getResampleRatio());
  resampler.prepareToPlay(samplesPerBlockExpected, outputSampleRate);

  const double ratio = getResampleRatio();
  if (input != nullptr && ratio > 0.0) {
    const auto inputPos = input->getNextReadPosition();
    outputPosition.store(
        static_cast<juce::int64>(std::llround(inputPos / ratio)),
        std::memory_order_relaxed);
  }
}

void ResamplingPositionableSource::releaseResources() {
  resampler.releaseResources();
}

void ResamplingPositionableSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &info) {
  // ---- Benchmark: bracket the decode + resample path -----------------
  // The background TimeSliceThread worker pulls PCM out of the
  // AudioFormatReader (which decodes AAC/WAV/FLAC/etc) and feeds it
  // through the JUCE resampler. getNextAudioBlock() is the only place
  // where both happen for the same call, so it is the natural point
  // to measure end-to-end cost. Hardware register read — safe from
  // the worker thread.
  const int64_t t0 = juce::Time::getHighResolutionTicks();
  decodeCallCount.fetch_add(1, std::memory_order_relaxed);
  // exchange gives us (previousStart, newStart) atomically so the
  // audio thread can read the gap between consecutive decode starts
  // without any decimation bias.
  const int64_t prevStart =
      lastDecodeStartTick.exchange(t0, std::memory_order_relaxed);
  previousDecodeStartTick.store(prevStart, std::memory_order_relaxed);

  resampler.getNextAudioBlock(info);

  const int64_t t1 = juce::Time::getHighResolutionTicks();
  lastDecodeDurationTicks.store(t1 - t0, std::memory_order_relaxed);

  if (info.numSamples > 0)
    outputPosition.fetch_add(info.numSamples, std::memory_order_relaxed);
}

void ResamplingPositionableSource::setNextReadPosition(
    juce::int64 newPosition) {
  outputPosition.store(newPosition, std::memory_order_relaxed);
  if (input != nullptr) {
    const double ratio = getResampleRatio();
    const juce::int64 inputPos =
        ratio > 0.0
            ? static_cast<juce::int64>(std::llround(newPosition * ratio))
            : newPosition;
    input->setNextReadPosition(inputPos);
  }
  resampler.flushBuffers();
}

juce::int64 ResamplingPositionableSource::getNextReadPosition() const {
  return outputPosition.load(std::memory_order_relaxed);
}

juce::int64 ResamplingPositionableSource::getTotalLength() const {
  if (input == nullptr)
    return 0;
  const auto inputLength = input->getTotalLength();
  if (inputLength <= 0)
    return inputLength;
  const double ratio = getResampleRatio();
  if (ratio <= 0.0)
    return inputLength;
  return static_cast<juce::int64>(
      std::llround(static_cast<double>(inputLength) / ratio));
}

bool ResamplingPositionableSource::isLooping() const {
  return input != nullptr && input->isLooping();
}

void ResamplingPositionableSource::setLooping(bool shouldLoop) {
  if (input != nullptr)
    input->setLooping(shouldLoop);
}
