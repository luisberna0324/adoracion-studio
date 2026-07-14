#pragma once

#include "TransportClock.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace playworship::audio {

class SectionScheduler {
public:
  using Frame = TransportClock::Frame;
  static constexpr int maxSections = 256;

  struct Section {
    Frame startFrame = 0;
    Frame endFrame = 0;
    Frame sourceStartFrame = 0;
    Frame sourceEndFrame = 0;
    Frame guideTriggerFrame = -1;
    int guideTriggerId = -1;
    bool triggerGuideOnEntry = false;
    bool insertedClip = false;
    bool muteInstruments = false;
    bool skipPlayback = false;
  };

  struct BlockDecision {
    int currentSection = -1;
    bool transition = false;
    bool stop = false;
    bool skippedSection = false;
    int targetSection = -1;
    Frame targetFrame = 0;
  };

  void publish(const std::vector<Section> &sections) noexcept;
  void clear() noexcept;

  int size() const noexcept;
  int findSection(Frame frame) const noexcept;
  int findGuideTrigger(Frame blockStart, int blockFrames) const noexcept;
  bool getSection(int index, Section &section) const noexcept;
  bool mapFrameToSourceFrame(Frame frame, Frame arrangementEndFrame,
                             Frame sourceEndFrame,
                             Frame &sourceFrameOut) const noexcept;

  BlockDecision evaluate(Frame blockStart, int blockFrames,
                         bool customArrangement, int pendingSection,
                         int loopSection) const noexcept;

private:
  struct Snapshot {
    int count = 0;
    std::array<Section, maxSections> sections{};
  };

  Snapshot readSnapshot() const noexcept;

  mutable std::mutex publishMutex;
  std::array<Snapshot, 3> snapshots{};
  std::atomic<int> publishedIndex{0};
  mutable std::atomic<int> readerIndex{-1};
};

} // namespace playworship::audio
