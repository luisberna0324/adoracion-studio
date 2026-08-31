#pragma once

#include "data/AppSettings.h"
#include "data/SessionPersistence.h"
#include "data/SongData.h"
#include <JuceHeader.h>

namespace PluginEditorSupport {

extern const int kDefaultTopBarH;
extern const int kDefaultWaveformH;
extern const int kCompactWaveformH;
extern const int kDefaultWaveformExpandedH;
extern const int kCompactWaveformExpandedH;
extern const int kLargeWaveformH;

extern const int kDefaultWindowW;
extern const int kDefaultWindowH;
extern const int kMinWindowW;
extern const int kMinWindowH;
extern const int kScreenMargin;
extern const int kAuthWindowW;
extern const int kAuthWindowH;
extern const int kCompactBreakpointW;
extern const int kCompactBreakpointH;
extern const int kLargeBreakpointW;
extern const int kLargeBreakpointH;

extern const juce::int64 kAudioDevicePollIntervalMs;
extern const juce::int64 kAudioDeviceMissingGraceMs;
extern const juce::int64 kAudioDeviceFallbackRetryMs;
extern const juce::int64 kMaxOfflineMs;
extern const juce::int64 kSessionReauthIntervalMs;
extern const juce::int64 kReauthGraceWindowMs;

struct StandaloneAudioContext {
  juce::AudioDeviceManager *manager = nullptr;
  int maxInputs = 0;
  int maxOutputs = 0;
};

StandaloneAudioContext getStandaloneAudioContext();
juce::String getCurrentDeviceKey(const StandaloneAudioContext &ctx);
bool enforceStandaloneOutputOnlyAudio(juce::AudioDeviceManager &manager);
bool fallbackToBuiltInOrDefaultOutput(juce::AudioDeviceManager &manager);
void hideStandaloneOptionsButton(juce::Component *fromComponent);

int categoryIndexForName(const juce::String &category);
CategoryRoutingDefaults routingForCategory(
    const std::vector<CategoryRoutingDefaults> &defaults,
    const juce::String &category);

juce::String urlEncode(const juce::String &value);
juce::StringPairArray parseCallbackParams(const juce::String &url);
juce::String readStringFromObject(const juce::var &objVar,
                                  const juce::Identifier &key);
bool parseBooleanLike(const juce::String &value, bool fallback);
juce::String userMessageForAccessDenial(const juce::String &reasonCode,
                                        const juce::String &reasonFallback);
bool hasValidAccessBySubscriptionState(const juce::String &subscriptionStatus,
                                       const juce::String &plan);
bool canUseSnapshotForLocalBootstrap(const PersistedSessionSnapshot &snap);

juce::String buildStableDeviceId();
juce::String getApiBaseUrl();

} // namespace PluginEditorSupport
