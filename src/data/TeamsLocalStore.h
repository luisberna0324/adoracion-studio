#pragma once

#include <juce_core/juce_core.h>

#include <vector>

/** Immutable origin for one downloaded, locally editable Team song revision. */
struct TeamSongLink {
  juce::String uid, teamId, songId;
  int revision = 0;
  juce::String localSongId;
};

/** Account-scoped metadata cache. It never stores credentials or audio blobs. */
class TeamsLocalStore final {
public:
  static juce::String onboardingLessonId(const juce::String &uid, const juce::String &teamId,
                                         const juce::String &membershipId);
  static bool saveOverview(const juce::String &uid, const juce::var &overview);
  static juce::var loadOverview(const juce::String &uid);
  static bool saveDetails(const juce::String &uid, const juce::String &teamId,
                          const juce::var &details);
  static juce::var loadDetails(const juce::String &uid,
                              const juce::String &teamId);

  /** Existing mappings are idempotent; remapping a revision is rejected. */
  static bool upsertSongLink(const TeamSongLink &link);
  static bool findSongLink(const juce::String &uid, const juce::String &teamId,
                           const juce::String &songId, int revision,
                           TeamSongLink &result);
  static std::vector<TeamSongLink> loadSongLinks(const juce::String &uid,
                                              const juce::String &teamId);

  static bool removeSongLinks(const juce::String &uid, const juce::String &teamId);
  /** Uses the same MTControls data root as SongLibraryStore. */
  static juce::File getAccountDirectory(const juce::String &uid);
};
