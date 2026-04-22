#include "PitchShiftProcessor.h"

#include <algorithm>

void PitchShiftProcessor::prepare(double sampleRate, int samplesPerBlock,
                                  int numChannels) {
  currentSampleRate = sampleRate;
  currentChannels = numChannels;
  preferredBlockSamples = samplesPerBlock;
  prepared = (currentSampleRate > 0.0 && currentChannels > 0);

  interleavedInput.clear();
  receiveBuffer.clear();
  outputFifo.clear();
  outputReadPos = 0;
  outputWritePos = 0;
  outputAvailableSamples = 0;

  if (!prepared) {
    latencySamples = 0;
    return;
  }

  if (currentChannels > SOUNDTOUCH_MAX_CHANNELS) {
    prepared = false;
    latencySamples = 0;
    return;
  }

  soundtouch.setSampleRate(static_cast<unsigned int>(currentSampleRate));
  soundtouch.setChannels(static_cast<unsigned int>(currentChannels));
  soundtouch.setPitchSemiTones(static_cast<float>(pitchSemiTones));
  soundtouch.setTempo(static_cast<float>(tempoRatio));
  soundtouch.setRate(1.0f);
  soundtouch.clear();

  if (samplesPerBlock > 0 && currentChannels > 0) {
    interleavedInput.resize(
        static_cast<size_t>(samplesPerBlock * currentChannels));
    receiveBuffer.resize(
        static_cast<size_t>(samplesPerBlock * currentChannels * 2));
  }

  primeLatency();
}

void PitchShiftProcessor::reset() {
  soundtouch.clear();
  primeLatency();
}

void PitchShiftProcessor::setPitchSemiTones(int semitones) {
  if (pitchSemiTones == semitones)
    return;
  pitchSemiTones = semitones;
  if (prepared) {
    soundtouch.setPitchSemiTones(static_cast<float>(pitchSemiTones));
    latencySamples = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
    if (latencySamples < 0)
      latencySamples = 0;
  }
}

void PitchShiftProcessor::setTempoRatio(double ratio) {
  const double normalized = juce::jlimit(0.5, 2.0, ratio);
  if (juce::approximatelyEqual(tempoRatio, normalized))
    return;
  tempoRatio = normalized;
  if (prepared) {
    soundtouch.setTempo(static_cast<float>(tempoRatio));
    latencySamples = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
    if (latencySamples < 0)
      latencySamples = 0;
  }
}

void PitchShiftProcessor::setPhaseOffsetSamples(int samples) noexcept {
  phaseOffsetSamples = juce::jmax(0, samples);
}

int PitchShiftProcessor::getPhaseOffsetSamples() const noexcept {
  return phaseOffsetSamples;
}

int PitchShiftProcessor::getPitchSemiTones() const noexcept {
  return pitchSemiTones;
}

double PitchShiftProcessor::getTempoRatio() const noexcept { return tempoRatio; }

int PitchShiftProcessor::getLatencySamples() const noexcept {
  return latencySamples;
}

int PitchShiftProcessor::getNumChannels() const noexcept {
  return currentChannels;
}

bool PitchShiftProcessor::isPrepared() const noexcept { return prepared; }

void PitchShiftProcessor::process(juce::AudioBuffer<float> &buffer) {
  process(buffer, buffer.getNumSamples(), nullptr);
}

void PitchShiftProcessor::process(juce::AudioBuffer<float> &buffer,
                                  int outputSamples,
                                  ProcessStats *stats) {
  if (stats != nullptr)
    *stats = {};

  if (!prepared || currentChannels <= 0)
    return;
  if (pitchSemiTones == 0 && juce::approximatelyEqual(tempoRatio, 1.0))
    return;

  const int inputSamples = buffer.getNumSamples();
  if (inputSamples <= 0 || outputSamples <= 0)
    return;
  if (stats != nullptr) {
    stats->inputFrames = inputSamples;
    stats->requestedOutputFrames = outputSamples;
    stats->fifoAvailableBeforeFrames =
        static_cast<int>(outputAvailableSamples /
                         static_cast<size_t>(currentChannels));
  }

  if (buffer.getNumChannels() != currentChannels) {
    prepare(currentSampleRate,
            juce::jmax(outputSamples, preferredBlockSamples),
            buffer.getNumChannels());
  }

  // Keep FIFO refill behavior tied to the real render block size, even if
  // incoming time-stretched chunks are much smaller.
  preferredBlockSamples = juce::jmax(preferredBlockSamples, outputSamples);

  const size_t requiredInputSize =
      static_cast<size_t>(inputSamples * currentChannels);
  if (interleavedInput.size() < requiredInputSize)
    interleavedInput.resize(requiredInputSize);

  for (int sample = 0; sample < inputSamples; ++sample) {
    const size_t base = static_cast<size_t>(sample * currentChannels);
    for (int ch = 0; ch < currentChannels; ++ch)
      interleavedInput[base + static_cast<size_t>(ch)] =
          buffer.getSample(ch, sample);
  }

  soundtouch.putSamples(interleavedInput.data(), inputSamples);

  const int framesNeeded = outputSamples;
  const int preferredFrames = juce::jmax(1, preferredBlockSamples);
  const int targetBufferedFrames = framesNeeded + preferredFrames;
  while (static_cast<int>(outputAvailableSamples /
                          static_cast<size_t>(currentChannels)) <
         targetBufferedFrames) {
    const int availableFrames =
        static_cast<int>(outputAvailableSamples /
                         static_cast<size_t>(currentChannels));
    const int missingFrames = targetBufferedFrames - availableFrames;
    const int maxFrames = juce::jmax(preferredFrames, missingFrames);
    if (receiveBuffer.size() <
        static_cast<size_t>(maxFrames * currentChannels)) {
      receiveBuffer.resize(
          static_cast<size_t>(maxFrames * currentChannels));
    }

    const int received = soundtouch.receiveSamples(receiveBuffer.data(),
                                                   maxFrames);
    if (received <= 0)
      break;
    if (stats != nullptr) {
      stats->receivedFrames += received;
      stats->receiveCalls += 1;
      stats->maxReceiveFrames = juce::jmax(stats->maxReceiveFrames, received);
    }

    const size_t appendCount =
        static_cast<size_t>(received * currentChannels);
    pushOutputSamples(receiveBuffer.data(), appendCount);
  }

  const int availableFrames =
      static_cast<int>(outputAvailableSamples /
                       static_cast<size_t>(currentChannels));

  if (availableFrames < framesNeeded) {
    if (stats != nullptr)
      stats->paddedFrames += (framesNeeded - availableFrames);
    const size_t padCount =
        static_cast<size_t>((framesNeeded - availableFrames) * currentChannels);
    pushSilenceSamples(padCount);
  }

  if (buffer.getNumSamples() < outputSamples) {
    buffer.setSize(currentChannels, outputSamples, true, false, true);
  }

  for (int sample = 0; sample < outputSamples; ++sample) {
    for (int ch = 0; ch < currentChannels; ++ch) {
      buffer.setSample(ch, sample, popOutputSample());
    }
  }

  if (stats != nullptr) {
    stats->fifoAvailableAfterFrames =
        static_cast<int>(outputAvailableSamples /
                         static_cast<size_t>(currentChannels));
  }
}

void PitchShiftProcessor::primeLatency() {
  latencySamples = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
  if (latencySamples < 0)
    latencySamples = 0;

  if (!prepared || currentChannels <= 0) {
    outputReadPos = 0;
    outputWritePos = 0;
    outputAvailableSamples = 0;
    return;
  }

  // Desynchronize SoundTouch's internal overlap-add window phase relative to
  // other parallel instances. We pump `phaseOffsetSamples` frames of silence
  // through SoundTouch and discard the output, so its window counter advances
  // by that many input frames before real audio starts flowing.
  if (phaseOffsetSamples > 0) {
    const int chunkFrames = juce::jmax(1, preferredBlockSamples);
    const size_t chunkInputSize =
        static_cast<size_t>(chunkFrames * currentChannels);
    const size_t chunkReceiveSize =
        static_cast<size_t>(chunkFrames * currentChannels);
    if (interleavedInput.size() < chunkInputSize)
      interleavedInput.resize(chunkInputSize);
    if (receiveBuffer.size() < chunkReceiveSize)
      receiveBuffer.resize(chunkReceiveSize);
    std::fill(interleavedInput.begin(),
              interleavedInput.begin() +
                  static_cast<std::ptrdiff_t>(chunkInputSize),
              0.0f);

    int remaining = phaseOffsetSamples;
    while (remaining > 0) {
      const int pushFrames = juce::jmin(remaining, chunkFrames);
      soundtouch.putSamples(interleavedInput.data(), pushFrames);
      remaining -= pushFrames;
      // Drain whatever SoundTouch produced so its output queue doesn't
      // overflow while we're shifting phase.
      while (true) {
        const int got =
            soundtouch.receiveSamples(receiveBuffer.data(), chunkFrames);
        if (got <= 0)
          break;
      }
    }

    // Latency is unchanged by pumping silence, but re-read defensively in case
    // SoundTouch updated its internal bookkeeping.
    latencySamples = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
    if (latencySamples < 0)
      latencySamples = 0;
  }

  const size_t channelCount =
      static_cast<size_t>(juce::jmax(1, currentChannels));
  const size_t blockSamples = static_cast<size_t>(
      juce::jmax(1, preferredBlockSamples)) *
                              channelCount;
  const size_t latencySampleCount =
      static_cast<size_t>(latencySamples) * channelCount;
  const size_t minCapacity = juce::jmax(
      static_cast<size_t>(4096),
      latencySampleCount + (blockSamples * static_cast<size_t>(8)));
  ensureOutputFifoCapacity(minCapacity);

  outputReadPos = 0;
  outputWritePos = 0;
  outputAvailableSamples = 0;
  pushSilenceSamples(latencySampleCount);
}

void PitchShiftProcessor::ensureOutputFifoCapacity(size_t minSamples) {
  if (outputFifo.size() >= minSamples)
    return;

  size_t newCapacity = outputFifo.empty() ? static_cast<size_t>(4096)
                                          : outputFifo.size();
  while (newCapacity < minSamples)
    newCapacity *= 2;

  std::vector<float> newFifo(newCapacity, 0.0f);
  const size_t oldCapacity = outputFifo.size();
  if (oldCapacity > 0 && outputAvailableSamples > 0) {
    for (size_t i = 0; i < outputAvailableSamples; ++i) {
      newFifo[i] = outputFifo[(outputReadPos + i) % oldCapacity];
    }
  }

  outputFifo.swap(newFifo);
  outputReadPos = 0;
  outputWritePos = outputAvailableSamples % outputFifo.size();
}

void PitchShiftProcessor::pushOutputSamples(const float *samples,
                                            size_t sampleCount) {
  if (sampleCount == 0)
    return;

  ensureOutputFifoCapacity(outputAvailableSamples + sampleCount);
  if (outputFifo.empty())
    return;

  const size_t capacity = outputFifo.size();
  const size_t firstCopyCount =
      juce::jmin(sampleCount, capacity - outputWritePos);
  std::copy(samples, samples + firstCopyCount,
            outputFifo.begin() +
                static_cast<std::ptrdiff_t>(outputWritePos));
  const size_t remainingCount = sampleCount - firstCopyCount;
  if (remainingCount > 0) {
    std::copy(samples + firstCopyCount, samples + sampleCount,
              outputFifo.begin());
  }

  outputWritePos = (outputWritePos + sampleCount) % capacity;
  outputAvailableSamples += sampleCount;
}

void PitchShiftProcessor::pushSilenceSamples(size_t sampleCount) {
  if (sampleCount == 0)
    return;

  ensureOutputFifoCapacity(outputAvailableSamples + sampleCount);
  if (outputFifo.empty())
    return;

  const size_t capacity = outputFifo.size();
  const size_t firstFillCount =
      juce::jmin(sampleCount, capacity - outputWritePos);
  std::fill(outputFifo.begin() + static_cast<std::ptrdiff_t>(outputWritePos),
            outputFifo.begin() + static_cast<std::ptrdiff_t>(outputWritePos +
                                                             firstFillCount),
            0.0f);
  const size_t remainingCount = sampleCount - firstFillCount;
  if (remainingCount > 0) {
    std::fill(outputFifo.begin(),
              outputFifo.begin() +
                  static_cast<std::ptrdiff_t>(remainingCount),
              0.0f);
  }

  outputWritePos = (outputWritePos + sampleCount) % capacity;
  outputAvailableSamples += sampleCount;
}

float PitchShiftProcessor::popOutputSample() {
  if (outputAvailableSamples == 0 || outputFifo.empty())
    return 0.0f;

  const float value = outputFifo[outputReadPos];
  outputReadPos = (outputReadPos + 1) % outputFifo.size();
  --outputAvailableSamples;
  return value;
}
