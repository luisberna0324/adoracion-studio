#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace playworship::learning {

// Observes changes to the real local demo. This class never operates controls,
// alters playback or accepts a state that was already present on step entry.
class DemoLearningProgress {
public:
  struct Stem {
    float volume = 1.0f;
    bool muted = false;
    bool solo = false;
  };
  struct Snapshot {
    juce::String songId;
    bool isDemo = false;
    bool playing = false;
    double position = 0.0;
    double startPosition = 0.0;
    // Processor revision changes when a transport control is explicitly used.
    std::uint64_t transportRevision = 0;
    // Stable channel order for this loaded demo, with linear fader values.
    std::vector<Stem> stems;
    int sectionLoopIndex = -1;
    bool practiceOpen = false;
    int practiceFocus = 0; // Same modes as Practice: 0 normal, 1 focus, 2 reduced.
    bool practiceLoopActive = false;
    // A, B or both are still present, even when Repeat is turned off.
    bool practiceLoopHasMarks = false;
  };

  void begin(const juce::String &stepId, const Snapshot &snapshot) {
    reset();
    if (!snapshot.isDemo || snapshot.songId.isEmpty()) return;
    step = stepId;
    baseline = previous = snapshot;
    activatedStems.resize(snapshot.stems.size(), false);
    cleanRegionSeen = snapshot.practiceOpen && !snapshot.practiceLoopActive && !snapshot.practiceLoopHasMarks;
    active = true;
  }

  void observe(const Snapshot &snapshot) {
    if (!active) return;
    // A different or unloaded song invalidates even completed evidence. The
    // integration must begin again after explicitly restoring the demo.
    if (!snapshot.isDemo || snapshot.songId != baseline.songId ||
        snapshot.stems.size() != baseline.stems.size()) {
      reset();
      return;
    }
    if (!complete) {
      const bool transportUsed = snapshot.transportRevision != previous.transportRevision;
      if (step == "demo-play" || step == "demo-resume")
        complete = transportUsed && !previous.playing && snapshot.playing;
      else if (step == "demo-pause")
        complete = transportUsed && previous.playing && !snapshot.playing &&
            std::abs(snapshot.position - snapshot.startPosition) > positionTolerance &&
            snapshot.position + positionTolerance >= previous.position;
      else if (step == "demo-stop")
        complete = transportUsed && !snapshot.playing &&
            std::isfinite(snapshot.position) && std::isfinite(snapshot.startPosition) &&
            snapshot.startPosition >= 0.0 &&
            std::abs(snapshot.position - snapshot.startPosition) <= positionTolerance;
      else if (step == "demo-fader") {
        for (size_t i = 0; i < snapshot.stems.size(); ++i)
          if (std::isfinite(snapshot.stems[i].volume) && std::isfinite(baseline.stems[i].volume) &&
              std::abs(snapshot.stems[i].volume - baseline.stems[i].volume) >= 0.025f)
            complete = true;
      } else if (step == "demo-mute" || step == "demo-solo") {
        for (size_t i = 0; i < snapshot.stems.size(); ++i) {
          const auto before = step == "demo-mute" ? previous.stems[i].muted : previous.stems[i].solo;
          const auto after = step == "demo-mute" ? snapshot.stems[i].muted : snapshot.stems[i].solo;
          if (!before && after) activatedStems[i] = true;
          if (before && !after && activatedStems[i]) complete = true;
        }
      } else if (step == "demo-section-loop") {
        if (previous.sectionLoopIndex < 0 && snapshot.sectionLoopIndex >= 0) activated = true;
        if (activated && previous.sectionLoopIndex >= 0 && snapshot.sectionLoopIndex < 0) complete = true;
      } else if (step == "demo-open-practice")
        complete = !previous.practiceOpen && snapshot.practiceOpen;
      else if (step == "demo-close-practice")
        complete = previous.practiceOpen && !snapshot.practiceOpen;
      else if (step == "demo-practice-focus") {
        if (!previous.practiceOpen || !snapshot.practiceOpen) activated = false;
        else {
          if (previous.practiceFocus != 1 && snapshot.practiceFocus == 1) activated = true;
          if (activated && snapshot.practiceFocus == 0) complete = true;
        }
      } else if (step == "demo-practice-ab") {
        if (!previous.practiceOpen || !snapshot.practiceOpen) {
          activated = false;
          cleanRegionSeen = snapshot.practiceOpen && !snapshot.practiceLoopActive && !snapshot.practiceLoopHasMarks;
        }
        else {
          if (cleanRegionSeen && !previous.practiceLoopActive && snapshot.practiceLoopActive && snapshot.practiceLoopHasMarks)
            activated = true;
          if (activated && !snapshot.practiceLoopActive && !snapshot.practiceLoopHasMarks) complete = true;
          if (!snapshot.practiceLoopActive && !snapshot.practiceLoopHasMarks) cleanRegionSeen = true;
        }
      }
    }
    previous = snapshot;
  }

  bool satisfied() const { return active && complete; }

  void reset() {
    step.clear();
    baseline = {};
    previous = {};
    activatedStems.clear();
    active = activated = complete = cleanRegionSeen = false;
  }

private:
  static constexpr double positionTolerance = 0.05;
  juce::String step;
  Snapshot baseline, previous;
  std::vector<bool> activatedStems;
  bool active = false, activated = false, complete = false, cleanRegionSeen = false;
};

} // namespace playworship::learning
