#include "PluginEditorSupport.h"
#include "PluginProcessor.h"
#include <algorithm>



namespace PluginEditorSupport {

const int kDefaultTopBarH = 60;
const int kDefaultWaveformH = 192;
const int kCompactWaveformH = 166;
const int kDefaultWaveformExpandedH = 272;
const int kCompactWaveformExpandedH = 232;
const int kLargeWaveformH = 212;

const int kDefaultWindowW = 1120;
const int kDefaultWindowH = 700;
const int kMinWindowW = 940;
const int kMinWindowH = 600;
const int kScreenMargin = 40;
const int kAuthWindowW = 680;
const int kAuthWindowH = 588;
const int kCompactBreakpointW = 1180;
const int kCompactBreakpointH = 700;
const int kLargeBreakpointW = 1680;
const int kLargeBreakpointH = 900;

const juce::int64 kAudioDevicePollIntervalMs = 1000;
const juce::int64 kAudioDeviceMissingGraceMs = 3000;
const juce::int64 kAudioDeviceFallbackRetryMs = 12000;

#ifndef MTCONTROLS_API_BASE_URL
#define MTCONTROLS_API_BASE_URL "https://api.playworship.app"
#endif
#ifndef MTCONTROLS_AUTH_BASE_URL
#define MTCONTROLS_AUTH_BASE_URL ""
#endif

#ifndef MTCONTROLS_SESSION_REAUTH_MINUTES
#define MTCONTROLS_SESSION_REAUTH_MINUTES 7200
#endif

#ifndef MTCONTROLS_REAUTH_GRACE_MINUTES
#define MTCONTROLS_REAUTH_GRACE_MINUTES 300
#endif

#ifndef MTCONTROLS_MAX_OFFLINE_HOURS
#define MTCONTROLS_MAX_OFFLINE_HOURS 168
#endif

const juce::int64 kMaxOfflineMs =
    static_cast<juce::int64>(MTCONTROLS_MAX_OFFLINE_HOURS) * 3600LL * 1000LL;

const juce::int64 kSessionReauthIntervalMs =
    static_cast<juce::int64>(MTCONTROLS_SESSION_REAUTH_MINUTES) * 60LL * 1000LL;

const juce::int64 kReauthGraceWindowMs =
    static_cast<juce::int64>(MTCONTROLS_REAUTH_GRACE_MINUTES) * 60LL * 1000LL;

StandaloneAudioContext getStandaloneAudioContext(MTControlsAudioProcessor &processor) {
  return { &processor.getAudioDeviceBridge().getAudioDeviceManager(),
           0, MTControlsAudioProcessor::maxStemCount };
}

juce::String getCurrentDeviceKey(const StandaloneAudioContext &ctx) {
  if (ctx.manager == nullptr)
    return {};
  auto *device = ctx.manager->getCurrentAudioDevice();
  if (device == nullptr)
    return {};
  const juce::String typeName = ctx.manager->getCurrentAudioDeviceType();
  const juce::String deviceName = device->getName();
  if (typeName.isEmpty() || deviceName.isEmpty())
    return {};
  return typeName + "|" + deviceName;
}

bool enforceStandaloneOutputOnlyAudio(juce::AudioDeviceManager &manager) {
  juce::AudioDeviceManager::AudioDeviceSetup setup;
  manager.getAudioDeviceSetup(setup);

  const bool alreadyOutputOnly =
      setup.inputDeviceName.isEmpty() && !setup.useDefaultInputChannels &&
      setup.inputChannels.isZero();
  if (alreadyOutputOnly)
    return true;

  setup.inputDeviceName = {};
  setup.useDefaultInputChannels = false;
  setup.inputChannels.clear();
  return manager.setAudioDeviceSetup(setup, false).isEmpty();
}

namespace {
bool deviceNameLooksBuiltIn(const juce::String &name) {
  const auto lowered = name.trim().toLowerCase();
  return lowered.contains("built-in") || lowered.contains("built in") ||
         lowered.contains("internal") || lowered.contains("default");
}

bool applyOutputDeviceSelection(juce::AudioDeviceManager &manager,
                                const juce::String &typeName,
                                const juce::String &outputName) {
  if (typeName.isEmpty() || outputName.isEmpty())
    return false;

  manager.setCurrentAudioDeviceType(typeName, false);
  if (!manager.getCurrentAudioDeviceType().equalsIgnoreCase(typeName))
    return false;

  juce::AudioDeviceManager::AudioDeviceSetup setup;
  manager.getAudioDeviceSetup(setup);
  setup.outputDeviceName = outputName;
  setup.inputDeviceName = {};
  setup.useDefaultInputChannels = false;
  setup.inputChannels.clear();
  setup.useDefaultOutputChannels = true;
  return manager.setAudioDeviceSetup(setup, false).isEmpty();
}
} // namespace

bool fallbackToBuiltInOrDefaultOutput(juce::AudioDeviceManager &manager) {
  struct Candidate {
    juce::String typeName;
    juce::String deviceName;
    int score = 0;
  };

  std::vector<Candidate> candidates;
  const auto currentType = manager.getCurrentAudioDeviceType();
  const auto &deviceTypes = manager.getAvailableDeviceTypes();
  for (auto *ioType : deviceTypes) {
    if (ioType == nullptr)
      continue;

    const auto typeName = ioType->getTypeName();
    const auto outputs = ioType->getDeviceNames(false);
    const int defaultIdx = ioType->getDefaultDeviceIndex(false);

    for (int i = 0; i < outputs.size(); ++i) {
      const auto outputName = outputs[i].trim();
      if (outputName.isEmpty())
        continue;

      int score = 0;
      if (typeName.equalsIgnoreCase(currentType))
        score += 2;
      if (i == defaultIdx)
        score += 3;
      if (deviceNameLooksBuiltIn(outputName))
        score += 4;

      candidates.push_back({typeName, outputName, score});
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) {
              if (a.score != b.score)
                return a.score > b.score;
              const int typeCmp = a.typeName.compareIgnoreCase(b.typeName);
              if (typeCmp != 0)
                return typeCmp < 0;
              return a.deviceName.compareIgnoreCase(b.deviceName) < 0;
            });

  for (const auto &candidate : candidates) {
    if (applyOutputDeviceSelection(manager, candidate.typeName,
                                   candidate.deviceName))
      return true;
  }
  return false;
}


int categoryIndexForName(const juce::String &category) {
  const auto needle = category.trim();
  if (needle.isEmpty())
    return -1;

  int otherIndex = -1;
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    const juce::String name = kTrackCategories[i];
    if (name.equalsIgnoreCase("Other"))
      otherIndex = static_cast<int>(i);
    if (name.equalsIgnoreCase(needle))
      return static_cast<int>(i);
  }
  return otherIndex;
}

CategoryRoutingDefaults routingForCategory(
    const std::vector<CategoryRoutingDefaults> &defaults,
    const juce::String &category) {
  const int idx = categoryIndexForName(category);
  if (idx < 0 || idx >= static_cast<int>(defaults.size()))
    return CategoryRoutingDefaults{};
  return defaults[static_cast<size_t>(idx)];
}

juce::String urlEncode(const juce::String &value) {
  return juce::URL::addEscapeChars(value, true);
}

namespace {
void addParamsFromChunk(const juce::String &chunk,
                        juce::StringPairArray &params) {
  auto cleaned = chunk.trim();
  if (cleaned.startsWithChar('?') || cleaned.startsWithChar('#'))
    cleaned = cleaned.substring(1);
  if (cleaned.isEmpty())
    return;

  const auto pairs = juce::StringArray::fromTokens(cleaned, "&", "");
  for (const auto &pair : pairs) {
    if (pair.isEmpty())
      continue;
    const auto sep = pair.indexOfChar('=');
    const auto key = juce::URL::removeEscapeChars(
        (sep >= 0 ? pair.substring(0, sep) : pair).replace("+", " "));
    const auto value = juce::URL::removeEscapeChars(
        (sep >= 0 ? pair.substring(sep + 1) : juce::String{}).replace("+", " "));
    if (key.isNotEmpty())
      params.set(key, value);
  }
}
} // namespace

juce::StringPairArray parseCallbackParams(const juce::String &url) {
  juce::StringPairArray params;
  const auto hashPos = url.indexOfChar('#');
  const auto queryPos = url.indexOfChar('?');
  if (queryPos >= 0) {
    const int queryEnd =
        (hashPos >= 0 && hashPos > queryPos) ? hashPos : url.length();
    addParamsFromChunk(url.substring(queryPos + 1, queryEnd), params);
  }
  if (hashPos >= 0)
    addParamsFromChunk(url.substring(hashPos + 1), params);
  return params;
}

juce::String readStringFromObject(const juce::var &objVar,
                                  const juce::Identifier &key) {
  if (const auto *obj = objVar.getDynamicObject()) {
    if (obj->hasProperty(key))
      return obj->getProperty(key).toString().trim();
  }
  return {};
}

bool parseBooleanLike(const juce::String &value, bool fallback) {
  const auto v = value.trim().toLowerCase();
  if (v.isEmpty())
    return fallback;
  if (v == "1" || v == "true" || v == "yes")
    return true;
  if (v == "0" || v == "false" || v == "no")
    return false;
  return fallback;
}

juce::String userMessageForAccessDenial(const juce::String &reasonCode,
                                        const juce::String &reasonFallback) {
  const auto code = reasonCode.trim().toUpperCase();
  if (code == "LICENSE_INACTIVE")
    return "Tu suscripcion no esta activa. Activa tu plan para continuar.";
  if (code == "NO_SUBSCRIPTION")
    return "No tienes acceso habilitado para esta cuenta. Inicia sesion nuevamente.";
  if (code == "CANCELLED_SUBSCRIPTION")
    return "Tu suscripcion fue cancelada. Reactiva tu plan para continuar.";
  if (code == "DEVICE_LIMIT")
    return "Alcanzaste el limite de dispositivos para esta licencia.";
  if (code == "LICENSE_NOT_FOUND")
    return "No encontramos una licencia activa para tu cuenta.";
  if (code == "MISSING_DEVICE_ID")
    return "No fue posible validar este dispositivo. Intenta de nuevo.";
  if (code == "UNAUTHORIZED")
    return "Tu sesion no es valida. Inicia sesion nuevamente.";
  if (code == "INTERNAL_ERROR")
    return "No se pudo validar tu acceso en este momento. Intenta de nuevo.";
  if (code == "INVALID_SUBSCRIPTION_STATE")
    return "Tu suscripcion no es valida actualmente. Inicia sesion nuevamente.";
  if (reasonFallback.isNotEmpty())
    return reasonFallback;
  return "Acceso denegado.";
}

bool hasValidAccessBySubscriptionState(const juce::String &subscriptionStatus,
                                       const juce::String &plan) {
  const auto status = subscriptionStatus.trim().toLowerCase();
  const auto normalizedPlan = plan.trim().toUpperCase();

  if (status == "free" || status == "trial")
    return true;

  if (status == "active")
    return normalizedPlan.isNotEmpty() && normalizedPlan != "NONE";

  return false;
}

bool canUseSnapshotForLocalBootstrap(const PersistedSessionSnapshot &snap) {
  if (!snap.allowed ||
      !hasValidAccessBySubscriptionState(snap.subscriptionStatus, snap.plan))
    return false;

  const auto nowMs = juce::Time::currentTimeMillis();
  const auto ageMs = nowMs - snap.lastOnlineValidationMs;
  return snap.lastOnlineValidationMs > 0 && ageMs >= 0 && ageMs <= kMaxOfflineMs;
}

juce::String buildStableDeviceId() {
  juce::String raw = juce::SystemStats::getUniqueDeviceID().trim();
  if (raw.isEmpty()) {
    raw = juce::SystemStats::getComputerName().trim() + "-" +
          juce::SystemStats::getOperatingSystemName().trim();
  }
  if (raw.isEmpty())
    raw = "unknown-device";

  juce::String normalized;
  normalized.preallocateBytes(raw.getNumBytesAsUTF8());
  bool lastWasDash = false;
  for (auto c : raw.toLowerCase()) {
    const bool alphaNum = juce::CharacterFunctions::isLetterOrDigit(c);
    if (alphaNum) {
      normalized << juce::String::charToString(c);
      lastWasDash = false;
    } else if (!lastWasDash) {
      normalized << "-";
      lastWasDash = true;
    }
  }
  normalized = normalized.trimCharactersAtStart("-").trimCharactersAtEnd("-");
  return normalized.isNotEmpty() ? normalized : juce::String("unknown-device");
}

juce::String sanitizeBackendBaseUrl(const juce::String &raw, bool allowLocalHttp) {
  const juce::String configured = raw.trim();
  const juce::String fallback = "https://api.playworship.app";
  if (configured.isEmpty())
    return fallback;

  const bool isHttps = configured.startsWithIgnoreCase("https://");
  const bool isHttp = configured.startsWithIgnoreCase("http://");
  if (!isHttps && !isHttp)
    return fallback;

  const juce::URL parsed(configured);
  const auto host = parsed.getDomain().trim().toLowerCase();
  if (host.isEmpty())
    return fallback;

  auto hostParts = juce::StringArray::fromTokens(host, ".", "");
  const auto secondOctet = hostParts.size() == 4 ? hostParts[1].getIntValue() : -1;
  const bool isPrivateNetworkHost =
      host.startsWith("10.") || host.startsWith("192.168.") ||
      (host.startsWith("172.") && secondOctet >= 16 && secondOctet <= 31);
  const bool isLocalHost = host == "localhost" || host == "127.0.0.1" ||
                           host.endsWith(".local") || isPrivateNetworkHost;
  if (isHttp && (!allowLocalHttp || !isLocalHost))
    return fallback;

  return configured.trimCharactersAtEnd("/");
}

juce::String getApiBaseUrl() {
  return sanitizeBackendBaseUrl(juce::String(MTCONTROLS_API_BASE_URL), true);
}

juce::String getAuthBaseUrl() {
  const juce::String configuredAuth = juce::String(MTCONTROLS_AUTH_BASE_URL).trim();
  if (configuredAuth.isNotEmpty())
    return sanitizeBackendBaseUrl(configuredAuth, false);

  return getApiBaseUrl();
}

} // namespace PluginEditorSupport
