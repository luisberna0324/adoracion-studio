#include "MetronomeEngine.h"

void MetronomeEngine::prepare(double sampleRateToUse) {
  sampleRate = sampleRateToUse;
  reset();
}

void MetronomeEngine::reset() {
  phase = 0.0;
  subPhase = 0.0;
  clickSamplesRemaining = 0;
  beatIndex = juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed)) - 1;
  subIndex = 0;
  currentClickIsMainBeat = true;
  clickGain = 0.12f;
}

void MetronomeEngine::setEnabled(bool enabledToUse) noexcept {
  enabled.store(enabledToUse, std::memory_order_relaxed);
}

bool MetronomeEngine::isEnabled() const noexcept {
  return enabled.load(std::memory_order_relaxed);
}

void MetronomeEngine::setBpm(double bpmToUse) noexcept {
  const double clamped = juce::jlimit(20.0, 300.0, bpmToUse);
  bpm.store(clamped, std::memory_order_relaxed);
}

double MetronomeEngine::getBpm() const noexcept {
  return bpm.load(std::memory_order_relaxed);
}

void MetronomeEngine::setBeatsPerBar(int beats) noexcept {
  const int clamped = juce::jmax(1, beats);
  beatsPerBar.store(clamped, std::memory_order_relaxed);
  beatIndex = clamped - 1;
}

int MetronomeEngine::getBeatsPerBar() const noexcept {
  return beatsPerBar.load(std::memory_order_relaxed);
}

void MetronomeEngine::setVolume(const float volumeToUse) noexcept {
  volume.store(juce::jlimit(0.0f, 1.0f, volumeToUse),
               std::memory_order_relaxed);
}

float MetronomeEngine::getVolume() const noexcept {
  return volume.load(std::memory_order_relaxed);
}

void MetronomeEngine::setMuted(const bool mutedToUse) noexcept {
  muted.store(mutedToUse, std::memory_order_relaxed);
}

bool MetronomeEngine::isMuted() const noexcept {
  return muted.load(std::memory_order_relaxed);
}

void MetronomeEngine::triggerImmediateClick() noexcept {
  pendingImmediateClick.store(true, std::memory_order_relaxed);
}

void MetronomeEngine::setSubdivision(int subdiv) noexcept {
  const int normalized = subdiv >= 4 ? 4 : (subdiv >= 2 ? 2 : 1);
  subdivision.store(normalized, std::memory_order_relaxed);
}

int MetronomeEngine::getSubdivision() const noexcept {
  return subdivision.load(std::memory_order_relaxed);
}

void MetronomeEngine::syncPhaseForTimeSeconds(double seconds) noexcept {
  const double bpmLocal = bpm.load(std::memory_order_relaxed);
  if (bpmLocal <= 0.0)
    return;
  const double beatPos = (juce::jmax(0.0, seconds) * bpmLocal) / 60.0;
  const int beats = juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed));
  const int wholeBeats = static_cast<int>(std::floor(beatPos));
  beatIndex = ((wholeBeats % beats) + beats) % beats;
  phase = beatPos - std::floor(beatPos);

  const int subdiv = juce::jmax(1, subdivision.load(std::memory_order_relaxed));
  const double subBeatPos = phase * static_cast<double>(subdiv);
  subIndex = static_cast<int>(std::floor(subBeatPos)) % subdiv;
  subPhase = subBeatPos - std::floor(subBeatPos);
}

void MetronomeEngine::process(juce::AudioBuffer<float> &buffer) noexcept {
  if (!enabled.load(std::memory_order_relaxed))
    return;
  const float gain = muted.load(std::memory_order_relaxed)
                         ? 0.0f
                         : juce::jlimit(0.0f, 1.0f,
                                        volume.load(std::memory_order_relaxed));
  if (gain <= 0.0001f)
    return;
  const int numSamples = buffer.getNumSamples();
  if (numSamples <= 0 || sampleRate <= 0.0)
    return;

  const double bpmLocal = bpm.load(std::memory_order_relaxed);
  const int beats =
      juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed));
  if (bpmLocal <= 0.0)
    return;

  const int subdiv = juce::jmax(1, subdivision.load(std::memory_order_relaxed));
  const double phaseInc = bpmLocal / (60.0 * sampleRate);
  const double subPhaseInc = phaseInc * static_cast<double>(subdiv);
  const int clickLength =
      juce::jmax(8, static_cast<int>(sampleRate * 0.025));
  // Main beat: 900 Hz, sub-beats: 1200 Hz (higher, thinner)
  const double mainClickFreq = 900.0;
  const double subClickFreq = 1200.0;
  const double twoPi = juce::MathConstants<double>::twoPi;
  const int numChannels = buffer.getNumChannels();

  if (pendingImmediateClick.exchange(false, std::memory_order_relaxed)) {
    clickSamplesRemaining = clickLength;
    clickGain = (beatIndex == 0) ? 0.18f : 0.12f;
    currentClickIsMainBeat = true;
  }

  for (int sample = 0; sample < numSamples; ++sample) {
    phase += phaseInc;

    // Main beat boundary
    if (phase >= 1.0) {
      phase -= 1.0;
      beatIndex = (beatIndex + 1) % beats;
      subIndex = 0;
      subPhase = 0.0;
      clickSamplesRemaining = clickLength;
      clickGain = (beatIndex == 0) ? 0.18f : 0.12f;
      currentClickIsMainBeat = true;
    } else if (subdiv > 1) {
      // Sub-beat tracking
      subPhase += subPhaseInc;
      if (subPhase >= 1.0) {
        subPhase -= 1.0;
        subIndex = (subIndex + 1) % subdiv;
        if (subIndex != 0) {
          // Sub-beat click (not the main beat which is subIndex==0)
          clickSamplesRemaining = clickLength;
          clickGain = 0.08f; // softer than main beats
          currentClickIsMainBeat = false;
        }
      }
    }

    float clickSample = 0.0f;
    if (clickSamplesRemaining > 0) {
      const int idx = clickLength - clickSamplesRemaining;
      const float env =
          std::exp(-6.0f * static_cast<float>(idx) /
                   static_cast<float>(clickLength));
      const double freq = currentClickIsMainBeat ? mainClickFreq : subClickFreq;
      clickSample = static_cast<float>(
                        std::sin(twoPi * freq *
                                 (static_cast<double>(idx) / sampleRate))) *
                    env * clickGain * gain;
      --clickSamplesRemaining;
    }

    if (clickSample == 0.0f)
      continue;

    for (int ch = 0; ch < numChannels; ++ch)
      buffer.addSample(ch, sample, clickSample);
  }
}
