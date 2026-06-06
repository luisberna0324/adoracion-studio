#include "DynamicGuideEngine.h"

#include <cctype>
#include <limits>
#include <unordered_set>

namespace {
std::string toKeyString(const juce::String &text) {
  return text.toStdString();
}

std::vector<juce::String>
deriveSectionKeysFromFilename(const juce::String &fileName) {
  std::vector<juce::String> keys;
  std::unordered_set<std::string> seen;

  auto label = fileName.trim();
  const int dash = label.indexOf(" - ");
  if (dash >= 0)
    label = label.substring(dash + 3).trim();

  const auto pushKey = [&](const juce::String &candidate) {
    const auto key = DynamicGuideEngine::normalizeSectionKey(candidate);
    if (key.isEmpty())
      return;
    const auto asStd = toKeyString(key);
    if (seen.insert(asStd).second)
      keys.push_back(key);
  };

  pushKey(label);

  const auto outside = label.upToFirstOccurrenceOf("(", false, false).trim();
  pushKey(outside);

  int start = 0;
  while (start < label.length()) {
    const int open = label.indexOfChar(start, '(');
    if (open < 0)
      break;
    const int close = label.indexOfChar(open + 1, ')');
    if (close < 0)
      break;
    pushKey(label.substring(open + 1, close).trim());
    start = close + 1;
  }

  return keys;
}
} // namespace

void DynamicGuideEngine::prepare(const double sampleRate) {
  outputSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  reset();
}

void DynamicGuideEngine::reset() noexcept {
  activeSample = nullptr;
  activePosition = 0.0;
  activeIncrement = 1.0;
}

bool DynamicGuideEngine::loadSectionDirectory(const juce::File &directory,
                                              juce::AudioFormatManager &formatManager,
                                              juce::String *errorMessage) {
  return loadGuideDirectories({directory}, formatManager, errorMessage);
}

bool DynamicGuideEngine::loadGuideDirectories(
    const std::vector<juce::File> &directories,
    juce::AudioFormatManager &formatManager,
    juce::String *errorMessage) {
  samplesByKey.clear();
  reset();

  if (directories.empty()) {
    if (errorMessage != nullptr)
      *errorMessage = "No guide directories configured.";
    return false;
  }

  for (const auto &directory : directories) {
    if (!directory.isDirectory()) {
      if (errorMessage != nullptr)
        *errorMessage = "Guide directory not found: " +
                        directory.getFullPathName();
      return false;
    }

    const auto files = directory.findChildFiles(
        juce::File::findFiles, false, "*.m4a;*.wav;*.aif;*.aiff;*.mp3");
    for (const auto &file : files) {
      auto reader = std::unique_ptr<juce::AudioFormatReader>(
          formatManager.createReaderFor(file));
      if (reader == nullptr)
        continue;
      const int numChannels =
          juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
      const int numSamples = static_cast<int>(
          juce::jlimit<juce::int64>(0, std::numeric_limits<int>::max(),
                                    reader->lengthInSamples));
      if (numSamples <= 0)
        continue;

      auto sample = std::make_shared<SectionSample>();
      sample->data.setSize(numChannels, numSamples);
      if (!reader->read(&sample->data, 0, numSamples, 0, true,
                        numChannels > 1))
        continue;
      sample->sampleRate =
          reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;

      const auto keys =
          deriveSectionKeysFromFilename(file.getFileNameWithoutExtension());
      if (keys.empty())
        continue;
      for (const auto &key : keys)
        samplesByKey[toKeyString(key)] = sample;
    }
  }

  if (samplesByKey.empty()) {
    if (errorMessage != nullptr)
      *errorMessage = "No dynamic guide samples found.";
    return false;
  }
  return true;
}

bool DynamicGuideEngine::triggerForSectionKey(const juce::String &sectionKey) {
  const auto normalized = normalizeSectionKey(sectionKey);
  if (normalized.isEmpty())
    return false;

  const auto it = samplesByKey.find(toKeyString(normalized));
  if (it == samplesByKey.end()) {
    lastMissingKey = sectionKey.trim();
    return false;
  }

  activeSample = it->second.get();
  activePosition = 0.0;
  activeIncrement = activeSample->sampleRate > 0.0 && outputSampleRate > 0.0
                        ? (activeSample->sampleRate / outputSampleRate)
                        : 1.0;
  if (!(activeIncrement > 0.0))
    activeIncrement = 1.0;
  return true;
}

void DynamicGuideEngine::render(juce::AudioBuffer<float> &output) noexcept {
  if (activeSample == nullptr)
    return;
  const int outSamples = output.getNumSamples();
  const int outChannels = output.getNumChannels();
  const int srcSamples = activeSample->data.getNumSamples();
  const int srcChannels = activeSample->data.getNumChannels();
  if (outSamples <= 0 || outChannels <= 0 || srcSamples <= 0 || srcChannels <= 0) {
    activeSample = nullptr;
    return;
  }

  for (int i = 0; i < outSamples; ++i) {
    const int pos0 = static_cast<int>(activePosition);
    if (pos0 >= srcSamples) {
      activeSample = nullptr;
      break;
    }
    int pos1 = pos0 + 1;
    if (pos1 >= srcSamples)
      pos1 = srcSamples - 1;
    const float frac =
        static_cast<float>(activePosition - static_cast<double>(pos0));

    for (int ch = 0; ch < outChannels; ++ch) {
      const int srcCh = juce::jmin(ch, srcChannels - 1);
      const auto *src = activeSample->data.getReadPointer(srcCh);
      const float v = src[pos0] + (src[pos1] - src[pos0]) * frac;
      output.addSample(ch, i, v);
    }

    activePosition += activeIncrement;
  }
}

int DynamicGuideEngine::getLoadedSectionCount() const noexcept {
  return static_cast<int>(samplesByKey.size());
}

bool DynamicGuideEngine::consumeLastMissingSectionKey(juce::String &outKey) {
  if (lastMissingKey.isEmpty())
    return false;
  outKey = lastMissingKey;
  lastMissingKey.clear();
  return true;
}

juce::String DynamicGuideEngine::normalizeSectionKey(const juce::String &raw) {
  auto text = raw.trim().toLowerCase();
  if (text.isEmpty())
    return {};

  juce::String normalized;
  normalized.preallocateBytes(text.getNumBytesAsUTF8());
  bool lastSpace = false;
  for (int i = 0; i < text.length(); ++i) {
    const auto ch = text[i];
    const bool isAlnum =
        juce::CharacterFunctions::isLetterOrDigit(ch) || ch == '#' || ch == '+' ||
        ch == '(' || ch == ')';
    if (isAlnum) {
      normalized << juce::String::charToString(ch);
      lastSpace = false;
      continue;
    }
    if (!lastSpace) {
      normalized << " ";
      lastSpace = true;
    }
  }
  return normalized.trim();
}

juce::String
DynamicGuideEngine::deriveSectionKeyFromFilename(const juce::String &fileName) {
  const auto keys = deriveSectionKeysFromFilename(fileName);
  if (keys.empty())
    return {};
  return keys.front();
}
