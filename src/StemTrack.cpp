#include "StemTrack.h"

StemTrack::StemTrack(std::unique_ptr<juce::AudioFormatReader> reader,
                     juce::TimeSliceThread &sharedReadThread,
                     double sampleRateToUse, int readAheadSamples,
                     const bool useReadAhead) {
  // Transfer ownership of reader to AudioFormatReaderSource
  readerSource =
      std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

  // Resample to the host sample rate if needed, then buffer on a background thread.
  resamplingSource = std::make_unique<ResamplingPositionableSource>(
      readerSource.get(), sampleRateToUse, 2);

  if (useReadAhead) {
    // Realtime playback must never block on disk I/O.
    bufferingSource = std::make_unique<StemBufferingSource>(
        resamplingSource.get(), sharedReadThread, false, readAheadSamples, 2);
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
  // Read from the buffering source directly into the provided temporary buffer
  jassert(activeSource != nullptr);
  activeSource->getNextAudioBlock(bufferToFill);

  const int numSamples = bufferToFill.numSamples;
  const int numChannels = bufferToFill.buffer->getNumChannels();
  const int startSample = bufferToFill.startSample;

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
  activeSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void StemTrack::releaseResources() { activeSource->releaseResources(); }

void StemTrack::setNextReadPosition(juce::int64 newPosition) {
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
