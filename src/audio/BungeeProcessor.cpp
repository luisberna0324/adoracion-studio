#include "BungeeProcessor.h"

#include <bungee/Bungee.h>
#include <bungee/Stream.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace {
int inputLatencyFramesToOutputSamples(const double inputLatencyFrames,
                                      const double inputFramesPerOutputFrame) {
  if (!std::isfinite(inputLatencyFrames) || inputLatencyFrames <= 0.0 ||
      !std::isfinite(inputFramesPerOutputFrame) ||
      inputFramesPerOutputFrame <= 0.0) {
    return 0;
  }

  const double outputSamples =
      inputLatencyFrames / inputFramesPerOutputFrame;
  return static_cast<int>(std::lround(std::min(
      outputSamples, static_cast<double>(std::numeric_limits<int>::max()))));
}
} // namespace

struct BungeeProcessor::Impl {
  using Stretcher = Bungee::Stretcher<Bungee::Basic>;
  using Stream = Bungee::Stream<Bungee::Basic>;

  enum class VoiceState : uint8_t {
    Empty = 0,
    Active,
    Ready,
    Retired,
    Rebuilding
  };

  struct VoiceSlot {
    std::unique_ptr<Stretcher> stretcher;
    std::unique_ptr<Stream> stream;
    std::atomic<VoiceState> state{VoiceState::Empty};
    std::atomic<uint64_t> generation{0};
    std::atomic<double> latencyInputFrames{0.0};
  };

  static_assert(std::atomic<VoiceState>::is_always_lock_free,
                "Bungee reset requires lock-free slot states");
  static_assert(std::atomic<bool>::is_always_lock_free,
                "Bungee reset requires a lock-free pending flag");
  static_assert(std::atomic<double>::is_always_lock_free,
                "Bungee latency publication must remain lock-free");
  static_assert(std::atomic<uint64_t>::is_always_lock_free,
                "Bungee reset requires lock-free generations and counters");

  double sampleRate = 0.0;
  int channels = 0;
  int maxOutputFrames = 0;
  int maxInputFrames = 0;
  int pitchSemitones = 0;
  double tempoRatio = 1.0;
  ContentType contentType = ContentType::Melodic;
  double inputFrameRemainder = 0.0;

  std::array<VoiceSlot, 2> voices;
  std::atomic<uint64_t> activeVoiceToken{0};
  std::atomic<bool> resetPending{false};
  std::atomic<uint64_t> resetSuccessCount{0};
  std::atomic<uint64_t> resetMissCount{0};
  std::atomic<uint64_t> serviceFailureCount{0};
  std::atomic<int> forcedVoiceBuildFailuresForTests{0};
  unsigned int consecutiveServiceFailures = 0;
  std::chrono::steady_clock::time_point nextServiceRetry{};
  std::vector<const float *> inputPointers;
  std::vector<float *> outputPointers;

  static constexpr uint64_t kMaxGeneration =
      std::numeric_limits<uint64_t>::max() >> 1U;
  static constexpr int kLatencyStableBlocks = 4;
  static constexpr int kMaxWarmupLatencyFrames = 65536;
  static constexpr int kServiceRetryBaseMilliseconds = 10;
  static constexpr int kServiceRetryMaxMilliseconds = 1000;

  static uint64_t nextGeneration(const uint64_t current) noexcept {
    return current == 0 || current >= kMaxGeneration ? 1 : current + 1;
  }

  static uint64_t makeVoiceToken(const int slot,
                                 const uint64_t generation) noexcept {
    return (generation << 1U) | static_cast<uint64_t>(slot & 1);
  }

  static int slotFromToken(const uint64_t token) noexcept {
    return static_cast<int>(token & 1U);
  }

  static uint64_t generationFromToken(const uint64_t token) noexcept {
    return token >> 1U;
  }

  VoiceSlot *activeVoice() noexcept {
    const uint64_t token = activeVoiceToken.load(std::memory_order_acquire);
    const uint64_t generation = generationFromToken(token);
    if (generation == 0)
      return nullptr;

    auto &slot = voices[static_cast<size_t>(slotFromToken(token))];
    if (slot.state.load(std::memory_order_acquire) != VoiceState::Active ||
        slot.generation.load(std::memory_order_acquire) != generation) {
      return nullptr;
    }
    return &slot;
  }

  const VoiceSlot *activeVoice() const noexcept {
    const uint64_t token = activeVoiceToken.load(std::memory_order_acquire);
    const uint64_t generation = generationFromToken(token);
    if (generation == 0)
      return nullptr;

    const auto &slot = voices[static_cast<size_t>(slotFromToken(token))];
    if (slot.state.load(std::memory_order_acquire) != VoiceState::Active ||
        slot.generation.load(std::memory_order_acquire) != generation) {
      return nullptr;
    }
    return &slot;
  }

  void clearVoice(VoiceSlot &slot) noexcept {
    slot.stream.reset();
    slot.stretcher.reset();
    slot.latencyInputFrames.store(0.0, std::memory_order_relaxed);
  }

  bool consumeForcedVoiceBuildFailureForTests() noexcept {
    int remaining =
        forcedVoiceBuildFailuresForTests.load(std::memory_order_relaxed);
    while (remaining > 0) {
      if (forcedVoiceBuildFailuresForTests.compare_exchange_weak(
              remaining, remaining - 1, std::memory_order_relaxed,
              std::memory_order_relaxed)) {
        return true;
      }
    }
    return false;
  }

  bool prewarmVoice(Stream &stream, double &stableLatencyInputFrames) {
    const int warmupFrames = std::min(maxInputFrames, maxOutputFrames);
    if (warmupFrames <= 0 || channels <= 0)
      return false;

    std::vector<float> warmupOutput(
        static_cast<size_t>(warmupFrames) * static_cast<size_t>(channels),
        0.0f);
    std::vector<float *> warmupOutputPointers(
        static_cast<size_t>(channels), nullptr);
    for (int channel = 0; channel < channels; ++channel) {
      warmupOutputPointers[static_cast<size_t>(channel)] =
          warmupOutput.data() +
          (static_cast<size_t>(channel) * static_cast<size_t>(warmupFrames));
    }

    const int64_t maxWarmupOutputFrames =
        static_cast<int64_t>(kMaxWarmupLatencyFrames) +
        (16LL * static_cast<int64_t>(warmupFrames));
    int64_t warmedOutputFrames = 0;
    int stableLatencyBlocks = 0;
    double lastObservedLatency = -1.0;

    while (warmedOutputFrames < maxWarmupOutputFrames &&
           stableLatencyBlocks < kLatencyStableBlocks) {
      const int produced =
          stream.process(nullptr, warmupOutputPointers.data(), warmupFrames,
                         static_cast<double>(warmupFrames), 1.0);
      if (produced <= 0)
        return false;
      warmedOutputFrames += produced;

      const double observedLatency = stream.latency();
      if (!std::isfinite(observedLatency) || observedLatency <= 0.0)
        continue;

      stableLatencyBlocks =
          lastObservedLatency > 0.0 &&
                  std::abs(observedLatency - lastObservedLatency) <= 2.0
              ? stableLatencyBlocks + 1
              : 1;
      lastObservedLatency = observedLatency;

      if (warmedOutputFrames <
          static_cast<int64_t>(std::ceil(observedLatency)) +
              (2LL * static_cast<int64_t>(warmupFrames))) {
        stableLatencyBlocks = 0;
      }
    }

    if (stableLatencyBlocks < kLatencyStableBlocks ||
        !std::isfinite(lastObservedLatency) ||
        lastObservedLatency <= 0.0) {
      return false;
    }

    stableLatencyInputFrames = lastObservedLatency;
    return true;
  }

  bool constructVoice(VoiceSlot &slot) noexcept {
    if (consumeForcedVoiceBuildFailureForTests())
      return false;

    try {
      const Bungee::SampleRates rates{
          static_cast<int>(std::lround(sampleRate)),
          static_cast<int>(std::lround(sampleRate))};
      auto newStretcher =
          std::make_unique<Stretcher>(rates, channels, -1);
      auto newStream =
          std::make_unique<Stream>(*newStretcher, maxInputFrames, channels);
      double stableLatencyInputFrames = 0.0;
      if (!prewarmVoice(*newStream, stableLatencyInputFrames))
        return false;

      clearVoice(slot);
      slot.stretcher = std::move(newStretcher);
      slot.stream = std::move(newStream);
      slot.latencyInputFrames.store(stableLatencyInputFrames,
                                    std::memory_order_relaxed);
      return true;
    } catch (...) {
      return false;
    }
  }

  bool activateReadyVoice(const bool countAsReset) noexcept {
    const uint64_t oldToken =
        activeVoiceToken.load(std::memory_order_acquire);
    const uint64_t oldGeneration = generationFromToken(oldToken);
    const int oldSlot =
        oldGeneration == 0 ? -1 : slotFromToken(oldToken);

    for (int candidateSlot = 0; candidateSlot < 2; ++candidateSlot) {
      if (candidateSlot == oldSlot)
        continue;

      auto &candidate = voices[static_cast<size_t>(candidateSlot)];
      auto expected = VoiceState::Ready;
      if (!candidate.state.compare_exchange_strong(
              expected, VoiceState::Active, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        continue;
      }

      const uint64_t candidateGeneration =
          candidate.generation.load(std::memory_order_acquire);
      if (candidateGeneration == 0) {
        candidate.state.store(VoiceState::Ready, std::memory_order_release);
        continue;
      }

      activeVoiceToken.store(
          makeVoiceToken(candidateSlot, candidateGeneration),
          std::memory_order_release);
      if (oldSlot >= 0) {
        voices[static_cast<size_t>(oldSlot)].state.store(
            VoiceState::Retired, std::memory_order_release);
      }
      resetPending.store(false, std::memory_order_release);
      if (countAsReset)
        resetSuccessCount.fetch_add(1, std::memory_order_relaxed);
      return true;
    }
    return false;
  }

  void recordServiceFailure() noexcept {
    serviceFailureCount.fetch_add(1, std::memory_order_relaxed);
    consecutiveServiceFailures =
        std::min(consecutiveServiceFailures + 1U, 16U);
    const unsigned int shift =
        std::min(consecutiveServiceFailures - 1U, 7U);
    const int retryMilliseconds = std::min(
        kServiceRetryMaxMilliseconds,
        kServiceRetryBaseMilliseconds * (1 << shift));
    nextServiceRetry = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(retryMilliseconds);
  }

  void clearServiceBackoff() noexcept {
    consecutiveServiceFailures = 0;
    nextServiceRetry = {};
  }
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
  impl->resetPending.store(false, std::memory_order_relaxed);
  impl->resetSuccessCount.store(0, std::memory_order_relaxed);
  impl->resetMissCount.store(0, std::memory_order_relaxed);
  impl->serviceFailureCount.store(0, std::memory_order_relaxed);
  impl->clearServiceBackoff();
  return rebuild();
}

bool BungeeProcessor::rebuild() {
  if (impl->sampleRate <= 0.0 || impl->channels <= 0 ||
      impl->maxInputFrames <= 0) {
    return false;
  }

  impl->activeVoiceToken.store(0, std::memory_order_release);
  impl->resetPending.store(false, std::memory_order_release);
  for (auto &voice : impl->voices)
    voice.state.store(Impl::VoiceState::Rebuilding,
                      std::memory_order_release);

  if (!impl->constructVoice(impl->voices[0]) ||
      !impl->constructVoice(impl->voices[1])) {
    for (auto &voice : impl->voices) {
      impl->clearVoice(voice);
      voice.state.store(Impl::VoiceState::Empty, std::memory_order_release);
    }
    return false;
  }

  const uint64_t activeGeneration = Impl::nextGeneration(
      impl->voices[0].generation.load(std::memory_order_relaxed));
  const uint64_t readyGeneration = Impl::nextGeneration(
      impl->voices[1].generation.load(std::memory_order_relaxed));
  impl->voices[0].generation.store(activeGeneration,
                                    std::memory_order_relaxed);
  impl->voices[1].generation.store(readyGeneration,
                                    std::memory_order_relaxed);
  impl->voices[0].state.store(Impl::VoiceState::Active,
                              std::memory_order_release);
  impl->voices[1].state.store(Impl::VoiceState::Ready,
                              std::memory_order_release);
  impl->activeVoiceToken.store(Impl::makeVoiceToken(0, activeGeneration),
                               std::memory_order_release);
  impl->inputFrameRemainder = 0.0;
  impl->clearServiceBackoff();
  return true;
}

void BungeeProcessor::reset() noexcept {
  impl->inputFrameRemainder = 0.0;
  impl->resetPending.store(true, std::memory_order_release);
  if (!impl->activateReadyVoice(true))
    impl->resetMissCount.fetch_add(1, std::memory_order_relaxed);
}

bool BungeeProcessor::servicePendingRebuild() noexcept {
  if (impl->sampleRate <= 0.0 || impl->channels <= 0 ||
      impl->maxInputFrames <= 0) {
    return false;
  }

  if (std::chrono::steady_clock::now() < impl->nextServiceRetry)
    return false;

  for (auto &voice : impl->voices) {
    auto expected = Impl::VoiceState::Retired;
    if (!voice.state.compare_exchange_strong(
            expected, Impl::VoiceState::Rebuilding,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      expected = Impl::VoiceState::Empty;
      if (!voice.state.compare_exchange_strong(
              expected, Impl::VoiceState::Rebuilding,
              std::memory_order_acq_rel, std::memory_order_acquire)) {
        continue;
      }
    }
    const auto retryState = expected;

    if (!impl->constructVoice(voice)) {
      voice.state.store(retryState, std::memory_order_release);
      impl->recordServiceFailure();
      return false;
    }

    const uint64_t generation = Impl::nextGeneration(
        voice.generation.load(std::memory_order_relaxed));
    voice.generation.store(generation, std::memory_order_relaxed);
    voice.state.store(Impl::VoiceState::Ready, std::memory_order_release);
    impl->clearServiceBackoff();
    return true;
  }
  return false;
}

bool BungeeProcessor::tryResolvePendingReset() noexcept {
  if (impl->resetPending.load(std::memory_order_acquire))
    return impl->activateReadyVoice(true);

  if (impl->activeVoice() == nullptr)
    return impl->activateReadyVoice(false);

  return true;
}

void BungeeProcessor::setPitchSemiTones(const int semitones) noexcept {
  impl->pitchSemitones = std::clamp(semitones, -12, 12);
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
  if (!tryResolvePendingReset()) {
    output.clear();
    return 0;
  }

  auto *voice = impl->activeVoice();
  if (voice == nullptr || !voice->stream || inputFrames <= 0 ||
      outputFrames <= 0 ||
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
    produced = voice->stream->process(
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
    const double latency = voice->stream->latency();
    if (std::isfinite(latency))
      voice->latencyInputFrames.store(std::max(0.0, latency),
                                      std::memory_order_relaxed);
  }
  return produced;
}

int BungeeProcessor::getPitchSemiTones() const noexcept {
  return impl->pitchSemitones;
}

double BungeeProcessor::getTempoRatio() const noexcept {
  return impl->tempoRatio;
}

int BungeeProcessor::getOutputLatencySamples() const noexcept {
  const auto *voice = impl->activeVoice();
  if (voice == nullptr)
    return 0;
  return inputLatencyFramesToOutputSamples(
      voice->latencyInputFrames.load(std::memory_order_relaxed),
      impl->tempoRatio);
}

int BungeeProcessor::getNumChannels() const noexcept {
  return impl->channels;
}

bool BungeeProcessor::isPrepared() const noexcept {
  return impl->activeVoice() != nullptr;
}

bool BungeeProcessor::hasReadyVoice() const noexcept {
  for (const auto &voice : impl->voices) {
    if (voice.state.load(std::memory_order_acquire) ==
        Impl::VoiceState::Ready) {
      return true;
    }
  }
  return false;
}

bool BungeeProcessor::hasPendingReset() const noexcept {
  return impl->resetPending.load(std::memory_order_acquire);
}

uint64_t BungeeProcessor::getResetSuccessCount() const noexcept {
  return impl->resetSuccessCount.load(std::memory_order_relaxed);
}

uint64_t BungeeProcessor::getResetMissCount() const noexcept {
  return impl->resetMissCount.load(std::memory_order_relaxed);
}

uint64_t BungeeProcessor::getServiceFailureCount() const noexcept {
  return impl->serviceFailureCount.load(std::memory_order_relaxed);
}

uint64_t BungeeProcessor::getActiveVoiceGeneration() const noexcept {
  return Impl::generationFromToken(
      impl->activeVoiceToken.load(std::memory_order_acquire));
}

int BungeeProcessor::getActiveVoiceSlotForDiagnostics() const noexcept {
  const uint64_t token =
      impl->activeVoiceToken.load(std::memory_order_acquire);
  return Impl::generationFromToken(token) == 0
             ? -1
             : Impl::slotFromToken(token);
}

const char *BungeeProcessor::backendName() const noexcept {
  return isPrepared() ? "bungee_basic" : "unavailable";
}

void BungeeProcessor::setForcedVoiceBuildFailuresForTests(
    const int failureCount) noexcept {
  impl->forcedVoiceBuildFailuresForTests.store(
      std::max(0, failureCount), std::memory_order_relaxed);
}
