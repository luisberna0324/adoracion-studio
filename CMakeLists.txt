cmake_minimum_required(VERSION 3.22)

set(MTCONTROLS_APP_VERSION "1.1.4" CACHE STRING
    "Semantic version used for build metadata (MAJOR.MINOR.PATCH)")
set(MTCONTROLS_IOS_BUILD_NUMBER "1" CACHE STRING
    "Monotonically increasing App Store build number")

if(DEFINED ENV{MTCONTROLS_APP_VERSION} AND NOT "$ENV{MTCONTROLS_APP_VERSION}" STREQUAL "")
    set(MTCONTROLS_APP_VERSION "$ENV{MTCONTROLS_APP_VERSION}")
endif()

if(NOT MTCONTROLS_APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "MTCONTROLS_APP_VERSION must match MAJOR.MINOR.PATCH, got: ${MTCONTROLS_APP_VERSION}")
endif()
if(NOT MTCONTROLS_IOS_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "MTCONTROLS_IOS_BUILD_NUMBER must be a positive integer, got: ${MTCONTROLS_IOS_BUILD_NUMBER}")
endif()

project(MTControls VERSION ${MTCONTROLS_APP_VERSION})

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
option(MTCONTROLS_BUILD_TESTS "Build lightweight audio engine core tests" OFF)
option(MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS
    "Log aggregate VU meter repaint counters every two seconds" OFF)
option(MTCONTROLS_ENABLE_SENTRY
    "Enable Sentry Native telemetry (requires SENTRY_NATIVE_DIR and SENTRY_DSN)" OFF)
set(SENTRY_NATIVE_DIR "" CACHE PATH
    "Path to a checked-out sentry-native SDK; never fetched automatically")
set(MTCONTROLS_SENTRY_DSN "$ENV{SENTRY_DSN}" CACHE STRING
    "Sentry DSN supplied by CI/environment; do not commit a value")
set(MTCONTROLS_SENTRY_ENVIRONMENT "$ENV{SENTRY_ENVIRONMENT}" CACHE STRING
    "Sentry environment: development, staging, or production")
if(MTCONTROLS_SENTRY_ENVIRONMENT STREQUAL "")
    set(MTCONTROLS_SENTRY_ENVIRONMENT "development")
endif()
set(MTCONTROLS_GIT_COMMIT "$ENV{GITHUB_SHA}" CACHE STRING
    "Commit SHA supplied by CI for the Sentry release")
if(MTCONTROLS_GIT_COMMIT STREQUAL "")
    execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE MTCONTROLS_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
option(MTCONTROLS_USE_BUNGEE
    "Use Bungee Basic as the pitch/tempo backend" ON)

# The distributed Bungee iOS framework currently contains device arm64 only.
# Allow the simulator to use the existing software fallback so UI builds can
# run in Xcode without linking a device-only binary.
set(_mtcontrols_ios_simulator OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND
   CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
    set(_mtcontrols_ios_simulator ON)
    set(MTCONTROLS_USE_BUNGEE OFF CACHE BOOL
        "Use Bungee Basic as the pitch/tempo backend" FORCE)
    message(STATUS
        "Audio backend: PitchShiftProcessor fallback (iOS simulator; Bungee device framework unavailable)")
endif()
set(MTCONTROLS_BUNGEE_DIR "${CMAKE_SOURCE_DIR}/third_party/bungee" CACHE PATH
    "Path to the unpacked Bungee SDK")
if(MTCONTROLS_BUNGEE_DIR STREQUAL "")
    set(MTCONTROLS_BUNGEE_DIR "${CMAKE_SOURCE_DIR}/third_party/bungee"
        CACHE PATH "Path to the unpacked Bungee SDK" FORCE)
endif()

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
set(_mtcontrols_guides_pack_default "")
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/guides/guides-en-es.zip")
    set(_mtcontrols_guides_pack_default
        "${CMAKE_SOURCE_DIR}/assets/guides/guides-en-es.zip")
endif()
set(MTCONTROLS_GUIDES_PACK "${_mtcontrols_guides_pack_default}" CACHE FILEPATH
    "Path to bundled EN/ES dynamic guides pack zip (guides-en-es.zip)")
if((NOT MTCONTROLS_GUIDES_PACK OR MTCONTROLS_GUIDES_PACK STREQUAL "") AND
   EXISTS "${CMAKE_SOURCE_DIR}/assets/guides/guides-en-es.zip")
    set(MTCONTROLS_GUIDES_PACK
        "${CMAKE_SOURCE_DIR}/assets/guides/guides-en-es.zip"
        CACHE FILEPATH
        "Path to bundled EN/ES dynamic guides pack zip (guides-en-es.zip)"
        FORCE)
endif()
set(_mtcontrols_clicks_pack_default "")
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/clicks/clicks-pack.zip")
    set(_mtcontrols_clicks_pack_default
        "${CMAKE_SOURCE_DIR}/assets/clicks/clicks-pack.zip")
endif()
set(MTCONTROLS_CLICKS_PACK "${_mtcontrols_clicks_pack_default}" CACHE FILEPATH
    "Path to bundled click samples pack zip (clicks-pack.zip)")
if((NOT MTCONTROLS_CLICKS_PACK OR MTCONTROLS_CLICKS_PACK STREQUAL "") AND
   EXISTS "${CMAKE_SOURCE_DIR}/assets/clicks/clicks-pack.zip")
    set(MTCONTROLS_CLICKS_PACK
        "${CMAKE_SOURCE_DIR}/assets/clicks/clicks-pack.zip"
        CACHE FILEPATH
        "Path to bundled click samples pack zip (clicks-pack.zip)"
        FORCE)
endif()
option(MTCONTROLS_BUNDLE_FFMPEG "Bundle ffmpeg inside build/install artifacts" OFF)
set(MTCONTROLS_BACKEND_MODE "cloud" CACHE STRING
    "PlayWorship backend target: cloud, local, or custom")
set_property(CACHE MTCONTROLS_BACKEND_MODE PROPERTY STRINGS cloud local custom)
set(MTCONTROLS_CLOUD_API_BASE_URL "https://api.playworship.app" CACHE STRING
    "Stable public PlayWorship API URL used by the cloud backend mode")
set(MTCONTROLS_LOCAL_API_BASE_URL "http://127.0.0.1:3000" CACHE STRING
    "Local PlayWorship API URL used by the local backend mode")
set(MTCONTROLS_API_BASE_URL "" CACHE STRING
    "Custom PlayWorship API URL; required only when MTCONTROLS_BACKEND_MODE=custom")
if(MTCONTROLS_BACKEND_MODE STREQUAL "cloud")
    set(MTCONTROLS_RESOLVED_API_BASE_URL "${MTCONTROLS_CLOUD_API_BASE_URL}")
elseif(MTCONTROLS_BACKEND_MODE STREQUAL "local")
    set(MTCONTROLS_RESOLVED_API_BASE_URL "${MTCONTROLS_LOCAL_API_BASE_URL}")
elseif(MTCONTROLS_BACKEND_MODE STREQUAL "custom")
    if(MTCONTROLS_API_BASE_URL STREQUAL "")
        message(FATAL_ERROR
            "MTCONTROLS_API_BASE_URL must be set when MTCONTROLS_BACKEND_MODE=custom")
    endif()
    set(MTCONTROLS_RESOLVED_API_BASE_URL "${MTCONTROLS_API_BASE_URL}")
else()
    message(FATAL_ERROR
        "Unknown MTCONTROLS_BACKEND_MODE='${MTCONTROLS_BACKEND_MODE}'. Use cloud, local, or custom.")
endif()
message(STATUS "PlayWorship backend: ${MTCONTROLS_BACKEND_MODE} (${MTCONTROLS_RESOLVED_API_BASE_URL})")
set(MTCONTROLS_AUTH_BASE_URL "" CACHE STRING
    "Optional base URL for PlayWorship OAuth start endpoint; defaults to the resolved API backend URL")
# Migrate builds generated before the stable API domain existed. Custom and
# local OAuth endpoints remain untouched.
if(MTCONTROLS_AUTH_BASE_URL STREQUAL "https://playworship-py5yabecxq-ue.a.run.app" OR
   MTCONTROLS_AUTH_BASE_URL STREQUAL "https://playworship-345830648064.us-east1.run.app")
    set(MTCONTROLS_AUTH_BASE_URL "" CACHE STRING
        "Optional base URL for PlayWorship OAuth start endpoint; defaults to the resolved API backend URL"
        FORCE)
endif()
set(MTCONTROLS_SESSION_REAUTH_MINUTES "7200" CACHE STRING
    "Minutes before forcing re-login in app (default: 5 days)")
set(MTCONTROLS_REAUTH_GRACE_MINUTES "300" CACHE STRING
    "Minutes of grace window after re-login is requested (default: 5 hours)")
set(MTCONTROLS_MAX_OFFLINE_HOURS "168" CACHE STRING
    "Max hours without network after last online validation (default: 7 days)")
set(MTCONTROLS_IOS_TARGETED_DEVICE_FAMILY "1,2" CACHE STRING
    "Xcode TARGETED_DEVICE_FAMILY for iOS: 2=iPad only, 1=iPhone only, 1,2=Universal")
set(MTCONTROLS_IOS_DEVELOPMENT_TEAM "" CACHE STRING
    "Apple development team used to sign the iOS app on physical devices")

include(FetchContent)

set(MTCONTROLS_SENTRY_ENABLED OFF)
if(MTCONTROLS_ENABLE_SENTRY)
    if(MTCONTROLS_SENTRY_DSN STREQUAL "")
        message(FATAL_ERROR "MTCONTROLS_ENABLE_SENTRY requires SENTRY_DSN in CI/environment")
    endif()
    if(NOT EXISTS "${SENTRY_NATIVE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "MTCONTROLS_ENABLE_SENTRY requires SENTRY_NATIVE_DIR pointing to sentry-native")
    endif()
    # Build the SDK locally with its transport/crash backend chosen by the
    # platform. Keeping it external makes third-party updates explicit.
    add_subdirectory("${SENTRY_NATIVE_DIR}" sentry-native EXCLUDE_FROM_ALL)
    set(MTCONTROLS_SENTRY_ENABLED ON)
endif()

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

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(MTCONTROLS_USE_BUNGEE OFF CACHE BOOL
        "Use Bungee Basic as the pitch/tempo backend" FORCE)
    message(STATUS
        "Audio backend: SoundTouch legacy (Windows x86; Bungee runtime unavailable)")
elseif(NOT MTCONTROLS_USE_BUNGEE AND NOT _mtcontrols_ios_simulator)
    message(FATAL_ERROR
        "Bungee is mandatory on this platform. "
        "SoundTouch fallback is supported only for Windows x86.")
endif()

set(MTCONTROLS_BUNGEE_AVAILABLE OFF)
if(MTCONTROLS_USE_BUNGEE)
    if(NOT EXISTS "${MTCONTROLS_BUNGEE_DIR}/include/bungee/Bungee.h" OR
       NOT EXISTS "${MTCONTROLS_BUNGEE_DIR}/LICENSE" OR
       NOT EXISTS "${MTCONTROLS_BUNGEE_DIR}/version.txt")
        message(FATAL_ERROR
            "Bungee SDK is required. Run scripts/bootstrap_bungee.sh before configuring. "
            "Expected SDK at: ${MTCONTROLS_BUNGEE_DIR}")
    endif()
    file(READ "${MTCONTROLS_BUNGEE_DIR}/version.txt"
        MTCONTROLS_BUNGEE_VERSION)
    string(STRIP "${MTCONTROLS_BUNGEE_VERSION}"
        MTCONTROLS_BUNGEE_VERSION)
    string(REGEX REPLACE "^v" "" MTCONTROLS_BUNGEE_VERSION
        "${MTCONTROLS_BUNGEE_VERSION}")

    add_library(MTControlsBungee SHARED IMPORTED GLOBAL)
    target_include_directories(MTControlsBungee INTERFACE
        "${MTCONTROLS_BUNGEE_DIR}/include")

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(_mtcontrols_bungee_framework
            "${MTCONTROLS_BUNGEE_DIR}/apple-ios/bungee.framework")
        if(NOT EXISTS "${_mtcontrols_bungee_framework}/bungee")
            message(FATAL_ERROR "Bungee iOS framework is missing.")
        endif()
        set_target_properties(MTControlsBungee PROPERTIES
            IMPORTED_LOCATION "${_mtcontrols_bungee_framework}/bungee")
    elseif(APPLE)
        set(_mtcontrols_bungee_framework
            "${MTCONTROLS_BUNGEE_DIR}/apple-mac/bungee.framework")
        if(NOT EXISTS "${_mtcontrols_bungee_framework}/Versions/Current/bungee")
            message(FATAL_ERROR "Bungee macOS framework is missing.")
        endif()
        set_target_properties(MTControlsBungee PROPERTIES
            IMPORTED_LOCATION
                "${_mtcontrols_bungee_framework}/Versions/Current/bungee")
    elseif(WIN32)
        if(NOT EXISTS "${MTCONTROLS_BUNGEE_DIR}/windows-x86_64/bungee.dll" OR
           NOT EXISTS "${MTCONTROLS_BUNGEE_DIR}/windows-x86_64/bungee.lib")
            message(FATAL_ERROR "Bungee Windows x64 runtime/import library is missing.")
        endif()
        set_target_properties(MTControlsBungee PROPERTIES
            IMPORTED_LOCATION
                "${MTCONTROLS_BUNGEE_DIR}/windows-x86_64/bungee.dll"
            IMPORTED_IMPLIB
                "${MTCONTROLS_BUNGEE_DIR}/windows-x86_64/bungee.lib")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        if(NOT EXISTS "${MTCONTROLS_BUNGEE_DIR}/linux-aarch64/libbungee.so")
            message(FATAL_ERROR "Bungee Linux arm64 runtime is missing.")
        endif()
        set_target_properties(MTControlsBungee PROPERTIES
            IMPORTED_LOCATION
                "${MTCONTROLS_BUNGEE_DIR}/linux-aarch64/libbungee.so")
    else()
        if(NOT EXISTS "${MTCONTROLS_BUNGEE_DIR}/linux-x86_64/libbungee.so")
            message(FATAL_ERROR "Bungee Linux x64 runtime is missing.")
        endif()
        set_target_properties(MTControlsBungee PROPERTIES
            IMPORTED_LOCATION
                "${MTCONTROLS_BUNGEE_DIR}/linux-x86_64/libbungee.so")
    endif()
    set(MTCONTROLS_BUNGEE_AVAILABLE ON)
    message(STATUS "Bungee backend: required and enabled (${MTCONTROLS_BUNGEE_DIR})")
endif()

if(MTCONTROLS_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(MTControlsCloudSettingsSyncTests PRODUCT_NAME "Cloud Settings Sync Tests")
    target_sources(MTControlsCloudSettingsSyncTests PRIVATE tests/cloud_settings_sync_state_tests.cpp)
    juce_generate_juce_header(MTControlsCloudSettingsSyncTests)
    target_include_directories(MTControlsCloudSettingsSyncTests PRIVATE src)
    target_link_libraries(MTControlsCloudSettingsSyncTests
        PRIVATE juce::juce_core juce::juce_cryptography)
    add_test(NAME cloud_settings_sync_state COMMAND MTControlsCloudSettingsSyncTests)
    juce_add_console_app(MTControlsSettingsSheetTests PRODUCT_NAME "Settings Sheet Tests")
    target_sources(MTControlsSettingsSheetTests PRIVATE
        tests/settings_versions_sheet_tests.cpp src/ui/components/SettingsVersionsSheet.cpp)
    juce_generate_juce_header(MTControlsSettingsSheetTests)
    target_include_directories(MTControlsSettingsSheetTests PRIVATE src)
    target_compile_definitions(MTControlsSettingsSheetTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MTControlsSettingsSheetTests PRIVATE
        juce::juce_gui_basics juce::juce_audio_basics juce::juce_cryptography)
    add_test(NAME settings_versions_sheet COMMAND MTControlsSettingsSheetTests)
    set_tests_properties(settings_versions_sheet PROPERTIES SKIP_RETURN_CODE 77)
    juce_add_console_app(MTControlsDiagnosticReportTests PRODUCT_NAME "Playback Diagnostic Report Tests")
    target_sources(MTControlsDiagnosticReportTests PRIVATE tests/playback_diagnostic_report_tests.cpp)
    juce_generate_juce_header(MTControlsDiagnosticReportTests)
    target_include_directories(MTControlsDiagnosticReportTests PRIVATE src)
    target_link_libraries(MTControlsDiagnosticReportTests
        PRIVATE juce::juce_core juce::juce_audio_basics)
    add_test(NAME playback_diagnostic_report COMMAND MTControlsDiagnosticReportTests)
    add_executable(MTControlsWaveformCacheTests tests/waveform_cache_tests.cpp)
    target_include_directories(MTControlsWaveformCacheTests PRIVATE src)
    add_test(NAME waveform_cache COMMAND MTControlsWaveformCacheTests)
    add_executable(MTControlsAudioEngineCoreTests
        tests/audio_engine_core_tests.cpp
        src/audio/TransportClock.cpp
        src/audio/TransportClock.h
        src/audio/SectionScheduler.cpp
        src/audio/SectionScheduler.h
    )
    target_include_directories(MTControlsAudioEngineCoreTests PRIVATE src)
    add_test(NAME audio_engine_core COMMAND MTControlsAudioEngineCoreTests)

    add_executable(MTControlsPitchShiftTests
        tests/pitch_shift_tests.cpp
        src/PitchShiftProcessor.cpp
        src/PitchShiftProcessor.h
        src/audio/SimpleDelayLine.cpp
        src/audio/SimpleDelayLine.h
    )
    target_include_directories(MTControlsPitchShiftTests PRIVATE src)
    target_compile_definitions(MTControlsPitchShiftTests
        PRIVATE SOUNDTOUCH_FLOAT_SAMPLES=1)
    target_link_libraries(MTControlsPitchShiftTests
        PRIVATE SoundTouch juce::juce_audio_basics)
    add_test(NAME pitch_shift_ring_buffer COMMAND MTControlsPitchShiftTests)

    add_executable(MTControlsStemBufferingTests
        tests/stem_buffering_source_tests.cpp
        src/StemBufferingSource.cpp
        src/StemBufferingSource.h
    )
    target_include_directories(MTControlsStemBufferingTests PRIVATE src)
    target_compile_definitions(MTControlsStemBufferingTests
        PRIVATE MTCONTROLS_ENABLE_TEST_HOOKS=1)
    target_link_libraries(MTControlsStemBufferingTests
        PRIVATE juce::juce_audio_basics)
    add_test(NAME stem_buffering_source COMMAND MTControlsStemBufferingTests)

    juce_add_console_app(MTControlsMetronomeTests
        PRODUCT_NAME "MTControls Metronome Tests")
    target_sources(MTControlsMetronomeTests PRIVATE
        tests/metronome_engine_tests.cpp
        src/MetronomeEngine.cpp
        src/MetronomeEngine.h
    )
    juce_generate_juce_header(MTControlsMetronomeTests)
    target_include_directories(MTControlsMetronomeTests PRIVATE src)
    target_compile_definitions(MTControlsMetronomeTests
        PRIVATE MTCONTROLS_ENABLE_TEST_HOOKS=1)
    target_link_libraries(MTControlsMetronomeTests
        PRIVATE juce::juce_audio_utils)
    add_test(NAME metronome_engine COMMAND MTControlsMetronomeTests)

    juce_add_console_app(MTControlsPadsEngineTests
        PRODUCT_NAME "MTControls Pads Engine Tests")
    target_sources(MTControlsPadsEngineTests PRIVATE
        tests/pads_engine_tests.cpp
        src/audio/PadsEngine.cpp
        src/audio/PadsEngine.h
    )
    juce_generate_juce_header(MTControlsPadsEngineTests)
    target_include_directories(MTControlsPadsEngineTests PRIVATE src)
    target_link_libraries(MTControlsPadsEngineTests
        PRIVATE juce::juce_audio_utils)
    add_test(NAME pads_engine COMMAND MTControlsPadsEngineTests)

    juce_add_console_app(MTControlsSongMixExportTimelineTests
        PRODUCT_NAME "MTControls Song Mix Export Timeline Tests")
    target_sources(MTControlsSongMixExportTimelineTests PRIVATE
        tests/song_mix_export_timeline_tests.cpp
        src/audio/SongMixExporter.h
        src/audio/DynamicGuideEngine.cpp
        src/audio/DynamicGuideEngine.h
        src/audio/DynamicGuideTiming.h
    )
    juce_generate_juce_header(MTControlsSongMixExportTimelineTests)
    target_include_directories(MTControlsSongMixExportTimelineTests PRIVATE src)
    target_link_libraries(MTControlsSongMixExportTimelineTests
        PRIVATE juce::juce_audio_utils)
    add_test(NAME song_mix_export_timeline
        COMMAND MTControlsSongMixExportTimelineTests)

    if(MTCONTROLS_BUNGEE_AVAILABLE)
        add_executable(MTControlsBungeeTests
            tests/bungee_processor_tests.cpp
            src/audio/BungeeProcessor.cpp
            src/audio/BungeeProcessor.h
        )
        target_include_directories(MTControlsBungeeTests PRIVATE src)
        target_link_libraries(MTControlsBungeeTests
            PRIVATE MTControlsBungee juce::juce_audio_basics)
        add_test(NAME bungee_processor COMMAND MTControlsBungeeTests)

        juce_add_console_app(MTControlsStemMixEngineTests
            PRODUCT_NAME "MTControls Stem Mix Engine Tests")
        target_sources(MTControlsStemMixEngineTests PRIVATE
            tests/stem_mix_engine_tests.cpp
            src/StemMixEngine.cpp
            src/StemMixEngine.h
            src/StemTrack.cpp
            src/StemTrack.h
            src/StemBufferingSource.cpp
            src/StemBufferingSource.h
            src/ResamplingPositionableSource.cpp
            src/ResamplingPositionableSource.h
            src/audio/BungeeProcessor.cpp
            src/audio/BungeeProcessor.h
        )
        juce_generate_juce_header(MTControlsStemMixEngineTests)
        target_sources(MTControlsStemMixEngineTests PRIVATE src/audio/StemOutputRouter.cpp)
        target_include_directories(MTControlsStemMixEngineTests PRIVATE src)
        target_compile_definitions(MTControlsStemMixEngineTests
            PRIVATE MTCONTROLS_USE_BUNGEE=1)
        target_link_libraries(MTControlsStemMixEngineTests
            PRIVATE MTControlsBungee juce::juce_audio_utils)
        add_test(NAME stem_mix_engine COMMAND MTControlsStemMixEngineTests)
    endif()

    juce_add_console_app(MTControlsStemOutputRouterTests
        PRODUCT_NAME "MTControls Stem Output Router Tests")
    target_sources(MTControlsStemOutputRouterTests PRIVATE
        tests/stem_output_router_tests.cpp
        src/audio/StemOutputRouter.cpp
        src/audio/StemOutputRouter.h
    )
    juce_generate_juce_header(MTControlsStemOutputRouterTests)
    target_include_directories(MTControlsStemOutputRouterTests PRIVATE src)
    target_link_libraries(MTControlsStemOutputRouterTests
        PRIVATE juce::juce_audio_basics)
    add_test(NAME stem_output_router
        COMMAND MTControlsStemOutputRouterTests)
endif()

if (TARGET soundstretch)
    set_target_properties(soundstretch PROPERTIES 
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.multitracks.playworship.soundstretch"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        MACOSX_BUNDLE OFF
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
endif()

set(MTCONTROLS_APP_ICON_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app-icon-1024.png")

set(MTCONTROLS_PLIST_TO_MERGE "")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MTCONTROLS_PLIST_TO_MERGE
        "<plist><dict><key>CFBundleDevelopmentRegion</key><string>en</string><key>CFBundleInfoDictionaryVersion</key><string>6.0</string><key>CFBundleVersion</key><string>${MTCONTROLS_IOS_BUILD_NUMBER}</string><key>CFBundleURLTypes</key><array><dict><key>CFBundleURLName</key><string>com.multitracks.playworship.auth</string><key>CFBundleURLSchemes</key><array><string>playworship</string></array></dict></array><key>ITSAppUsesNonExemptEncryption</key><false/><key>LSApplicationCategoryType</key><string>public.app-category.music</string><key>NSAppTransportSecurity</key><dict><key>NSAllowsLocalNetworking</key><true/></dict><key>UIRequiresFullScreen</key><true/></dict></plist>")
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
    ICON_BIG "${MTCONTROLS_APP_ICON_PATH}"
    ICON_SMALL "${MTCONTROLS_APP_ICON_PATH}"
    LAUNCH_STORYBOARD_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/ios/LaunchScreen.storyboard"
    PLIST_TO_MERGE "${MTCONTROLS_PLIST_TO_MERGE}"
    DOCUMENT_EXTENSIONS pwproj
    BUNDLE_ID "com.multitracks.playworship"
    # Performance uses both landscape directions. iPhone also allows portrait
    # for Library/setlist management; the editor switches the runtime mask.
    IPAD_SCREEN_ORIENTATIONS
        UIInterfaceOrientationLandscapeLeft
        UIInterfaceOrientationLandscapeRight
    IPHONE_SCREEN_ORIENTATIONS
        UIInterfaceOrientationPortrait
        UIInterfaceOrientationLandscapeLeft
        UIInterfaceOrientationLandscapeRight
    BACKGROUND_AUDIO_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "PlayWorship uses Bluetooth to discover and pair Bluetooth MIDI devices."
    LOCAL_NETWORK_PERMISSION_ENABLED TRUE
    LOCAL_NETWORK_PERMISSION_TEXT "PlayWorship connects to compatible music and presentation services on your local network."
)

# iOS / iPadOS: device family (JUCE reads this on MTControls for the standalone .app).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(MTControls PROPERTIES
        JUCE_TARGETED_DEVICE_FAMILY "${MTCONTROLS_IOS_TARGETED_DEVICE_FAMILY}"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "18.5")
    if(TARGET MTControls_Standalone)
        set_target_properties(MTControls_Standalone PROPERTIES
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "18.5"
            XCODE_ATTRIBUTE_MARKETING_VERSION "${MTCONTROLS_APP_VERSION}"
            XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${MTCONTROLS_IOS_BUILD_NUMBER}"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES")
        set(_mtcontrols_privacy_manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/ios/PrivacyInfo.xcprivacy")
        set_source_files_properties("${_mtcontrols_privacy_manifest}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(MTControls_Standalone PRIVATE
            "${_mtcontrols_privacy_manifest}")
        if(MTCONTROLS_IOS_DEVELOPMENT_TEAM)
            set_target_properties(MTControls_Standalone PROPERTIES
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM
                "${MTCONTROLS_IOS_DEVELOPMENT_TEAM}")
        endif()
    endif()
endif()

juce_generate_juce_header(MTControls)

if(TARGET MTControls_Standalone AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(MTControls_Standalone PRIVATE src/StandaloneApp.cpp)
    target_compile_definitions(MTControls_Standalone
        PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

juce_add_binary_data(MTControlsAssets
    SOURCES
        assets/logoPlayWorship.jpeg
        assets/pwLogoSinFondo.png
        assets/fonts/InterRegular.ttf
        assets/fonts/InterMedium.ttf
        assets/fonts/InterSemiBold.ttf
        assets/fonts/InterBold.ttf
        assets/ui-icons/arrow_left.svg
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
        assets/ui-icons/more_horizontal.svg
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
        assets/ui-icons/image_edit.svg
        assets/ui-icons/pencil.svg
        assets/ui-icons/trash.svg
        assets/ui-icons/volume_x.svg
        assets/ui-icons/file_audio.svg
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    foreach(aux_target IN ITEMS MTControls MTControlsAssets SoundTouch)
        if(TARGET ${aux_target})
            set_target_properties(${aux_target} PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
                XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
        endif()
    endforeach()
endif()

target_sources(MTControls
    PRIVATE
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/PluginEditorSupport.cpp
        src/PluginEditorSupport.h
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/telemetry/Telemetry.cpp
        src/telemetry/Telemetry.h
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
        src/audio/SongMixExporter.cpp
        src/audio/SongMixExporter.h
        src/audio/StemCache.cpp
        src/audio/StemCache.h
        src/SongLoadProbe.cpp
        src/SongLoadProbe.h
        src/audio/TimeStretchProcessor.h
        src/audio/DynamicGuideEngine.cpp
        src/audio/DynamicGuideEngine.h
        src/audio/DynamicGuideTiming.h
        src/audio/OutputChannelCatalog.cpp
        src/audio/OutputChannelCatalog.h
        src/audio/PadsEngine.cpp
        src/audio/PadsEngine.h
        src/audio/RoutingSuggestionEngine.cpp
        src/audio/RoutingSuggestionEngine.h
        src/audio/SimpleDelayLine.cpp
        src/audio/SimpleDelayLine.h
        src/audio/SongTransitionEnvelope.h
        src/audio/StandaloneAudioDeviceBridge.cpp
        src/audio/StandaloneAudioDeviceBridge.h
        src/audio/TransportClock.cpp
        src/audio/TransportClock.h
        src/audio/SectionScheduler.cpp
        src/audio/SectionScheduler.h
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
        src/data/CloudSyncTypes.h
        src/data/CloudProjectArchive.cpp
        src/data/CloudProjectArchive.h
        src/data/CloudLibraryClient.cpp
        src/data/CloudLibraryClient.h
        src/data/CloudSettingsClient.cpp
        src/data/CloudSettingsClient.h
        src/data/CloudSyncIndexStore.cpp
        src/data/CloudSyncIndexStore.h
        src/data/CloudTransferClient.cpp
        src/data/CloudTransferClient.h
        src/data/CloudSignedUpload.h
        src/data/SongSyncService.cpp
        src/data/SongSyncService.h
        src/data/LocalAuthCallbackServer.cpp
        src/data/LocalAuthCallbackServer.h
        src/data/SessionAuthLogger.cpp
        src/data/SessionAuthLogger.h
        src/data/SessionPersistence.cpp
        src/data/SessionPersistence.h
        src/data/SecuenciasClient.cpp
        src/data/SecuenciasClient.h
        src/data/Localization.h
        src/data/PrimeLibrarySource.cpp
        src/data/PrimeLibrarySource.h
        src/data/ProjectStore.cpp
        src/data/ProjectStore.h
        src/data/SongData.cpp
        src/data/SongData.h
        src/data/SongLibraryStore.cpp
        src/data/SongLibraryStore.h
        src/ui/DeviceNotificationBanner.cpp
        src/ui/DeviceNotificationBanner.h
        src/ui/UiCommon.cpp
        src/ui/UiCommon.h
        src/platform/NativeIpadChoicePicker.h
        src/ui/components/ChannelStripCard.cpp
        src/ui/components/ChannelStripCard.h
        src/ui/components/ClickDivisionPanel.cpp
        src/ui/components/ClickDivisionPanel.h
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
        src/ui/panels/BottomStatusBar.cpp
        src/ui/panels/BottomStatusBar.h
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
        src/ui/components/SettingsVersionsSheet.cpp
        src/ui/components/SettingsVersionsSheet.h
        src/ui/components/ReauthPromptModal.h
        src/ui/panels/PadsComponent.cpp
        src/ui/panels/PadsComponent.h
        src/ui/components/WelcomeModal.cpp
        src/ui/components/WelcomeModal.h
)

if(APPLE)
    target_sources(MTControls PRIVATE src/data/CloudSignedUpload.mm)
else()
    target_sources(MTControls PRIVATE src/data/CloudSignedUpload.cpp)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(MTControls PRIVATE
        src/platform/NativeAuthSession.mm
        src/platform/NativeAuthSession.h
        src/platform/NativeIpadChoicePicker.mm)
    target_link_libraries(MTControls PRIVATE "-framework AuthenticationServices")
else()
    target_sources(MTControls PRIVATE
        src/platform/NativeAuthSession.cpp
        src/platform/NativeAuthSession.h
        src/platform/NativeIpadChoicePicker.cpp)
endif()

if(MTCONTROLS_BUNGEE_AVAILABLE)
    target_sources(MTControls PRIVATE
        src/audio/BungeeProcessor.cpp
        src/audio/BungeeProcessor.h)
endif()

# Allow subdirectories under src/ (e.g. src/data/ProjectStore.cpp) to
# include headers from the top-level src/ directory (e.g. SongLoadProbe.h).
target_include_directories(MTControls PRIVATE src)

target_compile_definitions(MTControls
    PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        MTCONTROLS_API_BASE_URL=\"${MTCONTROLS_RESOLVED_API_BASE_URL}\"
        MTCONTROLS_AUTH_BASE_URL=\"${MTCONTROLS_AUTH_BASE_URL}\"
        MTCONTROLS_ASSETS_BASE_URL=\"${MTCONTROLS_ASSETS_BASE_URL}\"
        MTCONTROLS_SESSION_REAUTH_MINUTES=${MTCONTROLS_SESSION_REAUTH_MINUTES}
        MTCONTROLS_REAUTH_GRACE_MINUTES=${MTCONTROLS_REAUTH_GRACE_MINUTES}
        MTCONTROLS_MAX_OFFLINE_HOURS=${MTCONTROLS_MAX_OFFLINE_HOURS}
        SOUNDTOUCH_FLOAT_SAMPLES=1
        MTCONTROLS_USE_BUNGEE=$<BOOL:${MTCONTROLS_BUNGEE_AVAILABLE}>
        MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS=$<BOOL:${MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS}>
        MTCONTROLS_SENTRY_ENABLED=$<BOOL:${MTCONTROLS_SENTRY_ENABLED}>
        MTCONTROLS_SENTRY_DSN="${MTCONTROLS_SENTRY_DSN}"
        MTCONTROLS_SENTRY_ENVIRONMENT="${MTCONTROLS_SENTRY_ENVIRONMENT}"
        MTCONTROLS_SENTRY_RELEASE="playworship-app@${MTCONTROLS_APP_VERSION}+${MTCONTROLS_IOS_BUILD_NUMBER}.${MTCONTROLS_GIT_COMMIT}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(MTControls PRIVATE JUCE_CONTENT_SHARING=1)
endif()

if (WIN32)
    target_compile_definitions(MTControls
        PRIVATE
            JUCE_ASIO=1
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
            # DirectWrite color glyph APIs used by JUCE require at least Windows 10 RS3 SDK surface.
            NTDDI_VERSION=0x0A000004
    )
endif()

# MSVC: literales UTF-8 en fuente (…, •, tildes) y juce::String deben coincidir con el encoding del .cpp.
if (MSVC)
    target_compile_options(MTControls PRIVATE /utf-8)
endif()

target_link_libraries(MTControls
    PRIVATE
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        MTControlsAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(MTCONTROLS_SENTRY_ENABLED)
    target_link_libraries(MTControls PRIVATE sentry::sentry)
endif()

if(MTCONTROLS_BUNGEE_AVAILABLE)
    target_link_libraries(MTControls PRIVATE MTControlsBungee)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(MTControls_Standalone PROPERTIES
            BUILD_RPATH "@executable_path/Frameworks"
            INSTALL_RPATH "@executable_path/Frameworks"
            XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
            XCODE_EMBED_FRAMEWORKS "${_mtcontrols_bungee_framework}"
            XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
            XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES)
        set_source_files_properties("${MTCONTROLS_BUNGEE_DIR}/LICENSE"
            PROPERTIES MACOSX_PACKAGE_LOCATION
                "Resources/ThirdPartyLicenses")
        target_sources(MTControls_Standalone PRIVATE
            "${MTCONTROLS_BUNGEE_DIR}/LICENSE")
    elseif(APPLE)
        set_target_properties(MTControls_Standalone PROPERTIES
            BUILD_RPATH "@executable_path/../Frameworks")
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E remove_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Frameworks/bungee.framework"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_mtcontrols_bungee_framework}/Versions/Current/bungee"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Frameworks/libbungee.dylib"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/ThirdPartyLicenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_BUNGEE_DIR}/LICENSE"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/ThirdPartyLicenses/Bungee-MPL-2.0.txt"
            COMMAND /usr/bin/install_name_tool -id
                "@rpath/libbungee.dylib"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Frameworks/libbungee.dylib"
            COMMAND /usr/bin/install_name_tool -change
                "@rpath/bungee.framework/Versions/${MTCONTROLS_BUNGEE_VERSION}/bungee"
                "@rpath/libbungee.dylib"
                "$<TARGET_FILE:MTControls_Standalone>"
            COMMAND /usr/bin/codesign --force --sign -
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Frameworks/libbungee.dylib"
            VERBATIM)
    elseif(WIN32)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_BUNGEE_DIR}/windows-x86_64/bungee.dll"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/bungee.dll"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ThirdPartyLicenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_BUNGEE_DIR}/LICENSE"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ThirdPartyLicenses/Bungee-MPL-2.0.txt"
            VERBATIM)
    elseif(UNIX AND NOT APPLE)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:MTControlsBungee>"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/libbungee.so"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ThirdPartyLicenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_BUNGEE_DIR}/LICENSE"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/ThirdPartyLicenses/Bungee-MPL-2.0.txt"
            VERBATIM)
        set_target_properties(MTControls_Standalone PROPERTIES
            BUILD_RPATH "$ORIGIN")
    endif()
endif()

if (TARGET SoundTouch)
    target_compile_definitions(SoundTouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)
    if (MSVC)
        target_compile_options(SoundTouch PRIVATE /w)
    else()
        target_compile_options(SoundTouch PRIVATE -w)
    endif()
    target_link_libraries(MTControls PRIVATE SoundTouch)
    target_include_directories(MTControls SYSTEM PRIVATE ${soundtouch_SOURCE_DIR}/include)
elseif (TARGET soundtouch)
    target_compile_definitions(soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)
    if (MSVC)
        target_compile_options(soundtouch PRIVATE /w)
    else()
        target_compile_options(soundtouch PRIVATE -w)
    endif()
    target_link_libraries(MTControls PRIVATE soundtouch)
    target_include_directories(MTControls SYSTEM PRIVATE ${soundtouch_SOURCE_DIR}/include)
else()
    message(FATAL_ERROR "SoundTouch target not found after FetchContent.")
endif()

if (TARGET MTControls_Standalone AND MTCONTROLS_BUNDLE_FFMPEG)
    # APPLE is true for iOS too; bundling the macOS ffmpeg binary into an iOS app breaks CodeSign.
    if (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND
        MTCONTROLS_FFMPEG_BIN_MACOS)
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

if (TARGET MTControls_Standalone AND MTCONTROLS_GUIDES_PACK AND
    EXISTS "${MTCONTROLS_GUIDES_PACK}")
    if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/installer"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_GUIDES_PACK}"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/installer/guides-en-es.zip"
            VERBATIM)
    elseif (APPLE)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/guides"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_GUIDES_PACK}"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/guides/guides-en-es.zip"
            VERBATIM)
    elseif (WIN32)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_GUIDES_PACK}"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer/guides-en-es.zip"
            VERBATIM)
    elseif (UNIX)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_GUIDES_PACK}"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer/guides-en-es.zip"
            VERBATIM)
    endif()
endif()

if (TARGET MTControls_Standalone AND MTCONTROLS_CLICKS_PACK AND
    EXISTS "${MTCONTROLS_CLICKS_PACK}")
    if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/installer"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_CLICKS_PACK}"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/installer/clicks-pack.zip"
            VERBATIM)
    elseif (APPLE)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/clicks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_CLICKS_PACK}"
                "$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Resources/clicks/clicks-pack.zip"
            VERBATIM)
    elseif (WIN32)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_CLICKS_PACK}"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer/clicks-pack.zip"
            VERBATIM)
    elseif (UNIX)
        add_custom_command(TARGET MTControls_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MTCONTROLS_CLICKS_PACK}"
                "$<TARGET_FILE_DIR:MTControls_Standalone>/installer/clicks-pack.zip"
            VERBATIM)
    endif()
endif()

if(TARGET MTControls_Standalone AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_custom_command(TARGET MTControls_Standalone POST_BUILD
        COMMAND /bin/sh -c
            "framework='$<TARGET_BUNDLE_CONTENT_DIR:MTControls_Standalone>/Frameworks/bungee.framework'; plist=\"$framework/Info.plist\"; if [ -f \"$plist\" ]; then /usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier com.zynaptiq.bungee' \"$plist\" 2>/dev/null || /usr/libexec/PlistBuddy -c 'Add :CFBundleIdentifier string com.zynaptiq.bungee' \"$plist\"; if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" --timestamp=none --generate-entitlement-der \"$framework\"; fi; fi"
        COMMENT "Preparing embedded iOS framework before app code signing"
        VERBATIM)
endif()

# This must remain the final macOS POST_BUILD command. All bundled resources
# (Bungee, ffmpeg, guides and click packs) must be present before sealing the
# app, otherwise codesign reports a missing or invalid sealed resource.
if(TARGET MTControls_Standalone AND APPLE AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_custom_command(TARGET MTControls_Standalone POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --sign -
            "$<TARGET_BUNDLE_DIR:MTControls_Standalone>"
        VERBATIM)
endif()
