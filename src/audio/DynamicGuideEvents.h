#pragma once

#include "DynamicGuideEngine.h"
#include "SectionScheduler.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace playworship::audio {
// Callback-local, allocation-free event rendering. Input order breaks ties:
// primary announcements are registered first, then cues in persisted order.
class DynamicGuideEvents {
public:
  static constexpr int capacity = SectionScheduler::maxSections + SectionScheduler::maxCues + 4;
  struct Event { int outputOffset = 0; int triggerId = -1; };

  void add(const int offset, const int id) noexcept {
    if (count < capacity && id >= 0)
      events[static_cast<size_t>(count++)] = {std::max(0, offset), id};
  }
  void addCues(const SectionScheduler::Snapshot &snapshot,
               const SectionScheduler::Frame startFrame, const int sourceFrames,
               const int outputStart, const int outputFrames) noexcept {
    if (!snapshot.valid || sourceFrames <= 0 || outputFrames <= 0)
      return;
    for (int i = 0; i < snapshot.cueCount; ++i) {
      const auto &cue = snapshot.cues[static_cast<size_t>(i)];
      const auto relative = cue.frame - startFrame;
      if (cue.skipPlayback || relative < 0 || relative >= sourceFrames)
        continue;
      const int offset = std::clamp(static_cast<int>(std::llround(
          static_cast<double>(relative) * outputFrames / sourceFrames)),
          0, outputFrames - 1);
      add(outputStart + offset, cue.guideTriggerId);
    }
  }
  void render(DynamicGuideEngine &engine, juce::AudioBuffer<float> &output,
              const int start, const int frames) noexcept {
    // Insertion sort is stable, bounded, and never allocates in the callback.
    for (int i = 1; i < count; ++i) {
      const auto value = events[static_cast<size_t>(i)];
      int j = i;
      while (j > 0 && events[static_cast<size_t>(j - 1)].outputOffset > value.outputOffset) {
        events[static_cast<size_t>(j)] = events[static_cast<size_t>(j - 1)];
        --j;
      }
      events[static_cast<size_t>(j)] = value;
    }
    const int end = start + frames;
    int cursor = start;
    for (int i = 0; i < count; ++i) {
      const auto &event = events[static_cast<size_t>(i)];
      if (event.outputOffset < start || event.outputOffset >= end)
        continue;
      engine.render(output, cursor, event.outputOffset - cursor);
      engine.triggerForId(event.triggerId);
      cursor = event.outputOffset;
    }
    engine.render(output, cursor, end - cursor);
  }
private:
  int count = 0;
  std::array<Event, capacity> events{};
};
} // namespace playworship::audio
