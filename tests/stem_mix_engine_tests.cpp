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
  std::vector<float> rightSamples;
  int outputLatencySamples = 0;
};

RenderResult renderImpulse(const RenderGroup group,
                           const int impulseFrame = kImpulseFrame,
                           const bool resetBeforeRender = false,
                           const float fadeStart = 1.0f,
                           const float fadeEnd = 1.0f,
                           const int outputChannel = 0,
                           const double tempo = 1.0,
                           const float rehearsalGain = 1.0f) {
  juce::TimeSliceThread unusedReadThread("stem-mix-test-read");
  std::vector<std::unique_ptr<StemTrack>> stems;
  stems.push_back(std::make_unique<StemTrack>(
      std::make_unique<ImpulseAudioFormatReader>(impulseFrame),
      unusedReadThread, kSampleRate, 32768, false));
  stems.front()->setRehearsalGain(rehearsalGain);
  stems.front()->prepareToPlay((kBlockSize * 2) + 8, kSampleRate);

  StemMixEngine engine;
  engine.prepare(kSampleRate, kBlockSize, 2);
  engine.setPitchSemiTones(3);
  engine.setTempoRatio(tempo);
  if (resetBeforeRender)
    engine.reset();

  std::array<std::atomic<bool>, 1> guideStates;
  std::array<std::atomic<bool>, 1> pitchStates;
  std::array<std::atomic<int>, 1> routingChannels{};
  std::array<std::atomic<int>, 1> routingWidths{};
  routingChannels[0].store(outputChannel);
  routingWidths[0].store(1);
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
                    inputFrames, 2,
                    outputChannel > 0 ? routingChannels.data() : nullptr,
                    outputChannel > 0 ? routingWidths.data() : nullptr);
    engine.renderToOutput(output, true, inputFrames, kBlockSize, fadeStart, fadeEnd, 1.0f,
                          1.0f);
    for (int sample = 0; sample < kBlockSize; ++sample) {
      result.samples.push_back(output.getSample(0, sample));
      result.rightSamples.push_back(output.getSample(1, sample));
    }
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
  for (auto group : {RenderGroup::Melodic, RenderGroup::Percussion, RenderGroup::Guide}) {
    for (int channel : {1, 2}) {
      const auto routed = renderImpulse(group, kImpulseFrame, false, 1.0f, 1.0f,
                                         channel, 125.0 / 127.0);
      const auto &audible = channel == 1 ? routed.samples : routed.rightSamples;
      const auto &silent = channel == 1 ? routed.rightSamples : routed.samples;
      expect(peakIndex(audible) >= 0, "routed group remains audible with pitch and tempo");
      expect(peakIndex(silent) == -1, "grouped time stretch cannot leak mono routing to the other channel");
    }
  }

  {
    // Regression: an 8192-frame scratch buffer used to make the Android
    // grouped route stretch a short callback to 8192 and discard its tail.
    // Compare both storage sizes across changing callback sizes with 23 stems.
    constexpr int capacity = 8192;
    constexpr int stemCount = 23;
    juce::TimeSliceThread unusedReadThread("callback-size-test");
    std::array<std::atomic<bool>, stemCount> guideStates{};
    std::array<std::atomic<bool>, stemCount> pitchStates{};
    std::array<std::vector<std::unique_ptr<StemTrack>>, 2> stems;
    std::array<StemMixEngine, 2> engines;
    for (int stem = 0; stem < stemCount; ++stem) {
      guideStates[stem].store(stem % 3 == 2);
      pitchStates[stem].store(stem % 3 == 0);
      for (auto &tracks : stems) {
        auto track = std::make_unique<StemTrack>(
            std::make_unique<ImpulseAudioFormatReader>(kImpulseFrame + stem * 17),
            unusedReadThread, kSampleRate, 32768, false);
        track->prepareToPlay(capacity, kSampleRate);
        track->setVolume(0.02f);
        tracks.push_back(std::move(track));
      }
    }
    for (auto &engine : engines) {
      engine.prepare(kSampleRate, capacity, 2);
      engine.setPitchSemiTones(-2);
      engine.setTempoRatio(125.0 / 127.0);
    }
    juce::AudioBuffer<float> scratch(2, capacity);
    constexpr std::array<int, 4> sizes{96, 512, 1920, 960};
    float peak = 0.0f;
    for (int block = 0; block < 48; ++block) {
      const int frames = sizes[static_cast<size_t>(block) % sizes.size()];
      juce::AudioBuffer<float> exact(2, frames);
      for (int channel = 0; channel < 2; ++channel)
        std::fill_n(scratch.getWritePointer(channel), capacity, -9.0f);
      for (int path = 0; path < 2; ++path) {
        auto &engine = engines[path];
        const int input = engine.requiredInputFrames(frames);
        engine.mixStems(stems[path], guideStates.data(), pitchStates.data(),
                        stemCount, input, 2);
        engine.renderToOutput(path == 0 ? exact : scratch, true, input, frames,
                              1.0f, 1.0f, 1.0f, 1.0f);
        expect(!engine.wasLastRenderBlocked(), "short callback renders all groups");
      }
      for (int channel = 0; channel < 2; ++channel) {
        for (int sample = 0; sample < frames; ++sample) {
          expect(std::abs(exact.getSample(channel, sample) -
                          scratch.getSample(channel, sample)) < 1.0e-6f,
                 "scratch capacity cannot change audio or tempo");
        }
        for (int sample = frames; sample < capacity; ++sample)
          expect(scratch.getSample(channel, sample) == -9.0f,
                 "grouped renderer must leave unused scratch tail untouched");
      }
      peak = std::max(peak, exact.getMagnitude(0, frames));
    }
    expect(peak > 0.001f, "23-stem regression renders audible signal, not only silence");
    for (auto &tracks : stems)
      for (auto &track : tracks)
        track->releaseResources();
  }

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
    blockedMix.renderToOutput(output, true, blockedInputFrames, kBlockSize, 1.0f, 1.0f,
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
    blockedMix.renderToOutput(output, true, recoveredInputFrames, kBlockSize, 1.0f, 1.0f,
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
      transitioningMix.renderToOutput(output, false, inputFrames, kBlockSize, 1.0f, 1.0f,
                                      1.0f, 1.0f);
    }

    const auto delayIncreaseSourceStart =
        stems.front()->getNextReadPosition();
    transitioningMix.setPitchSemiTones(3);
    const int pitchedInputFrames =
        transitioningMix.requiredInputFrames(kBlockSize);
    transitioningMix.mixStems(stems, guideStates.data(), pitchStates.data(), 1,
                              pitchedInputFrames, 2);
    transitioningMix.renderToOutput(output, true, pitchedInputFrames, kBlockSize, 1.0f,
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
    transitioningMix.renderToOutput(output, false, directInputFrames, kBlockSize, 1.0f,
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

  const RenderResult fadedInstrument =
      renderImpulse(RenderGroup::Melodic, kImpulseFrame, false, 0.0f, 0.0f);
  const RenderResult preservedGuide =
      renderImpulse(RenderGroup::Guide, kImpulseFrame, false, 0.0f, 0.0f);
  expect(peakIndex(fadedInstrument.samples) == -1,
         "transport fade silences instrument stems");
  expect(peakIndex(preservedGuide.samples) >= 0,
         "transport fade preserves click and guide stems");

  for (const auto group : {RenderGroup::Melodic, RenderGroup::Percussion, RenderGroup::Guide}) {
    for (const int output : {0, 1}) {
      const auto normal = renderImpulse(group, kImpulseFrame, false, 1, 1, output, 0.8);
      const auto attenuated = renderImpulse(group, kImpulseFrame, false, 1, 1, output, 0.8, .25f);
      float normalPeak = 0, attenuatedPeak = 0;
      for (size_t i = 0; i < normal.samples.size(); ++i) {
        normalPeak = std::max(normalPeak, std::abs(normal.samples[i]));
        attenuatedPeak = std::max(attenuatedPeak, std::abs(attenuated.samples[i]));
      }
      expect(normalPeak > 0 && std::abs(attenuatedPeak / normalPeak - .25f) < .025f,
             "rehearsal gain reaches stretched, percussion and routed output paths");
    }
  }
  std::cout << "stem mix engine tests passed\n";
  return 0;
}
