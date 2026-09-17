#include "MetronomeEngine.h"

#include <limits>
#include <memory>

namespace {
constexpr const char *kAudioFilePattern = "*.wav;*.aif;*.aiff;*.flac;*.mp3";
constexpr float kEighthClickGainScale = 0.49f;
constexpr float kSixteenthClickGainScale = 0.20f;

juce::String sanitizeToken(juce::String text) {
  text = text.trim().toLowerCase();
  text = text.replaceCharacter(' ', '-');
  text = text.replaceCharacter('_', '-');
  text = text.replaceCharacter('.', '-');
  while (text.contains("--"))
    text = text.replace("--", "-");
  return text;
}

bool fileNameMatchesCanonical(const juce::File &file, const juce::String &style,
                              const juce::String &role) {
  const auto name = sanitizeToken(file.getFileNameWithoutExtension());
  const auto expected = "click-" + sanitizeToken(style) + "-" + sanitizeToken(role);
  return name == expected;
}

bool fileNameMatchesLegacy(const juce::File &file, const juce::String &style,
                           const juce::String &role) {
  const auto name = sanitizeToken(file.getFileNameWithoutExtension());
  return name.contains(sanitizeToken(style)) && name.contains(sanitizeToken(role));
}
} // namespace

void MetronomeEngine::prepare(double sampleRateToUse) {
  sampleRate = sampleRateToUse;
  reset();
}

void MetronomeEngine::reset() {
  resetAudioState();
  appliedResetGeneration =
      requestedResetGeneration.load(std::memory_order_acquire);
}

void MetronomeEngine::requestReset() noexcept {
  requestedResetGeneration.fetch_add(1, std::memory_order_release);
}

void MetronomeEngine::resetAudioState() noexcept {
  phase = 0.0;
  subPhase = 0.0;
  activeBpm = bpm.load(std::memory_order_relaxed);
  activeSubdivision = normalizeSubdivision(
      requestedSubdivision.load(std::memory_order_relaxed));
  clickSamplesRemaining = 0;
  beatIndex = juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed)) - 1;
  subIndex = 0;
  boundaryClickPending = false;
  boundarySubdivision = activeSubdivision;
  currentClickIsMainBeat = true;
  clickGain = 1.00f;
  currentClickTone = ClickTone::downbeat;
  for (auto &voice : sampleVoices)
    voice = {};
}

void MetronomeEngine::applyPendingReset() noexcept {
  const auto requested =
      requestedResetGeneration.load(std::memory_order_acquire);
  if (requested == appliedResetGeneration)
    return;
  resetAudioState();
  appliedResetGeneration = requested;
}

void MetronomeEngine::setEnabled(bool enabledToUse) noexcept {
  enabled.store(enabledToUse, std::memory_order_relaxed);
}

bool MetronomeEngine::isEnabled() const noexcept {
  return enabled.load(std::memory_order_relaxed);
}

void MetronomeEngine::setBpm(double bpmToUse) noexcept {
  const double clamped = juce::jlimit(20.0, 300.0, bpmToUse);
  bpm.store(clamped, std::memory_order_relaxed);
}

double MetronomeEngine::getBpm() const noexcept {
  return bpm.load(std::memory_order_relaxed);
}

void MetronomeEngine::setBeatsPerBar(int beats) noexcept {
  const int clamped = juce::jmax(1, beats);
  beatsPerBar.store(clamped, std::memory_order_relaxed);
}

int MetronomeEngine::getBeatsPerBar() const noexcept {
  return beatsPerBar.load(std::memory_order_relaxed);
}

void MetronomeEngine::setVolume(const float volumeToUse) noexcept {
  volume.store(juce::jlimit(0.0f, 1.0f, volumeToUse),
               std::memory_order_relaxed);
}

float MetronomeEngine::getVolume() const noexcept {
  return volume.load(std::memory_order_relaxed);
}

void MetronomeEngine::setMuted(const bool mutedToUse) noexcept {
  muted.store(mutedToUse, std::memory_order_relaxed);
}

bool MetronomeEngine::isMuted() const noexcept {
  return muted.load(std::memory_order_relaxed);
}

void MetronomeEngine::triggerImmediateClick() noexcept {
  pendingImmediateClick.store(true, std::memory_order_relaxed);
}

void MetronomeEngine::setSubdivision(int subdiv) noexcept {
  requestedSubdivision.store(normalizeSubdivision(subdiv),
                             std::memory_order_release);
}

int MetronomeEngine::getSubdivision() const noexcept {
  return requestedSubdivision.load(std::memory_order_acquire);
}

void MetronomeEngine::setDivisionMask(int mask) noexcept {
  divisionMask.store(normalizeDivisionMask(mask), std::memory_order_relaxed);
}

int MetronomeEngine::getDivisionMask() const noexcept {
  return divisionMask.load(std::memory_order_relaxed);
}

void MetronomeEngine::syncPhaseForTimeSeconds(double seconds) noexcept {
  const double bpmLocal = bpm.load(std::memory_order_relaxed);
  if (bpmLocal <= 0.0)
    return;
  syncPhaseForBeatPosition(
      (juce::jmax(0.0, seconds) * bpmLocal) / 60.0, bpmLocal);
}

void MetronomeEngine::syncPhaseForBeatPosition(
    const double beatPosition, const double effectiveBpm) noexcept {
  applyPendingReset();
  activeBpm = juce::jlimit(20.0, 300.0, effectiveBpm);
  const double beatPos = juce::jmax(0.0, beatPosition);
  const int beats = juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed));
  const int wholeBeats = static_cast<int>(std::floor(beatPos));
  beatIndex = ((wholeBeats % beats) + beats) % beats;
  phase = beatPos - std::floor(beatPos);

  // The processor synchronizes the metronome at the beginning of every audio
  // callback. When a beat lands exactly on that boundary, phase is zero and
  // the normal increment-before-trigger loop would otherwise wait one full
  // beat before sounding it. Half a sample is enough tolerance for floating
  // point conversion between transport frames, seconds and beat position.
  const double samplesPerBeat = (sampleRate * 60.0) / activeBpm;
  const double boundaryTolerance =
      samplesPerBeat > 0.0 ? 0.5 / samplesPerBeat : 0.0;
  boundaryClickPending = phase <= boundaryTolerance;
  if (boundaryClickPending) {
    boundarySubdivision = normalizeSubdivision(
        requestedSubdivision.load(std::memory_order_acquire));
  }

  const int subdiv = juce::jmax(1, activeSubdivision);
  const double subBeatPos = phase * static_cast<double>(subdiv);
  subIndex = static_cast<int>(std::floor(subBeatPos)) % subdiv;
  subPhase = subBeatPos - std::floor(subBeatPos);
}

void MetronomeEngine::process(juce::AudioBuffer<float> &buffer) noexcept {
  process(buffer, 0, buffer.getNumSamples());
}

void MetronomeEngine::process(juce::AudioBuffer<float> &buffer,
                              const int startSample,
                              const int samplesToProcess) noexcept {
  if (!enabled.load(std::memory_order_relaxed))
    return;
  const float gain = muted.load(std::memory_order_relaxed)
                         ? 0.0f
                         : juce::jlimit(0.0f, 1.0f,
                                        volume.load(std::memory_order_relaxed));
  const bool shouldRenderAudio = gain > 0.0001f;
  const int boundedStart =
      juce::jlimit(0, buffer.getNumSamples(), startSample);
  const int numSamples = juce::jlimit(
      0, buffer.getNumSamples() - boundedStart, samplesToProcess);
  if (numSamples <= 0 || sampleRate <= 0.0)
    return;

  const double bpmLocal = activeBpm;
  const int beats =
      juce::jmax(1, beatsPerBar.load(std::memory_order_relaxed));
  if (bpmLocal <= 0.0)
    return;

  int subdiv = juce::jmax(1, activeSubdivision);
  const int activeMask = normalizeDivisionMask(
      divisionMask.load(std::memory_order_relaxed));
  const double phaseInc = bpmLocal / (60.0 * sampleRate);
  double subPhaseInc = phaseInc * static_cast<double>(subdiv);
  const int numChannels = buffer.getNumChannels();
  bool sampleVoicesActive = hasActiveSampleVoices();

  const bool consumeImmediateClick =
      pendingImmediateClick.exchange(false, std::memory_order_relaxed);
  const bool consumeBoundaryClick = boundaryClickPending;
  boundaryClickPending = false;
  if (consumeBoundaryClick) {
    // A callback-aligned beat is still a real musical boundary. Publish a
    // pending subdivision change here just as the in-buffer boundary path
    // does below.
    const int requested = boundarySubdivision;
    if (requested != activeSubdivision) {
      activeSubdivision = requested;
      subdiv = activeSubdivision;
      subIndex = 0;
      subPhase = 0.0;
      subPhaseInc = phaseInc * static_cast<double>(subdiv);
    }
  }
  if (consumeImmediateClick && shouldRenderAudio && activeMask != 0) {
    const ClickLayer layer =
        (activeMask & 0x01) != 0 ? ClickLayer::downbeat
                                 : (activeMask & 0x02) != 0
                                       ? ClickLayer::eighth
                                       : ClickLayer::sixteenth;
    triggerClick(layer, true, (beatIndex == 0) ? 0.95f : 0.80f);
    sampleVoicesActive = true;
  } else if (consumeBoundaryClick && shouldRenderAudio &&
             isLayerEnabled(activeMask, ClickLayer::downbeat)) {
    // Immediate and boundary triggers are coalesced so play/seek at an exact
    // beat cannot start two overlapping copies of the same click sample.
    triggerClick(ClickLayer::downbeat, true,
                 (beatIndex == 0) ? 0.95f : 0.80f);
    sampleVoicesActive = true;
  }

  for (int sample = 0; sample < numSamples; ++sample) {
    phase += phaseInc;

    // Main beat boundary
    if (phase >= 1.0) {
      phase -= 1.0;
      beatIndex = (beatIndex + 1) % beats;
      subIndex = 0;
      subPhase = 0.0;

      // The requested value is persistent; the audio thread never clears a
      // control publication, so a concurrent update cannot be lost.
      const int requested = normalizeSubdivision(
          requestedSubdivision.load(std::memory_order_acquire));
      if (requested != activeSubdivision) {
        activeSubdivision = requested;
        subdiv = activeSubdivision;
        subPhaseInc = phaseInc * static_cast<double>(subdiv);
      }

      if (shouldRenderAudio && isLayerEnabled(activeMask, ClickLayer::downbeat)) {
        triggerClick(ClickLayer::downbeat, true,
                     (beatIndex == 0) ? 0.95f : 0.80f);
        sampleVoicesActive = true;
      }
    } else if (subdiv > 1) {
      // Sub-beat tracking
      subPhase += subPhaseInc;
      if (subPhase >= 1.0) {
        subPhase -= 1.0;
        subIndex = (subIndex + 1) % subdiv;
        if (subIndex != 0) {
          const auto layer = (subdiv == 2 || (subdiv >= 4 && subIndex == 2))
                                 ? ClickLayer::eighth
                                 : ClickLayer::sixteenth;
          if (shouldRenderAudio && isLayerEnabled(activeMask, layer)) {
            triggerClick(layer, false,
                         layer == ClickLayer::eighth ? 0.72f : 0.60f);
            sampleVoicesActive = true;
          }
        }
      }
    }

    float clickSample = 0.0f;
    if (sampleVoicesActive)
      clickSample += renderSampleVoices();
    if (clickSamplesRemaining > 0)
      clickSample += renderSyntheticClick();
    clickSample *= gain;

    if (clickSample == 0.0f)
      continue;

    for (int ch = 0; ch < numChannels; ++ch)
      buffer.addSample(ch, boundedStart + sample, clickSample);
  }
}

bool MetronomeEngine::loadClickSamplesFromFolder(
    const juce::File &folder, const juce::String &style,
    juce::AudioFormatManager &formatManager, juce::String *errorMessage) {
  if (!folder.isDirectory()) {
    if (errorMessage != nullptr)
      *errorMessage = "Click samples folder not found: " +
                      folder.getFullPathName();
    return false;
  }

  const auto normalizedStyle = style.trim();
  if (normalizedStyle.isEmpty()) {
    if (errorMessage != nullptr)
      *errorMessage = "Click sample style is empty.";
    return false;
  }

  const auto files =
      folder.findChildFiles(juce::File::findFiles, false, kAudioFilePattern);
  const auto findSample = [&files, &normalizedStyle](
                              const juce::String &role) -> juce::File {
    for (const auto &file : files) {
      if (fileNameMatchesCanonical(file, normalizedStyle, role))
        return file;
    }
    for (const auto &file : files) {
      if (fileNameMatchesLegacy(file, normalizedStyle, role))
        return file;
    }
    return {};
  };

  auto nextSamples = std::make_shared<ClickSampleSet>();
  nextSamples->style = normalizedStyle;

  struct RoleLoad {
    const char *name = nullptr;
    ClickSample *target = nullptr;
  };
  const std::array<RoleLoad, 4> roles{{
      {"accents", &nextSamples->downbeat},
      {"quarter", &nextSamples->quarter},
      {"eighth", &nextSamples->eighth},
      {"sixteenth", &nextSamples->sixteenth},
  }};

  int loaded = 0;
  for (const auto &role : roles) {
    const auto file = findSample(role.name);
    if (file == juce::File{})
      continue;
    if (loadSampleFile(file, formatManager, *role.target))
      ++loaded;
  }

  if (loaded <= 0) {
    if (errorMessage != nullptr)
      *errorMessage = "No click samples found for style: " + normalizedStyle;
    return false;
  }

  if (!publishClickSamples(std::shared_ptr<const ClickSampleSet>(nextSamples))) {
    if (errorMessage != nullptr)
      *errorMessage = "Unable to retain click sample generation.";
    return false;
  }
  return true;
}

void MetronomeEngine::clearClickSamples() noexcept {
  publishedClickSamples.store(nullptr, std::memory_order_release);
}

bool MetronomeEngine::hasClickSamples() const noexcept {
  return publishedClickSamples.load(std::memory_order_acquire) != nullptr;
}

int MetronomeEngine::normalizeDivisionMask(const int mask) noexcept {
  return mask & 0x07;
}

int MetronomeEngine::normalizeSubdivision(const int subdiv) noexcept {
  return subdiv >= 4 ? 4 : (subdiv >= 2 ? 2 : 1);
}

bool MetronomeEngine::isLayerEnabled(const int mask,
                                     const ClickLayer layer) noexcept {
  const int normalized = normalizeDivisionMask(mask);
  switch (layer) {
  case ClickLayer::downbeat:
    return (normalized & 0x01) != 0;
  case ClickLayer::eighth:
    return (normalized & 0x02) != 0;
  case ClickLayer::sixteenth:
    return (normalized & 0x04) != 0;
  }
  return false;
}

bool MetronomeEngine::loadSampleFile(juce::File const &file,
                                     juce::AudioFormatManager &formatManager,
                                     ClickSample &sample) {
  auto reader =
      std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file));
  if (reader == nullptr)
    return false;

  const int channels =
      juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
  const int lengthSamples = static_cast<int>(
      juce::jlimit<juce::int64>(0, std::numeric_limits<int>::max(),
                                reader->lengthInSamples));
  if (lengthSamples <= 0)
    return false;

  ClickSample next;
  next.data.setSize(channels, lengthSamples);
  if (!reader->read(&next.data, 0, lengthSamples, 0, true, channels > 1))
    return false;
  next.sourceSampleRate =
      reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
  float peak = 0.0f;
  for (int ch = 0; ch < channels; ++ch)
    peak = juce::jmax(peak, next.data.getMagnitude(ch, 0, lengthSamples));
  next.normalizationGain =
      peak > 0.00001f ? juce::jlimit(0.0f, 8.0f, 0.98f / peak) : 1.0f;
  sample = std::move(next);
  return true;
}

void MetronomeEngine::triggerClick(const ClickLayer layer,
                                   const bool mainBeat,
                                   const float triggerGain) noexcept {
  const float layerGainScale = layer == ClickLayer::eighth
                                   ? kEighthClickGainScale
                                   : (layer == ClickLayer::sixteenth
                                          ? kSixteenthClickGainScale
                                          : 1.0f);
  const float adjustedTriggerGain = triggerGain * layerGainScale;
  if (auto *samples = publishedClickSamples.load(std::memory_order_acquire)) {
    if (auto *sampleToUse = selectSampleForTrigger(*samples, layer);
        sampleToUse != nullptr &&
        triggerSampleClick(samples, sampleToUse, adjustedTriggerGain)) {
      if (layer != ClickLayer::downbeat)
        triggerSyntheticClick(layer, mainBeat,
                              (layer == ClickLayer::eighth ? 0.085f : 0.065f) *
                                  layerGainScale);
      return;
    }
  }

  triggerSyntheticClick(layer, mainBeat, adjustedTriggerGain);
}

const MetronomeEngine::ClickSample *MetronomeEngine::selectSampleForTrigger(
    const ClickSampleSet &sampleSet, const ClickLayer layer) const noexcept {
  switch (layer) {
  case ClickLayer::downbeat:
    // Test mode: ignore the dedicated accents sample so the downbeat uses
    // the same timbre as the base click.
    if (sampleSet.quarter.isValid())
      return &sampleSet.quarter;
    break;
  case ClickLayer::eighth:
    if (sampleSet.eighth.isValid())
      return &sampleSet.eighth;
    if (sampleSet.quarter.isValid())
      return &sampleSet.quarter;
    break;
  case ClickLayer::sixteenth:
    if (sampleSet.sixteenth.isValid())
      return &sampleSet.sixteenth;
    if (sampleSet.eighth.isValid())
      return &sampleSet.eighth;
    if (sampleSet.quarter.isValid())
      return &sampleSet.quarter;
    break;
  }
  return nullptr;
}

void MetronomeEngine::triggerSyntheticClick(const ClickLayer layer,
                                            const bool mainBeat,
                                            const float triggerGain) noexcept {
  clickSamplesRemaining = juce::jmax(8, static_cast<int>(sampleRate * 0.025));
  clickGain = triggerGain;
  currentClickIsMainBeat = mainBeat;
  currentClickTone = layer == ClickLayer::downbeat
                         ? ClickTone::downbeat
                         : (layer == ClickLayer::eighth ? ClickTone::eighth
                                                        : ClickTone::sixteenth);
}

bool MetronomeEngine::publishClickSamples(
    std::shared_ptr<const ClickSampleSet> nextSamples) noexcept {
  if (nextSamples == nullptr) {
    publishedClickSamples.store(nullptr, std::memory_order_release);
    return true;
  }

  const auto *rawSamples = nextSamples.get();
  try {
    const juce::ScopedLock lock(clickSamplesGenerationLock);
    clickSampleGenerations.push_back(std::move(nextSamples));
  } catch (...) {
    return false;
  }

  publishedClickSamples.store(rawSamples, std::memory_order_release);
  return true;
}

bool MetronomeEngine::triggerSampleClick(
    const ClickSampleSet *sampleSet, const ClickSample *sample,
    const float triggerGain) noexcept {
  if (sampleSet == nullptr || sample == nullptr || !sample->isValid() ||
      sampleRate <= 0.0)
    return false;

  SampleVoice *target = nullptr;
  for (auto &voice : sampleVoices) {
    if (!voice.isActive()) {
      target = &voice;
      break;
    }
  }
  if (target == nullptr)
    target = &sampleVoices.front();

  target->sampleSet = sampleSet;
  target->sample = sample;
  target->position = 0.0;
  target->increment = sample->sourceSampleRate / sampleRate;
  if (!(target->increment > 0.0))
    target->increment = 1.0;
  target->gain = triggerGain;
  return true;
}

bool MetronomeEngine::hasActiveSampleVoices() const noexcept {
  for (const auto &voice : sampleVoices) {
    if (voice.isActive())
      return true;
  }
  return false;
}

float MetronomeEngine::renderSyntheticClick() noexcept {
  if (clickSamplesRemaining <= 0 || sampleRate <= 0.0)
    return 0.0f;

  const int clickLength =
      currentClickTone == ClickTone::sixteenth
          ? juce::jmax(6, static_cast<int>(sampleRate * 0.018))
          : currentClickTone == ClickTone::eighth
                ? juce::jmax(7, static_cast<int>(sampleRate * 0.022))
                : juce::jmax(8, static_cast<int>(sampleRate * 0.028));
  const int idx = clickLength - clickSamplesRemaining;
  const float env =
      std::exp((currentClickTone == ClickTone::sixteenth ? -8.0f : currentClickTone == ClickTone::eighth ? -6.8f : -5.8f) *
               static_cast<float>(idx) /
               static_cast<float>(clickLength));
  const double freq = currentClickTone == ClickTone::downbeat
                          ? 880.0
                          : currentClickTone == ClickTone::eighth ? 1250.0
                                                                  : 1650.0;
  const float value = static_cast<float>(
                          std::sin(juce::MathConstants<double>::twoPi * freq *
                                   (static_cast<double>(idx) / sampleRate))) *
                      env * clickGain;
  --clickSamplesRemaining;
  return value;
}

float MetronomeEngine::renderSampleVoices() noexcept {
  float mixed = 0.0f;
  for (auto &voice : sampleVoices) {
    if (!voice.isActive())
      continue;

    const auto &data = voice.sample->data;
    const int srcSamples = data.getNumSamples();
    const int srcChannels = data.getNumChannels();
    const int pos0 = static_cast<int>(voice.position);
    if (pos0 >= srcSamples) {
      voice = {};
      continue;
    }

    int pos1 = pos0 + 1;
    if (pos1 >= srcSamples)
      pos1 = srcSamples - 1;
    const float frac =
        static_cast<float>(voice.position - static_cast<double>(pos0));

    float sampleValue = 0.0f;
    for (int ch = 0; ch < srcChannels; ++ch) {
      const auto *src = data.getReadPointer(ch);
      sampleValue += src[pos0] + (src[pos1] - src[pos0]) * frac;
    }
    sampleValue /= static_cast<float>(juce::jmax(1, srcChannels));
    mixed += sampleValue * voice.gain * voice.sample->normalizationGain;

    voice.position += voice.increment;
    if (voice.position >= static_cast<double>(srcSamples))
      voice = {};
  }
  return mixed;
}
