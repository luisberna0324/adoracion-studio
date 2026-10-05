#include "PortableFlacConversion.h"

#include <cmath>

namespace portable_audio {
namespace {

// Keep the file stream alive after the encoder finalizes its header/tail, so
// errors reported during the writer's destructor cannot go unnoticed.
class BorrowedOutputStream final : public juce::OutputStream {
public:
  explicit BorrowedOutputStream(juce::FileOutputStream &stream) : file(stream) {}
  juce::int64 getPosition() override { return file.getPosition(); }
  bool setPosition(juce::int64 position) override {
    return file.setPosition(position);
  }
  bool write(const void *data, size_t size) override {
    return file.write(data, size);
  }
  void flush() override { file.flush(); }

private:
  juce::FileOutputStream &file;
};

} // namespace

bool convertToFlac(const juce::File &source, const juce::File &destination,
                   juce::String &errorMessage,
                   const ConversionProgressFn &onProgress) {
  errorMessage.clear();
  const auto fail = [&errorMessage](const juce::String &message) {
    errorMessage = message;
    return false;
  };
  if (source == destination)
    return fail("FLAC conversion requires a separate destination file.");
  if (!source.existsAsFile())
    return fail("Stem source missing: " + source.getFileName());
#if JUCE_MAC && MTCONTROLS_MAC_APP_STORE
  if (!source.hasFileExtension("wav;aif;aiff;flac;mp3;m4a"))
    return fail("Imports support WAV, AIFF, FLAC, MP3 and native M4A files.");
#else
  if (!source.hasFileExtension("wav;aif;aiff;flac;mp3"))
    return fail("Mobile imports support WAV, AIFF, FLAC and MP3 files.");
#endif

  juce::AudioFormatManager formats;
  formats.registerFormat(new juce::WavAudioFormat(), true);
  formats.registerFormat(new juce::AiffAudioFormat(), false);
  formats.registerFormat(new juce::FlacAudioFormat(), false);
  // MinMP3 lives inside juce_core; JUCE_USE_MP3AUDIOFORMAT=1 is already set
  // in android/app/src/main/cpp/CMakeLists.txt and CMakeLists.txt so the
  // decoder is compiled into the binary even on mobile.
  formats.registerFormat(new juce::MP3AudioFormat(), false);
#if JUCE_MAC && MTCONTROLS_MAC_APP_STORE
  // M4A import stays available in the sandboxed desktop edition. CoreAudio
  // decodes it in process; never fall back to downloading an executable.
  formats.registerFormat(new juce::CoreAudioFormat(), false);
#endif
  auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formats.createReaderFor(source));
  if (reader == nullptr)
    return fail("Couldn't decode stem: " + source.getFileName());
  if (!std::isfinite(reader->sampleRate) || reader->sampleRate <= 0.0 ||
      reader->sampleRate > 655350.0 ||
      std::floor(reader->sampleRate) != reader->sampleRate ||
      reader->lengthInSamples <= 0 || reader->numChannels < 1 ||
      reader->numChannels > 8)
    return fail("Stem has unsupported FLAC audio dimensions: " +
                source.getFileName());

  if (destination.isDirectory() ||
      destination.getParentDirectory().createDirectory().failed())
    return fail("Couldn't create FLAC destination: " +
                destination.getFileName());

  const auto sampleRate = reader->sampleRate;
  const auto channels = reader->numChannels;
  const auto totalSamples = reader->lengthInSamples;
  const auto bits = reader->usesFloatingPointData || reader->bitsPerSample > 16
                        ? 24 : 16;
  // RAII removes partial data on every early return; the target is replaced
  // only after finalization and a successful reopen.
  juce::TemporaryFile temporary(destination);
  {
    juce::FileOutputStream file(temporary.getFile());
    if (!file.openedOk())
      return fail("Couldn't open temporary FLAC output.");
    std::unique_ptr<juce::OutputStream> output =
        std::make_unique<BorrowedOutputStream>(file);
    juce::FlacAudioFormat flac;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(static_cast<int>(channels))
                             .withBitsPerSample(bits)
                             .withQualityOptionIndex(5);
    auto writer = flac.createWriterFor(output, options);
    if (writer == nullptr)
      return fail("Couldn't initialize the FLAC encoder.");
    if (onProgress)
      onProgress(0.0);
    constexpr juce::int64 blockSize = 65536;
    for (juce::int64 position = 0; position < totalSamples;) {
      const auto count = juce::jmin(blockSize, totalSamples - position);
      // Unlike the stereo AudioBuffer read overload, this preserves all
      // source channels and the precision of 24-bit integer PCM.
      if (!writer->writeFromAudioReader(*reader, position, count))
        return fail("Couldn't read or encode stem: " + source.getFileName());
      position += count;
      if (onProgress)
        onProgress(0.99 * static_cast<double>(position) /
                   static_cast<double>(totalSamples));
    }
    writer.reset(); // FLAC finish writes final frame and updates STREAMINFO.
    file.flush();
    if (file.getStatus().failed())
      return fail("Couldn't finalize FLAC output: " +
                  file.getStatus().getErrorMessage());
  }

  auto encoded = std::unique_ptr<juce::AudioFormatReader>(
      formats.createReaderFor(temporary.getFile()));
  if (encoded == nullptr || encoded->sampleRate != sampleRate ||
      encoded->numChannels != channels ||
      encoded->lengthInSamples != totalSamples ||
      encoded->bitsPerSample != static_cast<unsigned int>(bits))
    return fail("The converted FLAC failed validation.");
  encoded.reset();
  reader.reset();
  if (!temporary.overwriteTargetFileWithTemporary())
    return fail("Couldn't store the completed FLAC file.");
  if (onProgress)
    onProgress(1.0);
  return true;
}

} // namespace portable_audio
