// Tests for StemOutputRouter — covers the contract the click and guide
// routing relies on, especially the mono fold-down of a stereo render
// buffer (which is what protects the dynamic guide from leaking onto
// channel 2 when the user has not opted into stereo).
#include "../src/audio/StemOutputRouter.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr int kNumSamples = 64;
constexpr int kDefaultOutputChannels = 8;

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectNear(const float actual, const float expected,
                const float tolerance, const char *message) {
  expect(std::fabs(actual - expected) <= tolerance, message);
}

void fillStereoBuffer(juce::AudioBuffer<float> &buffer, const float left,
                      const float right) {
  buffer.clear();
  for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
    buffer.setSample(0, sample, left);
    buffer.setSample(1, sample, right);
  }
}

void testMonoRoutingFromStereoBufferKeepsGuideOnSingleChannel() {
  // Reproduces the "guía sale por canal 2" regression: when the
  // dynamic guide render buffer (always stereo internally) is routed
  // as mono Out 1, the announcement must end up *only* on the
  // configured physical channel and must not bleed onto the main
  // mix bus on Out 1-2.
  StemOutputRouter router;
  juce::AudioBuffer<float> guideBuffer(2, kNumSamples);
  fillStereoBuffer(guideBuffer, 0.8f, 0.8f);

  juce::AudioBuffer<float> output(kDefaultOutputChannels, kNumSamples);
  output.clear();

  router.routeStemToOutputChannels(guideBuffer, /*outputChannelIndex=*/0,
                                   /*width=*/1, output);

  expectNear(output.getMagnitude(0, 0, kNumSamples), 0.8f, 1.0e-6f,
             "mono routing lands the guide on the configured channel");
  for (int channel = 1; channel < kDefaultOutputChannels; ++channel) {
    expect(output.getMagnitude(channel, 0, kNumSamples) == 0.0f,
           "mono routing must not leak the guide onto neighbouring channels");
  }
}

void testMonoRoutingFoldsDownAsymmetricStereo() {
  // If the source file is genuinely stereo with different L/R
  // content, mono fold-down averages both sides. This protects the
  // musician from missing half of a localized announcement.
  StemOutputRouter router;
  juce::AudioBuffer<float> guideBuffer(2, kNumSamples);
  fillStereoBuffer(guideBuffer, 1.0f, 0.0f);

  juce::AudioBuffer<float> output(kDefaultOutputChannels, kNumSamples);
  output.clear();

  router.routeStemToOutputChannels(guideBuffer, /*outputChannelIndex=*/4,
                                   /*width=*/1, output);

  expectNear(output.getMagnitude(4, 0, kNumSamples), 0.5f, 1.0e-6f,
             "mono routing averages L+R when collapsing a stereo source");
  for (int channel = 0; channel < kDefaultOutputChannels; ++channel) {
    if (channel == 4)
      continue;
    expect(output.getMagnitude(channel, 0, kNumSamples) == 0.0f,
           "mono routing only writes to the requested physical channel");
  }
}

void testStereoRoutingLandsOnConsecutiveChannels() {
  StemOutputRouter router;
  juce::AudioBuffer<float> guideBuffer(2, kNumSamples);
  fillStereoBuffer(guideBuffer, 0.4f, 0.6f);

  juce::AudioBuffer<float> output(kDefaultOutputChannels, kNumSamples);
  output.clear();

  router.routeStemToOutputChannels(guideBuffer, /*outputChannelIndex=*/5,
                                   /*width=*/2, output);

  expectNear(output.getMagnitude(5, 0, kNumSamples), 0.4f, 1.0e-6f,
             "stereo routing puts the L source on the base channel");
  expectNear(output.getMagnitude(6, 0, kNumSamples), 0.6f, 1.0e-6f,
             "stereo routing puts the R source on the next channel");
  for (int channel = 0; channel < kDefaultOutputChannels; ++channel) {
    if (channel == 5 || channel == 6)
      continue;
    expect(output.getMagnitude(channel, 0, kNumSamples) == 0.0f,
           "stereo routing only writes to the requested pair");
  }
}

void testStereoRoutingRefusedAtLastChannel() {
  // When the requested stereo pair would overflow the output, the
  // router must drop the write entirely instead of writing a partial
  // guide onto channel N-1 alone (which would surface as a phantom
  // "guide on channel 2" complaint).
  StemOutputRouter router;
  juce::AudioBuffer<float> guideBuffer(2, kNumSamples);
  fillStereoBuffer(guideBuffer, 0.4f, 0.6f);

  juce::AudioBuffer<float> output(kDefaultOutputChannels, kNumSamples);
  output.clear();

  router.routeStemToOutputChannels(guideBuffer, /*outputChannelIndex=*/7,
                                   /*width=*/2, output);

  for (int channel = 0; channel < kDefaultOutputChannels; ++channel) {
    expect(output.getMagnitude(channel, 0, kNumSamples) == 0.0f,
           "stereo routing at the last channel is dropped, never partial");
  }
}

void testClickMonoRoutingOnOutOne() {
  // The metronome defaults to mono Out 1; on a 2-channel host the
  // click must land on channel 0 and not bleed into channel 1.
  StemOutputRouter router;
  juce::AudioBuffer<float> clickBuffer(2, kNumSamples);
  fillStereoBuffer(clickBuffer, 0.5f, 0.5f);

  juce::AudioBuffer<float> output(2, kNumSamples);
  output.clear();

  router.routeStemToOutputChannels(clickBuffer, /*outputChannelIndex=*/0,
                                   /*width=*/1, output);

  expectNear(output.getMagnitude(0, 0, kNumSamples), 0.5f, 1.0e-6f,
             "click mono routes to Out 1 (channel 0)");
  expect(output.getMagnitude(1, 0, kNumSamples) == 0.0f,
         "click mono on Out 1 never bleeds into Out 2");
}

} // namespace

int main() {
  testMonoRoutingFromStereoBufferKeepsGuideOnSingleChannel();
  testMonoRoutingFoldsDownAsymmetricStereo();
  testStereoRoutingLandsOnConsecutiveChannels();
  testStereoRoutingRefusedAtLastChannel();
  testClickMonoRoutingOnOutOne();
  std::cout << "Stem output router tests passed\n";
  return 0;
}
