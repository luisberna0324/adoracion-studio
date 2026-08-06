#include "PadsEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {
bool isTokenChar(const juce::juce_wchar c) {
  return juce::CharacterFunctions::isLetterOrDigit(c) || c == '#';
}

float smoothstep(const float t) {
  const float clamped = juce::jlimit(0.0f, 1.0f, t);
  return clamped * clamped * (3.0f - 2.0f * clamped);
}

constexpr double kCrossfadeFastSec = 1.0;
constexpr double kCrossfadeMediumSec = 3.0;
constexpr double kCrossfadeLongSec = 5.0;
constexpr double kLoopCrossfadeSec = 2.0;
} // namespace

int PadsEngine::noteIndexForSongKey(const juce::String &songKey) noexcept {
  auto normalized = songKey.trim().toUpperCase();
  if (normalized.startsWith("KEY OF "))
    normalized = normalized.substring(7).trimStart();
  if (normalized.isEmpty())
    return -1;

  int noteIndex = -1;
  switch (normalized[0]) {
  case 'C': noteIndex = 0; break;
  case 'D': noteIndex = 2; break;
  case 'E': noteIndex = 4; break;
  case 'F': noteIndex = 5; break;
  case 'G': noteIndex = 7; break;
  case 'A': noteIndex = 9; break;
  case 'B': noteIndex = 11; break;
  default: return -1;
  }

  if (normalized.length() > 1) {
    const auto accidental = normalized[1];
    if (accidental == '#' || accidental == 0x266f)
      ++noteIndex;
    else if (accidental == 'B' || accidental == 0x266d)
      --noteIndex;
  }

  return (noteIndex + kNoteCount) % kNoteCount;
}

void PadsEngine::prepare(const double sampleRate, const int maxBlockSize) {
  outputSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  maxBlock = juce::jmax(0, maxBlockSize);
  updateFadeDurations();
}

void PadsEngine::setCrossfadePreset(const CrossfadePreset preset) noexcept {
  crossfadePreset.store(preset, std::memory_order_relaxed);
  updateFadeDurations();
}

double PadsEngine::getCrossfadeDurationSeconds() const noexcept {
  switch (crossfadePreset.load(std::memory_order_relaxed)) {
  case CrossfadePreset::Fast:
    return kCrossfadeFastSec;
  case CrossfadePreset::Long:
    return kCrossfadeLongSec;
  case CrossfadePreset::Medium:
  default:
    return kCrossfadeMediumSec;
  }
}

void PadsEngine::beginSongTransitionFadeOut() noexcept {
  songTransitionTargetGain.store(0.0f, std::memory_order_relaxed);
}

void PadsEngine::beginSongTransitionFadeIn() noexcept {
  songTransitionTargetGain.store(1.0f, std::memory_order_relaxed);
}

void PadsEngine::updateFadeDurations() noexcept {
  const double seconds = getCrossfadeDurationSeconds();

  // Symmetric crossfade: incoming fades in while outgoing fades out over
  // the same interval.
  fadeInSamples = juce::jmax(
      1, static_cast<int>(std::llround(outputSampleRate * seconds)));
  fadeOutSamples = juce::jmax(
      1, static_cast<int>(std::llround(outputSampleRate * seconds)));
}

void PadsEngine::releaseResources() { stopAll(); }

void PadsEngine::setEnabled(const bool shouldEnable) noexcept {
  enabled.store(shouldEnable, std::memory_order_relaxed);
  if (!shouldEnable) {
    activeNoteIndex.store(-1, std::memory_order_relaxed);
    for (auto &voice : voices) {
      if (voice.state != VoiceState::stopped)
        beginFadeOut(voice);
    }
  }
}

int PadsEngine::cleanlinessScore(const juce::String &name) {
  int score = 0;
  if (!name.contains(" ."))
    score += 2;
  if (name.trim() == name)
    score += 1;
  if (!name.contains("  "))
    score += 1;
  return score;
}

int PadsEngine::parseNoteIndexFromFilename(const juce::String &name) {
  const juce::String upper = name.toUpperCase();
  struct NoteToken {
    const char *token;
    int idx;
  };
  static constexpr NoteToken tokens[] = {{"C#", 1}, {"D#", 3}, {"F#", 6},
                                         {"G#", 8}, {"A#", 10}, {"C", 0},
                                         {"D", 2}, {"E", 4},   {"F", 5},
                                         {"G", 7}, {"A", 9},   {"B", 11}};

  for (const auto &token : tokens) {
    const juce::String needle(token.token);
    int pos = upper.indexOf(needle);
    while (pos >= 0) {
      const int end = pos + needle.length();
      const bool leftOk = pos == 0 || !isTokenChar(upper[pos - 1]);
      const bool rightOk = end >= upper.length() || !isTokenChar(upper[end]);
      if (leftOk && rightOk)
        return token.idx;
      pos = upper.indexOf(pos + 1, needle);
    }
  }
  return -1;
}

PadsEngine::LoadResult
PadsEngine::loadFromFolder(const juce::File &folder,
                           juce::AudioFormatManager &formatManager) {
  LoadResult result;
  if (!folder.isDirectory()) {
    result.statusText = "Pads folder not found";
    return result;
  }

  struct Candidate {
    juce::File file;
    int score = 0;
    juce::int64 modTimeMs = 0;
  };
  std::array<std::optional<Candidate>, kNoteCount> selected;
  const auto files = folder.findChildFiles(juce::File::findFiles, false, "*.mp3;*.wav;*.aif;*.aiff");
  for (const auto &file : files) {
    const int idx = parseNoteIndexFromFilename(file.getFileNameWithoutExtension());
    if (!juce::isPositiveAndBelow(idx, kNoteCount))
      continue;
    Candidate cand;
    cand.file = file;
    cand.score = cleanlinessScore(file.getFileName());
    cand.modTimeMs = file.getLastModificationTime().toMilliseconds();
    auto &slot = selected[static_cast<size_t>(idx)];
    if (!slot.has_value() || cand.score > slot->score ||
        (cand.score == slot->score && cand.modTimeMs > slot->modTimeMs)) {
      slot = cand;
    }
  }

  int loaded = 0;
  std::array<NoteSample, kNoteCount> nextSamples;
  for (int i = 0; i < kNoteCount; ++i) {
    auto &slot = selected[static_cast<size_t>(i)];
    if (!slot.has_value())
      continue;

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(slot->file));
    if (!reader)
      continue;

    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const int lengthSamples = static_cast<int>(
        juce::jlimit<juce::int64>(0, std::numeric_limits<int>::max(),
                                  reader->lengthInSamples));
    if (lengthSamples <= 0)
      continue;

    NoteSample note;
    note.data.setSize(channels, lengthSamples);
    reader->read(&note.data, 0, lengthSamples, 0, true, channels > 1);
    note.sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    note.valid = true;
    nextSamples[static_cast<size_t>(i)] = std::move(note);
    ++loaded;
  }

  if (loaded < kNoteCount) {
    result.statusText = "Missing files";
    result.loadedNotes = loaded;
    return result;
  }

  noteSamples = std::move(nextSamples);
  stopAll();
  result.ok = true;
  result.loadedNotes = loaded;
  result.statusText = "Loaded";
  return result;
}

void PadsEngine::updateVoiceIncrement(Voice &voice) {
  if (!juce::isPositiveAndBelow(voice.noteIndex, kNoteCount))
    return;
  const auto &sample = noteSamples[static_cast<size_t>(voice.noteIndex)];
  if (!sample.valid || sample.sourceSampleRate <= 0.0 || outputSampleRate <= 0.0)
    return;
  const double octaveRatio = std::pow(2.0, static_cast<double>(voice.octave));
  voice.increment = (sample.sourceSampleRate / outputSampleRate) * octaveRatio;
}

void PadsEngine::beginFadeOut(Voice &voice) {
  if (voice.state == VoiceState::stopped)
    return;
  voice.fadeOutStartGain = juce::jlimit(0.0f, 1.0f, voice.gain);
  voice.fadeOutProgress = 0.0f;
  voice.state = VoiceState::fadingOut;
}

void PadsEngine::fadeOutAllActive() {
  for (auto &voice : voices) {
    if (voice.state == VoiceState::stopped)
      continue;
    beginFadeOut(voice);
  }
}

PadsEngine::Voice *PadsEngine::findBestVoiceForTrigger() {
  for (auto &voice : voices) {
    if (voice.state == VoiceState::stopped)
      return &voice;
  }

  Voice *best = nullptr;
  float lowestGain = std::numeric_limits<float>::max();
  for (auto &voice : voices) {
    if (voice.gain < lowestGain) {
      lowestGain = voice.gain;
      best = &voice;
    }
  }
  return best;
}

void PadsEngine::triggerNote(const int noteIndex, const int octave) {
  if (!enabled.load(std::memory_order_relaxed))
    return;
  if (!juce::isPositiveAndBelow(noteIndex, kNoteCount))
    return;
  if (!noteSamples[static_cast<size_t>(noteIndex)].valid)
    return;

  fadeOutAllActive();

  auto *voice = findBestVoiceForTrigger();
  if (voice == nullptr)
    return;

  voice->noteIndex = noteIndex;
  voice->octave = juce::jlimit(-3, 3, octave);
  voice->position = 0.0;
  voice->gain = 0.0f;
  voice->fadeOutStartGain = 0.0f;
  voice->fadeOutProgress = 0.0f;
  voice->state = VoiceState::fadingIn;
  updateVoiceIncrement(*voice);
  activeNoteIndex.store(noteIndex, std::memory_order_relaxed);
}

void PadsEngine::triggerNoteForSongTransition(const int noteIndex,
                                              const int octave) {
  if (!enabled.load(std::memory_order_relaxed) ||
      !juce::isPositiveAndBelow(noteIndex, kNoteCount) ||
      !noteSamples[static_cast<size_t>(noteIndex)].valid)
    return;

  // The pads bus has already faded down with the outgoing song. Remove every
  // old voice before it becomes audible again, then stage the new key at full
  // voice gain; the bus envelope supplies the configured fade-in.
  for (auto &voice : voices) {
    voice = Voice{};
  }
  songTransitionGain = 0.0f;
  songTransitionTargetGain.store(0.0f, std::memory_order_relaxed);

  auto *voice = findBestVoiceForTrigger();
  if (voice == nullptr)
    return;
  voice->noteIndex = noteIndex;
  voice->octave = juce::jlimit(-3, 3, octave);
  voice->position = 0.0;
  voice->gain = 1.0f;
  voice->state = VoiceState::playing;
  updateVoiceIncrement(*voice);
  activeNoteIndex.store(noteIndex, std::memory_order_relaxed);
  outputLevel.store(0.0f, std::memory_order_relaxed);
}

void PadsEngine::stopNote(const int noteIndex) {
  if (!juce::isPositiveAndBelow(noteIndex, kNoteCount))
    return;
  for (auto &voice : voices) {
    if (voice.state == VoiceState::stopped)
      continue;
    if (voice.noteIndex != noteIndex)
      continue;
    beginFadeOut(voice);
  }
  int expected = noteIndex;
  activeNoteIndex.compare_exchange_strong(expected, -1,
                                          std::memory_order_relaxed);
}

void PadsEngine::stopAll() {
  activeNoteIndex.store(-1, std::memory_order_relaxed);
  songTransitionTargetGain.store(1.0f, std::memory_order_relaxed);
  songTransitionGain = 1.0f;
  for (auto &voice : voices) {
    voice.state = VoiceState::stopped;
    voice.position = 0.0;
    voice.gain = 0.0f;
    voice.noteIndex = -1;
    voice.octave = 0;
    voice.increment = 1.0;
    voice.fadeOutStartGain = 0.0f;
    voice.fadeOutProgress = 0.0f;
  }
  outputLevel.store(0.0f, std::memory_order_relaxed);
}

bool PadsEngine::hasActiveVoices() const noexcept {
  for (const auto &voice : voices) {
    if (voice.state != VoiceState::stopped)
      return true;
  }
  return false;
}

void PadsEngine::applySongTransitionGain(
    juce::AudioBuffer<float> &outputBuffer) {
  const int numSamples = outputBuffer.getNumSamples();
  const int numChannels = outputBuffer.getNumChannels();
  if (numSamples <= 0 || numChannels <= 0)
    return;

  const float target = songTransitionTargetGain.load(std::memory_order_relaxed);
  const float step = 1.0f / static_cast<float>(juce::jmax(1, fadeInSamples));
  for (int sample = 0; sample < numSamples; ++sample) {
    const float gain = songTransitionGain;
    for (int channel = 0; channel < numChannels; ++channel)
      outputBuffer.setSample(channel, sample,
                             outputBuffer.getSample(channel, sample) * gain);

    if (songTransitionGain < target)
      songTransitionGain = juce::jmin(target, songTransitionGain + step);
    else if (songTransitionGain > target)
      songTransitionGain = juce::jmax(target, songTransitionGain - step);
  }
}

void PadsEngine::renderVoice(Voice &voice, juce::AudioBuffer<float> &outputBuffer) {
  if (voice.state == VoiceState::stopped || !juce::isPositiveAndBelow(voice.noteIndex, kNoteCount))
    return;

  const auto &sample = noteSamples[static_cast<size_t>(voice.noteIndex)];
  if (!sample.valid || sample.data.getNumSamples() <= 0) {
    voice.state = VoiceState::stopped;
    return;
  }

  const int outSamples = outputBuffer.getNumSamples();
  const int outChans = outputBuffer.getNumChannels();
  const int srcSamples = sample.data.getNumSamples();
  const int srcChans = sample.data.getNumChannels();
  if (outSamples <= 0 || outChans <= 0 || srcChans <= 0)
    return;

  // Ambient pads are long, but they must remain continuous indefinitely. Mix
  // the beginning over the final two seconds and skip that already-rendered
  // overlap when wrapping. Short samples retain the simple boundary loop.
  const int requestedLoopCrossfade = static_cast<int>(std::llround(
      sample.sourceSampleRate * kLoopCrossfadeSec));
  const int loopCrossfadeSamples =
      srcSamples >= 8
          ? juce::jlimit(1, juce::jmax(1, srcSamples / 4),
                         juce::jmax(1, requestedLoopCrossfade))
          : 0;
  const double loopCrossfadeStart =
      static_cast<double>(srcSamples - loopCrossfadeSamples);
  const double loopSpan = static_cast<double>(
      juce::jmax(1, srcSamples - loopCrossfadeSamples));

  const auto readInterpolated = [&sample, srcSamples](const int channel,
                                                       double position) {
    while (position >= static_cast<double>(srcSamples))
      position -= static_cast<double>(srcSamples);
    while (position < 0.0)
      position += static_cast<double>(srcSamples);
    const int pos0 = static_cast<int>(position);
    const int pos1 = pos0 + 1 < srcSamples ? pos0 + 1 : 0;
    const float frac = static_cast<float>(position - static_cast<double>(pos0));
    const auto *src = sample.data.getReadPointer(channel);
    return src[pos0] + (src[pos1] - src[pos0]) * frac;
  };

  const float inStep = 1.0f / static_cast<float>(fadeInSamples);
  const float outStep = 1.0f / static_cast<float>(fadeOutSamples);
  for (int i = 0; i < outSamples; ++i) {
    const bool inLoopCrossfade =
        loopCrossfadeSamples > 0 && voice.position >= loopCrossfadeStart;
    const float loopMix = inLoopCrossfade
                              ? static_cast<float>(juce::jlimit(
                                    0.0, 1.0,
                                    (voice.position - loopCrossfadeStart) /
                                        static_cast<double>(loopCrossfadeSamples)))
                              : 0.0f;

    for (int ch = 0; ch < outChans; ++ch) {
      const int srcCh = juce::jmin(ch, srcChans - 1);
      const float tail = readInterpolated(srcCh, voice.position);
      const float head = inLoopCrossfade
                             ? readInterpolated(
                                   srcCh, voice.position - loopCrossfadeStart)
                             : 0.0f;
      const float v = inLoopCrossfade
                          ? (tail * (1.0f - loopMix)) + (head * loopMix)
                          : tail;
      outputBuffer.addSample(ch, i, v * voice.gain);
    }

    voice.position += voice.increment;
    while (voice.position >= static_cast<double>(srcSamples))
      voice.position -= loopSpan;

    if (voice.state == VoiceState::fadingIn) {
      voice.gain = juce::jmin(1.0f, voice.gain + inStep);
      if (voice.gain >= 0.999f)
        voice.state = VoiceState::playing;
    } else if (voice.state == VoiceState::fadingOut) {
      voice.fadeOutProgress = juce::jmin(1.0f, voice.fadeOutProgress + outStep);
      const float fadeShape = smoothstep(voice.fadeOutProgress);
      voice.gain = juce::jmax(0.0f, voice.fadeOutStartGain * (1.0f - fadeShape));
      if (voice.fadeOutProgress >= 0.999f || voice.gain <= 0.0001f) {
        voice.state = VoiceState::stopped;
        voice.gain = 0.0f;
        voice.fadeOutStartGain = 0.0f;
        voice.fadeOutProgress = 0.0f;
        break;
      }
    }
  }
}

void PadsEngine::process(juce::AudioBuffer<float> &outputBuffer) {
  if (!enabled.load(std::memory_order_relaxed) && !hasActiveVoices()) {
    outputLevel.store(0.0f, std::memory_order_relaxed);
    return;
  }

  for (auto &voice : voices)
    renderVoice(voice, outputBuffer);

  applySongTransitionGain(outputBuffer);

  float peak = 0.0f;
  for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) {
    peak = juce::jmax(peak, outputBuffer.getMagnitude(ch, 0, outputBuffer.getNumSamples()));
  }
  outputLevel.store(peak, std::memory_order_relaxed);
}
