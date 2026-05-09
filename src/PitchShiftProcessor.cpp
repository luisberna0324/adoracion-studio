#include "PitchShiftProcessor.h"

#include <algorithm>

void PitchShiftProcessor::prepare(double sampleRate, int samplesPerBlock,
                                  int numChannels) {
  currentSampleRate = sampleRate;
  currentChannels = numChannels;
  prepared = (currentSampleRate > 0.0 && currentChannels > 0);

  interleavedInput.clear();
  receiveBuffer.clear();
  outputFifo.clear();
  outputReadPos = 0;

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
  configureQualitySettings();
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
  outputFifo.clear();
  outputReadPos = 0;
  primeLatency();
}

void PitchShiftProcessor::setPitchSemiTones(int semitones) {
  if (pitchSemiTones == semitones)
    return;
  pitchSemiTones = semitones;
  if (prepared) {
    soundtouch.setPitchSemiTones(static_cast<float>(pitchSemiTones));
    // Pitch changes should feel immediate to the user. Flush time-domain
    // history so old-pitch frames don't continue leaking after a key change.
    reset();
  }
}

void PitchShiftProcessor::setTempoRatio(double ratio) {
  const double normalized = juce::jlimit(0.75, 2.0, ratio);
  if (juce::approximatelyEqual(tempoRatio, normalized))
    return;
  tempoRatio = normalized;
  if (prepared) {
    configureQualitySettings();
    soundtouch.setTempo(static_cast<float>(tempoRatio));
    refreshLatencyFromSettings();
  }
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
  if (!prepared || currentChannels <= 0)
    return;
  if (pitchSemiTones == 0 && juce::approximatelyEqual(tempoRatio, 1.0))
    return;

  const int numSamples = buffer.getNumSamples();
  if (numSamples <= 0)
    return;

  if (buffer.getNumChannels() != currentChannels) {
    prepare(currentSampleRate, numSamples, buffer.getNumChannels());
  }

  const size_t requiredInputSize =
      static_cast<size_t>(numSamples * currentChannels);
  if (interleavedInput.size() < requiredInputSize)
    interleavedInput.resize(requiredInputSize);

  for (int sample = 0; sample < numSamples; ++sample) {
    const size_t base = static_cast<size_t>(sample * currentChannels);
    for (int ch = 0; ch < currentChannels; ++ch)
      interleavedInput[base + static_cast<size_t>(ch)] =
          buffer.getSample(ch, sample);
  }

  soundtouch.putSamples(interleavedInput.data(), numSamples);

  const int framesNeeded = numSamples;
  while (static_cast<int>((outputFifo.size() - outputReadPos) /
                          static_cast<size_t>(currentChannels)) <
         framesNeeded) {
    const int maxFrames =
        std::max(framesNeeded,
                 static_cast<int>(receiveBuffer.size() /
                                  static_cast<size_t>(currentChannels)));
    if (receiveBuffer.size() <
        static_cast<size_t>(maxFrames * currentChannels)) {
      receiveBuffer.resize(
          static_cast<size_t>(maxFrames * currentChannels));
    }

    const int received = soundtouch.receiveSamples(receiveBuffer.data(),
                                                   maxFrames);
    if (received <= 0)
      break;

    const size_t appendCount =
        static_cast<size_t>(received * currentChannels);
    outputFifo.insert(outputFifo.end(), receiveBuffer.begin(),
                      receiveBuffer.begin() + appendCount);
  }

  const int availableFrames =
      static_cast<int>((outputFifo.size() - outputReadPos) /
                       static_cast<size_t>(currentChannels));

  if (availableFrames < framesNeeded) {
    const size_t padCount =
        static_cast<size_t>((framesNeeded - availableFrames) * currentChannels);
    outputFifo.insert(outputFifo.end(), padCount, 0.0f);
  }

  for (int sample = 0; sample < numSamples; ++sample) {
    const size_t base = outputReadPos +
                        static_cast<size_t>(sample * currentChannels);
    for (int ch = 0; ch < currentChannels; ++ch) {
      buffer.setSample(ch, sample,
                       outputFifo[base + static_cast<size_t>(ch)]);
    }
  }

  outputReadPos += requiredInputSize;
  if (outputReadPos > outputFifo.size() / 2) {
    outputFifo.erase(outputFifo.begin(), outputFifo.begin() +
                                            static_cast<std::ptrdiff_t>(
                                                outputReadPos));
    outputReadPos = 0;
  }
}

void PitchShiftProcessor::primeLatency() {
  latencySamples = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
  if (latencySamples < 0)
    latencySamples = 0;
  outputFifo.clear();
  outputReadPos = 0;
  if (latencySamples > 0 && currentChannels > 0) {
    outputFifo.resize(static_cast<size_t>(latencySamples * currentChannels),
                      0.0f);
  }
}

void PitchShiftProcessor::configureQualitySettings() {
  const bool slowingDown = tempoRatio < 0.999;
  const bool heavyStretch = tempoRatio < 0.85;
  const int sequenceMs = heavyStretch ? 95 : (slowingDown ? 82 : 46);
  const int seekWindowMs = heavyStretch ? 35 : (slowingDown ? 28 : 18);
  const int overlapMs = heavyStretch ? 12 : (slowingDown ? 10 : 8);

  // Favor quality over CPU while stretching down tempo in live playback.
  soundtouch.setSetting(SETTING_USE_QUICKSEEK, 0);
  soundtouch.setSetting(SETTING_SEQUENCE_MS, sequenceMs);
  soundtouch.setSetting(SETTING_SEEKWINDOW_MS, seekWindowMs);
  soundtouch.setSetting(SETTING_OVERLAP_MS, overlapMs);
}

void PitchShiftProcessor::refreshLatencyFromSettings() {
  int updatedLatency = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
  if (updatedLatency < 0)
    updatedLatency = 0;
  if (updatedLatency == latencySamples)
    return;

  if (currentChannels <= 0) {
    latencySamples = updatedLatency;
    return;
  }

  const int deltaSamples = updatedLatency - latencySamples;
  latencySamples = updatedLatency;

  if (deltaSamples > 0) {
    const size_t padCount =
        static_cast<size_t>(deltaSamples * currentChannels);
    outputFifo.insert(
        outputFifo.begin() + static_cast<std::ptrdiff_t>(outputReadPos),
        padCount, 0.0f);
    return;
  }

  const size_t availableToSkip =
      outputFifo.size() > outputReadPos ? (outputFifo.size() - outputReadPos)
                                        : 0u;
  const size_t skipCount =
      std::min(static_cast<size_t>((-deltaSamples) * currentChannels),
               availableToSkip);
  outputReadPos += skipCount;
  if (outputReadPos > outputFifo.size() / 2) {
    outputFifo.erase(outputFifo.begin(), outputFifo.begin() +
                                            static_cast<std::ptrdiff_t>(
                                                outputReadPos));
    outputReadPos = 0;
  }
}
