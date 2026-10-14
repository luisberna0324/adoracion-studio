#include "ui/panels/SongListPanel.h"
#include "ui/components/AppModal.h"
#include "data/AppSettings.h"
#include "data/SessionAuthLogger.h"
#include "data/SessionPersistence.h"
#include "IsolatedTestRoot.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void pumpUntil(const std::function<bool()> &done, const char *message) {
  const auto started = juce::Time::getMillisecondCounter();
  while (!done() && juce::Time::getMillisecondCounter() - started < 10000)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  expect(done(), message);
}

bool hasSong(const juce::String &id) {
  const auto songs = SongLibraryStore::loadSongs();
  return std::any_of(songs.begin(), songs.end(), [&](const auto &song) {
    return song.projectId == id;
  });
}

void capture(juce::Component &component, const juce::String &name) {
  const auto path = juce::SystemStats::getEnvironmentVariable(
      "PW_PROJECT_IMPORT_CAPTURE_DIR", {});
  if (path.isEmpty())
    return;
  expect(juce::File::isAbsolutePath(path), "capture folder must be absolute");
  const juce::File directory(path);
  expect(directory.createDirectory().wasOk(), "create project import capture folder");
  const auto file = directory.getChildFile(name + ".png");
  auto stream = file.createOutputStream();
  expect(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk(),
         "open project import PNG output");
  expect(juce::PNGImageFormat().writeImageToStream(
             component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f),
             *stream),
         "render the actual project import component");
}

struct ProjectFixture {
  static constexpr int sampleRate = 24000;
  static constexpr int sampleCount = sampleRate * 8;
  static constexpr int stemCount = 8;

  juce::String id;
  juce::File directory, source, package;
  ProjectDocument document;
  juce::String sourceHash, packageHash;

  explicit ProjectFixture(const char *identity)
      : id(identity), directory(isolatedTestRoot().getChildFile(id)),
        source(directory.getChildFile("external-source.flac")),
        package(directory.getChildFile("portable.pwproj")) {
    expect(directory.createDirectory().wasOk(), "create isolated project fixture");
    {
      std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
      juce::FlacAudioFormat flac;
      auto writer = flac.createWriterFor(
          stream, juce::AudioFormatWriterOptions{}.withSampleRate(sampleRate)
                      .withNumChannels(1).withBitsPerSample(16));
      expect(writer != nullptr, "create actual FLAC source writer");
      juce::AudioBuffer<float> samples(1, sampleCount);
      // Incompressible deterministic samples give extraction meaningful work
      // without a large or copyrighted external fixture.
      juce::Random random(0x4f50454e);
      for (int i = 0; i < sampleCount; ++i)
        samples.setSample(0, i, (random.nextFloat() - 0.5f) * 0.5f);
      expect(writer->writeFromAudioSampleBuffer(samples, 0, sampleCount),
             "write readable FLAC source samples");
    }

    document.song.id = document.song.projectId = id;
    document.song.title = "Project import fixture";
    document.song.artist = "Local integration test";
    document.song.bpm = 96;
    document.song.key = "D";
    document.song.timeSignature = "4/4";
    document.settings.pitchSemitones = -2;
    document.settings.tempoBpm = 104;
    document.settings.mix.master.volume = 0.67f;
    document.settings.window.endSec = 8;
    document.settings.windowCustomized = true;
    for (int i = 0; i < stemCount; ++i) {
      ImportedStemData stem;
      stem.stemId = id + "-stem-" + juce::String(i);
      stem.storedPath = stem.sourcePath = source.getFullPathName();
      stem.fileName = source.getFileName();
      stem.displayName = "Instrument " + juce::String(i + 1);
      stem.category = "Keys";
      stem.durationSec = 8;
      stem.sampleRate = sampleRate;
      stem.channels = 1;
      document.song.stems.push_back(stem);
      StemMixSnapshot mix;
      mix.stemId = stem.stemId;
      mix.volume = 0.42f;
      document.settings.mix.stems.push_back(mix);
    }
    SongArrangementPartSnapshot part;
    part.id = "verse";
    part.name = "Verse";
    part.color = standardArrangementPartColorForName(part.name);
    part.endSec = part.sourceEndSec = 8;
    document.song.arrangement.parts.push_back(part);
    document.waveformBins.assign(kWaveformPreviewBinCount, 0.25f);
    juce::String error;
    expect(ProjectStore::save(document, package, &error),
           "create a real portable project using the production writer");
    sourceHash = juce::SHA256(source).toHexString();
    packageHash = juce::SHA256(package).toHexString();
    expect(!ProjectStore::getCacheDirectory(id).getChildFile("song-settings.json")
                .existsAsFile(),
           "project import fixture begins with a cold cache");
  }

  void expectSourcesPreserved() const {
    expect(source.existsAsFile() && package.existsAsFile() &&
               juce::SHA256(source).toHexString() == sourceHash &&
               juce::SHA256(package).toHexString() == packageHash,
           "opening never deletes or modifies the source audio or portable project");
  }

  void expectOpenedDocument(const ProjectDocument &opened,
                            juce::File expectedPackage = {}) const {
    if (expectedPackage == juce::File{})
      expectedPackage = package;
    expect(opened.song.projectId == id && opened.projectFile == expectedPackage &&
               juce::File(opened.song.projectPath) == expectedPackage &&
               opened.song.stems.size() == stemCount &&
               opened.song.title == document.song.title &&
               opened.song.arrangement.parts.size() == 1,
           "activation receives the complete opened project document");
    expect(ProjectStore::settingsJsonEqual(opened.settings, document.settings) &&
               opened.waveformBins == document.waveformBins,
           "activation preserves portable performance settings and waveform bins");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto &stem : opened.song.stems) {
      const juce::File cached(stem.storedPath);
      const auto reader = std::unique_ptr<juce::AudioFormatReader>(
          formats.createReaderFor(cached));
      expect(cached.isAChildOf(ProjectStore::getCacheDirectory(id)) &&
                 cached.hasFileExtension("flac") && reader != nullptr &&
                 reader->sampleRate == sampleRate && reader->numChannels == 1 &&
                 reader->lengthInSamples == sampleCount,
             "all activated stems are extracted, valid and readable FLAC audio");
      juce::AudioBuffer<float> samples(1, 1024);
      expect(reader->read(&samples, 0, samples.getNumSamples(), 0, true, false) &&
                 samples.getMagnitude(0, 0, samples.getNumSamples()) > 0.1f,
             "activated stems contain real decoded audio samples");
    }
  }
};

struct PanelHost final : juce::Component {
  PanelHost() : panel(std::make_unique<SongListPanel>()) {
    setVisible(true);
    addAndMakeVisible(*panel);
    setSize(1200, 800);
  }
  void resized() override {
    if (panel != nullptr)
      panel->setBounds(getLocalBounds());
  }
  void paint(juce::Graphics &g) override { g.fillAll(UITheme::appBg); }
  std::unique_ptr<SongListPanel> panel;
};

struct Activation {
  int count = 0;
  int index = -1;
  bool messageThread = false;
  bool registeredAtActivation = false;
  ProjectDocument document;

  void bind(SongListPanel &panel) {
    panel.setOnProjectActivated([this](ProjectDocument opened, int songIndex) {
      ++count;
      index = songIndex;
      messageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
      registeredAtActivation = hasSong(opened.song.projectId);
      document = std::move(opened);
    });
  }
};
} // namespace

struct SongListPanelTestAccess {
  static bool hasProgressOverlay(const SongListPanel &panel) {
    return panel.projectOpenOverlay != nullptr;
  }
  static void targetSetlist(SongListPanel &panel, const juce::String &id) {
    panel.libraryTargetSetlistId = id;
  }
  static bool hasErrorDialog(const SongListPanel &panel) {
    return panel.appModal != nullptr;
  }
  static void dismissError(SongListPanel &panel) { panel.appModal.reset(); }
  static std::shared_ptr<std::atomic<bool>> cancellation(SongListPanel &panel) {
    return panel.projectImportCancelled;
  }
  static void importSandboxCopy(SongListPanel &panel, const juce::File &source) {
    panel.beginProjectImport(juce::URL(source), true);
  }
};

namespace {
void testAsyncImport(const ProjectFixture &fixture,
                     const ProjectFixture &secondFixture) {
  PanelHost host;
  Activation activated;
  activated.bind(*host.panel);
  const auto setlist = SongLibraryStore::createSetlist("Import destination");
  expect(setlist.isNotEmpty() && SongLibraryStore::setActiveSetlistId(setlist),
         "create and activate the isolated target setlist");
  SongListPanelTestAccess::targetSetlist(*host.panel, setlist);
  const auto before = SongLibraryStore::loadSongs().size();
  bool heartbeat = false, heartbeatWhileBusy = false;
  bool heartbeatBeforeRegistration = false;
  juce::MessageManager::callAsync([&] {
    heartbeat = true;
    heartbeatWhileBusy = host.panel->isProjectImportInProgress();
    heartbeatBeforeRegistration = SongLibraryStore::loadSongs().size() == before;
  });
  host.panel->openProjectFile(fixture.package);
  expect(host.panel->isProjectImportInProgress() && activated.count == 0 &&
             SongListPanelTestAccess::hasProgressOverlay(*host.panel) &&
             SongLibraryStore::loadSongs().size() == before,
         "open returns with progress visible, before registration or activation");
  host.panel->openProjectFile(secondFixture.package);
  expect(host.panel->isProjectImportInProgress() && activated.count == 0,
         "a second open is ignored while the first import is busy");
  capture(host, "project-import-desktop");
  host.panel->setMinimalPhoneMode(true);
  host.setSize(390, 844);
  capture(host, "project-import-phone");
  host.panel->setMinimalPhoneMode(false);
  host.setSize(1200, 800);
  pumpUntil([&] { return heartbeat && activated.count > 0 &&
                        !host.panel->isProjectImportInProgress(); },
            "message loop remains responsive and finishes asynchronous import");
  expect(heartbeatWhileBusy && heartbeatBeforeRegistration,
         "message-thread heartbeat runs while import is pending");
  expect(activated.count == 1 && activated.messageThread &&
             activated.registeredAtActivation && activated.index >= 0 &&
             host.panel->getSongIndexById(activated.document.song.id) == activated.index,
         "exactly one complete project activates on the message thread after registration");
  expect(!SongListPanelTestAccess::hasProgressOverlay(*host.panel) &&
             !SongListPanelTestAccess::hasErrorDialog(*host.panel) &&
             SongLibraryStore::loadSongs().size() == before + 1 &&
             !hasSong(secondFixture.id),
         "success removes progress and never registers the ignored second project");
  fixture.expectOpenedDocument(activated.document);
  fixture.expectSourcesPreserved();
  secondFixture.expectSourcesPreserved();
}

void testInvalidProjectAndRetry(const ProjectFixture &retryFixture) {
  const auto invalid = isolatedTestRoot().getChildFile("invalid.pwproj");
  {
    juce::ZipFile::Builder zip;
    constexpr char payload[] = "A ZIP without a project manifest";
    zip.addEntry(new juce::MemoryInputStream(payload, sizeof(payload), true),
                 0, "readme.txt", juce::Time::getCurrentTime());
    juce::FileOutputStream stream(invalid);
    expect(stream.openedOk() && zip.writeToStream(stream, nullptr),
           "create invalid project archive with no manifest");
  }
  const auto invalidHash = juce::SHA256(invalid).toHexString();
  const auto before = SongLibraryStore::loadSongs().size();
  PanelHost host;
  Activation activated;
  activated.bind(*host.panel);
  host.panel->openProjectFile(invalid);
  expect(host.panel->isProjectImportInProgress() &&
             SongListPanelTestAccess::hasProgressOverlay(*host.panel),
         "invalid archive is validated through the asynchronous import path");
  pumpUntil([&] { return !host.panel->isProjectImportInProgress() &&
                        SongListPanelTestAccess::hasErrorDialog(*host.panel); },
            "failed import ends progress and reports a dismissible in-app error");
  expect(activated.count == 0 &&
             !SongListPanelTestAccess::hasProgressOverlay(*host.panel) &&
             SongLibraryStore::loadSongs().size() == before &&
             invalid.existsAsFile() && juce::SHA256(invalid).toHexString() == invalidHash,
         "invalid project never activates, registers a partial song or modifies its source");
  host.setSize(390, 844);
  capture(host, "project-import-error-phone");
  SongListPanelTestAccess::dismissError(*host.panel);
  host.panel->openProjectFile(retryFixture.package);
  expect(host.panel->isProjectImportInProgress(),
         "failure releases the busy guard so a valid project can be retried");
  pumpUntil([&] { return activated.count == 1 &&
                        !host.panel->isProjectImportInProgress(); },
            "valid retry finishes after a failed import");
  expect(activated.messageThread && activated.registeredAtActivation &&
             !SongListPanelTestAccess::hasErrorDialog(*host.panel) &&
             SongLibraryStore::loadSongs().size() == before + 1,
         "retry activates exactly once and leaves no stale error dialog");
  retryFixture.expectOpenedDocument(activated.document);
  retryFixture.expectSourcesPreserved();
}

void testDestructionDuringImport(const ProjectFixture &fixture) {
  const auto before = SongLibraryStore::loadSongs().size();
  PanelHost host;
  Activation activated;
  activated.bind(*host.panel);
  host.panel->openProjectFile(fixture.package);
  expect(host.panel->isProjectImportInProgress(),
         "lifetime test starts an asynchronous project import");
  const auto cancelled = SongListPanelTestAccess::cancellation(*host.panel);
  expect(cancelled != nullptr && !cancelled->load(),
         "pending import owns its cancellation token");

  // Do not dispatch UI completion. Let the real worker start extraction, then
  // destroy the panel while its asynchronous result is still pending.
  const auto started = juce::Time::getMillisecondCounter();
  const auto cache = ProjectStore::getCacheDirectory(fixture.id);
  while (!cache.isDirectory() &&
         juce::Time::getMillisecondCounter() - started < 10000)
    juce::Thread::sleep(1);
  expect(cache.isDirectory() && activated.count == 0,
         "real project extraction starts without dispatching the message thread");
  const juce::Component::SafePointer<SongListPanel> safePanel(host.panel.get());
  host.panel.reset();
  expect(safePanel == nullptr && cancelled->load(),
         "destroying the panel invalidates its SafePointer and cancels import delivery");
  // Worker and queued callbacks hold the same token. Wait until they release
  // it before the isolated runner removes any source fixture.
  pumpUntil([&] { return cancelled.use_count() == 1; },
            "abandoned worker and queued progress callbacks finish safely");
  expect(activated.count == 0 && !hasSong(fixture.id) &&
             SongLibraryStore::loadSongs().size() == before,
         "destroyed panel receives no activation and registers no abandoned import");
  fixture.expectSourcesPreserved();
}

void testSandboxCopy(const ProjectFixture &fixture) {
  const auto imports = SongLibraryStore::getRootDirectory()
                           .getChildFile("imports/projects");
  const auto before = SongLibraryStore::loadSongs().size();
  PanelHost host;
  Activation activated;
  activated.bind(*host.panel);
  SongListPanelTestAccess::importSandboxCopy(*host.panel, fixture.package);
  expect(host.panel->isProjectImportInProgress() && activated.count == 0,
         "provider-style sandbox copy starts asynchronously");
  pumpUntil([&] { return activated.count == 1 &&
                        !host.panel->isProjectImportInProgress(); },
            "sandbox copy opens and activates its private portable project");
  const auto privatePackage = activated.document.projectFile;
  expect(activated.messageThread && activated.registeredAtActivation &&
             privatePackage != fixture.package &&
             privatePackage.getParentDirectory() == imports &&
             privatePackage.existsAsFile() &&
             juce::SHA256(privatePackage).toHexString() == fixture.packageHash &&
             SongLibraryStore::loadSongs().size() == before,
         "copy is byte-identical, private and registered without duplicating project identity");
  const auto songs = SongLibraryStore::loadSongs();
  const auto registered = std::find_if(songs.begin(), songs.end(), [&](const auto &song) {
    return song.projectId == fixture.id;
  });
  expect(registered != songs.end() &&
             juce::File(registered->projectPath) == privatePackage,
         "library persists the sandbox package path rather than the external source");
  fixture.expectOpenedDocument(activated.document, privatePackage);
  fixture.expectSourcesPreserved();

  // A failure may discard only the copy created by that request. Include an
  // unrelated pre-existing file so a broad import-directory cleanup fails.
  const auto sentinel = imports.getChildFile("keep-existing.pwproj");
  expect(sentinel.replaceWithText("Existing unrelated private project"),
         "create an unrelated pre-existing import fixture");
  const auto sentinelHash = juce::SHA256(sentinel).toHexString();
  const auto inventory = [&] {
    juce::StringArray paths;
    for (const auto &file : imports.findChildFiles(
             juce::File::findFilesAndDirectories, false))
      paths.add(file.getFullPathName());
    paths.sort(false);
    return paths;
  };
  const auto filesBeforeFailure = inventory();
  const auto invalid = isolatedTestRoot().getChildFile("invalid.pwproj");
  expect(invalid.existsAsFile(), "reuse the invalid source archive fixture");
  const auto invalidHash = juce::SHA256(invalid).toHexString();
  SongListPanelTestAccess::importSandboxCopy(*host.panel, invalid);
  expect(host.panel->isProjectImportInProgress(),
         "invalid source also runs through private-copy validation");
  pumpUntil([&] { return !host.panel->isProjectImportInProgress() &&
                        SongListPanelTestAccess::hasErrorDialog(*host.panel) &&
                        inventory() == filesBeforeFailure; },
            "failed sandbox import removes only its new private copy");
  expect(activated.count == 1 && SongLibraryStore::loadSongs().size() == before &&
             invalid.existsAsFile() &&
             juce::SHA256(invalid).toHexString() == invalidHash &&
             juce::SHA256(privatePackage).toHexString() == fixture.packageHash &&
             juce::SHA256(sentinel).toHexString() == sentinelHash,
         "failed copy preserves its external source, previous project and unrelated private files");
  SongListPanelTestAccess::dismissError(*host.panel);
  fixture.expectSourcesPreserved();
}
} // namespace

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  try {
    const auto root = isolatedTestRoot();
    expect(SongLibraryStore::getRootDirectory().isAChildOf(root) &&
               SessionPersistence::getSnapshotFile().isAChildOf(root) &&
               SessionAuthLogger::getLogFile().isAChildOf(root),
           "project import integration tests require private temporary storage");
    juce::ScopedJuceInitialiser_GUI gui;
    applyTheme(ThemePreset::Spotify);
    const ProjectFixture first("project-import-first");
    const ProjectFixture second("project-import-second");
    const ProjectFixture abandoned("project-import-abandoned");
    testAsyncImport(first, second);
    testInvalidProjectAndRetry(second);
    testSandboxCopy(first);
    testDestructionDuringImport(abandoned);
    ProjectStore::flushAllPendingSettings();
    std::cout << "Project import: all checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Project import: " << error.what() << '\n';
    return 1;
  }
}
