#pragma once

#include "ProjectStore.h"

/** Explicit, offline installation of the bundled learning song. */
class DemoSongService final {
public:
  static constexpr const char *kProjectId =
      "7f3e2cd6-ff35-5b0a-a726-1dfdf8ccc66c";

  static juce::String projectId() { return kProjectId; }
  /** Uses the portable identity, never the title, artist or local library ID. */
  static bool isDemoSong(const ImportedSongData &song);

  /**
   * Synchronous disk work; callers may run it on their import worker and must
   * serialize it with other library changes. Progress runs on the calling
   * thread. The caller controls playback and UI presentation.
   *
   * An existing usable entry is returned unchanged, preserving its package,
   * local settings and setlist memberships. A missing entry is installed from
   * the original bundled package, including after the user deletes the demo.
   * An existing damaged entry returns an error without resetting its edits.
   *
   * The output is cleared on failure. No account, entitlement, selection,
   * playback or existing setlist is changed by this operation.
   */
  static juce::Result ensureInstalled(
      ImportedSongData &outSong, const ProjectProgressFn &onProgress = {});
};
