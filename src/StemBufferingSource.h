#pragma once
#include <JuceHeader.h>

class StemBufferingSource : public juce::PositionableAudioSource,
                            private juce::TimeSliceClient {
public:
  StemBufferingSource(juce::PositionableAudioSource *source,
                      juce::TimeSliceThread &backgroundThread,
                      bool deleteSourceWhenDeleted, int numberOfSamplesToBuffer,
                      int numberOfChannels = 2,
                      bool prefillBufferOnPrepareToPlay = true);

  ~StemBufferingSource() override;

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void releaseResources() override;
  void getNextAudioBlock(const juce::AudioSourceChannelInfo &) override;

  void setNextReadPosition(juce::int64 newPosition) override;
  juce::int64 getNextReadPosition() const override;
  juce::int64 getTotalLength() const override { return source->getTotalLength(); }
  bool isLooping() const override { return source->isLooping(); }

  bool waitForNextAudioBlockReady(const juce::AudioSourceChannelInfo &info,
                                  juce::uint32 timeout);

private:
  juce::Range<int> getValidBufferRange(int numSamples) const;
  bool readNextBufferChunk();
  void readBufferSection(juce::int64 start, int length, int bufferOffset);
  int useTimeSlice() override;

  juce::OptionalScopedPointer<juce::PositionableAudioSource> source;
  juce::TimeSliceThread &backgroundThread;
  int numberOfSamplesToBuffer, numberOfChannels;
  juce::AudioBuffer<float> buffer;
  // Background-thread staging buffer to avoid holding the shared lock during disk reads.
  juce::AudioBuffer<float> readBuffer;
  juce::CriticalSection callbackLock, bufferRangeLock;
  juce::WaitableEvent bufferReadyEvent;
  juce::int64 bufferValidStart = 0, bufferValidEnd = 0;
  std::atomic<juce::int64> nextPlayPos{0};
  double sampleRate = 0;
  bool wasSourceLooping = false, isPrepared = false;
  const bool prefillBuffer;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemBufferingSource)
};
