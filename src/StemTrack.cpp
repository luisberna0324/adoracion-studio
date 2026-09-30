#include "StemTrack.h"

#include <cmath>

StemTrack::StemTrack(std::unique_ptr<juce::AudioFormatReader> reader,
                     juce::TimeSliceThread &sharedReadThread,
                     double sampleRateToUse, int readAheadSamples,
                     const bool useReadAhead,
                     std::unique_ptr<juce::AudioFormatReader> prefetchReader) {
  // Transfer ownership of reader to AudioFormatReaderSource
  readerSource =
      std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

  // Resample to the host sample rate if needed, then buffer on a background thread.
  resamplingSource = std::make_unique<ResamplingPositionableSource>(
      readerSource.get(), sampleRateToUse, 2);

  // P0.2 (perf): diferir la construcción de la cadena de prefetch. El
  // segundo AudioFormatReader se guarda crudo y se materializa en
  // ensurePrefetchChain() la primera vez que se necesita. Esto evita
  // el segundo FileInputStream::open y la duplicación del costo de AV
  // en Windows durante el loadSongStems. El bufferingSource arranca
  // sin prefetch source; requestPrefetchPosition lo inyecta.
  deferredPrefetchReader = std::move(prefetchReader);
  prefetchSampleRate = sampleRateToUse;
  // Se ajustará con la primera llamada a ensurePrefetchChain; por
  // ahora un valor seguro para que ensurePrefetchChain no asuma cero.
  prefetchBlockSize = 512;

  if (useReadAhead) {
    // Realtime playback must never block on disk I/O.
    bufferingSource = std::make_unique<StemBufferingSource>(
        resamplingSource.get(), sharedReadThread, false, readAheadSamples, 2,
        true, nullptr);
    activeSource = bufferingSource.get();
  } else {
    // Offline rendering advances faster than a read-ahead worker can refill;
    // synchronous reads are deterministic and cannot create buffer-miss gaps.
    activeSource = resamplingSource.get();
  }
}

StemTrack::~StemTrack() { bufferingSource.reset(); }

void StemTrack::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  jassert(activeSource != nullptr);
  int discontinuityOffset = -1;
  int discontinuityFade = 0;
  int leadingFadeFrames = pendingLeadingDiscontinuityFadeFrames;
  pendingLeadingDiscontinuityFadeFrames = 0;
  if (readDiscontinuityPending) {
    const int prefixFrames =
        juce::jlimit(0, bufferToFill.numSamples,
                     readDiscontinuityPrefixFrames);
    const auto targetFrame = readDiscontinuityTargetFrame;
    discontinuityFade = readDiscontinuityFadeFrames;
    readDiscontinuityPending = false;

    if (prefixFrames > 0) {
      juce::AudioSourceChannelInfo prefixInfo(
          bufferToFill.buffer, bufferToFill.startSample, prefixFrames);
      activeSource->getNextAudioBlock(prefixInfo);
    }

    activeSource->setNextReadPosition(targetFrame);
    if (prefixFrames < bufferToFill.numSamples) {
      juce::AudioSourceChannelInfo suffixInfo(
          bufferToFill.buffer, bufferToFill.startSample + prefixFrames,
          bufferToFill.numSamples - prefixFrames);
      activeSource->getNextAudioBlock(suffixInfo);
    }
    discontinuityOffset = prefixFrames;
    if (prefixFrames == 0)
      leadingFadeFrames = juce::jmax(leadingFadeFrames, discontinuityFade);
    else if (prefixFrames == bufferToFill.numSamples)
      pendingLeadingDiscontinuityFadeFrames =
          juce::jmax(pendingLeadingDiscontinuityFadeFrames,
                     discontinuityFade);
  } else {
    // Read from the buffering source directly into the provided temporary
    // buffer.
    activeSource->getNextAudioBlock(bufferToFill);
  }

  const int numSamples = bufferToFill.numSamples;
  const int numChannels = bufferToFill.buffer->getNumChannels();
  const int startSample = bufferToFill.startSample;

  if (leadingFadeFrames > 0 && numSamples > 0) {
    const int fadeFrames = juce::jmin(leadingFadeFrames, numSamples);
    constexpr float halfPi = juce::MathConstants<float>::halfPi;
    for (int sample = 0; sample < fadeFrames; ++sample) {
      const float phase =
          static_cast<float>(sample + 1) / static_cast<float>(fadeFrames);
      const float gain = std::sin(halfPi * phase);
      for (int channel = 0; channel < numChannels; ++channel) {
        bufferToFill.buffer->applyGain(
            channel, startSample + sample, 1, gain);
      }
    }
  }

  if (discontinuityOffset > 0 && discontinuityOffset < numSamples &&
      discontinuityFade > 0) {
    const int fadeFrames =
        juce::jmin(discontinuityFade,
                   juce::jmin(discontinuityOffset,
                              numSamples - discontinuityOffset));
    constexpr float halfPi = juce::MathConstants<float>::halfPi;
    for (int sample = 0; sample < fadeFrames; ++sample) {
      const float phase =
          static_cast<float>(sample + 1) /
          static_cast<float>(fadeFrames);
      const float fadeOut = std::cos(halfPi * phase);
      const float fadeIn = std::sin(halfPi * phase);
      const int outgoingSample =
          startSample + discontinuityOffset - fadeFrames + sample;
      const int incomingSample = startSample + discontinuityOffset + sample;
      for (int channel = 0; channel < numChannels; ++channel) {
        bufferToFill.buffer->setSample(
            channel, outgoingSample,
            bufferToFill.buffer->getSample(channel, outgoingSample) *
                fadeOut);
        bufferToFill.buffer->setSample(
            channel, incomingSample,
            bufferToFill.buffer->getSample(channel, incomingSample) * fadeIn);
      }
    }
  } else if (discontinuityOffset == numSamples &&
             discontinuityFade > 0) {
    const int fadeFrames = juce::jmin(discontinuityFade, numSamples);
    constexpr float halfPi = juce::MathConstants<float>::halfPi;
    for (int sample = 0; sample < fadeFrames; ++sample) {
      const float phase =
          static_cast<float>(sample + 1) / static_cast<float>(fadeFrames);
      const float gain = std::cos(halfPi * phase);
      const int outgoingSample =
          startSample + numSamples - fadeFrames + sample;
      for (int channel = 0; channel < numChannels; ++channel) {
        bufferToFill.buffer->applyGain(channel, outgoingSample, 1, gain);
      }
    }
  }

  // Every render route reads through StemTrack, including stretched and routed
  // stems. Smooth only this temporary factor, preserving the saved mix controls.
  rehearsalRamp.setTargetValue(rehearsalGain.load(std::memory_order_relaxed));
  if (rehearsalRamp.isSmoothing()) {
    for (int sample = startSample; sample < startSample + numSamples; ++sample) {
      const auto gain = rehearsalRamp.getNextValue();
      for (int channel = 0; channel < numChannels; ++channel)
        bufferToFill.buffer->getWritePointer(channel)[sample] *= gain;
    }
  } else if (rehearsalRamp.getCurrentValue() != 1.0f) {
    bufferToFill.buffer->applyGain(startSample, numSamples, rehearsalRamp.getCurrentValue());
  }

  const bool isAudible =
      !isMuted() && (!isAnySoloActive.load(std::memory_order_relaxed) || isSoloed());
      
  const float userVolume = juce::jlimit(0.0f, 1.0f, getVolume());
  const float trimGain = juce::jlimit(0.0f, 2.0f, getTrim());
  const float mixGain = globalMixGain.load(std::memory_order_relaxed);
  const float stemGain = isAudible ? (userVolume * trimGain * mixGain) : 0.0f;

  // The VU needs the same input peak. When diagnostics are active, reuse the
  // integrity pass instead of traversing every stem buffer twice.
  float magnitude = 0.0f;
  if (signalIntegrityFullCaptureEnabled) {
    signalIntegrityMetrics = playworship::audio::measureSignalIntegrity(
        *bufferToFill.buffer, startSample, numSamples, stemGain,
        signalIntegrityState);
    magnitude = signalIntegrityMetrics.inputPeak;
  } else {
    for (int channel = 0; channel < numChannels; ++channel) {
      magnitude = juce::jmax(
          magnitude,
          bufferToFill.buffer->getMagnitude(channel, startSample, numSamples));
    }
    if (signalIntegrityTrackingEnabled) {
      playworship::audio::updateSignalIntegrityStateFromBufferEnd(
          *bufferToFill.buffer, startSample, numSamples, stemGain,
          signalIntegrityState);
    }
  }

  if (stemGain > 0.0f) {
    bufferToFill.buffer->applyGain(startSample, numSamples, stemGain);
  } else {
    bufferToFill.clearActiveBufferRegion();
  }

  // Update meter reading
  const float meterGain = isAudible ? (userVolume * trimGain) : 0.0f;
  currentMagnitude.store(magnitude * meterGain, std::memory_order_relaxed);
}

void StemTrack::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
  rehearsalRamp.reset(sampleRate, 0.05);
  rehearsalRamp.setCurrentAndTargetValue(rehearsalGain.load(std::memory_order_relaxed));
  readDiscontinuityPending = false;
  pendingLeadingDiscontinuityFadeFrames = 0;
  signalIntegrityState.reset();
  activeSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
  // P0.2 (perf): capturar el block size real del host para que cuando
  // la cadena de prefetch se construya en lazy, use el valor vigente
  // y no un default hardcodeado.
  prefetchBlockSize = samplesPerBlockExpected;
  prefetchSampleRate = sampleRate;
}

void StemTrack::releaseResources() {
  readDiscontinuityPending = false;
  pendingLeadingDiscontinuityFadeFrames = 0;
  signalIntegrityState.reset();
  activeSource->releaseResources();
}

void StemTrack::setNextReadPosition(juce::int64 newPosition) {
  readDiscontinuityPending = false;
  pendingLeadingDiscontinuityFadeFrames = 0;
  signalIntegrityState.reset();
  activeSource->setNextReadPosition(newPosition);
}

juce::int64 StemTrack::getNextReadPosition() const {
  return activeSource->getNextReadPosition();
}

juce::int64 StemTrack::getTotalLength() const {
  return activeSource->getTotalLength();
}

bool StemTrack::isLooping() const { return activeSource->isLooping(); }

void StemTrack::setLooping(bool shouldLoop) {
  activeSource->setLooping(shouldLoop);
}

bool StemTrack::waitForNextAudioBlockReady(const int numSamples,
                                           const juce::uint32 timeoutMs) const {
  if (bufferingSource == nullptr || numSamples <= 0)
    return true;

  juce::AudioBuffer<float> readinessBuffer(2, numSamples);
  juce::AudioSourceChannelInfo info(&readinessBuffer, 0, numSamples);
  return bufferingSource->waitForNextAudioBlockReady(info, timeoutMs);
}

void StemTrack::requestPrefetchPosition(const juce::int64 position,
                                        const int minimumFrames) noexcept {
  if (bufferingSource == nullptr)
    return;
  // P0.2 (perf): construir la cadena de prefetch en lazy la primera
  // vez que se pide. Esto saca el segundo createReaderFor y su costo
  // de AV del path crítico del loadSongStems. Llamar esto desde el
  // message thread es seguro: el I/O se paga una vez por canción
  // cuando el usuario solicita un seek/loop/arrangement, no durante
  // el cambio de canción.
  ensurePrefetchChain();
  bufferingSource->requestPrefetchPosition(position, minimumFrames);
}

void StemTrack::ensurePrefetchChain() {
  // Solo se construye una vez por track. std::call_once garantiza
  // que si dos threads llaman esto en paralelo, solo uno construye.
  std::call_once(prefetchChainBuilt, [this]() {
    if (deferredPrefetchReader == nullptr || bufferingSource == nullptr)
      return;
    if (prefetchReaderSource != nullptr)
      return;  // ya construido (defensa)

    prefetchReaderSource =
        std::make_unique<juce::AudioFormatReaderSource>(
            deferredPrefetchReader.release(), true);
    prefetchResamplingSource = std::make_unique<ResamplingPositionableSource>(
        prefetchReaderSource.get(), prefetchSampleRate, 2);

    // Preparar la fuente ya con el sample rate del host para que el
    // primer read del worker no tenga que hacerlo después.
    if (prefetchBlockSize > 0 && prefetchSampleRate > 0.0) {
      prefetchResamplingSource->prepareToPlay(prefetchBlockSize,
                                              prefetchSampleRate);
    }

    // Inyectar en el buffering source. setPrefetchSource re-asigna el
    // prefetch buffer y bumpea la generación para invalidar cualquier
    // request pendiente contra la cadena vieja.
    bufferingSource->setPrefetchSource(prefetchResamplingSource.get(),
                                       static_cast<int>(prefetchSampleRate),
                                       prefetchBlockSize);
  });
}

bool StemTrack::isPrefetchReady(const juce::int64 position,
                                const int minimumFrames) const noexcept {
  return bufferingSource == nullptr ||
         bufferingSource->isPrefetchReady(position, minimumFrames);
}

void StemTrack::scheduleReadDiscontinuity(
    const int prefixFrames, const juce::int64 targetFrame,
    const int fadeFrames) noexcept {
  readDiscontinuityPrefixFrames = juce::jmax(0, prefixFrames);
  readDiscontinuityTargetFrame = juce::jmax<juce::int64>(0, targetFrame);
  readDiscontinuityFadeFrames = juce::jmax(0, fadeFrames);
  readDiscontinuityPending = true;
}
