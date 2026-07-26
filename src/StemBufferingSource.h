#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

class StemBufferingSource : public juce::PositionableAudioSource,
                            private juce::TimeSliceClient {
public:
  static_assert(std::atomic<juce::int64>::is_always_lock_free,
                "Stem streaming requires lock-free frame atomics");
  static_assert(std::atomic<uint64_t>::is_always_lock_free,
                "Stem streaming requires lock-free generations");
  static_assert(std::atomic<int>::is_always_lock_free,
                "Stem streaming requires lock-free request sizes");
  static_assert(std::atomic<bool>::is_always_lock_free,
                "Stem streaming requires lock-free lifecycle publication");

  StemBufferingSource(juce::PositionableAudioSource *source,
                      juce::TimeSliceThread &backgroundThread,
                      bool deleteSourceWhenDeleted, int numberOfSamplesToBuffer,
                      int numberOfChannels = 2,
                      bool prefillBufferOnPrepareToPlay = true,
                      juce::PositionableAudioSource *prefetchSource = nullptr);

  ~StemBufferingSource() override;

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void releaseResources() override;
  void getNextAudioBlock(const juce::AudioSourceChannelInfo &) override;

  void setNextReadPosition(juce::int64 newPosition) override;
  juce::int64 getNextReadPosition() const override;
  juce::int64 getTotalLength() const override { return source->getTotalLength(); }
  bool isLooping() const override { return source->isLooping(); }
  void setLooping(bool shouldLoop) override;

  // Lock-free request used by the audio thread to prepare a future seek/loop
  // target without invalidating the currently audible read-ahead window.
  void requestPrefetchPosition(juce::int64 position,
                               int minimumFrames) noexcept;
  bool isPrefetchReady(juce::int64 position,
                       int minimumFrames) const noexcept;

  bool waitForNextAudioBlockReady(const juce::AudioSourceChannelInfo &info,
                                  juce::uint32 timeout);
  juce::int64 getRealtimeMissCount() const noexcept {
    return realtimeMissCount.load(std::memory_order_relaxed);
  }
  juce::int64 getRealtimeMissingFrameCount() const noexcept {
    return realtimeMissingFrameCount.load(std::memory_order_relaxed);
  }

#if defined(MTCONTROLS_ENABLE_TEST_HOOKS) && MTCONTROLS_ENABLE_TEST_HOOKS
  using BeforeCursorAdvanceTestHook = void (*)(StemBufferingSource &) noexcept;
  void setBeforeCursorAdvanceTestHook(
      BeforeCursorAdvanceTestHook hook) noexcept {
    beforeCursorAdvanceTestHook = hook;
  }
#endif

private:
  struct PrefetchRangeSnapshot {
    juce::int64 start = 0;
    juce::int64 end = 0;
    uint64_t generation = 0;
    bool valid = false;
  };

  juce::Range<int> getValidBufferRange(int numSamples,
                                       juce::int64 position) const;
  juce::Range<int> getValidPrefetchRange(int numSamples,
                                         juce::int64 position) const;
  PrefetchRangeSnapshot readPrefetchRangeSnapshot() const noexcept;
  void publishValidBufferRange(juce::int64 start, juce::int64 end,
                               uint64_t epoch) noexcept;
  void publishPrefetchRange(juce::int64 start, juce::int64 end,
                            uint64_t generation) noexcept;
  bool readPendingPrefetch();
  bool readNextBufferChunk();
  void readBufferSection(juce::int64 start, int length, int bufferOffset);
  int useTimeSlice() override;

  juce::OptionalScopedPointer<juce::PositionableAudioSource> source;
  // Independent chain (reader + resampler) owned by StemTrack. Seeking this
  // source cannot flush or reposition the currently audible chain.
  juce::PositionableAudioSource *prefetchSource = nullptr;
  juce::TimeSliceThread &backgroundThread;
  int numberOfSamplesToBuffer, numberOfChannels;
  juce::AudioBuffer<float> buffer;
  // Background-thread staging buffer to avoid holding the shared lock during disk reads.
  juce::AudioBuffer<float> readBuffer;
  juce::CriticalSection callbackLock;
  // A compact second bank keeps the next seek/loop target available while the
  // main ring continues serving the current timeline.
  juce::AudioBuffer<float> prefetchBuffer;
  juce::CriticalSection prefetchLock;
  juce::WaitableEvent bufferReadyEvent;
  std::atomic<juce::int64> bufferValidStart{0};
  std::atomic<juce::int64> bufferValidEnd{0};
  // The worker is the sole range publisher. A seqlock makes the start/end
  // pair coherent without making the audio callback wait, while the epoch
  // invalidates an old window immediately when another thread requests a seek.
  std::atomic<uint64_t> bufferRangeSequence{0};
  std::atomic<uint64_t> requestedBufferEpoch{1};
  std::atomic<uint64_t> publishedBufferEpoch{0};
  uint64_t workerBufferEpoch = 0;
  // Independent from the buffer epoch: a seek advances this before publishing
  // its destination so an in-flight callback cannot advance that destination.
  std::atomic<uint64_t> cursorEpoch{0};
  std::atomic<juce::int64> nextPlayPos{0};
  std::atomic<juce::int64> requestedPrefetchPosition{0};
  std::atomic<int> requestedPrefetchFrames{0};
  std::atomic<int> prefetchCapacityFrames{0};
  std::atomic<uint64_t> requestedPrefetchGeneration{0};
  std::atomic<uint64_t> pinnedPrefetchGeneration{0};
  std::atomic<int> lastAudioBlockFrames{0};
  uint64_t workerPrefetchGeneration = 0;
  std::atomic<uint64_t> prefetchRangeSequence{0};
  std::atomic<juce::int64> prefetchedRangeStart{0};
  std::atomic<juce::int64> prefetchedRangeEnd{0};
  std::atomic<uint64_t> publishedPrefetchGeneration{0};
  std::atomic<juce::int64> realtimeMissCount{0};
  std::atomic<juce::int64> realtimeMissingFrameCount{0};
#if defined(MTCONTROLS_ENABLE_TEST_HOOKS) && MTCONTROLS_ENABLE_TEST_HOOKS
  BeforeCursorAdvanceTestHook beforeCursorAdvanceTestHook = nullptr;
#endif
  double sampleRate = 0;
  bool wasSourceLooping = false;
  std::atomic<bool> isPrepared{false};
  const bool prefillBuffer;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemBufferingSource)
};
