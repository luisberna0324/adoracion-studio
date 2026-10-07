#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>
#include <unordered_map>

class DynamicGuideEngine {
public:
  void prepare(double sampleRate);
  void reset() noexcept;

  bool loadSectionDirectory(const juce::File &directory,
                            juce::AudioFormatManager &formatManager,
                            juce::String *errorMessage = nullptr);
  bool loadGuideDirectories(const std::vector<juce::File> &directories,
                            juce::AudioFormatManager &formatManager,
                            juce::String *errorMessage = nullptr);

  bool triggerForSectionKey(const juce::String &sectionKey);
  int resolveTriggerId(const juce::String &sectionKey) const;
  bool triggerForId(int triggerId) noexcept;
  void render(juce::AudioBuffer<float> &output) noexcept;
  void render(juce::AudioBuffer<float> &output, int startSample,
              int numSamples) noexcept;

  int getLoadedSectionCount() const noexcept;
  bool consumeLastMissingSectionKey(juce::String &outKey);

  static juce::String normalizeSectionKey(const juce::String &raw);
  static juce::String deriveSectionKeyFromFilename(const juce::String &fileName);

private:
  struct SectionSample {
    juce::AudioBuffer<float> data;
    double sampleRate = 0.0;
  };

  // A bounded polyphonic pool preserves closely placed spoken cues. When
  // full, the new trigger is rejected instead of cutting a voice or queuing
  // a stale instruction to play after its timeline position.
  static constexpr int maxVoices = 8;
  struct Voice {
    const SectionSample *sample = nullptr;
    double position = 0.0;
    double increment = 1.0;
  };
  std::array<Voice, maxVoices> voices{};
  double outputSampleRate = 44100.0;
  juce::String lastMissingKey;
  std::unordered_map<std::string, std::shared_ptr<SectionSample>> samplesByKey;
  std::unordered_map<std::string, int> triggerIdByKey;
  std::vector<std::shared_ptr<SectionSample>> samplesByTriggerId;
};
