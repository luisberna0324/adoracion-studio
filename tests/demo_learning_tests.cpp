#include "ui/learning/DemoLearningProgress.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using Progress = playworship::learning::DemoLearningProgress;

namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Demo learning: " << message << '\n';
    std::exit(1);
  }
}

Progress::Snapshot demo() {
  Progress::Snapshot snapshot;
  snapshot.songId = "local-demo-song";
  snapshot.isDemo = true;
  snapshot.stems.resize(8);
  return snapshot;
}

void transportRequiresChangesDuringThisStep() {
  Progress progress;
  auto snapshot = demo();
  snapshot.playing = true;
  snapshot.position = 4.0;
  progress.begin("demo-play", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "entering while already playing cannot pass Play");
  snapshot.playing = false;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "Pause does not substitute for Play");
  snapshot.playing = true;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "a new real stopped-to-playing transition passes Play");

  progress.begin("demo-pause", snapshot);
  expect(!progress.satisfied(), "starting another step clears completed evidence");
  snapshot.playing = false;
  snapshot.position = 0.0;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "Stop returning to zero does not substitute for Pause");
  snapshot.playing = true;
  snapshot.position = 5.0;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  snapshot.playing = false;
  snapshot.position = 5.1;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "Pause preserves the observed playback position");

  progress.begin("demo-pause", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "an already paused song cannot pass Pause");
  progress.begin("demo-resume", snapshot);
  snapshot.playing = true;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "a fresh Play action passes the separate resume step");
  progress.begin("demo-pause", snapshot);
  snapshot.position = 90.0;
  snapshot.playing = false;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "natural playback end cannot stand in for an explicit Pause");
  progress.begin("demo-stop", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "a paused nonzero position alone does not pass Stop");
  snapshot.position = 0.0;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "Stop returns from a nonzero paused position");

  progress.begin("demo-stop", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "already stopped at the start does not pass Stop");
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "an explicit new Stop also passes when already at the start");
  progress.begin("demo-stop", snapshot);
  snapshot.playing = true;
  snapshot.position = 7.0;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  snapshot.position = 0.0;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "a loop wrap while playing cannot count as Stop");
  snapshot.position = 6.0;
  progress.observe(snapshot);
  snapshot.playing = false;
  snapshot.position = 0.0;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "a natural end cannot pass Stop without a control revision");
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "a playing song can be stopped at the start");

  snapshot.startPosition = 10.0;
  snapshot.position = 14.0;
  snapshot.playing = true;
  progress.begin("demo-stop", snapshot);
  snapshot.playing = false;
  snapshot.position = 0.0;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "zero cannot pass Stop when the song's configured Start is ten seconds");
  snapshot.position = 10.0;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "Stop accepts returning to the configured nonzero Start");

  progress.begin("demo-stop", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "already stopped at the configured Start still requires a fresh action");
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "an explicit Stop at an unchanged nonzero Start passes");

  snapshot.playing = true;
  snapshot.position = 10.02;
  progress.begin("demo-pause", snapshot);
  snapshot.playing = false;
  snapshot.position = 10.0;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "Stop near a configured nonzero Start must not be mistaken for Pause");
}

void mixerRequiresAChangeAndMatchedPairs() {
  Progress progress;
  auto snapshot = demo();
  progress.begin("demo-fader", snapshot);
  snapshot.stems[0].volume -= 0.001f;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "rounding noise cannot pass the fader exercise");
  snapshot.stems[0].volume = std::numeric_limits<float>::quiet_NaN();
  progress.observe(snapshot);
  expect(!progress.satisfied(), "invalid volume cannot pass the fader exercise");
  snapshot.stems[0].volume = 0.9f;
  progress.observe(snapshot);
  expect(progress.satisfied(), "a deliberate linear volume change passes the fader exercise");

  for (const auto *step : {"demo-mute", "demo-solo"}) {
    snapshot = demo();
    const auto flag = [step](Progress::Stem &stem) -> bool & {
      return juce::String(step) == "demo-mute" ? stem.muted : stem.solo;
    };
    flag(snapshot.stems[0]) = true;
    progress.begin(step, snapshot);
    progress.observe(snapshot);
    expect(!progress.satisfied(), "a preexisting active channel does not count");
    flag(snapshot.stems[0]) = false;
    progress.observe(snapshot);
    expect(!progress.satisfied(), "only disabling an initial Mute/Solo is not a full exercise");
    flag(snapshot.stems[1]) = true;
    progress.observe(snapshot);
    expect(!progress.satisfied(), "activating a channel still requires restoring it");
    flag(snapshot.stems[0]) = true;
    progress.observe(snapshot);
    flag(snapshot.stems[1]) = false;
    progress.observe(snapshot);
    expect(progress.satisfied(), "turning the same newly activated channel off completes its pair");

    snapshot = demo();
    flag(snapshot.stems[1]) = true;
    progress.begin(step, snapshot);
    flag(snapshot.stems[0]) = true;
    progress.observe(snapshot);
    flag(snapshot.stems[1]) = false;
    progress.observe(snapshot);
    expect(!progress.satisfied(), "activation and restoration on different channels cannot form a pair");
    flag(snapshot.stems[0]) = false;
    progress.observe(snapshot);
    expect(progress.satisfied(), "the originally activated channel must be restored");
  }
}

void sectionLoopRequiresEnableAndDisable() {
  Progress progress;
  auto snapshot = demo();
  snapshot.sectionLoopIndex = 1;
  progress.begin("demo-section-loop", snapshot);
  snapshot.sectionLoopIndex = -1;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "disabling a loop from before the step cannot pass");
  snapshot.sectionLoopIndex = 2;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "an enabled loop still needs to be disabled");
  snapshot.sectionLoopIndex = -1;
  progress.observe(snapshot);
  expect(progress.satisfied(), "enable and disable completes the section loop exercise");
}

void practiceRequiresOpeningFocusAndRestoration() {
  Progress progress;
  auto snapshot = demo();
  progress.begin("demo-open-practice", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "closed Practice cannot pass opening");
  snapshot.practiceOpen = true;
  progress.observe(snapshot);
  expect(progress.satisfied(), "opening the real Practice panel passes");
  progress.begin("demo-open-practice", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "already open Practice cannot pass a fresh opening exercise");

  snapshot.practiceFocus = 1;
  progress.begin("demo-practice-focus", snapshot);
  snapshot.practiceFocus = 0;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "restoring a preexisting focus does not count as focusing");
  snapshot.practiceFocus = 2;
  progress.observe(snapshot);
  snapshot.practiceFocus = 0;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "reducing an instrument is not focusing it");
  snapshot.practiceFocus = 1;
  progress.observe(snapshot);
  snapshot.practiceOpen = false;
  snapshot.practiceFocus = 0;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "closing Practice must not fake an explicit mix restoration");
  snapshot.practiceOpen = true;
  progress.observe(snapshot);
  snapshot.practiceFocus = 1;
  progress.observe(snapshot);
  snapshot.practiceFocus = 0;
  progress.observe(snapshot);
  expect(progress.satisfied(), "focus then normal mix while open passes the exercise");

  progress.begin("demo-close-practice", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "an open panel alone does not pass close");
  snapshot.practiceOpen = false;
  progress.observe(snapshot);
  expect(progress.satisfied(), "the observed close transition passes");
  progress.begin("demo-close-practice", snapshot);
  progress.observe(snapshot);
  expect(!progress.satisfied(), "already closed Practice does not pass a fresh close exercise");
}

void abRequiresANewRegionAndDeletionOfMarks() {
  Progress progress;
  auto snapshot = demo();
  snapshot.practiceOpen = true;
  snapshot.practiceLoopHasMarks = true;
  snapshot.practiceLoopActive = true;
  progress.begin("demo-practice-ab", snapshot);
  snapshot.practiceLoopActive = false;
  progress.observe(snapshot);
  snapshot.practiceLoopActive = true;
  progress.observe(snapshot);
  snapshot.practiceLoopActive = snapshot.practiceLoopHasMarks = false;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "toggling a preexisting region and deleting it is not creating a new A-B region");
  snapshot.practiceLoopHasMarks = true;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "only marking A is insufficient");
  snapshot.practiceLoopActive = true;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "creating an active A-B region still requires clearing it");
  snapshot.practiceLoopActive = false;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "Repeat off with retained A-B marks does not pass Clear region");
  snapshot.practiceLoopHasMarks = false;
  progress.observe(snapshot);
  expect(progress.satisfied(), "deleting both marks after a new region completes A-B");

  progress.begin("demo-practice-ab", snapshot);
  snapshot.practiceLoopActive = snapshot.practiceLoopHasMarks = true;
  progress.observe(snapshot);
  snapshot.practiceOpen = snapshot.practiceLoopActive = snapshot.practiceLoopHasMarks = false;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "automatic region cleanup on panel close cannot substitute for the trash control");
}

void identityAndStepBoundariesDiscardEvidence() {
  Progress progress;
  auto snapshot = demo();
  snapshot.isDemo = false;
  progress.begin("demo-play", snapshot);
  snapshot.playing = true;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "ordinary songs cannot earn demo evidence");

  snapshot = demo();
  progress.begin("demo-play", snapshot);
  snapshot.songId = "another-demo";
  snapshot.playing = true;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "a different demo song cannot complete this attempt");
  snapshot.songId = "local-demo-song";
  snapshot.playing = false;
  progress.observe(snapshot);
  snapshot.playing = true;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "returning after a song change needs a fresh baseline");

  snapshot = demo();
  progress.begin("demo-play", snapshot);
  snapshot.playing = true;
  ++snapshot.transportRevision;
  progress.observe(snapshot);
  expect(progress.satisfied(), "valid exercise starts confirmed");
  snapshot.isDemo = false;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "unloading or leaving the demo clears even completed evidence");

  snapshot = demo();
  progress.begin("demo-mute", snapshot);
  snapshot.stems[0].muted = true;
  progress.observe(snapshot);
  progress.begin("demo-mute", snapshot);
  snapshot.stems[0].muted = false;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "restarting the same step discards half-completed pairs");
  progress.begin("demo-fader", snapshot);
  snapshot.stems.pop_back();
  snapshot.stems[0].volume = 0.5f;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "a different channel layout invalidates channel comparisons");

  snapshot = demo();
  progress.begin("unknown-step", snapshot);
  snapshot.playing = true;
  snapshot.stems[0].volume = 0.2f;
  progress.observe(snapshot);
  expect(!progress.satisfied(), "unknown steps cannot accidentally become actionable");
  progress.reset();
  expect(!progress.satisfied(), "closing the guide clears evidence");
}
} // namespace

int main() {
  transportRequiresChangesDuringThisStep();
  mixerRequiresAChangeAndMatchedPairs();
  sectionLoopRequiresEnableAndDisable();
  practiceRequiresOpeningFocusAndRestoration();
  abRequiresANewRegionAndDeletionOfMarks();
  identityAndStepBoundariesDiscardEvidence();
  std::cout << "Demo learning: all regression checks passed\n";
}
