#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace ProjectArtwork {
using DownloadToDirectory = std::function<bool(
    const juce::String &, const juce::File &, juce::String &)>;

/** Resolve the cover reference produced by the creation wizard before packing.
    Existing local covers are preserved. A manually chosen file must still be
    available; remote covers retain the library's best-effort download policy.
    Only a private staging directory is written, never the selected source.
*/
inline bool resolveMissingLocalFile(const juce::String &sourceReference,
                                    juce::String &localPath,
                                    const juce::File &stagingDirectory,
                                    juce::String &error,
                                    const DownloadToDirectory &download) {
  error.clear();
  if (const juce::File existing(localPath);
      existing.existsAsFile() && existing.getSize() > 0)
    return true;

  localPath.clear();
  const auto source = sourceReference.trim();
  if (source.isEmpty())
    return true;

  if (source.startsWithIgnoreCase("manual:")) {
    const juce::File selected(source.substring(7).trim());
    if (!selected.existsAsFile() || selected.getSize() <= 0) {
      error = "Artwork file not found: " + selected.getFullPathName();
      return false;
    }
    auto extension = selected.getFileExtension().toLowerCase();
    if (extension == ".jpeg")
      extension = ".jpg";
    if (extension != ".jpg" && extension != ".png" && extension != ".webp")
      extension = ".jpg";
    const auto target = stagingDirectory.getChildFile("artwork" + extension);
    if (stagingDirectory.createDirectory().failed() ||
        !selected.copyFileTo(target)) {
      error = "Couldn't prepare selected artwork for the project.";
      return false;
    }
    localPath = target.getFullPathName();
    return true;
  }

  if ((source.startsWithIgnoreCase("https://") ||
       source.startsWithIgnoreCase("http://")) &&
      download && stagingDirectory.createDirectory().wasOk()) {
    juce::String downloadedPath;
    if (download(source, stagingDirectory, downloadedPath)) {
      const juce::File downloaded(downloadedPath);
      if (downloaded.existsAsFile() && downloaded.getSize() > 0)
        localPath = downloaded.getFullPathName();
    }
  }
  // A failed remote cover must not prevent saving usable audio. Its original
  // reference remains in the manifest so opening can retry the existing cache
  // download path. Once a local cover is packaged, save clears that reference.
  return true;
}
} // namespace ProjectArtwork
