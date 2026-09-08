#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <JuceHeader.h>

#if JUCE_ANDROID
#include <android/log.h>
#include <jni.h>
#define PW_ANDROID_LOG(message) __android_log_print (ANDROID_LOG_INFO, "PlayWorship", "%s", message)
#else
#define PW_ANDROID_LOG(message) do {} while (false)
#endif

#if JUCE_ANDROID
namespace juce
{
void juce_juceEventsAndroidStartApp();
}

extern "C" JNIEXPORT void JNICALL Java_com_rmsl_juce_JuceActivity_startJUCEApplication (JNIEnv*, jobject)
{
    // JUCE normally starts from the Application lifecycle callback. Starting
    // here as well guarantees that the callback is registered before this
    // activity reaches onResume on current Android releases.
    juce::juce_juceEventsAndroidStartApp();
}
#endif

class PlayWorshipAndroidApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "PlayWorship"; }
    const juce::String getApplicationVersion() override { return "1.1.4"; }

    void initialise (const juce::String&) override
    {
        // Android tablets report their full density-independent area to JUCE.
        // A small additional scale keeps text and touch targets comfortable
        // without changing the desktop or iOS layouts.
        juce::Desktop::getInstance().setGlobalScaleFactor (1.12f);
        PW_ANDROID_LOG ("initialise: creating processor");
        processor = std::make_unique<MTControlsAudioProcessor>();
        PW_ANDROID_LOG ("initialise: creating main window");
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *processor);
        PW_ANDROID_LOG ("initialise: completed");
    }

    void shutdown() override
    {
        mainWindow.reset();
        processor.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
#if JUCE_ANDROID
        class AndroidRootComponent;
#endif
    public:
        MainWindow (const juce::String& name, MTControlsAudioProcessor& processor)
            : DocumentWindow (name, juce::Colours::black, DocumentWindow::allButtons),
              audioProcessor (&processor)
        {
            auto& audioManager = processor.getAudioDeviceBridge().getAudioDeviceManager();
            audioPlayer.setProcessor (&processor);
            audioManager.addAudioCallback (&audioPlayer);
            setUsingNativeTitleBar (true);
#if JUCE_ANDROID
            setFullScreen (true);
            juce::Desktop::getInstance().setKioskModeComponent (this);
#else
            setFullScreen (true);
#endif
#if JUCE_ANDROID
            setContentOwned (new AndroidRootComponent (processor.createEditor()), true);
#else
            setContentOwned (processor.createEditor(), true);
#endif
#if JUCE_ANDROID
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                setBounds (display->userArea);
#endif
            setVisible (true);
            PW_ANDROID_LOG ("main window visible");
        }

        ~MainWindow() override
        {
            if (audioProcessor != nullptr)
            {
                auto& audioManager = audioProcessor->getAudioDeviceBridge().getAudioDeviceManager();
                audioManager.removeAudioCallback (&audioPlayer);
            }

            audioPlayer.setProcessor (nullptr);
        }

#if JUCE_ANDROID
        void parentSizeChanged() override
        {
            if (auto* content = getContentComponent())
                content->resized();
        }
#endif

        void closeButtonPressed() override
        {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                app->systemRequestedQuit();
        }

#if JUCE_ANDROID
    private:
        MTControlsAudioProcessor* audioProcessor = nullptr;
        juce::AudioProcessorPlayer audioPlayer;

        class AndroidRootComponent final : public juce::Component
        {
        public:
            explicit AndroidRootComponent (juce::AudioProcessorEditor* editor)
                : child (editor)
            {
                addAndMakeVisible (child.get());
            }

            void paint (juce::Graphics& graphics) override
            {
                graphics.fillAll (juce::Colour (0xff101820));
            }

            void resized() override
            {
                child->setBounds (getLocalBounds());
            }

        private:
            std::unique_ptr<juce::AudioProcessorEditor> child;
        };
#endif
    };

    std::unique_ptr<MTControlsAudioProcessor> processor;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (PlayWorshipAndroidApplication)
