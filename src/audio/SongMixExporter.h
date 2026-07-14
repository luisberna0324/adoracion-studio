#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

struct StemMixExportStemState {
  juce::String filePath;
  float volume = 1.0f;
  float gain = 1.0f;
  bool muted = false;
  bool soloed = false;
  bool clickGuide = false;
  bool pitchShiftable = false;
};

struct SongMixExportRequest {
  juce::String songTitle;
  juce::File outputFile;
  double sampleRate = 44100.0;
  int numChannels = 2;
  double startSeconds = 0.0;
  double endSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;
  float masterVolume = 1.0f;
  float masterGain = 1.0f;
  bool masterMuted = false;
  std::vector<StemMixExportStemState> stems;
};

class SongMixExporter {
public:
  struct Result {
    bool ok = false;
    juce::String error;
    juce::File outputFile;
    double durationSeconds = 0.0;
  };

  static Result exportToWav(const SongMixExportRequest &request,
                            const std::function<void(double)> &onProgress);
};
