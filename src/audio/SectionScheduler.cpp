#include "SectionScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace playworship::audio {
namespace {

constexpr std::uint32_t insertedClipFlag = 1U << 0U;
constexpr std::uint32_t muteInstrumentsFlag = 1U << 1U;
constexpr std::uint32_t skipPlaybackFlag = 1U << 2U;
constexpr std::uint32_t triggerGuideOnEntryFlag = 1U << 3U;

SectionScheduler::Frame
saturatingBlockEnd(const SectionScheduler::Frame blockStart,
                   const int blockFrames) noexcept {
  if (blockFrames <= 0)
    return blockStart;
  const auto maximum = std::numeric_limits<SectionScheduler::Frame>::max();
  return blockStart > maximum - blockFrames ? maximum
                                            : blockStart + blockFrames;
}

int transitionOffset(const SectionScheduler::Frame blockStart,
                     const SectionScheduler::Frame transitionFrame,
                     const int blockFrames) noexcept {
  const auto distance = std::clamp(
      transitionFrame - blockStart, static_cast<SectionScheduler::Frame>(0),
      static_cast<SectionScheduler::Frame>(std::max(0, blockFrames)));
  return static_cast<int>(distance);
}

} // namespace

void SectionScheduler::AtomicSection::store(
    const Section &section) noexcept {
  startFrame.store(section.startFrame, std::memory_order_relaxed);
  endFrame.store(section.endFrame, std::memory_order_relaxed);
  sourceStartFrame.store(section.sourceStartFrame, std::memory_order_relaxed);
  sourceEndFrame.store(section.sourceEndFrame, std::memory_order_relaxed);
  guideTriggerFrame.store(section.guideTriggerFrame,
                          std::memory_order_relaxed);
  guideTriggerId.store(section.guideTriggerId, std::memory_order_relaxed);

  std::uint32_t sectionFlags = 0;
  if (section.insertedClip)
    sectionFlags |= insertedClipFlag;
  if (section.muteInstruments)
    sectionFlags |= muteInstrumentsFlag;
  if (section.skipPlayback)
    sectionFlags |= skipPlaybackFlag;
  if (section.triggerGuideOnEntry)
    sectionFlags |= triggerGuideOnEntryFlag;
  flags.store(sectionFlags, std::memory_order_relaxed);
}

SectionScheduler::Section
SectionScheduler::AtomicSection::load() const noexcept {
  const auto sectionFlags = flags.load(std::memory_order_relaxed);
  return {
      startFrame.load(std::memory_order_relaxed),
      endFrame.load(std::memory_order_relaxed),
      sourceStartFrame.load(std::memory_order_relaxed),
      sourceEndFrame.load(std::memory_order_relaxed),
      guideTriggerFrame.load(std::memory_order_relaxed),
      guideTriggerId.load(std::memory_order_relaxed),
      (sectionFlags & triggerGuideOnEntryFlag) != 0U,
      (sectionFlags & insertedClipFlag) != 0U,
      (sectionFlags & muteInstrumentsFlag) != 0U,
      (sectionFlags & skipPlaybackFlag) != 0U,
  };
}

void SectionScheduler::publish(
    const std::vector<Section> &sections) noexcept {
  const std::lock_guard<std::mutex> lock(publishMutex);
  const int published = publishedIndex.load(std::memory_order_acquire);
  const int writeIndex = (published + 1) % snapshotBankCount;
  auto &bank = snapshotBanks[static_cast<std::size_t>(writeIndex)];

  // Atomic fields make an old bank safe to inspect even if a rapid publisher
  // wraps around while a reader is copying it. The odd/even sequence lets that
  // reader reject a mixed generation without owning a shared hazard slot.
  bank.sequence.fetch_add(1, std::memory_order_acq_rel);

  const int count =
      std::min(static_cast<int>(sections.size()), maxSections);
  for (int i = 0; i < count; ++i) {
    const auto &source = sections[static_cast<std::size_t>(i)];
    const auto normalizedStart = std::max<Frame>(0, source.startFrame);
    const auto normalizedEnd =
        std::max(normalizedStart, source.endFrame);
    const auto rawSourceStart =
        source.sourceEndFrame > source.sourceStartFrame
            ? source.sourceStartFrame
            : source.startFrame;
    const auto rawSourceEnd =
        source.sourceEndFrame > source.sourceStartFrame ? source.sourceEndFrame
                                                        : source.endFrame;
    const auto normalizedSourceStart =
        std::max<Frame>(0, rawSourceStart);
    const auto normalizedSourceEnd =
        std::max(normalizedSourceStart, rawSourceEnd);

    bank.sections[static_cast<std::size_t>(i)].store(
        {normalizedStart,
         normalizedEnd,
         normalizedSourceStart,
         normalizedSourceEnd,
         source.guideTriggerFrame,
         source.guideTriggerId,
         source.triggerGuideOnEntry,
         source.insertedClip,
         source.muteInstruments,
         source.skipPlayback});
  }

  const auto generation = nextGeneration++;
  if (nextGeneration == 0)
    nextGeneration = 1;
  bank.count.store(count, std::memory_order_relaxed);
  bank.generation.store(generation, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  bank.sequence.fetch_add(1, std::memory_order_release);
  publishedIndex.store(writeIndex, std::memory_order_release);
}

void SectionScheduler::clear() noexcept { publish({}); }

bool SectionScheduler::tryReadBank(const int bankIndex,
                                   Snapshot &snapshot) const noexcept {
  if (bankIndex < 0 || bankIndex >= snapshotBankCount)
    return false;

  const auto &bank =
      snapshotBanks[static_cast<std::size_t>(bankIndex)];
  const auto sequenceBefore =
      bank.sequence.load(std::memory_order_acquire);
  if ((sequenceBefore & 1U) != 0U)
    return false;

  snapshot.generation =
      bank.generation.load(std::memory_order_relaxed);
  snapshot.count = std::clamp(
      bank.count.load(std::memory_order_relaxed), 0, maxSections);
  for (int i = 0; i < snapshot.count; ++i) {
    snapshot.sections[static_cast<std::size_t>(i)] =
        bank.sections[static_cast<std::size_t>(i)].load();
  }

  // Keep every field load inside the two sequence observations. Since every
  // field is itself atomic, a failed validation is still data-race-free.
  std::atomic_thread_fence(std::memory_order_acquire);
  const auto sequenceAfter =
      bank.sequence.load(std::memory_order_acquire);
  if (sequenceBefore != sequenceAfter || (sequenceAfter & 1U) != 0U)
    return false;

  snapshot.valid = true;
  return true;
}

SectionScheduler::Snapshot
SectionScheduler::acquireSnapshot() const noexcept {
  Snapshot candidate;
  for (int attempt = 0; attempt < maxSnapshotAcquireAttempts; ++attempt) {
    candidate.valid = false;
    const int index = publishedIndex.load(std::memory_order_acquire);
    if (tryReadBank(index, candidate))
      return candidate;
  }
  return {};
}

int SectionScheduler::size(const Snapshot &snapshot) noexcept {
  return snapshot.valid ? std::clamp(snapshot.count, 0, maxSections) : 0;
}

int SectionScheduler::findSection(const Snapshot &snapshot,
                                  const Frame frame) noexcept {
  const int count = size(snapshot);
  if (count <= 0)
    return -1;

  for (int i = count - 1; i >= 0; --i) {
    const auto &section = snapshot.sections[static_cast<std::size_t>(i)];
    if (frame >= section.startFrame && frame < section.endFrame)
      return i;
  }

  int nearest = 0;
  Frame nearestDistance = std::numeric_limits<Frame>::max();
  for (int i = 0; i < count; ++i) {
    const auto &section = snapshot.sections[static_cast<std::size_t>(i)];
    const Frame distance =
        frame < section.startFrame ? section.startFrame - frame
                                   : frame - section.endFrame;
    if (distance < nearestDistance) {
      nearest = i;
      nearestDistance = distance;
    }
  }
  return nearest;
}

int SectionScheduler::findGuideTrigger(const Snapshot &snapshot,
                                       const Frame blockStart,
                                       const int blockFrames) noexcept {
  if (blockFrames <= 0)
    return -1;
  const Frame blockEnd = saturatingBlockEnd(blockStart, blockFrames);
  const int count = size(snapshot);
  for (int i = count - 1; i >= 0; --i) {
    const Frame trigger =
        snapshot.sections[static_cast<std::size_t>(i)].guideTriggerFrame;
    if (trigger >= blockStart && trigger < blockEnd)
      return i;
  }
  return -1;
}

bool SectionScheduler::getSection(const Snapshot &snapshot, const int index,
                                  Section &section) noexcept {
  const int count = size(snapshot);
  if (index < 0 || index >= count)
    return false;
  section = snapshot.sections[static_cast<std::size_t>(index)];
  return true;
}

bool SectionScheduler::mapFrameToSourceFrame(
    const Snapshot &snapshot, const Frame frame,
    const Frame arrangementEndFrame, const Frame sourceEndFrame,
    Frame &sourceFrameOut) noexcept {
  const int count = size(snapshot);
  if (count <= 0 || sourceEndFrame <= 0)
    return false;

  Frame insertedOffset = 0;
  for (int i = 0; i < count; ++i) {
    const auto &section = snapshot.sections[static_cast<std::size_t>(i)];
    if (!section.insertedClip)
      continue;

    if (frame < section.startFrame) {
      sourceFrameOut =
          std::clamp(frame - insertedOffset, static_cast<Frame>(0),
                     sourceEndFrame);
      return true;
    }

    if (frame < section.endFrame) {
      const auto arrangementDuration =
          std::max<Frame>(1, section.endFrame - section.startFrame);
      const auto sourceDuration =
          std::max<Frame>(1, section.sourceEndFrame - section.sourceStartFrame);
      const auto arrangementOffset =
          std::clamp(frame - section.startFrame, static_cast<Frame>(0),
                     arrangementDuration);
      if (arrangementDuration == sourceDuration)
        sourceFrameOut = section.sourceStartFrame + arrangementOffset;
      else {
        const double normalized = static_cast<double>(arrangementOffset) /
                                  static_cast<double>(arrangementDuration);
        sourceFrameOut = section.sourceStartFrame +
                         static_cast<Frame>(std::llround(
                             normalized * static_cast<double>(sourceDuration)));
      }
      sourceFrameOut =
          std::clamp(sourceFrameOut, static_cast<Frame>(0), sourceEndFrame);
      return true;
    }

    insertedOffset += section.endFrame - section.startFrame;
  }

  static_cast<void>(arrangementEndFrame);
  sourceFrameOut = frame - insertedOffset;
  sourceFrameOut =
      std::clamp(sourceFrameOut, static_cast<Frame>(0), sourceEndFrame);
  return true;
}

SectionScheduler::BlockDecision SectionScheduler::evaluate(
    const Snapshot &snapshot, const Frame blockStart, const int blockFrames,
    const bool customArrangement, const int pendingSection,
    const int loopSection) noexcept {
  BlockDecision decision;
  const int count = size(snapshot);
  if (count <= 0 || blockFrames <= 0)
    return decision;

  const auto findInSnapshot = [&snapshot, count](const Frame frame) noexcept {
    for (int i = count - 1; i >= 0; --i) {
      const auto &section = snapshot.sections[static_cast<std::size_t>(i)];
      if (frame >= section.startFrame && frame < section.endFrame)
        return i;
    }
    return -1;
  };

  decision.currentSection = findInSnapshot(blockStart);
  if (decision.currentSection >= 0) {
    const auto &current =
        snapshot.sections[static_cast<std::size_t>(decision.currentSection)];
    if (current.skipPlayback) {
      decision.transition = true;
      decision.skippedSection = true;
      decision.transitionOffsetFrames = 0;
      if (decision.currentSection + 1 < count) {
        decision.targetSection = decision.currentSection + 1;
        decision.targetFrame =
            snapshot.sections[static_cast<std::size_t>(decision.targetSection)]
                .startFrame;
      } else {
        decision.targetSection = -1;
        decision.targetFrame = current.endFrame;
      }
      return decision;
    }
  }

  const Frame blockEnd = saturatingBlockEnd(blockStart, blockFrames);
  const bool validLoop = loopSection >= 0 && loopSection < count;
  if (validLoop) {
    const auto &loop =
        snapshot.sections[static_cast<std::size_t>(loopSection)];
    if (blockStart >= loop.startFrame && blockStart < loop.endFrame &&
        blockEnd >= loop.endFrame) {
      decision.currentSection = loopSection;
      decision.transition = true;
      decision.targetSection = loopSection;
      decision.targetFrame = loop.startFrame;
      decision.transitionOffsetFrames =
          transitionOffset(blockStart, loop.endFrame, blockFrames);
      return decision;
    }
    if (blockStart >= loop.startFrame && blockStart < loop.endFrame) {
      // Still inside the loop. A pending jump to a different section escapes
      // the loop at the next loop boundary, mirroring the wrap behavior above.
      const bool validPendingEscape =
          pendingSection >= 0 && pendingSection < count &&
          pendingSection != loopSection;
      if (validPendingEscape) {
        decision.currentSection = loopSection;
        decision.transition = true;
        decision.targetSection = pendingSection;
        decision.targetFrame =
            snapshot.sections[static_cast<std::size_t>(pendingSection)]
                .startFrame;
        decision.transitionOffsetFrames =
            transitionOffset(blockStart, loop.endFrame, blockFrames);
        return decision;
      }
      return decision;
    }
  }

  if (decision.currentSection < 0)
    return decision;

  const auto &current =
      snapshot.sections[static_cast<std::size_t>(decision.currentSection)];
  if (blockEnd < current.endFrame)
    return decision;

  const bool validPending =
      pendingSection >= 0 && pendingSection < count &&
      pendingSection != decision.currentSection;
  if (validPending) {
    decision.transition = true;
    decision.targetSection = pendingSection;
    decision.targetFrame =
        snapshot.sections[static_cast<std::size_t>(pendingSection)].startFrame;
    decision.transitionOffsetFrames =
        transitionOffset(blockStart, current.endFrame, blockFrames);
    return decision;
  }

  if (customArrangement && decision.currentSection + 1 < count) {
    const auto &next =
        snapshot.sections[static_cast<std::size_t>(decision.currentSection +
                                                   1)];
    if (current.endFrame == next.startFrame &&
        current.sourceEndFrame != next.sourceStartFrame) {
      decision.transition = true;
      decision.targetSection = decision.currentSection + 1;
      decision.targetFrame = next.startFrame;
      decision.transitionOffsetFrames =
          transitionOffset(blockStart, current.endFrame, blockFrames);
      return decision;
    }
  }

  return decision;
}

int SectionScheduler::size() const noexcept {
  return size(acquireSnapshot());
}

int SectionScheduler::findSection(const Frame frame) const noexcept {
  return findSection(acquireSnapshot(), frame);
}

int SectionScheduler::findGuideTrigger(const Frame blockStart,
                                       const int blockFrames) const noexcept {
  return findGuideTrigger(acquireSnapshot(), blockStart, blockFrames);
}

bool SectionScheduler::getSection(const int index,
                                  Section &section) const noexcept {
  return getSection(acquireSnapshot(), index, section);
}

bool SectionScheduler::mapFrameToSourceFrame(
    const Frame frame, const Frame arrangementEndFrame,
    const Frame sourceEndFrame, Frame &sourceFrameOut) const noexcept {
  return mapFrameToSourceFrame(acquireSnapshot(), frame, arrangementEndFrame,
                               sourceEndFrame, sourceFrameOut);
}

SectionScheduler::BlockDecision SectionScheduler::evaluate(
    const Frame blockStart, const int blockFrames,
    const bool customArrangement, const int pendingSection,
    const int loopSection) const noexcept {
  return evaluate(acquireSnapshot(), blockStart, blockFrames,
                  customArrangement, pendingSection, loopSection);
}

} // namespace playworship::audio
