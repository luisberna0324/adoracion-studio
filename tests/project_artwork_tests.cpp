#include "data/ProjectArtwork.h"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool value, const char *message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

struct TemporaryArtworkDirectory {
  juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("playworship-artwork-test", {}, false);
  TemporaryArtworkDirectory() {
    expect(root.createDirectory().wasOk(), "create isolated artwork test directory");
  }
  ~TemporaryArtworkDirectory() { root.deleteRecursively(); }
};

constexpr char coverBytes[] = "deterministic-cover-bytes";
bool writeCover(const juce::File &file) {
  return file.replaceWithData(coverBytes, sizeof(coverBytes));
}

void testManualCover(const juce::File &root) {
  const auto selected = root.getChildFile("chosen-cover.jpeg");
  expect(writeCover(selected), "create selected cover");
  const auto stage = root.getChildFile("manual-stage");
  juce::String path, error;
  int downloads = 0;
  const auto downloader = [&](const juce::String &, const juce::File &,
                               juce::String &) {
    ++downloads;
    return false;
  };
  expect(ProjectArtwork::resolveMissingLocalFile(
             "manual:" + selected.getFullPathName(), path, stage, error, downloader),
         "manual-only draft resolves its artwork before packing");
  expect(error.isEmpty() && downloads == 0,
         "manual cover does not access the network");
  expect(juce::File(path) == stage.getChildFile("artwork.jpg") &&
             juce::File(path).getSize() == sizeof(coverBytes) && selected.existsAsFile(),
         "manual cover is copied into private staging, preserving the source");

  // Exercise the same ZIP entry shape used by ProjectStore. The archive must
  // contain cover bytes, not a dependency on the original manual file path.
  const auto archive = root.getChildFile("cover-roundtrip.pwproj");
  {
    juce::ZipFile::Builder builder;
    builder.addFile(juce::File(path), 0, "artwork.jpg");
    juce::FileOutputStream output(archive);
    expect(output.openedOk() && builder.writeToStream(output, nullptr),
           "embed staged artwork in the portable package");
  }
  expect(selected.deleteFile() && stage.deleteRecursively(),
         "remove original/staging cover after packaging");
  juce::ZipFile zip(archive);
  const int index = zip.getIndexOfFileName("artwork.jpg");
  expect(index >= 0, "portable package retains the cover entry offline");
  const auto stream = std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(index));
  juce::MemoryBlock bytes;
  expect(stream != nullptr && stream->readIntoMemoryBlock(bytes) == sizeof(coverBytes) &&
             bytes.matches(coverBytes, sizeof(coverBytes)),
         "packaged cover survives removal of its source");

  path = root.getChildFile("missing-old-cover.png").getFullPathName();
  expect(!ProjectArtwork::resolveMissingLocalFile(
             "manual:" + selected.getFullPathName(), path, stage, error, downloader) &&
             path.isEmpty() && error.isNotEmpty(),
         "missing selected cover reports failure instead of silently disappearing");
}

void testExistingAndRemoteCover(const juce::File &root) {
  const auto existing = root.getChildFile("existing.png");
  expect(writeCover(existing), "create already-resolved cover");
  const auto stage = root.getChildFile("remote-stage");
  const juce::String remote = "https://example.invalid/cover.png";
  juce::String path = existing.getFullPathName(), error;
  int downloads = 0;
  const auto downloader = [&](const juce::String &url, const juce::File &directory,
                               juce::String &result) {
    ++downloads;
    expect(url == remote && directory == stage,
           "remote downloader receives the source and private staging directory");
    const auto file = directory.getChildFile("artwork.png");
    result = file.getFullPathName();
    return writeCover(file);
  };
  expect(ProjectArtwork::resolveMissingLocalFile(remote, path, stage, error, downloader) &&
             path == existing.getFullPathName() && downloads == 0,
         "existing persisted artwork is not redownloaded or replaced");
  path.clear();
  expect(ProjectArtwork::resolveMissingLocalFile(remote, path, stage, error, downloader) &&
             downloads == 1 && juce::File(path).existsAsFile(),
         "remote-only draft resolves a local cover for portable packaging");

  path = root.getChildFile("stale.png").getFullPathName();
  expect(ProjectArtwork::resolveMissingLocalFile(
             remote, path, stage, error,
             [](const juce::String &, const juce::File &, juce::String &) { return false; }) &&
             path.isEmpty() && error.isEmpty(),
         "offline remote cover preserves best-effort save semantics without a stale path");

  expect(ProjectArtwork::resolveMissingLocalFile({}, path, stage, error, downloader) &&
             path.isEmpty() && downloads == 1,
         "creation without artwork never invokes downloader");
}
} // namespace

int main() {
  TemporaryArtworkDirectory temporary;
  testManualCover(temporary.root);
  testExistingAndRemoteCover(temporary.root);
  std::cout << "Project artwork: all checks passed\n";
}
