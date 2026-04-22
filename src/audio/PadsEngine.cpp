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
} // namespace

void PadsEngine::prepare(const double sampleRate, const int maxBlockSize) {
  outputSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  maxBlock = juce::jmax(0, maxBlockSize);
  // Ramps tuned for ambient note transitions with audible overlap.
  fadeInSamples = juce::jmax(
      1, static_cast<int>(std::llround(outputSampleRate * 0.18)));
  fadeOutSamples = juce::jmax(
      1, static_cast<int>(std::llround(outputSampleRate * 1.10)));
}

void PadsEngine::releaseResources() { stopAll(); }

void PadsEngine::setEnabled(const bool shouldEnable) noexcept {
  enabled.store(shouldEnable, std::memory_order_relaxed);
  if (!shouldEnable) {
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
}

void PadsEngine::stopAll() {
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

  const float inStep = 1.0f / static_cast<float>(fadeInSamples);
  const float outStep = 1.0f / static_cast<float>(fadeOutSamples);
  for (int i = 0; i < outSamples; ++i) {
    int pos0 = static_cast<int>(voice.position);
    while (pos0 >= srcSamples)
      pos0 -= srcSamples;
    int pos1 = pos0 + 1;
    if (pos1 >= srcSamples)
      pos1 = 0;
    const float frac = static_cast<float>(voice.position - static_cast<double>(pos0));

    for (int ch = 0; ch < outChans; ++ch) {
      const int srcCh = juce::jmin(ch, srcChans - 1);
      const auto *src = sample.data.getReadPointer(srcCh);
      const float v = src[pos0] + (src[pos1] - src[pos0]) * frac;
      outputBuffer.addSample(ch, i, v * voice.gain);
    }

    voice.position += voice.increment;
    while (voice.position >= static_cast<double>(srcSamples))
      voice.position -= static_cast<double>(srcSamples);

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

  float peak = 0.0f;
  for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) {
    peak = juce::jmax(peak, outputBuffer.getMagnitude(ch, 0, outputBuffer.getNumSamples()));
  }
  outputLevel.store(peak, std::memory_order_relaxed);
}
