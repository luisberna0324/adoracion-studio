#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace playworship::learning {

enum class Anchor { None, SongSelection, PlayPause, Stop, Waveform, Mixer,
                    Metronome, Tempo, Key, Pads, More, Settings, PracticeLoop, PracticeExit };

struct Step {
  juce::String id;
  juce::String title;
  juce::String description;
  juce::String task;
  Anchor anchor = Anchor::None;
  bool needsSong = false;
};

struct Lesson {
  juce::String id;
  juce::String title;
  juce::String description;
  std::vector<Step> steps;
};

// Content is resolved in the current language and for the real phone features.
std::vector<Lesson> makeLessons(bool phone, bool english);

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
