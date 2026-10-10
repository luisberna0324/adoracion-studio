#include "network/RemoteSession.h"
#include "network/PlaybackSession.h"
#include "audio/PracticeLoop.h"
#include "ui/panels/PracticeAccompaniment.h"
#include <JuceHeader.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>

using playworship::remote::DiscoveredServer;
using playworship::remote::RemoteSession;

template <typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 5000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

// A disconnected or mirrored UI must never mutate its local audio processor.
struct TestProcessor {
  float volume = 0.75f;
  int writes = 0;
  int practiceStem = -1;
  int practiceMode = 0;
  int practiceWrites = 0;
  bool dynamicGuideEnabled = false, guideAssets = false, guideMuted = true;
  bool metronomeEnabled = false, clickDivisionEnabled = false, metronomeMuted = true;
  int clickDivision = 0;
  std::vector<bool> stemMutes{false, true, true};
  bool loaded = true;
  bool playing = false;
  double position = 4.0;
  uint64_t transportRevision = 0;
  playworship::audio::PracticeLoop practiceLoop;
  juce::String practiceLoopError;
  int practiceLoopWrites = 0;
  juce::String automationPartId;
  int automationWrites = 0;
  bool isSectionAutomationEditing() const { return automationPartId.isNotEmpty(); }
  juce::String getSectionAutomationEditingPartId() const { return automationPartId; }
  juce::String getSectionAutomationEditingPartName() const { return "Verse"; }
  bool beginSectionAutomationEditing(const juce::String &partId) {
    if (isSectionAutomationEditing() || partId.isEmpty()) return false;
    automationPartId = partId; ++automationWrites; return true;
  }
  bool finishSectionAutomationEditing(bool) {
    if (!isSectionAutomationEditing()) return false;
    automationPartId.clear(); ++automationWrites; return true;
  }
  bool clearSectionAutomation(const juce::String &) { ++automationWrites; return true; }
  bool isStemSectionAutomated(int) const { return false; }
  bool isPadsSectionAutomated() const { return false; }
  double getSongStartSeconds() const { return 0.0; }
  double getSongEndSeconds() const { return 100.0; }
  double getMinimumPracticeLoopDuration() const { return 0.25; }
  bool isPracticeLoopActive() const { return practiceLoop.snapshot().active; }
  bool hasPracticeLoopPreparation() const { return practiceLoop.snapshot().prepared; }
  juce::Range<double> getPracticeLoopRange() const {
    const auto range = practiceLoop.snapshot();
    return range.active ? juce::Range<double>(range.startSeconds, range.endSeconds) : juce::Range<double>{};
  }
  uint64_t getPracticeLoopRevision() const { return practiceLoop.revision(); }
  juce::String getPracticeLoopError() const { return practiceLoopError; }
  bool preparePracticeLoop(double start) {
    if (!loaded || !std::isfinite(start) || start < 0.0 || start >= 100.0 || isPracticeLoopActive())
      return false;
    practiceLoop.setRange(start, start); ++practiceLoopWrites; return true;
  }
  bool setPracticeLoopRange(double start, double end) {
    practiceLoopError.clear();
    if (!loaded || !playworship::audio::PracticeLoop::isValidRange(start, end, 0.0, 100.0)) {
      practiceLoopError = "invalid-range"; return false;
    }
    practiceLoop.setRange(start, end); ++practiceLoopWrites; return true;
  }
  void clearPracticeLoop() { practiceLoop.clear(); practiceLoopError.clear(); ++practiceLoopWrites; }
  std::vector<juce::String> transportCalls;
  uint64_t getTransportControlRevision() const { return transportRevision; }
  void markTransportControlIntent() { ++transportRevision; }
  void pauseSong() { playing = false; ++transportRevision; transportCalls.push_back("pause"); }
  void playSong() { playing = true; ++transportRevision; transportCalls.push_back("play"); }
  void stopSong() { playing = false; position = 0.0; ++transportRevision; transportCalls.push_back("stop"); }
  void seekSongTo(double seconds) { position = juce::jlimit(0.0, 100.0, seconds); ++transportRevision; transportCalls.push_back("seek"); }
  void setPracticeListeningMode(int index, int mode) {
    practiceStem = mode == 0 ? -1 : index;
    practiceMode = mode;
    ++practiceWrites;
  }
  int getPracticeListeningStem() const { return practiceStem; }
  int getPracticeListeningMode() const { return practiceMode; }
  float getStemVolume(int) const { return volume; }
  float getStemGain(int) const { return 1.0f; }
  float getMasterVolume() const { return volume; }
  float getMasterGain() const { return 1.0f; }
  float getMetronomeVolume() const { return volume; }
  float getMetronomeGain() const { return 1.0f; }
  float getDynamicGuideVolume() const { return volume; }
  float getDynamicGuideGain() const { return 1.0f; }
  bool isDynamicGuideEnabled() const { return dynamicGuideEnabled; }
  bool hasDynamicGuideAssets() const { return guideAssets; }
  bool isDynamicGuideMuted() const { return guideMuted; }
  bool isMetronomeEnabled() const { return metronomeEnabled; }
  bool isClickDivisionEnabled() const { return clickDivisionEnabled; }
  bool isMetronomeMuted() const { return metronomeMuted; }
  int getClickDivision() const { return clickDivision; }
  bool isStemMuted(int index) const { return stemMutes.at(index); }
  void setDynamicGuideMuted(bool value) { guideMuted = value; ++writes; }
  void setMetronomeEnabled(bool value) { metronomeEnabled = value; ++writes; }
  void setClickDivisionEnabled(bool value) { clickDivisionEnabled = value; ++writes; }
  void setMetronomeMuted(bool value) { metronomeMuted = value; ++writes; }
  void setClickDivision(int value) { clickDivision = value; ++writes; }
  void setStemMute(int index, bool value) { stemMutes.at(index) = value; ++writes; }
  float getPadsVolume() const { return volume; }
  float getPadsGain() const { return 1.0f; }
  void setStemVolume(int, float v) { volume = v; ++writes; }
  int getLoadedStemCount() const { return 1; }
  float getStemLevel(int) const { return 0.1f; }
  bool hasLoadedSong() const { return loaded; }
  double getSongCurrentPositionSeconds() const { return position; }
  double getPlaybackTempoBpm() const { return 120.0; }
  double getSongLengthSeconds() const { return 100.0; }
  bool isSongPlaying() const { return playing; }
};
bool testPracticeScrubLease() {
  const auto expect = [](bool ok, const char *message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  for (const bool initiallyPlaying : {false, true}) {
    TestProcessor processor;
    processor.playing = initiallyPlaying;
    PracticeScrubLease lease;
    if (!expect(lease.begin(processor, "song", "gesture"), "Scrub begin must acquire a lease")) return false;
    const auto afterBegin = processor.transportCalls.size();
    if (!expect(!processor.playing && afterBegin == (initiallyPlaying ? 1u : 0u),
                "Scrub only pauses a playing song")) return false;
    if (!expect(lease.begin(processor, "song", "gesture") &&
                processor.transportCalls.size() == afterBegin,
                "Repeated begin preserves the original playback intent")) return false;
    if (!expect(!lease.finish(processor, "song", "other-token", 50.0) && lease.token == "gesture",
                "Another token cannot seek or consume this lease")) return false;
    if (!expect(lease.finish(processor, "song", "gesture", 28.0) && processor.position == 28.0 &&
                processor.playing == initiallyPlaying,
                "Scrub finish seeks and restores only an originally playing song")) return false;
    const std::vector<juce::String> expected = initiallyPlaying
        ? std::vector<juce::String>{"pause", "seek", "play"}
        : std::vector<juce::String>{"seek"};
    if (!expect(processor.transportCalls == expected,
                "Scrub transport order must be pause, one seek, then optional play")) return false;
    const auto afterFinish = processor.transportCalls.size();
    if (!expect(!lease.finish(processor, "song", "gesture", 70.0) &&
                processor.transportCalls.size() == afterFinish,
                "Repeated finish must not seek or play again")) return false;
  }
  for (const int supersedingAction : {0, 1, 2, 3}) {
    TestProcessor processor;
    processor.playing = true;
    PracticeScrubLease lease;
    if (!lease.begin(processor, "song", "gesture")) return false;
    if (supersedingAction == 0) processor.stopSong();
    if (supersedingAction == 1) processor.seekSongTo(65.0);
    if (supersedingAction == 2) processor.pauseSong();
    if (supersedingAction == 3) ++processor.transportRevision; // Same-song reload.
    const auto before = processor.transportCalls.size();
    const double position = processor.position;
    if (!expect(!lease.finish(processor, "song", "gesture", 28.0) &&
                processor.transportCalls.size() == before && processor.position == position && !processor.playing,
                "A superseding control or reload prevents stale seek and resume")) return false;
  }
  {
    TestProcessor processor;
    processor.playing = true;
    PracticeScrubLease lease;
    lease.begin(processor, "song", "gesture");
    if (!expect(!lease.finish(processor, "other-song", "gesture", 28.0) &&
                processor.transportCalls == std::vector<juce::String>{"pause"},
                "A song identity change prevents stale finish")) return false;
  }
  for (const bool restore : {false, true}) {
    TestProcessor processor;
    processor.playing = true;
    PracticeScrubLease lease;
    lease.begin(processor, "song", "gesture");
    if (!expect(lease.cancel(processor, "song", "gesture", restore) && processor.playing == restore &&
                processor.position == 4.0,
                "Cancel can restore a valid pause without seeking")) return false;
    const auto calls = processor.transportCalls.size();
    lease.cancel(processor, "song", "gesture", true);
    if (!expect(processor.transportCalls.size() == calls, "Repeated cancel is harmless")) return false;
  }
  {
    TestProcessor processor;
    processor.playing = true;
    PracticeScrubLease first, second;
    first.begin(processor, "song", "first");
    second.begin(processor, "song", "second");
    const auto afterBegin = processor.transportCalls.size();
    first.cancel(processor, "song", "first", true);
    if (!expect(!processor.playing && processor.transportCalls.size() == afterBegin,
                "Older client cleanup cannot resume over a newer paused scrub")) return false;
    if (!expect(second.finish(processor, "song", "second", 65.0) && !processor.playing,
                "A second client sees the paused server and does not resume it")) return false;
    const auto calls = processor.transportCalls.size();
    if (!expect(!first.finish(processor, "song", "first", 28.0) &&
                processor.position == 65.0 && processor.transportCalls.size() == calls,
                "The other client's seek invalidates the older lease")) return false;
  }
  {
    TestProcessor processor;
    processor.playing = true;
    PracticeScrubLease lease;
    lease.begin(processor, "song", "gesture");
    processor.stopSong();
    const auto calls = processor.transportCalls.size();
    lease.cancel(processor, "song", "gesture", true);
    if (!expect(!processor.playing && processor.transportCalls.size() == calls,
                "Disconnect cleanup cannot undo another client's Stop")) return false;
  }
  return true;
}

bool testPracticeScrubAdapter() {
  TestProcessor local;
  local.playing = true;
  BasicPlaybackSession<TestProcessor> session(local);
  session.beginPracticeScrub();
  session.beginPracticeScrub();
  if (local.transportCalls != std::vector<juce::String>{"pause"}) return false;
  if (!session.finishPracticeScrub(23.0) ||
      local.transportCalls != std::vector<juce::String>{"pause", "seek", "play"}) return false;
  if (session.finishPracticeScrub(70.0)) return false;
  session.beginPracticeScrub();
  local.stopSong();
  const auto writes = local.transportCalls.size();
  if (session.finishPracticeScrub(70.0) || local.transportCalls.size() != writes) return false;

  session.setRemote(true);
  struct Sent { juce::String id, action; juce::var value; };
  std::vector<Sent> sent;
  session.onCommand = [&](const juce::String &id, const juce::String &action,
                          const juce::var &value, const juce::String &) {
    sent.push_back({id, action, value});
  };
  session.beginPracticeScrub();
  if (!sent.empty()) return false;
  const auto state = juce::JSON::parse(R"({"song":{"id":"song"},"position":20,"length":100,"playing":true,"tempo":120})");
  session.applyState(state);
  session.beginPracticeScrub();
  session.beginPracticeScrub();
  if (sent.size() != 1 || sent[0].action != "transport.scrubBegin" ||
      sent[0].value.getProperty("songId", {}).toString() != "song" ||
      sent[0].value.getProperty("token", {}).toString().isEmpty()) return false;
  const auto token = sent[0].value.getProperty("token", {}).toString();
  if (!session.finishPracticeScrub(45.0) || sent.size() != 2 ||
      sent[1].action != "transport.scrubEnd" ||
      sent[1].value.getProperty("token", {}).toString() != token ||
      static_cast<double>(sent[1].value.getProperty("seconds", -1)) != 45.0 ||
      local.transportCalls.size() != writes) return false;
  const auto rejectedEndId = sent[1].id;
  if (session.wasPracticeScrubFinishRejected()) return false;
  session.applyAcknowledgement("other-peer-end", false);
  if (session.wasPracticeScrubFinishRejected()) return false;
  session.applyAcknowledgement(rejectedEndId, false);
  if (!session.wasPracticeScrubFinishRejected()) return false;
  session.applyAcknowledgement("other-peer-end", true);
  if (!session.wasPracticeScrubFinishRejected()) return false;
  session.beginPracticeScrub();
  if (session.wasPracticeScrubFinishRejected()) return false;
  session.applyAcknowledgement(rejectedEndId, false);
  if (session.wasPracticeScrubFinishRejected()) return false;
  session.cancelPracticeScrub(true);
  if (sent.size() != 4 || sent.back().action != "transport.scrubCancel" ||
      !static_cast<bool>(sent.back().value.getProperty("restorePlayback", false))) return false;
  session.beginPracticeScrub();
  session.applyAcknowledgement(sent.back().id, false);
  if (session.finishPracticeScrub(70.0) || sent.size() != 5) return false;
  session.beginPracticeScrub();
  session.setConnected(false);
  session.cancelPracticeScrub(true);
  if (session.finishPracticeScrub(70.0) || sent.size() != 6) return false;
  session.applyState(state);
  session.beginPracticeScrub();
  auto changedSong = state.clone();
  changedSong.getProperty("song", {}).getDynamicObject()->setProperty("id", "other-song");
  session.applyState(changedSong);
  if (session.finishPracticeScrub(70.0) || sent.size() != 7) return false;
  return local.transportCalls.size() == writes;
}

bool testPlaybackAdapter() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  session.setStemVolume(0, 0.5f);
  if (local.writes != 1 || session.getStemVolume(0) != 0.5f) return false;
  session.setRemote(true);
  int sent = 0;
  session.onCommand = [&](const juce::String &, const juce::String &action, const juce::var &value, const juce::String &target) {
    if (action == "mix.volume" && target == "included" && static_cast<double>(value) == 0.25) ++sent;
  };
  session.setStemVolume(0, 0.25f);
  if (sent || local.writes != 1) return false;
  session.applyState(juce::JSON::parse(R"({"song":{"id":"song","bpm":120,"stems":[{"id":"excluded","include":false},{"id":"included","include":true}]},"length":100,"position":18,"playing":false,"tempo":120,"mixer":[{"target":"included","volume":0.6}]})"));
  if (session.getLoadedStemCount() != 1 || std::abs(session.getStemVolume(0)-0.6f) > 0.001f) return false;
  session.setStemVolume(0, 0.25f);
  if (sent != 1 || local.writes != 1) return false;
  session.applyMeters(juce::JSON::parse(R"({"position":19,"playing":false,"levels":{"included":{"l":0.8,"r":0.8}}})"));
  if (std::abs(session.getStemLevel(0)-0.8f)>0.001f || session.getSongCurrentPositionSeconds()!=19) return false;
  session.setConnected(false);
  session.setStemVolume(0, 0.25f);
  if (session.getStemLevel(0)!=0 || session.getSongCurrentPositionSeconds()!=19 || sent!=1) return false;
  session.setRemote(false);
  return session.getStemVolume(0)==0.5f && local.writes==1;
}

bool testSectionAutomationAdapter() {
  const auto expect = [](bool ok, const char *message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> owner(local), observer(local);
  owner.sectionAutomationCanEdit = [] { return false; };
  owner.setStemVolume(0, 0.1f);
  if (!expect(!owner.canEditSectionAutomation() &&
              !owner.beginSectionAutomationEditing("verse") && local.writes == 0,
              "Server controls respect a remote editor's exclusive ownership")) return false;
  owner.sectionAutomationCanEdit = {};
  if (!owner.beginSectionAutomationEditing("verse") ||
      !owner.finishSectionAutomationEditing(false) || local.automationWrites != 2) return false;
  owner.setRemote(true);
  observer.setRemote(true);
  struct Sent { juce::String id, action, target; juce::var value; };
  std::vector<Sent> sent;
  owner.onCommand = [&](const auto &id, const auto &action, const auto &value, const auto &target) {
    sent.push_back({id, action, target, value});
  };
  int observerCommands = 0;
  observer.onCommand = [&](const auto &, const auto &, const auto &, const auto &) { ++observerCommands; };
  auto state = juce::JSON::parse(R"({"seq":1,"song":{"id":"song","stems":[{"id":"stem","include":true},{"id":"renamed-click","name":"Track","category":"Other","fileName":"Click.flac","include":true}]},"settings":{"arrangement":{"parts":[{"id":"verse","name":"Verse"}]}},"sectionAutomation":{"editing":false},"mixer":[{"target":"stem","volume":0.5,"gain":1,"sectionAutomated":true}],"ui":{"isPadsSectionAutomated":true}})");
  owner.applyState(state);
  observer.applyState(state);
  if (!expect(!owner.beginSectionAutomationEditing("unknown") &&
              owner.beginSectionAutomationEditing("verse") &&
              owner.isSectionAutomationEditing() && owner.canEditSectionAutomation() &&
              owner.getSectionAutomationEditingPartId() == "verse" &&
              owner.getSectionAutomationEditingPartName() == "Verse",
              "Begin shows the selected section immediately without waiting for a network echo")) return false;
  const auto leaseId = sent.back().id;
  if (!expect(sent.back().action == "automation.begin" &&
              sent.back().target == "verse" &&
              sent.back().value.getProperty("songId", {}).toString() == "song",
              "Automation begin binds its intent to a stable section and song")) return false;
  owner.setStemVolume(0, 0.25f);
  if (!expect(sent.back().action == "mix.volume" &&
              sent.back().value.getProperty("songId", {}).toString() == "song" &&
              sent.back().value.getProperty("editCommandId", {}).toString() == leaseId &&
              static_cast<double>(sent.back().value.getProperty("controlValue", {})) == 0.25 &&
              owner.getStemVolume(0) == 0.25f,
              "Draft faders retain their preview while identifying the exact editing lease")) return false;
  owner.applyAcknowledgement(leaseId, true);
  state.getDynamicObject()->setProperty("seq", 2);
  auto *editing = state.getProperty("sectionAutomation", {}).getDynamicObject();
  editing->setProperty("editing", true);
  editing->setProperty("partId", "verse");
  editing->setProperty("partName", "Verse");
  editing->setProperty("commandId", leaseId);
  owner.applyState(state);
  observer.applyState(state);
  observer.send("mix.volume", 0.7, "stem");
  observer.send("pads.volume", 0.7);
  if (!expect(owner.canEditSectionAutomation() && !observer.canEditSectionAutomation() &&
              observer.isSectionAutomationEditing() && observerCommands == 0 &&
              !observer.beginSectionAutomationEditing("verse") &&
              !observer.finishSectionAutomationEditing(true) &&
              !observer.clearSectionAutomation("verse"),
              "Other clients can observe a draft but cannot write or finish it")) return false;
  if (!expect(owner.isStemSectionAutomated(0) && owner.isPadsSectionAutomated(),
              "Remote automation markers mirror authoritative controls")) return false;
  observer.send("mix.volume", 0.7, "master");
  observer.send("mix.volume", 0.7, "click");
  observer.send("mix.gain", 0.7, "guide");
  observer.send("mix.solo", true, "stem");
  observer.send("mix.output", 2, "stem");
  if (!expect(observerCommands == 5,
              "Master, click, guide, solo and routing remain manual during another client's automation draft")) return false;
  observer.send("mix.volume", 0.7, "renamed-click");
  owner.send("mix.volume", 0.7, "renamed-click");
  if (!expect(observerCommands == 6 && sent.back().value.isDouble(),
              "A renamed Click with Other category stays manual by its portable filename")) return false;
  if (!owner.clearSectionAutomation("verse") || sent.back().action != "automation.clear" ||
      sent.back().value.getProperty("editCommandId", {}).toString() != leaseId) return false;
  if (!owner.finishSectionAutomationEditing(true) || sent.back().action != "automation.finish" ||
      !static_cast<bool>(sent.back().value.getProperty("save", false)) ||
      sent.back().value.getProperty("editCommandId", {}).toString() != leaseId) return false;
  owner.setConnected(false);
  const auto count = sent.size();
  owner.send("mix.volume", 0.9, "stem");
  if (!expect(!owner.canEditSectionAutomation() && !owner.isSectionAutomationEditing() &&
              owner.getSectionAutomationEditingPartId().isEmpty() && sent.size() == count,
              "Disconnect discards local ownership and disables draft controls")) return false;
  editing->setProperty("editing", false);
  editing->setProperty("partId", juce::String{});
  editing->setProperty("commandId", juce::String{});
  state.getDynamicObject()->setProperty("seq", 3);
  owner.applyState(state);
  if (!owner.beginSectionAutomationEditing("verse")) return false;
  owner.applyAcknowledgement(sent.back().id, false, "busy");
  if (!expect(!owner.isSectionAutomationEditing() && owner.canEditSectionAutomation(),
              "Rejected begin restores ordinary controls")) return false;
  if (!owner.beginSectionAutomationEditing("verse")) return false;
  auto differentSong = state.clone();
  differentSong.getProperty("song", {}).getDynamicObject()->setProperty("id", "different-song");
  owner.applyState(differentSong);
  return expect(!owner.isSectionAutomationEditing() && local.automationWrites == 2 && local.writes == 0,
                "Remote automation never mutates the client's audio processor and loses ownership on song change");
}

bool testPracticeListeningAdapter() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  session.setPracticeListeningMode(0, 1);
  if (local.practiceWrites != 1 || session.getPracticeListeningStem() != 0 ||
      session.getPracticeListeningMode() != 1 || local.volume != 0.75f || local.writes != 0) return false;
  session.setPracticeListeningMode(-1, 0);
  if (session.getPracticeListeningMode() != 0 || session.getPracticeListeningStem() != -1) return false;

  session.setRemote(true);
  int sent = 0;
  juce::String lastTarget, lastAction;
  juce::var lastValue;
  session.onCommand = [&](const juce::String &, const juce::String &action,
                          const juce::var &value, const juce::String &target) {
    ++sent; lastTarget = target; lastAction = action; lastValue = value;
  };
  session.setPracticeListeningMode(0, 1);
  if (sent != 0 || local.practiceWrites != 2) return false;
  auto state = juce::JSON::parse(R"({"song":{"id":"practice-song","stems":[{"id":"excluded","include":false},{"id":"first"},{"id":"second"}]},"practiceMode":1,"practiceStemId":"second"})");
  session.applyState(state);
  if (session.getPracticeListeningMode() != 1 || session.getPracticeListeningStem() != 1) return false;
  session.setPracticeListeningMode(0, 2);
  session.setPracticeListeningMode(0, 2);
  if (sent != 2 || lastAction != "practice.listen" || lastTarget != "first" ||
      static_cast<int>(lastValue.getProperty("mode", -1)) != 2 ||
      lastValue.getProperty("songId", {}).toString() != "practice-song" ||
      local.practiceWrites != 2 || session.getPracticeListeningMode() != 2 ||
      session.getPracticeListeningStem() != 0) return false;
  session.setPracticeListeningMode(2, 1);
  session.setPracticeListeningMode(0, 3);
  if (sent != 2) return false;
  state.getDynamicObject()->setProperty("practiceMode", 2);
  state.getDynamicObject()->setProperty("practiceStemId", "first");
  session.applyState(state);
  if (session.getPracticeListeningMode() != 2 || session.getPracticeListeningStem() != 0) return false;
  session.setPracticeListeningMode(-1, 0);
  if (sent != 3 || lastTarget.isNotEmpty() || static_cast<int>(lastValue.getProperty("mode", -1)) != 0 ||
      session.getPracticeListeningMode() != 0 || session.getPracticeListeningStem() != -1 ||
      session.ownsPracticeListening()) return false;
  session.setConnected(false);
  session.setPracticeListeningMode(0, 1);
  if (sent != 3 || session.getPracticeListeningMode() != 0 || session.getPracticeListeningStem() != -1) return false;
  session.setRemote(false);
  return local.practiceWrites == 2 && session.getPracticeListeningMode() == 0 && local.writes == 0;
}

bool testPracticeListeningOwnership() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  session.setRemote(true);
  juce::String lastId, lastAction, lastTarget;
  juce::var lastValue;
  session.onCommand = [&](const juce::String &id, const juce::String &action,
                          const juce::var &value, const juce::String &target) {
    lastId = id; lastAction = action; lastValue = value; lastTarget = target;
  };
  auto state = [](int sequence, const juce::String &commandId,
                  int mode = 1, const juce::String &songId = "practice-song") {
    auto result = juce::JSON::parse(R"({"song":{"id":"practice-song","stems":[{"id":"first"}]},"practiceMode":1,"practiceStemId":"first"})");
    result.getDynamicObject()->setProperty("seq", sequence);
    result.getDynamicObject()->setProperty("practiceCommandId", commandId);
    result.getDynamicObject()->setProperty("practiceMode", mode);
    result.getProperty("song", {}).getDynamicObject()->setProperty("id", songId);
    return result;
  };
  session.applyState(state(10, "other-client"));
  if (session.ownsPracticeListening()) return false;
  session.setPracticeListeningMode(0, 2);
  const auto firstId = lastId;
  if (!session.ownsPracticeListening()) return false;
  session.applyAcknowledgement("earlier-listen", false);
  session.applyState(state(10, "other-client"));
  if (!session.ownsPracticeListening()) return false;
  session.applyAcknowledgement(firstId, true);
  session.applyState(state(11, firstId, 2));
  if (!session.ownsPracticeListening()) return false;

  // A local server gesture retains its mode but removes our command lease.
  session.applyState(state(12, {}));
  if (session.ownsPracticeListening()) return false;
  session.setPracticeListeningMode(0, 2);
  const auto rejectedId = lastId;
  session.applyAcknowledgement(rejectedId, false);
  if (session.ownsPracticeListening()) return false;
  session.applyState(state(13, rejectedId, 2));
  if (session.ownsPracticeListening()) return false;

  session.setPracticeListeningMode(0, 1);
  const auto releasedId = lastId;
  session.releasePracticeListening();
  if (session.ownsPracticeListening() || lastAction != "practice.release" ||
      lastTarget.isNotEmpty() || static_cast<int>(lastValue.getProperty("mode", -1)) != 0 ||
      lastValue.getProperty("songId", {}).toString() != "practice-song") return false;
  session.applyState(state(14, releasedId));
  if (session.ownsPracticeListening()) return false;

  session.setPracticeListeningMode(0, 2);
  const auto earlierId = lastId;
  session.setPracticeListeningMode(0, 1);
  const auto latestId = lastId;
  session.applyAcknowledgement(earlierId, false);
  session.applyState(state(15, earlierId, 2));
  if (!session.ownsPracticeListening()) return false;
  session.applyAcknowledgement(latestId, true);
  session.applyState(state(16, "another-client", 2));
  if (session.ownsPracticeListening()) return false;

  session.setPracticeListeningMode(0, 1);
  const auto songChangedId = lastId;
  session.applyAcknowledgement(songChangedId, true);
  session.applyState(state(17, songChangedId, 1, "new-song"));
  if (session.ownsPracticeListening()) return false;
  session.setPracticeListeningMode(0, 1);
  session.setConnected(false);
  return !session.ownsPracticeListening() && local.practiceWrites == 0;
}

bool testPracticeListeningPreview() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  session.setRemote(true);
  juce::String lastId, lastAction;
  session.onCommand = [&](const juce::String &id, const juce::String &action,
                          const juce::var &, const juce::String &) {
    lastId = id; lastAction = action;
  };
  auto snapshot = [](int sequence, int mode, const juce::String &stem,
                     const juce::String &commandId, const juce::String &song = "practice-song") {
    auto result = juce::JSON::parse(R"({"song":{"id":"practice-song","stems":[{"id":"excluded","include":false},{"id":"first"},{"id":"second"}]}})");
    result.getDynamicObject()->setProperty("seq", sequence);
    result.getDynamicObject()->setProperty("practiceMode", mode);
    result.getDynamicObject()->setProperty("practiceStemId", stem);
    result.getDynamicObject()->setProperty("practiceCommandId", commandId);
    result.getProperty("song", {}).getDynamicObject()->setProperty("id", song);
    return result;
  };
  auto expect = [&](int mode, int stem, bool owner, const char *message) {
    if (session.getPracticeListeningMode() == mode &&
        session.getPracticeListeningStem() == stem && session.ownsPracticeListening() == owner)
      return true;
    std::cerr << message << '\n'; return false;
  };
  auto expectConfirmed = [&](int mode, int stem, const char *message) {
    if (session.getConfirmedPracticeListeningMode() == mode &&
        session.getConfirmedPracticeListeningStem() == stem)
      return true;
    std::cerr << message << '\n'; return false;
  };
  session.applyState(snapshot(10, 1, "second", "other-client"));
  session.setPracticeListeningMode(0, 2);
  const auto reducedId = lastId;
  if (!expect(2, 0, true, "Listen must preview mode and stem before a snapshot")) return false;
  if (!expectConfirmed(1, 1, "Glow must retain the confirmed focus while a reduced gesture is pending")) return false;
  session.applyState(snapshot(10, 1, "second", "other-client"));
  if (!expect(2, 0, true, "A delayed snapshot cannot undo the latest listen")) return false;

  session.setPracticeListeningMode(-1, 0);
  const auto normalId = lastId;
  if (!expect(0, -1, false, "Normal must preview immediately and revoke retarget ownership")) return false;
  if (!expectConfirmed(1, 1, "Pending restore cannot hide a still-confirmed focus")) return false;
  session.applyAcknowledgement(reducedId, true);
  session.applyState(snapshot(11, 2, "first", reducedId));
  if (!expect(0, -1, false, "An older ACK and snapshot cannot revive reduced mode")) return false;
  if (!expectConfirmed(2, 0, "Presentation reflects actual reduced audio even while normal awaits confirmation")) return false;
  session.applyAcknowledgement(normalId, true);
  session.applyState(snapshot(11, 2, "first", reducedId));
  if (!expect(0, -1, false, "ACK alone cannot end the normal preview")) return false;
  session.applyState(snapshot(12, 0, {}, {}));
  if (!expect(0, -1, false, "The following sequence confirms normal")) return false;
  if (!expectConfirmed(0, -1, "Confirmed normal removes the focus presentation")) return false;

  session.setPracticeListeningMode(1, 1);
  const auto earlierId = lastId;
  session.setPracticeListeningMode(0, 2);
  const auto latestId = lastId;
  session.applyAcknowledgement(earlierId, false);
  session.applyState(snapshot(13, 1, "second", earlierId));
  if (!expect(2, 0, true, "Fast steps and older rejections must preserve the latest stem and mode")) return false;
  session.applyAcknowledgement(latestId, true);
  session.applyState(snapshot(13, 1, "second", earlierId));
  if (!expect(2, 0, true, "Reapplying the ACK sequence must preserve the preview")) return false;
  session.applyState(snapshot(14, 1, "second", "other-client"));
  if (!expect(1, 1, false, "A newer authoritative sequence reconciles a superseded intent")) return false;

  session.setPracticeListeningMode(0, 2);
  session.applyAcknowledgement(lastId, false);
  if (!expect(1, 1, false, "A rejected listen reverts to the confirmed mode and stem")) return false;
  if (!expectConfirmed(1, 1, "A rejected request never replaces the confirmed focus")) return false;
  session.setPracticeListeningMode(1, 1);
  const auto ownedId = lastId;
  session.applyAcknowledgement(ownedId, true);
  session.applyState(snapshot(15, 1, "second", ownedId));
  if (!expect(1, 1, true, "Confirmed active listening retains its lease")) return false;
  session.setPracticeListeningMode(-1, 0);
  session.applyAcknowledgement(lastId, false);
  if (!expect(1, 1, false, "Rejected normal reverts visually without reacquiring the old lease")) return false;

  session.setPracticeListeningMode(0, 2);
  const auto releasedId = lastId;
  session.releasePracticeListening();
  if (lastAction != "practice.release" ||
      !expect(1, 1, false, "Release clears a pending listen without changing confirmed state")) return false;
  session.applyAcknowledgement(releasedId, true);
  if (!expect(1, 1, false, "An ACK after release cannot restore the preview")) return false;
  session.setPracticeListeningMode(0, 2);
  session.applyState(snapshot(16, 0, {}, {}, "new-song"));
  if (!expect(0, -1, false, "Changing song clears a pending listen")) return false;
  session.setPracticeListeningMode(1, 1);
  session.setConnected(false);
  if (!expect(0, -1, false, "Disconnect clears a pending listen")) return false;
  if (!expectConfirmed(0, -1, "Disconnect clears confirmed focus presentation")) return false;

  session.applyState(snapshot(17, 0, {}, {}, "new-song"));
  session.setPracticeListeningMode(1, 1);
  if (!expectConfirmed(0, -1, "Pending focus from normal does not illuminate the glow")) return false;
  if (!expect(1, 1, true, "A fresh connected listen is optimistic")) return false;
  if (!waitUntil([&] { return session.getPracticeListeningMode() == 0; }, 2500) ||
      !expect(0, -1, false, "An unacknowledged preview expires even without another snapshot")) return false;
  return local.practiceWrites == 0 && local.writes == 0 && local.volume == 0.75f;
}

bool testRemoteFaderReconciliation() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  session.setRemote(true);
  juce::String latestId;
  session.onCommand = [&](const juce::String &id, const juce::String &, const juce::var &, const juce::String &) { latestId = id; };
  auto state = juce::JSON::parse(R"({"seq":1,"song":{"id":"song","stems":[{"id":"stem"}]},"mixer":[{"target":"stem","volume":0.2,"gain":1},{"target":"master","volume":0.2,"gain":1},{"target":"click","volume":0.2,"gain":1},{"target":"guide","volume":0.2,"gain":1}],"ui":{"getPadsVolume":0.2,"getPadsGain":1}})");
  session.applyState(state);
  auto expect = [](bool ok, const char *reason) {
    if (!ok) std::cerr << reason << '\n';
    return ok;
  };
  auto equal = [](float a, float b) { return std::abs(a-b) < 0.0001f; };
  struct Control { juce::String action, target; std::function<float()> read; };
  std::vector<Control> controls{
    {"mix.volume", "stem", [&] { return session.getStemVolume(0); }},
    {"mix.gain", "stem", [&] { return session.getStemGain(0); }},
    {"mix.volume", "master", [&] { return session.getMasterVolume(); }},
    {"mix.gain", "master", [&] { return session.getMasterGain(); }},
    {"mix.volume", "click", [&] { return session.getMetronomeVolume(); }},
    {"mix.gain", "click", [&] { return session.getMetronomeGain(); }},
    {"mix.volume", "guide", [&] { return session.getDynamicGuideVolume(); }},
    {"mix.gain", "guide", [&] { return session.getDynamicGuideGain(); }},
    {"pads.volume", {}, [&] { return session.getPadsVolume(); }},
    {"pads.gain", {}, [&] { return session.getPadsGain(); }}
  };
  for (auto &control : controls) {
    session.send(control.action, 0.4, control.target);
    const auto earlierId = latestId;
    if (!expect(equal(control.read(), 0.4f), "Fader must follow input before network reply")) return false;
    session.send(control.action, 0.8, control.target);
    session.applyAcknowledgement(earlierId, true);
    session.applyState(state);
    if (!expect(equal(control.read(), 0.8f), "An earlier ack/snapshot must not undo a later drag")) return false;
    session.applyAcknowledgement("another-client", false);
    if (!expect(equal(control.read(), 0.8f), "Another client's ack must not clear this preview")) return false;
    session.applyAcknowledgement(latestId, true);
    session.applyState(state);
    if (!expect(equal(control.read(), 0.8f), "Reapplying the same UI snapshot must not clear an acknowledged preview")) return false;
  }
  // The first newer snapshot after ack is authoritative, including clamped values.
  auto confirmed = state.clone();
  confirmed.getDynamicObject()->setProperty("seq", 2);
  auto confirmedMixer = confirmed.getProperty("mixer", {});
  for (auto &row : *confirmedMixer.getArray()) {
    row.getDynamicObject()->setProperty("volume", 0.75);
    row.getDynamicObject()->setProperty("gain", 0.75);
  }
  auto ui = confirmed.getProperty("ui", {});
  ui.getDynamicObject()->setProperty("getPadsVolume", 0.75);
  ui.getDynamicObject()->setProperty("getPadsGain", 0.75);
  session.applyState(confirmed);
  for (const auto &control : controls)
    if (!expect(equal(control.read(), 0.75f), "Confirmed server values must reach all controls")) return false;
  session.send("mix.volume", 0.9, "stem");
  session.applyAcknowledgement(latestId, false);
  if (!expect(equal(session.getStemVolume(0), 0.75f), "Rejected commands must restore confirmed state")) return false;
  session.send("mix.volume", 0.9, "stem");
  session.setConnected(false);
  if (!expect(equal(session.getStemVolume(0), 0.75f), "Disconnect must discard unconfirmed preview")) return false;
  session.applyState(confirmed);
  session.send("mix.volume", 0.9, "stem");
  auto otherSong = state.clone();
  otherSong.getProperty("song", {}).getDynamicObject()->setProperty("id", "other-song");
  session.applyState(otherSong);
  if (!expect(equal(session.getStemVolume(0), 0.2f), "Changing songs must discard the previous preview")) return false;
  return expect(local.writes == 0, "Client preview must never write to local audio");
}

bool testPracticeLoopAdapter() {
  const auto expect = [](bool ok, const char *message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  if (!expect(session.preparePracticeLoop(8.0) && session.ownsPracticeLoop() &&
              !session.isPracticeLoopActive(), "A prepares without activating a local loop")) return false;
  if (!expect(session.setPracticeLoopRange(8.0, 15.0) && session.ownsPracticeLoop(),
              "B activates and owns local A/B")) return false;
  local.setPracticeLoopRange(25.0, 30.0);
  session.releasePracticeLoop();
  if (!expect(local.isPracticeLoopActive() && local.getPracticeLoopRange().getStart() == 25.0,
              "Closing an older local panel cannot clear a newer loop")) return false;
  local.clearPracticeLoop();
  if (!session.preparePracticeLoop(7.0)) return false;
  session.releasePracticeLoop();
  if (!expect(!local.hasPracticeLoopPreparation(), "Closing A's draft releases its preparation")) return false;

  session.setRemote(true);
  struct Sent { juce::String id, action; juce::var value; };
  std::vector<Sent> sent;
  session.onCommand = [&](const juce::String &id, const juce::String &action,
                          const juce::var &value, const juce::String &) {
    sent.push_back({id, action, value});
  };
  const auto snapshot = [](int seq, const juce::String &id, bool active,
                            bool prepared = false, const juce::String &song = "song") {
    auto result = juce::JSON::parse(R"({"song":{"id":"song","stems":[{"id":"stem"}]},"length":100,"windowStart":0,"windowEnd":100,"practiceLoopStart":8,"practiceLoopEnd":15,"practiceLoopMinimumDuration":0.25})");
    result.getDynamicObject()->setProperty("seq", seq);
    result.getDynamicObject()->setProperty("practiceLoopCommandId", id);
    result.getDynamicObject()->setProperty("practiceLoopActive", active);
    result.getDynamicObject()->setProperty("practiceLoopPrepared", prepared);
    result.getProperty("song", {}).getDynamicObject()->setProperty("id", song);
    return result;
  };
  const int before = local.practiceLoopWrites;
  if (!expect(!session.setPracticeLoopRange(8.0, 15.0) && sent.empty(),
              "Disconnected clients cannot start a loop")) return false;
  session.applyState(snapshot(1, {}, false));
  if (!session.preparePracticeLoop(8.0)) return false;
  const auto prepareId = sent.back().id;
  if (!expect(sent.back().action == "practice.loopPrepare" &&
              sent.back().value.getProperty("start", {}).isDouble() &&
              session.ownsPracticeLoop() && !session.isPracticeLoopActive() &&
              session.getPracticeLoopRange().isEmpty(),
              "Pending remote A owns preparation without a phantom loop")) return false;
  session.applyAcknowledgement(prepareId, true);
  if (!expect(session.ownsPracticeLoop() && !session.isPracticeLoopActive(),
              "Preparation ack cannot revoke its own cleanup lease")) return false;
  session.applyAcknowledgement("unrelated-fader", true);
  if (!expect(session.ownsPracticeLoop(), "An unrelated ack cannot clear A/B ownership")) return false;
  session.applyState(snapshot(2, prepareId, false, true));
  if (!expect(session.ownsPracticeLoop() && !session.isPracticeLoopActive(),
              "Confirmed preparation retains release ownership")) return false;
  if (!session.setPracticeLoopRange(8.0, 15.0)) return false;
  const auto loopId = sent.back().id;
  if (!expect(sent.back().action == "practice.loop" && session.isPracticeLoopActive() &&
              session.getPracticeLoopRange().getStart() == 8.0,
              "Remote loop previews its requested range until ack")) return false;
  session.applyAcknowledgement("older-loop", false, "rejected");
  if (session.wasPracticeLoopRequestRejected()) return false;
  session.applyAcknowledgement(loopId, false, "discontinuous-arrangement");
  if (!expect(session.wasPracticeLoopRequestRejected() &&
              session.getPracticeLoopError() == "discontinuous-arrangement" &&
              !session.isPracticeLoopActive() && session.ownsPracticeLoop(),
              "Rejected B restores A's lease and exposes the server reason")) return false;
  session.releasePracticeLoop();
  if (!expect(sent.back().action == "practice.loopRelease" &&
              sent.back().value.getProperty("commandId", {}).toString() == prepareId &&
              !session.ownsPracticeLoop(), "Release identifies the still-owned A command")) return false;
  session.applyState(snapshot(3, {}, false));
  if (!session.setPracticeLoopRange(8.0, 15.0)) return false;
  const auto acceptedId = sent.back().id;
  session.applyAcknowledgement(acceptedId, true);
  session.applyState(snapshot(4, acceptedId, true));
  if (!session.ownsPracticeLoop()) return false;
  session.applyState(snapshot(5, "new-client", true));
  if (!expect(!session.ownsPracticeLoop(), "Newer remote owner revokes an old panel's lease")) return false;
  if (!session.setPracticeLoopRange(8.0, 15.0)) return false;
  session.setConnected(false);
  if (!expect(!session.ownsPracticeLoop() && !session.isPracticeLoopActive(),
              "Disconnect drops loop previews and ownership")) return false;
  session.applyState(snapshot(6, {}, false));
  if (!session.preparePracticeLoop(8.0)) return false;
  session.applyState(snapshot(7, {}, false, false, "other-song"));
  return expect(!session.ownsPracticeLoop() && local.practiceLoopWrites == before,
                "Song changes revoke preparation; clients never mutate local audio");
}

bool testPracticeAccompanimentIncompleteState() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  if (!session.hasConfirmedDynamicGuideMixState() ||
      !session.hasConfirmedMetronomeState()) return false;
  session.setRemote(true);
  const auto snapshot = [] {
    return juce::JSON::parse(R"({"song":{"id":"song"},"ui":{"isDynamicGuideEnabled":true,"hasDynamicGuideAssets":true,"isMetronomeEnabled":true,"isClickDivisionEnabled":true,"getClickDivision":1},"mixer":[{"target":"guide","mute":false},{"target":"click","mute":false}]})");
  };
  const auto expect = [](bool ok, const char *message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  auto complete = snapshot();
  session.applyState(complete);
  if (!expect(session.hasConfirmedDynamicGuideMixState() && session.hasConfirmedMetronomeState(),
              "Complete virtual mixer states are usable")) return false;

  for (const int bus : {0, 1}) {
    for (const int malformed : {0, 1, 2}) {
      auto next = snapshot();
      auto rows = BasicPlaybackSession<TestProcessor>::field(next, "mixer");
      if (malformed == 0)
        rows.getArray()->remove(bus);
      else if (malformed == 1)
        rows.getArray()->getReference(bus).getDynamicObject()->removeProperty("mute");
      else
        rows.getArray()->getReference(bus).getDynamicObject()->setProperty("mute", "false");
      session.applyState(next);
      if (!expect(bus == 0 ? !session.hasConfirmedDynamicGuideMixState()
                           : !session.hasConfirmedMetronomeState(),
                  "Missing mixer bus, missing mute and string mute cannot imply ON")) return false;
      if (!expect(bus == 0 ? session.hasConfirmedMetronomeState()
                           : session.hasConfirmedDynamicGuideMixState(),
                  "A malformed bus does not disable the other valid bus")) return false;
    }
  }
  for (const char *field : {"isMetronomeEnabled", "isClickDivisionEnabled", "getClickDivision"}) {
    for (const bool remove : {false, true}) {
      auto next = snapshot();
      auto ui = BasicPlaybackSession<TestProcessor>::field(next, "ui");
      if (remove) ui.getDynamicObject()->removeProperty(field);
      else ui.getDynamicObject()->setProperty(field, "1");
      session.applyState(next);
      if (!expect(!session.hasConfirmedMetronomeState(),
                  "Metronome flags and division require complete typed state")) return false;
    }
  }
  for (const int mask : {-1, 8, 99}) {
    auto next = snapshot();
    BasicPlaybackSession<TestProcessor>::field(next, "ui").getDynamicObject()
        ->setProperty("getClickDivision", mask);
    session.applyState(next);
    if (!expect(!session.hasConfirmedMetronomeState(),
                "Out-of-range division masks cannot establish metronome state")) return false;
  }
  for (const int mask : {0, 1, 7}) {
    auto next = snapshot();
    BasicPlaybackSession<TestProcessor>::field(next, "ui").getDynamicObject()
        ->setProperty("getClickDivision", mask);
    session.applyState(next);
    if (!expect(session.hasConfirmedMetronomeState(),
                "Every supported mask, including recoverable zero, has valid state")) return false;
  }
  session.setConnected(false);
  return expect(!session.hasConfirmedDynamicGuideMixState() &&
                    !session.hasConfirmedMetronomeState(),
                "Disconnected virtual bus snapshots are not usable state");
}

bool testPracticeAccompanimentAdapter() {
  using namespace playworship::practice;
  const auto expect = [](bool ok, const char *message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  local.dynamicGuideEnabled = true;
  local.guideAssets = true;
  local.practiceMode = 1;
  local.practiceStem = 0;
  if (!expect(session.hasGuideSourceState() && session.isDynamicGuideEnabled() &&
                  session.hasDynamicGuideAssets() &&
                  session.getConfirmedPracticeListeningMode() == 1 &&
                  session.getConfirmedPracticeListeningStem() == 0,
              "Local guide state is read from the processor")) return false;
  setGuideEnabled(session, GuideSelection{true, true, {}}, true);
  if (!expect(!local.guideMuted && local.writes == 1,
              "Local guide ON delegates only to dynamic mute")) return false;
  const int localWrites = local.writes;
  session.setRemote(true);
  struct Command { juce::String action, target; juce::var value; };
  std::vector<Command> commands;
  session.onCommand = [&](const juce::String &, const juce::String &action,
                          const juce::var &value, const juce::String &target) {
    commands.push_back({action, target, value});
  };
  auto state = juce::JSON::parse(R"({"seq":1,"song":{"id":"song","stems":[{"id":"excluded","include":false},{"id":"music","include":true},{"id":"guide-a","include":true},{"id":"guide-b","include":true}]},"settings":{"dynamicGuide":{"enabled":true}},"ui":{"isMetronomeEnabled":false,"isClickDivisionEnabled":false,"getClickDivision":0},"mixer":[{"target":"guide","mute":true},{"target":"click","mute":true},{"target":"music","mute":false},{"target":"guide-a","mute":true},{"target":"guide-b","mute":true}]})");
  session.applyState(state);
  if (!expect(!session.hasGuideSourceState() && !session.isDynamicGuideEnabled() &&
                  !session.hasDynamicGuideAssets(),
              "Old servers must not infer guide selection from cached settings")) return false;
  auto missingMixState = juce::JSON::parse(R"({"seq":1,"song":{"id":"song","stems":[{"id":"guide-metadata-only","include":true}]},"ui":{"isDynamicGuideEnabled":false,"hasDynamicGuideAssets":true},"mixer":[]})");
  session.applyState(missingMixState);
  if (!expect(session.getLoadedStemCount() == 1 && !session.hasConfirmedStemMixState(0),
              "Included guide metadata without a server mixer row is not a loaded source")) return false;
  session.applyState(state);
  if (!expect(session.hasConfirmedStemMixState(1) && session.hasConfirmedStemMixState(2) &&
                  !session.hasConfirmedStemMixState(-1) && !session.hasConfirmedStemMixState(3),
              "Guide availability requires an authoritative mixer row for each included index")) return false;
  auto ui = BasicPlaybackSession<TestProcessor>::field(state, "ui");
  ui.getDynamicObject()->setProperty("isDynamicGuideEnabled", true);
  session.applyState(state);
  if (!expect(!session.hasGuideSourceState(),
              "An incomplete source snapshot is not reliable")) return false;
  ui.getDynamicObject()->setProperty("hasDynamicGuideAssets", "true");
  session.applyState(state);
  if (!expect(!session.hasGuideSourceState(),
              "Source metadata requires booleans, not truthy strings")) return false;
  ui.getDynamicObject()->setProperty("hasDynamicGuideAssets", false);
  session.applyState(state);
  if (!expect(session.hasGuideSourceState() && session.isDynamicGuideEnabled() &&
                  !session.hasDynamicGuideAssets(),
              "Selected dynamic source can correctly report an absent pack")) return false;
  setGuideEnabled(session, GuideSelection{false, true, {}}, true);
  if (!expect(commands.empty(), "Unavailable guide does not send an on/off command")) return false;
  ui.getDynamicObject()->setProperty("hasDynamicGuideAssets", true);
  session.applyState(state);
  setGuideEnabled(session, GuideSelection{true, true, {}}, true);
  if (!expect(commands.size() == 1 && commands.back().action == "mix.mute" &&
                  commands.back().target == "guide" && !static_cast<bool>(commands.back().value) &&
                  session.isDynamicGuideMuted() && local.writes == localWrites,
              "Remote guide ON sends mute only, retaining the authoritative OFF state")) return false;
  auto mixer = BasicPlaybackSession<TestProcessor>::field(state, "mixer");
  mixer.getArray()->getReference(0).getDynamicObject()->setProperty("mute", false);
  state.getDynamicObject()->setProperty("seq", 2);
  session.applyState(state);
  if (!expect(guideEnabled(session, GuideSelection{true, true, {}}),
              "Dynamic guide ON is confirmed only by the next mixer state")) return false;
  ui.getDynamicObject()->setProperty("isDynamicGuideEnabled", false);
  session.applyState(state);
  if (!expect(session.hasGuideSourceState() && !session.isDynamicGuideEnabled(),
              "Original source state comes from live UI fields, not stale settings")) return false;
  setGuideEnabled(session, GuideSelection{true, false, {1, 2}}, true);
  if (!expect(commands.size() == 3 && commands[1].target == "guide-a" &&
                  commands[2].target == "guide-b" && commands[1].action == "mix.mute" &&
                  commands[2].action == "mix.mute" && local.writes == localWrites,
              "Original guide commands use included stem IDs and never the local processor")) return false;
  setMetronomeEnabled(session, true);
  if (!expect(commands.size() == 7 && commands[3].action == "click.division" &&
                  static_cast<int>(commands[3].value) == 1 &&
                  commands[4].action == "click.enabled" &&
                  commands[5].action == "mix.mute" && commands[5].target == "click" &&
                  commands[6].action == "click.metronome" && !metronomeEnabled(session),
              "Remote metronome enables all gates without optimistic ON")) return false;
  ui.getDynamicObject()->setProperty("isMetronomeEnabled", true);
  ui.getDynamicObject()->setProperty("isClickDivisionEnabled", true);
  ui.getDynamicObject()->setProperty("getClickDivision", 1);
  mixer.getArray()->getReference(1).getDynamicObject()->setProperty("mute", false);
  state.getDynamicObject()->setProperty("seq", 3);
  session.applyState(state);
  if (!expect(metronomeEnabled(session) && local.writes == localWrites,
              "Remote metronome reflects confirmed state without local audio writes")) return false;
  session.setConnected(false);
  setGuideEnabled(session, GuideSelection{true, true, {}}, false);
  setMetronomeEnabled(session, false);
  return expect(commands.size() == 7 && local.writes == localWrites &&
                    local.practiceMode == 1 && local.practiceStem == 0,
                "Disconnected accompaniment cannot send commands or mutate local audio");
}

int main() {
  if (!testPracticeAccompanimentIncompleteState()) { std::cerr << "Incomplete accompaniment state failed\n"; return 1; }
  if (!testPracticeAccompanimentAdapter()) { std::cerr << "Practice accompaniment adapter failed\n"; return 1; }
  if (!testSectionAutomationAdapter()) { std::cerr << "Section automation adapter failed\n"; return 1; }
  if (!testPracticeLoopAdapter()) { std::cerr << "Practice loop adapter failed\n"; return 1; }
  if (!testPracticeScrubLease()) { std::cerr << "Practice scrub ownership failed\n"; return 1; }
  if (!testPracticeScrubAdapter()) { std::cerr << "Practice scrub adapter failed\n"; return 1; }
  if (!testPracticeListeningOwnership()) { std::cerr << "Practice listening ownership failed\n"; return 1; }
  if (!testPracticeListeningAdapter()) { std::cerr << "Practice listening adapter isolation failed\n"; return 1; }
  if (!testPracticeListeningPreview()) { std::cerr << "Practice listening preview failed\n"; return 1; }
  if (!testRemoteFaderReconciliation()) return 1;
  if (!testPlaybackAdapter()) { std::cerr << "Playback adapter isolation failed\n"; return 1; }
  if (const char *name = std::getenv("PW_REMOTE_ADVERTISE_ONLY")) {
    RemoteSession advertiser;
    if (!advertiser.becomeServer(name) || !waitUntil([&] {
          return advertiser.getDiscoveryStatus() == playworship::remote::LanDiscovery::Status::Ready;
        }, 8000)) return 1;
    std::cout << "ready\n" << std::flush;
    std::string done;
    std::getline(std::cin, done);
    return 0;
  }
  if (const char *expected = std::getenv("PW_REMOTE_DISCOVERY_EXPECT")) {
    RemoteSession browser;
    browser.becomeClient();
    DiscoveredServer endpoint;
    const bool found = waitUntil([&] {
      for (const auto &server : browser.getDiscoveredServers())
        if (server.instance == expected) {
          endpoint = server;
          return true;
        }
      return false;
    }, 8000);
    if (!found) { std::cerr << "external server not discovered\n"; return 1; }
    browser.connectTo(endpoint);
    if (!waitUntil([&] { return browser.isConnected(); })) {
      std::cerr << "discovered address did not connect\n"; return 1;
    }
    std::cout << "external server discovered and connected\n";
    return 0;
  }
  RemoteSession server;
  RemoteSession first;
  RemoteSession second;
  if (!server.becomeServer("Remote test")) {
    std::cerr << "Could not start local server\n";
    return 1;
  }
  first.becomeClient();
  second.becomeClient();
  DiscoveredServer address;
  if (std::getenv("PW_REMOTE_DIRECT_CONNECT") != nullptr) {
    address = {"Remote test", "127.0.0.1", server.getPort(),
               juce::Time::currentTimeMillis()};
  } else if (!waitUntil([&] {
        for (const auto &found : first.getDiscoveredServers())
          if (found.instance == "Remote test" &&
              found.port == server.getPort()) {
            address = found;
            return true;
          }
        return false;
      }, 8000)) {
    std::cerr << "Client did not discover the advertised server\n";
    return 1;
  }
  first.connectTo(address);
  second.connectTo(address);
  if (!waitUntil([&] {
        return first.isConnected() && second.isConnected() &&
               server.getClientCount() == 2;
      })) {
    std::cerr << "Both clients did not connect\n";
    return 1;
  }

  std::vector<float> waveform(10000);
  for (size_t i = 0; i < waveform.size(); ++i)
    waveform[i] = static_cast<float>(static_cast<int>(i % 101) - 50) / 50.0f;
  std::vector<uint8_t> expectedBytes;
  expectedBytes.reserve(waveform.size() * sizeof(float));
  for (float value : waveform) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift : {24, 16, 8, 0})
      expectedBytes.push_back(static_cast<uint8_t>((bits >> shift) & 0xff));
  }
  const auto hash =
      juce::SHA256(waveform.data(), waveform.size() * sizeof(float))
          .toHexString();
  server.publish(R"({"v":1,"type":"snapshot","seq":1,"library":{"songs":[]}})", waveform,
                 hash);
  int firstSnapshots = 0, secondSnapshots = 0;
  int firstWaves = 0, secondWaves = 0;
  const bool bothReceived = waitUntil([&] {
    for (const auto &event : first.drainEvents()) {
      firstSnapshots += event.kind == RemoteSession::Event::Kind::Snapshot;
      if (event.kind == RemoteSession::Event::Kind::Waveform) {
        ++firstWaves;
        if (event.bytes != expectedBytes)
          return false;
      }
    }
    for (const auto &event : second.drainEvents()) {
      secondSnapshots += event.kind == RemoteSession::Event::Kind::Snapshot;
      secondWaves += event.kind == RemoteSession::Event::Kind::Waveform;
    }
    return firstSnapshots > 0 && secondSnapshots > 0 &&
           firstWaves == 1 && secondWaves == 1;
  });
  if (!bothReceived) {
    std::cerr << "Snapshot or waveform missing from a client\n";
    return 1;
  }

  server.publish(R"({"v":1,"type":"snapshot","seq":2,"library":{"songs":[]}})", waveform, hash);
  bool deltaWithoutLibrary = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      const auto value = juce::JSON::parse(event.text);
      if (value.getProperty("seq",0) == juce::var(2))
        deltaWithoutLibrary = !value.getDynamicObject()->hasProperty("library");
    }
    return deltaWithoutLibrary;
  })) { std::cerr << "Unchanged catalogue was retransmitted\n"; return 1; }
  server.drainEvents();
  second.sendCommand("identify-disconnecting-peer", "transport.seek", 1.0);
  std::string departingPeer;
  if (!waitUntil([&] {
    for (const auto &event : server.drainEvents())
      if (event.kind == RemoteSession::Event::Kind::Command &&
          event.value.getProperty("id", {}).toString() == "identify-disconnecting-peer")
        departingPeer = event.sourceId;
    return !departingPeer.empty();
  })) { std::cerr << "Command has no peer identity\n"; return 1; }
  first.sendCommand("other-peer-kept", "transport.seek", 2.0);
  // Give the loopback sender time to queue the other client's command before
  // closing this peer; its cleanup must preserve that reliable command.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  second.stop();
  bool peerDisconnected = false, otherCommandKept = false;
  if (!waitUntil([&] {
    for (const auto &event : server.drainEvents()) {
      if (event.kind == RemoteSession::Event::Kind::PeerDisconnected && event.sourceId == departingPeer)
        peerDisconnected = true;
      if (event.kind == RemoteSession::Event::Kind::Command &&
          event.value.getProperty("id", {}).toString() == "other-peer-kept") otherCommandKept = true;
    }
    return peerDisconnected && otherCommandKept;
  })) { std::cerr << "Peer close lost its identity or another client's command\n"; return 1; }
  second.becomeClient(); second.connectTo(address);
  bool rejoinedWithLibrary=false, rejoinedWithWave=false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      if (event.kind == RemoteSession::Event::Kind::Waveform) rejoinedWithWave = event.bytes == expectedBytes;
      if (event.kind == RemoteSession::Event::Kind::Snapshot) {
        const auto value = juce::JSON::parse(event.text);
        rejoinedWithLibrary = value.getProperty("library",{}).isObject();
      }
    }
    return rejoinedWithLibrary && rejoinedWithWave;
  })) { std::cerr << "Reconnecting client did not receive full state\n"; return 1; }

  server.broadcast(R"({"v":1,"type":"meters","position":19,"levels":{"master":{"l":0.8,"r":0.6}}})");
  bool firstMeter=false, secondMeter=false;
  if (!waitUntil([&] {
    for (const auto &event : first.drainEvents()) if (event.kind == RemoteSession::Event::Kind::Meters) firstMeter=true;
    for (const auto &event : second.drainEvents()) if (event.kind == RemoteSession::Event::Kind::Meters) secondMeter=true;
    return firstMeter && secondMeter;
  })) { std::cerr << "Meter broadcast missing\n"; return 1; }

  first.sendCommand("command-1", "transport.seek", 18.5);
  bool receivedCommand = waitUntil([&] {
    for (const auto &event : server.drainEvents()) {
      if (event.kind != RemoteSession::Event::Kind::Command)
        continue;
      const auto json = juce::JSON::parse(event.text);
      if (json.getProperty("id", {}).toString() == "command-1" &&
          json.getProperty("action", {}).toString() == "transport.seek")
        return true;
    }
    return false;
  });
  if (!receivedCommand) {
    std::cerr << "Server did not receive command\n";
    return 1;
  }
  const auto scrubPayload = juce::JSON::parse(
      R"({"songId":"song","token":"ordered-scrub","seconds":24,"restorePlayback":true})");
  first.sendCommand("scrub-order-begin", "transport.scrubBegin", scrubPayload);
  first.sendCommand("scrub-order-end", "transport.scrubEnd", scrubPayload);
  first.sendCommand("scrub-order-cancel", "transport.scrubCancel", scrubPayload);
  std::vector<juce::String> scrubActions;
  std::string scrubPeer;
  bool wrongScrubPeer = false;
  if (!waitUntil([&] {
    for (const auto &event : server.drainEvents()) {
      if (event.kind != RemoteSession::Event::Kind::Command) continue;
      const auto json = event.value;
      if (!json.getProperty("id", {}).toString().startsWith("scrub-order-")) continue;
      if (scrubPeer.empty()) scrubPeer = event.sourceId;
      wrongScrubPeer = wrongScrubPeer || scrubPeer != event.sourceId ||
          json.getProperty("value", {}).getProperty("token", {}).toString() != "ordered-scrub";
      scrubActions.push_back(json.getProperty("action", {}).toString());
    }
    return scrubActions.size() >= 3;
  }) || wrongScrubPeer || scrubActions != std::vector<juce::String>{
      "transport.scrubBegin", "transport.scrubEnd", "transport.scrubCancel"}) {
    std::cerr << "Practice scrub commands must preserve peer ownership and FIFO order\n";
    return 1;
  }
  const auto loopPreparation = juce::JSON::parse(R"({"songId":"song","start":12})");
  const auto loopRange = juce::JSON::parse(R"({"songId":"song","start":12,"end":16})");
  const auto loopRelease = juce::JSON::parse(R"({"songId":"song","commandId":"loop-order-range"})");
  first.sendCommand("loop-order-prepare", "practice.loopPrepare", loopPreparation);
  first.sendCommand("loop-order-range", "practice.loop", loopRange);
  first.sendCommand("loop-order-release", "practice.loopRelease", loopRelease);
  std::vector<juce::String> loopActions;
  std::string loopPeer;
  bool wrongLoopPayload = false;
  if (!waitUntil([&] {
    for (const auto &event : server.drainEvents()) {
      if (event.kind != RemoteSession::Event::Kind::Command) continue;
      const auto json = event.value;
      const auto id = json.getProperty("id", {}).toString();
      if (!id.startsWith("loop-order-")) continue;
      if (loopPeer.empty()) loopPeer = event.sourceId;
      const auto payload = json.getProperty("value", {});
      wrongLoopPayload = wrongLoopPayload || loopPeer != event.sourceId ||
          payload.getProperty("songId", {}).toString() != "song" ||
          (id == "loop-order-release" && payload.getProperty("commandId", {}).toString() != "loop-order-range") ||
          (id == "loop-order-range" && static_cast<double>(payload.getProperty("end", 0.0)) != 16.0);
      loopActions.push_back(json.getProperty("action", {}).toString());
    }
    return loopActions.size() >= 3;
  }) || wrongLoopPayload || loopActions != std::vector<juce::String>{
      "practice.loopPrepare", "practice.loop", "practice.loopRelease"}) {
    std::cerr << "Practice loop commands must preserve range, lease ID and FIFO order\n";
    return 1;
  }
  server.sendAck("command-1", true);
  if (!waitUntil([&] {
        for (const auto &event : first.drainEvents()) {
          if (event.kind != RemoteSession::Event::Kind::Snapshot)
            continue;
          if (juce::JSON::parse(event.text)
                  .getProperty("type", {})
                  .toString() == "ack")
            return true;
        }
        return false;
      })) {
    std::cerr << "Client did not receive acknowledgement\n";
    return 1;
  }
  // New clients negotiate metadata deltas; their delivered event must still
  // contain the complete song/settings without carrying artwork over the wire.
  auto metadataState = juce::JSON::parse(R"({"v":1,"type":"snapshot","seq":10,"song":{"id":"delta-song","artwork":"cached-art"},"settings":{"window":3}})");
  server.publishState(metadataState, waveform, hash);
  bool fullMetadata = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents())
      if (static_cast<int>(event.value.getProperty("seq", 0)) == 10)
        fullMetadata = event.value.getProperty("song", {}).getProperty("artwork", {}).toString() == "cached-art";
    return fullMetadata;
  })) return 1;
  auto deltaState = juce::JSON::parse(R"({"v":1,"type":"snapshot","seq":11})");
  deltaState.getDynamicObject()->setProperty("song", metadataState.getProperty("song", {}));
  deltaState.getDynamicObject()->setProperty("settings", metadataState.getProperty("settings", {}));
  server.publishState(deltaState, waveform, hash);
  bool mergedDelta = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      if (static_cast<int>(event.value.getProperty("seq", 0)) != 11) continue;
      const auto wire = juce::JSON::parse(event.text);
      mergedDelta = !wire.hasProperty("song") && !wire.hasProperty("settings") &&
          event.value.getProperty("song", {}).getProperty("artwork", {}).toString() == "cached-art" &&
          static_cast<int>(event.value.getProperty("settings", {}).getProperty("window", 0)) == 3;
    }
    return mergedDelta;
  })) { std::cerr << "Incremental metadata was retransmitted or not merged\n"; return 1; }

  // Burst control values collapse per client/target, but transport commands
  // remain ordered barriers. This models a busy UI that drains once per frame.
  first.drainEvents(); second.drainEvents(); server.drainEvents();
  for (int i = 0; i < 60; ++i) first.sendCommand("fader-a-" + juce::String(i), "mix.volume", i / 100.0, "master");
  first.sendCommand("barrier", "transport.stop");
  for (int i = 0; i < 60; ++i) first.sendCommand("fader-b-" + juce::String(i), "mix.volume", i / 100.0, "master");
  first.sendCommand("end-burst", "test.barrier");
  std::vector<juce::String> ids;
  if (!waitUntil([&] {
    for (const auto &event : server.drainEvents()) {
      if (event.kind == RemoteSession::Event::Kind::Command)
        ids.push_back(juce::JSON::parse(event.text).getProperty("id", {}).toString());
    }
    return std::find(ids.begin(), ids.end(), "end-burst") != ids.end();
  })) { std::cerr << "Burst lost its final action\n"; return 1; }
  const auto a = std::find(ids.begin(), ids.end(), "fader-a-59");
  const auto barrier = std::find(ids.begin(), ids.end(), "barrier");
  const auto b = std::find(ids.begin(), ids.end(), "fader-b-59");
  if (a == ids.end() || b == ids.end() || barrier == ids.end() || !(a < barrier && barrier < b) || ids.size() >= 122) {
    std::cerr << "Fader coalescing lost ordering or did not reduce work\n"; return 1;
  }

  // A real TCP peer completes WebSocket upgrade, then stops reading. Large
  // frames fill its receive window; publishing must still return promptly and
  // the healthy second client must receive the final ack followed by its state.
  juce::StreamingSocket slowPeer;
  if (!slowPeer.connect("127.0.0.1", server.getPort(), 2000)) return 1;
  const std::string handshake = "GET /remote HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
  slowPeer.write(handshake.data(), static_cast<int>(handshake.size()));
  if (!waitUntil([&] { return server.getClientCount() == 3; })) return 1;
  double maxPublishMs = 0;
  const juce::String padding = juce::String::repeatedString("x", 512 * 1024);
  for (int i = 0; i < 40; ++i) {
    auto value = juce::JSON::parse(R"({"v":1,"type":"snapshot"})");
    value.getDynamicObject()->setProperty("seq", 100 + i);
    value.getDynamicObject()->setProperty("padding", padding);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    server.publishState(value, waveform, hash);
    maxPublishMs = std::max(maxPublishMs, juce::Time::getMillisecondCounterHiRes() - start);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  if (maxPublishMs > 100.0) { std::cerr << "Slow reader blocked publication: " << maxPublishMs << " ms\n"; return 1; }
  second.drainEvents();
  server.sendAck("after-pressure", true);
  server.publishState(juce::JSON::parse(R"({"v":1,"type":"snapshot","seq":200})"), waveform, hash);
  bool ackAfterPressure = false, finalState = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      if (event.text.empty()) continue;
      const auto json = juce::JSON::parse(event.text);
      if (json.getProperty("id", {}).toString() == "after-pressure") ackAfterPressure = true;
      if (static_cast<int>(json.getProperty("seq", 0)) == 200) finalState = ackAfterPressure;
    }
    return finalState;
  })) { std::cerr << "Slow peer stalled healthy client or reordered ack/state\n"; return 1; }
  slowPeer.close();
  std::cout << "slow-reader max publish: " << maxPublishMs << " ms; burst events: " << ids.size() << "\n";
  std::cout << "remote session loopback passed\n";
  return 0;
}
