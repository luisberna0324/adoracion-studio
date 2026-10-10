#pragma once

#include <vector>

namespace playworship::practice {

// UI/control-thread commands only. A guide's selected source is never an
// on/off switch: silence the selected bus, leaving selection and listening
// focus untouched. Indices are loaded/included stem indices, not library rows.
struct GuideSelection {
  bool available = false;
  bool dynamic = false;
  std::vector<int> stemIndices;
};

template <typename Stems, typename IsGuide>
std::vector<int> originalGuideStemIndices(const Stems &stems,
                                        const int loadedStemCount,
                                        IsGuide isGuide) {
  std::vector<int> result;
  int includedIndex = 0;
  for (const auto &stem : stems) {
    if (!stem.include)
      continue;
    const int index = includedIndex++;
    if (index >= loadedStemCount)
      break;
    if (isGuide(stem))
      result.push_back(index);
  }
  return result;
}

template <typename Session>
bool guideEnabled(const Session &session, const GuideSelection &selection) {
  if (!selection.available)
    return false;
  if (selection.dynamic)
    return !session.isDynamicGuideMuted();
  for (const auto index : selection.stemIndices)
    if (!session.isStemMuted(index))
      return true;
  return false;
}

template <typename Session>
void setGuideEnabled(Session &session, const GuideSelection &selection,
                     const bool enabled) {
  if (!selection.available)
    return;
  if (selection.dynamic) {
    if (session.isDynamicGuideMuted() == enabled)
      session.setDynamicGuideMuted(!enabled);
    return;
  }
  for (const auto index : selection.stemIndices)
    if (session.isStemMuted(index) == enabled)
      session.setStemMute(index, !enabled);
}

// Click Dyn is independent of original click stems. This power switch never
// changes those stems, the guide, Solo, routing, faders, or practice focus.
template <typename Session>
bool metronomeEnabled(const Session &session) {
  return session.isMetronomeEnabled() && session.isClickDivisionEnabled() &&
         (session.getClickDivision() & 0x07) != 0 &&
         !session.isMetronomeMuted();
}

template <typename Session>
void setMetronomeEnabled(Session &session, const bool enabled) {
  if (enabled) {
    // Keep an existing musical choice; an empty mask needs a pulse to make
    // ON useful. Restore quarter notes only in that otherwise-silent case.
    if ((session.getClickDivision() & 0x07) == 0)
      session.setClickDivision(1);
    if (!session.isClickDivisionEnabled())
      session.setClickDivisionEnabled(true);
    if (session.isMetronomeMuted())
      session.setMetronomeMuted(false);
  }
  if (session.isMetronomeEnabled() != enabled)
    session.setMetronomeEnabled(enabled);
}

} // namespace playworship::practice
