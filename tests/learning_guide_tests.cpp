#include "ui/learning/LearningGuideModel.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>

using namespace playworship::learning;

namespace {
void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Learning guide: " << message << '\n';
    std::exit(1);
  }
}

struct TemporaryDirectory {
  juce::File directory = juce::File(juce::String(std::filesystem::temp_directory_path().string()))
                             .getNonexistentChildFile("playworship-learning-tests", {}, false);
  TemporaryDirectory() { expect(directory.createDirectory().wasOk(), "create isolated temporary directory"); }
  ~TemporaryDirectory() { directory.deleteRecursively(); }
};

const Lesson &findLesson(const std::vector<Lesson> &lessons, const char *id) {
  const auto found = std::find_if(lessons.begin(), lessons.end(),
                                [id](const Lesson &lesson) { return lesson.id == id; });
  expect(found != lessons.end(), "find expected lesson");
  return *found;
}

const Step &findStep(const Lesson &lesson, const char *id) {
  const auto found = std::find_if(lesson.steps.begin(), lesson.steps.end(),
                                [id](const Step &step) { return step.id == id; });
  expect(found != lesson.steps.end(), "find expected step");
  return *found;
}

void verifyCatalog(const std::vector<Lesson> &spanish, const std::vector<Lesson> &english) {
  expect(spanish.size() == 5 && english.size() == spanish.size(), "five complete localized courses");
  std::set<juce::String> lessonIds;
  for (size_t i = 0; i < spanish.size(); ++i) {
    const auto &lesson = spanish[i];
    const auto &translated = english[i];
    expect(lesson.id.isNotEmpty() && lessonIds.insert(lesson.id).second, "unique stable lesson IDs");
    expect(lesson.id == translated.id && lesson.steps.size() == translated.steps.size(),
           "localization preserves course and step identity");
    expect(lesson.title.isNotEmpty() && lesson.description.isNotEmpty() && !lesson.steps.empty(),
           "course has display content");
    expect(translated.title.isNotEmpty() && translated.description.isNotEmpty(), "English course has display content");
    std::set<juce::String> stepIds;
    for (size_t j = 0; j < lesson.steps.size(); ++j) {
      const auto &step = lesson.steps[j];
      const auto &translation = translated.steps[j];
      expect(step.id.isNotEmpty() && stepIds.insert(step.id).second, "unique stable step IDs within course");
      expect(step.id == translation.id && step.anchor == translation.anchor &&
                 step.needsSong == translation.needsSong && step.condition == translation.condition,
             "localization preserves interactive behavior");
      expect(step.title.isNotEmpty() && step.description.isNotEmpty() && step.task.isNotEmpty(), "Spanish step has useful content");
      expect(translation.title.isNotEmpty() && translation.description.isNotEmpty() && translation.task.isNotEmpty(),
             "English step has useful content");
      expect(step.description != translation.description && step.task != translation.task,
             "English descriptions and tasks are translated");
      expect(step.task.length() < 100 && translation.task.length() < 100, "step tasks are short");
    }
  }
}
} // namespace

int main() {
  const auto desktop = makeLessons(false, false);
  const auto desktopEnglish = makeLessons(false, true);
  const auto phone = makeLessons(true, false);
  const auto phoneEnglish = makeLessons(true, true);
  verifyCatalog(desktop, desktopEnglish);
  verifyCatalog(phone, phoneEnglish);
  const auto &first = findLesson(desktop, "first-steps");
  expect(findStep(first, "welcome").anchor == Anchor::None && !findStep(first, "welcome").needsSong,
         "welcome works with an empty library");
  expect(findStep(first, "choose-song").anchor == Anchor::SongSelection && !findStep(first, "choose-song").needsSong,
         "song selection is available before loading");
  expect(findStep(first, "play-pause").needsSong && findStep(first, "stop").needsSong,
         "transport lessons require a loaded song");
  const auto &mix = findLesson(desktop, "mix-click");
  expect(findStep(mix, "instrument-level").anchor == Anchor::Mixer, "fader targets the mixer");
  expect(findStep(mix, "metronome").anchor == Anchor::Metronome, "metronome targets its control");
  expect(findStep(mix, "tempo").anchor == Anchor::Tempo && findStep(mix, "key").anchor == Anchor::Key,
         "tempo and key target their actual controls");
  expect(findStep(mix, "pads").anchor == Anchor::Pads && !findStep(mix, "pads").needsSong,
         "independent Pads lesson does not require a song");
  const auto &phonePractice = findLesson(phone, "practice-sections");
  const auto &desktopSections = findLesson(desktop, "practice-sections");
  expect(findStep(phonePractice, "open-practice").anchor == Anchor::More &&
             findStep(phonePractice, "open-practice").needsSong,
         "phone practice starts from More and requires a song");
  expect(findStep(phonePractice, "practice-navigation").anchor == Anchor::Waveform &&
             findStep(phonePractice, "practice-ab").anchor == Anchor::PracticeLoop,
         "practice gestures target the waveform and A-B exposes waveform plus region controls");
  expect(findStep(desktopSections, "section-automation").anchor == Anchor::Waveform &&
             desktopSections.title != phonePractice.title &&
             desktopSections.steps.front().id != phonePractice.steps.front().id,
         "desktop and tablet teach sections instead of phone-only Practice");
  const auto &server = findLesson(desktop, "remote-server");
  const auto &client = findLesson(desktop, "remote-client");
  for (const auto &lesson : desktop)
    expect(lesson.id != "settings-remote" && !lesson.id.contains("reconnect"),
           "retired general remote course and connection-recovery course are absent");
  for (const auto *course : {&server, &client}) {
    expect(course->steps.front().condition == StepCondition::None,
           "remote course first explains the prerequisite second device");
    std::set<StepCondition> conditions;
    for (const auto &step : course->steps) {
      conditions.insert(step.condition);
      expect(!step.id.contains("reconnect"), "remote courses do not include a reconnection exercise");
      if (step.anchor == Anchor::Settings || step.anchor == Anchor::RemoteSettings ||
          step.anchor == Anchor::RemoteMode || step.anchor == Anchor::RemoteAlias ||
          step.anchor == Anchor::RemoteStatus || step.anchor == Anchor::RemoteServers)
        expect(!step.needsSong, "settings remain usable before a song is loaded");
    }
    for (const auto condition : {StepCondition::SettingsOpen, StepCondition::RemotePageOpen,
                                StepCondition::SettingsClosed, StepCondition::SongLoaded})
      expect(conditions.count(condition) == 1, "each remote course opens Settings and returns to a loaded song");
  }
  expect(findStep(server, "server-enable").anchor == Anchor::RemoteMode &&
             findStep(server, "server-enable").condition == StepCondition::ServerModeActive &&
             findStep(server, "server-listening").condition == StepCondition::ServerListening &&
             findStep(server, "server-connect-client").condition == StepCondition::ServerHasClient,
         "server course requires activation, availability and a connected client");
  expect(findStep(server, "server-name").anchor == Anchor::RemoteAlias &&
             findStep(server, "server-name").condition == StepCondition::None,
         "a custom alias is optional and does not force a settings mutation");
  expect(findStep(client, "client-enable").anchor == Anchor::RemoteMode &&
             findStep(client, "client-enable").condition == StepCondition::ClientModeActive &&
             findStep(client, "client-connect").anchor == Anchor::RemoteServers &&
             findStep(client, "client-connect").condition == StepCondition::RemoteConnected,
         "client course requires the real mode and a full connected state");
  expect(findStep(client, "client-indicator").anchor == Anchor::RemoteIndicator &&
             findStep(client, "client-play").anchor == Anchor::PlayPause &&
             findStep(client, "client-play").condition == StepCondition::RemotePlayApplied &&
             findStep(client, "client-stop").anchor == Anchor::Stop &&
             findStep(client, "client-stop").condition == StepCondition::RemoteStopApplied,
         "client practice targets the indicator and requires confirmed Play and Stop");
  for (const auto *course : {&first, &mix, &phonePractice, &desktopSections})
    for (const auto &step : course->steps)
      expect(step.condition == StepCondition::None, "existing introductory courses keep their manual progression");

  StepFacts facts;
  expect(evaluateStepCondition(StepCondition::None, facts) &&
             evaluateStepCondition(StepCondition::SettingsClosed, facts),
         "explanatory steps and an already closed Settings page are available");
  for (const auto condition : {StepCondition::SettingsOpen, StepCondition::RemotePageOpen,
                              StepCondition::ServerModeActive, StepCondition::ServerListening,
                              StepCondition::ServerHasClient, StepCondition::ClientModeActive,
                              StepCondition::RemoteConnected, StepCondition::SongLoaded,
                              StepCondition::RemotePlayApplied, StepCondition::RemoteStopApplied})
    expect(!evaluateStepCondition(condition, facts), "missing real actions cannot unlock actionable steps");
  facts.remotePageOpen = true;
  expect(!evaluateStepCondition(StepCondition::RemotePageOpen, facts),
         "stale destination does not imply Settings is open");
  facts.settingsOpen = true;
  expect(evaluateStepCondition(StepCondition::SettingsOpen, facts) &&
             evaluateStepCondition(StepCondition::RemotePageOpen, facts) &&
             !evaluateStepCondition(StepCondition::SettingsClosed, facts),
         "Settings navigation reflects the current visible page");
  facts.serverListening = facts.serverHasClient = true;
  expect(!evaluateStepCondition(StepCondition::ServerListening, facts) &&
             !evaluateStepCondition(StepCondition::ServerHasClient, facts),
         "stale listening or client facts cannot count after leaving Server mode");
  facts.serverModeActive = true;
  expect(evaluateStepCondition(StepCondition::ServerModeActive, facts) &&
             evaluateStepCondition(StepCondition::ServerListening, facts) &&
             evaluateStepCondition(StepCondition::ServerHasClient, facts),
         "server activation and the live connected client unlock their steps");
  facts.serverListening = false;
  expect(!evaluateStepCondition(StepCondition::ServerHasClient, facts),
         "a stopped listener cannot satisfy the connected-client step");
  facts.remoteConnected = facts.remotePlayApplied = facts.remoteStopApplied = true;
  expect(!evaluateStepCondition(StepCondition::RemoteConnected, facts) &&
             !evaluateStepCondition(StepCondition::RemotePlayApplied, facts) &&
             !evaluateStepCondition(StepCondition::RemoteStopApplied, facts),
         "remote evidence cannot satisfy a client exercise outside Client mode");
  facts.clientModeActive = true;
  expect(evaluateStepCondition(StepCondition::ClientModeActive, facts) &&
             evaluateStepCondition(StepCondition::RemoteConnected, facts) &&
             !evaluateStepCondition(StepCondition::RemotePlayApplied, facts),
         "connected empty session cannot complete a song playback exercise");
  facts.songLoaded = true;
  facts.remotePlayApplied = facts.remoteStopApplied = false;
  expect(evaluateStepCondition(StepCondition::SongLoaded, facts) &&
             !evaluateStepCondition(StepCondition::RemotePlayApplied, facts) &&
             !evaluateStepCondition(StepCondition::RemoteStopApplied, facts),
         "a loaded remote song does not substitute for this client's confirmed commands");
  facts.remotePlayApplied = true;
  expect(evaluateStepCondition(StepCondition::RemotePlayApplied, facts) &&
             !evaluateStepCondition(StepCondition::RemoteStopApplied, facts),
         "Play confirmation does not unlock the separate Stop exercise");
  facts.remoteStopApplied = true;
  expect(evaluateStepCondition(StepCondition::RemoteStopApplied, facts),
         "Stop confirmation unlocks its own exercise");
  facts.remoteConnected = false;
  expect(!evaluateStepCondition(StepCondition::RemotePlayApplied, facts) &&
             !evaluateStepCondition(StepCondition::RemoteStopApplied, facts),
         "a disconnected client cannot progress with stale command confirmations");

  StepFacts resumed;
  const auto &playExercise = findStep(client, "client-play");
  const auto &stopExercise = findStep(client, "client-stop");
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::OpenSettings,
         "a playback exercise resumed in Local highlights Settings instead of an unusable Play control");
  resumed.settingsOpen = true;
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::OpenRemotePage,
         "resume directs an open Settings panel to the remote page first");
  resumed.remotePageOpen = true;
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::ChooseClient,
         "resume on the remote page directs the user to Client mode");
  resumed.clientModeActive = true;
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::WaitClient,
         "a client without the full server state waits at discovery and connection controls");
  resumed.remotePageOpen = false;
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::OpenRemotePage,
         "connection prerequisites remain reachable from another Settings page");
  resumed.settingsOpen = false;
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::OpenSettings,
         "an unconnected Client with Settings closed can reopen the real server selector");
  resumed.remoteConnected = true;
  resumed.settingsOpen = true;
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::CloseSettings,
         "a connected playback exercise points to Close rather than a control underneath Settings");
  resumed.settingsOpen = false;
  resumed.songLoaded = true;
  expect(remoteStepPrerequisite(client.id, playExercise, resumed) == RemotePrerequisite::None &&
             !evaluateStepCondition(playExercise.condition, resumed) &&
             !evaluateStepCondition(stopExercise.condition, resumed),
         "recovered prerequisites return to the saved exercise without faking confirmed Play or Stop");
  for (const auto *step : {&findStep(client, "client-load-song"), &stopExercise,
                           &findStep(client, "client-indicator")}) {
    resumed.settingsOpen = true;
    expect(remoteStepPrerequisite(client.id, *step, resumed) == RemotePrerequisite::CloseSettings,
           "all client player destinations remain reachable after restoring the connection in Settings");
  }
  for (const auto *step : {&findStep(client, "client-prepare"),
                           &findStep(client, "client-open-settings"),
                           &findStep(client, "client-open-remote"),
                           &findStep(client, "client-enable"),
                           &findStep(client, "client-ready")})
    expect(remoteStepPrerequisite(client.id, *step, {}) == RemotePrerequisite::None,
           "preparation, explicit navigation, initial mode choice and summary keep their designed steps");

  resumed = {};
  const auto &serverName = findStep(server, "server-name");
  expect(remoteStepPrerequisite(server.id, serverName, resumed) == RemotePrerequisite::OpenSettings,
         "resuming the optional server alias first restores access to Server mode");
  resumed.settingsOpen = resumed.remotePageOpen = true;
  expect(remoteStepPrerequisite(server.id, serverName, resumed) == RemotePrerequisite::ChooseServer,
         "server exercises direct a Local device to the existing Server selector");
  resumed.serverModeActive = true;
  expect(remoteStepPrerequisite(server.id, serverName, resumed) == RemotePrerequisite::WaitServer,
         "an unready server listener is a prerequisite even for an explanatory alias step");
  resumed.serverListening = true;
  expect(remoteStepPrerequisite(server.id, serverName, resumed) == RemotePrerequisite::None &&
             remoteStepPrerequisite(server.id, findStep(server, "server-connect-client"), resumed) ==
                 RemotePrerequisite::None,
         "a ready listener returns to alias review or the original connected-client exercise");
  expect(remoteStepPrerequisite(server.id, findStep(server, "server-load-song"), resumed) ==
             RemotePrerequisite::CloseSettings,
         "server song loading returns from Settings to the player after the listener is restored");
  resumed.settingsOpen = false;
  expect(remoteStepPrerequisite(server.id, findStep(server, "server-load-song"), resumed) ==
             RemotePrerequisite::None,
         "server player exercise resumes unchanged once its prerequisites are restored");
  for (const auto *course : {&first, &mix, &phonePractice, &desktopSections})
    for (const auto &step : course->steps)
      expect(remoteStepPrerequisite(course->id, step, {}) == RemotePrerequisite::None,
             "remote resume prerequisites do not alter unrelated introductory guides");

  TemporaryDirectory temporary;
  const auto file = temporary.directory.getChildFile("nested").getChildFile("progress.json");
  {
    ProgressStore store(file);
    expect(!file.exists() && !store.hasProgress(first) && store.resumeIndex(first) == 0,
           "opening a fresh store neither writes nor touches real preferences");
    expect(store.record(first, 2, false).wasOk(), "record current step");
    expect(file.existsAsFile() && store.hasProgress(first) && store.resumeIndex(first) == 2,
           "recorded step is available immediately");
    expect(store.get(first.id).stepId == first.steps[2].id && !store.get(first.id).completed,
           "stored state uses stable step IDs");
    const auto encoded = juce::JSON::parse(file);
    expect(encoded["version"] == juce::var(1) &&
               encoded["lessons"].getProperty(juce::Identifier(first.id), {})["stepId"].toString() == first.steps[2].id,
           "serialized schema persists an ID rather than an index");
    const auto stamp = juce::Time(1500000000000LL);
    expect(file.setLastModificationTime(stamp), "set timestamp for unchanged-write regression");
    const auto savedTime = file.getLastModificationTime();
    expect(store.record(first, 2, false).wasOk() && file.getLastModificationTime() == savedTime,
           "unchanged step does not rewrite its file");
    expect(store.record(first, -1, false).failed() &&
               store.record(first, static_cast<int>(first.steps.size()), false).failed(),
           "invalid step indices are rejected");
    expect(store.resumeIndex(first) == 2, "invalid record keeps previous progress");
  }
  {
    ProgressStore restored(file);
    expect(restored.hasProgress(first) && restored.resumeIndex(first) == 2 &&
               !restored.get(first.id).completed,
           "progress survives a store restart");
    auto reordered = first;
    std::swap(reordered.steps[0], reordered.steps[2]);
    expect(restored.resumeIndex(reordered) == 0 && restored.hasProgress(reordered),
           "resume follows the same step after course reordering");
    auto changed = first;
    changed.steps.erase(changed.steps.begin() + 2);
    expect(restored.resumeIndex(changed) == 0 && !restored.hasProgress(changed),
           "a removed step safely resumes at the first step");
    expect(restored.record(first, static_cast<int>(first.steps.size()) - 1, true).wasOk(), "record course completion");
  }
  {
    ProgressStore completed(file);
    expect(completed.get(first.id).completed && completed.hasProgress(first), "completion round-trips");
    expect(completed.restart(first).wasOk() && completed.resumeIndex(first) == 0 &&
               !completed.get(first.id).completed,
           "restart resets completion and selects the first step");
    const auto stamp = juce::Time(1500000000000LL);
    expect(file.setLastModificationTime(stamp), "set timestamp before repeated restart");
    const auto savedTime = file.getLastModificationTime();
    expect(completed.restart(first).wasOk() && file.getLastModificationTime() == savedTime,
           "restart at an already fresh first step avoids redundant writes");
    expect(completed.record(phonePractice, 2, false).wasOk(), "record phone-specific progress");
    expect(completed.resumeIndex(desktopSections) == 0 && !completed.hasProgress(desktopSections),
           "phone-only steps do not resume into unrelated desktop actions");
  }
  {
    ProgressStore restarted(file);
    expect(restarted.get(first.id).stepId == first.steps.front().id && !restarted.get(first.id).completed,
           "restart persists its reset");
    expect(restarted.resumeIndex(phonePractice) == 2, "independent course progress survives another course reset");
  }

  const auto invalid = temporary.directory.getChildFile("invalid.json");
  for (const auto *payload : {"not JSON", "[]", "null", "{}", "{\"version\":2,\"lessons\":{}}",
                             "{\"version\":\"1\",\"lessons\":{}}", "{\"version\":1,\"lessons\":[]}"}) {
    expect(invalid.replaceWithText(payload), "write malformed test fixture");
    ProgressStore parsed(invalid);
    expect(!parsed.hasProgress(first) && parsed.resumeIndex(first) == 0, "malformed or future schema defaults safely");
  }
  expect(invalid.replaceWithText(R"({"version":1,"lessons":{"first-steps":{"stepId":"play-pause","completed":false},"mix-click":{"stepId":12,"completed":true},"remote-server":{"stepId":"server-open-settings","completed":"true"},"bad id":{"stepId":"welcome","completed":false}}})"),
         "write mixed-validity payload");
  {
    ProgressStore mixed(invalid);
    expect(mixed.resumeIndex(first) == 2 && mixed.hasProgress(first), "valid records survive invalid neighbours");
    expect(!mixed.hasProgress(mix) && !mixed.hasProgress(server) && mixed.get("bad id").stepId.isEmpty(),
           "wrong value types and unsafe IDs are ignored");
  }
  expect(invalid.replaceWithText(R"({"version":1,"lessons":{"settings-remote":{"stepId":"remote-reconnect","completed":true}}})"),
         "write completion from the retired general remote course");
  {
    ProgressStore retired(invalid);
    expect(!retired.hasProgress(server) && !retired.hasProgress(client) &&
               retired.resumeIndex(server) == 0 && retired.resumeIndex(client) == 0,
           "old reading-only completion does not complete the new practical courses");
    expect(retired.record(server, 4, false).wasOk() && retired.record(client, 9, false).wasOk(),
           "remote courses record independent stable steps");
    ProgressStore restored(invalid);
    expect(restored.resumeIndex(server) == 4 && restored.resumeIndex(client) == 9,
           "remote progress round-trips independently on both courses");
  }
  expect(invalid.replaceWithText(R"({"version":1,"lessons":{"first-steps":{"stepId":"retired-step","completed":true}}})"),
         "write unknown-step payload");
  {
    ProgressStore unknown(invalid);
    expect(unknown.resumeIndex(first) == 0 && !unknown.hasProgress(first), "unknown completed step cannot mark current course complete");
    expect(unknown.restart(first).wasOk() && unknown.hasProgress(first) && !unknown.get(first.id).completed,
           "restart repairs an unknown step");
  }
  expect(invalid.replaceWithText(juce::String::repeatedString("x", 64 * 1024 + 1)), "write oversized payload");
  expect(!ProgressStore(invalid).hasProgress(first), "oversized payload is not parsed");

  auto *manyEntries = new juce::DynamicObject();
  for (int i = 0; i < 130; ++i) {
    auto *entry = new juce::DynamicObject();
    entry->setProperty("stepId", "first");
    entry->setProperty("completed", false);
    manyEntries->setProperty("course-" + juce::String(i), juce::var(entry));
  }
  auto *manyRoot = new juce::DynamicObject();
  manyRoot->setProperty("version", 1);
  manyRoot->setProperty("lessons", juce::var(manyEntries));
  expect(invalid.replaceWithText(juce::JSON::toString(juce::var(manyRoot))), "write bounded-count fixture");
  {
    ProgressStore bounded(invalid);
    int count = 0;
    for (int i = 0; i < 130; ++i)
      count += bounded.get("course-" + juce::String(i)).stepId.isNotEmpty() ? 1 : 0;
    expect(count == 128 && bounded.record(first, 0, false).failed(), "loaded and recorded progress has a sane course limit");
  }

  const auto impossibleTarget = temporary.directory.getChildFile("existing-directory.json");
  expect(impossibleTarget.createDirectory().wasOk(), "create replacement-error fixture");
  {
    ProgressStore cannotSave(impossibleTarget);
    expect(cannotSave.record(first, 1, false).failed(), "write failure reaches the UI as a Result");
    expect(!cannotSave.hasProgress(first) && cannotSave.resumeIndex(first) == 0 && impossibleTarget.isDirectory(),
           "failed atomic save rolls memory back and preserves the original target");
  }
  Lesson empty;
  expect(ProgressStore(file).restart(empty).failed(), "empty course cannot be persisted");
  std::cout << "Learning guide: all regression checks passed\n";
}
