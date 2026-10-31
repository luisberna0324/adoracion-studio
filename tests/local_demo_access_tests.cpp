#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#include "data/AuthSessionApi.h"
#include "data/DemoSongService.h"
#include "data/SessionPersistence.h"
#include "IsolatedTestRoot.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/WaveformBar.h"
#include "ui/panels/PadsComponent.h"
#include "ui/panels/SongListPanel.h"
#include "ui/panels/SongImportWizardComponent.h"
#include "ui/panels/LyricsPanel.h"
#include "network/RemoteStateCodec.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
std::atomic<int> refreshAttempts{0};
std::atomic<int> validationAttempts{0};
int checks = 0;

void expect(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

void drainMessages(int milliseconds = 40) {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

template <typename Predicate>
void waitFor(Predicate ready, const char *message) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
  while (!ready() && juce::Time::getMillisecondCounterHiRes() < deadline)
    drainMessages(20);
  expect(ready(), message);
}
} // namespace

// Replace only the external Firebase transports in this executable. Every
// editor, processor, demo installer and UI callback remains production code.
// Unexpected network work fails safely and is counted instead of contacting
// Firebase with the fixture credentials.
FirebaseRefreshResult AuthSessionApi::refreshFirebaseIdToken(
    const juce::String &, const juce::String &, int) {
  ++refreshAttempts;
  FirebaseRefreshResult result;
  result.errorMessage = "network";
  return result;
}

ValidateAccessResult AuthSessionApi::fetchValidateAccess(
    const juce::String &, const juce::String &, const juce::String &,
    const juce::String &, int) {
  ++validationAttempts;
  ValidateAccessResult result;
  result.rawError = "network";
  return result;
}

struct LocalDemoAccessTestAccess {
  static bool load(MTControlsAudioProcessorEditor &editor, const ImportedSongData &song) {
    return editor.loadSongAction(song, -1);
  }
  static void layout(MTControlsAudioProcessorEditor &editor, int width, int height, bool phone) {
    editor.setSize(width, height);
    editor.layoutMainUi(phone); // Same layout used by resized(), without pretending Linux is an iPhone.
  }
  static void initializePhoneInputUi(MTControlsAudioProcessorEditor &editor) {
    // Only the isolated fixture enters the ordinary UI without authentication;
    // this keeps the input regression valid in production Release builds too.
    editor.initializeMainUi();
    expect(editor.mainUiInitialized, "phone input fixture builds the real application UI");
    expect(!editor.localDemoSession && !hasCloudIdentity(editor),
           "phone input fixture grants no demo login, Firebase identity or license");
  }
  static bool relayoutPhoneInputUi(MTControlsAudioProcessorEditor &editor) {
    if (!editor.mainUiInitialized)
      return false;
    // Keep the explicitly simulated phone profile through native host/focus
    // resize callbacks too. Do not reopen the drawer or alter its state.
    editor.layoutMainUi(true);
    return true;
  }
  static SongListPanel &library(MTControlsAudioProcessorEditor &editor) {
    return *dynamic_cast<SongListPanel *>(editor.songListPanel.get());
  }
  static juce::Component &drawerScrim(MTControlsAudioProcessorEditor &editor) {
    // The scrim's definition is intentionally private to PluginEditor.cpp.
    for (auto *child : editor.getChildren())
      if (static_cast<void *>(child) == static_cast<void *>(editor.setlistDrawerScrim.get()))
        return *child;
    throw std::runtime_error("editor contains its real drawer scrim");
  }
  static void layoutPhoneDrawer(MTControlsAudioProcessorEditor &editor,
                                int width, int height, bool open = true) {
    // A desktop build cannot infer a phone from its OS. Suppress only the
    // automatic desktop-profile resize, then invoke the real phone layout.
    {
      juce::ScopedValueSetter<bool> deferDesktopLayout(editor.mainUiInitialized, false);
      editor.setSize(width, height);
    }
    editor.isSetlistDrawerOpen = open;
    editor.isSetlistCollapsed = false;
    editor.layoutMainUi(true);
  }
  static bool drawerOpen(const MTControlsAudioProcessorEditor &editor) {
    return editor.isSetlistDrawerOpen;
  }
  static bool setlistCollapsed(const MTControlsAudioProcessorEditor &editor) {
    return editor.isSetlistCollapsed;
  }
  static auto &wave(MTControlsAudioProcessorEditor &editor) { return *editor.waveformBarPtr; }
  static auto &top(MTControlsAudioProcessorEditor &editor) { return *editor.topBar; }
  static auto mixerBounds(MTControlsAudioProcessorEditor &editor) { return editor.channelGridPanel->getBounds(); }
  static void expandWave(MTControlsAudioProcessorEditor &editor, bool expanded) { editor.isWaveformHeightExpanded = expanded; editor.resized(); }
  static bool expanded(MTControlsAudioProcessorEditor &editor) { return editor.isWaveformHeightExpanded; }
  static bool padsMatchProfile(MTControlsAudioProcessorEditor &editor, bool phone) {
    return editor.padsComponent &&
           editor.padsComponent->isVisible() == (!phone && !editor.isPadsCollapsed);
  }
  static void refreshPresentation(MTControlsAudioProcessorEditor &editor) { editor.refreshWaveformPresentationForSong(); }
  static void presentMetadata(MTControlsAudioProcessorEditor &editor, ImportedSongData song) {
    editor.currentLoadedSong = std::move(song); editor.hasCurrentLoadedSong = true;
    editor.refreshWaveformPresentationForSong();
  }
  static void remoteSnapshot(MTControlsAudioProcessorEditor &editor, const ImportedSongData &song) {
    editor.playbackSession.setRemote(true); editor.playbackSession.setConnected(true);
    auto *snapshot = new juce::DynamicObject();
    snapshot->setProperty("song", playworship::remote::encodeSong(song));
    snapshot->setProperty("playing", false); snapshot->setProperty("position", 42.0);
    snapshot->setProperty("length", song.blankSong ? 0.0 : 90.0);
    snapshot->setProperty("settings", ProjectStore::serializeSettings({}));
    editor.applyRemoteSnapshot(juce::var(snapshot));
  }
  static void leaveRemotePreview(MTControlsAudioProcessorEditor &editor) { editor.playbackSession.setRemote(false); }
  static void login(MTControlsAudioProcessorEditor &editor,
                    const juce::String &user, const juce::String &password) {
    editor.launchLocalDemoLogin(user, password);
  }
  static void logout(MTControlsAudioProcessorEditor &editor) {
    editor.handleLogout();
  }
  static uint64_t appleFixture(MTControlsAudioProcessorEditor& editor, const juce::String& provider = "apple.com") {
    editor.appleCredentialMonitor.reset();
    editor.localDemoSession = false;
    editor.sessionResumeCancelled = false;
    editor.activeSession.uid = "apple-lifecycle-fixture";
    editor.activeSession.email = "canonical@example.test";
    editor.activeSession.authProvider = provider;
    editor.activeSession.authProviderUser = "verified-apple-subject";
    editor.activeSession.refreshToken = "fixture-refresh-not-real";
    editor.activeSession.firebaseApiKey = "fixture-public-key";
    editor.activeSession.allowed = true;
    ++editor.appleCredentialGeneration;
    editor.saveSessionSnapshotToDisk();
    return editor.appleCredentialGeneration;
  }
  static void appleState(MTControlsAudioProcessorEditor& editor, uint64_t generation,
                         NativeAppleSignIn::CredentialState state,
                         const juce::String& uid = "apple-lifecycle-fixture",
                         const juce::String& user = "verified-apple-subject") {
    editor.applyAppleCredentialState(generation, uid, user, state);
  }
  static void lateResume(MTControlsAudioProcessorEditor& editor, PersistedSessionSnapshot snapshot) {
    editor.applySessionResumeOnMessageThread(true, true, std::move(snapshot), {}, false);
  }
  static bool local(const MTControlsAudioProcessorEditor &editor) {
    return editor.localDemoSession;
  }
  static bool ready(const MTControlsAudioProcessorEditor &editor) {
    return editor.mainUiInitialized && editor.hasCurrentLoadedSong &&
        !editor.demoLoadingOverlay && !editor.songLoadBusy;
  }
  static bool welcome(const MTControlsAudioProcessorEditor &editor) {
    return editor.welcomeModal && editor.welcomeModal->isVisible();
  }
  static bool hasCloudIdentity(const MTControlsAudioProcessorEditor &editor) {
    return editor.activeSession.uid.isNotEmpty() ||
        editor.activeSession.refreshToken.isNotEmpty() ||
        editor.activeSession.firebaseApiKey.isNotEmpty() ||
        editor.activeSession.licenseKey.isNotEmpty();
  }
  static bool allowed(const MTControlsAudioProcessorEditor &editor) {
    return editor.activeSession.allowed;
  }
  static bool hasLesson(const MTControlsAudioProcessorEditor &editor) {
    return editor.learningGuide != nullptr || editor.learningLessonId.isNotEmpty();
  }
  static const ImportedSongData &song(const MTControlsAudioProcessorEditor &editor) {
    return editor.currentLoadedSong;
  }
  static void exerciseProtectedOperations(MTControlsAudioProcessorEditor &editor) {
    editor.saveSessionSnapshotToDisk();
    editor.tryResumeSessionAsync();
    editor.refreshCloudLibrary("offline demo regression");
    editor.updateTeamsOnboarding();
  }
#if !JUCE_IOS && !JUCE_ANDROID
  static void installLyricsPreview(MTControlsAudioProcessorEditor &editor,
                                  MTControlsAudioProcessor &processor,
                                  const playworship::lyrics::SongLyrics &lyrics) {
    juce::String error;
    expect(SongLibraryStore::updateSongFreeShowCues(editor.currentLoadedSong.id, lyrics, &error),
           "preview persists independent lyric marks through the project store");
    editor.currentLoadedSong.freeShowCues = lyrics;
    processor.setSongLyrics(lyrics);
    expect(editor.lyricsPanel != nullptr, "desktop lyric panel is present");
    editor.lyricsPanel->setSongLyrics(lyrics);
    editor.lyricsPanel->setDuration(processor.getSongLengthSeconds());
    editor.lyricsPanel->setArrangement(processor.getSongArrangementSnapshot());
    editor.lyricsViewActive = true;
    editor.resized();
  }
#endif
  static bool teamsAndCatalogRemainInactive(const MTControlsAudioProcessorEditor &editor) {
    return !editor.teamsOnboardingPending && !editor.hasTeamsMembership &&
        !editor.canManageSharedCloud && editor.cloudCatalogSongs.empty() &&
        editor.cloudDownloadsInProgress.empty() && editor.cloudUploadsInProgress.empty();
  }
};

struct SongListPanelTestAccess {
  static SongImportWizardComponent &clickLibraryPlus(SongListPanel &panel) {
    panel.viewMode = SongListPanel::ViewMode::Songs;
    panel.showPage(1, false);
    panel.openLibraryBrowser();
    for (auto *child : panel.libraryCatalogPanel.getChildren()) {
      if (child->getName() != "createSong") continue;
      auto *button = dynamic_cast<juce::Button *>(child);
      expect(button != nullptr, "Biblioteca exposes its real create-song button");
      button->onClick();
      expect(panel.libraryCreationWizard != nullptr && !panel.songImportOverlay,
             "Biblioteca + opens the production internal creation page");
      return *panel.libraryCreationWizard;
    }
    throw std::runtime_error("Biblioteca contains the create-song action");
  }

  static juce::Component *editorOverlay(SongListPanel &panel) {
    // SongWizardOverlay is intentionally private to SongListPanel.cpp. Match
    // its owned address to the real host child without exposing a test route.
    if (auto *host = panel.getParentComponent())
      for (auto *child : host->getChildren())
        if (static_cast<void *>(child) ==
            static_cast<void *>(panel.songImportOverlay.get()))
          return child;
    return nullptr;
  }

  static SongImportWizardComponent &openEditor(SongListPanel &panel,
                                               const juce::String &songId) {
    panel.openSongEditWizard(songId);
    auto *overlay = editorOverlay(panel);
    expect(overlay != nullptr, "Editar opens its owned overlay under the actual editor host");
    for (auto *child : overlay->getChildren())
      if (auto *wizard = dynamic_cast<SongImportWizardComponent *>(child))
        return *wizard;
    throw std::runtime_error("song editor overlay contains its real wizard");
  }
};

struct SongImportWizardTestAccess {
  static juce::TextEditor &title(SongImportWizardComponent &wizard) {
    return wizard.songTitleEditor;
  }
  static void close(SongImportWizardComponent &wizard) { wizard.closeDialog(); }
};

struct WaveformEditingTestAccess {
  static std::vector<juce::Component *> controls(TopBar &top) {
    return {&top.playButton, &top.stopButton, &top.metroToggleButton, &top.keyDisplay, &top.bpmSegmentButton};
  }
  static void refreshTop(TopBar &top) { top.refreshFromProviders(); }
  static std::vector<bool> availability(TopBar &top) {
    std::vector<bool> result; for (auto *control : controls(top)) result.push_back(control->isVisible()); return result;
  }
  static void checkTop(TopBar &top, bool blank, const std::vector<bool> &before) {
    expect(!blank || (!top.waveformViewButton.isVisible() && !top.sectionsViewButton.isVisible()),
           "blank hides waveform and section selectors");
    const auto children = controls(top);
    for (size_t index = 0; index < children.size(); ++index) {
      auto *control = children[index];
      // Existing compact phones can fold the direct BPM segment away. The
      // blank layout must preserve every control available in that profile.
      if (!before[index]) continue;
      const bool fits = control->isVisible() && !control->getBounds().isEmpty() && top.getLocalBounds().contains(control->getBounds());
      if (!fits) std::cerr << "top control='" << control->getName() << "' bounds=" << control->getBounds().toString()
                          << " visible=" << control->isVisible() << " parent=" << top.getLocalBounds().toString() << '\n';
      expect(fits, "transport, click, key and tempo remain visible inside the top bar");
    }
  }
};

namespace {
void testOfflineAssetGateAndPrepareResidentPads() {
  AppSettings::setLocalDemoSessionActive(false);
  juce::String error;
  const auto stateBefore = AppSettings::getAssetsBootstrapState().padsStatus;
  const auto begin = juce::Time::getMillisecondCounterHiRes();
  expect(!AssetBootstrapManager::ensurePadsPackReady(&error, false) &&
             error.isNotEmpty(),
         "missing Pads do not start a download before choosing an authenticated session");
  expect(juce::Time::getMillisecondCounterHiRes() - begin < 2000.0 &&
             AppSettings::getAssetsBootstrapState().padsStatus == stateBefore,
         "cache-only bootstrap returns promptly without changing state to Downloading");
  AppSettings::setLocalDemoSessionActive(true);
  error.clear();
  expect(!AssetBootstrapManager::ensurePadsPackReady(&error, true) && error.isNotEmpty() &&
             AppSettings::getAssetsBootstrapState().padsStatus == stateBefore,
         "persisted local mode blocks missing Pads even if a caller requests online bootstrap");

  // Prepare an entirely private small local pack before constructing the
  // processor. Its legacy desktop fallback must never inspect/copy personal
  // Downloads for this integration test.
  const auto pads = AssetBootstrapManager::getPadsPackDirectory();
  auto stream = std::unique_ptr<juce::OutputStream>(pads.getChildFile("C.wav").createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(44100).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "create resident local pad fixture");
  juce::AudioBuffer<float> samples(1, 44100);
  samples.clear(); // Private synthetic fixture; no third-party audio is used.
  expect(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
         "write resident pad fixture");
  writer.reset();
  const juce::StringArray names{"C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  for (const auto &name : names)
    expect(pads.getChildFile("C.wav").copyFileTo(pads.getChildFile(name + ".wav")),
           "copy the twelve private resident pad notes");
  expect(AssetBootstrapManager::ensurePadsPackReady(&error, false),
         "local mode retains access to an already-installed pack without downloading");
}

struct AccountFixture {
  AccountFixture() {
    PersistedSessionSnapshot existing;
    existing.uid = "offline-demo-real-account-fixture";
    existing.email = "fixture@example.test";
    existing.firebaseApiKey = "public-key-fixture";
    existing.refreshToken = "refresh-fixture-not-a-token";
    existing.licenseKey = "license-fixture";
    existing.plan = "fixture-plan";
    existing.subscriptionStatus = "expired";
    existing.allowed = false;
    existing.lastOnlineValidationMs = 12345678;
    expect(SessionPersistence::save(existing), "create isolated authenticated account fixture");
    snapshot = SessionPersistence::getSnapshotFile();
    bytes = snapshot.loadFileAsString();
    modificationTime = snapshot.getLastModificationTime();
    AppSettings::setLastSuccessfulLoginMs(12345678);
    AppSettings::setReauthGraceDeadlineMs(87654321);
  }

  void unchanged() const {
    expect(snapshot.existsAsFile() && snapshot.loadFileAsString() == bytes &&
               snapshot.getLastModificationTime() == modificationTime,
           "local entry, restart and exit preserve the real account snapshot byte for byte");
    expect(AppSettings::getLastSuccessfulLoginMs() == 12345678 &&
               AppSettings::getReauthGraceDeadlineMs() == 87654321,
           "local demo never changes the real account login or renewal timestamps");
  }

  juce::File snapshot;
  juce::String bytes;
  juce::Time modificationTime;
};

void assertPreparedDemo(MTControlsAudioProcessor &processor,
                        MTControlsAudioProcessorEditor &editor) {
  waitFor([&] { return LocalDemoAccessTestAccess::ready(editor); },
          "local access installs and loads the included demo");
  const auto &song = LocalDemoAccessTestAccess::song(editor);
  expect(LocalDemoAccessTestAccess::local(editor) &&
             AppSettings::getLocalDemoSessionActive() &&
             LocalDemoAccessTestAccess::allowed(editor),
         "local preference restores a permitted local workspace");
  expect(!LocalDemoAccessTestAccess::hasCloudIdentity(editor),
         "demo access never invents a Firebase UID, tokens or license");
  expect(DemoSongService::isDemoSong(song) && song.stems.size() == 8 &&
             processor.getLoadedStemCount() == 8,
         "included eight-instrument demo is the loaded ordinary song");
  expect(std::abs(processor.getSongLengthSeconds() - 90.0) < 0.001 &&
             !processor.isSongPlaying(),
         "demo opens for 90 seconds in pause without autoplay");
  expect(!LocalDemoAccessTestAccess::hasLesson(editor) &&
             !LocalDemoAccessTestAccess::welcome(editor),
         "quick access enters the app without forcing the learning overlay");
  for (const auto &stem : song.stems)
    expect(juce::File(stem.storedPath).existsAsFile() &&
               juce::File(stem.storedPath).isAChildOf(isolatedTestRoot()),
           "every instrument comes from the bundled private installation");
}

void testBlankSongLayoutAndTransitions() {
  AppSettings::setLocalDemoSessionActive(true);
  MTControlsAudioProcessor processor; processor.prepareToPlay(44100.0, 256);
  MTControlsAudioProcessorEditor editor(processor);
  assertPreparedDemo(processor, editor);
  const auto audioSong = LocalDemoAccessTestAccess::song(editor);
  auto &wave = LocalDemoAccessTestAccess::wave(editor);
  waitFor([&] { return !wave.getWaveformBinsCopy().empty(); }, "normal song waveform arrives through its real cache/decoder");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_BLANK_LAYOUT_CAPTURE_DIR", {});
  juce::Array<juce::var> geometry;
  struct Size { int width, height; bool phone; const char *name; };
  const Size sizes[] = {{667, 375, true, "phone-small"}, {844, 390, true, "phone"},
                        {1024, 768, false, "tablet"}, {768, 1024, false, "tablet-portrait"},
                        {1280, 900, false, "desktop"}, {1440, 900, false, "desktop-wide"}};
  std::vector<juce::Rectangle<int>> normalMixer;
  std::vector<int> normalWave;
  std::vector<std::vector<bool>> normalControls;
  const auto capture = [&](const Size &size, const char *phase) {
    WaveformEditingTestAccess::refreshTop(LocalDemoAccessTestAccess::top(editor));
    auto *row = new juce::DynamicObject();
    row->setProperty("phase", phase); row->setProperty("profile", size.name);
    row->setProperty("size", juce::String(size.width) + "x" + juce::String(size.height));
    row->setProperty("wave", wave.getBounds().toString());
    row->setProperty("mixer", LocalDemoAccessTestAccess::mixerBounds(editor).toString());
    geometry.add(juce::var(row));
    if (capturePath.isEmpty()) return;
    const auto file = juce::File(capturePath).getChildFile(juce::String(phase) + "-" + size.name + ".png");
    file.getParentDirectory().createDirectory(); file.deleteFile();
    juce::FileOutputStream stream(file); juce::PNGImageFormat png;
    expect(stream.openedOk() && png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()), stream),
           "real editor snapshot writes");
  };
  for (const auto &size : sizes) {
    LocalDemoAccessTestAccess::layout(editor, size.width, size.height, size.phone);
    expect(wave.isVisible() && wave.getHeight() > 0, "normal song reserves its real waveform area");
    expect(LocalDemoAccessTestAccess::padsMatchProfile(editor, size.phone), "ordinary profile keeps the existing pad drawer available");
    normalMixer.push_back(LocalDemoAccessTestAccess::mixerBounds(editor)); normalWave.push_back(wave.getHeight());
    capture(size, "audio");
    normalControls.push_back(WaveformEditingTestAccess::availability(LocalDemoAccessTestAccess::top(editor)));
  }
  LocalDemoAccessTestAccess::expandWave(editor, true);
  ImportedSongData blank; blank.blankSong = true; blank.id = "blank-layout";
  blank.title = juce::String::fromUTF8("Adoración libre"); blank.key = "C"; blank.bpm = 126; blank.timeSignature = "4/4";
  expect(LocalDemoAccessTestAccess::load(editor, blank), "editor loads a real blank song without stems");
  drainMessages(220);
  expect(LocalDemoAccessTestAccess::expanded(editor), "collapsing does not discard the normal waveform height preference");
  processor.setMetronomeEnabled(true); processor.setPadsEnabled(true); processor.setPadsVolume(0.37f);
  processor.setPlaybackTempoBpm(136.0); processor.setPitchSemiTones(2); processor.playSong();
  juce::AudioBuffer<float> output(processor.getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
  for (int block = 0; block < 8; ++block) processor.processBlock(output, midi);
  const auto position = processor.getSongCurrentPositionSeconds();
  const auto revision = processor.getTransportControlRevision();
  for (size_t index = 0; index < std::size(sizes); ++index) {
    const auto &size = sizes[index];
    LocalDemoAccessTestAccess::layout(editor, size.width, size.height, size.phone);
    const auto mixer = LocalDemoAccessTestAccess::mixerBounds(editor);
    expect(!wave.isVisible() && wave.getHeight() == 0 && wave.getBounds().isEmpty(), "blank waveform occupies zero host space");
    expect(mixer.getY() == LocalDemoAccessTestAccess::top(editor).getBottom() &&
               mixer.getY() == normalMixer[index].getY() - normalWave[index] &&
               mixer.getHeight() == normalMixer[index].getHeight() + normalWave[index],
           "all former waveform height is recovered by the existing mixer");
    expect(editor.getLocalBounds().contains(mixer), "expanded mixer fits the phone/tablet/desktop editor");
    expect(LocalDemoAccessTestAccess::padsMatchProfile(editor, size.phone), "blank preserves the existing phone pad action or desktop drawer visibility");
    capture(size, "blank");
    WaveformEditingTestAccess::checkTop(LocalDemoAccessTestAccess::top(editor), true, normalControls[index]);
    expect(processor.isSongPlaying() && processor.getSongCurrentPositionSeconds() == position &&
               processor.getTransportControlRevision() == revision && processor.isMetronomeEnabled() &&
               processor.isPadsEnabled() && std::abs(processor.getPadsVolume() - 0.37f) < 0.001f &&
               processor.getPlaybackTempoBpm() == 136.0 && processor.getPitchSemiTones() == 2,
           "layout changes never restart transport or overwrite click/pad/key/tempo state");
  }
  processor.processBlock(output, midi);
  expect(processor.getSongCurrentPositionSeconds() > position, "audio clock continues after relayout");
  processor.pauseSong();
  expect(LocalDemoAccessTestAccess::load(editor, audioSong), "normal song restores through the existing loader");
  drainMessages(220);
  expect(!processor.isBlankSongLoaded() && processor.getLoadedStemCount() == 8 && !processor.isSongPlaying(),
         "restoring ordinary audio loads its real stems without unintended autoplay");
  expect(LocalDemoAccessTestAccess::expanded(editor) && wave.isVisible() && wave.getHeight() > 0,
         "normal song restores the saved expanded waveform without another resize");
  LocalDemoAccessTestAccess::expandWave(editor, false);
  waitFor([&] { return !wave.getWaveformBinsCopy().empty(); }, "normal waveform returns after blank playback");
  for (size_t index = 0; index < std::size(sizes); ++index) {
    const auto &size = sizes[index]; LocalDemoAccessTestAccess::layout(editor, size.width, size.height, size.phone);
    expect(wave.isVisible() && wave.getHeight() == normalWave[index] &&
               LocalDemoAccessTestAccess::mixerBounds(editor) == normalMixer[index],
           "ordinary audio restores the exact original adaptive geometry");
    expect(LocalDemoAccessTestAccess::padsMatchProfile(editor, size.phone), "restored audio keeps the pad drawer visible after leaving phone mode");
    capture(size, "restored");
  }
  wave.setWaveformBins({}); wave.setSongLoadingState(true, 0.25, "Leyendo audio");
  LocalDemoAccessTestAccess::refreshPresentation(editor);
  expect(wave.isVisible() && wave.getHeight() > 0, "normal missing bins/loading state never collapses the waveform");
  auto missing = audioSong; missing.stems.clear();
  LocalDemoAccessTestAccess::presentMetadata(editor, missing);
  expect(wave.isVisible() && wave.getHeight() > 0, "normal missing-audio metadata is not inferred to be a blank");
  const auto beforeRemote = processor.getTransportControlRevision();
  LocalDemoAccessTestAccess::remoteSnapshot(editor, blank);
  expect(!wave.isVisible() && wave.getHeight() == 0, "remote blank snapshot immediately collapses the host area");
  auto withAudio = blank; withAudio.blankSong = false; withAudio.stems = audioSong.stems;
  LocalDemoAccessTestAccess::remoteSnapshot(editor, withAudio);
  expect(wave.isVisible() && wave.getHeight() > 0, "adding audio to the same remote song restores its waveform area");
  withAudio.blankSong = true; // A stale flag must not hide received real stems.
  LocalDemoAccessTestAccess::remoteSnapshot(editor, withAudio);
  expect(wave.isVisible() && wave.getHeight() > 0, "explicit stems win over a stale blank flag");
  expect(processor.getTransportControlRevision() == beforeRemote, "remote presentation does not mutate the local transport");
  LocalDemoAccessTestAccess::leaveRemotePreview(editor);
  if (capturePath.isNotEmpty())
    expect(juce::File(capturePath).getChildFile("geometry.json").replaceWithText(juce::JSON::toString(juce::var(geometry))), "geometry report writes");
}

void testPhoneDrawerTextInputSurvivesRelayout() {
  AppSettings::setLocalDemoSessionActive(false);
  MTControlsAudioProcessor processor;
  MTControlsAudioProcessorEditor editor(processor);
  LocalDemoAccessTestAccess::initializePhoneInputUi(editor);
  LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, 390, 800);
  auto &library = LocalDemoAccessTestAccess::library(editor);
  auto &wizard = SongListPanelTestAccess::clickLibraryPlus(library);
  drainMessages(380); // Complete the actual Biblioteca -> Detalles transition.
  LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, 390, 800);
  auto &scrim = LocalDemoAccessTestAccess::drawerScrim(editor);
  auto &title = SongImportWizardTestAccess::title(wizard);
  title.setText("Ensayo Android", juce::dontSendNotification);
  title.setHighlightedRegion({2, 7});
  const auto draft = title.getText();
  const auto selection = title.getHighlightedRegion();
  const auto caret = title.getCaretPosition();

  struct StabilityObserver final : juce::ComponentListener {
    explicit StabilityObserver(juce::Component &target) : component(target) {
      component.addComponentListener(this);
    }
    ~StabilityObserver() override { component.removeComponentListener(this); }
    void componentVisibilityChanged(juce::Component &changed) override {
      if (!changed.isVisible()) ++hidden;
    }
    void componentMovedOrResized(juce::Component &changed, bool, bool) override {
      if (changed.getBounds().isEmpty()) ++emptyBounds;
    }
    juce::Component &component;
    int hidden = 0, emptyBounds = 0;
  } libraryChanges(library), scrimChanges(scrim);

  // Android propagates IME/system insets to resized() even when window bounds
  // are unchanged. Shorter viewport and orientation reflows exercise the same
  // root phone-layout path; checking only the isolated wizard missed this bug.
  for (const auto size : {juce::Point<int>{390, 800}, {390, 800}, {390, 560},
                          {390, 800}, {800, 390}, {360, 720}, {390, 800}}) {
    LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, size.x, size.y);
    expect(libraryChanges.hidden == 0 && libraryChanges.emptyBounds == 0 &&
               scrimChanges.hidden == 0 && scrimChanges.emptyBounds == 0,
           "open phone drawer never hides or empties its text-input ancestors during relayout");
    expect(library.isVisible() && scrim.isVisible() && title.isVisible() &&
               !title.getBounds().isEmpty() && !wizard.getBounds().isEmpty(),
           "the real creation page remains visible and laid out in the open drawer");
    expect(title.getText() == draft && title.getHighlightedRegion() == selection &&
               title.getCaretPosition() == caret,
           "phone inset and orientation reflows preserve the draft, selection and caret");
  }
  title.insertTextAtCaret("ayo");
  expect(title.getText() == draft.replaceSection(selection.getStart(), selection.getLength(), "ayo"),
         "typing continues at the preserved selection after repeated root reflows");

  // Native focus is additional evidence when the desktop grants it. The
  // deterministic visibility regression above always runs, without OS focus.
  juce::DocumentWindow focusWindow("PlayWorship phone input regression", juce::Colours::black, 0);
  focusWindow.setUsingNativeTitleBar(true);
  focusWindow.setContentNonOwned(&editor, false);
  focusWindow.setContentComponentSize(390, 800);
  focusWindow.centreWithSize(focusWindow.getWidth(), focusWindow.getHeight());
  focusWindow.setVisible(true);
  focusWindow.toFront(true);
  LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, 390, 800);
  // Creating the desktop host above is a separate profile transition, not an
  // Android inset update. Observe the now-open phone drawer from this point.
  libraryChanges.hidden = libraryChanges.emptyBounds = 0;
  scrimChanges.hidden = scrimChanges.emptyBounds = 0;
  title.grabKeyboardFocus();
  const bool nativeFocusGranted = title.hasKeyboardFocus(false);
  if (nativeFocusGranted) {
    LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, 390, 800);
    LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, 390, 800);
    expect(title.hasKeyboardFocus(false),
           "an OS-focused text editor keeps focus through unchanged keyboard-inset relayouts");
  } else {
    std::cout << "Phone input: OS focus unavailable; deterministic drawer and draft checks ran.\n";
  }
  expect(libraryChanges.hidden == 0 && libraryChanges.emptyBounds == 0 &&
             scrimChanges.hidden == 0 && scrimChanges.emptyBounds == 0,
         "native focus checks do not hide or empty the open drawer");
  LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, 390, 800, false);
  expect(!library.isVisible() && !scrim.isVisible() && libraryChanges.hidden == 1 &&
             scrimChanges.hidden == 1,
         "closing the phone drawer still hides the library and scrim exactly once");
  if (nativeFocusGranted)
    expect(!title.hasKeyboardFocus(false), "closing the drawer releases the hidden text editor's focus");
  focusWindow.clearContentComponent();
}

void testPhoneSongEditTextInputSurvivesRelayout() {
  AppSettings::setLocalDemoSessionActive(false);
  ImportedSongData song;
  expect(DemoSongService::ensureInstalled(song).wasOk(),
         "song editor input fixture uses a real local song without network access");
  MTControlsAudioProcessor processor;
  class PhoneProfileEditor final : public MTControlsAudioProcessorEditor {
  public:
    using MTControlsAudioProcessorEditor::MTControlsAudioProcessorEditor;
    bool simulatePhoneProfile = false;
    void resized() override {
      if (simulatePhoneProfile &&
          LocalDemoAccessTestAccess::relayoutPhoneInputUi(*this))
        return;
      MTControlsAudioProcessorEditor::resized();
    }
  } editor(processor);
  LocalDemoAccessTestAccess::initializePhoneInputUi(editor);
  juce::DocumentWindow focusWindow("PlayWorship song editor input regression",
                                    juce::Colours::black, 0);
  focusWindow.setUsingNativeTitleBar(true);
  focusWindow.setContentNonOwned(&editor, false);
  focusWindow.setContentComponentSize(390, 800);
  focusWindow.centreWithSize(focusWindow.getWidth(), focusWindow.getHeight());
  focusWindow.setVisible(true);
  focusWindow.toFront(true);
  // Finish native desktop work-area startup before simulating a phone. The
  // fixture then retains that profile for later host/focus resize callbacks;
  // macOS itself cannot infer the phone profile from these window dimensions.
  drainMessages(100);
  editor.simulatePhoneProfile = true;
  LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, 390, 800);
  auto &library = LocalDemoAccessTestAccess::library(editor);
  library.openLocalLibrary();
  auto &scrim = LocalDemoAccessTestAccess::drawerScrim(editor);
  auto &wizard = SongListPanelTestAccess::openEditor(library, song.id);
  auto *const overlay = SongListPanelTestAccess::editorOverlay(library);
  expect(overlay->getParentComponent() == &editor &&
             library.getParentComponent() == &editor,
         "Editar and Biblioteca are siblings under the actual application editor");
  auto &title = SongImportWizardTestAccess::title(wizard);
  title.setText("Ensayo iPhone", juce::dontSendNotification);
  title.setHighlightedRegion({2, 7});
  title.grabKeyboardFocus();
  const bool nativeFocusGranted = title.hasKeyboardFocus(false);
  const auto draft = title.getText();
  const auto selection = title.getHighlightedRegion();
  const auto caret = title.getCaretPosition();

  // A keyboard inset refresh can relayout unchanged bounds. Also cover the
  // smaller available area and rotation: both raise the real library drawer.
  for (const auto size : {juce::Point<int>{390, 800}, {390, 800}, {390, 560},
                          {390, 800}, {800, 390}, {360, 720}, {390, 800}}) {
    LocalDemoAccessTestAccess::layoutPhoneDrawer(editor, size.x, size.y);
    expect(library.isSongEditorOpen() &&
               SongListPanelTestAccess::editorOverlay(library) == overlay &&
               overlay->isVisible() && wizard.isVisible() && title.isVisible(),
           "keyboard and orientation reflows preserve the same open song editor");
    expect(editor.getIndexOfChildComponent(overlay) >
                   editor.getIndexOfChildComponent(&library) &&
               editor.getIndexOfChildComponent(overlay) >
                   editor.getIndexOfChildComponent(&scrim),
           "Editar stays in front of Biblioteca and its scrim after each root reflow");
    const auto point = editor.getLocalPoint(&title, title.getLocalBounds().getCentre());
    auto *hit = editor.getComponentAt(point);
    expect(hit == &title || title.isParentOf(hit),
           "the song text field still receives touches instead of the covered library");
    expect(title.getText() == draft && title.getHighlightedRegion() == selection &&
               title.getCaretPosition() == caret,
           "song editor reflows retain the draft, selection and caret");
    if (nativeFocusGranted)
      expect(title.hasKeyboardFocus(false),
             "raising the song overlay preserves the already focused editor");
  }
  if (!nativeFocusGranted)
    std::cout << "Song editor input: OS focus unavailable; deterministic stacking and draft checks ran.\n";
  title.insertTextAtCaret("ayo");
  expect(title.getText() == draft.replaceSection(selection.getStart(), selection.getLength(), "ayo"),
         "editing continues at the preserved selection after song editor root reflows");
  SongImportWizardTestAccess::close(wizard);
  waitFor([&] { return !library.isSongEditorOpen(); },
          "the explicit song editor close still dismisses its overlay");
  if (editor.getChildren().contains(overlay) || !library.isVisible() || !scrim.isVisible())
    std::cout << "Song editor close state: overlayChild="
              << editor.getChildren().contains(overlay)
              << " libraryVisible=" << library.isVisible()
              << " scrimVisible=" << scrim.isVisible()
              << " drawerOpen=" << LocalDemoAccessTestAccess::drawerOpen(editor)
              << " setlistCollapsed=" << LocalDemoAccessTestAccess::setlistCollapsed(editor)
              << " libraryStillOwned=" << (library.getParentComponent() == &editor)
              << " editorBounds=" << editor.getBounds().toString() << '\n';
  expect(!editor.getChildren().contains(overlay) && library.isVisible() && scrim.isVisible(),
         "closing Editar exposes the still-open library drawer without changing its owner");
  focusWindow.clearContentComponent();
}

void testLocalSessionIsolationAndRestart() {
  AccountFixture account;
  // Persisted local state takes priority even if there is also a denied/old
  // account snapshot. Using Firebase resume here would clear that snapshot.
  AppSettings::setLocalDemoSessionActive(true);
  {
    MTControlsAudioProcessor processor;
    MTControlsAudioProcessorEditor editor(processor);
    assertPreparedDemo(processor, editor);
    account.unchanged();

    LocalDemoAccessTestAccess::exerciseProtectedOperations(editor);
    drainMessages(80);
    expect(LocalDemoAccessTestAccess::teamsAndCatalogRemainInactive(editor),
           "cloud search, Teams polling and synchronization stay inactive locally");
    expect(refreshAttempts.load() == 0 && validationAttempts.load() == 0,
           "local startup and explicit resume/cloud requests never call Firebase transports");
    account.unchanged();

    // Deliver both a late success and a late denial while local mode is active.
    // Neither may replace local identity, erase credentials, or force logout.
    PersistedSessionSnapshot late;
    late.uid = "stale-network-account";
    late.allowed = true;
    late.refreshToken = "stale-refresh";
    late.firebaseApiKey = "stale-key";
    late.plan = "teams";
    late.subscriptionStatus = "active";
    editor.applySessionResumeOnMessageThread(true, true, late, {}, false);
    editor.applySessionResumeOnMessageThread(false, false, late, "stale denial", true);
    expect(LocalDemoAccessTestAccess::local(editor) &&
               !LocalDemoAccessTestAccess::hasCloudIdentity(editor) &&
               !LocalDemoAccessTestAccess::welcome(editor),
           "delayed authentication results cannot replace or revoke a local demo session");
    account.unchanged();

    LocalDemoAccessTestAccess::logout(editor);
    expect(!LocalDemoAccessTestAccess::local(editor) &&
               !AppSettings::getLocalDemoSessionActive() &&
               LocalDemoAccessTestAccess::welcome(editor),
           "leaving local mode returns to normal sign-in and clears only the local preference");
    account.unchanged();

    LocalDemoAccessTestAccess::login(editor, "another-user", "usuarioplay0324");
    LocalDemoAccessTestAccess::login(editor, "usuarioplay0324", "incorrect");
    expect(!LocalDemoAccessTestAccess::local(editor) &&
               !AppSettings::getLocalDemoSessionActive() &&
               LocalDemoAccessTestAccess::welcome(editor),
           "incorrect local credentials neither enter nor persist a local session");
    account.unchanged();

    LocalDemoAccessTestAccess::login(editor, "usuarioplay0324", "usuarioplay0324");
    assertPreparedDemo(processor, editor);
    account.unchanged();
  }
  drainMessages();

  // A new editor exercises restart rather than merely consulting the getter.
  {
    MTControlsAudioProcessor processor;
    MTControlsAudioProcessorEditor editor(processor);
    assertPreparedDemo(processor, editor);
    expect(SongLibraryStore::loadSongs().size() == 1,
           "restarting local mode reuses the demo without duplicate library entries");
    account.unchanged();
    LocalDemoAccessTestAccess::logout(editor);
    account.unchanged();
  }
  drainMessages(80);
  expect(refreshAttempts.load() == 0 && validationAttempts.load() == 0,
         "all local credential, session restart and Teams scenarios make zero Firebase requests");
}

void previewLocalDemoLogin(int seconds, bool lyricsPreview = false) {
  // The optional real peer starts at Welcome, with no account fixture and
  // private app storage. It supports manual typing/navigation/transport and
  // is never shown by CTest. Reusing the production editor gives visual QA
  // both before and after entering local mode without signing out a user.
  SessionPersistence::clear();
  AppSettings::setLocalDemoSessionActive(false);
  AppSettings::setLastSuccessfulLoginMs(0);
  AppSettings::setReauthGraceDeadlineMs(0);
  MTControlsAudioProcessor processor;
  class PreviewWindow final : public juce::DocumentWindow {
  public:
    explicit PreviewWindow(MTControlsAudioProcessor &processor, bool lyricsView)
        : juce::DocumentWindow(lyricsView ? "PlayWorship - Editor real (prueba aislada)"
                                        : "PlayWorship - Acceso local (prueba aislada)",
                               juce::Colour(0xff14221b),
                               juce::DocumentWindow::closeButton) {
      setUsingNativeTitleBar(true);
      setResizable(true, false);
      setContentOwned(new MTControlsAudioProcessorEditor(processor), false);
      setContentComponentSize(lyricsView ? 1440 : 1200, lyricsView ? 900 : 760);
      centreWithSize(getWidth(), getHeight());
      setVisible(true);
      toFront(true);
    }
    void closeButtonPressed() override { setVisible(false); }
  } window(processor, lyricsPreview);

#if !JUCE_IOS && !JUCE_ANDROID
  if (lyricsPreview) {
    auto &editor = *static_cast<MTControlsAudioProcessorEditor *>(window.getContentComponent());
    // The existing local-demo login flow is exercised unchanged. Only this
    // optional isolated test prepares data; no production authentication path changes.
    LocalDemoAccessTestAccess::login(editor, "usuarioplay0324", "usuarioplay0324");
    assertPreparedDemo(processor, editor);
    auto &client = processor.getFreeShowClient();
    std::atomic<bool> connected{false}, connectionFinished{false}, catalogueFinished{false};
    client.testConnection([&](bool ok, juce::String) {
      connected.store(ok); connectionFinished.store(true);
    });
    waitFor([&] { return connectionFinished.load(); }, "real FreeShow connection completes");
    expect(connected.load(), "lyrics preview requires the real local FreeShow API");
    playworship::lyrics::ShowCatalog catalogue;
    client.requestShow("pw-synthetic-validation", [&](auto result) {
      catalogue = std::move(result); catalogueFinished.store(true);
    });
    waitFor([&] { return catalogueFinished.load(); }, "real FreeShow catalogue completes");
    expect(catalogue.ok(), "real synthetic FreeShow show is resolved");
    const playworship::lyrics::Layout *layout = nullptr;
    for (const auto &candidate : catalogue.layouts)
      if (candidate.id == "principal") layout = &candidate;
    expect(layout != nullptr && layout->slides.size() == 7,
           "fixture contains two chorus screens repeated twice and literal Coro2");
    playworship::lyrics::SongLyrics lyrics;
    lyrics.enabled = true;
    lyrics.showId = catalogue.id; lyrics.showName = catalogue.name;
    lyrics.layoutId = layout->id; lyrics.layoutName = layout->name;
    const double times[] = {0.0, 13.3, 19.4, 35.2, 49.8, 58.9, 75.2};
    for (size_t i = 0; i < layout->slides.size(); ++i) {
      playworship::lyrics::Mark mark;
      mark.id = "preview-mark-" + juce::String(static_cast<int>(i + 1));
      mark.seconds = times[i]; mark.target = layout->slides[i].target;
      lyrics.marks.push_back(std::move(mark));
    }
    playworship::lyrics::Mark hide;
    hide.id = "preview-hide"; hide.seconds = 86.0;
    hide.target.hide = true; hide.target.title = "Ocultar letras";
    lyrics.marks.push_back(std::move(hide));
    LocalDemoAccessTestAccess::installLyricsPreview(editor, processor, lyrics);
    std::cout << "Lyrics preview: real production editor and FreeShow v1.6.6 API; "
                 "own synthetic lyrics; existing bundled demo audio; no output command sent.\n"
              << std::flush;
  }
#else
  juce::ignoreUnused(lyricsPreview);
#endif

  std::cout << "Local demo preview: real editor; private storage; enter "
            << "usuarioplay0324 / usuarioplay0324. Closes in " << seconds
            << " seconds. Firebase calls are intercepted, not sent.\n" << std::flush;
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
  while (window.isVisible() && juce::Time::getMillisecondCounterHiRes() < deadline)
    drainMessages(20);
  expect(refreshAttempts.load() == 0 && validationAttempts.load() == 0,
         "manual local preview never requests Firebase transport");
}
} // namespace

namespace {
void testAppleCredentialStateIsolation() {
  AppSettings::setLocalDemoSessionActive(false);
  SessionPersistence::clear();
  MTControlsAudioProcessor processor;
  MTControlsAudioProcessorEditor editor(processor);
  const auto generation = LocalDemoAccessTestAccess::appleFixture(editor);
  PersistedSessionSnapshot snapshot;
  expect(SessionPersistence::load(snapshot) && snapshot.authProvider == "apple.com" &&
         snapshot.authProviderUser == "verified-apple-subject",
         "Apple provider metadata survives the existing session snapshot without Apple tokens");
  LocalDemoAccessTestAccess::appleState(editor, generation, NativeAppleSignIn::CredentialState::authorized);
  LocalDemoAccessTestAccess::appleState(editor, generation, NativeAppleSignIn::CredentialState::unavailable);
  LocalDemoAccessTestAccess::appleState(editor, generation - 1, NativeAppleSignIn::CredentialState::revoked);
  LocalDemoAccessTestAccess::appleState(editor, generation, NativeAppleSignIn::CredentialState::revoked, "old-uid");
  LocalDemoAccessTestAccess::appleState(editor, generation, NativeAppleSignIn::CredentialState::revoked,
                                     "apple-lifecycle-fixture", "old-apple-user");
  expect(LocalDemoAccessTestAccess::hasCloudIdentity(editor) && SessionPersistence::getSnapshotFile().existsAsFile(),
         "authorized, OS errors and stale or foreign revocation callbacks preserve the current session");
  const auto googleGeneration = LocalDemoAccessTestAccess::appleFixture(editor, "google.com");
  LocalDemoAccessTestAccess::appleState(editor, googleGeneration, NativeAppleSignIn::CredentialState::revoked);
  expect(LocalDemoAccessTestAccess::hasCloudIdentity(editor), "Apple revocation never signs out a Google session");
  for (auto state : {NativeAppleSignIn::CredentialState::revoked,
                     NativeAppleSignIn::CredentialState::notFound,
                     NativeAppleSignIn::CredentialState::transferred}) {
    const auto current = LocalDemoAccessTestAccess::appleFixture(editor);
    LocalDemoAccessTestAccess::appleState(editor, current, state);
    LocalDemoAccessTestAccess::appleState(editor, current, state);
    LocalDemoAccessTestAccess::lateResume(editor, snapshot);
    expect(!LocalDemoAccessTestAccess::hasCloudIdentity(editor) &&
           !SessionPersistence::getSnapshotFile().existsAsFile() && LocalDemoAccessTestAccess::welcome(editor),
           "confirmed credential loss clears local session once; late resume cannot restore it");
  }
}
} // namespace

int main(int argc, char **argv) {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  try {
    const auto root = isolatedTestRoot();
    expect(SongLibraryStore::getRootDirectory().isAChildOf(root) &&
               SessionPersistence::getSnapshotFile().isAChildOf(root) &&
               AssetBootstrapManager::getAssetsRootDirectory().isAChildOf(root),
           "editor integration uses only the explicitly isolated app storage");
    juce::ScopedJuceInitialiser_GUI gui;
    const auto *display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (display == nullptr || display->userArea.isEmpty()) {
      std::cerr << "Local demo editor integration requires an available OS display.\n";
      return 77;
    }
    testOfflineAssetGateAndPrepareResidentPads();
    if (argc > 1 && juce::String(argv[1]) == "--blank-layout-only") {
      testBlankSongLayoutAndTransitions();
      ProjectStore::flushAllPendingSettings();
      std::cout << "Blank layout: " << checks << " checks passed; native editor, transport and remote snapshots\n";
      return 0;
    }
    if (argc > 1 && juce::String(argv[1]) == "--phone-input-only") {
      testPhoneDrawerTextInputSurvivesRelayout();
      testPhoneSongEditTextInputSurvivesRelayout();
      ProjectStore::flushAllPendingSettings();
      std::cout << "Phone input: " << checks << " checks passed; real drawer, song-creation page and song editor overlay\n";
      return 0;
    }
    const bool environmentPreview = juce::SystemStats::getEnvironmentVariable(
        "PLAYWORSHIP_LOCAL_DEMO_PREVIEW", {}) == "1";
    const bool lyricsPreview = argc > 1 && juce::String(argv[1]) == "--lyrics-preview";
    if (environmentPreview || lyricsPreview || (argc > 1 && juce::String(argv[1]) == "--preview")) {
      const int seconds = argc > 2 ? juce::jlimit(15, 300, juce::String(argv[2]).getIntValue())
                                  : (environmentPreview ? 300 : 180);
      previewLocalDemoLogin(seconds, lyricsPreview);
      return 0;
    }
    testPhoneDrawerTextInputSurvivesRelayout();
    testPhoneSongEditTextInputSurvivesRelayout();
#if JUCE_DEBUG
    testLocalSessionIsolationAndRestart();
    testAppleCredentialStateIsolation();
#else
    {
      AppSettings::setLocalDemoSessionActive(true);
      MTControlsAudioProcessor processor;
      MTControlsAudioProcessorEditor editor(processor);
      expect(!LocalDemoAccessTestAccess::local(editor), "production never restores persisted test access");
      const bool allowedBefore = LocalDemoAccessTestAccess::allowed(editor);
      LocalDemoAccessTestAccess::login(editor, "usuarioplay0324", "usuarioplay0324");
      expect(!LocalDemoAccessTestAccess::local(editor) && LocalDemoAccessTestAccess::allowed(editor) == allowedBefore,
             "production rejects direct test-login dispatch");
    }
#endif
    ProjectStore::flushAllPendingSettings();
    std::cout << "Local demo access: " << checks
              << " checks passed; Firebase transports: 0; eight bundled stems, no autoplay\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
