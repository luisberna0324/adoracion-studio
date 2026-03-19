cmake_minimum_required(VERSION 3.22)

project(MTControls VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "JUCE_DIR is not set. Configure with: cmake -S . -B build -DJUCE_DIR=/path/to/JUCE")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(MTControls
    COMPANY_NAME "Multitracks"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE MLTK
    PLUGIN_CODE Mtc1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Adoracion Estudio"
)

juce_generate_juce_header(MTControls)

target_sources(MTControls
    PRIVATE
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/ResamplingPositionableSource.cpp
        src/ResamplingPositionableSource.h
        src/StemTrack.cpp
        src/StemTrack.h
        src/StemBufferingSource.cpp
        src/StemBufferingSource.h
        src/data/SongData.cpp
        src/data/SongData.h
        src/ui/UiCommon.cpp
        src/ui/UiCommon.h
        src/ui/panels/ChannelGridPanel.cpp
        src/ui/panels/ChannelGridPanel.h
        src/ui/panels/MixerFaderPanel.cpp
        src/ui/panels/MixerFaderPanel.h
        src/ui/panels/TopBar.cpp
        src/ui/panels/TopBar.h
        src/ui/panels/WaveformBar.cpp
        src/ui/panels/WaveformBar.h
)

target_compile_definitions(MTControls
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MTControls
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
target_sources(MTControls PRIVATE src/WaveformRowComponent.cpp)
