cmake_minimum_required(VERSION 3.22)

set(MTCONTROLS_APP_VERSION "0.1.0" CACHE STRING
    "Semantic version used for build metadata (MAJOR.MINOR.PATCH)")

if(DEFINED ENV{MTCONTROLS_APP_VERSION} AND NOT "$ENV{MTCONTROLS_APP_VERSION}" STREQUAL "")
    set(MTCONTROLS_APP_VERSION "$ENV{MTCONTROLS_APP_VERSION}")
endif()

if(NOT MTCONTROLS_APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "MTCONTROLS_APP_VERSION must match MAJOR.MINOR.PATCH, got: ${MTCONTROLS_APP_VERSION}")
endif()

set(APP_TARGET_NAME PlayWorship)
set(APP_ASSETS_TARGET PlayWorshipAssets)
set(APP_STANDALONE_TARGET ${APP_TARGET_NAME}_Standalone)

project(${APP_TARGET_NAME} VERSION ${MTCONTROLS_APP_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(MTCONTROLS_FFMPEG_BIN_MACOS "" CACHE FILEPATH
    "Path to ffmpeg binary that will be bundled in the macOS app")
set(MTCONTROLS_FFMPEG_BIN_WINDOWS "" CACHE FILEPATH
    "Path to ffmpeg.exe that will be bundled in the Windows build")
set(MTCONTROLS_API_BASE_URL "https://api.playworship.com" CACHE STRING
    "Base URL for PlayWorship backend/login frontend")
set(MTCONTROLS_SESSION_REAUTH_MINUTES "7200" CACHE STRING
    "Minutes before forcing re-login in app (default: 5 days)")
set(MTCONTROLS_REAUTH_GRACE_MINUTES "300" CACHE STRING
    "Minutes of grace window after re-login is requested (default: 5 hours)")

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

juce_add_plugin(${APP_TARGET_NAME}
    COMPANY_NAME "Multitracks"
    VERSION "${MTCONTROLS_APP_VERSION}"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
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
)

juce_generate_juce_header(${APP_TARGET_NAME})

juce_add_binary_data(${APP_ASSETS_TARGET}
    SOURCES
        assets/logoPlayWorship.jpeg
)

target_sources(${APP_TARGET_NAME}
    PRIVATE
        src/PluginEditor.cpp
        src/PluginEditor.h
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
        src/ui/components/StereoVUMeter.cpp
        src/ui/components/StereoVUMeter.h
        src/ui/components/WaveformRowComponent.cpp
        src/ui/components/WaveformRowComponent.h
        src/ui/components/MetronomePanel.cpp
        src/ui/components/MetronomePanel.h
        src/ui/panels/ChannelGridPanel.cpp
        src/ui/panels/ChannelGridPanel.h
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
)

target_compile_definitions(${APP_TARGET_NAME}
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        MTCONTROLS_API_BASE_URL=\"${MTCONTROLS_API_BASE_URL}\"
        MTCONTROLS_SESSION_REAUTH_MINUTES=${MTCONTROLS_SESSION_REAUTH_MINUTES}
        MTCONTROLS_REAUTH_GRACE_MINUTES=${MTCONTROLS_REAUTH_GRACE_MINUTES}
        SOUNDTOUCH_FLOAT_SAMPLES=1
)

target_link_libraries(${APP_TARGET_NAME}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ${APP_ASSETS_TARGET}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if (TARGET SoundTouch)
    target_compile_definitions(SoundTouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)
    target_link_libraries(${APP_TARGET_NAME} PRIVATE SoundTouch)
    target_include_directories(${APP_TARGET_NAME} PRIVATE ${soundtouch_SOURCE_DIR}/include)
elseif (TARGET soundtouch)
    target_compile_definitions(soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)
    target_link_libraries(${APP_TARGET_NAME} PRIVATE soundtouch)
    target_include_directories(${APP_TARGET_NAME} PRIVATE ${soundtouch_SOURCE_DIR}/include)
else()
    message(FATAL_ERROR "SoundTouch target not found after FetchContent.")
endif()

if (TARGET ${APP_STANDALONE_TARGET})
    if (APPLE AND MTCONTROLS_FFMPEG_BIN_MACOS)
        add_custom_command(TARGET ${APP_STANDALONE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:${APP_STANDALONE_TARGET}>/Resources/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_FFMPEG_BIN_MACOS}"
                "$<TARGET_BUNDLE_CONTENT_DIR:${APP_STANDALONE_TARGET}>/Resources/ffmpeg/ffmpeg"
            VERBATIM)
    endif()

    if (WIN32 AND MTCONTROLS_FFMPEG_BIN_WINDOWS)
        add_custom_command(TARGET ${APP_STANDALONE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${APP_STANDALONE_TARGET}>/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_FFMPEG_BIN_WINDOWS}"
                "$<TARGET_FILE_DIR:${APP_STANDALONE_TARGET}>/ffmpeg/ffmpeg.exe"
            VERBATIM)
    endif()
endif()
