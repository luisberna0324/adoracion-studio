#include "AppIdentity.h"
#include "audio/StandaloneAudioRuntime.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <JuceHeader.h>

#if JUCE_ANDROID
#include <android/log.h>
#include <jni.h>
#include <juce_opengl/juce_opengl.h>
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
    const juce::String getApplicationName() override { return playworship::appName; }
    const juce::String getApplicationVersion() override { return playworship::appVersion; }

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
              audioProcessor (&processor),
              audioRuntime (processor, processor.getAudioDeviceBridge())
        {
            const auto audioError = audioRuntime.start(MTControlsAudioProcessor::maxStemCount);
            if (audioError.isNotEmpty())
                PW_ANDROID_LOG(audioError.toRawUTF8());
            setUsingNativeTitleBar (true);
#if JUCE_ANDROID
            // The window owns the size; content must not resize the native peer.
            setContentOwned (new AndroidRootComponent (processor.createEditor()), false);
#else
            setContentOwned (processor.createEditor(), true);
#endif
#if JUCE_ANDROID
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                setBounds (display->userArea);
#endif
            // Enter fullscreen AFTER all explicit bounds changes. Component's
            // updateBounds() passes fullscreen=false to the Android peer, which
            // otherwise leaves a fixed landscape-sized view after rotation.
            setFullScreen (true);
#if JUCE_ANDROID
            juce::Desktop::getInstance().setKioskModeComponent (this);
#endif
            setVisible (true);
            PW_ANDROID_LOG ("main window visible");
        }

        ~MainWindow() override
        {
            audioRuntime.stop();

        }

#if JUCE_ANDROID
        void parentSizeChanged() override
        {
            juce::DocumentWindow::parentSizeChanged();
            if (auto* peer = getPeer())
                peer->setFullScreen (true);
            // The native MATCH_PARENT view reports its new size to the peer;
            // DocumentWindow then resizes root, editor, and the GL surface.
            auto* content = getContentComponent();
            const auto previousBounds = content != nullptr ? content->getBounds()
                                                           : juce::Rectangle<int>();
            juce::DocumentWindow::resized();
            // Hiding system bars changes the safe area without necessarily
            // changing the fullscreen window size. Propagate that update too.
            if (content != nullptr && content->getBounds() == previousBounds)
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
        StandaloneAudioRuntime audioRuntime;

        class AndroidRootComponent final : public juce::Component
        {
        public:
            explicit AndroidRootComponent (juce::AudioProcessorEditor* editor)
                : child (editor)
            {
                setOpaque (true);
                addAndMakeVisible (child.get());
                // The Android software peer repaints and uploads the whole
                // window even for small meter/playhead invalidations. JUCE's
                // GL component renderer retains the surface and honours dirty
                // regions, leaving the message thread available for touch.
                openGLContext.setContinuousRepainting (false);
                openGLContext.setImageCacheSize (32 * 1024 * 1024);
                openGLContext.attachTo (*this);
            }

            ~AndroidRootComponent() override
            {
                // Stop rendering before the editor and its children disappear.
                openGLContext.detach();
            }

            void paint (juce::Graphics& graphics) override
            {
                graphics.fillAll (juce::Colour (0xff101820));
            }

            void resized() override
            {
                const auto bounds = getLocalBounds();
                if (child->getBounds() != bounds)
                    child->setBounds (bounds);
                else
                    child->resized();
            }

        private:
            std::unique_ptr<juce::AudioProcessorEditor> child;
            juce::OpenGLContext openGLContext;
        };
#endif
    };

    std::unique_ptr<MTControlsAudioProcessor> processor;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (PlayWorshipAndroidApplication)
