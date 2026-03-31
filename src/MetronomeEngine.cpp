#include "MetronomeEngine.h"

void MetronomeEngine::prepare(double sampleRateToUse) {
  sampleRate = sampleRateToUse;
  reset();
}

void MetronomeEngine::reset() {
  phase = 0.0;
  clickSamplesRemaining = 0;
  beatIndex = juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed)) - 1;
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

void MetronomeEngine::syncPhaseForTimeSeconds(double seconds) noexcept {
  const double bpmLocal = bpm.load(std::memory_order_relaxed);
  if (bpmLocal <= 0.0)
    return;
  const double beatPos = (seconds * bpmLocal) / 60.0;
  phase = beatPos - std::floor(beatPos);
}

void MetronomeEngine::process(juce::AudioBuffer<float> &buffer) noexcept {
  if (!enabled.load(std::memory_order_relaxed))
    return;
  const int numSamples = buffer.getNumSamples();
  if (numSamples <= 0 || sampleRate <= 0.0)
    return;

  const double bpmLocal = bpm.load(std::memory_order_relaxed);
  const int beats =
      juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed));
  if (bpmLocal <= 0.0)
    return;

  const double phaseInc = bpmLocal / (60.0 * sampleRate);
  const int clickLength =
      juce::jmax(8, static_cast<int>(sampleRate * 0.025));
  const double clickFreq = 900.0;
  const double twoPi = juce::MathConstants<double>::twoPi;
  const int numChannels = buffer.getNumChannels();

  for (int sample = 0; sample < numSamples; ++sample) {
    phase += phaseInc;
    if (phase >= 1.0) {
      phase -= 1.0;
      clickSamplesRemaining = clickLength;
      beatIndex = (beatIndex + 1) % beats;
      clickGain = (beatIndex == 0) ? 0.18f : 0.12f;
    }

    float clickSample = 0.0f;
    if (clickSamplesRemaining > 0) {
      const int idx = clickLength - clickSamplesRemaining;
      const float env =
          std::exp(-6.0f * static_cast<float>(idx) /
                   static_cast<float>(clickLength));
      clickSample = static_cast<float>(
                        std::sin(twoPi * clickFreq *
                                 (static_cast<double>(idx) / sampleRate))) *
                    env * clickGain;
      --clickSamplesRemaining;
    }

    if (clickSample == 0.0f)
      continue;

    for (int ch = 0; ch < numChannels; ++ch)
      buffer.addSample(ch, sample, clickSample);
  }
}
