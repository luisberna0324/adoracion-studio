#include "StemBufferingSource.h"

StemBufferingSource::StemBufferingSource(juce::PositionableAudioSource *s,
                                         juce::TimeSliceThread &thread,
                                         bool deleteSourceWhenDeleted,
                                         int bufferSizeSamples, int numChannels,
                                         bool prefillBufferOnPrepareToPlay)
    : source(s, deleteSourceWhenDeleted), backgroundThread(thread),
      numberOfSamplesToBuffer(juce::jmax(32768, bufferSizeSamples)),
      numberOfChannels(numChannels), prefillBuffer(prefillBufferOnPrepareToPlay) {
  jassert(source != nullptr);
}

StemBufferingSource::~StemBufferingSource() { releaseResources(); }

void StemBufferingSource::prepareToPlay(int samplesPerBlockExpected,
                                        double newSampleRate) {
  auto bufferSizeNeeded =
      juce::jmax(samplesPerBlockExpected * 2, numberOfSamplesToBuffer);

  if (!juce::approximatelyEqual(newSampleRate, sampleRate) ||
      bufferSizeNeeded != buffer.getNumSamples() || !isPrepared) {
    backgroundThread.removeTimeSliceClient(this);

    isPrepared = true;
    sampleRate = newSampleRate;

    source->prepareToPlay(samplesPerBlockExpected, newSampleRate);

    buffer.setSize(numberOfChannels, bufferSizeNeeded);
    buffer.clear();

    const auto epoch =
        requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    workerBufferEpoch = epoch;
    publishValidBufferRange(0, 0, epoch);

    backgroundThread.addTimeSliceClient(this);

    do {
      backgroundThread.moveToFrontOfQueue(this);
      juce::Thread::sleep(5);
    } while (prefillBuffer &&
             (bufferValidEnd.load(std::memory_order_acquire) -
                  bufferValidStart.load(std::memory_order_acquire) <
              std::min((int)(newSampleRate / 4.0), buffer.getNumSamples() / 2)));
  }
}

void StemBufferingSource::releaseResources() {
  isPrepared = false;
  backgroundThread.removeTimeSliceClient(this);

  const auto epoch =
      requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
  workerBufferEpoch = epoch;
  publishValidBufferRange(0, 0, epoch);
  buffer.setSize(numberOfChannels, 0);

  if (source != this)
    source->releaseResources();
}

void StemBufferingSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &info) {
  if (info.numSamples <= 0)
    return;

  const auto blockStart = nextPlayPos.load(std::memory_order_acquire);
  const auto blockCursorEpoch =
      cursorEpoch.load(std::memory_order_acquire);
  const auto blockBufferEpoch =
      requestedBufferEpoch.load(std::memory_order_acquire);
  const auto bufferRange =
      getValidBufferRange(info.numSamples, blockStart);
  auto validStart =
      juce::jlimit(0, info.numSamples, bufferRange.getStart());
  auto validEnd =
      juce::jlimit(validStart, info.numSamples, bufferRange.getEnd());

  // Start from deterministic silence so empty/partial read-ahead ranges and
  // output channels beyond numberOfChannels cannot leak stale samples.
  info.clearActiveBufferRegion();

  if (validStart < validEnd) {
    if (!callbackLock.tryEnter()) {
      validStart = validEnd = 0;
    } else {
      // The worker may have republished its window between the optimistic
      // atomic snapshot and this lock. Revalidate while the ring itself is
      // stable so stale offsets can never address newly repurposed storage.
      const auto lockedRange =
          getValidBufferRange(info.numSamples, blockStart);
      validStart =
          juce::jlimit(0, info.numSamples, lockedRange.getStart());
      validEnd =
          juce::jlimit(validStart, info.numSamples, lockedRange.getEnd());

      if (validStart < validEnd) {
        for (int chan =
                 std::min(numberOfChannels, info.buffer->getNumChannels());
             --chan >= 0;) {
          jassert(buffer.getNumSamples() > 0);

          const auto startBufferIndex =
              (int)((validStart + blockStart) % buffer.getNumSamples());
          const auto endBufferIndex =
              (int)((validEnd + blockStart) % buffer.getNumSamples());

          if (startBufferIndex < endBufferIndex) {
            info.buffer->copyFrom(chan, info.startSample + validStart, buffer,
                                  chan, startBufferIndex,
                                  validEnd - validStart);
          } else {
            const auto initialSize = buffer.getNumSamples() - startBufferIndex;

            info.buffer->copyFrom(chan, info.startSample + validStart, buffer,
                                  chan, startBufferIndex, initialSize);

            info.buffer->copyFrom(
                chan, info.startSample + validStart + initialSize, buffer, chan,
                0, (validEnd - validStart) - initialSize);
          }
        }
      }

      // A control-thread seek can invalidate the range without waiting for
      // this short copy lock. If it landed during the copy, discard the old
      // timeline audio instead of exposing one stale block.
      if (requestedBufferEpoch.load(std::memory_order_acquire) !=
          blockBufferEpoch) {
        info.clearActiveBufferRegion();
        validStart = validEnd = 0;
      }
      callbackLock.exit();
    }
  }

  const auto missingFrames =
      info.numSamples - juce::jmax(0, validEnd - validStart);
  if (missingFrames > 0) {
    realtimeMissCount.fetch_add(1, std::memory_order_relaxed);
    realtimeMissingFrameCount.fetch_add(missingFrames,
                                        std::memory_order_relaxed);
  }

#if defined(MTCONTROLS_ENABLE_TEST_HOOKS) && MTCONTROLS_ENABLE_TEST_HOOKS
  if (beforeCursorAdvanceTestHook != nullptr)
    beforeCursorAdvanceTestHook(*this);
#endif

  // Advance only the position captured for this block. A concurrent seek
  // increments the epoch before publishing its destination, so either this
  // epoch check fails, the CAS fails against the new position, or the seek
  // overwrites an advance that completed first. The callback can therefore
  // never add its block length to the seek destination.
  auto expectedPosition = blockStart;
  const auto maximumPosition = std::numeric_limits<juce::int64>::max();
  const auto advancedPosition =
      blockStart > maximumPosition - info.numSamples
          ? maximumPosition
          : blockStart + static_cast<juce::int64>(info.numSamples);
  if (cursorEpoch.load(std::memory_order_acquire) == blockCursorEpoch) {
    nextPlayPos.compare_exchange_strong(
        expectedPosition, advancedPosition,
        std::memory_order_release, std::memory_order_relaxed);
  }
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

void StemBufferingSource::setNextReadPosition(juce::int64 newPosition) {
  // Announce the seek before publishing its position. Combined with the
  // callback's captured epoch and position CAS, this ordering guarantees that
  // an in-flight block cannot advance the new destination.
  cursorEpoch.fetch_add(1, std::memory_order_acq_rel);
  nextPlayPos.store(newPosition, std::memory_order_release);

  // Advance the buffer epoch only after publishing the destination, so a
  // worker that observes the new epoch must also observe the new position.
  // Do not touch the worker-owned start/end pair here.
  requestedBufferEpoch.fetch_add(1, std::memory_order_acq_rel);
  backgroundThread.moveToFrontOfQueue(this);
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
  const auto addChunkWithoutOverflow = [](const juce::int64 value) {
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
  return readNextBufferChunk() ? 10 : 100;
}
