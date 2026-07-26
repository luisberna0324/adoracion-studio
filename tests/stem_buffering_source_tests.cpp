#include "../src/StemBufferingSource.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

class RampSource final : public juce::PositionableAudioSource {
public:
  explicit RampSource(const juce::int64 sourceLength,
                      const float sampleBias = 0.0f)
      : length(sourceLength), bias(sampleBias) {}

  static float sampleAt(const juce::int64 position) {
    return static_cast<float>((position % 1000) + 1) / 1000.0f;
  }

  void prepareToPlay(int, double) override {}
  void releaseResources() override {}

  void getNextAudioBlock(
      const juce::AudioSourceChannelInfo &info) override {
    info.clearActiveBufferRegion();

    for (int sample = 0; sample < info.numSamples; ++sample) {
      auto sourcePosition = position + sample;
      if (looping && length > 0)
        sourcePosition %= length;

      if (sourcePosition < 0 || sourcePosition >= length)
        continue;

      const auto value = sampleAt(sourcePosition) + bias;
      for (int channel = 0; channel < info.buffer->getNumChannels();
           ++channel) {
        info.buffer->setSample(channel, info.startSample + sample, value);
      }
    }

    position += info.numSamples;
  }

  void setNextReadPosition(const juce::int64 newPosition) override {
    setPositionCallCount.fetch_add(1, std::memory_order_relaxed);
    position = newPosition;
  }

  int getSetPositionCallCount() const noexcept {
    return setPositionCallCount.load(std::memory_order_relaxed);
  }

  juce::int64 getNextReadPosition() const override {
    return looping && position > 0 && length > 0 ? position % length
                                                 : position;
  }

  juce::int64 getTotalLength() const override { return length; }
  bool isLooping() const override { return looping; }
  void setLooping(const bool shouldLoop) override { looping = shouldLoop; }

private:
  juce::int64 position = 0;
  const juce::int64 length;
  const float bias;
  bool looping = false;
  std::atomic<int> setPositionCallCount{0};
};

void expectNear(const float actual, const float expected,
                const char *message) {
  expect(std::abs(actual - expected) < 1.0e-6f, message);
}

void testEmptyReadAheadAdvancesAndCountsMissingFrames() {
  RampSource source(100000);
  juce::TimeSliceThread inactiveReadThread("inactive read thread");
  StemBufferingSource bufferingSource(&source, inactiveReadThread, false,
                                      32768, 2, false);
  bufferingSource.prepareToPlay(128, 48000.0);

  juce::AudioBuffer<float> output(2, 144);
  for (int channel = 0; channel < output.getNumChannels(); ++channel)
    for (int sample = 0; sample < output.getNumSamples(); ++sample)
      output.setSample(channel, sample, 1.0f);

  const juce::AudioSourceChannelInfo info(&output, 8, 128);
  bufferingSource.getNextAudioBlock(info);

  for (int channel = 0; channel < output.getNumChannels(); ++channel) {
    for (int sample = 0; sample < 8; ++sample)
      expectNear(output.getSample(channel, sample), 1.0f,
                 "samples before active region remain untouched");
    for (int sample = 8; sample < 136; ++sample)
      expectNear(output.getSample(channel, sample), 0.0f,
                 "empty read-ahead range produces silence");
    for (int sample = 136; sample < output.getNumSamples(); ++sample)
      expectNear(output.getSample(channel, sample), 1.0f,
                 "samples after active region remain untouched");
  }

  expect(bufferingSource.getNextReadPosition() == 128,
         "empty read-ahead range advances one full callback");
  expect(bufferingSource.getRealtimeMissCount() == 1,
         "empty read-ahead range counts one callback miss");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 128,
         "empty read-ahead range counts every missing frame");

  bufferingSource.getNextAudioBlock(info);
  expect(bufferingSource.getNextReadPosition() == 256,
         "consecutive empty range advances exactly once");
  expect(bufferingSource.getRealtimeMissCount() == 2,
         "consecutive empty range counts another callback miss");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 256,
         "missing-frame metric is cumulative");
}

void testPartialReadAheadPreservesValidAudioAndAdvances() {
  constexpr int ringCapacity = 32768;
  constexpr int ringSafetyGap = 4;
  constexpr int validFrames = ringCapacity - ringSafetyGap;
  constexpr int consumedFrames = 32600;
  constexpr int requestedFrames = 256;
  constexpr int readableFrames = validFrames - consumedFrames;
  constexpr int missingFrames = requestedFrames - readableFrames;

  RampSource source(100000);
  juce::TimeSliceThread readThread("partial read thread");
  expect(readThread.startThread(), "background read thread starts");

  StemBufferingSource bufferingSource(&source, readThread, false,
                                      ringCapacity, 2, false);
  bufferingSource.prepareToPlay(512, 48000.0);

  juce::AudioBuffer<float> readinessBuffer(2, consumedFrames);
  const juce::AudioSourceChannelInfo readinessInfo(&readinessBuffer, 0,
                                                   consumedFrames);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "initial read-ahead window becomes ready");
  readThread.stopThread(2000);

  juce::AudioBuffer<float> consumed(2, consumedFrames);
  const juce::AudioSourceChannelInfo consumedInfo(&consumed, 0,
                                                  consumedFrames);
  bufferingSource.getNextAudioBlock(consumedInfo);
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "fully buffered block does not count as a miss");

  juce::AudioBuffer<float> partial(2, requestedFrames);
  for (int channel = 0; channel < partial.getNumChannels(); ++channel)
    for (int sample = 0; sample < partial.getNumSamples(); ++sample)
      partial.setSample(channel, sample, -1.0f);

  const juce::AudioSourceChannelInfo partialInfo(&partial, 0,
                                                 requestedFrames);
  bufferingSource.getNextAudioBlock(partialInfo);

  for (int channel = 0; channel < partial.getNumChannels(); ++channel) {
    for (int sample = 0; sample < readableFrames; ++sample) {
      expectNear(partial.getSample(channel, sample),
                 RampSource::sampleAt(consumedFrames + sample),
                 "partial range preserves buffered audio");
    }
    for (int sample = readableFrames; sample < requestedFrames; ++sample)
      expectNear(partial.getSample(channel, sample), 0.0f,
                 "partial range fills the missing suffix with silence");
  }

  expect(bufferingSource.getNextReadPosition() ==
             consumedFrames + requestedFrames,
         "partial range advances one full callback");
  expect(bufferingSource.getRealtimeMissCount() == 1,
         "partial range counts one callback miss");
  expect(bufferingSource.getRealtimeMissingFrameCount() == missingFrames,
         "partial range counts only unavailable frames");

  juce::AudioBuffer<float> empty(2, 64);
  const juce::AudioSourceChannelInfo emptyInfo(&empty, 0, 64);
  bufferingSource.getNextAudioBlock(emptyInfo);
  expect(bufferingSource.getNextReadPosition() ==
             consumedFrames + requestedFrames + 64,
         "empty recovery block advances exactly once");
  expect(bufferingSource.getRealtimeMissCount() == 2,
         "empty recovery block counts one additional miss");
  expect(bufferingSource.getRealtimeMissingFrameCount() ==
             missingFrames + 64,
             "empty recovery block adds all unavailable frames");
}

void testEmptyLoopingRangeAdvancesModuloSourceLength() {
  RampSource source(1000);
  source.setLooping(true);
  juce::TimeSliceThread inactiveReadThread("inactive loop read thread");
  StemBufferingSource bufferingSource(&source, inactiveReadThread, false,
                                      32768, 2, false);
  bufferingSource.prepareToPlay(128, 48000.0);
  bufferingSource.setNextReadPosition(990);

  juce::AudioBuffer<float> output(2, 128);
  const juce::AudioSourceChannelInfo info(&output, 0, 128);
  bufferingSource.getNextAudioBlock(info);

  expect(bufferingSource.getNextReadPosition() == 118,
         "empty looping block advances monotonically and reports modulo");
  expect(bufferingSource.getRealtimeMissCount() == 1,
         "empty looping block counts one callback miss");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 128,
         "empty looping block counts all unavailable frames");
}

void testBufferedLoopCrossingPreservesAudio() {
  constexpr int sourceLength = 1000;
  RampSource source(sourceLength);
  source.setLooping(true);
  juce::TimeSliceThread readThread("buffered loop read thread");
  expect(readThread.startThread(), "loop read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false,
                                      32768, 2, false);
  bufferingSource.prepareToPlay(128, 48000.0);

  juce::AudioBuffer<float> readiness(2, 1118);
  const juce::AudioSourceChannelInfo readinessInfo(&readiness, 0, 1118);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "looping source prebuffers across its boundary");
  readThread.stopThread(2000);

  juce::AudioBuffer<float> prefix(2, 990);
  const juce::AudioSourceChannelInfo prefixInfo(&prefix, 0, 990);
  bufferingSource.getNextAudioBlock(prefixInfo);

  juce::AudioBuffer<float> crossing(2, 128);
  const juce::AudioSourceChannelInfo crossingInfo(&crossing, 0, 128);
  bufferingSource.getNextAudioBlock(crossingInfo);
  for (int channel = 0; channel < crossing.getNumChannels(); ++channel) {
    for (int sample = 0; sample < crossing.getNumSamples(); ++sample) {
      expectNear(
          crossing.getSample(channel, sample),
          RampSource::sampleAt((990 + sample) % sourceLength),
          "buffered loop crossing reads the wrapped source position");
    }
  }
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "fully published loop crossing is not an underrun");
  expect(bufferingSource.getNextReadPosition() == 118,
         "buffered loop crossing reports the wrapped cursor");
}

void testPublishedEofSilenceIsNotAnUnderrun() {
  RampSource source(100);
  juce::TimeSliceThread readThread("EOF read thread");
  expect(readThread.startThread(), "EOF read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false,
                                      32768, 2, false);
  bufferingSource.prepareToPlay(128, 48000.0);

  juce::AudioBuffer<float> output(2, 128);
  const juce::AudioSourceChannelInfo info(&output, 0, 128);
  expect(bufferingSource.waitForNextAudioBlockReady(info, 2000),
         "worker publishes source data and EOF silence");
  readThread.stopThread(2000);
  bufferingSource.getNextAudioBlock(info);

  for (int channel = 0; channel < output.getNumChannels(); ++channel) {
    for (int sample = 0; sample < 100; ++sample)
      expectNear(output.getSample(channel, sample),
                 RampSource::sampleAt(sample),
                 "EOF block keeps available source audio");
    for (int sample = 100; sample < 128; ++sample)
      expectNear(output.getSample(channel, sample), 0.0f,
                 "worker-published EOF region is silent");
  }
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "published EOF silence is not counted as missing read-ahead");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 0,
         "published EOF silence adds no missing frames");
}

void testPrefetchedSeekKeepsCurrentWindowAndSuppliesTargetImmediately() {
  constexpr juce::int64 target = 50000;
  constexpr int blockFrames = 512;
  RampSource source(100000);
  RampSource prefetchSource(100000);
  juce::TimeSliceThread readThread("prefetched seek read thread");
  expect(readThread.startThread(), "prefetch read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false, &prefetchSource);
  bufferingSource.prepareToPlay(blockFrames, 48000.0);

  juce::AudioBuffer<float> readiness(2, blockFrames);
  const juce::AudioSourceChannelInfo readinessInfo(&readiness, 0, blockFrames);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "current timeline window becomes ready");
  const auto mainSourceSeekCallsBeforePrefetch =
      source.getSetPositionCallCount();

  bufferingSource.requestPrefetchPosition(target, blockFrames);
  const auto deadline = juce::Time::getMillisecondCounter() + 2000U;
  while (!bufferingSource.isPrefetchReady(target, blockFrames) &&
         juce::Time::getMillisecondCounter() < deadline) {
    juce::Thread::sleep(1);
  }
  expect(bufferingSource.isPrefetchReady(target, blockFrames),
         "secondary target window becomes ready");
  readThread.stopThread(2000);
  expect(source.getSetPositionCallCount() ==
             mainSourceSeekCallsBeforePrefetch,
         "target prefetch never seeks the main playback source");
  expect(prefetchSource.getSetPositionCallCount() > 0,
         "target prefetch seeks only its independent source");

  juce::AudioBuffer<float> current(2, blockFrames);
  const juce::AudioSourceChannelInfo currentInfo(&current, 0, blockFrames);
  bufferingSource.getNextAudioBlock(currentInfo);
  for (int channel = 0; channel < current.getNumChannels(); ++channel) {
    for (int sample = 0; sample < blockFrames; ++sample) {
      expectNear(current.getSample(channel, sample),
                 RampSource::sampleAt(sample),
                 "prefetch does not replace the current audible window");
    }
  }

  const auto missesBeforeSeek = bufferingSource.getRealtimeMissCount();
  bufferingSource.setNextReadPosition(target);
  juce::AudioBuffer<float> jumped(2, blockFrames);
  const juce::AudioSourceChannelInfo jumpedInfo(&jumped, 0, blockFrames);
  bufferingSource.getNextAudioBlock(jumpedInfo);
  for (int channel = 0; channel < jumped.getNumChannels(); ++channel) {
    for (int sample = 0; sample < blockFrames; ++sample) {
      expectNear(jumped.getSample(channel, sample),
                 RampSource::sampleAt(target + sample),
                 "prefetched seek emits target audio in its first callback");
    }
  }
  expect(bufferingSource.getRealtimeMissCount() == missesBeforeSeek,
         "ready prefetched seek adds no streaming miss");
  expect(bufferingSource.getNextReadPosition() == target + blockFrames,
         "prefetched seek advances from its exact target");
}

void testSeekPrefetchBankCannotBeOverwrittenBeforeMainHandoff() {
  constexpr juce::int64 target = 50000;
  constexpr juce::int64 otherTarget = 70000;
  constexpr int blockFrames = 512;
  constexpr int prefetchedFrames = blockFrames * 2;

  RampSource source(100000);
  RampSource prefetchSource(100000);
  juce::TimeSliceThread readThread("prefetch pin read thread");
  expect(readThread.startThread(), "prefetch pin thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false, &prefetchSource);
  bufferingSource.prepareToPlay(blockFrames, 48000.0);

  juce::AudioBuffer<float> readiness(2, blockFrames);
  const juce::AudioSourceChannelInfo readinessInfo(&readiness, 0, blockFrames);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "prefetch pin current window becomes ready");

  bufferingSource.requestPrefetchPosition(target, prefetchedFrames);
  const auto deadline = juce::Time::getMillisecondCounter() + 2000U;
  while (!bufferingSource.isPrefetchReady(target, prefetchedFrames) &&
         juce::Time::getMillisecondCounter() < deadline) {
    juce::Thread::sleep(1);
  }
  expect(bufferingSource.isPrefetchReady(target, prefetchedFrames),
         "prefetch pin target bank covers two callbacks");
  readThread.stopThread(2000);

  bufferingSource.setNextReadPosition(target);
  juce::AudioBuffer<float> first(2, blockFrames);
  const juce::AudioSourceChannelInfo firstInfo(&first, 0, blockFrames);
  bufferingSource.getNextAudioBlock(firstInfo);

  bufferingSource.requestPrefetchPosition(otherTarget, blockFrames);
  expect(!bufferingSource.isPrefetchReady(otherTarget, blockFrames),
         "new target request is ignored while seek handoff is pinned");

  juce::AudioBuffer<float> second(2, blockFrames);
  const juce::AudioSourceChannelInfo secondInfo(&second, 0, blockFrames);
  bufferingSource.getNextAudioBlock(secondInfo);
  for (int channel = 0; channel < second.getNumChannels(); ++channel) {
    for (int sample = 0; sample < blockFrames; ++sample) {
      expectNear(second.getSample(channel, sample),
                 RampSource::sampleAt(target + blockFrames + sample),
                 "pinned target bank supplies the second post-seek block");
    }
  }
}

void testMainRingTakesPrecedenceOverOverlappingPrefetch() {
  constexpr int blockFrames = 512;
  constexpr float prefetchBias = 2.0f;

  RampSource source(100000);
  RampSource prefetchSource(100000, prefetchBias);
  juce::TimeSliceThread readThread("overlapping prefetch read thread");
  expect(readThread.startThread(), "overlapping prefetch thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false, &prefetchSource);
  bufferingSource.prepareToPlay(blockFrames, 48000.0);

  juce::AudioBuffer<float> readiness(2, blockFrames);
  const juce::AudioSourceChannelInfo readinessInfo(&readiness, 0, blockFrames);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "main window becomes ready before overlapping prefetch");

  bufferingSource.requestPrefetchPosition(0, blockFrames);
  const auto deadline = juce::Time::getMillisecondCounter() + 2000U;
  while (!bufferingSource.isPrefetchReady(0, blockFrames) &&
         juce::Time::getMillisecondCounter() < deadline) {
    juce::Thread::sleep(1);
  }
  expect(bufferingSource.isPrefetchReady(0, blockFrames),
         "overlapping target bank becomes ready");
  readThread.stopThread(2000);

  juce::AudioBuffer<float> output(2, blockFrames);
  const juce::AudioSourceChannelInfo outputInfo(&output, 0, blockFrames);
  bufferingSource.getNextAudioBlock(outputInfo);

  for (int channel = 0; channel < output.getNumChannels(); ++channel) {
    for (int sample = 0; sample < blockFrames; ++sample) {
      expectNear(output.getSample(channel, sample),
                 RampSource::sampleAt(sample),
                 "main ring wins every sample shared with target bank");
      expect(std::abs(output.getSample(channel, sample) -
                      (RampSource::sampleAt(sample) + prefetchBias)) >
                 1.0f,
             "overlapping prefetch cannot overwrite main ring audio");
    }
  }
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "fully overlapping ready banks produce no streaming miss");
}

void testPrefetchedTargetCanSplitOneCallbackAtExactBoundary() {
  constexpr juce::int64 target = 50000;
  constexpr int blockFrames = 512;
  constexpr int boundaryOffset = 488;
  constexpr int suffixFrames = blockFrames - boundaryOffset;

  RampSource source(100000);
  RampSource prefetchSource(100000);
  juce::TimeSliceThread readThread("split callback read thread");
  expect(readThread.startThread(), "split callback read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false, &prefetchSource);
  bufferingSource.prepareToPlay(blockFrames, 48000.0);

  juce::AudioBuffer<float> readiness(2, blockFrames);
  const juce::AudioSourceChannelInfo readinessInfo(&readiness, 0, blockFrames);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "split callback current window becomes ready");

  bufferingSource.requestPrefetchPosition(target, blockFrames);
  const auto deadline = juce::Time::getMillisecondCounter() + 2000U;
  while (!bufferingSource.isPrefetchReady(target, blockFrames) &&
         juce::Time::getMillisecondCounter() < deadline) {
    juce::Thread::sleep(1);
  }
  expect(bufferingSource.isPrefetchReady(target, blockFrames),
         "split callback target bank becomes ready");
  readThread.stopThread(2000);

  juce::AudioBuffer<float> output(2, blockFrames);
  const juce::AudioSourceChannelInfo prefixInfo(&output, 0, boundaryOffset);
  bufferingSource.getNextAudioBlock(prefixInfo);
  bufferingSource.setNextReadPosition(target);
  const juce::AudioSourceChannelInfo suffixInfo(
      &output, boundaryOffset, suffixFrames);
  bufferingSource.getNextAudioBlock(suffixInfo);

  for (int channel = 0; channel < output.getNumChannels(); ++channel) {
    for (int sample = 0; sample < boundaryOffset; ++sample) {
      expectNear(output.getSample(channel, sample),
                 RampSource::sampleAt(sample),
                 "split callback keeps every pre-boundary source frame");
    }
    for (int sample = 0; sample < suffixFrames; ++sample) {
      expectNear(output.getSample(channel, boundaryOffset + sample),
                 RampSource::sampleAt(target + sample),
                 "split callback starts target audio at the exact offset");
    }
  }

  expect(bufferingSource.getRealtimeMissCount() == 0,
         "fully prefetched split callback has no streaming miss");
  expect(bufferingSource.getNextReadPosition() == target + suffixFrames,
         "split callback clock advances only the target suffix after jump");
}

void testFarCursorCannotWrapRangeOffsets() {
  RampSource source(100000);
  juce::TimeSliceThread inactiveReadThread("inactive far cursor thread");
  StemBufferingSource bufferingSource(&source, inactiveReadThread, false,
                                      32768, 2, false);
  bufferingSource.prepareToPlay(128, 48000.0);

  constexpr juce::int64 farPosition =
      static_cast<juce::int64>(std::numeric_limits<int>::max()) - 63;
  bufferingSource.setNextReadPosition(farPosition);
  juce::AudioBuffer<float> output(2, 128);
  output.clear();
  const juce::AudioSourceChannelInfo info(&output, 0, 128);
  bufferingSource.getNextAudioBlock(info);

  expect(bufferingSource.getNextReadPosition() == farPosition + 128,
         "far cursor still advances exactly one callback");
  expect(bufferingSource.getRealtimeMissCount() == 1,
         "far empty range cannot wrap into a false valid interval");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 128,
         "far empty range counts every missing frame");
}

#if defined(MTCONTROLS_ENABLE_TEST_HOOKS) && MTCONTROLS_ENABLE_TEST_HOOKS
constexpr juce::int64 concurrentSeekDestination = 64000;

void seekImmediatelyBeforeCursorAdvance(
    StemBufferingSource &bufferingSource) noexcept {
  bufferingSource.setNextReadPosition(concurrentSeekDestination);
  bufferingSource.setBeforeCursorAdvanceTestHook(nullptr);
}

void testConcurrentSeekWinsOverInFlightBlockAdvance() {
  RampSource source(100000);
  juce::TimeSliceThread inactiveReadThread("cursor race test thread");
  StemBufferingSource bufferingSource(&source, inactiveReadThread, false,
                                      32768, 2, false);
  bufferingSource.prepareToPlay(128, 48000.0);
  bufferingSource.setBeforeCursorAdvanceTestHook(
      seekImmediatelyBeforeCursorAdvance);

  juce::AudioBuffer<float> output(2, 128);
  const juce::AudioSourceChannelInfo info(&output, 0, 128);
  bufferingSource.getNextAudioBlock(info);

  expect(bufferingSource.getNextReadPosition() == concurrentSeekDestination,
         "in-flight callback cannot add its block to a concurrent seek");

  bufferingSource.getNextAudioBlock(info);
  expect(bufferingSource.getNextReadPosition() ==
             concurrentSeekDestination + 128,
         "the callback after the seek advances the new timeline normally");
}
#endif

} // namespace

int main() {
  testEmptyReadAheadAdvancesAndCountsMissingFrames();
  testPartialReadAheadPreservesValidAudioAndAdvances();
  testEmptyLoopingRangeAdvancesModuloSourceLength();
  testBufferedLoopCrossingPreservesAudio();
  testPublishedEofSilenceIsNotAnUnderrun();
  testPrefetchedSeekKeepsCurrentWindowAndSuppliesTargetImmediately();
  testSeekPrefetchBankCannotBeOverwrittenBeforeMainHandoff();
  testMainRingTakesPrecedenceOverOverlappingPrefetch();
  testPrefetchedTargetCanSplitOneCallbackAtExactBoundary();
  testFarCursorCannotWrapRangeOffsets();
#if defined(MTCONTROLS_ENABLE_TEST_HOOKS) && MTCONTROLS_ENABLE_TEST_HOOKS
  testConcurrentSeekWinsOverInFlightBlockAdvance();
#endif
  std::cout << "stem buffering source tests passed\n";
  return 0;
}
