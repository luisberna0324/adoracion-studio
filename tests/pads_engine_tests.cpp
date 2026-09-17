#include "../src/audio/PadsEngine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

struct PadsEngineTestPeer {
  static void installConstantSample(PadsEngine &engine, const int noteIndex,
                                    const int sampleCount,
                                    const double sampleRate,
                                    const float value = 1.0f) {
    auto &sample = engine.noteSamples[static_cast<size_t>(noteIndex)];
    sample.data.setSize(1, sampleCount);
    sample.data.clear();
    sample.data.applyGain(1.0f);
    for (int i = 0; i < sampleCount; ++i)
      sample.data.setSample(0, i, value);
    sample.sourceSampleRate = sampleRate;
    sample.valid = true;
  }

  static double activeVoicePosition(const PadsEngine &engine) {
    for (const auto &voice : engine.voices) {
      if (voice.state != PadsEngine::VoiceState::stopped)
        return voice.position;
    }
    return -1.0;
  }

  static float songTransitionGain(const PadsEngine &engine) {
    return engine.songTransitionGain;
  }

  static int activeVoiceCount(const PadsEngine &engine) {
    int count = 0;
    for (const auto &voice : engine.voices)
      if (voice.state != PadsEngine::VoiceState::stopped)
        ++count;
    return count;
  }

  static bool hasSourceFile(const PadsEngine &engine, const int noteIndex) {
    return engine.noteSamples[static_cast<size_t>(noteIndex)]
        .sourceFile.existsAsFile();
  }

  static void setSourceFile(PadsEngine &engine, const int noteIndex,
                            const juce::File &file) {
    engine.noteSamples[static_cast<size_t>(noteIndex)].sourceFile = file;
  }
};

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void testSongKeyParsing() {
  expect(PadsEngine::noteIndexForSongKey("C") == 0, "C maps to pad C");
  expect(PadsEngine::noteIndexForSongKey("F#") == 6, "F# maps to pad F#");
  expect(PadsEngine::noteIndexForSongKey("Bb") == 10, "Bb maps to pad A#");
  expect(PadsEngine::noteIndexForSongKey("Am") == 9,
         "minor suffix preserves the root note");
  expect(PadsEngine::noteIndexForSongKey("Key of C#m") == 1,
         "key prefix and minor suffix are accepted");
  expect(PadsEngine::noteIndexForSongKey("7") == -1,
         "unknown key does not change the active pad");
}

void testPadLoopsWithEndCrossfade() {
  PadsEngine engine;
  engine.prepare(8.0, 32);
  engine.setCrossfadePreset(PadsEngine::CrossfadePreset::Fast);
  PadsEngineTestPeer::installConstantSample(engine, 0, 16, 8.0);
  engine.triggerNote(0, 0);
  expect(engine.getActiveNoteIndex() == 0,
         "trigger publishes the active note for UI and song following");

  juce::AudioBuffer<float> output(1, 32);
  output.clear();
  engine.process(output);

  expect(output.getMagnitude(0, 20, 12) > 0.9f,
         "pad remains audible after passing the source end");
  expect(std::abs(PadsEngineTestPeer::activeVoicePosition(engine) - 8.0) < 1.0e-9,
         "loop skips the head already rendered by the end crossfade");

  engine.stopNote(0);
  expect(engine.getActiveNoteIndex() == -1,
         "stopping the selected pad clears the published active note");
}

void testSongKeyTransitionCrossfadesVoicesWithoutSilence() {
  PadsEngine engine;
  engine.prepare(8.0, 8);
  engine.setCrossfadePreset(PadsEngine::CrossfadePreset::Fast);
  expect(engine.getCrossfadeDurationSeconds() == 1.0,
         "fast transition uses one second");
  engine.setCrossfadePreset(PadsEngine::CrossfadePreset::Medium);
  expect(engine.getCrossfadeDurationSeconds() == 3.0,
         "medium transition uses three seconds");
  engine.setCrossfadePreset(PadsEngine::CrossfadePreset::Long);
  expect(engine.getCrossfadeDurationSeconds() == 5.0,
         "long transition uses five seconds");
  engine.setCrossfadePreset(PadsEngine::CrossfadePreset::Fast);
  PadsEngineTestPeer::installConstantSample(engine, 0, 64, 8.0);
  PadsEngineTestPeer::installConstantSample(engine, 2, 64, 8.0, 1.0f);
  engine.triggerNote(0, 0);

  juce::AudioBuffer<float> output(1, 8);
  output.clear();
  engine.process(output);

  engine.triggerNote(2, 0);
  expect(engine.getActiveNoteIndex() == 2,
         "new song key becomes the active pad immediately");
  expect(PadsEngineTestPeer::activeVoiceCount(engine) == 2,
         "outgoing and incoming pad voices overlap during retune");

  output.clear();
  engine.process(output);
  expect(PadsEngineTestPeer::songTransitionGain(engine) == 1.0f,
         "retuning does not fade the complete pads bus to silence");
  for (int sample = 0; sample < output.getNumSamples(); ++sample)
    expect(output.getSample(0, sample) > 0.25f,
           "a song-key change keeps the pads bus audible throughout");
}

void testUnusedDecodedSamplesCanBeReleasedSafely() {
  PadsEngine engine;
  engine.prepare(8.0, 8);
  const auto source = juce::File::getSpecialLocation(
      juce::File::currentExecutableFile);
  for (int note = 0; note < 3; ++note) {
    PadsEngineTestPeer::setSourceFile(engine, note, source);
    PadsEngineTestPeer::installConstantSample(engine, note, 64, 8.0);
  }

  engine.triggerNote(0, 0);
  engine.releaseUnusedSamples(2);

  expect(engine.isNoteLoaded(0),
         "sample used by an active voice remains decoded");
  expect(!engine.isNoteLoaded(1), "inactive sample buffer is released");
  expect(engine.isNoteLoaded(2), "explicitly requested sample remains decoded");
  expect(PadsEngineTestPeer::hasSourceFile(engine, 1),
         "releasing PCM keeps the indexed source file for reloading");
}

void testResidentAttackPromotesWithoutRestartAndSurvivesEviction() {
  PadsEngine engine;
  engine.prepare(8.0, 32);
  engine.setCrossfadePreset(PadsEngine::CrossfadePreset::Fast);
  PadsEngineTestPeer::setSourceFile(engine, 0,
      juce::File::getSpecialLocation(juce::File::currentExecutableFile));
  PadsEngine::DecodedNote attack;
  attack.sourceSampleRate = 8.0;
  attack.totalSamples = 128;
  attack.data.setSize(1, 32);
  for (int i = 0; i < 32; ++i)
    attack.data.setSample(0, i, 0.5f);
  expect(engine.installDecodedNote(0, std::move(attack)), "attack installs");
  expect(engine.isNoteLoaded(0) && !engine.isNoteFullyLoaded(0),
         "attack is playable before the full file is available");
  engine.triggerNote(0, 0);
  juce::AudioBuffer<float> output(1, 16);
  output.clear();
  engine.process(output);
  expect(output.getSample(0, 15) > 0.49f, "attack produces audio immediately");

  PadsEngine::DecodedNote full;
  full.sourceSampleRate = 8.0;
  full.totalSamples = 128;
  full.data.setSize(1, 128);
  for (int i = 0; i < 128; ++i)
    full.data.setSample(0, i, 0.5f);
  engine.installDecodedNote(0, std::move(full));
  expect(PadsEngineTestPeer::activeVoicePosition(engine) == 16.0,
         "publishing full PCM preserves the playback position");
  for (int block = 0; block < 3; ++block) {
    output.clear();
    engine.process(output);
    for (int i = 0; i < output.getNumSamples(); ++i)
      expect(std::abs(output.getSample(0, i) - 0.5f) < 0.0001f,
             "audio continues past the attack boundary without a gap");
  }
  engine.stopAll();
  engine.releaseUnusedSamples();
  expect(engine.isNoteLoaded(0) && !engine.isNoteFullyLoaded(0),
         "eviction keeps the attack ready for another immediate trigger");
  engine.triggerNote(0, 0);
  output.clear();
  engine.process(output);
  expect(output.getSample(0, 15) > 0.49f, "retrigger works after full PCM eviction");
}

void testPartialDecodePreservesLengthAndCancellation() {
  const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getNonexistentChildFile("pads-decode-test", ".wav");
  {
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(8000).withNumChannels(1).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    expect(writer != nullptr, "test WAV opens");
    juce::AudioBuffer<float> source(1, 32000);
    for (int i = 0; i < 32000; ++i)
      source.setSample(0, i, static_cast<float>(i) / 32000.0f);
    expect(writer->writeFromAudioSampleBuffer(source, 0, 32000), "test WAV writes");
  }
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  PadsEngine::DecodedNote attack, full;
  expect(PadsEngine::decodeNoteFile(file, formats, attack, 1.0), "partial WAV reads");
  expect(attack.data.getNumSamples() == 8000 && attack.totalSamples == 32000,
         "partial decode keeps the true loop length");
  expect(PadsEngine::decodeNoteFile(file, formats, full), "full WAV reads");
  expect(attack.data.getSample(0, 7999) == full.data.getSample(0, 7999),
         "attack and full PCM agree at their shared boundary");
  expect(!PadsEngine::decodeNoteFile(file, formats, full, 0.0, [] { return true; }),
         "obsolete decode can be cancelled");
  PadsEngine::DecodedNote shortAttack;
  expect(PadsEngine::decodeNoteFile(file, formats, shortAttack, 64.0),
         "short files are fully prepared during bank warmup");
  PadsEngine engine;
  PadsEngineTestPeer::setSourceFile(engine, 0, file);
  engine.installDecodedNote(0, std::move(shortAttack));
  engine.releaseUnusedSamples();
  expect(engine.isNoteFullyLoaded(0),
         "a short prepared file stays resident without repeated disk loads");
  file.deleteFile();
}

} // namespace

int main() {
  testSongKeyParsing();
  testPadLoopsWithEndCrossfade();
  testSongKeyTransitionCrossfadesVoicesWithoutSilence();
  testUnusedDecodedSamplesCanBeReleasedSafely();
  testResidentAttackPromotesWithoutRestartAndSurvivesEviction();
  testPartialDecodePreservesLengthAndCancellation();
  std::cout << "Pads engine tests passed\n";
  return 0;
}
