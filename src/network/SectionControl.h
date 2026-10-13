#pragma once
#include <JuceHeader.h>
#include "../data/ArrangementIdentity.h"

namespace playworship::remote {
inline bool isSectionControlAction(const juce::String &action) {
  return action == "arrange.jumpById" || action == "arrange.loopById" ||
         action == "arrange.clearJumpById" || action == "arrange.clearLoopById" ||
         action == "arrange.jump" || action == "arrange.loop" ||
         action == "arrange.clearJump" || action == "arrange.clearLoop";
}

template <typename Processor>
juce::var encodeSectionControls(const Processor &processor,
                               const juce::String &songId) {
  auto *o = new juce::DynamicObject();
  o->setProperty("v", 1);
  o->setProperty("songId", songId);
  o->setProperty("revision", processor.getArrangementControlRevision());
  const auto jump = processor.getArrangementJumpState();
  o->setProperty("pendingPartId", processor.getArrangementPartId(jump.first));
  o->setProperty("preparingPartId", processor.getArrangementPartId(jump.second));
  o->setProperty("loopPartId", processor.getArrangementPartId(processor.getArrangementPartLoopIndex()));
  return juce::var(o);
}

// Executed on the message thread. The processor rechecks the revision and
// resolves the stable ID under its control lock before changing any intent.
template <typename Processor>
bool applySectionControl(Processor &processor, const juce::String &songId,
                         bool loading, const juce::String &action,
                         const juce::var &value, const juce::var &target,
                         juce::String &error) {
  if (!isSectionControlAction(action)) {
    error = "Invalid section action"; return false;
  }
  if (!action.endsWith("ById")) {
    error = "Update the client app to control sections safely";
    return false; // v1 indices carry no song/revision; never guess a destination.
  }
  if (loading || songId.isEmpty() || !processor.hasLoadedSong()) {
    error = "Wait for the song to finish loading"; return false;
  }
  const auto song = value.getProperty("songId", {});
  const auto revision = value.getProperty("revision", {});
  if (!value.isObject() || !song.isString() || !revision.isString() ||
      revision.toString().isEmpty()) {
    error = "Invalid section request"; return false;
  }
  if (song.toString() != songId) {
    error = "The song changed; select the section again"; return false;
  }
  if (revision.toString() != processor.getArrangementControlRevision()) {
    error = "The arrangement changed; select the section again"; return false;
  }
  if (action == "arrange.clearJumpById") {
    processor.clearPendingArrangementPartJump(); return true;
  }
  if (action == "arrange.clearLoopById") {
    processor.clearArrangementPartLoop(); return true;
  }
  const auto metadata = processor.getSongArrangementSnapshot();
  const auto partId = target.toString();
  if (!target.isString() || partId.isEmpty() || partId.length() > 512 ||
      playworship::arrangement::uniquePartIndex(metadata.parts, partId) < 0) {
    error = "The section is unavailable; select another section"; return false;
  }
  const bool accepted = action == "arrange.jumpById"
      ? processor.requestArrangementPartJumpById(partId, revision.toString())
      : processor.toggleArrangementPartLoopById(partId, revision.toString());
  if (!accepted) error = "The section is unavailable; select another section";
  return accepted;
}
} // namespace playworship::remote
