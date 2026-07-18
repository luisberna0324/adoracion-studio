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

    bufferValidStart.store(0, std::memory_order_relaxed);
    bufferValidEnd.store(0, std::memory_order_relaxed);

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
    if (!callbackLock.tryEnter()) {
      info.clearActiveBufferRegion();
      realtimeMissCount.fetch_add(1, std::memory_order_relaxed);
      nextPlayPos += info.numSamples;
      return;
    }
    for (int chan = std::min(numberOfChannels, info.buffer->getNumChannels());
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
    callbackLock.exit();
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
  const auto currentPosition = nextPlayPos.load(std::memory_order_acquire);
  const auto validStart = bufferValidStart.load(std::memory_order_acquire);
  const auto validEnd = bufferValidEnd.load(std::memory_order_acquire);
  if (newPosition == currentPosition && newPosition >= validStart &&
      newPosition < validEnd) {
    return;
  }

  nextPlayPos = newPosition;
  bufferValidStart.store(0, std::memory_order_release);
  bufferValidEnd.store(0, std::memory_order_release);
  backgroundThread.moveToFrontOfQueue(this);
}

juce::Range<int>
StemBufferingSource::getValidBufferRange(int numSamples) const {
  const auto pos = nextPlayPos.load();
  const auto validStart = bufferValidStart.load(std::memory_order_acquire);
  const auto validEnd = bufferValidEnd.load(std::memory_order_acquire);

  return {(int)(juce::jlimit(validStart, validEnd, pos) - pos),
          (int)(juce::jlimit(validStart, validEnd, pos + numSamples) -
                pos)};
}

bool StemBufferingSource::readNextBufferChunk() {
  juce::int64 newBVS, newBVE, sectionToReadStart, sectionToReadEnd;

  if (wasSourceLooping != isLooping()) {
    wasSourceLooping = isLooping();
    bufferValidStart.store(0, std::memory_order_release);
    bufferValidEnd.store(0, std::memory_order_release);
  }

  newBVS = std::max((juce::int64)0, nextPlayPos.load());
  newBVE = newBVS + buffer.getNumSamples() - 4;
  sectionToReadStart = 0;
  sectionToReadEnd = 0;
  const auto validStart = bufferValidStart.load(std::memory_order_acquire);
  const auto validEnd = bufferValidEnd.load(std::memory_order_acquire);

  constexpr int maxChunkSize = 32768;

  if (newBVS < validStart || newBVS >= validEnd) {
    newBVE = std::min(newBVE, newBVS + (juce::int64)maxChunkSize);

    sectionToReadStart = newBVS;
    sectionToReadEnd = newBVE;

    bufferValidStart.store(0, std::memory_order_release);
    bufferValidEnd.store(0, std::memory_order_release);
  } else if (std::abs((int)(newBVS - validStart)) > 512 ||
             std::abs((int)(newBVE - validEnd)) > 512) {
    newBVE = std::min(newBVE, validEnd + (juce::int64)maxChunkSize);

    sectionToReadStart = validEnd;
    sectionToReadEnd = newBVE;

    bufferValidStart.store(newBVS, std::memory_order_release);
    bufferValidEnd.store(std::min(validEnd, newBVE),
                         std::memory_order_release);
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

  bufferValidStart.store(newBVS, std::memory_order_release);
  bufferValidEnd.store(newBVE, std::memory_order_release);

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
