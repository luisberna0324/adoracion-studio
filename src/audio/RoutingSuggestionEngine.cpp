#include "RoutingSuggestionEngine.h"

#include <algorithm>

bool RoutingSuggestionEngine::stemNameIsClickOrCue(const juce::String &name) {
  const auto lower = name.toLowerCase();
  return lower.contains("click") || lower.contains("cue") ||
         lower.contains("metronome");
}

bool RoutingSuggestionEngine::isCurrentCompatible(
    const int numChannels, const int numStems,
    const std::vector<int> &current) {
  if (static_cast<int>(current.size()) != numStems)
    return false;
  const int maxCh = juce::jmax(1, numChannels);
  for (const int c : current) {
    if (c < 1 || c > maxCh)
      return false;
  }
  return true;
}

std::vector<StemRoutingSuggestion> RoutingSuggestionEngine::suggest(
    const int numAvailableChannels, const std::vector<juce::String> &stemNames,
    const std::vector<int> &currentChannelsOneBased) {
  const int nStems = static_cast<int>(stemNames.size());
  const int nCh = juce::jmax(1, numAvailableChannels);

  std::vector<StemRoutingSuggestion> out;
  out.reserve(static_cast<size_t>(nStems));

  if (isCurrentCompatible(nCh, nStems, currentChannelsOneBased)) {
    for (int i = 0; i < nStems; ++i) {
      StemRoutingSuggestion s;
      s.stemIndex = i;
      s.stemName = stemNames[static_cast<size_t>(i)];
      s.suggestedChannel = currentChannelsOneBased[static_cast<size_t>(i)];
      out.push_back(std::move(s));
    }
    return out;
  }

  if (nCh <= 2) {
    for (int i = 0; i < nStems; ++i) {
      StemRoutingSuggestion s;
      s.stemIndex = i;
      s.stemName = stemNames[static_cast<size_t>(i)];
      s.suggestedChannel = 1;
      out.push_back(std::move(s));
    }
    return out;
  }

  if (nCh >= nStems) {
    for (int i = 0; i < nStems; ++i) {
      StemRoutingSuggestion s;
      s.stemIndex = i;
      s.stemName = stemNames[static_cast<size_t>(i)];
      s.suggestedChannel = i + 1;
      out.push_back(std::move(s));
    }
    return out;
  }

  std::vector<int> musicIdx;
  std::vector<int> clickIdx;
  musicIdx.reserve(static_cast<size_t>(nStems));
  clickIdx.reserve(static_cast<size_t>(nStems));
  for (int i = 0; i < nStems; ++i) {
    if (stemNameIsClickOrCue(stemNames[static_cast<size_t>(i)]))
      clickIdx.push_back(i);
    else
      musicIdx.push_back(i);
  }

  const int lastCh = nCh;
  std::vector<int> assigned(static_cast<size_t>(nStems), lastCh);

  const int musicChCount = juce::jmax(1, nCh - 1);
  int musicSlot = 0;
  for (const int idx : musicIdx) {
    assigned[static_cast<size_t>(idx)] = (musicSlot % musicChCount) + 1;
    ++musicSlot;
  }

  for (const int idx : clickIdx)
    assigned[static_cast<size_t>(idx)] = lastCh;

  for (int i = 0; i < nStems; ++i) {
    StemRoutingSuggestion s;
    s.stemIndex = i;
    s.stemName = stemNames[static_cast<size_t>(i)];
    s.suggestedChannel = assigned[static_cast<size_t>(i)];
    out.push_back(std::move(s));
  }
  return out;
}
