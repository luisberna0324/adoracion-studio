#include "StemTrack.h"

#include <cmath>

StemTrack::StemTrack(std::unique_ptr<juce::AudioFormatReader> reader,
                     juce::TimeSliceThread &sharedReadThread,
                     double sampleRateToUse, int readAheadSamples,
                     const bool useReadAhead,
                     std::unique_ptr<juce::AudioFormatReader> prefetchReader) {
  // Transfer ownership of reader to AudioFormatReaderSource
  readerSource =
      std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

  // Resample to the host sample rate if needed, then buffer on a background thread.
  resamplingSource = std::make_unique<ResamplingPositionableSource>(
      readerSource.get(), sampleRateToUse, 2);

  if (useReadAhead) {
    if (prefetchReader != nullptr) {
      prefetchReaderSource =
          std::make_unique<juce::AudioFormatReaderSource>(
              prefetchReader.release(), true);
      prefetchResamplingSource =
          std::make_unique<ResamplingPositionableSource>(
              prefetchReaderSource.get(), sampleRateToUse, 2);
    }
    // Realtime playback must never block on disk I/O.
    bufferingSource = std::make_unique<StemBufferingSource>(
        resamplingSource.get(), sharedReadThread, false, readAheadSamples, 2,
        true, prefetchResamplingSource.get());
    activeSource = bufferingSource.get();
  } else {
    // Offline rendering advances faster than a read-ahead worker can refill;
    // synchronous reads are deterministic and cannot create buffer-miss gaps.
    activeSource = resamplingSource.get();
  }
}

StemTrack::~StemTrack() { bufferingSource.reset(); }

void StemTrack::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  jassert(activeSource != nullptr);
  int discontinuityOffset = -1;
  int discontinuityFade = 0;
  int leadingFadeFrames = pendingLeadingDiscontinuityFadeFrames;
  pendingLeadingDiscontinuityFadeFrames = 0;
  if (readDiscontinuityPending) {
    const int prefixFrames =
        juce::jlimit(0, bufferToFill.numSamples,
                     readDiscontinuityPrefixFrames);
    const auto targetFrame = readDiscontinuityTargetFrame;
    discontinuityFade = readDiscontinuityFadeFrames;
    readDiscontinuityPending = false;

    if (prefixFrames > 0) {
      juce::AudioSourceChannelInfo prefixInfo(
          bufferToFill.buffer, bufferToFill.startSample, prefixFrames);
      activeSource->getNextAudioBlock(prefixInfo);
    }

    activeSource->setNextReadPosition(targetFrame);
    if (prefixFrames < bufferToFill.numSamples) {
      juce::AudioSourceChannelInfo suffixInfo(
          bufferToFill.buffer, bufferToFill.startSample + prefixFrames,
          bufferToFill.numSamples - prefixFrames);
      activeSource->getNextAudioBlock(suffixInfo);
    }
    discontinuityOffset = prefixFrames;
    if (prefixFrames == 0)
      leadingFadeFrames = juce::jmax(leadingFadeFrames, discontinuityFade);
    else if (prefixFrames == bufferToFill.numSamples)
      pendingLeadingDiscontinuityFadeFrames =
          juce::jmax(pendingLeadingDiscontinuityFadeFrames,
                     discontinuityFade);
  } else {
    // Read from the buffering source directly into the provided temporary
    // buffer.
    activeSource->getNextAudioBlock(bufferToFill);
  }

  const int numSamples = bufferToFill.numSamples;
  const int numChannels = bufferToFill.buffer->getNumChannels();
  const int startSample = bufferToFill.startSample;

  if (leadingFadeFrames > 0 && numSamples > 0) {
    const int fadeFrames = juce::jmin(leadingFadeFrames, numSamples);
    constexpr float halfPi = juce::MathConstants<float>::halfPi;
    for (int sample = 0; sample < fadeFrames; ++sample) {
      const float phase =
          static_cast<float>(sample + 1) / static_cast<float>(fadeFrames);
      const float gain = std::sin(halfPi * phase);
      for (int channel = 0; channel < numChannels; ++channel) {
        bufferToFill.buffer->applyGain(
            channel, startSample + sample, 1, gain);
      }
    }
  }

  if (discontinuityOffset > 0 && discontinuityOffset < numSamples &&
      discontinuityFade > 0) {
    const int fadeFrames =
        juce::jmin(discontinuityFade,
                   juce::jmin(discontinuityOffset,
                              numSamples - discontinuityOffset));
    constexpr float halfPi = juce::MathConstants<float>::halfPi;
    for (int sample = 0; sample < fadeFrames; ++sample) {
      const float phase =
          static_cast<float>(sample + 1) /
          static_cast<float>(fadeFrames);
      const float fadeOut = std::cos(halfPi * phase);
      const float fadeIn = std::sin(halfPi * phase);
      const int outgoingSample =
          startSample + discontinuityOffset - fadeFrames + sample;
      const int incomingSample = startSample + discontinuityOffset + sample;
      for (int channel = 0; channel < numChannels; ++channel) {
        bufferToFill.buffer->setSample(
            channel, outgoingSample,
            bufferToFill.buffer->getSample(channel, outgoingSample) *
                fadeOut);
        bufferToFill.buffer->setSample(
            channel, incomingSample,
            bufferToFill.buffer->getSample(channel, incomingSample) * fadeIn);
      }
    }
  } else if (discontinuityOffset == numSamples &&
             discontinuityFade > 0) {
    const int fadeFrames = juce::jmin(discontinuityFade, numSamples);
    constexpr float halfPi = juce::MathConstants<float>::halfPi;
    for (int sample = 0; sample < fadeFrames; ++sample) {
      const float phase =
          static_cast<float>(sample + 1) / static_cast<float>(fadeFrames);
      const float gain = std::cos(halfPi * phase);
      const int outgoingSample =
          startSample + numSamples - fadeFrames + sample;
      for (int channel = 0; channel < numChannels; ++channel) {
        bufferToFill.buffer->applyGain(channel, outgoingSample, 1, gain);
      }
    }
  }

  // Find max amplitude for VU (before gain is applied to match old logic)
  float magnitude = 0.0f;
  for (int channel = 0; channel < numChannels; ++channel) {
    magnitude = juce::jmax(
        magnitude, bufferToFill.buffer->getMagnitude(channel, startSample, numSamples));
  }

  const bool isAudible =
      !isMuted() && (!isAnySoloActive.load(std::memory_order_relaxed) || isSoloed());
      
  const float userVolume = juce::jlimit(0.0f, 1.0f, getVolume());
  const float trimGain = juce::jlimit(0.0f, 2.0f, getTrim());
  const float mixGain = globalMixGain.load(std::memory_order_relaxed);
  const float stemGain = isAudible ? (userVolume * trimGain * mixGain) : 0.0f;

  if (stemGain > 0.0f) {
    bufferToFill.buffer->applyGain(startSample, numSamples, stemGain);
  } else {
    bufferToFill.clearActiveBufferRegion();
  }

  // Update meter reading
  const float meterGain = isAudible ? (userVolume * trimGain) : 0.0f;
  currentMagnitude.store(magnitude * meterGain, std::memory_order_relaxed);
}

void StemTrack::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
  readDiscontinuityPending = false;
  pendingLeadingDiscontinuityFadeFrames = 0;
  activeSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void StemTrack::releaseResources() {
  readDiscontinuityPending = false;
  pendingLeadingDiscontinuityFadeFrames = 0;
  activeSource->releaseResources();
}

void StemTrack::setNextReadPosition(juce::int64 newPosition) {
  readDiscontinuityPending = false;
  pendingLeadingDiscontinuityFadeFrames = 0;
  activeSource->setNextReadPosition(newPosition);
}

juce::int64 StemTrack::getNextReadPosition() const {
  return activeSource->getNextReadPosition();
}

juce::int64 StemTrack::getTotalLength() const {
  return activeSource->getTotalLength();
}

bool StemTrack::isLooping() const { return activeSource->isLooping(); }

void StemTrack::setLooping(bool shouldLoop) {
  activeSource->setLooping(shouldLoop);
}

bool StemTrack::waitForNextAudioBlockReady(const int numSamples,
                                           const juce::uint32 timeoutMs) const {
  if (bufferingSource == nullptr || numSamples <= 0)
    return true;

  juce::AudioBuffer<float> readinessBuffer(2, numSamples);
  juce::AudioSourceChannelInfo info(&readinessBuffer, 0, numSamples);
  return bufferingSource->waitForNextAudioBlockReady(info, timeoutMs);
}

void StemTrack::requestPrefetchPosition(const juce::int64 position,
                                        const int minimumFrames) noexcept {
  if (bufferingSource != nullptr)
    bufferingSource->requestPrefetchPosition(position, minimumFrames);
}

bool StemTrack::isPrefetchReady(const juce::int64 position,
                                const int minimumFrames) const noexcept {
  return bufferingSource == nullptr ||
         bufferingSource->isPrefetchReady(position, minimumFrames);
}

void StemTrack::scheduleReadDiscontinuity(
    const int prefixFrames, const juce::int64 targetFrame,
    const int fadeFrames) noexcept {
  readDiscontinuityPrefixFrames = juce::jmax(0, prefixFrames);
  readDiscontinuityTargetFrame = juce::jmax<juce::int64>(0, targetFrame);
  readDiscontinuityFadeFrames = juce::jmax(0, fadeFrames);
  readDiscontinuityPending = true;
}
