#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#include "data/AuthSessionApi.h"
#include "data/DemoSongService.h"
#include "data/SessionPersistence.h"
#include "IsolatedTestRoot.h"

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
  static void login(MTControlsAudioProcessorEditor &editor,
                    const juce::String &user, const juce::String &password) {
    editor.launchLocalDemoLogin(user, password);
  }
  static void logout(MTControlsAudioProcessorEditor &editor) {
    editor.handleLogout();
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
  static bool teamsAndCatalogRemainInactive(const MTControlsAudioProcessorEditor &editor) {
    return !editor.teamsOnboardingPending && !editor.hasTeamsMembership &&
        !editor.canManageSharedCloud && editor.cloudCatalogSongs.empty() &&
        editor.cloudDownloadsInProgress.empty() && editor.cloudUploadsInProgress.empty();
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

void previewLocalDemoLogin(int seconds) {
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
    explicit PreviewWindow(MTControlsAudioProcessor &processor)
        : juce::DocumentWindow("PlayWorship - Acceso local (prueba aislada)",
                               juce::Colour(0xff14221b),
                               juce::DocumentWindow::closeButton) {
      setUsingNativeTitleBar(true);
      setResizable(true, false);
      setContentOwned(new MTControlsAudioProcessorEditor(processor), false);
      setContentComponentSize(1200, 760);
      centreWithSize(getWidth(), getHeight());
      setVisible(true);
      toFront(true);
    }
    void closeButtonPressed() override { setVisible(false); }
  } window(processor);

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
    const bool environmentPreview = juce::SystemStats::getEnvironmentVariable(
        "PLAYWORSHIP_LOCAL_DEMO_PREVIEW", {}) == "1";
    if (environmentPreview || (argc > 1 && juce::String(argv[1]) == "--preview")) {
      const int seconds = argc > 2 ? juce::jlimit(15, 300, juce::String(argv[2]).getIntValue())
                                  : (environmentPreview ? 300 : 180);
      previewLocalDemoLogin(seconds);
      return 0;
    }
    testLocalSessionIsolationAndRestart();
    ProjectStore::flushAllPendingSettings();
    std::cout << "Local demo access: " << checks
              << " checks passed; Firebase transports: 0; eight bundled stems, no autoplay\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
