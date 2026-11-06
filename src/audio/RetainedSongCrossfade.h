#pragma once
#include "../StemTrack.h"
#include "../StemMixEngine.h"
#include "SimpleDelayLine.h"
#include "StemOutputRouter.h"
#include "RetainedVirtualBuses.h"
#include <array>

namespace playworship::audio {
// Nonmovable Bungee instances and delay histories travel together by pointer.
// Allocate/prepare each bank on the control side; capture swaps owners only.
struct SongProcessingState {
  static constexpr int maxTracks = 64;
  StemMixEngine mixEngine;
  std::array<TimeStretchProcessor, maxTracks> pitchProcessors;
  std::array<SimpleDelayLine, maxTracks> delayLines;
  std::atomic<int> preparedCount{0};
};

class RetainedSongCrossfade {
public:
  static constexpr int maxTracks = SongProcessingState::maxTracks;
  void prepare(int maxFrames, int outputChannels, double rate) {
    temp.setSize(2, maxFrames); input.setSize(2, maxFrames * 2 + 8);
    groupedScratch.setSize(2, maxFrames); mix.setSize(outputChannels, maxFrames);
    virtualBuses.prepare(maxFrames, rate);
    capacityFrames = maxFrames; capacityChannels = outputChannels;
  }
  void clear() {
    tracks.clear();
    dsp = std::make_unique<SongProcessingState>();
    remainingFrames.store(0, std::memory_order_relaxed);
  }
  bool empty() const noexcept { return tracks.empty(); }
  float capturedMasterGain() const noexcept { return masterGain; }
  RetainedVirtualBuses& buses() noexcept { return virtualBuses; }
  void capture(std::vector<std::unique_ptr<StemTrack>>& source,
      std::unique_ptr<SongProcessingState>& activeDsp,
      const std::array<std::atomic<int>, maxTracks>& outputChannels,
      const std::array<std::atomic<int>, maxTracks>& outputWidths,
      const std::array<std::atomic<bool>, maxTracks>& clickGuide,
      const std::array<std::atomic<bool>, maxTracks>& pitchShift,
      float master, juce::int64 sourceFrame, juce::int64 endFrame,
      juce::int64 fadeOutFrames, int capturedPitch, double ratio,
      int latency, double rate,
      double rampStart, double rampTarget, int rampElapsed, int rampTotal, double originalTempo) noexcept {
    tracks.swap(source); dsp.swap(activeDsp); masterGain = master;
    sourcePosition = sourceFrame; sourceEnd = endFrame; windowFadeFrames = fadeOutFrames;
    pitch = capturedPitch; tempoRatio = ratio; sampleRate = rate; latencySamples = latency; originalBpm = originalTempo;
    tempoRampStart = rampStart; tempoRampTarget = rampTarget;
    tempoRampElapsed = rampElapsed; tempoRampTotal = rampTotal;
    for (size_t i = 0; i < tracks.size(); ++i) {
      channels[i].store(outputChannels[i].load(std::memory_order_relaxed));
      widths[i].store(outputWidths[i].load(std::memory_order_relaxed));
      clickGuideStems[i].store(clickGuide[i].load(std::memory_order_relaxed));
      pitchStems[i].store(pitchShift[i].load(std::memory_order_relaxed));
      tracks[i]->clearMixStateTransitions();
    }
    outputRenderedAfterSourceEnd = 0;
    updateRemaining();
  }
  juce::int64 remaining() const noexcept { return remainingFrames.load(std::memory_order_relaxed); }
  int servicePendingRebuilds() noexcept {
    int rebuilt = dsp->mixEngine.servicePendingProcessorRebuilds();
    for (int i = 0; i < dsp->preparedCount.load(std::memory_order_acquire); ++i)
      rebuilt += dsp->pitchProcessors[(size_t)i].servicePendingRebuild() ? 1 : 0;
    return rebuilt;
  }
  void addTo(juce::AudioBuffer<float>& output, float startGain, float endGain, int fadeFrames = -1) noexcept {
    const int n = output.getNumSamples(), outputChannels = output.getNumChannels();
    if (n > capacityFrames || outputChannels > capacityChannels) return;
    mix.setSize(outputChannels, n, false, false, true); mix.clear();
    if (tracks.empty() || remaining() <= 0) return;

    if (tempoRampElapsed < tempoRampTotal) {
      const double t = (double) juce::jmin(tempoRampTotal, tempoRampElapsed + n / 2) / tempoRampTotal;
      tempoRatio = tempoRampStart + (tempoRampTarget - tempoRampStart) * t * t * (3.0 - 2.0 * t);
      tempoRampElapsed = juce::jmin(tempoRampTotal, tempoRampElapsed + n);
    } else tempoRatio = tempoRampTarget;
    dsp->mixEngine.setPitchSemiTones(pitch); dsp->mixEngine.setTempoRatio(tempoRatio);
    const bool timeActive = pitch != 0 || !juce::approximatelyEqual(tempoRatio, 1.0);
    const bool tempoActive = !juce::approximatelyEqual(tempoRatio, 1.0);
    const int requiredInput = timeActive ? dsp->mixEngine.requiredInputFrames(n) : n;
    const int readFrames = (int) juce::jlimit<juce::int64>(0, requiredInput, sourceEnd - sourcePosition);
    const auto blockSourceStart = sourcePosition;
    const float windowStart = windowFadeAt(sourcePosition);
    const float windowEnd = windowFadeAt(sourcePosition + requiredInput);
    bool routeActive = false, homogeneous = true, pitchCompatible = true;
    int commonChannel = channels[0].load(), commonWidth = widths[0].load();
    for (size_t i = 0; i < tracks.size(); ++i) {
      const int channel = channels[i].load(), width = widths[i].load();
      routeActive = routeActive || channel > 1 || (outputChannels > 1 && width == 1);
      homogeneous = homogeneous && channel == commonChannel && width == commonWidth;
      pitchCompatible = pitchCompatible && !(pitch != 0 && clickGuideStems[i].load() && pitchStems[i].load());
    }
    const bool groupedStereo = outputChannels == 2;
    const bool grouped = (!routeActive || homogeneous || groupedStereo) && pitchCompatible;
    bool blocked = false;
    if (grouped && (timeActive || !routeActive)) {
      dsp->mixEngine.mixStems(tracks, clickGuideStems.data(), pitchStems.data(), (int) tracks.size(),
          requiredInput, outputChannels, routeActive && groupedStereo ? channels.data() : nullptr,
          routeActive && groupedStereo ? widths.data() : nullptr, readFrames);
      const bool routeCommon = routeActive && !groupedStereo;
      auto& rendered = routeCommon ? groupedScratch : mix;
      rendered.clear(0, n);
      dsp->mixEngine.renderToOutput(rendered, timeActive, requiredInput, n, 1.0f, 1.0f, windowStart, windowEnd);
      blocked = dsp->mixEngine.wasLastRenderBlocked();
      latencySamples = dsp->mixEngine.getOutputLatencySamples();
      if (routeCommon) route(rendered, commonChannel, commonWidth, mix);
    } else {
      std::array<int, maxTracks> latencies{};
      bool ready = true;
      int targetLatency = 0;
      for (size_t i = 0; i < tracks.size(); ++i) {
        if (timeActive && (tempoActive || pitchStems[i].load())) {
          auto& proc = dsp->pitchProcessors[i];
          proc.setPitchSemiTones(pitchStems[i].load() ? pitch : 0);
          proc.setTempoRatio(tempoRatio);
          ready = proc.tryResolvePendingReset() && proc.isPrepared() && ready;
          latencies[i] = proc.getLatencySamples();
          targetLatency = juce::jmax(targetLatency, latencies[i]);
        }
      }
      latencySamples = targetLatency;
      for (size_t i = 0; i < tracks.size(); ++i) {
        input.clear(0, requiredInput);
        juce::AudioBuffer<float> scratch(temp.getArrayOfWritePointers(), 2, 0, n); scratch.clear();
        const bool process = timeActive && (tempoActive || pitchStems[i].load());
        if (process) {
          juce::AudioSourceChannelInfo info(&input, 0, readFrames); tracks[i]->getNextAudioBlock(info);
          if (ready) dsp->pitchProcessors[i].process(input, requiredInput, scratch, n);
        } else {
          juce::AudioSourceChannelInfo info(&scratch, 0, readFrames); tracks[i]->getNextAudioBlock(info);
        }
        auto& delay = dsp->delayLines[i];
        delay.setDelay(juce::jmax(0, targetLatency - latencies[i]));
        if (ready && delay.isActive()) delay.process(scratch);
        if (!ready) { scratch.clear(); delay.invalidateHistory(); }
        if (!clickGuideStems[i].load()) scratch.applyGainRamp(0, n, windowStart, windowEnd);
        route(scratch, channels[i].load(), widths[i].load(), mix);
      }
      blocked = !ready;
    }
    sourcePosition += readFrames;
    if (blockSourceStart >= sourceEnd) outputRenderedAfterSourceEnd += n;
    else if (sourcePosition >= sourceEnd) outputRenderedAfterSourceEnd += juce::jmax(0, n - (int) std::ceil(readFrames / tempoRatio));
    virtualBuses.setEffectiveBpm(originalBpm * tempoRatio);
    virtualBuses.addTo(mix, blockSourceStart, readFrames, n, sampleRate, latencySamples, blockSourceStart >= sourceEnd,
        (int) juce::jlimit<juce::int64>(0, n, (juce::int64)std::ceil((sourceEnd-blockSourceStart)/tempoRatio)));
    if (blocked) mix.clear();
    updateRemaining();
    const int rampFrames = fadeFrames < 0 ? n : juce::jmax(1, fadeFrames);
    for (int ch = 0; ch < outputChannels; ++ch) {
      auto* destination = output.getWritePointer(ch); const auto* source = mix.getReadPointer(ch);
      for (int sample = 0; sample < n; ++sample) {
        const float progress = juce::jmin(1.0f, (float) sample / (float) rampFrames);
        destination[sample] += source[sample] * masterGain * (startGain + (endGain - startGain) * progress);
      }
    }
  }
private:
  float windowFadeAt(juce::int64 frame) const noexcept {
    return windowFadeFrames > 0 ? juce::jlimit(0.0f, 1.0f, (float)(sourceEnd-frame)/(float)windowFadeFrames) : 1.0f;
  }
  void updateRemaining() noexcept {
    const auto sourceRemaining = juce::jmax<juce::int64>(0, sourceEnd-sourcePosition);
    remainingFrames.store(juce::jmax<juce::int64>(0,
        (juce::int64) std::ceil(sourceRemaining / juce::jmax(0.01, tempoRatio))
          + latencySamples - outputRenderedAfterSourceEnd), std::memory_order_relaxed);
  }
  void route(const juce::AudioBuffer<float>& source, int channel, int width, juce::AudioBuffer<float>& target) noexcept {
    width = juce::jlimit(1, target.getNumChannels() < 2 ? 1 : 2, width);
    channel = juce::jlimit(1, juce::jmax(1, target.getNumChannels() - (width == 2 ? 1 : 0)), channel);
    router.routeStemToOutputChannels(source, channel-1, width, target);
  }
  std::vector<std::unique_ptr<StemTrack>> tracks;
  std::unique_ptr<SongProcessingState> dsp = std::make_unique<SongProcessingState>();
  std::array<std::atomic<int>, maxTracks> channels{}, widths{};
  std::array<std::atomic<bool>, maxTracks> clickGuideStems{}, pitchStems{};
  RetainedVirtualBuses virtualBuses;
  juce::int64 sourcePosition = 0, sourceEnd = 0, windowFadeFrames = 0, outputRenderedAfterSourceEnd = 0;
  int pitch = 0, latencySamples = 0;
  double originalBpm = 120.0;
  double tempoRatio = 1.0, sampleRate = 44100.0, tempoRampStart = 1.0, tempoRampTarget = 1.0;
  int tempoRampElapsed = 0, tempoRampTotal = 0;
  juce::AudioBuffer<float> temp, input, groupedScratch, mix;
  StemOutputRouter router;
  float masterGain = 1.0f;
  int capacityFrames = 0, capacityChannels = 0;
  std::atomic<juce::int64> remainingFrames{0};
};
}
