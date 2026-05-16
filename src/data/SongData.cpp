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

const std::array<const char *, 14> kSongSectionSuggestions = {{
    "Intro",
    "Verse",
    "Pre-Chorus",
    "Chorus",
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

const std::array<ArrangementSectionStyle, 14> kArrangementSectionStyles = {{
    {"Intro", "Intro", "Intro", "ffB66A8A", {"intro", "introduccion", "introduction"}},
    {"Verse", "Verso", "Verse", "ff8A7A68", {"verse", "verso"}},
    {"Pre-Chorus", "Pre-Coro", "Pre-Chorus", "ff5E8F76",
     {"prechorus", "pre-coro", "precoro", "prechorus", "pre"}},
    {"Chorus", "Coro", "Chorus", "ff6FA36D", {"chorus", "coro"}},
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
  if (const auto *style = findArrangementSectionStyle(name))
    return L10n::tr(style->spanishLabel, style->englishLabel);

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

  report(0.03);
  juce::int64 maxLengthSamples = 0;
  for (const auto &file : files) {
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(file))) {
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
  for (const auto &file : files) {
    const double spanStart = static_cast<double>(fileIndex) / static_cast<double>(nFiles);
    const double spanEnd =
        static_cast<double>(fileIndex + 1) / static_cast<double>(nFiles);
    report(0.07 + 0.88 * spanStart);

    std::fill(stemBins.begin(), stemBins.end(), 0.0f);

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(file));
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
