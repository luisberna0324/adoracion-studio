cmake_minimum_required(VERSION 3.22)

set(MTCONTROLS_APP_VERSION "1.1.4" CACHE STRING
    "Semantic version used for build metadata (MAJOR.MINOR.PATCH)")
set(MTCONTROLS_IOS_BUILD_NUMBER "2" CACHE STRING
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
option(MTCONTROLS_BUILD_TESTS "Build the PlayWorship regression tests" OFF)
option(MTCONTROLS_APPLE_SIGN_IN_ENABLED
    "Enable native Apple sign-in when portal entitlements and backend are configured" OFF)
if(MTCONTROLS_APPLE_SIGN_IN_ENABLED AND NOT APPLE)
    message(FATAL_ERROR "Native Apple sign-in is available only on Apple platforms")
endif()
option(MTCONTROLS_MAC_APP_STORE
    "Build the sandboxed, helper-free Mac App Store variant (Xcode only)" OFF)
set(MTCONTROLS_MAC_DEVELOPMENT_TEAM "" CACHE STRING
    "Apple development team for the optional Mac App Store variant")
set(MTCONTROLS_MAC_BUILD_NUMBER "1" CACHE STRING
    "Monotonically increasing Mac App Store build number")
if(MTCONTROLS_MAC_APP_STORE)
    if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
       NOT CMAKE_GENERATOR STREQUAL "Xcode")
        message(FATAL_ERROR "MTCONTROLS_MAC_APP_STORE requires macOS and the Xcode generator")
    endif()
    if(NOT MTCONTROLS_MAC_DEVELOPMENT_TEAM MATCHES "^[A-Z0-9]+$")
        message(FATAL_ERROR "Set MTCONTROLS_MAC_DEVELOPMENT_TEAM for the Mac App Store variant")
    endif()
    if(NOT MTCONTROLS_MAC_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "MTCONTROLS_MAC_BUILD_NUMBER must be a positive integer")
    endif()
endif()
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
if(MTCONTROLS_MAC_APP_STORE AND MTCONTROLS_BUNDLE_FFMPEG)
    message(FATAL_ERROR
        "The initial Mac App Store variant uses built-in codecs and WAV export; do not bundle FFmpeg")
endif()
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
include(cmake/IOSFormKeyboard.cmake)

FetchContent_Declare(
    soundtouch
    URL https://gitlab.com/soundtouch/soundtouch/-/archive/2.3.1/soundtouch-2.3.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(soundtouch)

# WebSocket client/server is shared by all standalone targets, including iOS.
set(USE_ZLIB OFF CACHE BOOL "Disable WebSocket compression" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "Do not install IXWebSocket" FORCE)
FetchContent_Declare(ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG 64fae7676bd8fe31f7cb4bcde7a6841892dad65e)
FetchContent_MakeAvailable(ixwebsocket)

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

add_library(PlayWorshipLyricsModel STATIC src/integrations/FreeShowModel.cpp)
target_include_directories(PlayWorshipLyricsModel PUBLIC src)
target_compile_definitions(PlayWorshipLyricsModel PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(PlayWorshipLyricsModel PUBLIC juce::juce_core)
set_target_properties(PlayWorshipLyricsModel PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MTCONTROLS_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    juce_add_console_app(MTControlsAccountDeletionTests PRODUCT_NAME "Account Deletion Flow Tests")
    target_sources(MTControlsAccountDeletionTests PRIVATE tests/account_deletion_tests.cpp
        src/ui/components/AccountDeletionModal.cpp src/data/AccountDeletionApi.cpp
        src/data/AuthSessionApi.cpp src/data/AppSettings.cpp src/data/SessionPersistence.cpp
        src/data/SongLibraryStore.cpp src/data/SessionAuthLogger.cpp)
    target_include_directories(MTControlsAccountDeletionTests PRIVATE src)
    target_compile_definitions(MTControlsAccountDeletionTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsAccountDeletionTests PRIVATE MTControls juce::juce_audio_utils juce::juce_cryptography MTControlsAssets)
    juce_generate_juce_header(MTControlsAccountDeletionTests)
    add_test(NAME account_deletion_flow COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py" "$<TARGET_FILE:MTControlsAccountDeletionTests>")
    juce_add_console_app(MTControlsEmailPasswordAuthTests PRODUCT_NAME "Email Password Auth Contract Tests")
    target_sources(MTControlsEmailPasswordAuthTests PRIVATE
        tests/email_password_auth_tests.cpp src/data/EmailPasswordAuthApi.cpp)
    target_include_directories(MTControlsEmailPasswordAuthTests PRIVATE src)
    target_link_libraries(MTControlsEmailPasswordAuthTests PRIVATE juce::juce_core)
    juce_generate_juce_header(MTControlsEmailPasswordAuthTests)
    add_test(NAME email_password_auth_contract COMMAND "$<TARGET_FILE:MTControlsEmailPasswordAuthTests>")
    juce_add_console_app(MTControlsEmailAuthUiTests PRODUCT_NAME "Email Authentication UI Tests")
    target_sources(MTControlsEmailAuthUiTests PRIVATE tests/email_auth_ui_tests.cpp src/data/AppSettings.cpp)
    target_include_directories(MTControlsEmailAuthUiTests PRIVATE src)
    target_compile_definitions(MTControlsEmailAuthUiTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsEmailAuthUiTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    juce_generate_juce_header(MTControlsEmailAuthUiTests)
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        juce_add_console_app(MTControlsLoginHelpModalUiTests PRODUCT_NAME "Login and Help Modal UI Tests")
        target_sources(MTControlsLoginHelpModalUiTests PRIVATE
            tests/login_help_modal_ui_tests.mm src/data/AppSettings.cpp)
        target_include_directories(MTControlsLoginHelpModalUiTests PRIVATE src)
        target_compile_definitions(MTControlsLoginHelpModalUiTests PRIVATE
            MTCONTROLS_ISOLATED_STORAGE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(MTControlsLoginHelpModalUiTests PRIVATE MTControls MTControlsAssets
            juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
        juce_generate_juce_header(MTControlsLoginHelpModalUiTests)
        add_test(NAME login_help_modal_ui COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
            "$<TARGET_FILE:MTControlsLoginHelpModalUiTests>")
    endif()
    add_test(NAME email_authentication_ui COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsEmailAuthUiTests>")
    juce_add_console_app(MTControlsLocalDemoAccessTests PRODUCT_NAME "Local Demo Access Tests")
    target_sources(MTControlsLocalDemoAccessTests PRIVATE
        tests/local_demo_access_tests.cpp src/data/AppSettings.cpp
        src/data/SongLibraryStore.cpp src/data/SessionPersistence.cpp
        src/data/SessionAuthLogger.cpp src/data/AssetBootstrapManager.cpp
        src/data/TeamsLocalStore.cpp src/ui/learning/LearningGuideModel.cpp)
    target_include_directories(MTControlsLocalDemoAccessTests PRIVATE src)
    target_compile_definitions(MTControlsLocalDemoAccessTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsLocalDemoAccessTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    juce_generate_juce_header(MTControlsLocalDemoAccessTests)
    add_test(NAME local_demo_access COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsLocalDemoAccessTests>")
    add_test(NAME blank_song_layout COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsLocalDemoAccessTests>" --blank-layout-only)
    if(MSVC)
        target_compile_options(MTControlsLocalDemoAccessTests PRIVATE /utf-8)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(local_demo_access PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    juce_add_console_app(MTControlsAppleSignInTests PRODUCT_NAME "Apple Sign In Contract Tests")
    target_sources(MTControlsAppleSignInTests PRIVATE
        tests/apple_signin_contract_tests.cpp src/data/AppleSignInApi.cpp
        src/platform/NativeAppleSignIn.cpp)
    target_include_directories(MTControlsAppleSignInTests PRIVATE src)
    target_link_libraries(MTControlsAppleSignInTests PRIVATE
        juce::juce_core juce::juce_cryptography)
    if(NOT APPLE)
        # The unavailable-platform implementation returns a Component pointer
        # and includes juce_gui_basics, so its test needs that module too.
        target_link_libraries(MTControlsAppleSignInTests PRIVATE juce::juce_gui_basics)
    endif()
    juce_generate_juce_header(MTControlsAppleSignInTests)
    add_test(NAME apple_signin_contract COMMAND "$<TARGET_FILE:MTControlsAppleSignInTests>")
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        juce_add_console_app(MTControlsAppleLifecycleTests PRODUCT_NAME "Native Apple Credential Lifecycle Tests")
        target_sources(MTControlsAppleLifecycleTests PRIVATE tests/apple_credential_lifecycle_tests.mm
            src/platform/NativeAppleSignIn.cpp src/platform/NativeAppleSignIn.mm)
        target_include_directories(MTControlsAppleLifecycleTests PRIVATE src)
        target_compile_definitions(MTControlsAppleLifecycleTests PRIVATE
            MTCONTROLS_NATIVE_APPLE_LIFECYCLE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(MTControlsAppleLifecycleTests PRIVATE
            juce::juce_core juce::juce_gui_extra juce::juce_cryptography "-framework AuthenticationServices")
        juce_generate_juce_header(MTControlsAppleLifecycleTests)
        add_test(NAME apple_credential_lifecycle COMMAND "$<TARGET_FILE:MTControlsAppleLifecycleTests>")
    endif()

    juce_add_console_app(MTControlsRemoteSessionTests PRODUCT_NAME "Remote Session Tests")
    target_sources(MTControlsRemoteSessionTests PRIVATE
        tests/remote_session_tests.cpp
        src/network/LanDiscovery.cpp
        src/network/RemoteSession.cpp)
    juce_generate_juce_header(MTControlsRemoteSessionTests)
    target_include_directories(MTControlsRemoteSessionTests PRIVATE src)
    target_link_libraries(MTControlsRemoteSessionTests PRIVATE
        juce::juce_core juce::juce_data_structures juce::juce_cryptography
        ixwebsocket::ixwebsocket)
    add_test(NAME remote_session COMMAND MTControlsRemoteSessionTests)
    juce_add_console_app(MTControlsRemoteSectionControlTests PRODUCT_NAME "Remote Section Control Tests")
    target_sources(MTControlsRemoteSectionControlTests PRIVATE
        tests/remote_section_control_tests.cpp src/network/LanDiscovery.cpp
        src/network/RemoteSession.cpp src/audio/SectionScheduler.cpp)
    juce_generate_juce_header(MTControlsRemoteSectionControlTests)
    target_include_directories(MTControlsRemoteSectionControlTests PRIVATE src)
    target_link_libraries(MTControlsRemoteSectionControlTests PRIVATE
        juce::juce_core juce::juce_data_structures juce::juce_cryptography ixwebsocket::ixwebsocket)
    add_test(NAME remote_section_control COMMAND MTControlsRemoteSectionControlTests)
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        juce_add_console_app(MTControlsPortableRemoteSessionTests PRODUCT_NAME "Portable Remote Session Tests")
        target_sources(MTControlsPortableRemoteSessionTests PRIVATE
            tests/remote_session_tests.cpp src/network/LanDiscovery.cpp src/network/RemoteSession.cpp)
        juce_generate_juce_header(MTControlsPortableRemoteSessionTests)
        target_include_directories(MTControlsPortableRemoteSessionTests PRIVATE src)
        target_compile_definitions(MTControlsPortableRemoteSessionTests PRIVATE PLAYWORSHIP_FORCE_PORTABLE_MDNS=1)
        target_link_libraries(MTControlsPortableRemoteSessionTests PRIVATE
            juce::juce_core juce::juce_data_structures juce::juce_cryptography ixwebsocket::ixwebsocket)
        add_test(NAME remote_session_portable_transport COMMAND MTControlsPortableRemoteSessionTests)
        set_tests_properties(remote_session_portable_transport PROPERTIES
            ENVIRONMENT "PW_REMOTE_DIRECT_CONNECT=1")
        add_test(NAME remote_session_portable COMMAND MTControlsPortableRemoteSessionTests)
        set_tests_properties(remote_session_portable PROPERTIES LABELS lan)
        add_test(NAME remote_discovery_interop COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/remote_discovery_interop.py"
            "$<TARGET_FILE:MTControlsRemoteSessionTests>"
            "$<TARGET_FILE:MTControlsPortableRemoteSessionTests>")
        set_tests_properties(remote_discovery_interop PROPERTIES LABELS lan)
    endif()
    juce_add_console_app(MTControlsCloudSettingsSyncTests PRODUCT_NAME "Cloud Settings Sync Tests")
    target_sources(MTControlsCloudSettingsSyncTests PRIVATE tests/cloud_settings_sync_state_tests.cpp)
    juce_generate_juce_header(MTControlsCloudSettingsSyncTests)
    target_include_directories(MTControlsCloudSettingsSyncTests PRIVATE src)
    target_link_libraries(MTControlsCloudSettingsSyncTests
        PRIVATE juce::juce_core juce::juce_cryptography)
    add_test(NAME cloud_settings_sync_state COMMAND MTControlsCloudSettingsSyncTests)
    juce_add_console_app(MTControlsTeamsLocalStoreTests PRODUCT_NAME "Teams Local Store Tests")
    target_sources(MTControlsTeamsLocalStoreTests PRIVATE tests/teams_local_store_tests.cpp src/data/TeamsLocalStore.cpp)
    juce_generate_juce_header(MTControlsTeamsLocalStoreTests)
    target_include_directories(MTControlsTeamsLocalStoreTests PRIVATE src)
    target_compile_definitions(MTControlsTeamsLocalStoreTests PRIVATE MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsTeamsLocalStoreTests PRIVATE juce::juce_core juce::juce_cryptography)
    add_test(NAME teams_local_store COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py" "$<TARGET_FILE:MTControlsTeamsLocalStoreTests>")
    juce_add_console_app(MTControlsTeamsPreparationTests PRODUCT_NAME "Teams Preparation Tests")
    target_sources(MTControlsTeamsPreparationTests PRIVATE tests/teams_preparation_tests.cpp
        src/data/TeamsPreparationService.cpp src/data/TeamsLocalStore.cpp
        src/data/SongLibraryStore.cpp src/data/SessionPersistence.cpp
        src/data/AppSettings.cpp src/data/SessionAuthLogger.cpp)
    juce_generate_juce_header(MTControlsTeamsPreparationTests)
    target_include_directories(MTControlsTeamsPreparationTests PRIVATE src)
    target_compile_definitions(MTControlsTeamsPreparationTests PRIVATE MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsTeamsPreparationTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    add_test(NAME teams_preparation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py" "$<TARGET_FILE:MTControlsTeamsPreparationTests>")
    juce_add_console_app(MTControlsTeamsPanelTests PRODUCT_NAME "Teams Panel Tests")
    target_sources(MTControlsTeamsPanelTests PRIVATE tests/teams_panel_tests.cpp src/data/AppSettings.cpp)
    juce_generate_juce_header(MTControlsTeamsPanelTests)
    target_include_directories(MTControlsTeamsPanelTests PRIVATE src)
    target_compile_definitions(MTControlsTeamsPanelTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MTControlsTeamsPanelTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    add_test(NAME teams_panel COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py" "$<TARGET_FILE:MTControlsTeamsPanelTests>")
    juce_add_console_app(MTControlsTeamsWorkspaceTests PRODUCT_NAME "Teams Workspace Tests")
    target_sources(MTControlsTeamsWorkspaceTests PRIVATE tests/teams_workspace_tests.cpp
        src/data/AppSettings.cpp src/data/TeamsLocalStore.cpp
        src/data/SongLibraryStore.cpp src/data/SessionPersistence.cpp
        src/data/SessionAuthLogger.cpp)
    juce_generate_juce_header(MTControlsTeamsWorkspaceTests)
    target_include_directories(MTControlsTeamsWorkspaceTests PRIVATE src)
    target_compile_definitions(MTControlsTeamsWorkspaceTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MTControlsTeamsWorkspaceTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    add_test(NAME teams_workspace COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py" "$<TARGET_FILE:MTControlsTeamsWorkspaceTests>")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(teams_panel teams_workspace PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    juce_add_gui_app(MTControlsTeamsUIPreview PRODUCT_NAME "PlayWorship Teams Preview")
    target_sources(MTControlsTeamsUIPreview PRIVATE tests/teams_ui_preview.cpp src/data/AppSettings.cpp)
    juce_generate_juce_header(MTControlsTeamsUIPreview)
    target_include_directories(MTControlsTeamsUIPreview PRIVATE src)
    target_compile_definitions(MTControlsTeamsUIPreview PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    target_link_libraries(MTControlsTeamsUIPreview PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    juce_add_console_app(MTControlsCloudSyncIndexTests
        PRODUCT_NAME "Cloud Sync Index Tests")
    target_sources(MTControlsCloudSyncIndexTests PRIVATE
        tests/cloud_sync_index_tests.cpp
        src/data/CloudSyncIndexStore.cpp
        src/data/SongLibraryStore.cpp
        src/data/AppSettings.cpp
        src/data/SessionAuthLogger.cpp)
    juce_generate_juce_header(MTControlsCloudSyncIndexTests)
    target_include_directories(MTControlsCloudSyncIndexTests PRIVATE src)
    target_compile_definitions(MTControlsCloudSyncIndexTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsCloudSyncIndexTests PRIVATE
        MTControls MTControlsAssets juce::juce_audio_utils
        juce::juce_dsp juce::juce_cryptography)
    add_test(NAME cloud_sync_index COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsCloudSyncIndexTests>")
    juce_add_console_app(MTControlsMidiSettingsTests PRODUCT_NAME "MIDI Settings Tests")
    target_sources(MTControlsMidiSettingsTests PRIVATE
        tests/midi_settings_tests.cpp src/data/AppSettings.cpp
        src/data/SongData.cpp)
    juce_generate_juce_header(MTControlsMidiSettingsTests)
    target_include_directories(MTControlsMidiSettingsTests PRIVATE src)
    target_compile_definitions(MTControlsMidiSettingsTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsMidiSettingsTests PRIVATE juce::juce_audio_utils)
    add_test(NAME midi_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsMidiSettingsTests>")
    juce_add_console_app(MTControlsSettingsSheetTests PRODUCT_NAME "Settings Sheet Tests")
    target_sources(MTControlsSettingsSheetTests PRIVATE
        tests/settings_versions_sheet_tests.cpp src/ui/components/SettingsVersionsSheet.cpp)
    juce_generate_juce_header(MTControlsSettingsSheetTests)
    target_include_directories(MTControlsSettingsSheetTests PRIVATE src)
    target_compile_definitions(MTControlsSettingsSheetTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MTControlsSettingsSheetTests PRIVATE
        juce::juce_gui_basics juce::juce_audio_basics juce::juce_cryptography)
    add_test(NAME settings_versions_sheet COMMAND MTControlsSettingsSheetTests)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(settings_versions_sheet PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    juce_add_console_app(MTControlsInputControlsTests PRODUCT_NAME "Input Controls Tests")
    if(APPLE)
        set_source_files_properties(tests/input_controls_tests.cpp PROPERTIES LANGUAGE OBJCXX)
    endif()
    target_sources(MTControlsInputControlsTests PRIVATE
        tests/input_controls_tests.cpp src/ui/UiCommon.cpp
        src/ui/components/SearchField.cpp)
    juce_generate_juce_header(MTControlsInputControlsTests)
    target_include_directories(MTControlsInputControlsTests PRIVATE src)
    target_compile_definitions(MTControlsInputControlsTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MTControlsInputControlsTests PRIVATE
        juce::juce_gui_basics juce::juce_audio_basics MTControlsAssets)
    add_test(NAME input_controls COMMAND MTControlsInputControlsTests)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(input_controls PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_test(NAME input_controls_popup COMMAND MTControlsInputControlsTests --popup)
    set_tests_properties(input_controls_popup PROPERTIES
        LABELS interactive SKIP_RETURN_CODE 77)
    juce_add_console_app(MTControlsLearningGuideTests PRODUCT_NAME "Learning Guide Tests")
    target_sources(MTControlsLearningGuideTests PRIVATE
        tests/learning_guide_tests.cpp src/ui/learning/LearningGuideModel.cpp)
    target_include_directories(MTControlsLearningGuideTests PRIVATE src)
    target_link_libraries(MTControlsLearningGuideTests PRIVATE juce::juce_core)
    add_test(NAME learning_guide COMMAND MTControlsLearningGuideTests)
    juce_add_console_app(MTControlsDemoLearningTests PRODUCT_NAME "Demo Learning Tests")
    target_sources(MTControlsDemoLearningTests PRIVATE tests/demo_learning_tests.cpp)
    target_include_directories(MTControlsDemoLearningTests PRIVATE src)
    target_link_libraries(MTControlsDemoLearningTests PRIVATE juce::juce_core)
    add_test(NAME demo_learning COMMAND MTControlsDemoLearningTests)
    juce_add_console_app(MTControlsDemoSongTests PRODUCT_NAME "Demo Song Tests")
    target_sources(MTControlsDemoSongTests PRIVATE tests/demo_song_tests.cpp
        src/data/DemoSongService.cpp src/data/SongLibraryStore.cpp
        src/data/SessionPersistence.cpp src/data/AppSettings.cpp src/data/SessionAuthLogger.cpp)
    juce_generate_juce_header(MTControlsDemoSongTests)
    target_include_directories(MTControlsDemoSongTests PRIVATE src)
    target_compile_definitions(MTControlsDemoSongTests PRIVATE MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsDemoSongTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    add_test(NAME demo_song COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py" "$<TARGET_FILE:MTControlsDemoSongTests>")
    juce_add_console_app(MTControlsProjectImportTests PRODUCT_NAME "Project Import Tests")
    target_sources(MTControlsProjectImportTests PRIVATE tests/project_import_tests.cpp
        src/data/AppSettings.cpp src/data/SongLibraryStore.cpp
        src/data/SessionPersistence.cpp src/data/SessionAuthLogger.cpp)
    juce_generate_juce_header(MTControlsProjectImportTests)
    target_include_directories(MTControlsProjectImportTests PRIVATE src)
    target_compile_definitions(MTControlsProjectImportTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MTControlsProjectImportTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    add_test(NAME project_import COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsProjectImportTests>")
    set_tests_properties(project_import PROPERTIES TIMEOUT 45)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(project_import PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(MSVC)
        target_compile_options(MTControlsDemoLearningTests PRIVATE /utf-8)
        target_compile_options(MTControlsDemoSongTests PRIVATE /utf-8)
        target_compile_options(MTControlsProjectImportTests PRIVATE /utf-8)
    endif()
    juce_add_console_app(MTControlsRemoteLearningTests PRODUCT_NAME "Remote Learning Tests")
    target_sources(MTControlsRemoteLearningTests PRIVATE tests/remote_learning_action_tests.cpp)
    target_include_directories(MTControlsRemoteLearningTests PRIVATE src)
    target_link_libraries(MTControlsRemoteLearningTests PRIVATE juce::juce_core)
    add_test(NAME remote_learning_action COMMAND MTControlsRemoteLearningTests)
    juce_add_console_app(MTControlsLearningOverlayTests PRODUCT_NAME "Learning Overlay Tests")
    target_sources(MTControlsLearningOverlayTests PRIVATE tests/learning_overlay_tests.cpp
        src/data/AppSettings.cpp)
    juce_generate_juce_header(MTControlsLearningOverlayTests)
    target_include_directories(MTControlsLearningOverlayTests PRIVATE src)
    target_compile_definitions(MTControlsLearningOverlayTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MTControlsLearningOverlayTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    if(MSVC)
        target_compile_options(MTControlsLearningGuideTests PRIVATE /utf-8)
        target_compile_options(MTControlsLearningOverlayTests PRIVATE /utf-8)
    endif()
    add_test(NAME learning_overlay COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsLearningOverlayTests>")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(learning_overlay PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    juce_add_console_app(MTControlsSettingsPanelTests PRODUCT_NAME "Settings Panel Tests")
    target_sources(MTControlsSettingsPanelTests PRIVATE tests/settings_panel_tests.cpp
        src/data/AppSettings.cpp)
    juce_generate_juce_header(MTControlsSettingsPanelTests)
    target_include_directories(MTControlsSettingsPanelTests PRIVATE src)
    target_compile_definitions(MTControlsSettingsPanelTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsSettingsPanelTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    add_test(NAME settings_panel COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSettingsPanelTests>")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(settings_panel PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    juce_add_console_app(MTControlsProjectArtworkTests PRODUCT_NAME "Project Artwork Tests")
    target_sources(MTControlsProjectArtworkTests PRIVATE tests/project_artwork_tests.cpp)
    target_include_directories(MTControlsProjectArtworkTests PRIVATE src)
    target_compile_definitions(MTControlsProjectArtworkTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsProjectArtworkTests PRIVATE juce::juce_core)
    add_test(NAME project_artwork COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsProjectArtworkTests>")
    juce_add_console_app(MTControlsPortableFlacTests PRODUCT_NAME "Portable FLAC Tests")
    target_sources(MTControlsPortableFlacTests PRIVATE
        tests/portable_flac_conversion_tests.cpp src/data/PortableFlacConversion.cpp)
    target_include_directories(MTControlsPortableFlacTests PRIVATE src)
    target_compile_definitions(MTControlsPortableFlacTests PRIVATE
        JUCE_USE_MP3AUDIOFORMAT=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsPortableFlacTests PRIVATE juce::juce_audio_formats)
    add_test(NAME portable_flac_conversion COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsPortableFlacTests>")
    juce_add_console_app(MTControlsImportStagingTests PRODUCT_NAME "Import Staging Tests")
    target_sources(MTControlsImportStagingTests PRIVATE
        tests/mobile_import_staging_tests.cpp src/data/MobileImportStaging.cpp)
    target_include_directories(MTControlsImportStagingTests PRIVATE src)
    target_compile_definitions(MTControlsImportStagingTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsImportStagingTests PRIVATE juce::juce_core)
    add_test(NAME mobile_import_staging COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsImportStagingTests>")
    if(MTCONTROLS_MAC_APP_STORE)
        juce_add_console_app(MTControlsMacAppStoreTests PRODUCT_NAME "Mac Store Tests")
        target_sources(MTControlsMacAppStoreTests PRIVATE
            tests/mac_app_store_tests.cpp
            src/data/AssetBootstrapManager.cpp src/data/AppSettings.cpp
            src/data/SongData.cpp
            src/data/MobileImportStaging.cpp src/data/PortableFlacConversion.cpp
            src/telemetry/Telemetry.cpp)
        target_include_directories(MTControlsMacAppStoreTests PRIVATE src)
        target_compile_definitions(MTControlsMacAppStoreTests PRIVATE
            MTCONTROLS_MAC_APP_STORE=1 MTCONTROLS_SENTRY_ENABLED=0
            MTCONTROLS_ISOLATED_STORAGE_TESTS=1 JUCE_USE_MP3AUDIOFORMAT=1)
        target_link_libraries(MTControlsMacAppStoreTests PRIVATE
            juce::juce_audio_formats juce::juce_graphics)
        juce_generate_juce_header(MTControlsMacAppStoreTests)
        add_test(NAME mac_app_store_safety COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
            "$<TARGET_FILE:MTControlsMacAppStoreTests>")
    endif()
    juce_add_console_app(MTControlsSongCreationTests PRODUCT_NAME "Song Creation Tests")
    target_sources(MTControlsSongCreationTests PRIVATE tests/song_project_creation_tests.cpp
        src/data/SongLibraryStore.cpp src/data/AppSettings.cpp src/data/SessionAuthLogger.cpp)
    target_compile_definitions(MTControlsSongCreationTests PRIVATE MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_include_directories(MTControlsSongCreationTests PRIVATE src)
    target_link_libraries(MTControlsSongCreationTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    juce_generate_juce_header(MTControlsSongCreationTests)
    add_test(NAME song_project_creation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSongCreationTests>")
    juce_add_console_app(MTControlsSongWizardTests PRODUCT_NAME "Song Wizard Tests")
    target_sources(MTControlsSongWizardTests PRIVATE tests/song_import_wizard_tests.cpp
        src/data/SongLibraryStore.cpp src/data/AppSettings.cpp src/data/SessionAuthLogger.cpp)
    target_include_directories(MTControlsSongWizardTests PRIVATE src)
    target_compile_definitions(MTControlsSongWizardTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsSongWizardTests PRIVATE MTControls MTControlsAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
    juce_generate_juce_header(MTControlsSongWizardTests)
    add_test(NAME song_import_wizard COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSongWizardTests>")
    juce_add_console_app(MTControlsPracticePanelGestureTests
        PRODUCT_NAME "Practice Panel Gesture Tests")
    target_sources(MTControlsPracticePanelGestureTests PRIVATE
        tests/practice_panel_gesture_tests.cpp
        tests/practice_waveform_geometry_tests.cpp
        tests/practice_playback_clock_tests.cpp
        tests/practice_viewport_tests.cpp
        tests/practice_loop_tests.cpp
        src/data/AppSettings.cpp)
    target_include_directories(MTControlsPracticePanelGestureTests PRIVATE src)
    target_compile_definitions(MTControlsPracticePanelGestureTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsPracticePanelGestureTests PRIVATE
        MTControls MTControlsAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_cryptography)
    juce_generate_juce_header(MTControlsPracticePanelGestureTests)
    add_test(NAME practice_panel_gestures COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsPracticePanelGestureTests>")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(practice_panel_gestures PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    juce_add_console_app(MTControlsWaveformEditingTests
        PRODUCT_NAME "Waveform Editing Tests")
    target_sources(MTControlsWaveformEditingTests PRIVATE
        tests/waveform_editing_tests.cpp src/data/AppSettings.cpp
        src/data/SongLibraryStore.cpp src/data/SessionAuthLogger.cpp
        src/data/AssetBootstrapManager.cpp)
    target_include_directories(MTControlsWaveformEditingTests PRIVATE src)
    target_compile_definitions(MTControlsWaveformEditingTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsWaveformEditingTests PRIVATE
        MTControls MTControlsAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_cryptography)
    juce_generate_juce_header(MTControlsWaveformEditingTests)
    add_test(NAME waveform_editing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsWaveformEditingTests>")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(waveform_editing PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    juce_add_console_app(MTControlsSectionAutomationTests
        PRODUCT_NAME "Section Automation Tests")
    target_sources(MTControlsSectionAutomationTests PRIVATE
        tests/section_automation_tests.cpp src/data/AppSettings.cpp
        src/data/SongLibraryStore.cpp src/data/SessionAuthLogger.cpp
        src/data/AssetBootstrapManager.cpp)
    target_include_directories(MTControlsSectionAutomationTests PRIVATE src)
    target_compile_definitions(MTControlsSectionAutomationTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsSectionAutomationTests PRIVATE
        MTControls MTControlsAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_cryptography)
    juce_generate_juce_header(MTControlsSectionAutomationTests)
    add_test(NAME section_automation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSectionAutomationTests>")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(section_automation PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_test(NAME blank_song_transport COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSectionAutomationTests>" --blank-only)
    add_test(NAME metronome_transport COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSectionAutomationTests>" --click-only)

    juce_add_console_app(MTControlsDiagnosticReportTests PRODUCT_NAME "Playback Diagnostic Report Tests")
    target_sources(MTControlsDiagnosticReportTests PRIVATE tests/playback_diagnostic_report_tests.cpp)
    juce_generate_juce_header(MTControlsDiagnosticReportTests)
    target_include_directories(MTControlsDiagnosticReportTests PRIVATE src)
    target_compile_definitions(MTControlsDiagnosticReportTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsDiagnosticReportTests
        PRIVATE juce::juce_core juce::juce_audio_basics)
    add_test(NAME playback_diagnostic_report COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsDiagnosticReportTests>")
    add_executable(MTControlsPracticeAccompanimentTests tests/practice_accompaniment_tests.cpp)
    target_include_directories(MTControlsPracticeAccompanimentTests PRIVATE src)
    add_test(NAME practice_accompaniment COMMAND MTControlsPracticeAccompanimentTests)
    add_executable(MTControlsPracticeToggleRequestTests tests/practice_toggle_request_tests.cpp)
    target_include_directories(MTControlsPracticeToggleRequestTests PRIVATE src)
    add_test(NAME practice_toggle_request COMMAND MTControlsPracticeToggleRequestTests)
    add_executable(MTControlsTouchWaveformNavigationTests tests/touch_waveform_navigation_tests.cpp)
    target_include_directories(MTControlsTouchWaveformNavigationTests PRIVATE src)
    add_test(NAME touch_waveform_navigation COMMAND MTControlsTouchWaveformNavigationTests)
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

    add_executable(MTControlsPracticeWaveformMappingTests
        tests/practice_waveform_mapping_tests.cpp
        src/audio/SectionScheduler.cpp)
    target_include_directories(MTControlsPracticeWaveformMappingTests PRIVATE src)
    add_test(NAME practice_waveform_mapping COMMAND MTControlsPracticeWaveformMappingTests)

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
    target_compile_definitions(MTControlsPadsEngineTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsPadsEngineTests
        PRIVATE juce::juce_audio_utils)
    add_test(NAME pads_engine COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsPadsEngineTests>")

    juce_add_console_app(MTControlsSongMixExportTimelineTests
        PRODUCT_NAME "MTControls Song Mix Export Timeline Tests")
    target_sources(MTControlsSongMixExportTimelineTests PRIVATE
        tests/song_mix_export_timeline_tests.cpp
        src/audio/SongMixExporter.h
        src/audio/DynamicGuideEngine.cpp
        src/audio/DynamicGuideEngine.h
        src/audio/DynamicGuideTiming.h
        src/audio/DynamicGuideEvents.h
        src/data/SongData.cpp
    )
    juce_generate_juce_header(MTControlsSongMixExportTimelineTests)
    target_include_directories(MTControlsSongMixExportTimelineTests PRIVATE src)
    target_compile_definitions(MTControlsSongMixExportTimelineTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsSongMixExportTimelineTests
        PRIVATE juce::juce_audio_utils)
    add_test(NAME song_mix_export_timeline
        COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSongMixExportTimelineTests>")

    juce_add_console_app(MTControlsSongMixExporterTests
        PRODUCT_NAME "Song Mix Exporter Tests")
    target_sources(MTControlsSongMixExporterTests PRIVATE
        tests/song_mix_exporter_tests.cpp src/data/AssetBootstrapManager.cpp
        src/data/AppSettings.cpp src/data/SessionAuthLogger.cpp)
    target_compile_definitions(MTControlsSongMixExporterTests PRIVATE
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    juce_generate_juce_header(MTControlsSongMixExporterTests)
    target_include_directories(MTControlsSongMixExporterTests PRIVATE src)
    target_link_libraries(MTControlsSongMixExporterTests PRIVATE
        MTControls MTControlsAssets juce::juce_audio_utils juce::juce_dsp)
    add_test(NAME song_mix_exporter COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsSongMixExporterTests>")

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

    juce_add_console_app(MTControlsStandaloneAudioRuntimeTests
        PRODUCT_NAME "Standalone Audio Runtime Tests")
    target_sources(MTControlsStandaloneAudioRuntimeTests PRIVATE
        tests/standalone_audio_runtime_tests.cpp
        src/data/AppSettings.cpp src/data/SongLibraryStore.cpp
        src/data/SessionAuthLogger.cpp src/data/AssetBootstrapManager.cpp)
    target_include_directories(MTControlsStandaloneAudioRuntimeTests PRIVATE src)
    target_compile_definitions(MTControlsStandaloneAudioRuntimeTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
    target_link_libraries(MTControlsStandaloneAudioRuntimeTests PRIVATE
        MTControls MTControlsAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_cryptography)
    juce_generate_juce_header(MTControlsStandaloneAudioRuntimeTests)
    add_test(NAME standalone_audio_runtime COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
        "$<TARGET_FILE:MTControlsStandaloneAudioRuntimeTests>")

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
        juce_add_console_app(MTControlsFreeShowProtocolTests PRODUCT_NAME "FreeShow Protocol Tests")
        target_sources(MTControlsFreeShowProtocolTests PRIVATE tests/freeshow_protocol_tests.cpp
            src/integrations/FreeShowClient.cpp)
        target_include_directories(MTControlsFreeShowProtocolTests PRIVATE src)
        target_link_libraries(MTControlsFreeShowProtocolTests PRIVATE PlayWorshipLyricsModel)
        add_test(NAME freeshow_protocol COMMAND "$<TARGET_FILE:MTControlsFreeShowProtocolTests>")

        juce_add_console_app(MTControlsLyricsCueTests PRODUCT_NAME "Lyrics Cue Scheduler Tests")
        target_sources(MTControlsLyricsCueTests PRIVATE tests/lyrics_cue_scheduler_tests.cpp)
        target_include_directories(MTControlsLyricsCueTests PRIVATE src)
        target_link_libraries(MTControlsLyricsCueTests PRIVATE juce::juce_core)
        add_test(NAME lyrics_cue_scheduler COMMAND "$<TARGET_FILE:MTControlsLyricsCueTests>")

        juce_add_console_app(MTControlsLyricsPersistenceTests PRODUCT_NAME "Lyrics Persistence Tests")
        target_sources(MTControlsLyricsPersistenceTests PRIVATE tests/lyrics_persistence_tests.cpp
            src/data/SongLibraryStore.cpp src/data/AppSettings.cpp src/data/SessionAuthLogger.cpp)
        target_include_directories(MTControlsLyricsPersistenceTests PRIVATE src)
        target_compile_definitions(MTControlsLyricsPersistenceTests PRIVATE
            JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
        target_link_libraries(MTControlsLyricsPersistenceTests PRIVATE MTControls MTControlsAssets
            juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
        juce_generate_juce_header(MTControlsLyricsPersistenceTests)
        add_test(NAME lyrics_persistence COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
            "$<TARGET_FILE:MTControlsLyricsPersistenceTests>")

        foreach(_lyrics_case IN ITEMS Panel Processor)
            string(TOLOWER "${_lyrics_case}" _lyrics_file)
            juce_add_console_app(MTControlsLyrics${_lyrics_case}Tests
                PRODUCT_NAME "Lyrics ${_lyrics_case} Tests")
            target_sources(MTControlsLyrics${_lyrics_case}Tests PRIVATE
                tests/lyrics_${_lyrics_file}_tests.cpp
                src/data/SongLibraryStore.cpp src/data/AppSettings.cpp
                src/data/SessionAuthLogger.cpp src/data/AssetBootstrapManager.cpp)
            target_include_directories(MTControlsLyrics${_lyrics_case}Tests PRIVATE src)
            target_compile_definitions(MTControlsLyrics${_lyrics_case}Tests PRIVATE
                JUCE_MODAL_LOOPS_PERMITTED=1 MTCONTROLS_ISOLATED_STORAGE_TESTS=1)
            target_link_libraries(MTControlsLyrics${_lyrics_case}Tests PRIVATE
                MTControls MTControlsAssets juce::juce_audio_utils juce::juce_dsp
                juce::juce_cryptography)
            juce_generate_juce_header(MTControlsLyrics${_lyrics_case}Tests)
            add_test(NAME lyrics_${_lyrics_file} COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_isolated_test.py"
                "$<TARGET_FILE:MTControlsLyrics${_lyrics_case}Tests>")
        endforeach()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set_tests_properties(lyrics_panel PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endif()

    # Build only the regression executables in CI; this avoids packaging and
    # signing the standalone app while still building MTControls where needed.
    get_property(_mtcontrols_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(_mtcontrols_test_targets)
    foreach(_mtcontrols_target IN LISTS _mtcontrols_directory_targets)
        if(_mtcontrols_target MATCHES "^MTControls.*Tests$")
            # Match the application's no-embedded-browser build. In particular,
            # Linux tests must not acquire an unused GTK/WebKit dependency.
            target_compile_definitions(${_mtcontrols_target} PRIVATE
                JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
            target_link_libraries(${_mtcontrols_target} PRIVATE PlayWorshipLyricsModel)
            list(APPEND _mtcontrols_test_targets "${_mtcontrols_target}")
        endif()
    endforeach()
    add_custom_target(PlayWorshipTests DEPENDS ${_mtcontrols_test_targets})
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
set(_mtcontrols_mac_sandbox_options "")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Selected .pwproj files are copied into the app's library before opening
    # (SongListPanel::importProjectUrlToSandbox); external files are not edited
    # in place and the app does not implement UIDocumentBrowserViewController.
    set(MTCONTROLS_PLIST_TO_MERGE
        "<plist><dict><key>CFBundleDevelopmentRegion</key><string>en</string><key>CFBundleInfoDictionaryVersion</key><string>6.0</string><key>CFBundleVersion</key><string>${MTCONTROLS_IOS_BUILD_NUMBER}</string><key>CFBundleURLTypes</key><array><dict><key>CFBundleURLName</key><string>com.multitracks.playworship.auth</string><key>CFBundleURLSchemes</key><array><string>playworship</string></array></dict></array><key>ITSAppUsesNonExemptEncryption</key><false/><key>LSSupportsOpeningDocumentsInPlace</key><false/><key>LSApplicationCategoryType</key><string>public.app-category.music</string><key>NSBonjourServices</key><array><string>_playworship._tcp</string></array><key>NSLocalNetworkUsageDescription</key><string>PlayWorship descubre y controla sesiones en tu red local.</string><key>NSAppTransportSecurity</key><dict><key>NSAllowsLocalNetworking</key><true/></dict><key>UIRequiresFullScreen</key><false/></dict></plist>")
elseif(MTCONTROLS_MAC_APP_STORE)
    set(MTCONTROLS_PLIST_TO_MERGE
        "<plist><dict><key>CFBundleVersion</key><string>${MTCONTROLS_MAC_BUILD_NUMBER}</string><key>CFBundleURLTypes</key><array><dict><key>CFBundleURLName</key><string>com.multitracks.playworship.auth</string><key>CFBundleURLSchemes</key><array><string>playworship</string></array></dict></array><key>ITSAppUsesNonExemptEncryption</key><false/><key>LSApplicationCategoryType</key><string>public.app-category.music</string><key>NSBonjourServices</key><array><string>_playworship._tcp</string></array><key>NSLocalNetworkUsageDescription</key><string>PlayWorship descubre y controla sesiones en tu red local.</string></dict></plist>")
    # Core MIDI/Bluetooth, local remote sessions, HTTPS, the desktop OAuth
    # loopback server and explicit Open/Save panels. No broad filesystem or
    # microphone entitlement is needed; persisted imports are private copies.
    set(_mtcontrols_mac_sandbox_options
        com.apple.security.network.client
        com.apple.security.network.server
        com.apple.security.device.usb
        com.apple.security.device.bluetooth
        com.apple.security.files.user-selected.read-write)
endif()

add_library(MTControls STATIC)

# Native application; MTControls is the reusable engine/UI core for tests.
juce_add_gui_app(MTControls_Standalone
    COMPANY_NAME "Multitracks"
    VERSION "${MTCONTROLS_APP_VERSION}"
    PRODUCT_NAME "PlayWorship"
    ICON_BIG "${MTCONTROLS_APP_ICON_PATH}"
    ICON_SMALL "${MTCONTROLS_APP_ICON_PATH}"
    LAUNCH_STORYBOARD_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/ios/LaunchScreen.storyboard"
    PLIST_TO_MERGE "${MTCONTROLS_PLIST_TO_MERGE}"
    DOCUMENT_EXTENSIONS pwproj
    BUNDLE_ID "com.multitracks.playworship"
    # iPad follows its scene's actual size in every orientation. iPhone keeps
    # its contextual portrait/landscape mask for authentication and panels.
    IPAD_SCREEN_ORIENTATIONS
        UIInterfaceOrientationLandscapeLeft
        UIInterfaceOrientationLandscapeRight
        UIInterfaceOrientationPortrait
        UIInterfaceOrientationPortraitUpsideDown
    IPHONE_SCREEN_ORIENTATIONS
        UIInterfaceOrientationLandscapeLeft
        UIInterfaceOrientationLandscapeRight
        UIInterfaceOrientationPortrait
    BACKGROUND_AUDIO_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "PlayWorship uses Bluetooth to discover and pair Bluetooth MIDI devices."
    LOCAL_NETWORK_PERMISSION_ENABLED TRUE
    LOCAL_NETWORK_PERMISSION_TEXT "PlayWorship connects to compatible music and presentation services on your local network."
    APP_SANDBOX_ENABLED "${MTCONTROLS_MAC_APP_STORE}"
    APP_SANDBOX_OPTIONS ${_mtcontrols_mac_sandbox_options}
)

if(MTCONTROLS_MAC_APP_STORE)
    set_target_properties(MTControls_Standalone PROPERTIES
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MTCONTROLS_MAC_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
        XCODE_ATTRIBUTE_ENABLE_APP_SANDBOX "YES"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${MTCONTROLS_APP_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${MTCONTROLS_MAC_BUILD_NUMBER}")
    set(_mtcontrols_mac_privacy_manifest
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/ios/PrivacyInfo.xcprivacy")
    set_source_files_properties("${_mtcontrols_mac_privacy_manifest}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(MTControls_Standalone PRIVATE "${_mtcontrols_mac_privacy_manifest}")
    foreach(symbol_target IN ITEMS MTControls MTControls_Standalone)
        target_compile_options(${symbol_target} PRIVATE "$<$<CONFIG:Release>:-g>")
        set_target_properties(${symbol_target} PROPERTIES
            XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT
            "$<IF:$<CONFIG:Release>,dwarf-with-dsym,dwarf>")
    endforeach()
endif()

if(MTCONTROLS_APPLE_SIGN_IN_ENABLED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    get_target_property(_apple_base_entitlements MTControls_Standalone
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS)
    if(NOT EXISTS "${_apple_base_entitlements}")
        message(FATAL_ERROR "Apple sign-in requires generated JUCE signing entitlements")
    endif()
    set(_apple_merged_entitlements
        "${CMAKE_CURRENT_BINARY_DIR}/MTControls_Standalone-Apple.entitlements")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/app-store/merge_apple_entitlements.py"
        "${_apple_base_entitlements}" "${_apple_merged_entitlements}"
        COMMAND_ERROR_IS_FATAL ANY)
    set_target_properties(MTControls_Standalone PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${_apple_merged_entitlements}")
endif()

# iOS / iPadOS: device family belongs to the native application.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(MTControls_Standalone PROPERTIES
        JUCE_TARGETED_DEVICE_FAMILY "${MTCONTROLS_IOS_TARGETED_DEVICE_FAMILY}"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "18.5")
    # Release archives need debug information from both the shared JUCE code
    # and the standalone wrapper to produce a useful, matching app dSYM.
    foreach(symbol_target IN ITEMS MTControls MTControls_Standalone)
        if(TARGET ${symbol_target})
            target_compile_options(${symbol_target} PRIVATE
                "$<$<CONFIG:Release>:-g>")
            set_target_properties(${symbol_target} PROPERTIES
                XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT
                "$<IF:$<CONFIG:Release>,dwarf-with-dsym,dwarf>")
        endif()
    endforeach()
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

# Keep installer/CI paths stable while removing the audio-plugin wrapper.
set_target_properties(MTControls_Standalone PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/MTControls_artefacts/$<CONFIG>/Standalone"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/MTControls_artefacts/$<CONFIG>/Standalone")
target_sources(MTControls_Standalone PRIVATE src/StandaloneApp.cpp)
target_include_directories(MTControls_Standalone PRIVATE
    src $<TARGET_PROPERTY:MTControls,INCLUDE_DIRECTORIES>)
target_link_libraries(MTControls_Standalone PRIVATE
    MTControls juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp)
# The app also compiles JUCE modules. Keep their feature switches identical to
# the static core (notably Windows ASIO, iOS sharing and MP3 decoding).
target_compile_definitions(MTControls_Standalone PRIVATE
    $<TARGET_PROPERTY:MTControls,COMPILE_DEFINITIONS>)
juce_generate_juce_header(MTControls_Standalone)
get_target_property(_mtcontrols_generated_sources MTControls_Standalone
    JUCE_GENERATED_SOURCES_DIRECTORY)
target_include_directories(MTControls PUBLIC "${_mtcontrols_generated_sources}")
target_sources(MTControls PRIVATE "${_mtcontrols_generated_sources}/JuceHeader.h")
target_compile_definitions(MTControls PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    MTCONTROLS_NATIVE_APP=1
    MTCONTROLS_APP_VERSION="${MTCONTROLS_APP_VERSION}")

include(cmake/PlayWorshipAssets.cmake)
juce_add_binary_data(MTControlsAssets SOURCES ${MTCONTROLS_ASSET_FILES})

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
        src/RemoteEditorIntegration.cpp
        src/TeamsIntegration.cpp
        src/PluginEditorSupport.cpp
        src/PluginEditorSupport.h
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/network/LanDiscovery.cpp
        src/network/LanDiscovery.h
        src/network/RemoteDeviceName.h
        src/network/RemoteSession.cpp
        src/network/RemoteSession.h
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
        src/audio/MidiBindingTrigger.h
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
        src/data/AccountDeletionApi.cpp
        src/data/AuthSessionApi.cpp
        src/data/AuthSessionApi.h
        src/data/EmailPasswordAuthApi.cpp
        src/data/EmailPasswordAuthApi.h
        src/EmailPasswordAuthIntegration.cpp
        src/LocalDemoAccessIntegration.cpp
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
        src/data/TeamsClient.cpp
        src/data/TeamsClient.h
        src/data/TeamsLocalStore.cpp
        src/data/TeamsLocalStore.h
        src/data/TeamsPreparationService.cpp
        src/data/TeamsPreparationService.h
        src/data/LocalAuthCallbackServer.cpp
        src/data/LocalAuthCallbackServer.h
        src/data/SessionAuthLogger.cpp
        src/data/SessionAuthLogger.h
        src/data/SessionPersistence.cpp
        src/data/SessionPersistence.h
        src/data/SecuenciasClient.cpp
        src/data/SecuenciasClient.h
        src/data/Localization.h
        src/data/SongProjectCreation.cpp
        src/data/SongProjectCreation.h
        src/data/ProjectStore.cpp
        src/data/ProjectStore.h
        src/data/SongData.cpp
        src/data/SongData.h
        src/data/SongLibraryStore.cpp
        src/data/PortableFlacConversion.cpp
        src/data/PortableFlacConversion.h
        src/data/MobileImportStaging.cpp
        src/data/MobileImportStaging.h
        src/data/SongLibraryStore.h
        src/LearningGuideIntegration.cpp
        src/DemoSongIntegration.cpp
        src/data/DemoSongService.cpp
        src/data/DemoSongService.h
        src/ui/learning/LearningGuideModel.cpp
        src/ui/learning/LearningGuideModel.h
        src/ui/learning/LearningGuideOverlay.cpp
        src/ui/learning/LearningGuideOverlay.h
        src/ui/DeviceNotificationBanner.cpp
        src/ui/DeviceNotificationBanner.h
        src/ui/UiCommon.cpp
        src/ui/UiCommon.h
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
        src/ui/panels/TeamsPanel.cpp
        src/ui/panels/TeamsPanel.h
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
        src/ui/components/AccountDeletionModal.cpp
        src/ui/components/ReauthPromptModal.cpp
        src/ui/components/AppModal.cpp
        src/ui/components/AppModal.h
        src/ui/components/SettingsVersionsSheet.cpp
        src/ui/components/SettingsVersionsSheet.h
        src/ui/components/ReauthPromptModal.h
        src/ui/panels/PadsComponent.cpp
        src/ui/panels/PadsComponent.h
        src/ui/panels/PracticePanel.cpp
        src/ui/panels/PracticePanel.h
        src/ui/components/WelcomeModal.cpp
        src/ui/components/WelcomeModal.h
        src/ui/components/AuthenticationStyle.h
)

if(APPLE)
    target_sources(MTControls PRIVATE
        src/data/CloudSignedUpload.mm
        src/network/RemoteDeviceName.mm
        src/platform/NativeAppleSignIn.mm)
    target_link_libraries(MTControls PRIVATE "-framework AuthenticationServices")
else()
    target_sources(MTControls PRIVATE
        src/data/CloudSignedUpload.cpp
        src/network/RemoteDeviceName.cpp)
endif()

target_sources(MTControls PRIVATE
    src/AppleSignInIntegration.cpp
    src/data/AppleSignInApi.cpp src/data/AppleSignInApi.h
    src/platform/NativeAppleSignIn.cpp src/platform/NativeAppleSignIn.h)

target_link_libraries(MTControls PUBLIC PlayWorshipLyricsModel)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    target_sources(MTControls PRIVATE
        src/integrations/FreeShowClient.cpp
        src/ui/panels/LyricsPanel.cpp)
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_link_libraries(MTControls PRIVATE "-framework SystemConfiguration")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(MTControls PRIVATE
        src/platform/IOSWindowLayout.mm
        src/platform/IOSWindowLayout.h
        src/platform/NativeAuthSession.mm
        src/platform/NativeAuthSession.h)
    target_link_libraries(MTControls PRIVATE "-framework AuthenticationServices")
else()
    target_sources(MTControls PRIVATE
        src/platform/NativeAuthSession.cpp
        src/platform/NativeAuthSession.h)
endif()

if(MTCONTROLS_BUNGEE_AVAILABLE)
    target_sources(MTControls PRIVATE
        src/audio/BungeeProcessor.cpp
        src/audio/BungeeProcessor.h)
endif()

# Allow subdirectories under src/ (e.g. src/data/ProjectStore.cpp) to
# include headers from the top-level src/ directory (e.g. SongLoadProbe.h).
target_include_directories(MTControls PRIVATE src)
target_compile_definitions(MTControls PUBLIC
    MTCONTROLS_MAC_APP_STORE=$<BOOL:${MTCONTROLS_MAC_APP_STORE}>
    MTCONTROLS_APPLE_SIGN_IN_ENABLED=$<BOOL:${MTCONTROLS_APPLE_SIGN_IN_ENABLED}>)

target_compile_definitions(MTControls
    PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        MTCONTROLS_API_BASE_URL=\"${MTCONTROLS_RESOLVED_API_BASE_URL}\"
        MTCONTROLS_AUTH_BASE_URL=\"${MTCONTROLS_AUTH_BASE_URL}\"
        MTCONTROLS_ASSETS_BASE_URL=\"${MTCONTROLS_ASSETS_BASE_URL}\"
        MTCONTROLS_SESSION_REAUTH_MINUTES=${MTCONTROLS_SESSION_REAUTH_MINUTES}
        MTCONTROLS_REAUTH_GRACE_MINUTES=${MTCONTROLS_REAUTH_GRACE_MINUTES}
        MTCONTROLS_MAX_OFFLINE_HOURS=${MTCONTROLS_MAX_OFFLINE_HOURS}
        MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS=$<BOOL:${MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS}>
        MTCONTROLS_SENTRY_ENABLED=$<BOOL:${MTCONTROLS_SENTRY_ENABLED}>
        MTCONTROLS_SENTRY_DSN="${MTCONTROLS_SENTRY_DSN}"
        MTCONTROLS_SENTRY_ENVIRONMENT="${MTCONTROLS_SENTRY_ENVIRONMENT}"
        MTCONTROLS_SENTRY_RELEASE="playworship-app@${MTCONTROLS_APP_VERSION}+${MTCONTROLS_IOS_BUILD_NUMBER}.${MTCONTROLS_GIT_COMMIT}"
    PUBLIC
        # JUCE modules must share codec configuration with their consumers.
        JUCE_USE_MP3AUDIOFORMAT=1
        # Public processor headers must use the same audio backend and sample
        # type as the library, including when compiled by regression tests.
        SOUNDTOUCH_FLOAT_SAMPLES=1
        MTCONTROLS_USE_BUNGEE=$<BOOL:${MTCONTROLS_BUNGEE_AVAILABLE}>
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
        ixwebsocket::ixwebsocket
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
    elseif(MTCONTROLS_MAC_APP_STORE)
        # Xcode embeds and signs the framework before signing the app. Keep
        # its original install name; the normal desktop dylib/ad-hoc path is
        # unsuitable for distribution and is intentionally a separate branch.
        # The vendor plist lacks CFBundleIdentifier. Prepare a private copy
        # before Xcode's Embed Frameworks/CodeSign steps; never mutate the SDK.
        set(_mtcontrols_mac_framework_stage
            "${CMAKE_CURRENT_BINARY_DIR}/mac-app-store-frameworks")
        file(MAKE_DIRECTORY "${_mtcontrols_mac_framework_stage}")
        file(COPY "${_mtcontrols_bungee_framework}"
            DESTINATION "${_mtcontrols_mac_framework_stage}")
        set(_mtcontrols_mac_bungee_framework
            "${_mtcontrols_mac_framework_stage}/bungee.framework")
        execute_process(COMMAND /usr/libexec/PlistBuddy
            -c "Set :CFBundleIdentifier com.zynaptiq.bungee"
            "${_mtcontrols_mac_bungee_framework}/Resources/Info.plist"
            RESULT_VARIABLE _mtcontrols_mac_bungee_plist_status
            ERROR_QUIET)
        if(NOT _mtcontrols_mac_bungee_plist_status EQUAL 0)
            execute_process(COMMAND /usr/libexec/PlistBuddy
                -c "Add :CFBundleIdentifier string com.zynaptiq.bungee"
                "${_mtcontrols_mac_bungee_framework}/Resources/Info.plist"
                RESULT_VARIABLE _mtcontrols_mac_bungee_plist_status)
        endif()
        if(NOT _mtcontrols_mac_bungee_plist_status EQUAL 0)
            message(FATAL_ERROR "Could not prepare the Mac App Store Bungee framework plist")
        endif()
        set_target_properties(MTControls_Standalone PROPERTIES
            BUILD_RPATH "@executable_path/../Frameworks"
            INSTALL_RPATH "@executable_path/../Frameworks"
            XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/../Frameworks"
            XCODE_EMBED_FRAMEWORKS "${_mtcontrols_mac_bungee_framework}"
            XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
            XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES)
        set_source_files_properties("${MTCONTROLS_BUNGEE_DIR}/LICENSE"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/ThirdPartyLicenses")
        target_sources(MTControls_Standalone PRIVATE "${MTCONTROLS_BUNGEE_DIR}/LICENSE")
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
    target_include_directories(MTControls SYSTEM PUBLIC ${soundtouch_SOURCE_DIR}/include)
elseif (TARGET soundtouch)
    target_compile_definitions(soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)
    if (MSVC)
        target_compile_options(soundtouch PRIVATE /w)
    else()
        target_compile_options(soundtouch PRIVATE -w)
    endif()
    target_link_libraries(MTControls PRIVATE soundtouch)
    target_include_directories(MTControls SYSTEM PUBLIC ${soundtouch_SOURCE_DIR}/include)
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
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT MTCONTROLS_MAC_APP_STORE)
    add_custom_command(TARGET MTControls_Standalone POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --sign -
            "$<TARGET_BUNDLE_DIR:MTControls_Standalone>"
        VERBATIM)
endif()
