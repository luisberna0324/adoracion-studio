#pragma once

#include "ResamplingPositionableSource.h"
#include "StemBufferingSource.h"
#include "audio/SignalIntegrityMetrics.h"
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
            int readAheadSamples = 262144, bool useReadAhead = true,
            std::unique_ptr<juce::AudioFormatReader> prefetchReader = {});
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
  void requestPrefetchPosition(juce::int64 position,
                               int minimumFrames) noexcept;
  bool isPrefetchReady(juce::int64 position,
                       int minimumFrames) const noexcept;
  // Audio-thread only. The next source read is assembled as
  // [current timeline prefix][target timeline suffix] without allocating.
  void scheduleReadDiscontinuity(int prefixFrames, juce::int64 targetFrame,
                                 int fadeFrames = 64) noexcept;

  // ==============================================================================
  // Lock-free Mix Controls (Written from UI, read from Audio Thread)

  void setVolume(float newVolume) noexcept { volume.store(newVolume, std::memory_order_relaxed); }
  float getVolume() const noexcept { return volume.load(std::memory_order_relaxed); }

  // Transient practice balance; independent of persisted fader/trim settings.
  void setRehearsalGain(float value) noexcept {
    rehearsalGain.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_relaxed);
  }

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
  void setSignalIntegrityCaptureEnabled(bool enabled,
                                        bool fullCapture = true) noexcept {
    signalIntegrityTrackingEnabled = enabled;
    signalIntegrityFullCaptureEnabled = enabled && fullCapture;
  }
  const playworship::audio::SignalIntegrityMetrics&
  getSignalIntegrityMetricsForDiagnostics() const noexcept {
    return signalIntegrityMetrics;
  }
  juce::int64 getRealtimeReadMissCount() const noexcept {
    return bufferingSource != nullptr ? bufferingSource->getRealtimeMissCount()
                                      : 0;
  }
  juce::int64 getRealtimeMissingFrameCount() const noexcept {
    return bufferingSource != nullptr
               ? bufferingSource->getRealtimeMissingFrameCount()
               : 0;
  }

  /**
   * Returns the underlying StemBufferingSource so the benchmark can sample
   * its worker-drain atomics. Lifetime is tied to this StemTrack, so the
   * returned pointer is valid as long as the StemTrack itself is.
   * Returns nullptr if the buffering source has not been initialised yet.
   */
  StemBufferingSource *bufferingSourceForBenchmark() const noexcept {
    return bufferingSource.get();
  }

  /**
   * Returns the main ResamplingPositionableSource so the benchmark can
   * sample its decode+resample atomics. The background worker feeds
   * decoded AAC through this on every useTimeSlice() that actually
   * does work. May be nullptr before prepareToPlay.
   */
  ResamplingPositionableSource *resamplingSourceForBenchmark() const noexcept {
    return resamplingSource.get();
  }

private:
  std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
  std::unique_ptr<ResamplingPositionableSource> resamplingSource;
  // P0.2 (perf): el segundo AudioFormatReader se mantiene crudo hasta
  // el primer requestPrefetchPosition. Esto evita pagar el segundo
  // FileInputStream::open y la duplicación del costo de AV en el
  // loadSongStems. Cuando la cadena de prefetch se necesita (seek,
  // loop, arrangement handoff), se construye en lazy.
  std::unique_ptr<juce::AudioFormatReaderSource> prefetchReaderSource;
  std::unique_ptr<ResamplingPositionableSource> prefetchResamplingSource;
  std::unique_ptr<juce::AudioFormatReader> deferredPrefetchReader;
  double prefetchSampleRate = 0.0;
  int prefetchBlockSize = 0;
  std::once_flag prefetchChainBuilt;
  void ensurePrefetchChain();
  std::unique_ptr<StemBufferingSource> bufferingSource;
  juce::PositionableAudioSource *activeSource = nullptr;

  std::atomic<float> volume{1.0f};
  std::atomic<float> rehearsalGain{1.0f};
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rehearsalRamp{1.0f};
  std::atomic<float> trim{1.0f};
  std::atomic<bool> muted{false};
  std::atomic<bool> soloed{false};
  std::atomic<float> globalMixGain{1.0f};
  std::atomic<bool> isAnySoloActive{false};
  std::atomic<float> currentMagnitude{0.0f};
  bool signalIntegrityTrackingEnabled = false;
  bool signalIntegrityFullCaptureEnabled = false;
  playworship::audio::SignalIntegrityMetrics signalIntegrityMetrics;
  playworship::audio::SignalIntegrityState signalIntegrityState;
  bool readDiscontinuityPending = false;
  int readDiscontinuityPrefixFrames = 0;
  int readDiscontinuityFadeFrames = 0;
  int pendingLeadingDiscontinuityFadeFrames = 0;
  juce::int64 readDiscontinuityTargetFrame = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemTrack)
};
