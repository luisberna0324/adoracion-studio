#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <memory>

class BungeeProcessor {
public:
  enum class ContentType : uint8_t { Melodic = 0, Percussive = 1 };

  BungeeProcessor();
  ~BungeeProcessor();

  BungeeProcessor(const BungeeProcessor &) = delete;
  BungeeProcessor &operator=(const BungeeProcessor &) = delete;

  bool prepare(double sampleRate, int maxOutputFrames, int numChannels);
  bool rebuild();
  // Stream construction allocates internal work buffers. Discontinuities are
  // handled by replacing a prepared voice outside the audio callback.
  void reset() noexcept;
  void setPitchSemiTones(int semitones) noexcept;
  void setTempoRatio(double ratio) noexcept;
  void setContentType(ContentType type) noexcept;

  int requiredInputFrames(int outputFrames) noexcept;
  int process(const juce::AudioBuffer<float> &input, int inputFrames,
              juce::AudioBuffer<float> &output, int outputFrames) noexcept;
  void process(juce::AudioBuffer<float> &buffer) noexcept {
    process(buffer, buffer.getNumSamples(), buffer, buffer.getNumSamples());
  }

  int getPitchSemiTones() const noexcept;
  double getTempoRatio() const noexcept;
  int getLatencySamples() const noexcept;
  int getNumChannels() const noexcept;
  bool isPrepared() const noexcept;
  const char *backendName() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
