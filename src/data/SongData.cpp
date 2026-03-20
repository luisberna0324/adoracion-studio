#include "SongData.h"

#include <algorithm>
#include <cmath>
#include <utility>

const std::array<const char *, 11> kTrackCategories = {{
    "Bass",
    "Click",
    "Accordion",
    "Cues",
    "Drums",
    "Acoustic",
    "Keys",
    "Percussion",
    "Piano",
    "Vocals",
    "Other",
}};

juce::String makeDisplayNameFromFile(const juce::File &file) {
  auto name = file.getFileNameWithoutExtension().trim();
  if (name.isEmpty())
    return "STEM";

  name = name.replaceCharacter('_', ' ')
             .replaceCharacter('-', ' ')
             .replaceCharacter('.', ' ')
             .toUpperCase();
  return name;
}

juce::String suggestCategoryFromFilename(const juce::String &filename) {
  const auto lower = filename.toLowerCase();
  if (lower.contains("click"))
    return "Click";
  if (lower.contains("cue") || lower.contains("guide"))
    return "Cues";
  if (lower.contains("bass"))
    return "Bass";
  if (lower.contains("drum") || lower.contains("kick") ||
      lower.contains("snare"))
    return "Drums";
  if (lower.contains("perc"))
    return "Percussion";
  if (lower.contains("piano"))
    return "Piano";
  if (lower.contains("key") || lower.contains("synth"))
    return "Keys";
  if (lower.contains("vox") || lower.contains("vocal"))
    return "Vocals";
  if (lower.contains("acoustic") || lower.contains("gtr") ||
      lower.contains("guitar"))
    return "Acoustic";
  return "Other";
}

int stemPriorityForStripOrder(const ImportedStemData &stem) {
  const auto bucket =
      (stem.displayName + " " + stem.category + " " + stem.fileName)
          .toLowerCase();

  if (bucket.contains("click") || bucket.contains("clic"))
    return 0;
  if (bucket.contains("guia") || bucket.contains("guide") ||
      bucket.contains("cue"))
    return 1;
  return 2;
}

std::vector<float> buildMixedWaveformBins(const ImportedSongData &song,
                                          int binCount) {
  if (binCount <= 0)
    return {};

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();

  std::vector<juce::File> files;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;

    juce::File file(stem.storedPath);
    if (!file.existsAsFile())
      file = juce::File(stem.sourcePath);
    if (!file.existsAsFile())
      continue;

    files.push_back(std::move(file));
  }

  if (files.empty())
    return {};

  juce::int64 maxLengthSamples = 0;
  for (const auto &file : files) {
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(file))) {
      maxLengthSamples = juce::jmax(maxLengthSamples, reader->lengthInSamples);
    }
  }

  if (maxLengthSamples <= 0)
    return {};

  std::vector<float> mixedBins(static_cast<size_t>(binCount), 0.0f);
  std::vector<float> stemBins(static_cast<size_t>(binCount), 0.0f);

  constexpr int blockSize = 4096;
  for (const auto &file : files) {
    std::fill(stemBins.begin(), stemBins.end(), 0.0f);

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(file));
    if (reader == nullptr)
      continue;

    const int channels = juce::jmax(1, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> readBuffer(channels, blockSize);

    juce::int64 readPos = 0;
    while (readPos < reader->lengthInSamples) {
      const int toRead = static_cast<int>(juce::jmin<juce::int64>(
          blockSize, reader->lengthInSamples - readPos));
      if (!reader->read(&readBuffer, 0, toRead, readPos, true, true))
        break;

      for (int sample = 0; sample < toRead; ++sample) {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
          mono += readBuffer.getSample(ch, sample);
        mono /= static_cast<float>(channels);

        const auto globalPos = readPos + static_cast<juce::int64>(sample);
        const int bin = juce::jlimit(
            0, binCount - 1,
            static_cast<int>((globalPos * static_cast<juce::int64>(binCount)) /
                             maxLengthSamples));

        stemBins[static_cast<size_t>(bin)] =
            juce::jmax(stemBins[static_cast<size_t>(bin)], std::abs(mono));
      }

      readPos += toRead;
    }

    for (int i = 0; i < binCount; ++i)
      mixedBins[static_cast<size_t>(i)] += stemBins[static_cast<size_t>(i)];
  }

  const auto maxIt = std::max_element(mixedBins.begin(), mixedBins.end());
  const float maxValue = (maxIt != mixedBins.end()) ? *maxIt : 0.0f;
  if (maxValue <= 0.0f)
    return mixedBins;

  for (auto &v : mixedBins) {
    const float normal = juce::jlimit(0.0f, 1.0f, v / maxValue);
    v = std::sqrt(normal);
  }

  return mixedBins;
}
