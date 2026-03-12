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

    const juce::ScopedLock sl(bufferRangeLock);

    bufferValidStart = 0;
    bufferValidEnd = 0;

    backgroundThread.addTimeSliceClient(this);

    do {
      const juce::ScopedUnlock ul(bufferRangeLock);
      backgroundThread.moveToFrontOfQueue(this);
      juce::Thread::sleep(5);
    } while (prefillBuffer &&
             (bufferValidEnd - bufferValidStart <
              juce::jmin(((int)newSampleRate) / 4, buffer.getNumSamples() / 2)));
  }
}

void StemBufferingSource::releaseResources() {
  isPrepared = false;
  backgroundThread.removeTimeSliceClient(this);

  buffer.setSize(numberOfChannels, 0);

  if (source != this)
    source->releaseResources();
}

void StemBufferingSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &info) {
  const auto bufferRange = getValidBufferRange(info.numSamples);

  if (bufferRange.isEmpty()) {
    info.clearActiveBufferRegion();
    return;
  }

  const auto validStart = bufferRange.getStart();
  const auto validEnd = bufferRange.getEnd();

  if (validStart > 0)
    info.buffer->clear(info.startSample, validStart);

  if (validEnd < info.numSamples)
    info.buffer->clear(info.startSample + validEnd, info.numSamples - validEnd);

  if (validStart < validEnd) {
    const juce::ScopedLock sl(callbackLock);
    for (int chan = juce::jmin(numberOfChannels, info.buffer->getNumChannels());
         --chan >= 0;) {
      jassert(buffer.getNumSamples() > 0);

      const auto startBufferIndex =
          (int)((validStart + nextPlayPos) % buffer.getNumSamples());
      const auto endBufferIndex =
          (int)((validEnd + nextPlayPos) % buffer.getNumSamples());

      if (startBufferIndex < endBufferIndex) {
        info.buffer->copyFrom(chan, info.startSample + validStart, buffer, chan,
                              startBufferIndex, validEnd - validStart);
      } else {
        const auto initialSize = buffer.getNumSamples() - startBufferIndex;

        info.buffer->copyFrom(chan, info.startSample + validStart, buffer, chan,
                              startBufferIndex, initialSize);

        info.buffer->copyFrom(chan, info.startSample + validStart + initialSize,
                              buffer, chan, 0,
                              (validEnd - validStart) - initialSize);
      }
    }
  }

  nextPlayPos += info.numSamples;
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
    const auto bufferRange = getValidBufferRange(info.numSamples);

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
  jassert(source->getTotalLength() > 0);
  const auto pos = nextPlayPos.load();

  return (source->isLooping() && nextPlayPos > 0)
             ? pos % source->getTotalLength()
             : pos;
}

void StemBufferingSource::setNextReadPosition(juce::int64 newPosition) {
  const juce::ScopedLock sl(bufferRangeLock);

  nextPlayPos = newPosition;
  backgroundThread.moveToFrontOfQueue(this);
}

juce::Range<int>
StemBufferingSource::getValidBufferRange(int numSamples) const {
  const juce::ScopedLock sl(bufferRangeLock);

  const auto pos = nextPlayPos.load();

  return {(int)(juce::jlimit(bufferValidStart, bufferValidEnd, pos) - pos),
          (int)(juce::jlimit(bufferValidStart, bufferValidEnd,
                             pos + numSamples) -
                pos)};
}

bool StemBufferingSource::readNextBufferChunk() {
  juce::int64 newBVS, newBVE, sectionToReadStart, sectionToReadEnd;

  {
    const juce::ScopedLock sl(bufferRangeLock);

    if (wasSourceLooping != isLooping()) {
      wasSourceLooping = isLooping();
      bufferValidStart = 0;
      bufferValidEnd = 0;
    }

    newBVS = juce::jmax((juce::int64)0, nextPlayPos.load());
    newBVE = newBVS + buffer.getNumSamples() - 4;
    sectionToReadStart = 0;
    sectionToReadEnd = 0;

    // CUSTOM FIX: Increased from 2048 to 32768 (around 0.74s)
    // This allows sequential reads to be MUCH larger, reducing disk IOPS
    // and thread switching thrashing from OS.
    constexpr int maxChunkSize = 32768;

    if (newBVS < bufferValidStart || newBVS >= bufferValidEnd) {
      newBVE = juce::jmin(newBVE, newBVS + maxChunkSize);

      sectionToReadStart = newBVS;
      sectionToReadEnd = newBVE;

      bufferValidStart = 0;
      bufferValidEnd = 0;
    } else if (std::abs((int)(newBVS - bufferValidStart)) > 512 ||
               std::abs((int)(newBVE - bufferValidEnd)) > 512) {
      newBVE = juce::jmin(newBVE, bufferValidEnd + maxChunkSize);

      sectionToReadStart = bufferValidEnd;
      sectionToReadEnd = newBVE;

      bufferValidStart = newBVS;
      bufferValidEnd = juce::jmin(bufferValidEnd, newBVE);
    }
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

  {
    const juce::ScopedLock sl2(bufferRangeLock);

    bufferValidStart = newBVS;
    bufferValidEnd = newBVE;
  }

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
