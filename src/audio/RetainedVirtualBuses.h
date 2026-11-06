#pragma once

#include "../MetronomeEngine.h"
#include "DynamicGuideEngine.h"
#include "DynamicGuideEvents.h"
#include "SectionScheduler.h"
#include "SimpleDelayLine.h"
#include "StemOutputRouter.h"

#include <array>
#include <utility>

namespace playworship::audio {
// Independently owned outgoing click/guide state. The catalogs are shared on
// the control thread; capture preserves voice cursors, grid phase and delay
// histories without allocating in the audio callback.
class RetainedVirtualBuses {
public:
  struct MixSnapshot {
    bool clickEnabled = false, guideEnabled = false;
    bool clickAudible = true, guideAudible = true;
    float clickVolume = 1.0f, clickGain = 1.0f, guideGain = 1.0f;
    int clickChannel = 1, clickWidth = 1, guideChannel = 1, guideWidth = 1;
    double originalBpm = 120.0, effectiveBpm = 120.0;
  };

  void prepare(int maxFrames, double rate) {
    capacityFrames = maxFrames;
    clickBuffer.setSize(2, maxFrames); guideBuffer.setSize(2, maxFrames);
    click.prepare(rate); guide.prepare(rate);
    clickDelay.prepare(2, 65536); guideDelay.prepare(2, 65536);
  }
  void prepareRetainedCopyFrom(MetronomeEngine &sourceClick, const DynamicGuideEngine &sourceGuide) {
    click.prepareRetainedCopyFrom(sourceClick);
    guide.prepareRetainedCopyFrom(sourceGuide);
    clickDelay.reset(); guideDelay.reset();
  }
  void captureRealtimeStateFrom(const MetronomeEngine &sourceClick,
      const DynamicGuideEngine &sourceGuide, SimpleDelayLine &sourceClickDelay,
      SimpleDelayLine &sourceGuideDelay, const SectionScheduler::Snapshot &sourceTimeline,
      const std::array<uint8_t, SectionScheduler::maxSections> &sourceAnnounced,
      const MixSnapshot &sourceMix) noexcept {
    click.captureRealtimeStateFrom(sourceClick); guide.captureRealtimeStateFrom(sourceGuide);
    std::swap(clickDelay, sourceClickDelay); std::swap(guideDelay, sourceGuideDelay);
    timeline = sourceTimeline; announced = sourceAnnounced; mix = sourceMix;
    click.setEnabled(mix.clickEnabled);
    click.setMuted(!mix.clickAudible); click.setVolume(mix.clickVolume);
  }
  void setEffectiveBpm(double bpm) noexcept {
    if (std::isfinite(bpm) && bpm > 0.0) mix.effectiveBpm = bpm;
  }
  void addTo(juce::AudioBuffer<float> &output, juce::int64 sourceStart,
      int inputFrames, int outputFrames, double sourceRate, int stemLatency,
      bool sourceEnded = false, int sourceOutputFrames = -1) noexcept {
    if (outputFrames <= 0 || outputFrames > capacityFrames || sourceRate <= 0.0) return;
    juce::AudioBuffer<float> clickSpan(clickBuffer.getArrayOfWritePointers(), 2, 0, outputFrames);
    juce::AudioBuffer<float> guideSpan(guideBuffer.getArrayOfWritePointers(), 2, 0, outputFrames);
    clickSpan.clear(); guideSpan.clear();
    const int rawFrames = sourceEnded ? 0 : (sourceOutputFrames < 0 ? outputFrames
        : juce::jlimit(0, outputFrames, sourceOutputFrames));
    if (mix.clickEnabled) {
      if (rawFrames > 0) {
        const double beat = (juce::jmax<juce::int64>(0, sourceStart) / sourceRate) * mix.originalBpm / 60.0;
        click.syncPhaseForBeatPosition(beat, mix.effectiveBpm);
        click.process(clickSpan, 0, rawFrames);
      }
      clickDelay.setDelay(stemLatency); clickDelay.process(clickSpan);
      clickSpan.applyGain(mix.clickGain);
      route(clickSpan, mix.clickChannel, mix.clickWidth, output);
    }
    if (mix.guideEnabled && rawFrames > 0) {
      DynamicGuideEvents events;
      const auto endFrame = sourceStart + inputFrames;
      if (inputFrames > 0 && timeline.valid) {
        for (int index = 0; index < timeline.count; ++index) {
          const auto &part = timeline.sections[(size_t) index];
          if (part.skipPlayback || announced[(size_t) index] || part.guideTriggerId < 0
              || part.guideTriggerFrame < sourceStart || part.guideTriggerFrame >= endFrame) continue;
          const int offset = juce::jlimit(0, rawFrames - 1, (int) std::llround(
              (double) (part.guideTriggerFrame - sourceStart) * rawFrames / inputFrames));
          events.add(offset, part.guideTriggerId); announced[(size_t) index] = 1;
        }
        events.addCues(timeline, sourceStart, inputFrames, 0, rawFrames);
      }
      events.render(guide, guideSpan, 0, rawFrames);
    }
    // Advance both the continuing voices and their existing delay even when
    // inaudible; mute/solo never resurfaces an old phrase from a frozen ring.
    if (!mix.guideEnabled || !mix.guideAudible) guideSpan.clear();
    guideDelay.setDelay(stemLatency); guideDelay.process(guideSpan);
    if (mix.guideEnabled && mix.guideAudible) {
      guideSpan.applyGain(mix.guideGain);
      route(guideSpan, mix.guideChannel, mix.guideWidth, output);
    }
  }
private:
  void route(const juce::AudioBuffer<float> &source, int channel, int width,
             juce::AudioBuffer<float> &output) noexcept {
    width = juce::jlimit(1, 2, width);
    if (output.getNumChannels() < 2) width = 1;
    channel = juce::jlimit(1, juce::jmax(1, output.getNumChannels()), channel);
    if (width == 2 && channel >= output.getNumChannels()) channel = juce::jmax(1, output.getNumChannels() - 1);
    router.routeStemToOutputChannels(source, channel - 1, width, output);
  }
  MetronomeEngine click;
  DynamicGuideEngine guide;
  SimpleDelayLine clickDelay, guideDelay;
  juce::AudioBuffer<float> clickBuffer, guideBuffer;
  StemOutputRouter router;
  SectionScheduler::Snapshot timeline;
  std::array<uint8_t, SectionScheduler::maxSections> announced{};
  MixSnapshot mix;
  int capacityFrames = 0;
};
}
