#pragma once

#include <JuceHeader.h>

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
  void render(juce::AudioBuffer<float> &output) noexcept;

  int getLoadedSectionCount() const noexcept;
  bool consumeLastMissingSectionKey(juce::String &outKey);

  static juce::String normalizeSectionKey(const juce::String &raw);
  static juce::String deriveSectionKeyFromFilename(const juce::String &fileName);

private:
  struct SectionSample {
    juce::AudioBuffer<float> data;
    double sampleRate = 0.0;
  };

  const SectionSample *activeSample = nullptr;
  double activePosition = 0.0;
  double activeIncrement = 1.0;
  double outputSampleRate = 44100.0;
  juce::String lastMissingKey;
  std::unordered_map<std::string, std::shared_ptr<SectionSample>> samplesByKey;
};
