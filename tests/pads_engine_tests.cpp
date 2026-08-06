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

void testSongTransitionUsesConfiguredCrossfade() {
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
  PadsEngineTestPeer::installConstantSample(engine, 2, 64, 8.0, -1.0f);
  engine.triggerNote(0, 0);

  juce::AudioBuffer<float> output(1, 8);
  output.clear();
  engine.process(output);

  engine.beginSongTransitionFadeOut();
  output.clear();
  engine.process(output);
  expect(PadsEngineTestPeer::songTransitionGain(engine) == 0.0f,
         "pad bus fades down over the configured one second");
  expect(engine.getActiveNoteIndex() == 0,
         "song transition fade preserves the selected pad note");

  engine.triggerNoteForSongTransition(2, 0);
  expect(PadsEngineTestPeer::songTransitionGain(engine) == 0.0f,
         "new song key is staged while the pads bus is silent");
  expect(engine.getActiveNoteIndex() == 2,
         "song transition publishes only the new pad note");
  engine.beginSongTransitionFadeIn();
  output.clear();
  engine.process(output);
  expect(PadsEngineTestPeer::songTransitionGain(engine) == 1.0f,
         "pad bus fades up over the same configured duration");
  expect(output.getSample(0, 7) < -0.8f,
         "outgoing pad tone is absent during the new fade-in");
}

} // namespace

int main() {
  testSongKeyParsing();
  testPadLoopsWithEndCrossfade();
  testSongTransitionUsesConfiguredCrossfade();
  std::cout << "Pads engine tests passed\n";
  return 0;
}
