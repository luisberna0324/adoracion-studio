#pragma once

#include <JuceHeader.h>
#include <atomic>

class ResamplingPositionableSource : public juce::PositionableAudioSource {
public:
  ResamplingPositionableSource(juce::PositionableAudioSource *inputSource,
                               double inputSampleRate,
                               int numChannels = 2);

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void releaseResources() override;
  void getNextAudioBlock(const juce::AudioSourceChannelInfo &info) override;

  void setNextReadPosition(juce::int64 newPosition) override;
  juce::int64 getNextReadPosition() const override;
  juce::int64 getTotalLength() const override;
  bool isLooping() const override;
  void setLooping(bool shouldLoop) override;

private:
  double getResampleRatio() const noexcept;

  juce::PositionableAudioSource *input = nullptr;
  juce::ResamplingAudioSource resampler;
  double sourceSampleRate = 0.0;
  double outputSampleRate = 0.0;
  std::atomic<juce::int64> outputPosition{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResamplingPositionableSource)
};
