#include "audio/LyricsCueScheduler.h"
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
using Scheduler = playworship::audio::LyricsCueScheduler;
void expect(bool ok, const char* text) {
  if (!ok) { std::cerr << "FAILED: " << text << '\n'; std::exit(1); }
}
std::vector<Scheduler::Event> drain(Scheduler& scheduler) {
  std::vector<Scheduler::Event> result;
  Scheduler::Event event;
  while (scheduler.popEvent(event)) result.push_back(event);
  return result;
}
void expectIndices(Scheduler& scheduler, std::initializer_list<int> wanted, const char* text) {
  const auto events = drain(scheduler);
  std::vector<int> indices;
  for (const auto& event : events) indices.push_back(event.cueIndex);
  expect(indices == std::vector<int>(wanted), text);
}
void prepare(Scheduler& scheduler) {
  scheduler.setDeliveryState(1, true);
  scheduler.publish({{0, 0}, {100, 1}, {150, 2}, {200, 3}, {300, 4}}, 1, 1);
  scheduler.beginBlock(scheduler.acquireSnapshot(), false);
}
void testCrossingsAndPause() {
  Scheduler scheduler; prepare(scheduler);
  scheduler.renderedSpan(0, 180);
  expectIndices(scheduler, {0, 1, 2}, "all dense cues crossed in a real span fire once");
  scheduler.renderedSpan(180, 200);
  expectIndices(scheduler, {}, "end-exclusive cue waits until its actual frame");
  scheduler.beginBlock(scheduler.acquireSnapshot(), false); // paused: no rendered span
  expectIndices(scheduler, {}, "pause cannot fire at a stationary playhead");
  scheduler.renderedSpan(200, 240);
  expectIndices(scheduler, {3}, "resume crosses the boundary cue once, no previous replay");
  scheduler.discontinuity(240, true, false);
  expectIndices(scheduler, {}, "same-position source revalidation on custom-arrangement resume does not repeat lyrics");
  scheduler.discontinuity(240, true, true);
  expectIndices(scheduler, {3}, "explicit seek at the same position synchronises the current target");
  scheduler.renderedSpan(240, 300);
  expectIndices(scheduler, {}, "no repeated current cue on normal progress");
  scheduler.beginBlock(scheduler.acquireSnapshot(), true);
  scheduler.discontinuity(0, false);
  expectIndices(scheduler, {}, "stop re-arms the start without projecting a new cue");
  scheduler.beginBlock(scheduler.acquireSnapshot(), false);
  scheduler.renderedSpan(0, 20);
  expectIndices(scheduler, {0}, "play after stop starts a fresh musical pass");
}
void testEffectiveSeekLoopAndTempo() {
  Scheduler scheduler; prepare(scheduler);
  scheduler.renderedSpan(0, 50); drain(scheduler);
  // A requested destination does nothing until the renderer confirms it.
  scheduler.renderedSpan(50, 100);
  expectIndices(scheduler, {}, "uncommitted seek cannot project its requested destination");
  scheduler.discontinuity(250);
  expectIndices(scheduler, {3}, "forward seek emits only current <= destination, omitting crossed marks");
  scheduler.renderedSpan(250, 320);
  expectIndices(scheduler, {4}, "post-seek actual suffix advances normally");
  scheduler.discontinuity(120);
  expectIndices(scheduler, {1}, "backward seek restores current cue");
  scheduler.renderedSpan(120, 200);
  expectIndices(scheduler, {2}, "backward seek re-arms later cues");
  scheduler.discontinuity(120);
  scheduler.renderedSpan(120, 160);
  expectIndices(scheduler, {1, 2}, "loop reinstates start cue then repeats marks in the new pass");
  scheduler.discontinuity(120);
  scheduler.discontinuity(120);
  expectIndices(scheduler, {1, 1}, "same-position explicit loop creates a fresh pass every time");
  scheduler.renderedSpan(120, 145); drain(scheduler);
  scheduler.renderedSpan(145, 180); // faster tempo consumes more original musical frames
  expectIndices(scheduler, {2}, "tempo changes keep cue anchor in real musical frames");
}
void testEditingDisconnectAndReplacement() {
  Scheduler scheduler; prepare(scheduler);
  scheduler.renderedSpan(0, 180);
  const auto old = drain(scheduler);
  expect(old.size() == 3 && !scheduler.isCurrent(old[0]) && scheduler.isCurrent(old[2]),
      "expired crossings are not replayed by a delayed worker");
  scheduler.setMaximumDeliveryAgeFrames(200);
  scheduler.beginBlock(scheduler.acquireSnapshot(), false, 201);
  expect(!scheduler.isCurrent(old[2]), "delayed worker discards a cue older than its audio-frame deadline");
  scheduler.setDeliveryState(2, false);
  expect(!scheduler.isCurrent(old[2]), "disconnect invalidates already queued cue");
  scheduler.renderedSpan(180, 320);
  expectIndices(scheduler, {}, "disconnected playback discards commands while advancing state");
  scheduler.setDeliveryState(3, true);
  scheduler.renderedSpan(320, 350);
  expectIndices(scheduler, {}, "reconnect does not replay disconnected history");
  scheduler.publish({{0, 0}, {340, 1}, {400, 2}}, 2, 1);
  scheduler.beginBlock(scheduler.acquireSnapshot(), false);
  scheduler.renderedSpan(350, 380);
  expectIndices(scheduler, {}, "editing or selecting a past mark never projects it");
  scheduler.renderedSpan(380, 410);
  auto edited = drain(scheduler);
  expect(edited.size() == 1 && edited[0].cueIndex == 2, "edited future cue uses its original stable index");
  scheduler.publish({{0, 9}}, 3, 2);
  expect(!scheduler.isCurrent(edited[0]), "song replacement invalidates previous queued targets");
  scheduler.beginBlock(scheduler.acquireSnapshot(), false);
  scheduler.renderedSpan(0, 100);
  expectIndices(scheduler, {9}, "new song starts independently of the previous playhead");
}
void testBeforeFirstMarkIsBlank() {
  Scheduler scheduler;
  scheduler.setDeliveryState(1, true);
  scheduler.publish({{100, 0}, {200, 1}}, 1, 1);
  scheduler.beginBlock(scheduler.acquireSnapshot(), false);
  scheduler.renderedSpan(0, 50);
  expectIndices(scheduler, {-1}, "initial span before first active mark clears old lyrics");
  scheduler.discontinuity(250); expectIndices(scheduler, {1}, "seek establishes a later current lyric");
  scheduler.discontinuity(50); expectIndices(scheduler, {-1}, "seek before first mark explicitly hides previous future lyric");
  scheduler.renderedSpan(50, 150); expectIndices(scheduler, {0}, "first mark projects normally after blank lead-in");
  scheduler.discontinuity(50); scheduler.renderedSpan(50, 90);
  expectIndices(scheduler, {-1}, "loop start before first mark reinstates blank lead-in");
  scheduler.publish({}, 2, 1); scheduler.beginBlock(scheduler.acquireSnapshot(), false);
  scheduler.discontinuity(0); scheduler.renderedSpan(0, 100);
  expectIndices(scheduler, {}, "empty or disabled lyrics never issue an implicit hide");
}
void testPublicationAndBounds() {
  Scheduler scheduler;
  std::vector<Scheduler::Cue> cues;
  for (int i = Scheduler::maxCues - 1; i >= 0; --i) cues.push_back({i * 2, i});
  scheduler.setDeliveryState(1, true);
  scheduler.publish(cues, 1, 1);
  scheduler.beginBlock(scheduler.acquireSnapshot(), false);
  scheduler.renderedSpan(0, 2 * Scheduler::maxCues);
  const auto events = drain(scheduler);
  expect(events.size() == Scheduler::maxCues && scheduler.droppedEvents() == 0,
      "maximum dense snapshot fits bounded realtime event storage");
  for (int i = 0; i < Scheduler::maxCues; ++i)
    expect(events[static_cast<size_t>(i)].cueIndex == i, "publication sorts time without changing identity");
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (std::uint64_t rev = 2; rev < 6000; ++rev)
      scheduler.publish({{static_cast<Scheduler::Frame>(rev), static_cast<int>(rev)}}, rev, 1);
    done.store(true);
  });
  while (!done.load()) {
    const auto snapshot = scheduler.acquireSnapshot();
    if (snapshot.valid && snapshot.revision > 1)
      expect(snapshot.count == 1 && snapshot.cues[0].frame == static_cast<Scheduler::Frame>(snapshot.revision) &&
             snapshot.cues[0].index == static_cast<int>(snapshot.revision),
             "bounded snapshot acquisition cannot expose mixed publication fields");
  }
  writer.join();
}
}
int main() {
  testCrossingsAndPause(); testEffectiveSeekLoopAndTempo();
  testEditingDisconnectAndReplacement(); testBeforeFirstMarkIsBlank(); testPublicationAndBounds();
  std::cout << "Lyrics cue scheduler tests passed\n";
}
