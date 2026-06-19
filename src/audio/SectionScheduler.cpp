#include "SectionScheduler.h"

#include <algorithm>
#include <limits>

namespace playworship::audio {

void SectionScheduler::publish(
    const std::vector<Section> &sections) noexcept {
  const std::lock_guard<std::mutex> lock(publishMutex);
  const int published = publishedIndex.load(std::memory_order_acquire);
  const int reading = readerIndex.load(std::memory_order_acquire);
  int writeIndex = 0;
  while (writeIndex == published || writeIndex == reading)
    ++writeIndex;

  auto &next = snapshots[static_cast<std::size_t>(writeIndex)];
  next.count = std::min(static_cast<int>(sections.size()), maxSections);
  for (int i = 0; i < next.count; ++i) {
    const auto &source = sections[static_cast<std::size_t>(i)];
    next.sections[static_cast<std::size_t>(i)] = {
        std::max<Frame>(0, source.startFrame),
        std::max(source.startFrame, source.endFrame)};
  }
  publishedIndex.store(writeIndex, std::memory_order_release);
}

void SectionScheduler::clear() noexcept { publish({}); }

SectionScheduler::Snapshot SectionScheduler::readSnapshot() const noexcept {
  for (;;) {
    const int index = publishedIndex.load(std::memory_order_acquire);
    readerIndex.store(index, std::memory_order_release);
    if (index != publishedIndex.load(std::memory_order_acquire))
      continue;
    Snapshot result = snapshots[static_cast<std::size_t>(index)];
    readerIndex.store(-1, std::memory_order_release);
    return result;
  }
}

int SectionScheduler::size() const noexcept {
  return readSnapshot().count;
}

int SectionScheduler::findSection(const Frame frame) const noexcept {
  const auto local = readSnapshot();
  if (local.count <= 0)
    return -1;

  for (int i = 0; i < local.count; ++i) {
    const auto &section = local.sections[static_cast<std::size_t>(i)];
    if (frame >= section.startFrame && frame < section.endFrame)
      return i;
  }

  int nearest = 0;
  Frame nearestDistance = std::numeric_limits<Frame>::max();
  for (int i = 0; i < local.count; ++i) {
    const auto &section = local.sections[static_cast<std::size_t>(i)];
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

SectionScheduler::BlockDecision SectionScheduler::evaluate(
    const Frame blockStart, const int blockFrames,
    const bool customArrangement, const int pendingSection,
    const int loopSection) const noexcept {
  BlockDecision decision;
  const auto local = readSnapshot();
  if (local.count <= 0 || blockFrames <= 0)
    return decision;

  const auto findInSnapshot = [&local](const Frame frame) noexcept {
    for (int i = 0; i < local.count; ++i) {
      const auto &section = local.sections[static_cast<std::size_t>(i)];
      if (frame >= section.startFrame && frame < section.endFrame)
        return i;
    }
    return -1;
  };

  decision.currentSection = findInSnapshot(blockStart);
  const bool validLoop = loopSection >= 0 && loopSection < local.count;
  if (validLoop) {
    const auto &loop = local.sections[static_cast<std::size_t>(loopSection)];
    if (blockStart >= loop.startFrame && blockStart < loop.endFrame &&
        blockStart + blockFrames >= loop.endFrame) {
      decision.currentSection = loopSection;
      decision.transition = true;
      decision.targetSection = loopSection;
      decision.targetFrame = loop.startFrame;
      return decision;
    }
    if (blockStart >= loop.startFrame && blockStart < loop.endFrame)
      return decision;
  }

  if (decision.currentSection < 0)
    return decision;

  const auto &current =
      local.sections[static_cast<std::size_t>(decision.currentSection)];
  if (blockStart + blockFrames < current.endFrame)
    return decision;

  const bool validPending =
      pendingSection >= 0 && pendingSection < local.count &&
      pendingSection != decision.currentSection;
  if (validPending) {
    decision.transition = true;
    decision.targetSection = pendingSection;
    decision.targetFrame =
        local.sections[static_cast<std::size_t>(pendingSection)].startFrame;
    return decision;
  }

  if (customArrangement) {
    const int next = decision.currentSection + 1;
    if (next < local.count) {
      decision.transition = true;
      decision.targetSection = next;
      decision.targetFrame =
          local.sections[static_cast<std::size_t>(next)].startFrame;
    } else {
      decision.stop = true;
    }
  }
  return decision;
}

} // namespace playworship::audio
