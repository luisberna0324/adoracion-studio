#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

namespace portable_audio {

using ConversionProgressFn = std::function<void(double)>;

// Background-thread conversion for mobile imports. Uses only JUCE's portable
// WAV/AIFF/FLAC/MP3 codecs, with bounded buffers and no external executable.
// The Mac App Store edition additionally decodes M4A via native CoreAudio.
// Integer sources up to 24 bits retain their precision; wider/floating-point
// input is stored as 24-bit PCM (the maximum supported by JUCE's FLAC writer).
// A failed conversion leaves an existing destination untouched.
bool convertToFlac(const juce::File &source, const juce::File &destination,
                   juce::String &errorMessage,
                   const ConversionProgressFn &onProgress = {});

} // namespace portable_audio
