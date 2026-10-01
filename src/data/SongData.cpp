#include "SongData.h"
#include "Localization.h"

#include <algorithm>
#include <array>
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

const std::array<const char *, 15> kSongSectionSuggestions = {{
    "Intro",
    "Verse",
    "Pre-Chorus",
    "Chorus",
    "Refrain",
    "Post-Chorus",
    "Bridge",
    "Instrumental",
    "Solo",
    "Breakdown",
    "Interlude",
    "Tag",
    "Vamp",
    "Outro",
    "Ending",
}};

int timeSignatureBeatsPerBar(const juce::String &timeSignature) {
  const auto trimmed = timeSignature.trim();
  int numerator = 4;
  int denominator = 4;

  if (trimmed.isNotEmpty()) {
    const int slash = trimmed.indexOfChar('/');
    if (slash > 0) {
      numerator = trimmed.substring(0, slash).getIntValue();
      denominator = trimmed.substring(slash + 1).getIntValue();
    } else {
      numerator = trimmed.getIntValue();
    }
  }

  if (numerator <= 0)
    numerator = 4;
  if (denominator <= 0)
    denominator = 4;

  // This app measures the metronome and bar grid in quarter-note units.
  // Convert the written meter so 6/8 becomes 3 quarter-note beats per bar.
  return juce::jmax(1, (numerator * 4) / denominator);
}

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
    normalized += static_cast<juce::juce_wchar>(juce::CharacterFunctions::isLetterOrDigit(c) ? c : ' ');
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

void sortStemsForStripOrder(std::vector<ImportedStemData> &stems) {
  if (stems.size() <= 1)
    return;

  std::stable_sort(
      stems.begin(), stems.end(),
      [](const ImportedStemData &a, const ImportedStemData &b) {
        return stemPriorityForStripOrder(a) < stemPriorityForStripOrder(b);
      });
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

juce::String makeRandomArrangementPartColor() {
  auto &rng = juce::Random::getSystemRandom();
  const float hue = rng.nextFloat();
  const float saturation = 0.52f + (rng.nextFloat() * 0.35f);
  const float brightness = 0.68f + (rng.nextFloat() * 0.24f);
  return juce::Colour::fromHSV(hue, saturation, brightness, 1.0f).toString();
}

bool isArrangementPartColorValid(const juce::String &value) {
  if (value.trim().isEmpty())
    return false;
  const auto parsed = juce::Colour::fromString(value);
  return parsed.getAlpha() > 0;
}

namespace {
struct ArrangementSectionStyle {
  const char *canonicalName = nullptr;
  const char *spanishLabel = nullptr;
  const char *englishLabel = nullptr;
  const char *colour = nullptr;
  std::initializer_list<const char *> aliases;
};

const std::array<ArrangementSectionStyle, 15> kArrangementSectionStyles = {{
    {"Intro", "Intro", "Intro", "ffB66A8A", {"intro", "introduccion", "introduction"}},
    {"Verse", "Verso", "Verse", "ff8A7A68", {"verse", "verso"}},
    {"Pre-Chorus", "Pre-Coro", "Pre-Chorus", "ff5E8F76",
     {"prechorus", "pre-coro", "precoro", "prechorus", "pre"}},
    {"Chorus", "Coro", "Chorus", "ff6FA36D", {"chorus", "coro"}},
    {"Refrain", "Refrain", "Refrain", "ff6D91A3",
     {"refrain", "estribillo"}},
    {"Post-Chorus", "Post-Coro", "Post-Chorus", "ff7D8F6B",
     {"postchorus", "post-coro", "postcoro"}},
    {"Bridge", "Puente", "Bridge", "ff7D74A8", {"bridge", "puente"}},
    {"Instrumental", "Instrumental", "Instrumental", "ffA8895E",
     {"instrumental", "instrument"}},
    {"Solo", "Solo", "Solo", "ffB88455", {"solo"}},
    {"Breakdown", "Breakdown", "Breakdown", "ff8A6B6B", {"breakdown", "break"}},
    {"Interlude", "Interlude", "Interlude", "ff6E9A91",
     {"interlude", "interludio"}},
    {"Tag", "Tag", "Tag", "ff9A7FA8", {"tag"}},
    {"Vamp", "Vamp", "Vamp", "ff7F9B67", {"vamp"}},
    {"Outro", "Outro", "Outro", "ff92786D", {"outro", "salida"}},
    {"Ending", "Final", "Ending", "ff9B5E75", {"ending", "final"}},
}};

juce::String normalizeArrangementSectionToken(juce::String name) {
  auto text = name.trim().toLowerCase();
  while (text.isNotEmpty()) {
    const auto ch = text[text.length() - 1];
    if (!juce::CharacterFunctions::isDigit(ch))
      break;
    text = text.dropLastCharacters(1).trimEnd();
  }

  juce::String normalized;
  normalized.preallocateBytes(text.getNumBytesAsUTF8());
  for (int i = 0; i < text.length(); ++i) {
    const auto ch = text[i];
    if (juce::CharacterFunctions::isLetterOrDigit(ch))
      normalized << juce::String::charToString(ch);
  }
  return normalized;
}

juce::String arrangementSectionVariantSuffix(const juce::String &name) {
  const auto trimmed = name.trim();
  int suffixStart = trimmed.length();
  while (suffixStart > 0 &&
         juce::CharacterFunctions::isDigit(trimmed[suffixStart - 1])) {
    --suffixStart;
  }
  if (suffixStart <= 0 || suffixStart == trimmed.length() ||
      !juce::CharacterFunctions::isWhitespace(trimmed[suffixStart - 1])) {
    return {};
  }
  return trimmed.substring(suffixStart);
}

const ArrangementSectionStyle *findArrangementSectionStyle(const juce::String &name) {
  const auto token = normalizeArrangementSectionToken(name);
  if (token.isEmpty())
    return nullptr;

  for (const auto &style : kArrangementSectionStyles) {
    if (token == normalizeArrangementSectionToken(style.canonicalName))
      return &style;
    for (const auto *alias : style.aliases) {
      if (token == normalizeArrangementSectionToken(alias))
        return &style;
    }
  }
  return nullptr;
}

juce::String countDisplayWordForName(const juce::String &name) {
  const auto token = normalizeArrangementSectionToken(name);
  if (token == "1" || token == "one" || token == "uno")
    return L10n::tr("Uno", "One");
  if (token == "2" || token == "two" || token == "dos")
    return L10n::tr("Dos", "Two");
  if (token == "3" || token == "three" || token == "tres")
    return L10n::tr("Tres", "Three");
  if (token == "4" || token == "four" || token == "cuatro")
    return L10n::tr("Cuatro", "Four");
  if (token == "5" || token == "five" || token == "cinco")
    return L10n::tr("Cinco", "Five");
  if (token == "6" || token == "six" || token == "seis")
    return L10n::tr("Seis", "Six");
  if (token == "7" || token == "seven" || token == "siete")
    return L10n::tr("Siete", "Seven");
  return {};
}

} // namespace

juce::String canonicalArrangementSectionName(const juce::String &name) {
  if (const auto *style = findArrangementSectionStyle(name))
    return style->canonicalName;
  return {};
}

bool isKnownArrangementSectionName(const juce::String &name) {
  return findArrangementSectionStyle(name) != nullptr;
}

juce::String standardArrangementPartColorForName(const juce::String &name) {
  if (const auto *style = findArrangementSectionStyle(name))
    return style->colour;
  return {};
}

juce::String fallbackArrangementPartColorForName(const juce::String &name) {
  auto token = normalizeArrangementSectionToken(name);
  if (token.isEmpty())
    token = "part";

  uint32_t hash = 2166136261u;
  for (int i = 0; i < token.length(); ++i) {
    hash ^= static_cast<uint32_t>(token[i]);
    hash *= 16777619u;
  }

  const float hue = static_cast<float>(hash % 360u) / 360.0f;
  const float saturation = 0.48f + (static_cast<float>((hash >> 8) % 24u) / 100.0f);
  const float brightness = 0.62f + (static_cast<float>((hash >> 16) % 18u) / 100.0f);
  return juce::Colour::fromHSV(hue, saturation, brightness, 1.0f).toString();
}

juce::String arrangementPartDisplayLabelForName(const juce::String &name) {
  if (const auto countWord = countDisplayWordForName(name);
      countWord.isNotEmpty()) {
    return countWord;
  }

  if (const auto *style = findArrangementSectionStyle(name)) {
    auto label = L10n::tr(style->spanishLabel, style->englishLabel);
    if (const auto variant = arrangementSectionVariantSuffix(name);
        variant.isNotEmpty()) {
      label << " " << variant;
    }
    return label;
  }

  const auto trimmed = name.trim();
  return trimmed.isNotEmpty() ? trimmed : L10n::tr("Parte", "Part");
}

double estimateSongLengthSeconds(const ImportedSongData &song) {
  double maxDuration = 0.0;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    maxDuration = std::max(maxDuration, stem.durationSec);
  }
  return std::max(0.0, maxDuration);
}

bool isProjectBackedSong(const ImportedSongData &song) {
  return song.projectPath.trim().isNotEmpty();
}

juce::String projectIdentityForSong(const ImportedSongData &song) {
  const auto projectId = song.projectId.trim();
  return projectId.isNotEmpty() ? projectId : song.id.trim();
}

juce::String playbackIdentityForSong(const ImportedSongData &song) {
  const auto localId = song.id.trim();
  if (localId.isEmpty())
    return {};
  const auto projectPath = song.projectPath.trim();
  return projectPath.isNotEmpty() ? localId + "|project:" + projectPath
                                  : localId + "|legacy";
}

std::vector<float> buildMixedWaveformBins(
    const ImportedSongData &song, const int binCount,
    const std::function<void(double)> &onProgress) {
  if (binCount <= 0)
    return {};

  const auto report = [&onProgress](double p) {
    if (onProgress)
      onProgress(juce::jlimit(0.0, 1.0, p));
  };

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();

  struct WaveformSource {
    juce::File file;
    bool contributesToWaveform = true;
  };
  std::vector<WaveformSource> files;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;

    juce::File file(stem.storedPath);
    if (!file.existsAsFile())
      file = juce::File(stem.sourcePath);
    if (!file.existsAsFile())
      continue;

    // Use the same click/guide roles as playback, including the accented
    // Spanish guide label. Keep their duration for timeline alignment, but
    // never add their samples to the visual mix.
    const auto label = stem.displayName + " " + stem.category + " " + stem.fileName;
    const bool isMusical = stemPriorityForStripOrder(stem) > 1 &&
                           !label.containsIgnoreCase(juce::String::fromUTF8("guía"));
    files.push_back({std::move(file), isMusical});
  }

  if (files.empty())
    return {};

  report(0.03);
  juce::int64 maxLengthSamples = 0;
  for (const auto &source : files) {
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(source.file))) {
      maxLengthSamples = std::max(maxLengthSamples, (juce::int64)reader->lengthInSamples);
    }
  }

  if (maxLengthSamples <= 0)
    return {};

  report(0.07);
  std::vector<float> mixedBins(static_cast<size_t>(binCount), 0.0f);
  std::vector<float> stemBins(static_cast<size_t>(binCount), 0.0f);

  constexpr int blockSize = 4096;
  const int nFiles = static_cast<int>(files.size());
  int fileIndex = 0;
  for (const auto &source : files) {
    const double spanStart = static_cast<double>(fileIndex) / static_cast<double>(nFiles);
    const double spanEnd =
        static_cast<double>(fileIndex + 1) / static_cast<double>(nFiles);
    report(0.07 + 0.88 * spanStart);

    if (!source.contributesToWaveform) {
      report(0.07 + 0.88 * spanEnd);
      ++fileIndex;
      continue;
    }

    std::fill(stemBins.begin(), stemBins.end(), 0.0f);

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(source.file));
    if (reader == nullptr) {
      ++fileIndex;
      continue;
    }

    const int channels = std::max(1, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> readBuffer(channels, blockSize);

    juce::int64 readPos = 0;
    juce::int64 lastProgressPos = 0;
    const juce::int64 totalLen = std::max((juce::int64)1, (juce::int64)reader->lengthInSamples);
    while (readPos < reader->lengthInSamples) {
      const int toRead = static_cast<int>(std::min((juce::int64)blockSize, (juce::int64)(reader->lengthInSamples - readPos)));
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
            std::max(stemBins[static_cast<size_t>(bin)], std::abs(mono));
      }

      readPos += toRead;
      if (onProgress && (readPos - lastProgressPos) >= 300000) {
        lastProgressPos = readPos;
        const double inner =
            static_cast<double>(readPos) / static_cast<double>(totalLen);
        report(0.07 + 0.88 * (spanStart + (spanEnd - spanStart) * inner));
      }
    }

    report(0.07 + 0.88 * spanEnd);
    for (int i = 0; i < binCount; ++i)
      mixedBins[static_cast<size_t>(i)] += stemBins[static_cast<size_t>(i)];
    ++fileIndex;
  }

  report(0.96);
  const auto maxIt = std::max_element(mixedBins.begin(), mixedBins.end());
  const float maxValue = (maxIt != mixedBins.end()) ? *maxIt : 0.0f;
  if (maxValue <= 0.0f) {
    report(1.0);
    return mixedBins;
  }

  for (auto &v : mixedBins) {
    const float normal = juce::jlimit(0.0f, 1.0f, v / maxValue);
    v = std::sqrt(normal);
  }

  report(1.0);
  return mixedBins;
}

std::vector<float> buildStemWaveformBins(const ImportedStemData &stem,
                                         const double songLengthSeconds,
                                         const int binCount,
                                         const std::function<bool()> &shouldCancel) {
  if (!stem.include || binCount <= 0 || songLengthSeconds <= 0.0 ||
      (shouldCancel && shouldCancel()))
    return {};

  juce::File file(stem.storedPath);
  if (!file.existsAsFile())
    file = juce::File(stem.sourcePath);
  if (!file.existsAsFile())
    return {};

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formatManager.createReaderFor(file));
  if (reader == nullptr || reader->sampleRate <= 0.0)
    return {};

  const auto timelineSamples = juce::jmax(
      static_cast<juce::int64>(1),
      static_cast<juce::int64>(std::ceil(songLengthSeconds * reader->sampleRate)));
  const int channels = juce::jmax(1, static_cast<int>(reader->numChannels));
  constexpr int blockSize = 4096;
  juce::AudioBuffer<float> buffer(channels, blockSize);
  std::vector<float> bins(static_cast<size_t>(binCount), 0.0f);
  for (juce::int64 position = 0; position < reader->lengthInSamples;) {
    if (shouldCancel && shouldCancel())
      return {};
    const int count = static_cast<int>(juce::jmin(
        static_cast<juce::int64>(blockSize), reader->lengthInSamples - position));
    if (!reader->read(&buffer, 0, count, position, true, true))
      break;
    for (int sample = 0; sample < count; ++sample) {
      float mono = 0.0f;
      for (int channel = 0; channel < channels; ++channel)
        mono += buffer.getSample(channel, sample);
      const auto bin = juce::jlimit(
          0, binCount - 1,
          static_cast<int>(((position + sample) * binCount) / timelineSamples));
      auto &peak = bins[static_cast<size_t>(bin)];
      peak = juce::jmax(peak, std::abs(mono / static_cast<float>(channels)));
    }
    position += count;
  }

  const auto maximum = *std::max_element(bins.begin(), bins.end());
  if (maximum > 0.0f)
    for (auto &peak : bins)
      peak = std::sqrt(juce::jlimit(0.0f, 1.0f, peak / maximum));
  return bins;
}
