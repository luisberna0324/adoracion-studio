#pragma once

#include "TransportClock.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace playworship::audio {

class SectionScheduler {
public:
  using Frame = TransportClock::Frame;
  static_assert(std::atomic<Frame>::is_always_lock_free,
                "SectionScheduler requires lock-free frame atomics");
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
    // Exact source-timeline offset within the evaluated block. A value equal
    // to blockFrames means the transition lands immediately after the block;
    // -1 means that no transition was scheduled.
    int transitionOffsetFrames = -1;
  };

  struct Snapshot {
    std::uint64_t generation = 0;
    int count = 0;
    std::array<Section, maxSections> sections{};
    bool valid = false;
  };

  void publish(const std::vector<Section> &sections) noexcept;
  void clear() noexcept;

  // Bounded, allocation-free acquisition. Under exceptional publication
  // contention this returns an invalid snapshot instead of spinning. A
  // realtime caller should keep using its previous valid snapshot.
  Snapshot acquireSnapshot() const noexcept;

  // Snapshot helpers let the audio callback acquire once and use one coherent
  // generation for every decision made during the block.
  static int size(const Snapshot &snapshot) noexcept;
  static int findSection(const Snapshot &snapshot, Frame frame) noexcept;
  static int findGuideTrigger(const Snapshot &snapshot, Frame blockStart,
                              int blockFrames) noexcept;
  static bool getSection(const Snapshot &snapshot, int index,
                         Section &section) noexcept;
  static bool mapFrameToSourceFrame(const Snapshot &snapshot, Frame frame,
                                    Frame arrangementEndFrame,
                                    Frame sourceEndFrame,
                                    Frame &sourceFrameOut) noexcept;
  static BlockDecision evaluate(const Snapshot &snapshot, Frame blockStart,
                                int blockFrames, bool customArrangement,
                                int pendingSection,
                                int loopSection) noexcept;

  // Compatibility helpers. Realtime code should prefer acquireSnapshot() once
  // followed by the overloads above.
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
  static constexpr int snapshotBankCount = 3;
  static constexpr int maxSnapshotAcquireAttempts = snapshotBankCount;

  struct AtomicSection {
    std::atomic<Frame> startFrame{0};
    std::atomic<Frame> endFrame{0};
    std::atomic<Frame> sourceStartFrame{0};
    std::atomic<Frame> sourceEndFrame{0};
    std::atomic<Frame> guideTriggerFrame{-1};
    std::atomic<int> guideTriggerId{-1};
    std::atomic<std::uint32_t> flags{0};

    void store(const Section &section) noexcept;
    Section load() const noexcept;
  };

  struct AtomicSnapshotBank {
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint64_t> generation{0};
    std::atomic<int> count{0};
    std::array<AtomicSection, maxSections> sections{};
  };

  bool tryReadBank(int bankIndex, Snapshot &snapshot) const noexcept;

  mutable std::mutex publishMutex;
  std::array<AtomicSnapshotBank, snapshotBankCount> snapshotBanks{};
  std::atomic<int> publishedIndex{0};
  std::uint64_t nextGeneration = 1;
};

} // namespace playworship::audio
