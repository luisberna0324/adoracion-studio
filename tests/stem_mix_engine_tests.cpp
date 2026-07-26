#include "../src/StemMixEngine.h"
#include "../src/audio/SimpleDelayLine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr int kImpulseFrame = (24 * kBlockSize) + 73;
constexpr int kEarlyImpulseFrame = 73;
constexpr int kRenderBlocks = 52;

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

class ImpulseAudioFormatReader final : public juce::AudioFormatReader {
public:
  explicit ImpulseAudioFormatReader(const juce::int64 impulseFrame)
      : AudioFormatReader(nullptr, "test impulse"),
        impulsePosition(impulseFrame) {
    sampleRate = kSampleRate;
    bitsPerSample = 32;
    lengthInSamples = kRenderBlocks * kBlockSize;
    numChannels = 2;
    usesFloatingPointData = true;
  }

  bool readSamples(int *const *destChannels, const int numDestChannels,
                   const int startOffsetInDestBuffer,
                   const juce::int64 startSampleInFile,
                   const int numSamples) override {
    for (int channel = 0; channel < numDestChannels; ++channel) {
      if (destChannels[channel] == nullptr)
        continue;

      auto *destination =
          reinterpret_cast<float *>(destChannels[channel]) +
          startOffsetInDestBuffer;
      std::fill(destination, destination + numSamples, 0.0f);
      if (impulsePosition >= startSampleInFile &&
          impulsePosition < startSampleInFile + numSamples) {
        destination[static_cast<int>(impulsePosition - startSampleInFile)] =
            1.0f;
      }
    }
    return true;
  }

private:
  juce::int64 impulsePosition = 0;
};

class RampAudioFormatReader final : public juce::AudioFormatReader {
public:
  RampAudioFormatReader() : AudioFormatReader(nullptr, "test ramp") {
    sampleRate = kSampleRate;
    bitsPerSample = 32;
    lengthInSamples = 160 * kBlockSize;
    numChannels = 2;
    usesFloatingPointData = true;
  }

  bool readSamples(int *const *destChannels, const int numDestChannels,
                   const int startOffsetInDestBuffer,
                   const juce::int64 startSampleInFile,
                   const int numSamples) override {
    for (int channel = 0; channel < numDestChannels; ++channel) {
      if (destChannels[channel] == nullptr)
        continue;

      auto *destination =
          reinterpret_cast<float *>(destChannels[channel]) +
          startOffsetInDestBuffer;
      for (int sample = 0; sample < numSamples; ++sample) {
        destination[sample] =
            static_cast<float>(startSampleInFile + sample) * kSlope;
      }
    }
    return true;
  }

  static constexpr float kSlope = 1.0e-5f;
};

enum class RenderGroup { Melodic, Percussion, Guide };

struct RenderResult {
  std::vector<float> samples;
  int outputLatencySamples = 0;
};

RenderResult renderImpulse(const RenderGroup group,
                           const int impulseFrame = kImpulseFrame,
                           const bool resetBeforeRender = false) {
  juce::TimeSliceThread unusedReadThread("stem-mix-test-read");
  std::vector<std::unique_ptr<StemTrack>> stems;
  stems.push_back(std::make_unique<StemTrack>(
      std::make_unique<ImpulseAudioFormatReader>(impulseFrame),
      unusedReadThread, kSampleRate, 32768, false));
  stems.front()->prepareToPlay((kBlockSize * 2) + 8, kSampleRate);

  StemMixEngine engine;
  engine.prepare(kSampleRate, kBlockSize, 2);
  engine.setPitchSemiTones(3);
  engine.setTempoRatio(1.0);
  if (resetBeforeRender)
    engine.reset();

  std::array<std::atomic<bool>, 1> guideStates;
  std::array<std::atomic<bool>, 1> pitchStates;
  guideStates[0].store(group == RenderGroup::Guide,
                       std::memory_order_relaxed);
  pitchStates[0].store(group == RenderGroup::Melodic,
                       std::memory_order_relaxed);

  RenderResult result;
  result.samples.reserve(kRenderBlocks * kBlockSize);
  juce::AudioBuffer<float> output(2, kBlockSize);
  for (int block = 0; block < kRenderBlocks; ++block) {
    const int inputFrames = engine.requiredInputFrames(kBlockSize);
    engine.mixStems(stems, guideStates.data(), pitchStates.data(), 1,
                    inputFrames, 2);
    engine.renderToOutput(output, true, inputFrames, 1.0f, 1.0f, 1.0f,
                          1.0f);
    for (int sample = 0; sample < kBlockSize; ++sample)
      result.samples.push_back(output.getSample(0, sample));
  }
  result.outputLatencySamples = engine.getOutputLatencySamples();
  stems.front()->releaseResources();
  return result;
}

int peakIndex(const std::vector<float> &samples) {
  const auto peak = std::max_element(
      samples.begin(), samples.end(),
      [](const float lhs, const float rhs) {
        return std::abs(lhs) < std::abs(rhs);
      });
  if (peak == samples.end() || std::abs(*peak) < 1.0e-5f)
    return -1;
  return static_cast<int>(std::distance(samples.begin(), peak));
}
} // namespace

int main() {
  {
    StemMixEngine lifecycle;
    lifecycle.prepare(kSampleRate, kBlockSize, 2);
    lifecycle.reset();
    expect(lifecycle.servicePendingProcessorRebuilds() == 3,
           "background service rebuilds all retired grouped voices");
    lifecycle.reset();
    expect(lifecycle.getTimeProcessorResetMissCount() == 0,
           "a serviced spare handles the next grouped reset");
  }

  {
    juce::TimeSliceThread unusedReadThread("blocked-mix-test-read");
    std::vector<std::unique_ptr<StemTrack>> stems;
    stems.push_back(std::make_unique<StemTrack>(
        std::make_unique<ImpulseAudioFormatReader>(kEarlyImpulseFrame),
        unusedReadThread, kSampleRate, 32768, false));
    stems.front()->prepareToPlay(kBlockSize, kSampleRate);
    std::array<std::atomic<bool>, 1> guideStates;
    std::array<std::atomic<bool>, 1> pitchStates;
    guideStates[0].store(false, std::memory_order_relaxed);
    pitchStates[0].store(true, std::memory_order_relaxed);

    StemMixEngine blockedMix;
    blockedMix.prepare(kSampleRate, kBlockSize, 2);
    blockedMix.setPitchSemiTones(3);
    blockedMix.reset();
    blockedMix.reset();
    expect(blockedMix.getTimeProcessorResetMissCount() == 3,
           "a second grouped reset reports every missing spare");

    juce::AudioBuffer<float> output(2, kBlockSize);
    const int blockedInputFrames =
        blockedMix.requiredInputFrames(kBlockSize);
    blockedMix.mixStems(stems, guideStates.data(), pitchStates.data(), 1,
                        blockedInputFrames, 2);
    blockedMix.renderToOutput(output, true, blockedInputFrames, 1.0f, 1.0f,
                              1.0f, 1.0f);
    expect(blockedMix.wasLastRenderBlocked(),
           "grouped render blocks as one unit without clean voices");
    expect(output.getMagnitude(0, 0, kBlockSize) == 0.0f,
           "blocked grouped render cannot leak a partial stem group");

    expect(blockedMix.servicePendingProcessorRebuilds() == 3,
           "service restores every grouped replacement voice");
    const int recoveredInputFrames =
        blockedMix.requiredInputFrames(kBlockSize);
    blockedMix.mixStems(stems, guideStates.data(), pitchStates.data(), 1,
                        recoveredInputFrames, 2);
    blockedMix.renderToOutput(output, true, recoveredInputFrames, 1.0f, 1.0f,
                              1.0f, 1.0f);
    expect(!blockedMix.wasLastRenderBlocked(),
           "grouped render resumes only after preflight resolves reset");
  }

  {
    constexpr int preRollBlocks = 130;
    constexpr int transitionSamples =
        SimpleDelayLine::kDelayTransitionSamples;
    juce::TimeSliceThread unusedReadThread("delay-transition-test-read");
    std::vector<std::unique_ptr<StemTrack>> stems;
    stems.push_back(std::make_unique<StemTrack>(
        std::make_unique<RampAudioFormatReader>(), unusedReadThread,
        kSampleRate, 32768, false));
    stems.front()->prepareToPlay((kBlockSize * 2) + 8, kSampleRate);

    std::array<std::atomic<bool>, 1> guideStates;
    std::array<std::atomic<bool>, 1> pitchStates;
    guideStates[0].store(true, std::memory_order_relaxed);
    pitchStates[0].store(false, std::memory_order_relaxed);

    StemMixEngine transitioningMix;
    transitioningMix.prepare(kSampleRate, kBlockSize, 2);
    juce::AudioBuffer<float> output(2, kBlockSize);
    for (int block = 0; block < preRollBlocks; ++block) {
      const int inputFrames =
          transitioningMix.requiredInputFrames(kBlockSize);
      transitioningMix.mixStems(stems, guideStates.data(), pitchStates.data(),
                                1, inputFrames, 2);
      transitioningMix.renderToOutput(output, false, inputFrames, 1.0f, 1.0f,
                                      1.0f, 1.0f);
    }

    const auto delayIncreaseSourceStart =
        stems.front()->getNextReadPosition();
    transitioningMix.setPitchSemiTones(3);
    const int pitchedInputFrames =
        transitioningMix.requiredInputFrames(kBlockSize);
    transitioningMix.mixStems(stems, guideStates.data(), pitchStates.data(), 1,
                              pitchedInputFrames, 2);
    transitioningMix.renderToOutput(output, true, pitchedInputFrames, 1.0f,
                                    1.0f, 1.0f, 1.0f);

    const int latency = transitioningMix.getOutputLatencySamples();
    expect(latency > 0,
           "guide transition test obtains a positive pitch latency");
    const auto expectedRampSample = [](const juce::int64 sourceFrame) {
      return static_cast<float>(sourceFrame) * RampAudioFormatReader::kSlope;
    };
    expect(std::abs(output.getSample(0, 0) -
                    expectedRampSample(delayIncreaseSourceStart)) <
               5.0e-5f,
           "grouped guide delay increase starts without a timeline jump");
    expect(std::abs(
               output.getSample(0, transitionSamples - 1) -
               expectedRampSample(delayIncreaseSourceStart +
                                  transitionSamples - 1 - latency)) <
               5.0e-5f,
           "grouped guide delay increase reaches the compensated tap");
    expect(std::abs(output.getSample(0, transitionSamples) -
                    expectedRampSample(delayIncreaseSourceStart +
                                       transitionSamples - latency)) <
               5.0e-5f,
           "grouped guide delay stays compensated after its transition");

    const auto delayDecreaseSourceStart =
        stems.front()->getNextReadPosition();
    transitioningMix.setPitchSemiTones(0);
    const int directInputFrames =
        transitioningMix.requiredInputFrames(kBlockSize);
    transitioningMix.mixStems(stems, guideStates.data(), pitchStates.data(), 1,
                              directInputFrames, 2);
    transitioningMix.renderToOutput(output, false, directInputFrames, 1.0f,
                                    1.0f, 1.0f, 1.0f);
    expect(std::abs(output.getSample(0, 0) -
                    expectedRampSample(delayDecreaseSourceStart - latency)) <
               5.0e-5f,
           "grouped guide delay decrease starts from the compensated tap");
    expect(std::abs(
               output.getSample(0, transitionSamples - 1) -
               expectedRampSample(delayDecreaseSourceStart +
                                  transitionSamples - 1)) <
               5.0e-5f,
           "grouped guide delay decrease reaches the direct tap");
    expect(std::abs(output.getSample(0, transitionSamples) -
                    expectedRampSample(delayDecreaseSourceStart +
                                       transitionSamples)) <
               5.0e-5f,
           "grouped guide delay stays direct after its transition");
    stems.front()->releaseResources();
  }

  const RenderResult melodic = renderImpulse(RenderGroup::Melodic);
  const RenderResult percussion = renderImpulse(RenderGroup::Percussion);
  const RenderResult guide = renderImpulse(RenderGroup::Guide);

  const int melodicPeak = peakIndex(melodic.samples);
  const int percussionPeak = peakIndex(percussion.samples);
  const int guidePeak = peakIndex(guide.samples);
  expect(melodic.outputLatencySamples > 0,
         "pitch-only render publishes a positive output latency");
  expect(melodicPeak >= 0, "melodic pitch path renders the test impulse");
  expect(percussionPeak >= 0, "percussion path renders the test impulse");
  expect(guidePeak >= 0, "guide path renders the test impulse");

  // Bungee can spread a single impulse across a short grain. The unpitched
  // groups should nevertheless land in the same block as its strongest
  // melodic transient, rather than arriving one full Bungee latency early.
  expect(std::abs(percussionPeak - melodicPeak) <= kBlockSize,
         "pitch-only percussion is aligned with the melodic group");
  expect(std::abs(guidePeak - melodicPeak) <= kBlockSize,
         "pitch-only guide is aligned with the melodic group");

  const RenderResult earlyMelodic =
      renderImpulse(RenderGroup::Melodic, kEarlyImpulseFrame, true);
  const RenderResult earlyPercussion =
      renderImpulse(RenderGroup::Percussion, kEarlyImpulseFrame, true);
  const RenderResult earlyGuide =
      renderImpulse(RenderGroup::Guide, kEarlyImpulseFrame, true);
  const int earlyMelodicPeak = peakIndex(earlyMelodic.samples);
  const int earlyPercussionPeak = peakIndex(earlyPercussion.samples);
  const int earlyGuidePeak = peakIndex(earlyGuide.samples);
  expect(earlyMelodicPeak >= 0,
         "melodic path renders an impulse immediately after reset");
  expect(earlyPercussionPeak >= 0,
         "percussion path renders an impulse immediately after reset");
  expect(earlyGuidePeak >= 0,
         "guide path renders an impulse immediately after reset");
  expect(std::abs(earlyPercussionPeak - earlyMelodicPeak) <= kBlockSize,
         "clean percussion delay is complete from the first sample");
  expect(std::abs(earlyGuidePeak - earlyMelodicPeak) <= kBlockSize,
         "clean guide delay is complete from the first sample");

  std::cout << "stem mix engine tests passed\n";
  return 0;
}
