#include "PitchShiftProcessor.h"

#include <algorithm>
#include <cmath>

namespace {
struct TempoQualitySettings {
  int profileId = 0;
  int sequenceMs = 82;
  int seekWindowMs = 28;
  int overlapMs = 10;
};

TempoQualitySettings tempoQualitySettingsForRatio(
    const double ratio, const PitchShiftProcessor::ContentType contentType) {
  const bool slowingDown = ratio < 0.999;

  if (contentType == PitchShiftProcessor::ContentType::Percussive) {
    // Percussive material needs tighter transient tracking to avoid
    // double-hit/flam artifacts while stretching down tempo.
    if (ratio < 0.88)
      return {3, 58, 14, 6};
    if (slowingDown)
      return {4, 50, 13, 6};
    return {5, 40, 10, 6};
  }

  if (ratio < 0.85)
    return {0, 95, 35, 12}; // Heavy slow-down melodic profile.
  if (slowingDown)
    return {1, 82, 28, 10}; // Moderate slow-down melodic profile.
  return {2, 46, 18, 8};    // Normal/speed-up melodic profile.
}
} // namespace

void PitchShiftProcessor::prepare(double sampleRate, int samplesPerBlock,
                                  int numChannels) {
  currentSampleRate = sampleRate;
  currentChannels = numChannels;
  prepared = (currentSampleRate > 0.0 && currentChannels > 0);

  interleavedInput.clear();
  dryFallback.clear();
  receiveBuffer.clear();
  outputFifo.clear();
  outputReadPos = 0;
  outputWritePos = 0;
  outputSampleCount = 0;
  pitchTransitionFramesRemaining = 0;
  pitchTransitionFramesTotal = 0;
  inputFrameRemainder = 0.0;
  maxBlockFrames = juce::jmax(0, samplesPerBlock);
  maxInputFrames = (maxBlockFrames * 2) + 8;

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
  latencySamples = juce::jmax(
      0, soundtouch.getSetting(SETTING_INITIAL_LATENCY));

  if (samplesPerBlock > 0 && currentChannels > 0) {
    interleavedInput.resize(
        static_cast<size_t>(maxInputFrames * currentChannels));
    dryFallback.resize(
        static_cast<size_t>(maxInputFrames * currentChannels));
    receiveBuffer.resize(
        static_cast<size_t>(samplesPerBlock * currentChannels * 2));
    const int fifoFrames =
        juce::jmax(samplesPerBlock * 4,
                   samplesPerBlock + latencySamples + 4096);
    outputFifo.resize(
        static_cast<size_t>(fifoFrames * currentChannels), 0.0f);
  }

  primeLatency();
}

void PitchShiftProcessor::reset() {
  soundtouch.clear();
  fifoClear();
  primeLatency();
}

void PitchShiftProcessor::setPitchSemiTones(int semitones) {
  if (pitchSemiTones == semitones)
    return;
  pitchSemiTones = semitones;
  if (prepared) {
    soundtouch.setPitchSemiTones(static_cast<float>(pitchSemiTones));
    // SoundTouch supports continuous pitch updates. Keep its queued frames and
    // overlap history so changing key does not insert a silent re-prime gap.
    refreshLatencyFromSettings();
    pitchTransitionFramesTotal =
        juce::jmax(latencySamples, maxBlockFrames * 4);
    pitchTransitionFramesRemaining = pitchTransitionFramesTotal;
  }
}

void PitchShiftProcessor::setTempoRatio(double ratio) {
  const double normalized = juce::jlimit(0.75, 2.0, ratio);
  if (juce::approximatelyEqual(tempoRatio, normalized))
    return;
  const int previousProfile =
      tempoQualitySettingsForRatio(tempoRatio, contentType).profileId;
  const int nextProfile =
      tempoQualitySettingsForRatio(normalized, contentType).profileId;
  tempoRatio = normalized;
  if (prepared) {
    configureQualitySettings();
    soundtouch.setTempo(static_cast<float>(tempoRatio));
    // Crossing between stretch profiles changes SoundTouch windowing strategy.
    // Flush pipeline so old-profile frames don't drift against other stems.
    if (previousProfile != nextProfile)
      reset();
    else
      refreshLatencyFromSettings();
  }
}

void PitchShiftProcessor::setContentType(const ContentType type) {
  if (contentType == type)
    return;
  contentType = type;
  if (prepared) {
    configureQualitySettings();
    // Windowing changes should apply immediately and without stale tails.
    reset();
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
  process(buffer, numSamples, buffer, numSamples);
}

int PitchShiftProcessor::process(const juce::AudioBuffer<float> &input,
                                 const int inputFrames,
                                 juce::AudioBuffer<float> &output,
                                 const int outputFrames) {
  if (!prepared || inputFrames <= 0 || outputFrames <= 0 ||
      inputFrames > maxInputFrames || outputFrames > maxBlockFrames ||
      input.getNumChannels() < currentChannels ||
      output.getNumChannels() < currentChannels) {
    output.clear();
    return 0;
  }

  if (pitchSemiTones == 0 && juce::approximatelyEqual(tempoRatio, 1.0)) {
    for (int channel = 0; channel < currentChannels; ++channel)
      output.copyFrom(channel, 0, input, channel, 0, outputFrames);
    return outputFrames;
  }

  for (int sample = 0; sample < inputFrames; ++sample) {
    const size_t base = static_cast<size_t>(sample * currentChannels);
    for (int channel = 0; channel < currentChannels; ++channel)
      interleavedInput[base + static_cast<size_t>(channel)] =
          input.getSample(channel, sample);
  }
  for (int sample = 0; sample < outputFrames; ++sample) {
    const int sourceSample = juce::jlimit(
        0, inputFrames - 1,
        static_cast<int>((static_cast<int64_t>(sample) * inputFrames) /
                         outputFrames));
    const size_t outputBase =
        static_cast<size_t>(sample * currentChannels);
    for (int channel = 0; channel < currentChannels; ++channel)
      dryFallback[outputBase + static_cast<size_t>(channel)] =
          input.getSample(channel, sourceSample);
  }

  soundtouch.putSamples(interleavedInput.data(),
                        static_cast<unsigned int>(inputFrames));
  while (fifoFramesAvailable() < outputFrames) {
    const int receiveCapacity = static_cast<int>(
        receiveBuffer.size() / static_cast<size_t>(currentChannels));
    const int received = static_cast<int>(soundtouch.receiveSamples(
        receiveBuffer.data(), static_cast<unsigned int>(receiveCapacity)));
    if (received <= 0)
      break;
    fifoPush(receiveBuffer.data(), received);
  }

  const int popped = fifoPop(interleavedInput.data(), outputFrames);
  for (int sample = 0; sample < outputFrames; ++sample) {
    const size_t base = static_cast<size_t>(sample * currentChannels);
    float processedMix = 1.0f;
    if (pitchTransitionFramesRemaining > 0 &&
        pitchTransitionFramesTotal > 0) {
      processedMix =
          1.0f -
          (static_cast<float>(pitchTransitionFramesRemaining) /
           static_cast<float>(pitchTransitionFramesTotal));
      --pitchTransitionFramesRemaining;
    }
    for (int channel = 0; channel < currentChannels; ++channel) {
      const size_t index = base + static_cast<size_t>(channel);
      const float dry = dryFallback[index];
      const float processed =
          sample < popped ? interleavedInput[index] : dry;
      output.setSample(channel, sample,
                       dry + ((processed - dry) * processedMix));
    }
  }
  return outputFrames;
}

int PitchShiftProcessor::requiredInputFrames(
    const int outputFrames) noexcept {
  if (outputFrames <= 0)
    return 0;
  const double exact =
      (static_cast<double>(outputFrames) * tempoRatio) +
      inputFrameRemainder;
  const int frames = static_cast<int>(std::floor(exact));
  inputFrameRemainder = exact - static_cast<double>(frames);
  return juce::jlimit(1, maxInputFrames, frames);
}

void PitchShiftProcessor::primeLatency() {
  latencySamples = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
  if (latencySamples < 0)
    latencySamples = 0;
  fifoClear();
  fifoPushSilence(latencySamples);
}

int PitchShiftProcessor::fifoFramesAvailable() const noexcept {
  if (currentChannels <= 0)
    return 0;
  return static_cast<int>(
      outputSampleCount / static_cast<size_t>(currentChannels));
}

void PitchShiftProcessor::fifoClear() noexcept {
  outputReadPos = 0;
  outputWritePos = 0;
  outputSampleCount = 0;
}

void PitchShiftProcessor::fifoPush(const float *samples,
                                   const int frames) noexcept {
  if (samples == nullptr || frames <= 0 || currentChannels <= 0 ||
      outputFifo.empty()) {
    return;
  }
  const size_t sampleCount =
      static_cast<size_t>(frames * currentChannels);
  for (size_t i = 0; i < sampleCount; ++i) {
    if (outputSampleCount >= outputFifo.size()) {
      outputReadPos = (outputReadPos + 1) % outputFifo.size();
      --outputSampleCount;
    }
    outputFifo[outputWritePos] = samples[i];
    outputWritePos = (outputWritePos + 1) % outputFifo.size();
    ++outputSampleCount;
  }
}

int PitchShiftProcessor::fifoPop(float *destination,
                                 const int frames) noexcept {
  if (destination == nullptr || frames <= 0 || currentChannels <= 0 ||
      outputFifo.empty()) {
    return 0;
  }
  const int available = fifoFramesAvailable();
  const int framesToPop = juce::jmin(frames, available);
  const size_t samplesToPop =
      static_cast<size_t>(framesToPop * currentChannels);
  for (size_t i = 0; i < samplesToPop; ++i) {
    destination[i] = outputFifo[outputReadPos];
    outputReadPos = (outputReadPos + 1) % outputFifo.size();
  }
  outputSampleCount -= samplesToPop;
  return framesToPop;
}

void PitchShiftProcessor::fifoPushSilence(const int frames) noexcept {
  if (frames <= 0 || currentChannels <= 0 || outputFifo.empty())
    return;
  const size_t sampleCount =
      static_cast<size_t>(frames * currentChannels);
  for (size_t i = 0; i < sampleCount; ++i) {
    if (outputSampleCount >= outputFifo.size())
      break;
    outputFifo[outputWritePos] = 0.0f;
    outputWritePos = (outputWritePos + 1) % outputFifo.size();
    ++outputSampleCount;
  }
}

void PitchShiftProcessor::configureQualitySettings() {
  const auto settings = tempoQualitySettingsForRatio(tempoRatio, contentType);

  // Favor quality over CPU while stretching down tempo in live playback.
  soundtouch.setSetting(SETTING_USE_QUICKSEEK, 0);
  soundtouch.setSetting(SETTING_SEQUENCE_MS, settings.sequenceMs);
  soundtouch.setSetting(SETTING_SEEKWINDOW_MS, settings.seekWindowMs);
  soundtouch.setSetting(SETTING_OVERLAP_MS, settings.overlapMs);
}

void PitchShiftProcessor::refreshLatencyFromSettings() {
  int updatedLatency = soundtouch.getSetting(SETTING_INITIAL_LATENCY);
  if (updatedLatency < 0)
    updatedLatency = 0;
  if (updatedLatency == latencySamples)
    return;

  latencySamples = updatedLatency;
  soundtouch.clear();
  fifoClear();
  fifoPushSilence(latencySamples);
}
