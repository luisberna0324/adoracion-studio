#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace playworship::learning {

enum class Anchor { None, SongSelection, PlayPause, Stop, Waveform, Mixer,
                    Metronome, Tempo, Key, Pads, More, Settings, PracticeLoop, PracticeExit,
                    RemoteSettings, RemoteMode, RemoteAlias, RemoteStatus, RemoteServers,
                    SettingsClose, RemoteIndicator, SharedCloud };

// None is an explanatory step. Other steps require facts from the real app;
// enabling Continue never performs the operation for the user.
enum class StepCondition { None, SettingsOpen, RemotePageOpen, ServerModeActive,
                           ServerListening, ServerHasClient, ClientModeActive,
                           RemoteConnected, SettingsClosed, SongLoaded,
                           RemotePlayApplied, RemoteStopApplied, CloudOpen, SharedSongDownloaded, SharedSongAdded };

struct StepFacts {
  bool cloudOpen = false, sharedSongDownloaded = false, sharedSongAdded = false;
  bool sharedCloudAccessible = false;
  bool settingsOpen = false;
  bool remotePageOpen = false;
  bool serverModeActive = false;
  bool serverListening = false;
  bool serverHasClient = false;
  bool clientModeActive = false;
  bool remoteConnected = false;
  bool songLoaded = false;
  // True only after this guide step's own command is accepted and its
  // subsequent snapshot confirms the result; another client's change is not
  // evidence that this device performed the exercise.
  bool remotePlayApplied = false;
  bool remoteStopApplied = false;
};

bool evaluateStepCondition(StepCondition condition, const StepFacts &facts);

struct Step {
  juce::String id;
  juce::String title;
  juce::String description;
  juce::String task;
  Anchor anchor = Anchor::None;
  bool needsSong = false;
  StepCondition condition = StepCondition::None;
};

// Missing setup is shown through the existing controls while preserving the
// persisted exercise. Restoring a session never substitutes for its own
// confirmed Play or Stop command.
enum class RemotePrerequisite { None, OpenSettings, OpenRemotePage, ChooseServer,
                                WaitServer, ChooseClient, WaitClient, CloseSettings };

RemotePrerequisite remoteStepPrerequisite(const juce::String &lessonId,
                                         const Step &step, const StepFacts &facts);

struct Lesson {
  juce::String id;
  juce::String title;
  juce::String description;
  std::vector<Step> steps;
};

// Content is resolved in the current language and for the real phone features.
std::vector<Lesson> makeLessons(bool phone, bool english);

Lesson makeSharedCloudLesson(const juce::String &id, const juce::String &owner, bool english);

struct LessonProgress {
  juce::String stepId;
  bool completed = false;
};

// Learning belongs to this device, independently of songs, accounts and LAN.
class ProgressStore {
public:
  explicit ProgressStore(juce::File file = defaultFile());
  static juce::File defaultFile();
  LessonProgress get(const juce::String &lessonId) const;
  int resumeIndex(const Lesson &lesson) const;
  bool hasProgress(const Lesson &lesson) const;
  juce::Result record(const Lesson &lesson, int stepIndex, bool completed);
  juce::Result restart(const Lesson &lesson);

private:
  void load();
  juce::Result save() const;
  juce::File file;
  std::map<juce::String, LessonProgress> progress;
};

} // namespace playworship::learning
