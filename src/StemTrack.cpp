#include "StemTrack.h"

StemTrack::StemTrack(std::unique_ptr<juce::AudioFormatReader> reader, double sampleRateToUse,
                     int readAheadSamples) 
    : readThread("StemReader") {
  readThread.startThread(juce::Thread::Priority::high);

  // Transfer ownership of reader to AudioFormatReaderSource
  readerSource =
      std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

  // Resample to the host sample rate if needed, then buffer on a background thread.
  resamplingSource = std::make_unique<ResamplingPositionableSource>(
      readerSource.get(), sampleRateToUse, 2);

  // Wrap it in a BufferingAudioSource with a dedicated read-ahead buffer thread
  bufferingSource = std::make_unique<StemBufferingSource>(
      resamplingSource.get(), readThread, false, readAheadSamples, 2);
}

StemTrack::~StemTrack() {
  // Destroy the client first so it deregisters from the thread
  bufferingSource.reset();
  // Safely stop the thread
  readThread.stopThread(2000);
}

void StemTrack::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  // Read from the buffering source directly into the provided temporary buffer
  bufferingSource->getNextAudioBlock(bufferToFill);

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
  bufferingSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void StemTrack::releaseResources() { bufferingSource->releaseResources(); }

void StemTrack::setNextReadPosition(juce::int64 newPosition) {
  bufferingSource->setNextReadPosition(newPosition);
}

juce::int64 StemTrack::getNextReadPosition() const {
  return bufferingSource->getNextReadPosition();
}

juce::int64 StemTrack::getTotalLength() const {
  return bufferingSource->getTotalLength();
}

bool StemTrack::isLooping() const { return bufferingSource->isLooping(); }

void StemTrack::setLooping(bool shouldLoop) {
  bufferingSource->setLooping(shouldLoop);
}
