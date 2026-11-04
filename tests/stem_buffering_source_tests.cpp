#include "../src/StemBufferingSource.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

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
    // An absolute frame stamp catches repeated blocks as well as silence. A
    // power-of-two divisor keeps the test's relevant positions exactly
    // representable, without the old ramp repeating every 1000 samples.
    return static_cast<float>(position + 1) / 1048576.0f;
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

// prepareToPlay must return even when the read-ahead service is unavailable.
// The fallback starts the worker after two seconds so the old unbounded code
// fails its elapsed assertion instead of hanging the entire test process.
void testPrefillReturnsWithoutAnActiveWorkerAndRecovers() {
  RampSource source(100000);
  juce::TimeSliceThread worker("initially inactive prefill worker");
  StemBufferingSource bufferingSource(&source, worker, false, 32768, 2, true);
  juce::WaitableEvent preparationFinished;
  std::atomic<bool> fallbackStartedWorker{false};
  std::thread fallback([&] {
    if (!preparationFinished.wait(2000)) {
      fallbackStartedWorker.store(true);
      if (!worker.startThread())
        std::abort();
    }
  });

  const auto startedAt = juce::Time::getMillisecondCounterHiRes();
  bufferingSource.prepareToPlay(128, 48000.0);
  const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAt;
  preparationFinished.signal();
  fallback.join();

  juce::AudioBuffer<float> output(2, 128);
  const juce::AudioSourceChannelInfo info(&output, 0, 128);
  const bool readyWithoutWorker =
      bufferingSource.waitForNextAudioBlockReady(info, 10);
  if (!worker.isThreadRunning())
    expect(worker.startThread(), "previously inactive read worker starts");
  const bool readyAfterRecovery =
      bufferingSource.waitForNextAudioBlockReady(info, 2000);
  if (readyAfterRecovery)
    bufferingSource.getNextAudioBlock(info);
  bufferingSource.releaseResources();
  expect(worker.stopThread(2000), "recovered prefill worker stops cleanly");

  expect(elapsedMs < 1000.0 && !fallbackStartedWorker.load(),
         "prefill returns promptly when its read worker is not running");
  expect(!readyWithoutWorker,
         "preparation without a worker cannot claim its first block is ready");
  expect(readyAfterRecovery,
         "readiness recovers when the read worker becomes available");
  for (int frame = 0; frame < 128; ++frame)
    expectNear(output.getSample(0, frame), RampSource::sampleAt(frame),
               "recovered read-ahead delivers exact initial audio frames");
}

class DelayedTimeSliceClient final : public juce::TimeSliceClient {
public:
  int useTimeSlice() override {
    entered.signal();
    // Finite fail-safe: the original prepare loop can complete eventually,
    // allowing its elapsed assertion to fail instead of leaving a stuck test.
    release.wait(3000);
    return -1;
  }

  juce::WaitableEvent entered;
  juce::WaitableEvent release;
};

void testPrefillBudgetReturnsWhileRunningWorkerIsDelayed() {
  RampSource source(100000);
  juce::TimeSliceThread worker("busy prefill worker");
  DelayedTimeSliceClient delayedClient;
  worker.addTimeSliceClient(&delayedClient);
  expect(worker.startThread(), "delayed read worker starts");
  expect(delayedClient.entered.wait(2000),
         "worker enters the controlled delay before preparation");
  StemBufferingSource bufferingSource(&source, worker, false, 32768, 2, true);

  const auto startedAt = juce::Time::getMillisecondCounterHiRes();
  bufferingSource.prepareToPlay(128, 48000.0);
  const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAt;
  juce::AudioBuffer<float> output(2, 128);
  const juce::AudioSourceChannelInfo info(&output, 0, 128);
  const bool readyWhileDelayed =
      bufferingSource.waitForNextAudioBlockReady(info, 25);
  delayedClient.release.signal();
  const bool readyAfterRecovery =
      bufferingSource.waitForNextAudioBlockReady(info, 2000);
  if (readyAfterRecovery)
    bufferingSource.getNextAudioBlock(info);
  bufferingSource.releaseResources();
  worker.removeTimeSliceClient(&delayedClient);
  expect(worker.stopThread(2000), "delayed prefill worker stops cleanly");

  expect(elapsedMs < 1000.0,
         "prefill budget expires while a running worker is delayed");
  expect(!readyWhileDelayed,
         "expired prefill budget does not bypass the first-block readiness gate");
  expect(readyAfterRecovery,
         "read-ahead keeps filling after the prefill budget expires");
  for (int frame = 0; frame < 128; ++frame)
    expectNear(output.getSample(0, frame), RampSource::sampleAt(frame),
               "delayed worker recovery preserves exact initial audio frames");
}

void testNormalPrefillPublishesReadyAudio() {
  RampSource source(100000);
  juce::TimeSliceThread worker("normal prefill worker");
  expect(worker.startThread(), "normal prefill worker starts");
  StemBufferingSource bufferingSource(&source, worker, false, 32768, 2, true);
  bufferingSource.prepareToPlay(512, 48000.0);
  juce::AudioBuffer<float> output(2, 512);
  const juce::AudioSourceChannelInfo info(&output, 0, 512);
  const bool ready = bufferingSource.waitForNextAudioBlockReady(info, 2000);
  if (ready)
    bufferingSource.getNextAudioBlock(info);
  const auto misses = bufferingSource.getRealtimeMissCount();
  bufferingSource.releaseResources();
  expect(worker.stopThread(2000), "normal prefill worker stops cleanly");

  expect(ready, "normal worker publishes its first block before playback");
  expect(misses == 0, "normal prefilling starts playback without an underrun");
  for (int frame = 0; frame < 512; ++frame)
    expectNear(output.getSample(0, frame), RampSource::sampleAt(frame),
               "normal prefill preserves exact initial audio frames");
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
class CallbackLockHolder final {
public:
  explicit CallbackLockHolder(StemBufferingSource &source)
      : holder([this, &source] {
          source.enterCallbackLockForTest();
          entered.signal();
          release.wait(-1);
          source.exitCallbackLockForTest();
        }) {
    expect(entered.wait(2000), "another thread acquires the callback lock");
  }

  ~CallbackLockHolder() {
    release.signal();
    holder.join();
  }

private:
  juce::WaitableEvent entered;
  juce::WaitableEvent release;
  std::thread holder;
};

void fillOutput(juce::AudioBuffer<float> &output, const float value) {
  for (int channel = 0; channel < output.getNumChannels(); ++channel)
    for (int sample = 0; sample < output.getNumSamples(); ++sample)
      output.setSample(channel, sample, value);
}

void expectStampedOutput(const juce::AudioBuffer<float> &output,
                         const int startSample, const int frames,
                         const juce::int64 firstPosition,
                         const char *message,
                         const juce::int64 sourceLength =
                             std::numeric_limits<juce::int64>::max(),
                         const int availableFrames =
                             std::numeric_limits<int>::max(),
                         const float bias = 0.0f) {
  for (int channel = 0; channel < output.getNumChannels(); ++channel) {
    for (int sample = 0; sample < frames; ++sample) {
      const auto position = firstPosition + sample;
      const auto expected = channel < 2 && sample < availableFrames &&
                                    position < sourceLength
                                ? RampSource::sampleAt(position) + bias
                                : 0.0f;
      // The path under test only copies floats; exact equality also detects a
      // one-frame duplication that a general audio epsilon could hide.
      expect(output.getSample(channel, startSample + sample) == expected,
             message);
    }
  }
}

void readyAndStopWorker(StemBufferingSource &bufferingSource,
                        juce::TimeSliceThread &readThread,
                        const int frames = 32764) {
  juce::AudioBuffer<float> readiness(2, frames);
  const juce::AudioSourceChannelInfo readinessInfo(&readiness, 0, frames);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "deterministic test publishes its full main ring");
  expect(readThread.stopThread(2000),
         "deterministic test stops the worker before contention");
}

void readStampedBlock(StemBufferingSource &bufferingSource, const int frames,
                      const char *message) {
  const auto start = bufferingSource.getNextReadPosition();
  juce::AudioBuffer<float> output(2, frames);
  const juce::AudioSourceChannelInfo info(&output, 0, frames);
  bufferingSource.getNextAudioBlock(info);
  expectStampedOutput(output, 0, frames, start, message);
  expect(bufferingSource.getNextReadPosition() == start + frames,
         "stamped callback advances exactly its own frame count");
}

void testPrivateCacheServesExactFutureFramesDuringRealLockContention() {
  constexpr int warmFrames = 256;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache contention thread");
  expect(readThread.startThread(), "private cache read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, warmFrames,
                   "first callback warms a frame-stamped private cache");

  const auto capacity = bufferingSource.getRealtimeCacheCapacityFramesForTest();
  expect(capacity > warmFrames + 960,
         "private cache can cover several variable callback sizes");
  const auto locksBefore = bufferingSource.getRealtimeLockMissCount();
  CallbackLockHolder holder(bufferingSource);
  int remainingFrames = capacity;
  for (const auto frames : {64, 256, 127, 513}) {
    const auto start = bufferingSource.getNextReadPosition();
    constexpr int outputOffset = 7;
    juce::AudioBuffer<float> output(3, frames + outputOffset + 11);
    fillOutput(output, -2.0f);
    const juce::AudioSourceChannelInfo info(&output, outputOffset, frames);
    bufferingSource.getNextAudioBlock(info);
    expectStampedOutput(output, outputOffset, frames, start,
                         "cache serves new future frames during actual lock contention");
    for (int channel = 0; channel < output.getNumChannels(); ++channel) {
      for (int sample = 0; sample < outputOffset; ++sample)
        expectNear(output.getSample(channel, sample), -2.0f,
                   "cache preserves output before the active region");
      for (int sample = outputOffset + frames;
           sample < output.getNumSamples(); ++sample)
        expectNear(output.getSample(channel, sample), -2.0f,
                   "cache preserves output after the active region");
    }
    expect(bufferingSource.getNextReadPosition() == start + frames,
           "variable callbacks advance without repeating any prior block");
    remainingFrames -= frames;
  }
  readStampedBlock(bufferingSource, remainingFrames,
                   "last cached frames survive the occupied worker lock");
  expect(bufferingSource.getNextReadPosition() == warmFrames + capacity,
         "contended callbacks consume each cached frame exactly once");
  expect(bufferingSource.getRealtimeLockMissCount() > locksBefore,
         "cache coverage does not hide the actual failed lock attempts");
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "brief actual lock contention no longer substitutes silence");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 0,
         "cached contention adds no missing-frame count");
}

void testPrivateCacheSurvivesAnInProgressRangePublication() {
  constexpr int warmFrames = 128;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache publication thread");
  expect(readThread.startThread(), "publication test read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, warmFrames,
                   "publication test warms the private cache");

  // The worker is stopped, so this deterministically models its sole-writer
  // odd sequence without racing with another test publisher.
  bufferingSource.setMainPublicationBusyForTest(true);
  for (const auto frames : {64, 129, 256, 513})
    readStampedBlock(bufferingSource, frames,
                     "an odd ring publication cannot discard cached future audio");
  bufferingSource.setMainPublicationBusyForTest(false);
  readStampedBlock(bufferingSource, 128,
                   "normal publication resumes without a timeline discontinuity");
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "an in-progress snapshot publication adds no cached underrun");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 0,
         "an in-progress publication substitutes no cached frames");
}

void testPrivateCacheRefillsBeforeItsOldWindowRunsOut() {
  constexpr int warmFrames = 128;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache refill thread");
  expect(readThread.startThread(), "refill test read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, warmFrames,
                   "refill test warms its initial private cache");
  const auto capacity = bufferingSource.getRealtimeCacheCapacityFramesForTest();
  readStampedBlock(bufferingSource, capacity - warmFrames - 64,
                   "an available ring refreshes the nearly exhausted cache");

  CallbackLockHolder holder(bufferingSource);
  readStampedBlock(bufferingSource, 256,
                   "refreshed cache crosses the original window while the lock is held");
  readStampedBlock(bufferingSource, 127,
                   "refreshed cache continues with exact subsequent frames");
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "proactive refill prevents an underrun at the old cache boundary");
}

void testExhaustedPrivateCacheCountsOnlyTheUnavailableSuffix() {
  constexpr int warmFrames = 128;
  constexpr int uncachedFrames = 137;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache exhaustion thread");
  expect(readThread.startThread(), "exhaustion test read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, warmFrames,
                   "exhaustion test warms the private cache");
  const auto capacity = bufferingSource.getRealtimeCacheCapacityFramesForTest();
  const auto cachedFrames = capacity;
  const auto requestedFrames = cachedFrames + uncachedFrames;

  CallbackLockHolder holder(bufferingSource);
  juce::AudioBuffer<float> output(2, requestedFrames);
  fillOutput(output, -1.0f);
  const juce::AudioSourceChannelInfo info(&output, 0, requestedFrames);
  bufferingSource.getNextAudioBlock(info);
  expectStampedOutput(output, 0, requestedFrames, warmFrames,
                       "an exhausted cache keeps its valid prefix and silences only the suffix",
                       source.getTotalLength(), cachedFrames);
  expect(bufferingSource.getNextReadPosition() ==
             warmFrames + capacity + uncachedFrames,
         "cache exhaustion still advances the complete callback");
  expect(bufferingSource.getRealtimeMissCount() == 1,
         "cache exhaustion counts exactly one affected callback");
  expect(bufferingSource.getRealtimeMissingFrameCount() == uncachedFrames,
         "cache exhaustion counts precisely the uncached suffix");

  juce::AudioBuffer<float> next(2, 64);
  const juce::AudioSourceChannelInfo nextInfo(&next, 0, 64);
  bufferingSource.getNextAudioBlock(nextInfo);
  expectStampedOutput(next, 0, 64, warmFrames + capacity + uncachedFrames,
                       "an exhausted cache never replays its old contents",
                       source.getTotalLength(), 0);
  expect(bufferingSource.getRealtimeMissCount() == 2,
         "continued contention after exhaustion counts the next callback");
  expect(bufferingSource.getRealtimeMissingFrameCount() == uncachedFrames + 64,
         "continued exhaustion accumulates only genuinely unavailable frames");
}

void testSeekInsidePrivateCacheInvalidatesThePreviousEpoch() {
  constexpr int warmFrames = 128;
  constexpr juce::int64 target = 512;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache seek thread");
  expect(readThread.startThread(), "cache seek test read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, warmFrames,
                   "seek test warms a cache containing its future target");
  expect(target + warmFrames <
             bufferingSource.getRealtimeCacheCapacityFramesForTest(),
         "seek destination lies physically inside the old cached window");

  CallbackLockHolder holder(bufferingSource);
  bufferingSource.setNextReadPosition(target);
  juce::AudioBuffer<float> output(2, warmFrames);
  const juce::AudioSourceChannelInfo info(&output, 0, warmFrames);
  bufferingSource.getNextAudioBlock(info);
  expectStampedOutput(output, 0, warmFrames, target,
                       "a seek invalidates old cache even when its samples contain the destination",
                       source.getTotalLength(), 0);
  expect(bufferingSource.getNextReadPosition() == target + warmFrames,
         "post-seek callback advances from the newly requested destination");
  expect(bufferingSource.getRealtimeMissingFrameCount() == warmFrames,
         "an unprepared new epoch records missing frames instead of stale cache audio");
}

void testPrefetchedSeekInsideOldCacheUsesTheTargetBank() {
  constexpr int blockFrames = 128;
  constexpr juce::int64 target = 512;
  constexpr float targetBias = 2.0f;
  RampSource source(100000);
  RampSource prefetchSource(100000, targetBias);
  juce::TimeSliceThread readThread("private cache prefetched seek thread");
  expect(readThread.startThread(), "cached prefetch read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false, &prefetchSource);
  bufferingSource.prepareToPlay(blockFrames, 48000.0);
  juce::AudioBuffer<float> readiness(2, 32764);
  const juce::AudioSourceChannelInfo readinessInfo(&readiness, 0, 32764);
  expect(bufferingSource.waitForNextAudioBlockReady(readinessInfo, 2000),
         "cached prefetch main ring becomes ready");
  bufferingSource.requestPrefetchPosition(target, blockFrames);
  const auto deadline = juce::Time::getMillisecondCounter() + 2000U;
  while (!bufferingSource.isPrefetchReady(target, blockFrames) &&
         juce::Time::getMillisecondCounter() < deadline)
    juce::Thread::sleep(1);
  expect(bufferingSource.isPrefetchReady(target, blockFrames),
         "cached prefetch target bank becomes ready");
  expect(readThread.stopThread(2000),
         "cached prefetch worker stops before deterministic handoff");
  readStampedBlock(bufferingSource, blockFrames,
                   "continuous main audio wins while the target overlaps its cache");

  CallbackLockHolder holder(bufferingSource);
  bufferingSource.setNextReadPosition(target);
  juce::AudioBuffer<float> output(2, blockFrames);
  const juce::AudioSourceChannelInfo info(&output, 0, blockFrames);
  bufferingSource.getNextAudioBlock(info);
  expectStampedOutput(output, 0, blockFrames, target,
                       "after seek the independently stamped target bank wins over stale private cache",
                       source.getTotalLength(), blockFrames, targetBias);
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "a ready target bank survives main cache invalidation without silence");
  expect(bufferingSource.getNextReadPosition() == target + blockFrames,
         "prefetched cache handoff advances from the exact seek position");
}

void testPrivateCachePreservesPublishedEofSilenceDuringContention() {
  constexpr int warmFrames = 128;
  constexpr int sourceLength = 192;
  constexpr int crossingFrames = 256;
  RampSource source(sourceLength);
  juce::TimeSliceThread readThread("private cache EOF thread");
  expect(readThread.startThread(), "cache EOF read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, warmFrames,
                   "EOF test warms audio plus worker-published EOF silence");

  CallbackLockHolder holder(bufferingSource);
  juce::AudioBuffer<float> output(2, crossingFrames);
  fillOutput(output, -1.0f);
  const juce::AudioSourceChannelInfo info(&output, 0, crossingFrames);
  bufferingSource.getNextAudioBlock(info);
  expectStampedOutput(output, 0, crossingFrames, warmFrames,
                       "cached EOF crossing preserves its final audio then published silence",
                       sourceLength);
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "cached published EOF silence is available data during contention");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 0,
         "cached published EOF silence adds no missing frames");
}

void testLargeCallbacksPreserveDirectRingAudioBeyondCacheCapacity() {
  constexpr int warmFrames = 128;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache large callback thread");
  expect(readThread.startThread(), "large callback read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, warmFrames,
                   "large callback test warms the private cache");
  const auto capacity = bufferingSource.getRealtimeCacheCapacityFramesForTest();
  readStampedBlock(bufferingSource, capacity + 173,
                   "a callback larger than cache combines cache with the original main ring");
  readStampedBlock(bufferingSource, 257,
                   "the callback following a large direct read has no duplicated frames");
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "large callbacks remain fully covered by a ready ring");
}

void testPrivateCacheCopiesAcrossThePhysicalRingWrap() {
  constexpr juce::int64 startPosition = 32640;
  constexpr int crossingFrames = 256;
  constexpr int contendedFrames = 512;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache physical wrap thread");
  expect(readThread.startThread(), "physical wrap read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(crossingFrames, 48000.0);
  bufferingSource.setNextReadPosition(startPosition);
  readyAndStopWorker(bufferingSource, readThread);
  readStampedBlock(bufferingSource, crossingFrames,
                   "first callback reads exact frames across the physical ring boundary");
  expect(bufferingSource.getNextReadPosition() ==
             startPosition + crossingFrames,
         "physical wrap never wraps the absolute audio cursor");

  CallbackLockHolder holder(bufferingSource);
  readStampedBlock(bufferingSource, contendedFrames,
                   "cache after physical ring wrap survives actual lock contention");
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "physical ring wrapping and contended cached reads add no underrun");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 0,
         "physical ring wrapping substitutes no available frames");
}

void testPrivateCacheRefillItselfWrapsAcrossBothRingSegments() {
  constexpr juce::int64 warmPosition = 32384;
  constexpr int warmFrames = 256;
  constexpr int crossingFrames = 512;
  RampSource source(100000);
  juce::TimeSliceThread readThread("private cache wrapped refill thread");
  expect(readThread.startThread(), "wrapped refill read thread starts");
  StemBufferingSource bufferingSource(&source, readThread, false, 32768, 2,
                                      false);
  bufferingSource.prepareToPlay(warmFrames, 48000.0);
  bufferingSource.setNextReadPosition(warmPosition);
  readyAndStopWorker(bufferingSource, readThread);
  // This callback ends at 32640. Its private refill therefore must copy 128
  // frames from the ring tail, followed by its remaining head segment.
  readStampedBlock(bufferingSource, warmFrames,
                   "warming near the ring end fills a private cache over both ring segments");

  CallbackLockHolder holder(bufferingSource);
  readStampedBlock(bufferingSource, crossingFrames,
                   "both segments of a physically wrapped private refill remain frame exact");
  expect(bufferingSource.getRealtimeMissCount() == 0,
         "physically wrapped private refill covers its contended callback");
  expect(bufferingSource.getRealtimeMissingFrameCount() == 0,
         "physically wrapped private refill introduces no silence");
}

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

void testSectionEntriesPreparePinAndReleaseWithoutAudioIO() {
  RampSource main(1000000), independent(1000000);
  juce::TimeSliceThread worker("section entry cache worker");
  expect(worker.startThread(), "entry worker starts");
  StemBufferingSource source(&main, worker, false, 32768, 2, false, &independent);
  source.prepareToPlay(256, 48000.0);
  source.configureSectionEntries({60000, 20000}, 2);
  const auto wait = [](auto predicate) {
    for (int i = 0; i < 1000; ++i) { if (predicate()) return true; juce::Thread::sleep(2); }
    return false;
  };
  expect(wait([&] { return source.isPrefetchReady(60000, 256) && source.isPrefetchReady(20000, 256); }),
         "loading prepares section entries in background without a target request");
  expect(source.getSectionCacheBytes() == 2 * 32768 * 2 * static_cast<int>(sizeof(float)), "cache accounts for exactly two float entry buffers");
  expect(worker.stopThread(1000), "worker stops for deterministic cache handoff");
  const int readsBefore = main.getSetPositionCallCount() + independent.getSetPositionCallCount();
  source.protectSectionTargets(60000, -1); source.setNextReadPosition(60000);
  juce::AudioBuffer<float> output(2, 256); juce::AudioSourceChannelInfo info(&output, 0, 256);
  source.getNextAudioBlock(info);
  for (int frame = 0; frame < 256; ++frame)
    expectNear(output.getSample(0, frame), RampSource::sampleAt(60000 + frame), "cold primary reads exact cached source frames without silence");
  expect(main.getSetPositionCallCount() + independent.getSetPositionCallCount() == readsBefore,
         "audio cache handoff never seeks or reads either decoder");
  source.requestPrefetchPosition(80000, 32768);
  expect(source.isPrefetchReady(60000, 256), "another preparation cannot replace resident selected entry");
  source.configureSectionEntries({60000, 20000}, 0);
  expect(worker.startThread(), "worker restarts for memory pressure");
  expect(wait([&] { source.requestPrefetchPosition(80000, 32768); return source.isPrefetchReady(80000, 256); }),
         "different foreground target actually finishes while the selected cache lease remains protected");
  expect(source.isPrefetchReady(60000, 256), "replacement bank cannot evict the protected selected prefix");
  expect(wait([&] { return source.getSectionCacheBytes() == 32768 * 2 * static_cast<int>(sizeof(float)); }),
         "pressure evicts unleased entries and preserves the selected destination");
  for (int block = 0; block < 140; ++block) {
    source.getNextAudioBlock(info); juce::Thread::sleep(6);
    for (int frame = 0; frame < 256; ++frame)
      expectNear(output.getSample(0, frame), RampSource::sampleAt(60256 + block * 256 + frame),
                 "cached entry hands off beyond its end to exact streaming frames");
  }
  source.protectSectionTargets(-1, -1);
  expect(wait([&] { return source.getSectionCacheBytes() == 0; }), "released lease returns cache memory under the reduced budget");
  source.releaseResources(); worker.stopThread(1000);
}

} // namespace

int main() {
  testPrefillReturnsWithoutAnActiveWorkerAndRecovers();
  testPrefillBudgetReturnsWhileRunningWorkerIsDelayed();
  testNormalPrefillPublishesReadyAudio();
  testSectionEntriesPreparePinAndReleaseWithoutAudioIO();
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
  testPrivateCacheServesExactFutureFramesDuringRealLockContention();
  testPrivateCacheSurvivesAnInProgressRangePublication();
  testPrivateCacheRefillsBeforeItsOldWindowRunsOut();
  testExhaustedPrivateCacheCountsOnlyTheUnavailableSuffix();
  testSeekInsidePrivateCacheInvalidatesThePreviousEpoch();
  testPrefetchedSeekInsideOldCacheUsesTheTargetBank();
  testPrivateCachePreservesPublishedEofSilenceDuringContention();
  testLargeCallbacksPreserveDirectRingAudioBeyondCacheCapacity();
  testPrivateCacheCopiesAcrossThePhysicalRingWrap();
  testPrivateCacheRefillItselfWrapsAcrossBothRingSegments();
  testConcurrentSeekWinsOverInFlightBlockAdvance();
#endif
  std::cout << "stem buffering source tests passed\n";
  return 0;
}
