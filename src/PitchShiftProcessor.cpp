#include "PitchShiftProcessor.h"

#include <algorithm>
#include <atomic>

namespace {
constexpr double kMinTempoRatio = 0.80;
constexpr double kMaxTempoRatio = 1.25;
std::atomic<bool> gCpuGuardEnabled{false};
std::atomic<uint32_t> gQualityVersion{1};

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

void PitchShiftProcessor::setGlobalCpuGuardEnabled(const bool enabled) noexcept {
  const bool previous = gCpuGuardEnabled.exchange(enabled, std::memory_order_relaxed);
  if (previous == enabled)
    return;
  gQualityVersion.fetch_add(1, std::memory_order_relaxed);
}

bool PitchShiftProcessor::isGlobalCpuGuardEnabled() noexcept {
  return gCpuGuardEnabled.load(std::memory_order_relaxed);
}

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
  appliedQualityVersion = gQualityVersion.load(std::memory_order_relaxed);
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
  ensureQualitySettingsCurrent();
  const double normalized = juce::jlimit(kMinTempoRatio, kMaxTempoRatio, ratio);
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
  ensureQualitySettingsCurrent();
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
  ensureQualitySettingsCurrent();
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
  const auto settings = tempoQualitySettingsForRatio(tempoRatio, contentType);
  const bool cpuGuard = isGlobalCpuGuardEnabled();

  int sequenceMs = settings.sequenceMs;
  int seekWindowMs = settings.seekWindowMs;
  int overlapMs = settings.overlapMs;
  int quickSeek = 0;

  if (cpuGuard) {
    // CPU guard trades some quality for lower runtime spikes during heavy
    // stretch operations.
    quickSeek = 1;
    sequenceMs = juce::jmax(28, sequenceMs - (contentType == ContentType::Percussive ? 12 : 24));
    seekWindowMs = juce::jmax(8, seekWindowMs - (contentType == ContentType::Percussive ? 4 : 10));
    overlapMs = juce::jmax(4, overlapMs - 2);
  }

  // Favor quality over CPU while stretching down tempo in live playback.
  soundtouch.setSetting(SETTING_USE_QUICKSEEK, quickSeek);
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

void PitchShiftProcessor::ensureQualitySettingsCurrent() {
  if (!prepared)
    return;
  const uint32_t globalVersion = gQualityVersion.load(std::memory_order_relaxed);
  if (globalVersion == appliedQualityVersion)
    return;
  appliedQualityVersion = globalVersion;
  configureQualitySettings();
  refreshLatencyFromSettings();
}
