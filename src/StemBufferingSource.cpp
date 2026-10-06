#include "StemBufferingSource.h"

#include <algorithm>
#include <array>

StemBufferingSource::StemBufferingSource(juce::PositionableAudioSource *s,
                                         juce::TimeSliceThread &thread,
                                         bool deleteSourceWhenDeleted,
                                         int bufferSizeSamples, int numChannels,
                                         bool prefillBufferOnPrepareToPlay,
                                         juce::PositionableAudioSource
                                             *prefetchSourceToUse)
    : source(s, deleteSourceWhenDeleted),
      prefetchSource(prefetchSourceToUse), backgroundThread(thread),
      numberOfSamplesToBuffer(juce::jmax(32768, bufferSizeSamples)),
      numberOfChannels(numChannels),
      prefillBuffer(prefillBufferOnPrepareToPlay) {
  jassert(source != nullptr);
  jassert(prefetchSource == nullptr || prefetchSource != source.get());
}

StemBufferingSource::~StemBufferingSource() { releaseResources(); }

void StemBufferingSource::prepareToPlay(int samplesPerBlockExpected,
                                        double newSampleRate) {
  auto bufferSizeNeeded =
      juce::jmax(samplesPerBlockExpected * 2, numberOfSamplesToBuffer);

  if (!juce::approximatelyEqual(newSampleRate, sampleRate) ||
      bufferSizeNeeded != buffer.getNumSamples() ||
      !isPrepared.load(std::memory_order_acquire)) {
    backgroundThread.removeTimeSliceClient(this);

    isPrepared.store(true, std::memory_order_release);
    sampleRate = newSampleRate;

    source->prepareToPlay(samplesPerBlockExpected, newSampleRate);
    if (prefetchSource != nullptr)
      prefetchSource->prepareToPlay(samplesPerBlockExpected, newSampleRate);

    buffer.setSize(numberOfChannels, bufferSizeNeeded);
    buffer.clear();
    const int cacheSize = juce::jmax(
        samplesPerBlockExpected * 2,
        juce::jlimit(2048, 8192, samplesPerBlockExpected * 8));
    realtimeReadCache.setSize(numberOfChannels, cacheSize);
    realtimeCacheFrames = 0;
    const int prefetchBufferSize =
        prefetchSource != nullptr
            ? juce::jmin(bufferSizeNeeded,
                         juce::jmax(kMinPrefetchBufferSamples,
                                    samplesPerBlockExpected * 4))
            : 0;
    prefetchBuffer.setSize(numberOfChannels, prefetchBufferSize);
    prefetchBuffer.clear();
    prefetchCapacityFrames.store(prefetchBufferSize,
                                 std::memory_order_release);

    const auto epoch =
        requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    workerBufferEpoch = epoch;
    publishValidBufferRange(0, 0, epoch);
    const auto prefetchGeneration =
        requestedPrefetchGeneration.fetch_add(1, std::memory_order_acq_rel) +
        1;
    workerPrefetchGeneration = prefetchGeneration;
    requestedPrefetchFrames.store(0, std::memory_order_relaxed);
    pinnedPrefetchGeneration.store(0, std::memory_order_release);
    publishPrefetchRange(0, 0, prefetchGeneration);

    backgroundThread.addTimeSliceClient(this);

    // P0.3 (perf): target de prefill mucho más chico. El original pedía
    // `min(sampleRate/4, buffer/2)` = ~250ms @ 44.1kHz. En Windows, con
    // antivirus activo, el primer read del archivo puede tardar 200-500ms,
    // y este loop se ejecuta en el UI thread del loadSongStems, donde
    // 250ms × N stems se acumula y se ve como "tarda mucho en cambiar de
    // canción". 4 blocks de headroom alcanzan para que el primer callback
    // no haga underrun, y el waitForNextAudioBlockReady posterior en
    // MTControlsAudioProcessor::loadSongStems sigue siendo el verdadero
    // gate antes del swap. Si la prefill corta antes, el worker sigue
    // llenando mientras la UI vuelve al message loop.
    const int prefillTarget = juce::jmax(
        samplesPerBlockExpected * 4,
        juce::jmin(samplesPerBlockExpected * 8, buffer.getNumSamples() / 4));
    do {
      backgroundThread.moveToFrontOfQueue(this);
      juce::Thread::sleep(5);
    } while (prefillBuffer &&
             (bufferValidEnd.load(std::memory_order_acquire) -
                  bufferValidStart.load(std::memory_order_acquire) <
              prefillTarget));
  }
}

void StemBufferingSource::releaseResources() {
  isPrepared.store(false, std::memory_order_release);
  prefetchCapacityFrames.store(0, std::memory_order_release);
  backgroundThread.removeTimeSliceClient(this);

  const auto epoch =
      requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
  workerBufferEpoch = epoch;
  publishValidBufferRange(0, 0, epoch);
  buffer.setSize(numberOfChannels, 0);
  realtimeCacheFrames = 0;
  realtimeReadCache.setSize(numberOfChannels, 0);
  const auto prefetchGeneration =
      requestedPrefetchGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
  workerPrefetchGeneration = prefetchGeneration;
  requestedPrefetchFrames.store(0, std::memory_order_relaxed);
  pinnedPrefetchGeneration.store(0, std::memory_order_release);
  publishPrefetchRange(0, 0, prefetchGeneration);
  prefetchBuffer.setSize(numberOfChannels, 0);

  if (source != this)
    source->releaseResources();
  if (prefetchSource != nullptr)
    prefetchSource->releaseResources();
}

void StemBufferingSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &info) {
  if (info.numSamples <= 0)
    return;

  const auto blockStart = nextPlayPos.load(std::memory_order_acquire);
  lastAudioBlockFrames.store(info.numSamples, std::memory_order_relaxed);
  const auto blockCursorEpoch = cursorEpoch.load(std::memory_order_acquire);
  const auto blockBufferEpoch =
      requestedBufferEpoch.load(std::memory_order_acquire);
  const auto blockPrefetchGeneration =
      requestedPrefetchGeneration.load(std::memory_order_acquire);
  const auto maximumPosition = std::numeric_limits<juce::int64>::max();
  const auto blockEnd = blockStart > maximumPosition - info.numSamples
      ? maximumPosition : blockStart + static_cast<juce::int64>(info.numSamples);

  info.clearActiveBufferRegion();
  auto cachedRange = getValidRealtimeCacheRange(
      info.numSamples, blockStart, blockBufferEpoch, blockCursorEpoch);
  if (!cachedRange.isEmpty()) {
    const auto offset = static_cast<int>(
        blockStart + cachedRange.getStart() - realtimeCacheStart);
    for (int channel = std::min(numberOfChannels, info.buffer->getNumChannels());
         --channel >= 0;)
      info.buffer->copyFrom(channel, info.startSample + cachedRange.getStart(),
                            realtimeReadCache, channel, offset,
                            cachedRange.getLength());
  }

  // Consume prepared frames before attempting any shared-ring access. Refill
  // halfway through the private window, leaving several callbacks to retry a
  // brief worker copy/publication without replacing valid audio with silence.
  const auto cachedFramesRemaining = cachedRange.isEmpty()
      ? juce::int64{0}
      : static_cast<juce::int64>(realtimeCacheFrames) -
            (blockStart - realtimeCacheStart);
  const bool cacheCoversBlock = cachedRange.getStart() == 0 &&
                               cachedRange.getEnd() == info.numSamples;
  const auto remainingAfterBlock = juce::jmax(
      juce::int64{0}, cachedFramesRemaining - info.numSamples);
  juce::Range<int> mainRange;
  if (!cacheCoversBlock || remainingAfterBlock <=
                              realtimeReadCache.getNumSamples() / 2) {
    const auto optimisticRange = getValidBufferRange(info.numSamples, blockStart);
    if (!optimisticRange.isEmpty()) {
      if (!callbackLock.tryEnter()) {
        realtimeLockMissCount.fetch_add(1, std::memory_order_relaxed);
      } else {
        mainRange = getValidBufferRange(info.numSamples, blockStart);
        if (!mainRange.isEmpty()) {
          const auto ringSize = buffer.getNumSamples();
          const auto ringOffset = static_cast<int>(
              (blockStart + mainRange.getStart()) % ringSize);
          const auto firstFrames = juce::jmin(mainRange.getLength(),
                                              ringSize - ringOffset);
          for (int channel = std::min(numberOfChannels, info.buffer->getNumChannels());
               --channel >= 0;) {
            info.buffer->copyFrom(channel, info.startSample + mainRange.getStart(),
                                  buffer, channel, ringOffset, firstFrames);
            if (firstFrames < mainRange.getLength())
              info.buffer->copyFrom(channel,
                  info.startSample + mainRange.getStart() + firstFrames,
                  buffer, channel, 0, mainRange.getLength() - firstFrames);
          }
        }

        const auto aheadRange = getValidBufferRange(
            realtimeReadCache.getNumSamples(), blockEnd);
        // A partial suffix must not erase a cached prefix. Only replace the
        // private window with a contiguous, longer window at this position.
        if (aheadRange.getStart() == 0 &&
            aheadRange.getLength() > remainingAfterBlock) {
          const auto ringSize = buffer.getNumSamples();
          const auto ringOffset = static_cast<int>(blockEnd % ringSize);
          const auto firstFrames = juce::jmin(aheadRange.getLength(),
                                              ringSize - ringOffset);
          for (int channel = 0; channel < numberOfChannels; ++channel) {
            realtimeReadCache.copyFrom(channel, 0, buffer, channel,
                                        ringOffset, firstFrames);
            if (firstFrames < aheadRange.getLength())
              realtimeReadCache.copyFrom(channel, firstFrames, buffer, channel,
                                          0, aheadRange.getLength() - firstFrames);
          }
          realtimeCacheStart = blockEnd;
          realtimeCacheFrames = aheadRange.getLength();
          realtimeCacheBufferEpoch = blockBufferEpoch;
          realtimeCacheCursorEpoch = blockCursorEpoch;
        }
        callbackLock.exit();
      }
    }
  }

  if (requestedBufferEpoch.load(std::memory_order_acquire) != blockBufferEpoch ||
      cursorEpoch.load(std::memory_order_acquire) != blockCursorEpoch) {
    info.clearActiveBufferRegion();
    cachedRange = mainRange = {};
  }

  // The target bank fills only holes left by the main ring AND its private
  // read-ahead copy. Both represent the same timeline and take precedence over
  // a cold-seek target that happens to overlap them.
  auto prefetchRange = getValidPrefetchRange(info.numSamples, blockStart);
  if (!prefetchRange.isEmpty()) {
    if (!prefetchLock.tryEnter()) {
      prefetchRange = {};
    } else {
      const auto snapshot = readPrefetchRangeSnapshot();
      prefetchRange = getValidPrefetchRange(info.numSamples, blockStart);
      std::array<juce::Range<int>, 3> copiedRanges{};
      int copiedCount = 0;
      bool copySucceeded = snapshot.valid &&
          snapshot.generation == blockPrefetchGeneration;
      const auto copyRange = [&](int start, int end) noexcept {
        if (end <= start)
          return true;
        const auto offset = blockStart + start - snapshot.start;
        if (offset < 0 || offset + end - start > prefetchBuffer.getNumSamples())
          return false;
        for (int channel = std::min(numberOfChannels, info.buffer->getNumChannels());
             --channel >= 0;)
          info.buffer->copyFrom(channel, info.startSample + start,
                                prefetchBuffer, channel, static_cast<int>(offset),
                                end - start);
        copiedRanges[static_cast<size_t>(copiedCount++)] = {start, end};
        return true;
      };
      if (copySucceeded && !prefetchRange.isEmpty()) {
        std::array<juce::Range<int>, 2> primary{cachedRange, mainRange};
        if (primary[1].getStart() < primary[0].getStart())
          std::swap(primary[0], primary[1]);
        auto uncoveredStart = prefetchRange.getStart();
        for (const auto range : primary) {
          if (range.isEmpty() || range.getEnd() <= uncoveredStart)
            continue;
          copySucceeded = copySucceeded && copyRange(
              uncoveredStart, juce::jmin(prefetchRange.getEnd(), range.getStart()));
          uncoveredStart = juce::jmax(uncoveredStart, range.getEnd());
          if (uncoveredStart >= prefetchRange.getEnd())
            break;
        }
        copySucceeded = copySucceeded && copyRange(
            uncoveredStart, prefetchRange.getEnd());
      }
      if (!copySucceeded ||
          requestedPrefetchGeneration.load(std::memory_order_acquire) !=
              blockPrefetchGeneration) {
        for (int i = 0; i < copiedCount; ++i) {
          const auto range = copiedRanges[static_cast<size_t>(i)];
          info.buffer->clear(info.startSample + range.getStart(), range.getLength());
        }
        prefetchRange = {};
      }
      prefetchLock.exit();
    }
  }

  // A concurrent seek wins over every copy, including the private cache.
  if (cursorEpoch.load(std::memory_order_acquire) != blockCursorEpoch ||
      requestedBufferEpoch.load(std::memory_order_acquire) != blockBufferEpoch) {
    info.clearActiveBufferRegion();
    cachedRange = mainRange = prefetchRange = {};
  }

  std::array<juce::Range<int>, 3> coveredRanges{cachedRange, mainRange, prefetchRange};
  std::sort(coveredRanges.begin(), coveredRanges.end(),
            [](auto left, auto right) { return left.getStart() < right.getStart(); });
  int coveredFrames = 0;
  int coveredEnd = 0;
  for (const auto range : coveredRanges) {
    if (!range.isEmpty()) {
      coveredFrames += juce::jmax(0, range.getEnd() -
                                       juce::jmax(coveredEnd, range.getStart()));
      coveredEnd = juce::jmax(coveredEnd, range.getEnd());
    }
  }
  const auto missingFrames = info.numSamples - coveredFrames;
  if (missingFrames > 0) {
    realtimeMissCount.fetch_add(1, std::memory_order_relaxed);
    realtimeMissingFrameCount.fetch_add(missingFrames,
                                        std::memory_order_relaxed);
  }

#if defined(MTCONTROLS_ENABLE_TEST_HOOKS) && MTCONTROLS_ENABLE_TEST_HOOKS
  if (beforeCursorAdvanceTestHook != nullptr)
    beforeCursorAdvanceTestHook(*this);
#endif

  // Never advance a seek destination published during this callback.
  auto expectedPosition = blockStart;
  if (cursorEpoch.load(std::memory_order_acquire) == blockCursorEpoch) {
    nextPlayPos.compare_exchange_strong(expectedPosition, blockEnd,
        std::memory_order_release, std::memory_order_relaxed);
  }
}

juce::Range<int> StemBufferingSource::getValidRealtimeCacheRange(
    int numSamples, juce::int64 position, uint64_t bufferEpoch,
    uint64_t readCursorEpoch) const noexcept {
  if (numSamples <= 0 || realtimeCacheFrames <= 0 ||
      realtimeCacheBufferEpoch != bufferEpoch ||
      realtimeCacheCursorEpoch != readCursorEpoch)
    return {};
  const auto maximum = std::numeric_limits<juce::int64>::max();
  const auto requestedEnd = position > maximum - numSamples
      ? maximum : position + numSamples;
  const auto cacheEnd = realtimeCacheStart > maximum - realtimeCacheFrames
      ? maximum : realtimeCacheStart + realtimeCacheFrames;
  const auto start = juce::jmax(position, realtimeCacheStart);
  const auto end = juce::jmin(requestedEnd, cacheEnd);
  if (end <= start)
    return {};
  return {static_cast<int>(start - position), static_cast<int>(end - position)};
}

bool StemBufferingSource::waitForNextAudioBlockReady(
    const juce::AudioSourceChannelInfo &info, juce::uint32 timeout) {
  if (source == nullptr || source->getTotalLength() <= 0)
    return false;

  if ((nextPlayPos + info.numSamples < 0) ||
      (!isLooping() && nextPlayPos > getTotalLength()))
    return true;

  const auto startTime = juce::Time::getMillisecondCounter();
  auto now = startTime;

  auto elapsed =
      (now >= startTime
           ? now - startTime
           : (std::numeric_limits<juce::uint32>::max() - startTime) + now);

  while (elapsed <= timeout) {
    const auto readinessStart =
        nextPlayPos.load(std::memory_order_acquire);
    const auto bufferRange =
        getValidBufferRange(info.numSamples, readinessStart);

    const auto validStart = bufferRange.getStart();
    const auto validEnd = bufferRange.getEnd();

    if (validStart <= 0 && validStart < validEnd && validEnd >= info.numSamples) {
      return true;
    }

    if (elapsed < timeout &&
        !bufferReadyEvent.wait(static_cast<int>(timeout - elapsed))) {
      return false;
    }

    now = juce::Time::getMillisecondCounter();
    elapsed =
        (now >= startTime
             ? now - startTime
             : (std::numeric_limits<juce::uint32>::max() - startTime) + now);
  }

  return false;
}

juce::int64 StemBufferingSource::getNextReadPosition() const {
  const auto length = source->getTotalLength();
  const auto pos = nextPlayPos.load(std::memory_order_acquire);
  jassert(length > 0);
  return source->isLooping() && pos > 0 && length > 0 ? pos % length : pos;
}

void StemBufferingSource::requestPrefetchPosition(
    const juce::int64 position, const int minimumFrames) noexcept {
  const int capacity =
      prefetchCapacityFrames.load(std::memory_order_acquire);
  if (prefetchSource == nullptr ||
      !isPrepared.load(std::memory_order_acquire) || capacity <= 0 ||
      minimumFrames <= 0) {
    return;
  }

  const auto normalizedPosition = juce::jmax<juce::int64>(0, position);
  const int frames = juce::jlimit(1, capacity, minimumFrames);
  auto pinnedGeneration =
      pinnedPrefetchGeneration.load(std::memory_order_acquire);
  if (pinnedGeneration != 0) {
    const int handoffFrames = juce::jlimit(
        1, capacity,
        juce::jmax(1, lastAudioBlockFrames.load(std::memory_order_relaxed)));
    const auto currentPosition = nextPlayPos.load(std::memory_order_acquire);
    const auto mainRange = getValidBufferRange(handoffFrames, currentPosition);
    if (mainRange.getStart() > 0 || mainRange.getEnd() < handoffFrames)
      return;

    pinnedPrefetchGeneration.compare_exchange_strong(
        pinnedGeneration, static_cast<uint64_t>(0),
        std::memory_order_acq_rel, std::memory_order_relaxed);
  }

  if (isPrefetchReady(normalizedPosition, frames))
    return;

  if (requestedPrefetchPosition.load(std::memory_order_relaxed) ==
          normalizedPosition &&
      requestedPrefetchFrames.load(std::memory_order_relaxed) >= frames) {
    return;
  }

  // Single producer: the audio callback publishes fields before the release
  // generation. The TimeSlice worker consumes the generation with acquire.
  requestedPrefetchPosition.store(normalizedPosition,
                                  std::memory_order_relaxed);
  requestedPrefetchFrames.store(frames, std::memory_order_relaxed);
  requestedPrefetchGeneration.fetch_add(1, std::memory_order_release);
}

bool StemBufferingSource::isPrefetchReady(
    const juce::int64 position, const int minimumFrames) const noexcept {
  if (minimumFrames <= 0)
    return true;
  if (prefetchSource == nullptr)
    return false;
  const int capacity =
      prefetchCapacityFrames.load(std::memory_order_acquire);
  if (capacity <= 0)
    return false;
  const int frames = juce::jlimit(1, capacity, minimumFrames);
  const auto range =
      getValidPrefetchRange(frames, juce::jmax<juce::int64>(0, position));
  return range.getStart() == 0 && range.getEnd() >= frames;
}

void StemBufferingSource::setNextReadPosition(juce::int64 newPosition) {
  // Announce the seek before publishing its position. Combined with the
  // callback's captured epoch and position CAS, this ordering guarantees that
  // an in-flight block cannot advance the new destination.
  cursorEpoch.fetch_add(1, std::memory_order_acq_rel);
  nextPlayPos.store(newPosition, std::memory_order_release);

  const auto prefetchGeneration =
      requestedPrefetchGeneration.load(std::memory_order_acquire);
  if (prefetchGeneration != 0 && isPrefetchReady(newPosition, 1))
    pinnedPrefetchGeneration.store(prefetchGeneration,
                                   std::memory_order_release);
  else
    pinnedPrefetchGeneration.store(0, std::memory_order_release);

  // Advance the buffer epoch only after publishing the destination, so a
  // worker that observes the new epoch must also observe the new position.
  // Do not touch the worker-owned start/end pair here.
  requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel);
}

void StemBufferingSource::setLooping(const bool shouldLoop) {
  source->setLooping(shouldLoop);
  if (prefetchSource != nullptr)
    prefetchSource->setLooping(shouldLoop);
  requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel);
  requestedPrefetchFrames.store(0, std::memory_order_relaxed);
  pinnedPrefetchGeneration.store(0, std::memory_order_release);
  requestedPrefetchGeneration.fetch_add(1, std::memory_order_release);
}

void StemBufferingSource::publishValidBufferRange(
    const juce::int64 start, const juce::int64 end,
    const uint64_t epoch) noexcept {
  const auto sequence =
      bufferRangeSequence.fetch_add(1, std::memory_order_acq_rel);
  jassert((sequence & 1U) == 0U);
  juce::ignoreUnused(sequence);
  bufferValidStart.store(start, std::memory_order_relaxed);
  bufferValidEnd.store(end, std::memory_order_relaxed);
  publishedBufferEpoch.store(epoch, std::memory_order_relaxed);
  bufferRangeSequence.fetch_add(1, std::memory_order_release);
}

StemBufferingSource::PrefetchRangeSnapshot
StemBufferingSource::readPrefetchRangeSnapshot() const noexcept {
  PrefetchRangeSnapshot snapshot;
  const auto sequenceBefore =
      prefetchRangeSequence.load(std::memory_order_acquire);
  if ((sequenceBefore & 1U) != 0U)
    return snapshot;

  snapshot.start = prefetchedRangeStart.load(std::memory_order_relaxed);
  snapshot.end = prefetchedRangeEnd.load(std::memory_order_relaxed);
  snapshot.generation =
      publishedPrefetchGeneration.load(std::memory_order_relaxed);

  const auto sequenceAfter =
      prefetchRangeSequence.load(std::memory_order_acquire);
  snapshot.valid =
      sequenceBefore == sequenceAfter && (sequenceAfter & 1U) == 0U &&
      snapshot.end > snapshot.start;
  return snapshot;
}

void StemBufferingSource::publishPrefetchRange(
    const juce::int64 start, const juce::int64 end,
    const uint64_t generation) noexcept {
  const auto sequence =
      prefetchRangeSequence.fetch_add(1, std::memory_order_acq_rel);
  jassert((sequence & 1U) == 0U);
  juce::ignoreUnused(sequence);
  prefetchedRangeStart.store(start, std::memory_order_relaxed);
  prefetchedRangeEnd.store(end, std::memory_order_relaxed);
  publishedPrefetchGeneration.store(generation, std::memory_order_relaxed);
  prefetchRangeSequence.fetch_add(1, std::memory_order_release);
}

juce::Range<int>
StemBufferingSource::getValidBufferRange(const int numSamples,
                                         const juce::int64 pos) const {
  if (numSamples <= 0)
    return {};

  const auto requestedEpoch =
      requestedBufferEpoch.load(std::memory_order_acquire);
  const auto sequenceBefore =
      bufferRangeSequence.load(std::memory_order_acquire);
  if ((sequenceBefore & 1U) != 0U)
    return {};
  const auto validStart = bufferValidStart.load(std::memory_order_relaxed);
  const auto validEnd = bufferValidEnd.load(std::memory_order_relaxed);
  const auto publishedEpoch =
      publishedBufferEpoch.load(std::memory_order_relaxed);
  const auto sequenceAfter =
      bufferRangeSequence.load(std::memory_order_acquire);
  if (sequenceBefore != sequenceAfter ||
      (sequenceAfter & 1U) != 0U ||
      publishedEpoch != requestedEpoch ||
      requestedBufferEpoch.load(std::memory_order_acquire) !=
          requestedEpoch) {
    return {};
  }

  const auto maxPosition = std::numeric_limits<juce::int64>::max();
  const auto requestedEnd =
      pos > maxPosition - numSamples ? maxPosition : pos + numSamples;
  const auto overlapStart = juce::jmax(pos, validStart);
  const auto overlapEnd = juce::jmin(requestedEnd, validEnd);
  if (overlapEnd <= overlapStart)
    return {};

  // The overlap is bounded by numSamples before narrowing to int, even when
  // the published window is far from a long-running monotonic play cursor.
  return {
      static_cast<int>(overlapStart - pos),
      static_cast<int>(overlapEnd - pos),
  };
}

juce::Range<int> StemBufferingSource::getValidPrefetchRange(
    const int numSamples, const juce::int64 pos) const {
  if (numSamples <= 0)
    return {};

  const auto requestedGeneration =
      requestedPrefetchGeneration.load(std::memory_order_acquire);
  const auto snapshot = readPrefetchRangeSnapshot();
  if (!snapshot.valid || snapshot.generation != requestedGeneration ||
      requestedPrefetchGeneration.load(std::memory_order_acquire) !=
          requestedGeneration) {
    return {};
  }

  const auto maxPosition = std::numeric_limits<juce::int64>::max();
  const auto requestedEnd =
      pos > maxPosition - numSamples ? maxPosition : pos + numSamples;
  const auto overlapStart = juce::jmax(pos, snapshot.start);
  const auto overlapEnd = juce::jmin(requestedEnd, snapshot.end);
  if (overlapEnd <= overlapStart)
    return {};

  return {
      static_cast<int>(overlapStart - pos),
      static_cast<int>(overlapEnd - pos),
  };
}

bool StemBufferingSource::readPendingPrefetch() {
  if (prefetchSource == nullptr)
    return false;
  const auto generation =
      requestedPrefetchGeneration.load(std::memory_order_acquire);
  if (generation == workerPrefetchGeneration)
    return false;

  const auto position =
      requestedPrefetchPosition.load(std::memory_order_relaxed);
  const int frames = juce::jlimit(
      0, prefetchBuffer.getNumSamples(),
      requestedPrefetchFrames.load(std::memory_order_relaxed));
  if (requestedPrefetchGeneration.load(std::memory_order_acquire) !=
      generation) {
    return true;
  }

  workerPrefetchGeneration = generation;
  publishPrefetchRange(0, 0, generation);
  if (frames <= 0)
    return true;

  // Prime the independent resampler before the published target. This avoids
  // exposing the cold filter transient that follows flushBuffers() at a seek.
  constexpr int prefetchPrerollFrames = 256;
  const int preroll =
      static_cast<int>(juce::jmin<juce::int64>(position,
                                               prefetchPrerollFrames));
  const auto warmupPosition = position - preroll;
  prefetchSource->setNextReadPosition(warmupPosition);
  readBuffer.setSize(numberOfChannels, juce::jmax(frames, preroll), false,
                     false, true);
  if (preroll > 0) {
    juce::AudioSourceChannelInfo warmupInfo(&readBuffer, 0, preroll);
    prefetchSource->getNextAudioBlock(warmupInfo);
  }
  juce::AudioSourceChannelInfo info(&readBuffer, 0, frames);
  prefetchSource->getNextAudioBlock(info);

  if (requestedPrefetchGeneration.load(std::memory_order_acquire) !=
      generation) {
    return true;
  }

  {
    const juce::ScopedLock lock(prefetchLock);
    for (int channel = 0; channel < numberOfChannels; ++channel)
      prefetchBuffer.copyFrom(channel, 0, readBuffer, channel, 0, frames);
    const auto maximumPosition = std::numeric_limits<juce::int64>::max();
    const auto end =
        position > maximumPosition - frames ? maximumPosition
                                            : position + frames;
    publishPrefetchRange(position, end, generation);
  }
  bufferReadyEvent.signal();
  return true;
}

void StemBufferingSource::setPrefetchSource(
    juce::PositionableAudioSource *newPrefetch, int sampleRate,
    int samplesPerBlockExpected) {
  // P0.2 (perf): instalar (o reemplazar) la cadena de prefetch en
  // runtime. La cadena se construye en lazy desde el primer
  // requestPrefetchPosition del StemTrack, no en el loadSongStems. Esto
  // evita el segundo createReaderFor y la duplicación del costo de AV
  // en Windows durante el cambio de canción.
  //
  // Solo se llama desde el message thread. El prefetchLock existente
  // serializa contra el audio thread que pueda estar copiando del
  // prefetchBuffer en getNextAudioBlock().
  if (prefetchSource == newPrefetch)
    return;

  // Liberar el anterior antes de instalar el nuevo para que release
  // (si corresponde) corra con el contexto previo.
  if (prefetchSource != nullptr) {
    prefetchSource->releaseResources();
  }

  prefetchSource = newPrefetch;

  // (Re)alocar el prefetch buffer. Mantener la misma fórmula que
  // prepareToPlay para que requestPrefetchPosition siga trabajando
  // con la capacidad que espera.
  const int newCapacity = prefetchSource != nullptr
                              ? juce::jmin(buffer.getNumSamples(),
                                           juce::jmax(
                                               kMinPrefetchBufferSamples,
                                               samplesPerBlockExpected * 4))
                              : 0;
  {
    const juce::ScopedLock bufferLock(prefetchLock);
    prefetchBuffer.setSize(numberOfChannels, newCapacity);
    prefetchBuffer.clear();
  }
  prefetchCapacityFrames.store(newCapacity, std::memory_order_release);

  // Si tenemos nuevo source y ya estamos preparados, preparearlo para
  // que el worker pueda leer de él.
  if (prefetchSource != nullptr && isPrepared.load(std::memory_order_acquire)) {
    prefetchSource->prepareToPlay(samplesPerBlockExpected,
                                  static_cast<double>(sampleRate));
  }

  // Bump de la generación para invalidar cualquier prefetch pendiente
  // que se hubiera solicitado contra la cadena vieja.
  const auto generation =
      requestedPrefetchGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
  workerPrefetchGeneration = generation;
  requestedPrefetchFrames.store(0, std::memory_order_relaxed);
  pinnedPrefetchGeneration.store(0, std::memory_order_release);
  publishPrefetchRange(0, 0, generation);
}

bool StemBufferingSource::readNextBufferChunk() {
  juce::int64 newBVS, newBVE, sectionToReadStart, sectionToReadEnd;

  if (wasSourceLooping != isLooping()) {
    wasSourceLooping = isLooping();
    requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel);
  }

  const auto workEpoch =
      requestedBufferEpoch.load(std::memory_order_acquire);
  if (workerBufferEpoch != workEpoch) {
    workerBufferEpoch = workEpoch;
    publishValidBufferRange(0, 0, workEpoch);
  }

  newBVS = std::max((juce::int64)0, nextPlayPos.load());
  const auto ringSpan =
      static_cast<juce::int64>(juce::jmax(0, buffer.getNumSamples() - 4));
  newBVE =
      newBVS > std::numeric_limits<juce::int64>::max() - ringSpan
          ? std::numeric_limits<juce::int64>::max()
          : newBVS + ringSpan;
  sectionToReadStart = 0;
  sectionToReadEnd = 0;
  const auto validStart = bufferValidStart.load(std::memory_order_acquire);
  const auto validEnd = bufferValidEnd.load(std::memory_order_acquire);

  constexpr int maxChunkSize = 32768;
  const auto addChunkWithoutOverflow =
      [maxChunkSize](const juce::int64 value) {
    const auto maximum = std::numeric_limits<juce::int64>::max();
    return value > maximum - maxChunkSize
               ? maximum
               : value + static_cast<juce::int64>(maxChunkSize);
  };

  if (newBVS < validStart || newBVS >= validEnd) {
    newBVE = std::min(newBVE, addChunkWithoutOverflow(newBVS));

    sectionToReadStart = newBVS;
    sectionToReadEnd = newBVE;

    publishValidBufferRange(0, 0, workEpoch);
  } else if (std::abs(newBVS - validStart) > 512 ||
             std::abs(newBVE - validEnd) > 512) {
    newBVE = std::min(newBVE, addChunkWithoutOverflow(validEnd));

    sectionToReadStart = validEnd;
    sectionToReadEnd = newBVE;

    publishValidBufferRange(newBVS, std::min(validEnd, newBVE), workEpoch);
  }

  if (sectionToReadStart == sectionToReadEnd)
    return false;

  jassert(buffer.getNumSamples() > 0);

  const auto bufferIndexStart =
      (int)(sectionToReadStart % buffer.getNumSamples());
  const auto bufferIndexEnd = (int)(sectionToReadEnd % buffer.getNumSamples());

  if (bufferIndexStart < bufferIndexEnd) {
    readBufferSection(sectionToReadStart,
                      (int)(sectionToReadEnd - sectionToReadStart),
                      bufferIndexStart);
  } else {
    const auto initialSize = buffer.getNumSamples() - bufferIndexStart;

    readBufferSection(sectionToReadStart, initialSize, bufferIndexStart);

    readBufferSection(sectionToReadStart + initialSize,
                      (int)(sectionToReadEnd - sectionToReadStart) -
                          initialSize,
                      0);
  }

  if (requestedBufferEpoch.load(std::memory_order_acquire) != workEpoch)
    return true;

  publishValidBufferRange(newBVS, newBVE, workEpoch);

  bufferReadyEvent.signal();
  return true;
}

void StemBufferingSource::readBufferSection(juce::int64 start, int length,
                                            int bufferOffset) {
  if (source->getNextReadPosition() != start)
    source->setNextReadPosition(start);

  if (length <= 0)
    return;

  // Read into a staging buffer without holding the shared lock.
  readBuffer.setSize(numberOfChannels, length, false, false, true);
  juce::AudioSourceChannelInfo info(&readBuffer, 0, length);
  source->getNextAudioBlock(info);

  // Copy into the shared ring buffer under a short lock.
  const juce::ScopedLock sl(callbackLock);
  for (int chan = 0; chan < numberOfChannels; ++chan)
    buffer.copyFrom(chan, bufferOffset, readBuffer, chan, 0, length);
}

int StemBufferingSource::useTimeSlice() {
  // ---- Benchmark: bracket the worker drain ----------------------------
  // The audio thread samples (lastWorkerDrainStartTick,
  // lastWorkerDrainDurationTicks) to learn how long each visit takes
  // and how long the worker has been away. getHighResolutionTicks is
  // a hardware register read — safe from the worker thread.
  const int64_t drainStartTick = juce::Time::getHighResolutionTicks();
  workerVisitCount.fetch_add(1, std::memory_order_relaxed);
  lastWorkerDrainStartTick.store(drainStartTick, std::memory_order_relaxed);

  // readPendingPrefetch returns true if a prefetch drain happened;
  // readNextBufferChunk returns true if a main-buffer refill happened.
  // Both are real I/O — the rest of the time useTimeSlice is a no-op
  // and the buffer is already full.
  const bool didPrefetch = readPendingPrefetch();
  const bool didMainRefill = !didPrefetch && readNextBufferChunk();
  const bool didRealWork = didPrefetch || didMainRefill;

  const int returnValue = didPrefetch ? 0 : (didMainRefill ? 5 : 10);

  const int64_t drainEndTick = juce::Time::getHighResolutionTicks();
  const int64_t drainDuration = drainEndTick - drainStartTick;
  lastWorkerDrainDurationTicks.store(drainDuration,
                                      std::memory_order_relaxed);

  // ---- Benchmark: track REAL drains separately -----------------------
  // Only update when the worker actually had to read from disk /
  // decoder. The exchange + store gives the audio thread a
  // (lastRealDrainEndTick, previousRealDrainEndTick) pair from which
  // it can compute the inter-real-drain gap without depending on
  // when the audio thread happens to sample.
  if (didRealWork) {
    const int64_t prevEnd =
        lastRealDrainEndTick.exchange(drainEndTick,
                                       std::memory_order_relaxed);
    previousRealDrainEndTick.store(prevEnd, std::memory_order_relaxed);
    lastRealDrainDurationTicks.store(drainDuration,
                                      std::memory_order_relaxed);
    realDrainCount.fetch_add(1, std::memory_order_relaxed);
  }
  return returnValue;
}
