#include "DynamicGuideEngine.h"

#include <cctype>
#include <limits>
#include <unordered_map>

namespace {
std::string toKeyString(const juce::String &text) {
  return text.toStdString();
}

struct SectionKeyCandidate {
  juce::String key;
  int priority = 0;
};

std::vector<SectionKeyCandidate>
deriveSectionKeyCandidatesFromFilename(const juce::String &fileName) {
  std::vector<SectionKeyCandidate> candidates;
  std::unordered_map<std::string, size_t> candidateIndexByKey;

  auto label = fileName.trim();
  const int dash = label.indexOf(" - ");
  if (dash >= 0)
    label = label.substring(dash + 3).trim();

  const auto pushKey = [&](const juce::String &candidate, const int priority) {
    const auto key = DynamicGuideEngine::normalizeSectionKey(candidate);
    if (key.isEmpty())
      return;
    const auto asStd = toKeyString(key);
    if (const auto existing = candidateIndexByKey.find(asStd);
        existing != candidateIndexByKey.end()) {
      candidates[existing->second].priority =
          juce::jmax(candidates[existing->second].priority, priority);
      return;
    }
    candidateIndexByKey.emplace(asStd, candidates.size());
    candidates.push_back({key, priority});
  };

  pushKey(label, 10);

  const auto outside = label.upToFirstOccurrenceOf("(", false, false).trim();
  const bool hasParenthesizedAlias = label.containsChar('(');
  pushKey(outside, hasParenthesizedAlias ? 40 : 30);

  int start = 0;
  while (start < label.length()) {
    const int open = label.indexOfChar(start, '(');
    if (open < 0)
      break;
    const int close = label.indexOfChar(open + 1, ')');
    if (close < 0)
      break;
    pushKey(label.substring(open + 1, close).trim(), 50);
    start = close + 1;
  }

  return candidates;
}

std::vector<juce::String>
deriveSectionKeysFromFilename(const juce::String &fileName) {
  std::vector<juce::String> keys;
  for (const auto &candidate : deriveSectionKeyCandidatesFromFilename(fileName))
    keys.push_back(candidate.key);
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
  triggerIdByKey.clear();
  samplesByTriggerId.clear();
  reset();
  std::unordered_map<std::string, int> samplePriorityByKey;

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
        juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
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

      const auto candidates = deriveSectionKeyCandidatesFromFilename(
          file.getFileNameWithoutExtension());
      if (candidates.empty())
        continue;
      for (const auto &candidate : candidates) {
        const auto key = toKeyString(candidate.key);
        const auto existingPriority = samplePriorityByKey.find(key);
        if (existingPriority != samplePriorityByKey.end() &&
            existingPriority->second >= candidate.priority) {
          continue;
        }
        samplesByKey[key] = sample;
        samplePriorityByKey[key] = candidate.priority;
      }
    }
  }

  if (samplesByKey.empty()) {
    if (errorMessage != nullptr)
      *errorMessage = "No dynamic guide samples found.";
    return false;
  }
  samplesByTriggerId.reserve(samplesByKey.size());
  for (const auto &entry : samplesByKey) {
    const int triggerId = static_cast<int>(samplesByTriggerId.size());
    triggerIdByKey.emplace(entry.first, triggerId);
    samplesByTriggerId.push_back(entry.second);
  }
  return true;
}

bool DynamicGuideEngine::triggerForSectionKey(const juce::String &sectionKey) {
  const int triggerId = resolveTriggerId(sectionKey);
  if (triggerId < 0) {
    lastMissingKey = sectionKey.trim();
    return false;
  }
  return triggerForId(triggerId);
}

int DynamicGuideEngine::resolveTriggerId(
    const juce::String &sectionKey) const {
  const auto normalized = normalizeSectionKey(sectionKey);
  if (normalized.isEmpty())
    return -1;

  if (const auto it = triggerIdByKey.find(toKeyString(normalized));
      it != triggerIdByKey.end()) {
    return it->second;
  }

  // The Spanish guide pack has no dedicated "Slowly Build" recording, while
  // the arrangement UI exposes it as "Sube lento". Keep the cue audible by
  // falling back to the existing Build/Sube intensidad sample. Exact matches
  // (including the English Slowly Build recording) always win above.
  if (normalized == "slowly build" || normalized == "sube lento") {
    if (const auto fallback = triggerIdByKey.find("build");
        fallback != triggerIdByKey.end()) {
      return fallback->second;
    }
  }

  return -1;
}

bool DynamicGuideEngine::triggerForId(const int triggerId) noexcept {
  if (!juce::isPositiveAndBelow(
          triggerId, static_cast<int>(samplesByTriggerId.size()))) {
    return false;
  }
  activeSample =
      samplesByTriggerId[static_cast<size_t>(triggerId)].get();
  activePosition = 0.0;
  activeIncrement = activeSample->sampleRate > 0.0 && outputSampleRate > 0.0
                        ? (activeSample->sampleRate / outputSampleRate)
                        : 1.0;
  if (!(activeIncrement > 0.0))
    activeIncrement = 1.0;
  return true;
}

void DynamicGuideEngine::render(juce::AudioBuffer<float> &output) noexcept {
  render(output, 0, output.getNumSamples());
}

void DynamicGuideEngine::render(juce::AudioBuffer<float> &output,
                                const int startSample,
                                const int samplesToRender) noexcept {
  if (activeSample == nullptr)
    return;
  const int boundedStart =
      juce::jlimit(0, output.getNumSamples(), startSample);
  const int outSamples = juce::jlimit(
      0, output.getNumSamples() - boundedStart, samplesToRender);
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
      output.addSample(ch, boundedStart + i, v);
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
