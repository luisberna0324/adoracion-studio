#include "AppIdentity.h"
#include "PluginEditor.h"
#include "data/ProjectStore.h"

#include <JuceHeader.h>
#include "audio/StandaloneAudioRuntime.h"
#include "ui/NativeEditorBoundsConstrainer.h"
#include "ui/UiCommon.h"
#if JUCE_IOS
#include "platform/IOSWindowLayout.h"
#endif

namespace {

juce::File findProjectArgument(const juce::String &commandLine) {
  juce::StringArray arguments;
  arguments.addTokens(commandLine, true);
  for (const auto &argument : arguments) {
    const juce::File candidate(argument.unquoted());
    if (candidate.hasFileExtension(ProjectStore::kFileExtension))
      return candidate;
  }
  return {};
}

juce::File findInitialProjectArgument(const juce::String &commandLine) {
  if (auto project = findProjectArgument(commandLine); project != juce::File{})
    return project;

  for (const auto &argument :
       juce::JUCEApplicationBase::getCommandLineParameterArray()) {
    const juce::File candidate(argument.unquoted());
    if (candidate.hasFileExtension(ProjectStore::kFileExtension))
      return candidate;
  }
  return {};
}

juce::String makeWindowTitle() {
  return juce::String::fromUTF8(playworship::appName) + " v" + playworship::appVersion;
}

class PlayWorshipStandaloneWindow final
    : public juce::DocumentWindow
#if JUCE_IOS
    , private juce::AsyncUpdater
#endif
{
public:
  PlayWorshipStandaloneWindow(MTControlsAudioProcessor &processorToUse,
                              juce::PropertySet *settingsToUse)
      : juce::DocumentWindow(
            makeWindowTitle(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            juce::DocumentWindow::allButtons),
        processor(processorToUse), settings(settingsToUse),
        editorConstrainer(*this, processorToUse) {
    setConstrainer(&editorConstrainer);
#if JUCE_IOS
    setTitleBarHeight(0);
    setFullScreen(true);
#else
    setUsingNativeTitleBar(true);
#endif
    auto *editor = processor.createEditorIfNeeded();
#if JUCE_IOS
    // UIKit owns window geometry. The editor must not resize the native peer.
    setContentOwned(editor, false);
#else
    setContentOwned(editor, true);
#endif
    if (editor != nullptr)
      setResizable(editor->isResizable(), false);
#if !JUCE_IOS && !JUCE_ANDROID
    if (settings != nullptr && settings->containsKey("windowX") && settings->containsKey("windowY"))
      setTopLeftPosition(settings->getIntValue("windowX"), settings->getIntValue("windowY"));
    else
      centreWithSize(getWidth(), getHeight());
#endif
  }

  ~PlayWorshipStandaloneWindow() override {
#if !JUCE_IOS && !JUCE_ANDROID
    if (settings != nullptr && !isFullScreen()) {
      settings->setValue("windowX", getX());
      settings->setValue("windowY", getY());
    }
#endif
    if (auto *editor = processor.getActiveEditor())
      processor.editorBeingDeleted(editor);
    clearContentComponent();
    setConstrainer(nullptr);
  }

  MTControlsAudioProcessor *getAudioProcessor() { return &processor; }

#if JUCE_IOS
  void parentSizeChanged() override {
    juce::DocumentWindow::parentSizeChanged();
    triggerAsyncUpdate();
  }

  void resized() override {
    juce::DocumentWindow::resized();
    if (!synchronisingLayout)
      triggerAsyncUpdate();
  }
#endif

  void closeButtonPressed() override {
    if (auto *app = juce::JUCEApplicationBase::getInstance())
      app->systemRequestedQuit();
  }

#if JUCE_IOS
private:
  void handleAsyncUpdate() override {
    const juce::ScopedValueSetter<bool> guard(synchronisingLayout, true);
    synchroniseIOSFullscreenWindow(*this);
  }

  bool synchronisingLayout = false;
#endif
private:
  MTControlsAudioProcessor &processor;
  juce::PropertySet *settings;
  NativeEditorBoundsConstrainer editorConstrainer;
};

void queueProjectOpen(PlayWorshipStandaloneWindow *window,
                      const juce::File &projectFile) {
  if (window == nullptr || projectFile == juce::File{})
    return;

  juce::MessageManager::callAsync(
      [safeWindow =
           juce::Component::SafePointer<PlayWorshipStandaloneWindow>(window),
       projectFile] {
        if (safeWindow == nullptr)
          return;
        auto *processor = safeWindow->getAudioProcessor();
        auto *editor = processor != nullptr
                           ? dynamic_cast<MTControlsAudioProcessorEditor *>(
                                 processor->getActiveEditor())
                           : nullptr;
        if (editor != nullptr)
          editor->openProjectFromExternalApplication(projectFile);
      });
}

class PlayWorshipStandaloneApp final : public juce::JUCEApplication {
public:
  PlayWorshipStandaloneApp() {
    juce::PropertiesFile::Options options;
    options.applicationName = juce::CharPointer_UTF8(playworship::appName);
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    options.folderName = "~/.config";
#else
    options.folderName = "";
#endif
    appProperties.setStorageParameters(options);
  }

  const juce::String getApplicationName() override {
    return juce::CharPointer_UTF8(playworship::appName);
  }
  const juce::String getApplicationVersion() override {
    return playworship::appVersion;
  }
  bool moreThanOneInstanceAllowed() override { return false; }

  void memoryWarningReceived() override {
    if (auto *p = mainWindow != nullptr ? dynamic_cast<MTControlsAudioProcessor *>(mainWindow->getAudioProcessor()) : nullptr)
      p->setSectionEntryCacheBudget(p->getSectionEntryCacheBudget() / 2);
  }

  bool backButtonPressed() override {
    auto *processor = mainWindow != nullptr ? mainWindow->getAudioProcessor() : nullptr;
    auto *editor = processor != nullptr
        ? dynamic_cast<MTControlsAudioProcessorEditor *>(processor->getActiveEditor()) : nullptr;
    return editor != nullptr && editor->navigateBackInSettings();
  }

  void initialise(const juce::String &commandLine) override {
#if JUCE_IOS || JUCE_ANDROID
    // Set the launch mask before JUCE creates its window/controller. iPhone
    // starts with portrait authentication; the editor restores landscape when
    // a saved or new session opens performance. iPad follows the orientation
    // and size of its native scene instead of using a fixed landscape canvas.
    const bool startsWithPhoneLogin =
#if JUCE_IOS
        juce::SystemStats::getDeviceDescription().containsIgnoreCase("iPhone");
#else
        false;
#endif
    int launchOrientations = juce::Desktop::rotatedClockwise |
                             juce::Desktop::rotatedAntiClockwise;
#if JUCE_IOS
    launchOrientations = startsWithPhoneLogin
                             ? juce::Desktop::upright
                             : launchOrientations | juce::Desktop::upright |
                                   juce::Desktop::upsideDown;
#endif
    juce::Desktop::getInstance().setOrientationsEnabled(launchOrientations);
#endif
    processor = std::make_unique<MTControlsAudioProcessor>();
    audioRuntime = std::make_unique<StandaloneAudioRuntime>(
        *processor, processor->getAudioDeviceBridge(), appProperties.getUserSettings());
    const auto audioError = audioRuntime->start(MTControlsAudioProcessor::maxStemCount);
    if (audioError.isNotEmpty())
      juce::Logger::writeToLog("Audio device: " + audioError);
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
      return;
    mainWindow = std::make_unique<PlayWorshipStandaloneWindow>(
        *processor, appProperties.getUserSettings());

#if JUCE_IOS
    mainWindow->setFullScreen(true);
#elif !JUCE_ANDROID
    // Desktop opens as a regular, resizable window filling the available
    // work area, keeping the native title bar, menu bar and taskbar visible.
    mainWindow->setFullScreen(false);
    expandTopLevelWindowToPrimaryWorkArea(mainWindow.get());
#endif
    mainWindow->setVisible(true);
#if JUCE_IOS
    juce::MessageManager::callAsync(
        [window = juce::Component::SafePointer<PlayWorshipStandaloneWindow>(mainWindow.get())] {
          if (window != nullptr)
            synchroniseIOSFullscreenWindow(*window);
        });
#endif
    queueProjectOpen(mainWindow.get(),
                     findInitialProjectArgument(commandLine));
  }

  void anotherInstanceStarted(const juce::String &commandLine) override {
    if (mainWindow != nullptr) {
      mainWindow->setMinimised(false);
      mainWindow->setVisible(true);
      mainWindow->toFront(true);
    }
    queueProjectOpen(mainWindow.get(), findProjectArgument(commandLine));
  }

  void resumed() override {
#if JUCE_IOS
    if (mainWindow != nullptr)
      synchroniseIOSFullscreenWindow(*mainWindow);
#endif
  }

  void shutdown() override {
    if (audioRuntime != nullptr) {
      audioRuntime->saveState();
      audioRuntime->stop();
    }
    mainWindow = nullptr;
    audioRuntime = nullptr;
    processor = nullptr;
    appProperties.saveIfNeeded();
  }

  void systemRequestedQuit() override {
    if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
      juce::Timer::callAfterDelay(100, [] {
        if (auto *app = juce::JUCEApplicationBase::getInstance())
          app->systemRequestedQuit();
      });
      return;
    }

    if (quitFlowActive)
      return;

    auto *processor = mainWindow != nullptr ? mainWindow->getAudioProcessor()
                                           : nullptr;
    auto *editor = processor != nullptr
                       ? dynamic_cast<MTControlsAudioProcessorEditor *>(
                             processor->getActiveEditor())
                       : nullptr;
    if (editor == nullptr) {
      completeQuit();
      return;
    }

    quitFlowActive = true;
    editor->requestApplicationQuit([this](const bool shouldQuit) {
      quitFlowActive = false;
      if (shouldQuit)
        completeQuit();
    });
  }

private:
  void completeQuit() {
    if (audioRuntime != nullptr)
      audioRuntime->saveState();
    quit();
  }

  juce::ApplicationProperties appProperties;
  std::unique_ptr<MTControlsAudioProcessor> processor;
  std::unique_ptr<StandaloneAudioRuntime> audioRuntime;
  std::unique_ptr<PlayWorshipStandaloneWindow> mainWindow;
  bool quitFlowActive = false;
};

} // namespace

START_JUCE_APPLICATION(PlayWorshipStandaloneApp)
