#include "BungeeProcessor.h"

#include <bungee/Bungee.h>
#include <bungee/Stream.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

struct BungeeProcessor::Impl {
  using Stretcher = Bungee::Stretcher<Bungee::Basic>;
  using Stream = Bungee::Stream<Bungee::Basic>;

  double sampleRate = 0.0;
  int channels = 0;
  int maxOutputFrames = 0;
  int maxInputFrames = 0;
  int pitchSemitones = 0;
  double tempoRatio = 1.0;
  ContentType contentType = ContentType::Melodic;
  int latencySamples = 0;
  bool producedOutput = false;
  double inputFrameRemainder = 0.0;

  std::unique_ptr<Stretcher> stretcher;
  std::unique_ptr<Stream> stream;
  std::vector<const float *> inputPointers;
  std::vector<float *> outputPointers;
};

BungeeProcessor::BungeeProcessor() : impl(std::make_unique<Impl>()) {}
BungeeProcessor::~BungeeProcessor() = default;

bool BungeeProcessor::prepare(const double sampleRate,
                              const int maxOutputFrames,
                              const int numChannels) {
  if (sampleRate <= 0.0 || maxOutputFrames <= 0 || numChannels <= 0)
    return false;
  impl->sampleRate = sampleRate;
  impl->channels = numChannels;
  impl->maxOutputFrames = maxOutputFrames;
  impl->maxInputFrames = static_cast<int>(
      std::ceil(static_cast<double>(maxOutputFrames) * 2.0)) + 8;
  impl->inputPointers.resize(static_cast<size_t>(numChannels), nullptr);
  impl->outputPointers.resize(static_cast<size_t>(numChannels), nullptr);
  return rebuild();
}

bool BungeeProcessor::rebuild() {
  if (impl->sampleRate <= 0.0 || impl->channels <= 0 ||
      impl->maxInputFrames <= 0) {
    return false;
  }
  try {
    const Bungee::SampleRates rates{
        static_cast<int>(std::lround(impl->sampleRate)),
        static_cast<int>(std::lround(impl->sampleRate))};
    impl->stretcher =
        std::make_unique<Impl::Stretcher>(rates, impl->channels, -1);
    impl->stream = std::make_unique<Impl::Stream>(
        *impl->stretcher, impl->maxInputFrames, impl->channels);
    impl->latencySamples = 0;
    impl->producedOutput = false;
    impl->inputFrameRemainder = 0.0;
    return true;
  } catch (...) {
    impl->stream.reset();
    impl->stretcher.reset();
    return false;
  }
}

void BungeeProcessor::reset() noexcept {
  // Deliberately allocation-free. A Bungee Stream has no in-place reset API;
  // rebuilding it here would allocate on the real-time thread.
}

void BungeeProcessor::setPitchSemiTones(const int semitones) noexcept {
  impl->pitchSemitones = std::clamp(semitones, -24, 24);
}

void BungeeProcessor::setTempoRatio(const double ratio) noexcept {
  impl->tempoRatio = std::clamp(ratio, 0.75, 2.0);
}

void BungeeProcessor::setContentType(const ContentType type) noexcept {
  impl->contentType = type;
}

int BungeeProcessor::requiredInputFrames(const int outputFrames) noexcept {
  if (outputFrames <= 0)
    return 0;
  const double exactFrames =
      (static_cast<double>(outputFrames) * impl->tempoRatio) +
      impl->inputFrameRemainder;
  const int inputFrames = static_cast<int>(std::floor(exactFrames));
  impl->inputFrameRemainder = exactFrames - static_cast<double>(inputFrames);
  return std::clamp(inputFrames, 1, impl->maxInputFrames);
}

int BungeeProcessor::process(const juce::AudioBuffer<float> &input,
                             const int inputFrames,
                             juce::AudioBuffer<float> &output,
                             const int outputFrames) noexcept {
  if (!impl->stream || inputFrames <= 0 || outputFrames <= 0 ||
      inputFrames > impl->maxInputFrames ||
      outputFrames > impl->maxOutputFrames ||
      input.getNumChannels() < impl->channels ||
      output.getNumChannels() < impl->channels) {
    output.clear();
    return 0;
  }

  for (int channel = 0; channel < impl->channels; ++channel) {
    impl->inputPointers[static_cast<size_t>(channel)] =
        input.getReadPointer(channel);
    impl->outputPointers[static_cast<size_t>(channel)] =
        output.getWritePointer(channel);
  }

  const double pitchScale =
      std::pow(2.0, static_cast<double>(impl->pitchSemitones) / 12.0);
  int produced = 0;
  try {
    produced = impl->stream->process(
        impl->inputPointers.data(), impl->outputPointers.data(), inputFrames,
        static_cast<double>(outputFrames), pitchScale);
  } catch (...) {
    output.clear();
    return 0;
  }

  if (produced < outputFrames) {
    for (int channel = 0; channel < impl->channels; ++channel)
      output.clear(channel, produced, outputFrames - produced);
  }
  if (produced > 0) {
    impl->producedOutput = true;
    const double latency = impl->stream->latency();
    if (std::isfinite(latency))
      impl->latencySamples =
          std::max(0, static_cast<int>(std::lround(latency)));
  }
  return produced;
}

int BungeeProcessor::getPitchSemiTones() const noexcept {
  return impl->pitchSemitones;
}

double BungeeProcessor::getTempoRatio() const noexcept {
  return impl->tempoRatio;
}

int BungeeProcessor::getLatencySamples() const noexcept {
  return impl->latencySamples;
}

int BungeeProcessor::getNumChannels() const noexcept {
  return impl->channels;
}

bool BungeeProcessor::isPrepared() const noexcept {
  return impl->stream != nullptr;
}

const char *BungeeProcessor::backendName() const noexcept {
  return isPrepared() ? "bungee_basic" : "unavailable";
}
