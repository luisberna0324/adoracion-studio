#pragma once

#define JucePlugin_Name "PlayWorship"
#define JucePlugin_VersionString "1.1.4"
#define JucePlugin_Manufacturer "Multitracks"
#define JucePlugin_ManufacturerCode 0x4d4c544b
#define JucePlugin_PluginCode 0x4d746331
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0
#define JucePlugin_EditorRequiresKeyboardFocus 0
#define JucePlugin_MaxNumInputChannels 2
#define JucePlugin_MaxNumOutputChannels 2
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_cryptography/juce_cryptography.h>
