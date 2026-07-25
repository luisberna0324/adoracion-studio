#pragma once

#include "ResamplingPositionableSource.h"
#include "StemBufferingSource.h"
#include <JuceHeader.h>
#include <atomic>

/**
 * StemTrack encapsulates the hybrid streaming pipeline for a single audio stem.
 * 
 * Pipeline:
 * AudioFormatReader -> AudioFormatReaderSource -> BufferingAudioSource
 * 
 * It acts as a PositionableAudioSource that can be mixed into the global MixerAudioSource,
 * providing lock-free mix controls (volume, mute, solo) evaluated in the audio thread.
 */
class StemTrack : public juce::PositionableAudioSource {
public:
  StemTrack(std::unique_ptr<juce::AudioFormatReader> reader,
            juce::TimeSliceThread &sharedReadThread, double sampleRateToUse,
            int readAheadSamples = 262144, bool useReadAhead = true);
  ~StemTrack() override;

  // ==============================================================================
  // PositionableAudioSource overrides
  void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void releaseResources() override;

  void setNextReadPosition(juce::int64 newPosition) override;
  juce::int64 getNextReadPosition() const override;
  juce::int64 getTotalLength() const override;
  bool isLooping() const override;
  void setLooping(bool shouldLoop) override;
  bool waitForNextAudioBlockReady(int numSamples,
                                  juce::uint32 timeoutMs) const;

  // ==============================================================================
  // Lock-free Mix Controls (Written from UI, read from Audio Thread)

  void setVolume(float newVolume) noexcept { volume.store(newVolume, std::memory_order_relaxed); }
  float getVolume() const noexcept { return volume.load(std::memory_order_relaxed); }

  void setTrim(float newTrim) noexcept { trim.store(newTrim, std::memory_order_relaxed); }
  float getTrim() const noexcept { return trim.load(std::memory_order_relaxed); }

  void setMuted(bool shouldMute) noexcept { muted.store(shouldMute, std::memory_order_relaxed); }
  bool isMuted() const noexcept { return muted.load(std::memory_order_relaxed); }

  void setSoloed(bool shouldSolo) noexcept { soloed.store(shouldSolo, std::memory_order_relaxed); }
  bool isSoloed() const noexcept { return soloed.load(std::memory_order_relaxed); }

  void setGlobalMixState(float mixGain, bool anySolo) noexcept {
    globalMixGain.store(mixGain, std::memory_order_relaxed);
    isAnySoloActive.store(anySolo, std::memory_order_relaxed);
  }

  float getMagnitude() const noexcept { return currentMagnitude.load(std::memory_order_relaxed); }
  juce::int64 getRealtimeReadMissCount() const noexcept {
    return bufferingSource != nullptr ? bufferingSource->getRealtimeMissCount()
                                      : 0;
  }
  juce::int64 getRealtimeMissingFrameCount() const noexcept {
    return bufferingSource != nullptr
               ? bufferingSource->getRealtimeMissingFrameCount()
               : 0;
  }

private:
  std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
  std::unique_ptr<ResamplingPositionableSource> resamplingSource;
  std::unique_ptr<StemBufferingSource> bufferingSource;
  juce::PositionableAudioSource *activeSource = nullptr;

  std::atomic<float> volume{1.0f};
  std::atomic<float> trim{1.0f};
  std::atomic<bool> muted{false};
  std::atomic<bool> soloed{false};
  std::atomic<float> globalMixGain{1.0f};
  std::atomic<bool> isAnySoloActive{false};
  std::atomic<float> currentMagnitude{0.0f};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemTrack)
};
