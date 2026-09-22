#include "data/ProjectStore.h"
#include "data/ApplicationDataPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double sampleRate = 8000.0;
constexpr int sampleCount = 1024;

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

// ProjectStore writes a recent-project registry, settings sidecars and caches.
// Never run this integration test against the user's ordinary app-data root.
// Compile this test and AppSettings/SongLibraryStore/SessionAuthLogger with
// MTCONTROLS_ISOLATED_STORAGE_TESTS=1. Set PLAYWORSHIP_CREATION_TEST_ROOT to an
// existing fresh mktemp directory; production builds ignore this variable.
bool isolatedStorageRoot(juce::File &root) {
#if !defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) || !MTCONTROLS_ISOLATED_STORAGE_TESTS
  juce::ignoreUnused(root);
  return false;
#else
  const auto requested = juce::SystemStats::getEnvironmentVariable(
      "PLAYWORSHIP_CREATION_TEST_ROOT", {});
  if (requested.isEmpty() || !juce::File::isAbsolutePath(requested))
    return false;
  try {
    root = ApplicationDataPaths::isolatedTestRoot();
    return ApplicationDataPaths::applicationDataDirectory().isAChildOf(root) &&
           ApplicationDataPaths::documentsDirectory().isAChildOf(root) &&
           ApplicationDataPaths::systemLogsDirectory().isAChildOf(root);
  } catch (const std::exception &error) {
    std::cerr << "Isolation check: " << error.what() << '\n';
    return false;
  }
#endif
}

struct Fixtures {
  juce::String projectId = juce::Uuid().toString();
  juce::String stemId = juce::Uuid().toString();
  juce::File directory;
  juce::File archive;
  juce::File libraryFile;

  explicit Fixtures(const juce::File &isolatedRoot)
      : directory(isolatedRoot.getChildFile("fixtures-" + projectId)),
        archive(directory.getChildFile("created-song.pwproj")) {
    expect(directory.createDirectory().wasOk(), "create test fixture directory");
    const auto storageRoot = SongLibraryStore::getRootDirectory();
    expect(storageRoot.isAChildOf(isolatedRoot), "actual library storage is isolated");
    libraryFile = storageRoot.getChildFile("songs.json");
    expect(!libraryFile.exists(),
           "integration test requires an isolated root without a user library");
  }

  ~Fixtures() {
    // The constructor proved the library did not exist before this test. Only
    // its own database, UUID cache and exact fixtures are removed, never a
    // broad application directory or an existing user database.
    if (libraryFile.existsAsFile())
      libraryFile.deleteFile();
    ProjectStore::discardLocalState(projectId, archive);
    directory.deleteRecursively();
  }
};

float expectedSample(int index) {
  return 0.25f * std::sin(static_cast<float>(index) * 0.075f);
}

void writeFlac(const juce::File &file) {
  std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
  juce::FlacAudioFormat format;
  auto writer = format.createWriterFor(
      stream, juce::AudioFormatWriterOptions{}
                  .withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "create actual FLAC writer");
  juce::AudioBuffer<float> audio(1, sampleCount);
  for (int i = 0; i < sampleCount; ++i)
    audio.setSample(0, i, expectedSample(i));
  expect(writer->writeFromAudioSampleBuffer(audio, 0, sampleCount),
         "write actual FLAC samples");
}

void writePng(const juce::File &file) {
  juce::Image image(juce::Image::ARGB, 8, 8, true);
  image.setPixelAt(3, 4, juce::Colour(0xff27cd68));
  juce::FileOutputStream stream(file);
  juce::PNGImageFormat format;
  expect(stream.openedOk() && format.writeImageToStream(image, stream),
         "write actual PNG cover");
}

ProjectDocument makeDocument(const Fixtures &fixtures, const juce::File &stem,
                              const juce::File &artwork) {
  ProjectDocument document;
  document.song.id = fixtures.projectId;
  document.song.projectId = fixtures.projectId;
  document.song.title = juce::String::fromUTF8(u8"Canción de integración");
  document.song.artist = "Artista de prueba";
  document.song.bpm = 92;
  document.song.key = "Bb";
  document.song.timeSignature = "6/8";
  document.song.createdMs = 1700000000000;
  document.song.artworkSource = "manual:" + artwork.getFullPathName();
  expect(document.song.artworkPath.isEmpty(),
         "fixture must reproduce a wizard draft with only artworkSource");
  ImportedStemData track;
  track.stemId = fixtures.stemId;
  track.sourcePath = stem.getFullPathName();
  track.fileName = stem.getFileName();
  track.displayName = "Piano";
  track.category = "Keys";
  track.durationSec = static_cast<double>(sampleCount) / sampleRate;
  track.sampleRate = static_cast<int>(sampleRate);
  track.channels = 1;
  document.song.stems.push_back(track);

  document.settings.tempoBpm = 96.0;
  document.settings.pitchSemitones = -2;
  document.settings.mix.master.volume = 0.67f;
  document.settings.mix.master.gain = 1.12f;
  StemMixSnapshot mix;
  mix.stemId = fixtures.stemId;
  mix.volume = 0.42f;
  mix.solo = true;
  document.settings.mix.stems.push_back(mix);
  document.settings.window.endSec = track.durationSec;
  document.settings.window.fadeOutSec = 0.025;
  document.settings.windowCustomized = true;
  return document;
}

void inspectPackage(const Fixtures &fixtures, const ProjectDocument &document) {
  juce::ZipFile zip(fixtures.archive);
  expect(zip.getNumEntries() == 3, "project contains manifest, FLAC and PNG only");
  const auto stemEntry = "audio/" + fixtures.stemId + ".flac";
  expect(zip.getIndexOfFileName(stemEntry) >= 0 &&
             zip.getIndexOfFileName("artwork.png") >= 0,
         "portable project embeds actual audio and manual artwork");
  const int manifestIndex = zip.getIndexOfFileName("manifest.json");
  expect(manifestIndex >= 0, "project has its real manifest");
  const auto stream = std::unique_ptr<juce::InputStream>(
      zip.createStreamForEntry(manifestIndex));
  expect(stream != nullptr, "manifest stream is readable");
  const auto json = stream->readEntireStreamAsString();
  expect(!json.contains(fixtures.directory.getFullPathName()) &&
             !json.contains("manual:"),
         "manifest contains no dependency on fixture/manual paths");
  const auto value = juce::JSON::parse(json);
  const auto *root = value.getDynamicObject();
  expect(root != nullptr, "manifest is valid JSON");
  const auto songValue = root->getProperty("song");
  const auto *song = songValue.getDynamicObject();
  expect(song != nullptr && song->getProperty("title").toString() == document.song.title &&
             song->getProperty("artist").toString() == document.song.artist &&
             song->getProperty("artworkPath").toString() == "artwork.png" &&
             song->getProperty("artworkSource").toString().isEmpty(),
         "manifest preserves metadata and uses a portable cover entry");
}

void expectRegisteredSong(const Fixtures &fixtures, const ImportedSongData &expected) {
  const auto library = SongLibraryStore::loadLibrarySnapshot();
  expect(library.songs.size() == 1, "reload finds exactly the registered project song");
  const auto &saved = library.songs.front();
  expect(saved.id == expected.id && saved.projectId == fixtures.projectId &&
             saved.title == expected.title && saved.artist == expected.artist &&
             saved.bpm == expected.bpm && saved.key == expected.key &&
             saved.timeSignature == expected.timeSignature,
         "registered song retains metadata and portable identity after library reload");
  expect(juce::File(saved.projectPath) == fixtures.archive &&
             juce::File(saved.projectPath).existsAsFile() && saved.stems.size() == 1,
         "reloaded library retains the valid portable project path and its stem");
  expect(saved.stems.front().stemId == fixtures.stemId &&
             juce::File(saved.stems.front().storedPath).existsAsFile() &&
             saved.stems.front().sampleRate == sampleRate &&
             saved.stems.front().channels == 1 &&
             juce::ImageFileFormat::loadFrom(juce::File(saved.artworkPath)).isValid(),
         "reloaded library retains readable cached audio and artwork paths");
}

// Deterministically block replacing only this test's songs.json. A nonempty
// directory survives attempts to replace the database; the real isolated
// database is held in our fixture directory and restored even on assertion.
class BlockedLibraryFile {
public:
  explicit BlockedLibraryFile(const Fixtures &fixtures)
      : target(fixtures.libraryFile),
        backup(fixtures.directory.getChildFile("registered-library.backup")),
        sentinel(target.getChildFile("test-sentinel.txt")) {}

  void block() {
    expect(target.moveFileTo(backup), "back up the test database before fault injection");
    expect(target.createDirectory().wasOk() && sentinel.replaceWithText("test-owned marker"),
           "create a nonempty test-owned destination to reject database replacement");
  }

  bool isIntact() const {
    return target.isDirectory() && sentinel.loadFileAsString() == "test-owned marker";
  }

  bool restore() {
    if (!backup.existsAsFile())
      return true;
    if (sentinel.existsAsFile() && !sentinel.deleteFile())
      return false;
    if (target.exists() && !target.deleteFile())
      return false;
    return backup.moveFileTo(target);
  }

  ~BlockedLibraryFile() { restore(); }

private:
  juce::File target, backup, sentinel;
};

void testLibraryRegistration(const Fixtures &fixtures, ImportedSongData song) {
  expect(SongLibraryStore::registerProjectSong(song),
         "created portable project registers successfully in the isolated library");
  expect(fixtures.libraryFile.existsAsFile(), "registration actually persists songs.json");
  expectRegisteredSong(fixtures, song);
  expect(SongLibraryStore::registerProjectSong(song),
         "registering the same project a second time remains valid");
  expectRegisteredSong(fixtures, song);

  juce::MemoryBlock originalLibrary;
  expect(fixtures.libraryFile.loadFileAsData(originalLibrary), "record registered database bytes");
  {
    BlockedLibraryFile blocked(fixtures);
    blocked.block();
    auto anotherSong = song;
    anotherSong.id = juce::Uuid().toString();
    anotherSong.projectId = anotherSong.id;
    anotherSong.title = "Registration must fail";
    expect(!SongLibraryStore::registerProjectSong(anotherSong),
           "database commit failure must not report a successfully registered song");
    expect(blocked.isIntact(), "failed commit must not replace the blocking sentinel directory");
    expect(fixtures.archive.existsAsFile(), "failed registration preserves the created package");
    expect(blocked.restore(), "restore the test's original registered database");
  }
  juce::MemoryBlock restoredLibrary;
  expect(fixtures.libraryFile.loadFileAsData(restoredLibrary) &&
             restoredLibrary == originalLibrary,
         "failed commit does not alter the prior registered database");
  expectRegisteredSong(fixtures, song);
}

void runRoundtripAndFailureChecks(Fixtures &fixtures) {
  const auto source = fixtures.directory.getChildFile("source.flac");
  const auto cover = fixtures.directory.getChildFile("cover.png");
  writeFlac(source);
  writePng(cover);
  const auto document = makeDocument(fixtures, source, cover);
  juce::String error;
  double progress = -1.0;
  expect(ProjectStore::save(document, fixtures.archive, &error, &progress),
         "real ProjectStore::save succeeds for wizard-style draft");
  expect(error.isEmpty() && progress == 1.0, "successful save reports completion");
  inspectPackage(fixtures, document);
  juce::MemoryBlock originalPackage;
  expect(fixtures.archive.loadFileAsData(originalPackage), "record created package bytes");

  expect(source.deleteFile() && cover.deleteFile(), "delete only this test's source fixtures");
  ProjectDocument opened;
  expect(ProjectStore::open(fixtures.archive, opened, &error),
         "real ProjectStore::open succeeds without original source files");
  expect(opened.song.title == document.song.title && opened.song.artist == document.song.artist &&
             opened.song.bpm == 92 && opened.song.key == "Bb" &&
             opened.song.timeSignature == "6/8" &&
             opened.song.projectId == fixtures.projectId && opened.song.stems.size() == 1,
         "opening preserves song metadata and identity");
  expect(ProjectStore::settingsJsonEqual(opened.settings, document.settings),
         "opening preserves all nondefault mixer/performance settings");
  const auto image = juce::ImageFileFormat::loadFrom(juce::File(opened.song.artworkPath));
  expect(image.isValid() && image.getWidth() == 8 && image.getHeight() == 8 &&
             image.getPixelAt(3, 4) == juce::Colour(0xff27cd68),
         "extracted artwork remains a valid, unchanged PNG");
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  const auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formats.createReaderFor(juce::File(opened.song.stems.front().storedPath)));
  expect(reader != nullptr && reader->sampleRate == sampleRate &&
             reader->numChannels == 1 && reader->lengthInSamples == sampleCount,
         "extracted FLAC is decodable with its original audio dimensions");
  juce::AudioBuffer<float> decoded(1, sampleCount);
  expect(reader->read(&decoded, 0, sampleCount, 0, true, false), "read extracted audio");
  for (int i = 0; i < sampleCount; ++i)
    expect(std::abs(decoded.getSample(0, i) - expectedSample(i)) < 0.0001f,
           "FLAC roundtrip preserves source samples within 16-bit precision");

  const auto expectUnchangedArchive = [&] {
    juce::MemoryBlock current;
    expect(fixtures.archive.loadFileAsData(current) && current == originalPackage,
           "failed save leaves existing portable project byte-for-byte intact");
  };
  progress = -1.0;
  expect(!ProjectStore::save(document, fixtures.archive, &error, &progress) &&
             error.isNotEmpty() && progress < 1.0,
         "missing source cannot report successful creation");
  expectUnchangedArchive();

  writeFlac(source);
  expect(!ProjectStore::save(document, fixtures.archive, &error) && error.isNotEmpty(),
         "missing manually selected cover cannot silently disappear on save");
  expectUnchangedArchive();

  writePng(cover);
  const auto invalidSource = fixtures.directory.getChildFile("invalid.flac");
  expect(invalidSource.replaceWithText("not a FLAC stream"), "create invalid audio fixture");
  auto invalid = document;
  invalid.song.stems.front().sourcePath = invalidSource.getFullPathName();
  invalid.song.stems.front().durationSec = 0.0;
  invalid.song.stems.front().sampleRate = 0;
  invalid.song.stems.front().channels = 0;
  expect(!ProjectStore::save(invalid, fixtures.archive, &error) &&
             error.contains(invalidSource.getFileName()),
         "invalid undecodable source does not report success");
  expectUnchangedArchive();
  expect(!fixtures.libraryFile.exists(),
         "package-only operations do not register prematurely in the library");
  testLibraryRegistration(fixtures, opened.song);
}
} // namespace

int main() {
  juce::File isolatedRoot;
  if (!isolatedStorageRoot(isolatedRoot)) {
    std::cerr << "SKIP: project creation test needs an isolated temporary app-data root; "
                 "compile with MTCONTROLS_ISOLATED_STORAGE_TESTS=1 and set "
                 "PLAYWORSHIP_CREATION_TEST_ROOT.\n";
    return 77;
  }
  juce::ScopedJuceInitialiser_GUI gui;
  try {
    Fixtures fixtures(isolatedRoot);
    runRoundtripAndFailureChecks(fixtures);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Song project creation: all integration checks passed\n";
}
