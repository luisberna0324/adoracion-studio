#include "PluginEditor.h"
#include "data/ProjectStore.h"

#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

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
  return juce::String::fromUTF8(JucePlugin_Name) + " v" + JucePlugin_VersionString;
}

class PlayWorshipStandaloneWindow final
    : public juce::StandaloneFilterWindow {
public:
  using juce::StandaloneFilterWindow::StandaloneFilterWindow;

#if JUCE_IOS
  void parentSizeChanged() override {
    juce::StandaloneFilterWindow::parentSizeChanged();
    // The scene can rotate while the editor is still being constructed. Keep
    // the native peer fullscreen after its first real display size arrives.
    if (auto *peer = getPeer())
      peer->setFullScreen(true);
    juce::StandaloneFilterWindow::resized();
  }
#endif

  void closeButtonPressed() override {
    if (auto *app = juce::JUCEApplicationBase::getInstance())
      app->systemRequestedQuit();
  }
};

void queueProjectOpen(juce::StandaloneFilterWindow *window,
                      const juce::File &projectFile) {
  if (window == nullptr || projectFile == juce::File{})
    return;

  juce::MessageManager::callAsync(
      [safeWindow =
           juce::Component::SafePointer<juce::StandaloneFilterWindow>(window),
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
    options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
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
    return juce::CharPointer_UTF8(JucePlugin_Name);
  }
  const juce::String getApplicationVersion() override {
    return JucePlugin_VersionString;
  }
  bool moreThanOneInstanceAllowed() override { return false; }

  void initialise(const juce::String &commandLine) override {
#if JUCE_IOS || JUCE_ANDROID
    // Set the launch mask before JUCE creates its window/controller. Waiting
    // until the editor constructor lets iOS start the scene in portrait.
    juce::Desktop::getInstance().setOrientationsEnabled(
        juce::Desktop::rotatedClockwise | juce::Desktop::rotatedAntiClockwise);
#endif
    mainWindow.reset(createWindow());
    if (mainWindow == nullptr) {
      pluginHolder = createPluginHolder();
      return;
    }

#if JUCE_IOS || JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX || JUCE_BSD
    mainWindow->setFullScreen(true);
#endif
    mainWindow->setVisible(true);
#if JUCE_IOS
    juce::MessageManager::callAsync(
        [window = juce::Component::SafePointer<juce::StandaloneFilterWindow>(mainWindow.get())] {
          if (window != nullptr)
            if (auto *peer = window->getPeer())
              peer->setFullScreen(true);
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

  void shutdown() override {
    pluginHolder = nullptr;
    mainWindow = nullptr;
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
    if (pluginHolder != nullptr)
      pluginHolder->savePluginState();
    if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr)
      mainWindow->pluginHolder->savePluginState();
    quit();
  }

  juce::StandaloneFilterWindow *createWindow() {
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
      return nullptr;
    return new PlayWorshipStandaloneWindow(
        makeWindowTitle(),
        juce::LookAndFeel::getDefaultLookAndFeel().findColour(
            juce::ResizableWindow::backgroundColourId),
        createPluginHolder());
  }

  std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder() {
#ifdef JucePlugin_PreferredChannelConfigurations
    constexpr juce::StandalonePluginHolder::PluginInOuts channels[]{
        JucePlugin_PreferredChannelConfigurations};
    const juce::Array<juce::StandalonePluginHolder::PluginInOuts>
        channelConfiguration(channels, juce::numElementsInArray(channels));
#else
    const juce::Array<juce::StandalonePluginHolder::PluginInOuts>
        channelConfiguration;
#endif
    return std::make_unique<juce::StandalonePluginHolder>(
        appProperties.getUserSettings(), false, juce::String{}, nullptr,
        channelConfiguration, false);
  }

  juce::ApplicationProperties appProperties;
  std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
  std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
  bool quitFlowActive = false;
};

} // namespace

START_JUCE_APPLICATION(PlayWorshipStandaloneApp)
