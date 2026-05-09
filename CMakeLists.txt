cmake_minimum_required(VERSION 3.22)

set(MTCONTROLS_APP_VERSION "0.1.0" CACHE STRING
    "Semantic version used for build metadata (MAJOR.MINOR.PATCH)")

if(DEFINED ENV{MTCONTROLS_APP_VERSION} AND NOT "$ENV{MTCONTROLS_APP_VERSION}" STREQUAL "")
    set(MTCONTROLS_APP_VERSION "$ENV{MTCONTROLS_APP_VERSION}")
endif()

if(NOT MTCONTROLS_APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "MTCONTROLS_APP_VERSION must match MAJOR.MINOR.PATCH, got: ${MTCONTROLS_APP_VERSION}")
endif()

project(MTControls VERSION ${MTCONTROLS_APP_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(_mtcontrols_ffmpeg_macos_default "")
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/macos/ffmpeg")
    set(_mtcontrols_ffmpeg_macos_default
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/macos/ffmpeg")
endif()
set(MTCONTROLS_FFMPEG_BIN_MACOS "${_mtcontrols_ffmpeg_macos_default}" CACHE FILEPATH
    "Path to ffmpeg binary that will be bundled in the macOS app")
set(_mtcontrols_ffmpeg_windows_default "")
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/windows/ffmpeg.exe")
    set(_mtcontrols_ffmpeg_windows_default
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/windows/ffmpeg.exe")
endif()
set(MTCONTROLS_FFMPEG_BIN_WINDOWS "${_mtcontrols_ffmpeg_windows_default}" CACHE FILEPATH
    "Path to ffmpeg.exe that will be bundled in the Windows build")
set(_mtcontrols_ffmpeg_linux_default "")
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/linux/ffmpeg")
    set(_mtcontrols_ffmpeg_linux_default
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/linux/ffmpeg")
endif()
set(MTCONTROLS_FFMPEG_BIN_LINUX "${_mtcontrols_ffmpeg_linux_default}" CACHE FILEPATH
    "Path to ffmpeg binary that will be bundled in the Linux build")
set(MTCONTROLS_ASSETS_BASE_URL "https://storage.googleapis.com/playworship-assets" CACHE STRING
    "Base URL for remote asset bootstrap downloads")
option(MTCONTROLS_BUNDLE_FFMPEG "Bundle ffmpeg inside build/install artifacts" OFF)
set(MTCONTROLS_API_BASE_URL "https://playworship-345830648064.us-east1.run.app" CACHE STRING
    "Base URL for PlayWorship backend/login frontend")
set(MTCONTROLS_SESSION_REAUTH_MINUTES "7200" CACHE STRING
    "Minutes before forcing re-login in app (default: 5 days)")
set(MTCONTROLS_REAUTH_GRACE_MINUTES "300" CACHE STRING
    "Minutes of grace window after re-login is requested (default: 5 hours)")
set(MTCONTROLS_MAX_OFFLINE_HOURS "168" CACHE STRING
    "Max hours without network after last online validation (default: 7 days)")
set(MTCONTROLS_IOS_TARGETED_DEVICE_FAMILY "2" CACHE STRING
    "Xcode TARGETED_DEVICE_FAMILY for iOS: 2=iPad only, 1=iPhone only, 1,2=Universal")

include(FetchContent)

if(NOT DEFINED JUCE_DIR OR JUCE_DIR STREQUAL "")
    message(FATAL_ERROR "JUCE_DIR is not set. Configure with: cmake -S . -B build -DJUCE_DIR=/path/to/JUCE")
endif()

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE_DIR does not point to a JUCE checkout: ${JUCE_DIR}")
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

FetchContent_Declare(
    soundtouch
    URL https://gitlab.com/soundtouch/soundtouch/-/archive/2.3.1/soundtouch-2.3.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(soundtouch)

if (TARGET soundstretch)
    set_target_properties(soundstretch PROPERTIES 
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.multitracks.playworship.soundstretch"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        MACOSX_BUNDLE OFF
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
endif()

juce_add_plugin(MTControls
    COMPANY_NAME "Multitracks"
    VERSION "${MTCONTROLS_APP_VERSION}"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE MLTK
    PLUGIN_CODE Mtc1
    FORMATS Standalone
    PRODUCT_NAME "PlayWorship"
    ICON_BIG "assets/icons/app-icon-1024.png"
    ICON_SMALL "assets/icons/app-icon-1024.png"
    BUNDLE_ID "com.multitracks.playworship"
    BACKGROUND_AUDIO_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE
    # iOS: committed asset catalog includes LaunchImage.launchimage so JUCE omits LaunchScreen.storyboard.
    # The default storyboard leaves LaunchScreen.storyboardc unsigned; device install then fails (invalid bundle).
    CUSTOM_XCASSETS_FOLDER "assets/ios/Images.xcassets"
)

# iOS / iPadOS: device family (JUCE reads this on MTControls for the standalone .app).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(MTControls PROPERTIES
        JUCE_TARGETED_DEVICE_FAMILY "${MTCONTROLS_IOS_TARGETED_DEVICE_FAMILY}")
endif()

juce_generate_juce_header(MTControls)

juce_add_binary_data(MTControlsAssets
    SOURCES
        assets/logoPlayWorship.jpeg
        assets/pwLogoSinFondo.png
        assets/fonts/InterRegular.ttf
        assets/fonts/InterMedium.ttf
        assets/fonts/InterSemiBold.ttf
        assets/fonts/InterBold.ttf
        assets/ui-icons/skip_back.svg
        assets/ui-icons/rewind.svg
        assets/ui-icons/play.svg
        assets/ui-icons/pause.svg
        assets/ui-icons/square.svg
        assets/ui-icons/fast_forward.svg
        assets/ui-icons/skip_forward.svg
        assets/ui-icons/fade_in.svg
        assets/ui-icons/fade_out.svg
        assets/ui-icons/settings.svg
        assets/ui-icons/search.svg
        assets/ui-icons/plus.svg
        assets/ui-icons/check.svg
        assets/ui-icons/minus.svg
        assets/ui-icons/more_vertical.svg
        assets/ui-icons/clock.svg
        assets/ui-icons/music.svg
        assets/ui-icons/midi.svg
        assets/ui-icons/audio_waveform.svg
        assets/ui-icons/metronome.svg
        assets/ui-icons/maximize.svg
        assets/ui-icons/copy.svg
        assets/ui-icons/repeat.svg
        assets/ui-icons/corner_down_right.svg
)

target_sources(MTControls
    PRIVATE
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/PluginEditorSupport.cpp
        src/PluginEditorSupport.h
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/MetronomeEngine.cpp
        src/MetronomeEngine.h
        src/PitchShiftProcessor.cpp
        src/PitchShiftProcessor.h
        src/StemMixEngine.cpp
        src/StemMixEngine.h
        src/ResamplingPositionableSource.cpp
        src/ResamplingPositionableSource.h
        src/StemTrack.cpp
        src/StemTrack.h
        src/StemBufferingSource.cpp
        src/StemBufferingSource.h
        src/audio/AudioDeviceMonitor.cpp
        src/audio/AudioDeviceMonitor.h
        src/audio/OutputChannelCatalog.cpp
        src/audio/OutputChannelCatalog.h
        src/audio/PadsEngine.cpp
        src/audio/PadsEngine.h
        src/audio/RoutingSuggestionEngine.cpp
        src/audio/RoutingSuggestionEngine.h
        src/audio/SimpleDelayLine.cpp
        src/audio/SimpleDelayLine.h
        src/audio/StandaloneAudioDeviceBridge.cpp
        src/audio/StandaloneAudioDeviceBridge.h
        src/audio/StemOutputRouter.cpp
        src/audio/StemOutputRouter.h
        src/audio/StemOutputRoutingState.cpp
        src/audio/StemOutputRoutingState.h
        src/data/AppSettings.cpp
        src/data/AppSettings.h
        src/data/AssetBootstrapManager.cpp
        src/data/AssetBootstrapManager.h
        src/data/AuthSessionApi.cpp
        src/data/AuthSessionApi.h
        src/data/LocalAuthCallbackServer.cpp
        src/data/LocalAuthCallbackServer.h
        src/data/SessionAuthLogger.cpp
        src/data/SessionAuthLogger.h
        src/data/SessionPersistence.cpp
        src/data/SessionPersistence.h
        src/data/Localization.h
        src/data/PrimeLibrarySource.cpp
        src/data/PrimeLibrarySource.h
        src/data/SongData.cpp
        src/data/SongData.h
        src/data/SongLibraryStore.cpp
        src/data/SongLibraryStore.h
        src/ui/DeviceNotificationBanner.cpp
        src/ui/DeviceNotificationBanner.h
        src/ui/UiCommon.cpp
        src/ui/UiCommon.h
        src/ui/components/ChannelStripCard.cpp
        src/ui/components/ChannelStripCard.h
        src/ui/components/ChannelStripLookAndFeel.cpp
        src/ui/components/ChannelStripLookAndFeel.h
        src/ui/components/SetlistHandlePanel.cpp
        src/ui/components/SetlistHandlePanel.h
        src/ui/components/SongLoadProgressOverlay.cpp
        src/ui/components/SongLoadProgressOverlay.h
        src/ui/components/StereoVUMeter.cpp
        src/ui/components/StereoVUMeter.h
        src/ui/components/SearchField.cpp
        src/ui/components/SearchField.h
        src/ui/components/SetlistSongsHeaderPanel.cpp
        src/ui/components/SetlistSongsHeaderPanel.h
        src/ui/components/WorkspaceTabsBar.cpp
        src/ui/components/WorkspaceTabsBar.h
        src/ui/components/WaveformRowComponent.cpp
        src/ui/components/WaveformRowComponent.h
        src/ui/components/MetronomePanel.cpp
        src/ui/components/MetronomePanel.h
        src/ui/panels/ChannelGridPanel.cpp
        src/ui/panels/ChannelGridPanel.h
        src/ui/panels/LibraryCatalogPanel.cpp
        src/ui/panels/LibraryCatalogPanel.h
        src/ui/panels/MixerFaderPanel.cpp
        src/ui/panels/MixerFaderPanel.h
        src/ui/panels/SettingsPanel.cpp
        src/ui/panels/SettingsPanel.h
        src/ui/panels/SongImportWizardComponent.cpp
        src/ui/panels/SongImportWizardComponent.h
        src/ui/panels/SongListPanel.cpp
        src/ui/panels/SongListPanel.h
        src/ui/panels/TopBar.cpp
        src/ui/panels/TopBar.h
        src/ui/panels/WaveformBar.cpp
        src/ui/panels/WaveformBar.h
        src/ui/components/NotePadButton.cpp
        src/ui/components/NotePadButton.h
        src/ui/components/OctaveControl.cpp
        src/ui/components/OctaveControl.h
        src/ui/components/PowerButton.cpp
        src/ui/components/PowerButton.h
        src/ui/components/PadsHandlePanel.cpp
        src/ui/components/PadsHandlePanel.h
        src/ui/components/ReauthPromptModal.cpp
        src/ui/components/ReauthPromptModal.h
        src/ui/panels/PadsComponent.cpp
        src/ui/panels/PadsComponent.h
        src/ui/components/WelcomeModal.cpp
        src/ui/components/WelcomeModal.h
)

target_compile_definitions(MTControls
    PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        MTCONTROLS_API_BASE_URL=\"${MTCONTROLS_API_BASE_URL}\"
        MTCONTROLS_ASSETS_BASE_URL=\"${MTCONTROLS_ASSETS_BASE_URL}\"
        MTCONTROLS_SESSION_REAUTH_MINUTES=${MTCONTROLS_SESSION_REAUTH_MINUTES}
        MTCONTROLS_REAUTH_GRACE_MINUTES=${MTCONTROLS_REAUTH_GRACE_MINUTES}
        MTCONTROLS_MAX_OFFLINE_HOURS=${MTCONTROLS_MAX_OFFLINE_HOURS}
        SOUNDTOUCH_FLOAT_SAMPLES=1
)

if (WIN32)
    target_compile_definitions(MTControls
        PRIVATE
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
            # DirectWrite color glyph APIs used by JUCE require at least Windows 10 RS3 SDK surface.
            NTDDI_VERSION=0x0A000004
    )
endif()

target_link_libraries(MTControls
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        MTControlsAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if (TARGET SoundTouch)
    target_compile_definitions(SoundTouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)
    target_link_libraries(MTControls PRIVATE SoundTouch)
    target_include_directories(MTControls PRIVATE ${soundtouch_SOURCE_DIR}/include)
elseif (TARGET soundtouch)
    target_compile_definitions(soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)
    target_link_libraries(MTControls PRIVATE soundtouch)
    target_include_directories(MTControls PRIVATE ${soundtouch_SOURCE_DIR}/include)
else()
    message(FATAL_ERROR "SoundTouch target not found after FetchContent.")
endif()

if (TARGET MTControls_Standalone AND MTCONTROLS_BUNDLE_FFMPEG)
    # APPLE is true for iOS too; bundling the macOS ffmpeg binary into an iOS app breaks CodeSign.
    if (APPLE AND MTCONTROLS_FFMPEG_BIN_MACOS)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_FFMPEG_BIN_MACOS}"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/ffmpeg/ffmpeg"
            COMMAND chmod +x "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/ffmpeg/ffmpeg"
            VERBATIM)
    endif()

    if (WIN32 AND MTCONTROLS_FFMPEG_BIN_WINDOWS)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_FFMPEG_BIN_WINDOWS}"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ffmpeg/ffmpeg.exe"
            VERBATIM)
    endif()

    if (UNIX AND NOT APPLE AND MTCONTROLS_FFMPEG_BIN_LINUX)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_FFMPEG_BIN_LINUX}"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ffmpeg/ffmpeg"
            COMMAND chmod +x "$<TARGET_FILE_DIR:MTControls_Standalone>/ffmpeg/ffmpeg"
            VERBATIM)
    endif()
endif()
