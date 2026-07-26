#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>
#include <cstdint>
#include <memory>

class BungeeProcessor {
public:
  enum class ContentType : uint8_t { Melodic = 0, Percussive = 1 };

  BungeeProcessor();
  ~BungeeProcessor();

  BungeeProcessor(const BungeeProcessor &) = delete;
  BungeeProcessor &operator=(const BungeeProcessor &) = delete;

  bool prepare(double sampleRate, int maxOutputFrames, int numChannels);
  // Destroys both Bungee voices and releases scratch-buffer storage. This may
  // deallocate and must only be called after audio processing and non-RT
  // maintenance have stopped. Pitch, tempo and content configuration survive
  // so a later prepare() restores the same logical settings.
  void releaseResources() noexcept;
  // Rebuilds both voice slots. Call only while audio processing is stopped.
  bool rebuild();
  // Allocation-free and lock-free. Swaps to the prepared spare voice and
  // retires the old one. If no clean voice is ready, the reset remains pending
  // and process() emits silence rather than leaking pre-discontinuity history.
  void reset() noexcept;
  // May allocate, pre-warm and destroy Bungee objects; call from a shared
  // non-RT service, never from process() or the audio callback. Empty slots
  // left by a failed prepare are also recoverable. Failed attempts use an
  // internal exponential backoff.
  bool servicePendingRebuild() noexcept;
  // Audio-thread preflight. Activates a clean Ready voice when a reset is
  // pending (or when prepare recovery has not published an active voice yet).
  // Returns false only while no clean voice can process the next block.
  bool tryResolvePendingReset() noexcept;
  void setPitchSemiTones(int semitones) noexcept;
  void setTempoRatio(double ratio) noexcept;
  void setContentType(ContentType type) noexcept;

  int requiredInputFrames(int outputFrames) noexcept;
  int process(const juce::AudioBuffer<float> &input, int inputFrames,
              juce::AudioBuffer<float> &output, int outputFrames) noexcept;
  void process(juce::AudioBuffer<float> &buffer) noexcept {
    process(buffer, buffer.getNumSamples(), buffer, buffer.getNumSamples());
  }

  int getPitchSemiTones() const noexcept;
  double getTempoRatio() const noexcept;
  // Bungee::Stream::latency() is reported in input frames. Public latency is
  // always converted to samples in the output timeline so callers can use it
  // directly for delay compensation.
  int getOutputLatencySamples() const noexcept;
  int getLatencySamples() const noexcept { return getOutputLatencySamples(); }
  int getNumChannels() const noexcept;
  bool isPrepared() const noexcept;
  bool hasReadyVoice() const noexcept;
  bool hasPendingReset() const noexcept;
  uint64_t getResetSuccessCount() const noexcept;
  uint64_t getResetMissCount() const noexcept;
  // Cumulative non-RT rebuild failures since the most recent prepare().
  uint64_t getServiceFailureCount() const noexcept;
  uint64_t getActiveVoiceGeneration() const noexcept;
  int getActiveVoiceSlotForDiagnostics() const noexcept;
  const char *backendName() const noexcept;

private:
  friend struct BungeeProcessorTestPeer;
  void setForcedVoiceBuildFailuresForTests(int failureCount) noexcept;
  int getAllocatedVoiceCountForTests() const noexcept;
  size_t getScratchPointerCapacityForTests() const noexcept;
  double getCurrentPitchSemiTonesForTests() const noexcept;
  int getPitchRampFramesRemainingForTests() const noexcept;
  int getLastProcessSegmentCountForTests() const noexcept;
  int getLastProcessedInputFramesForTests() const noexcept;
  int getLastProducedOutputFramesForTests() const noexcept;
  double getLastAppliedPitchScaleForTests(int segment) const noexcept;

  struct Impl;
  std::unique_ptr<Impl> impl;
};
