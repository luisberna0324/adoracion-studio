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

struct SongMixExportVirtualSourceState {
  bool enabled = false;
  float volume = 1.0f;
  float gain = 1.0f;
  bool muted = false;
  bool soloed = false;
};

struct SongMixExportGuideSection {
  juce::String name;
  double startSeconds = 0.0;
};

struct SongMixExportRequest {
  enum class Format { Wav, Mp3, M4a };
  juce::File outputFile;
  Format format = Format::Wav;
  double sampleRate = 44100.0;
  double startSeconds = 0.0;
  double endSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;
  double playbackBpm = 120.0;
  float masterVolume = 1.0f;
  float masterGain = 1.0f;
  bool masterMuted = false;
  SongMixExportVirtualSourceState dynamicClick;
  int clickDivisionMask = 1;
  int beatsPerBar = 4;
  juce::String clickStyle = "Classic";
  SongMixExportVirtualSourceState dynamicGuide;
  juce::String guideLanguage = "es";
  std::vector<SongMixExportGuideSection> guideSections;
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

  // Runs off the message thread. The callback receives rendering progress.
  static Result exportToWav(const SongMixExportRequest &request,
                            const std::function<void(double)> &onProgress);
};
