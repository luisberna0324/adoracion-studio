cmake_minimum_required(VERSION 3.22)
project(PlayWorshipAndroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../")
set(JUCE_ROOT "${PROJECT_ROOT}/../third_party/JUCE")
set(SOUNDTOUCH_ROOT "${PROJECT_ROOT}/build-local-backend/_deps/soundtouch-src")

file(GLOB_RECURSE PLAYWORSHIP_SOURCES
    "${PROJECT_ROOT}/src/*.cpp")
list(FILTER PLAYWORSHIP_SOURCES EXCLUDE REGEX "/StandaloneApp\\.cpp$")
# Android uses the bundled Bungee backend for pitch and time stretching.
# Keep the legacy SoundTouch processor out of this target so Android does not
# depend on the local desktop-only SoundTouch checkout.
list(FILTER PLAYWORSHIP_SOURCES EXCLUDE REGEX "/PitchShiftProcessor\\.cpp$")
file(GLOB BINARY_DATA_SOURCES "${CMAKE_CURRENT_LIST_DIR}/BinaryData*.cpp")
file(GLOB SHEENBIDI_SOURCES "${JUCE_ROOT}/modules/juce_graphics/unicode/sheenbidi/Source/*.c")
file(GLOB_RECURSE OBOE_SOURCES "${JUCE_ROOT}/modules/juce_audio_devices/native/oboe/src/*.cpp")
add_library(playworship SHARED ${PLAYWORSHIP_SOURCES} ${BINARY_DATA_SOURCES} ${SHEENBIDI_SOURCES} ${OBOE_SOURCES}
    "${JUCE_ROOT}/modules/juce_core/juce_core.cpp"
    "${JUCE_ROOT}/modules/juce_core/juce_core_CompilationTime.cpp"
    "${JUCE_ROOT}/modules/juce_data_structures/juce_data_structures.cpp"
    "${JUCE_ROOT}/modules/juce_events/juce_events.cpp"
    "${JUCE_ROOT}/modules/juce_graphics/juce_graphics.cpp"
    "${JUCE_ROOT}/modules/juce_graphics/juce_graphics_Harfbuzz.cpp"
    "${JUCE_ROOT}/modules/juce_gui_basics/juce_gui_basics.cpp"
    "${JUCE_ROOT}/modules/juce_gui_extra/juce_gui_extra.cpp"
    "${JUCE_ROOT}/modules/juce_opengl/juce_opengl.cpp"
    "${JUCE_ROOT}/modules/juce_audio_basics/juce_audio_basics.cpp"
    "${JUCE_ROOT}/modules/juce_audio_devices/juce_audio_devices.cpp"
    "${JUCE_ROOT}/modules/juce_audio_formats/juce_audio_formats.cpp"
    "${JUCE_ROOT}/modules/juce_audio_processors/juce_audio_processors.cpp"
    "${JUCE_ROOT}/modules/juce_audio_processors_headless/juce_audio_processors_headless.cpp"
    "${JUCE_ROOT}/modules/juce_audio_utils/juce_audio_utils.cpp"
    "${JUCE_ROOT}/modules/juce_cryptography/juce_cryptography.cpp"
    "${JUCE_ROOT}/modules/juce_dsp/juce_dsp.cpp"
    "${CMAKE_ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c")
set_target_properties(playworship PROPERTIES OUTPUT_NAME "juce_jni")

# The routinely installed APK is assembleDebug. Unoptimised native UI rendering
# and audio DSP make even simple interactions stall. Keep debug symbols and the
# debuggable APK while optimising native code. Disable only for
# instruction-by-instruction debugging.
option(PLAYWORSHIP_ANDROID_OPTIMIZE_DEBUG "Optimise native Android debug builds" ON)
if(PLAYWORSHIP_ANDROID_OPTIMIZE_DEBUG)
    target_compile_options(playworship PRIVATE "$<$<CONFIG:Debug>:-O2>")
endif()

target_include_directories(playworship PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${PROJECT_ROOT}/src"
    "${JUCE_ROOT}/modules"
    "${JUCE_ROOT}/modules/juce_audio_devices/native/oboe/include"
    "${JUCE_ROOT}/modules/juce_audio_devices/native/oboe/src"
    "${PROJECT_ROOT}/third_party/bungee/include")
target_include_directories(playworship PRIVATE "${CMAKE_ANDROID_NDK}/sources/android/cpufeatures")

target_compile_definitions(playworship PRIVATE
    JUCE_ANDROID=1 JUCE_STANDALONE_APPLICATION=1
    MTCONTROLS_USE_BUNGEE=1
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_ANDROID_OBOE=1 JUCE_USE_ANDROID_OPENSLES=0
    JUCE_MODULE_AVAILABLE_juce_events=1
    JUCE_MODULE_AVAILABLE_juce_opengl=1
    JUCE_MODULE_AVAILABLE_juce_audio_devices=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_ANDROID_ACTIVITY_CLASSNAME=com/rmsl/juce/JuceActivity
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LV2=0)

add_library(bungee SHARED IMPORTED)
set_target_properties(bungee PROPERTIES IMPORTED_LOCATION
    "${PROJECT_ROOT}/third_party/bungee/android-arm64-v8a/libbungee.so")

find_library(log-lib log)
find_library(android-lib android)
find_library(opensles-lib OpenSLES)
find_library(aaudio-lib aaudio)
target_link_libraries(playworship PRIVATE bungee ${log-lib} ${android-lib} ${opensles-lib} ${aaudio-lib} EGL GLESv2 z)
