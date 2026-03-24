#include "SongData.h"

#include <algorithm>
#include <cmath>
#include <utility>

const std::array<const char *, 19> kTrackCategories = {{
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
    "Electric Guitars",
    "Lead Guitars",
    "Synths",
    "Strings",
    "Brass",
    "Woodwinds",
    "FX",
    "Pads/Ambience",
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
  juce::String normalized;
  normalized.preallocateBytes(lower.getNumBytesAsUTF8());
  for (int i = 0; i < lower.length(); ++i) {
    const juce::juce_wchar c = lower[i];
    normalized += juce::CharacterFunctions::isLetterOrDigit(c) ? c : ' ';
  }
  auto tokens = juce::StringArray::fromTokens(normalized, " ", "");
  tokens.removeEmptyStrings();

  auto hasToken = [&tokens](const juce::String &t) {
    for (const auto &tok : tokens) {
      if (tok.equalsIgnoreCase(t))
        return true;
    }
    return false;
  };

  if (lower.contains("click") || hasToken("click"))
    return "Click";
  if (lower.contains("cue") || lower.contains("guide") || hasToken("cue") ||
      hasToken("cues"))
    return "Cues";
  if (lower.contains("bass") || hasToken("bass"))
    return "Bass";
  if (lower.contains("drum") || lower.contains("kick") ||
      lower.contains("snare") || hasToken("drums") || hasToken("drum"))
    return "Drums";
  if (lower.contains("perc") || hasToken("perc") || hasToken("percussion"))
    return "Percussion";
  if (lower.contains("piano") || hasToken("piano"))
    return "Piano";
  if (lower.contains("vocal") || lower.contains("vox") || hasToken("vocal") ||
      hasToken("vocals"))
    return "Vocals";

  if (lower.contains("synth") || hasToken("synth") || hasToken("synths"))
    return "Synths";
  if (lower.contains("pad") || lower.contains("ambience") ||
      lower.contains("ambient") || hasToken("pad") || hasToken("pads") ||
      hasToken("ambience") || hasToken("ambient"))
    return "Pads/Ambience";
  if (lower.contains("string") || hasToken("string") || hasToken("strings") ||
      hasToken("violin") || hasToken("cello"))
    return "Strings";
  if (lower.contains("brass") || hasToken("brass") || hasToken("trumpet") ||
      hasToken("trombone") || hasToken("horn"))
    return "Brass";
  if (lower.contains("woodwind") || hasToken("woodwind") ||
      hasToken("woodwinds") || hasToken("flute") || hasToken("sax") ||
      hasToken("saxophone") || hasToken("clarinet") || hasToken("oboe") ||
      hasToken("bassoon"))
    return "Woodwinds";
  if (lower.contains("fx") || hasToken("fx") || hasToken("sfx"))
    return "FX";

  if (lower.contains("lead") || hasToken("lead") || hasToken("solo") ||
      hasToken("lg"))
    return "Lead Guitars";
  if (lower.contains("electric") || hasToken("electric") || hasToken("eg"))
    return "Electric Guitars";
  if (lower.contains("acoustic") || lower.contains("gtr") ||
      lower.contains("guitar") || hasToken("ag") || hasToken("acoustic") ||
      hasToken("acgtr"))
    return "Acoustic";

  if (lower.contains("key") || hasToken("keys"))
    return "Keys";

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

bool isStemPitchShiftable(const ImportedStemData &stem) {
  const auto category = stem.category.trim();
  if (category.isEmpty())
    return true;
  if (category.equalsIgnoreCase("Drums") ||
      category.equalsIgnoreCase("Percussion") ||
      category.equalsIgnoreCase("Click") ||
      category.equalsIgnoreCase("Cues"))
    return false;
  return true;
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
