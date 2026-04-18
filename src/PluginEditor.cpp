#include "PluginEditor.h"
#include "BinaryData.h"
#include "data/AppSettings.h"
#include "data/AuthSessionApi.h"
#include "data/Localization.h"
#include "data/SessionAuthLogger.h"
#include "data/SessionPersistence.h"
#include "data/SongData.h"
#include "data/SongLibraryStore.h"
#include "ui/UiCommon.h"
#include "ui/components/SetlistHandlePanel.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/SettingsPanel.h"
#include "ui/panels/SongListPanel.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/WaveformBar.h"
#include "ui/panels/PadsComponent.h"
#include "ui/components/PadsHandlePanel.h"
#include "ui/components/SongLoadProgressOverlay.h"
#include "audio/AudioDeviceMonitor.h"
#include <algorithm>
#include <cctype>
#include <thread>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace {
constexpr int kDefaultTopBarH = 60;
constexpr int kDefaultWaveformH = 180;
constexpr int kCompactWaveformH = 156;
constexpr int kDefaultWaveformExpandedH = 220;
constexpr int kCompactWaveformExpandedH = 200;
constexpr int kLargeWaveformH = 200;

constexpr int kDefaultWindowW = 1120;
constexpr int kDefaultWindowH = 700;
/** Minimo para que la UI siga siendo usable (setlist + canales). */
constexpr int kMinWindowW = 940;
constexpr int kMinWindowH = 600;
constexpr int kScreenMargin = 40;
constexpr int kAuthWindowW = 680;
constexpr int kAuthWindowH = 588;
constexpr int kCompactBreakpointW = 1180;
constexpr int kCompactBreakpointH = 700;
constexpr int kLargeBreakpointW = 1680;
constexpr int kLargeBreakpointH = 900;
constexpr const char *kSubscriptionInfoUrl = "https://playwordship.gumroad.com/";

#ifndef MTCONTROLS_API_BASE_URL
#define MTCONTROLS_API_BASE_URL "https://playworship-345830648064.us-east1.run.app"
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

constexpr juce::int64 kMaxOfflineMs =
    static_cast<juce::int64>(MTCONTROLS_MAX_OFFLINE_HOURS) * 3600LL * 1000LL;

constexpr juce::int64 kSessionReauthIntervalMs =
    static_cast<juce::int64>(MTCONTROLS_SESSION_REAUTH_MINUTES) * 60LL * 1000LL;
constexpr juce::int64 kReauthGraceWindowMs =
    static_cast<juce::int64>(MTCONTROLS_REAUTH_GRACE_MINUTES) * 60LL * 1000LL;

struct StandaloneAudioContext {
  juce::AudioDeviceManager *manager = nullptr;
  int maxInputs = 0;
  int maxOutputs = 0;
};

StandaloneAudioContext getStandaloneAudioContext() {
#if JucePlugin_Build_Standalone
  if (auto *holder = juce::StandalonePluginHolder::getInstance()) {
    StandaloneAudioContext ctx;
    ctx.manager = &holder->deviceManager;
    ctx.maxInputs = holder->getNumInputChannels();
    ctx.maxOutputs = holder->getNumOutputChannels();
    return ctx;
  }
#endif
  return {};
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

#if JucePlugin_Build_Standalone
void hideStandaloneOptionsButton(juce::Component *fromComponent) {
  if (fromComponent == nullptr)
    return;

  auto *topLevel = fromComponent->getTopLevelComponent();
  if (topLevel == nullptr)
    return;

  auto hideRecursively = [](auto &&self, juce::Component *component) -> void {
    if (component == nullptr)
      return;

    if (auto *button = dynamic_cast<juce::Button *>(component)) {
      const auto text = button->getButtonText().trim();
      if (text.equalsIgnoreCase("Options") ||
          text.equalsIgnoreCase("Opciones")) {
        button->setVisible(false);
        button->setEnabled(false);
      }
    }

    for (int i = 0; i < component->getNumChildComponents(); ++i)
      self(self, component->getChildComponent(i));
  };

  hideRecursively(hideRecursively, topLevel);
}
#endif

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
  if (code == "TRIAL_EXPIRED")
    return "Tu periodo de prueba ha finalizado. Debes comprar una suscripcion para continuar.";
  if (code == "NO_SUBSCRIPTION")
    return "No tienes una suscripcion activa. Inicia un plan para continuar.";
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

  if (status == "trial")
    return true;

  if (status == "active")
    return normalizedPlan.isNotEmpty() && normalizedPlan != "NONE";

  return false;
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

juce::String getApiBaseUrl() {
  const juce::String configured = juce::String(MTCONTROLS_API_BASE_URL).trim();
  const juce::String fallback = "https://playworship-345830648064.us-east1.run.app";
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

  const bool isLocalHost = host == "localhost" || host == "127.0.0.1";
  if (isHttp && !isLocalHost)
    return fallback;

  return configured.trimCharactersAtEnd("/");
}

} // namespace

class LocalAuthCallbackServer final : private juce::Thread {
public:
  using Callback = std::function<void(const juce::String &)>;

  explicit LocalAuthCallbackServer(Callback cb)
      : juce::Thread("AuthCallbackServer"), onCallback(std::move(cb)) {}

  ~LocalAuthCallbackServer() override { stop(); }

  bool start(int &outPort) {
    stop();
    outPort = 0;

    if (listenerSocket.createListener(0, "127.0.0.1")) {
      outPort = listenerSocket.getBoundPort();
      if (outPort > 0) {
        startThread();
        return true;
      }
      listenerSocket.close();
    }

    for (int i = 0; i < 32; ++i) {
      const int candidate = 43123 + i;
      if (!listenerSocket.createListener(candidate, "127.0.0.1"))
        continue;
      outPort = listenerSocket.getBoundPort();
      if (outPort <= 0)
        outPort = candidate;
      startThread();
      return true;
    }
    return false;
  }

  void stop() {
    signalThreadShouldExit();
    listenerSocket.close();
    stopThread(1500);
  }

private:
  void run() override {
    while (!threadShouldExit()) {
      auto *client = listenerSocket.waitForNextConnection();
      if (client == nullptr)
        continue;

      juce::String rawRequest;
      char buffer[4096] = {};
      if (client->waitUntilReady(true, 1200) == 1) {
        for (int attempt = 0; attempt < 32; ++attempt) {
          const int bytesRead =
              client->read(buffer, static_cast<int>(sizeof(buffer) - 1), false);
          if (bytesRead <= 0)
            break;
          buffer[bytesRead] = '\0';
          rawRequest += juce::String::fromUTF8(buffer);
          // Stop once request line + headers are complete. Callback URLs can
          // be long due to encoded session payload, so we allow larger reads.
          if (rawRequest.contains("\r\n\r\n"))
            break;
          if (client->waitUntilReady(true, 40) != 1)
            break;
        }
      }

      const auto firstLine =
          rawRequest.upToFirstOccurrenceOf("\r\n", false, false);
      const auto parts = juce::StringArray::fromTokens(firstLine, " ", "");
      const auto requestPath = parts.size() >= 2 ? parts[1].trim() : juce::String{};
      if (requestPath.startsWith("/auth/callback")) {
        if (onCallback)
          onCallback("http://127.0.0.1" + requestPath);
      }

      const juce::String response =
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/html; charset=utf-8\r\n"
          "Connection: close\r\n\r\n"
          "<html><body><h3>Procesando autenticacion...</h3>"
          "<p>Vuelve a la aplicacion para confirmar el resultado.</p>"
          "<script>setTimeout(function(){ window.close(); }, 900);</script>"
          "</body></html>";
      client->write(response.toRawUTF8(), response.getNumBytesAsUTF8() - 1);
      client->close();
      delete client;
    }
  }

  juce::StreamingSocket listenerSocket;
  Callback onCallback;
};

namespace {
/** Botones tipo pildora (referencia: pantallas de bienvenida centradas). */
class WelcomePillLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  enum class Style { Primary, Secondary };
  explicit WelcomePillLookAndFeel(Style s) : style(s) {}

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool isMouseOverButton,
                            bool isButtonDown) override {
    juce::ignoreUnused(backgroundColour);
    auto r = button.getLocalBounds().toFloat().reduced(0.5f, 1.0f);
    const float rad = r.getHeight() * 0.5f;
    if (style == Style::Primary) {
      auto fill = juce::Colour(0xfff5f5f7);
      if (isButtonDown)
        fill = fill.darker(0.14f);
      else if (isMouseOverButton)
        fill = juce::Colour(0xffffffff);
      g.setColour(fill);
      g.fillRoundedRectangle(r, rad);
      g.setColour(juce::Colour(0xff000000).withAlpha(0.06f));
      g.drawRoundedRectangle(r.reduced(0.5f), rad - 0.25f, 1.0f);
    } else {
      auto fill = juce::Colour(0x00000000);
      if (isButtonDown)
        fill = juce::Colour(0xff2a2a30).withAlpha(0.9f);
      else if (isMouseOverButton)
        fill = juce::Colour(0xff34343c).withAlpha(0.55f);
      g.setColour(fill);
      g.fillRoundedRectangle(r, rad);
      const auto border = juce::Colour(0xff5c5c66);
      g.setColour(border.withMultipliedAlpha(isMouseOverButton ? 1.1f : 0.85f));
      g.drawRoundedRectangle(r.reduced(0.5f), rad - 0.25f, 1.0f);
    }
  }

  juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override {
    const float px =
        juce::jlimit(15.0f, 17.0f, static_cast<float>(buttonHeight) * 0.34f);
    return juce::Font(
        juce::FontOptions(px).withStyle(style == Style::Primary ? "Bold"
                                                                 : "Regular"));
  }

private:
  Style style;
};

class WelcomeModal final : public juce::Component {
public:
  std::function<void()> onLoginRequested;

  WelcomeModal() {
    setOpaque(false);

    logoImage = juce::ImageFileFormat::loadFrom(
        BinaryData::pwLogoSinFondo_png, BinaryData::pwLogoSinFondo_pngSize);
    if (logoImage.isValid()) {
      logo.setOpaque(false);
      addAndMakeVisible(logo);
    }

    auto clearLabelBg = [](juce::Label &l) {
      l.setOpaque(false);
      l.setColour(juce::Label::backgroundColourId,
                  juce::Colours::transparentBlack);
      l.setColour(juce::Label::outlineColourId,
                  juce::Colours::transparentBlack);
    };

    title.setText(
        L10n::tr("Te damos la bienvenida\na PlayWorship",
                 "Welcome to\nPlayWorship"),
        juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setFont(juce::FontOptions(30.0f).withStyle("Bold"));
    title.setMinimumHorizontalScale(0.82f);
    title.setColour(juce::Label::textColourId,
                    juce::Colour::fromRGB(252, 252, 255));
    addAndMakeVisible(title);
    clearLabelBg(title);

    subtitle.setText(
        L10n::tr("Inicia sesion para continuar con tu biblioteca y setlists.",
                 "Sign in to continue with your library and setlists."),
        juce::dontSendNotification);
    subtitle.setJustificationType(juce::Justification::centred);
    subtitle.setFont(juce::FontOptions(15.0f));
    subtitle.setColour(juce::Label::textColourId,
                       juce::Colour::fromRGB(168, 170, 184));
    addAndMakeVisible(subtitle);
    clearLabelBg(subtitle);

    status.setText(
        L10n::tr("Pulsa Ingresar para abrir el navegador.",
                 "Press Sign in to open the browser."),
        juce::dontSendNotification);
    status.setJustificationType(juce::Justification::centred);
    status.setFont(juce::FontOptions(13.0f));
    status.setColour(juce::Label::textColourId,
                     juce::Colour::fromRGB(146, 148, 160));
    addAndMakeVisible(status);
    clearLabelBg(status);

    loginButton.setButtonText(L10n::tr("Ingresar", "Sign in"));
    loginButton.setLookAndFeel(&primaryLaf);
    loginButton.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::transparentWhite);
    loginButton.setColour(juce::TextButton::buttonOnColourId,
                          juce::Colours::transparentWhite);
    loginButton.setColour(juce::TextButton::textColourOffId,
                          juce::Colour::fromRGB(18, 18, 22));
    loginButton.setColour(juce::TextButton::textColourOnId,
                          juce::Colour::fromRGB(18, 18, 22));
    loginButton.onClick = [this] {
      if (onLoginRequested)
        onLoginRequested();
    };
    addAndMakeVisible(loginButton);

    moreInfoButton.setButtonText(
        L10n::tr("Mas informacion", "More information"));
    moreInfoButton.setLookAndFeel(&secondaryLaf);
    moreInfoButton.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::transparentWhite);
    moreInfoButton.setColour(juce::TextButton::buttonOnColourId,
                             juce::Colours::transparentWhite);
    moreInfoButton.setColour(juce::TextButton::textColourOffId,
                             juce::Colour::fromRGB(236, 236, 242));
    moreInfoButton.setColour(juce::TextButton::textColourOnId,
                             juce::Colour::fromRGB(252, 252, 255));
    moreInfoButton.onClick = [] {
      juce::URL(kSubscriptionInfoUrl).launchInDefaultBrowser();
    };
    addAndMakeVisible(moreInfoButton);
  }

  ~WelcomeModal() override {
    loginButton.setLookAndFeel(nullptr);
    moreInfoButton.setLookAndFeel(nullptr);
  }

  void setStatusText(const juce::String &text, bool isError) {
    status.setText(text, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId,
                     isError ? juce::Colour::fromRGB(255, 164, 164)
                             : juce::Colour::fromRGB(146, 148, 160));
  }

  void resetStatusToDefault() {
    setStatusText(L10n::tr("Pulsa Ingresar para abrir el navegador.",
                           "Press Sign in to open the browser."),
                  false);
    setChecking(false);
  }

  void setChecking(bool isChecking) {
    loginButton.setVisible(!isChecking);
    moreInfoButton.setVisible(!isChecking);
    subtitle.setVisible(!isChecking);
  }

  void paint(juce::Graphics &) override {}

  void resized() override {
    const int sidePad = juce::jlimit(36, 56, getWidth() / 9);
    const int topPad = juce::jlimit(32, 48, getHeight() / 11);
    auto area = getLocalBounds().reduced(sidePad, topPad);
    const int maxCol = 420;
    const int colW = juce::jmin(maxCol, area.getWidth());
    auto column = area.withSizeKeepingCentre(colW, area.getHeight());

    if (logoImage.isValid()) {
      auto logoArea = column.removeFromTop(160);
      const int logoW = juce::jmin(400, logoArea.getWidth());
      const int logoH = logoArea.getHeight();
      logo.setImage(logoImage, juce::RectanglePlacement::centred);
      logo.setBounds(logoArea.withSizeKeepingCentre(logoW, logoH));
      column.removeFromTop(24);
    } else {
      column.removeFromTop(8);
    }

    title.setBounds(column.removeFromTop(72));
    column.removeFromTop(12);
    subtitle.setBounds(column.removeFromTop(52));
    column.removeFromTop(20);
    status.setBounds(column.removeFromTop(38));
    column.removeFromTop(26);

    const int btnH = 48;
    loginButton.setBounds(column.removeFromTop(btnH));
    column.removeFromTop(12);
    moreInfoButton.setBounds(column.removeFromTop(btnH));
  }

private:
  WelcomePillLookAndFeel primaryLaf{WelcomePillLookAndFeel::Style::Primary};
  WelcomePillLookAndFeel secondaryLaf{WelcomePillLookAndFeel::Style::Secondary};
  juce::ImageComponent logo;
  juce::Image logoImage;
  juce::Label title;
  juce::Label subtitle;
  juce::Label status;
  juce::TextButton loginButton;
  juce::TextButton moreInfoButton;
};

class ReauthPromptModal final : public juce::Component {
public:
  std::function<void()> onLoginNow;
  std::function<void()> onLater;

  ReauthPromptModal() {
    title.setText(L10n::tr("Renovar sesion", "Renew session"),
                  juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setFont(juce::FontOptions(20.0f).withStyle("Bold"));
    addAndMakeVisible(title);

    message.setText(
        L10n::tr("Para continuar con seguridad, necesitamos que vuelvas a iniciar sesion.",
                 "To continue securely, please sign in again."),
        juce::dontSendNotification);
    message.setJustificationType(juce::Justification::centred);
    message.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(message);

    const int graceMinutes =
        juce::jmax(1, static_cast<int>(MTCONTROLS_REAUTH_GRACE_MINUTES));
    juce::String graceText;
    if (graceMinutes % 60 == 0) {
      const int hours = graceMinutes / 60;
      graceText = L10n::tr("Tienes hasta ", "You have up to ") +
                  juce::String(hours) +
                  (hours == 1 ? L10n::tr(" hora", " hour")
                              : L10n::tr(" horas", " hours")) +
                  L10n::tr(" de gracia para completar el login.",
                           " grace period to complete sign in.");
    } else {
      graceText = L10n::tr("Tienes ", "You have ") + juce::String(graceMinutes) +
                  L10n::tr(" minutos de gracia para completar el login.",
                           " minutes of grace period to complete sign in.");
    }
    graceInfo.setText(graceText, juce::dontSendNotification);
    graceInfo.setJustificationType(juce::Justification::centred);
    graceInfo.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(graceInfo);

    status.setText(L10n::tr("Pulsa Ingresar ahora para abrir el navegador.",
                            "Press Sign in now to open the browser."),
                   juce::dontSendNotification);
    status.setJustificationType(juce::Justification::centred);
    status.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(status);

    loginNowButton.setButtonText(L10n::tr("Ingresar ahora", "Sign in now"));
    loginNowButton.onClick = [this] {
      if (onLoginNow)
        onLoginNow();
    };
    addAndMakeVisible(loginNowButton);

    laterButton.setButtonText(L10n::tr("Mas tarde", "Later"));
    laterButton.onClick = [this] {
      if (onLater)
        onLater();
    };
    addAndMakeVisible(laterButton);

    infoLink.setURL(juce::URL(kSubscriptionInfoUrl));
    infoLink.setButtonText(L10n::tr("Mas informacion", "More information"));
    infoLink.setFont(juce::FontOptions(13.0f).withStyle("Underlined"), false,
                     juce::Justification::centred);
    addAndMakeVisible(infoLink);
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 70));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(juce::Colour::fromRGB(65, 76, 98));
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(14);
    title.setBounds(area.removeFromTop(34));
    area.removeFromTop(4);
    message.setBounds(area.removeFromTop(52));
    graceInfo.setBounds(area.removeFromTop(28));
    status.setBounds(area.removeFromTop(26));
    area.removeFromTop(12);

    auto controlsRow = area.removeFromTop(42).withSizeKeepingCentre(520, 36);
    auto loginArea = controlsRow.removeFromLeft(185);
    controlsRow.removeFromLeft(10);
    auto laterArea = controlsRow.removeFromLeft(145);
    controlsRow.removeFromLeft(10);
    auto linkArea = controlsRow;
    loginNowButton.setBounds(loginArea);
    laterButton.setBounds(laterArea);
    infoLink.setBounds(linkArea);
  }

private:
  void setStatusText(const juce::String &text, bool isError) {
    status.setText(text, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId,
                     isError ? juce::Colour::fromRGB(255, 164, 164)
                             : juce::Colour::fromRGB(205, 214, 227));
  }

public:
  void showWaitingForCallback() {
    setStatusText(L10n::tr("Esperando autenticacion en el navegador...",
                           "Waiting for browser authentication..."),
                  false);
    loginNowButton.setEnabled(false);
    laterButton.setEnabled(false);
  }

  void showReadyState() {
    setStatusText(L10n::tr("Pulsa Ingresar ahora para abrir el navegador.",
                           "Press Sign in now to open the browser."),
                  false);
    loginNowButton.setEnabled(true);
    laterButton.setEnabled(true);
  }

  void showError(const juce::String &text) {
    setStatusText(text, true);
    loginNowButton.setEnabled(true);
    laterButton.setEnabled(true);
  }

private:
  juce::Label title;
  juce::Label message;
  juce::Label graceInfo;
  juce::Label status;
  juce::TextButton loginNowButton;
  juce::TextButton laterButton;
  juce::HyperlinkButton infoLink{"Mas informacion",
                                 juce::URL(kSubscriptionInfoUrl)};
};

} // namespace

MTControlsAudioProcessorEditor::MTControlsAudioProcessorEditor(
    MTControlsAudioProcessor &p)
    : AudioProcessorEditor(p) {
  SessionAuthLogger::write("PluginEditor ctor");
  SessionAuthLogger::write("PluginEditor ctor: log file " +
                           SessionAuthLogger::getLogFile().getFullPathName());
  currentThemeId = AppSettings::getThemePresetId();
  applyTheme(static_cast<ThemePreset>(currentThemeId));

  const auto userArea =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
  const int authW = juce::jmin(kAuthWindowW, userArea.getWidth());
  const int authH = juce::jmin(kAuthWindowH, userArea.getHeight());
  setSize(authW, authH);
  setResizable(false, false);
  setResizeLimits(authW, authH, authW, authH);
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this),
       authW, authH] {
        if (safe == nullptr)
          return;
        if (auto *top = safe->getTopLevelComponent()) {
          auto bounds = top->getBounds();
          bounds.setSize(authW, authH);
          if (auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds))
            bounds.setCentre(display->userArea.getCentre());
          top->setBounds(bounds);
        }
      });

  showWelcomeOnlyMode();
  initializeWelcomeModal();
  lastSuccessfulLoginMs = AppSettings::getLastSuccessfulLoginMs();
  reauthGraceDeadlineMs = AppSettings::getReauthGraceDeadlineMs();
  {
    PersistedSessionSnapshot bootSnap;
    if (SessionPersistence::load(bootSnap))
      lastOnlineValidationMs = bootSnap.lastOnlineValidationMs;
  }
  tryResumeSessionAsync();
  startTimerHz(12);

#if JucePlugin_Build_Standalone
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
           this)] {
        if (safe != nullptr)
          hideStandaloneOptionsButton(safe.getComponent());
      });
#endif
}

ResponsiveLayoutProfile MTControlsAudioProcessorEditor::resolveResponsiveLayoutProfile()
    const {
  switch (AppSettings::getLayoutMode()) {
  case AppLayoutMode::Compact:
    return ResponsiveLayoutProfile::Compact;
  case AppLayoutMode::Standard:
    return ResponsiveLayoutProfile::Standard;
  case AppLayoutMode::Large:
    return ResponsiveLayoutProfile::Large;
  case AppLayoutMode::Auto:
  default:
    break;
  }

  const int w = getWidth() > 0 ? getWidth() : kDefaultWindowW;
  const int h = getHeight() > 0 ? getHeight() : kDefaultWindowH;
  if (w < kCompactBreakpointW || h < kCompactBreakpointH)
    return ResponsiveLayoutProfile::Compact;
  if (w >= kLargeBreakpointW && h >= kLargeBreakpointH)
    return ResponsiveLayoutProfile::Large;
  return ResponsiveLayoutProfile::Standard;
}

void MTControlsAudioProcessorEditor::applyResponsiveLayoutProfile(
    const ResponsiveLayoutProfile profile) {
  responsiveLayoutProfile = profile;

  if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
    bar->setResponsiveProfile(profile);
  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
    grid->setResponsiveProfile(profile);
}

void MTControlsAudioProcessorEditor::updateResponsiveLayoutProfile() {
  const auto nextProfile = resolveResponsiveLayoutProfile();
  if (nextProfile != responsiveLayoutProfile)
    applyResponsiveLayoutProfile(nextProfile);
}

void MTControlsAudioProcessorEditor::initializeMainUi() {
  if (lastSuccessfulLoginMs > 0) {
    const auto nowMs = juce::Time::currentTimeMillis();
    const auto sessionDueAtMs = lastSuccessfulLoginMs + kSessionReauthIntervalMs;
    if (nowMs >= sessionDueAtMs) {
      // El bloqueo en arranque solo aplica si el usuario ya eligio "Mas tarde"
      // y su periodo de gracia ya vencio.
      if (reauthGraceDeadlineMs > 0 && nowMs >= reauthGraceDeadlineMs) {
        showWelcomeOnlyMode();
        if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
          modal->setStatusText(
              "Tu periodo de gracia termino. Debes iniciar sesion para continuar.",
              true);
        }
        return;
      }
    }
  }

  if (mainUiInitialized)
    return;

  if (topBar != nullptr && waveformBar != nullptr && channelGridPanel != nullptr &&
      songListPanel != nullptr && setlistHandlePanel != nullptr) {
    topBar->setVisible(true);
    waveformBar->setVisible(true);
    channelGridPanel->setVisible(true);
    songListPanel->setVisible(true);
    setlistHandlePanel->setVisible(true);
    if (welcomeModal)
      welcomeModal->setVisible(false);
    if (reauthPromptModal)
      reauthPromptModal->setVisible(false);
    setResizable(true, true);
    setResizeLimits(kMinWindowW, kMinWindowH, 32000, 32000);

    const auto userArea =
        juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    const int maxW = juce::jmax(kMinWindowW, userArea.getWidth() - kScreenMargin);
    const int maxH = juce::jmax(kMinWindowH, userArea.getHeight() - kScreenMargin);
    const int w = juce::jlimit(kMinWindowW, maxW, kDefaultWindowW);
    const int h = juce::jlimit(kMinWindowH, maxH, kDefaultWindowH);

    if (AppSettings::getExpandWindowOnLaunch()) {
      juce::MessageManager::callAsync(
          [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
               this)] {
            if (safe != nullptr)
              expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
          });
    } else {
      setSize(w, h);
      juce::MessageManager::callAsync(
          [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
               this),
           w, h] {
            if (safe == nullptr)
              return;
            if (auto *top = safe->getTopLevelComponent()) {
              auto bounds = top->getBounds();
              bounds.setSize(w, h);
              if (auto *display =
                      juce::Desktop::getInstance().getDisplays().getDisplayForRect(
                          bounds))
                bounds.setCentre(display->userArea.getCentre());
              top->setBounds(bounds);
            }
          });
    }

    applyResponsiveLayoutProfile(resolveResponsiveLayoutProfile());
    if (!autoSetlistCollapseInitialized) {
      isSetlistCollapsed =
          (responsiveLayoutProfile == ResponsiveLayoutProfile::Compact);
      autoSetlistCollapseInitialized = true;
    }
    mainUiInitialized = true;
    resized();
    return;
  }

  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  auto topBarImpl = std::make_unique<TopBar>(p);
  auto *topBarRaw = topBarImpl.get();
  topBar = std::move(topBarImpl);
  topBarRaw->setResponsiveProfile(resolveResponsiveLayoutProfile());
  topBarRaw->setTransportProviders(
      [&p] { return p.isSongPlaying(); },
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongEndSeconds(); },
      [&p] { return p.getLoadedSongKey(); });

  auto waveformBarImpl = std::make_unique<WaveformBar>();
  auto *waveformBarRaw = waveformBarImpl.get();
  waveformBar = std::move(waveformBarImpl);
  waveformBarRaw->setTransportProviders(
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongLengthSeconds(); });
  waveformBarRaw->setSeekCallback([&p](double targetSeconds) {
    p.seekSongTo(targetSeconds);
  });
  waveformBarRaw->setOnArrangementPartJumpRequest(
      [&p](int partIndex) { p.requestArrangementPartJump(partIndex); });
  waveformBarRaw->setPendingArrangementPartProvider(
      [&p] { return p.getPendingArrangementPartJumpIndex(); });
  waveformBarRaw->setOnEditToggle([this](bool expanded) {
    isWaveformExpanded = expanded;
    resized();
    repaint();
  });
  waveformBarRaw->setOnArrangementChanged(
      [this, &p](const SongArrangementSnapshot &arrangement,
                 bool isCustomArrangement) {
        if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
          return;
        SongLibraryStore::updateArrangementForActiveSetlist(
            currentLoadedSong.id, arrangement, isCustomArrangement);
        // Las partes son marcadores visuales/editables y no deben alterar
        // el flujo de reproduccion normal de la cancion.
        p.setSongArrangement(arrangement, false);
      });
  waveformBarRaw->setOnArrangementReset([this, &p] {
    if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
      return;
    SongLibraryStore::resetArrangementForActiveSetlist(currentLoadedSong.id);
    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (SongLibraryStore::getActiveSetlistArrangement(currentLoadedSong.id,
                                                      arrangement,
                                                      isCustomArrangement)) {
      p.setSongArrangement(arrangement, false);
    }
  });

  auto channelGridImpl =
      std::make_unique<ChannelGridPanel>(stripLookAndFeel, p);
  auto *channelGridRaw = channelGridImpl.get();
  channelGridPanel = std::move(channelGridImpl);
  channelGridRaw->setResponsiveProfile(resolveResponsiveLayoutProfile());

  p.getAudioDeviceBridge().setOnDeviceConnected(
      [&p, channelGridRaw](const AudioDeviceMonitor::DeviceInfo &info) {
        if (!info.isMultichannel)
          return;
        p.getAudioDeviceBridge().setAvailableOutputChannelCount(
            juce::jmax(2, info.numOutputChannels));
        p.refreshPadsRoutingFromSettings();
        if (channelGridRaw != nullptr)
          channelGridRaw->refreshStemRoutingVisibility();
      });

  refreshTopBarTheme = [this, topBarRaw, waveformBarRaw, channelGridRaw] {
    if (topBarRaw)
      topBarRaw->refreshThemeColours();
    if (waveformBarRaw)
      waveformBarRaw->refreshThemeColours();
    if (channelGridRaw)
      channelGridRaw->refreshThemeColours();
    if (padsComponent)
      padsComponent->refreshThemeColours();
    if (padsHandlePanel)
      padsHandlePanel->repaint();
  };

  auto songListImpl = std::make_unique<SongListPanel>();
  auto *songListRaw = songListImpl.get();
  songListPanel = std::move(songListImpl);
  songListPanelRaw = songListRaw;
  songListRaw->setOnSetlistNameChanged([this](const juce::String &name) {
    if (setlistHandlePanel)
      setlistHandlePanel->setTitle(name);
  });

  struct SongLoadUiScope {
    bool &busy;
    SongLoadProgressOverlay *overlay;
    ~SongLoadUiScope() {
      busy = false;
      if (overlay != nullptr)
        overlay->setVisible(false);
    }
  };

  auto loadSong = [channelGridRaw, waveformBarRaw, &p,
                   this](const ImportedSongData &song, int songIndex) -> bool {
    if (songLoadBusy)
      return false;
    songLoadBusy = true;
    SongLoadUiScope loadUiScope{songLoadBusy, songLoadOverlay.get()};

    auto pumpUi = [] {
      if (auto *mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(14);
    };

    auto pushLoadUi = [this, pumpUi](double overall01, const juce::String &phase) {
      if (songLoadOverlay == nullptr)
        return;
      songLoadOverlay->setProgress(overall01);
      songLoadOverlay->setPhaseText(phase);
      if (!songLoadOverlay->isVisible()) {
        songLoadOverlay->setVisible(true);
        songLoadOverlay->toFront(false);
      }
      pumpUi();
    };

    juce::StringArray stemPaths;
    std::vector<bool> clickGuideMask;
    std::vector<bool> pitchShiftMask;
    int clickStemIndex = -1;
    ImportedSongData loadedSong = song;
    loadedSong.stems.clear();
    loadedSong.stems.reserve(song.stems.size());
    std::vector<ImportedStemData> resolvedStems;
    resolvedStems.reserve(song.stems.size());

    for (const auto &stem : song.stems) {
      if (!stem.include)
        continue;

      juce::File file(stem.storedPath);
#if JUCE_WINDOWS
      // Legacy fallback: older imports stored ALAC .m4a, which can fail to
      // decode on some Windows setups. Prefer original source when available.
      if (file.existsAsFile() && file.hasFileExtension(".m4a")) {
        const juce::File source(stem.sourcePath);
        if (source.existsAsFile())
          file = source;
      }
#endif
      if (!file.existsAsFile())
        file = juce::File(stem.sourcePath);
      if (file.existsAsFile()) {
        ImportedStemData loadedStem = stem;
        loadedStem.storedPath = file.getFullPathName();
        loadedStem.include = true;
        resolvedStems.push_back(std::move(loadedStem));
      }
    }

    std::stable_sort(resolvedStems.begin(), resolvedStems.end(),
                     [](const ImportedStemData &a, const ImportedStemData &b) {
                       return stemPriorityForStripOrder(a) <
                              stemPriorityForStripOrder(b);
                     });

    for (const auto &loadedStem : resolvedStems) {
      const int stemPriority = stemPriorityForStripOrder(loadedStem);
      const int mappedStemIndex = static_cast<int>(loadedSong.stems.size());
      if (clickStemIndex < 0 && stemPriority == 0)
        clickStemIndex = mappedStemIndex;
      stemPaths.add(loadedStem.storedPath);
      loadedSong.stems.push_back(loadedStem);
      const bool isClickGuide = stemPriority <= 1;
      clickGuideMask.push_back(isClickGuide);
      pitchShiftMask.push_back(isStemPitchShiftable(loadedStem));
    }

    pushLoadUi(0.0, L10n::tr("Preparando pistas…", "Preparing tracks…"));

    const bool loaded = p.loadSongStems(
        stemPaths, song.title, song.key, static_cast<double>(song.bpm),
        [&pushLoadUi](double stemPhase01) {
          pushLoadUi(0.55 * stemPhase01,
                     L10n::tr("Leyendo audio de las pistas…",
                              "Reading stem audio…"));
        });

    if (!loaded) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon,
          L10n::tr("Error de carga", "Load error"),
          L10n::tr("No se pudieron cargar los stems de esta cancion.",
                   "Couldn't load audio stems for this song."));
      waveformBarRaw->setWaveformBins({});
      waveformBarRaw->setSongRhythm(0.0, {});
      p.setStemClickGuideMask({});
      p.setMetronomeClickStemIndex(-1);
      channelGridRaw->setSong(ImportedSongData{});
      hasCurrentLoadedSong = false;
      currentLoadedSong = ImportedSongData{};
      return false;
    }

    p.setStemClickGuideMask(clickGuideMask);
    p.setStemPitchShiftMask(pitchShiftMask);
    p.setMetronomeClickStemIndex(clickStemIndex);

    SongMixSnapshot mixSnapshot;
    if (SongLibraryStore::getActiveSetlistMix(loadedSong.id, mixSnapshot)) {
      p.setMasterVolume(mixSnapshot.master.volume);
      p.setMasterGain(mixSnapshot.master.gain);
      p.setMasterMute(mixSnapshot.master.mute);

      for (size_t i = 0; i < loadedSong.stems.size(); ++i) {
        const auto &stem = loadedSong.stems[i];
        if (!stem.include)
          continue;
        const auto it = std::find_if(
            mixSnapshot.stems.begin(), mixSnapshot.stems.end(),
            [&stem](const StemMixSnapshot &snapshot) {
              return snapshot.stemId == stem.stemId;
            });
        if (it == mixSnapshot.stems.end())
          continue;
        const int stemIndex = static_cast<int>(i);
        p.setStemVolume(stemIndex, it->volume);
        p.setStemGain(stemIndex, it->gain);
        p.setStemMute(stemIndex, it->mute);
        p.setStemSolo(stemIndex, it->solo);
      }
    }

    currentLoadedSong = loadedSong;
    hasCurrentLoadedSong = true;
    AppSettings::setLastSelectedSongId(currentLoadedSong.id);
    applyRoutingDefaultsToSong(currentLoadedSong);

    channelGridRaw->setSong(loadedSong);
    currentSongIndex = songIndex;

    auto cachedBins = SongLibraryStore::loadWaveformCache(loadedSong.id);
    if (cachedBins.empty()) {
      pushLoadUi(0.58, L10n::tr("Generando vista de forma de onda…",
                                "Generating waveform preview…"));
      cachedBins = buildMixedWaveformBins(
          loadedSong, 1400, [&pushLoadUi](const double wf01) {
            pushLoadUi(0.58 + 0.40 * wf01,
                       L10n::tr("Generando vista de forma de onda…",
                                "Generating waveform preview…"));
          });
      SongLibraryStore::saveWaveformCache(loadedSong.id, cachedBins);
    } else {
      pushLoadUi(0.98, L10n::tr("Cargando forma de onda en caché…",
                                "Loading cached waveform…"));
    }

    pushLoadUi(1.0, L10n::tr("Finalizando…", "Finishing…"));
    waveformBarRaw->setWaveformBins(std::move(cachedBins));
    waveformBarRaw->setSongRhythm(static_cast<double>(loadedSong.bpm),
                                  loadedSong.timeSignature);
    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (!SongLibraryStore::getActiveSetlistArrangement(
            loadedSong.id, arrangement, isCustomArrangement)) {
      arrangement.parts.clear();
      isCustomArrangement = false;
    }
    waveformBarRaw->setArrangement(arrangement, isCustomArrangement);
    p.setSongArrangement(arrangement, false);
    return true;
  };

  songListRaw->setOnSongActivated(
      [loadSong](const ImportedSongData &song, int songIndex) {
        (void)loadSong(song, songIndex);
      });

  auto loadSongByIndex = [songListRaw, loadSong](int index) -> bool {
    if (!songListRaw)
      return false;
    if (const auto *song = songListRaw->getSongPtr(index))
      return loadSong(*song, index);
    return false;
  };

  loadSongByIndexAction = [songListRaw, loadSongByIndex, &p](int index,
                                                              bool autoPlay) {
    if (!songListRaw)
      return;
    if (!juce::isPositiveAndBelow(index, songListRaw->getSongCount()))
      return;
    if (loadSongByIndex(index) && autoPlay)
      p.playSong();
  };

  {
    const auto lastSelectedSongId = AppSettings::getLastSelectedSongId();
    if (lastSelectedSongId.isNotEmpty()) {
      const int index = songListRaw != nullptr
                            ? songListRaw->getSongIndexById(lastSelectedSongId)
                            : -1;
      if (index >= 0)
        loadSongByIndex(index);
    }
  }

  auto resolveCurrentSongIndex = [songListRaw, this]() -> int {
    if (!songListRaw)
      return -1;
    if (hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
      const int resolved = songListRaw->getSongIndexById(currentLoadedSong.id);
      if (resolved >= 0) {
        currentSongIndex = resolved;
        return resolved;
      }
    }
    return currentSongIndex;
  };

  auto prevSong = [songListRaw, loadSongByIndex, resolveCurrentSongIndex]() {
    if (!songListRaw)
      return;
    const int count = songListRaw->getSongCount();
    if (count <= 0)
      return;
    int index = resolveCurrentSongIndex();
    if (index < 0)
      index = songListRaw->getLastActivatedIndex();
    if (index < 0)
      index = 0;
    const int target = juce::jlimit(0, count - 1, index - 1);
    if (target == index)
      return;
    loadSongByIndex(target);
  };

  auto nextSong = [songListRaw, loadSongByIndex, resolveCurrentSongIndex]() {
    if (!songListRaw)
      return;
    const int count = songListRaw->getSongCount();
    if (count <= 0)
      return;
    int index = resolveCurrentSongIndex();
    if (index < 0)
      index = songListRaw->getLastActivatedIndex();
    if (index < 0)
      index = 0;
    const int target = juce::jlimit(0, count - 1, index + 1);
    if (target == index)
      return;
    loadSongByIndex(target);
  };

  prevSongAction = prevSong;
  nextSongAction = nextSong;

  topBarRaw->setTransportCallbacks(
      [&p] { p.toggleSongPlayPause(); }, [&p] { p.stopSong(); }, prevSong,
      nextSong, [&p](bool fadeOut) {
        p.setFadeStopsPlayback(false);
        p.setFadeActive(fadeOut);
      },
      [&p](double deltaSec) { p.seekSongRelative(deltaSec); });
  topBarRaw->setOnSettingsWithAnchor(
      [this](juce::Rectangle<int> anchor, juce::Component *parent) {
        showSettings(anchor, parent);
      });

  setlistHandlePanel = std::make_unique<SetlistHandlePanel>("Setlist A");
  setlistHandlePanel->onToggle = [this] {
    autoSetlistCollapseInitialized = true;
    isSetlistCollapsed = !isSetlistCollapsed;
    resized();
    repaint();
  };
  if (songListRaw)
    setlistHandlePanel->setTitle(songListRaw->getActiveSetlistName());

  padsComponent = std::make_unique<PadsComponent>(p, stripLookAndFeel);
  padsComponent->setVisible(false);
  addChildComponent(*padsComponent);

  padsHandlePanel = std::make_unique<PadsHandlePanel>();
  padsHandlePanel->setCollapsed(isPadsCollapsed);
  padsHandlePanel->onToggle = [this] {
    isPadsCollapsed = !isPadsCollapsed;
    padsHandlePanel->setCollapsed(isPadsCollapsed);
    padsComponent->setVisible(!isPadsCollapsed);
    resized();
  };
  addAndMakeVisible(*padsHandlePanel);

  addAndMakeVisible(*topBar);
  addAndMakeVisible(*waveformBar);
  addAndMakeVisible(*channelGridPanel);
  addAndMakeVisible(*songListPanel);
  addAndMakeVisible(*setlistHandlePanel);

  songLoadOverlay = std::make_unique<SongLoadProgressOverlay>();
  addChildComponent(*songLoadOverlay);
  songLoadOverlay->setVisible(false);

  if (reauthPromptModal == nullptr) {
    auto prompt = std::make_unique<ReauthPromptModal>();
    prompt->onLoginNow = [this] {
      reauthPromptDismissed = false;
      reauthLoginInProgress = true;
      if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
        reauth->showWaitingForCallback();
      launchExternalLogin();
    };
    prompt->onLater = [this] {
      reauthPromptDismissed = true;
      reauthLoginInProgress = false;
      hideReauthPromptModal();
    };
    reauthPromptModal = std::move(prompt);
    addAndMakeVisible(*reauthPromptModal);
    reauthPromptModal->setVisible(false);
  }
  if (welcomeModal)
    welcomeModal->setVisible(false);

  const auto userArea =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
  const int maxW = juce::jmax(kMinWindowW, userArea.getWidth() - kScreenMargin);
  const int maxH = juce::jmax(kMinWindowH, userArea.getHeight() - kScreenMargin);
  const int w = juce::jlimit(kMinWindowW, maxW, kDefaultWindowW);
  const int h = juce::jlimit(kMinWindowH, maxH, kDefaultWindowH);
  setResizable(true, true);
  setResizeLimits(kMinWindowW, kMinWindowH, 32000, 32000);
  setSize(w, h);
  applyResponsiveLayoutProfile(resolveResponsiveLayoutProfile());
  if (!autoSetlistCollapseInitialized) {
    isSetlistCollapsed =
        (responsiveLayoutProfile == ResponsiveLayoutProfile::Compact);
    autoSetlistCollapseInitialized = true;
  }

  if (AppSettings::getExpandWindowOnLaunch()) {
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
             this)] {
          if (safe != nullptr)
            expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
        });
  }

  setWantsKeyboardFocus(true);
  grabKeyboardFocus();

  wasSongPlaying = p.isSongPlaying();
  lastObservedSongPositionSec = p.getSongCurrentPositionSeconds();
  startTimerHz(20);
  mainUiInitialized = true;
  resized();
}

void MTControlsAudioProcessorEditor::initializeWelcomeModal() {
  auto modal = std::make_unique<WelcomeModal>();
  modal->onLoginRequested = [this] { launchExternalLogin(); };
  welcomeModal = std::move(modal);
  addAndMakeVisible(*welcomeModal);
  resized();
}

void MTControlsAudioProcessorEditor::launchExternalLogin() {
  if (localAuthServer != nullptr)
    localAuthServer->stop();
  localAuthServer = std::make_unique<LocalAuthCallbackServer>(
      [this](const juce::String &callbackUrl) { enqueueAuthCallbackUrl(callbackUrl); });
  authCallbackPort = 0;

  if (!localAuthServer->start(authCallbackPort)) {
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(
          "No se pudo iniciar el callback local. Verifica puertos locales.", true);
    if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
      reauth->showError("No se pudo iniciar el callback local. Verifica puertos.");
    reauthLoginInProgress = false;
    authCallbackPort = 0;
    return;
  }

  authExpectedState = juce::Uuid().toString();
  const juce::String redirectUri =
      "http://127.0.0.1:" + juce::String(authCallbackPort) + "/auth/callback";
  const juce::String deviceId = buildStableDeviceId();
  const juce::String apiBaseUrl = getApiBaseUrl();
  const juce::String loginUrl =
      apiBaseUrl + "/login?redirect_uri=" + urlEncode(redirectUri) +
      "&state=" + urlEncode(authExpectedState) +
      "&deviceId=" + urlEncode(deviceId);

  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
    modal->setStatusText("Esperando autenticacion en el navegador...", false);
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
    reauth->showWaitingForCallback();

  juce::URL(loginUrl).launchInDefaultBrowser();
}

void MTControlsAudioProcessorEditor::completeBootstrapLogin(
    const juce::String &callbackUrl) {
  const auto params = parseCallbackParams(callbackUrl);
  const auto receivedState = params["state"].trim();
  if (authExpectedState.isEmpty() || receivedState.isEmpty() ||
      receivedState != authExpectedState) {
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
      modal->setStatusText("Autenticacion invalida. Reintenta.", true);
    }
    if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
      reauth->showError("Autenticacion invalida. Reintenta.");
    reauthLoginInProgress = false;
    return;
  }

  activeSession.uid = params["uid"].trim();
  activeSession.email = params["email"].trim();
  activeSession.plan = params["plan"].trim();
  activeSession.subscriptionStatus = params["subscriptionStatus"].trim();
  activeSession.expiresAt = params["expiresAt"].trim();
  activeSession.allowed = parseBooleanLike(params["allowed"], false);
  activeSession.reasonCode = params["reasonCode"].trim();
  activeSession.reason = params["reason"].trim();
  activeSession.refreshToken = params["refreshToken"].trim();
  activeSession.firebaseApiKey = params["firebaseApiKey"].trim();
  activeSession.licenseKey = params["licenseKey"].trim();
  const auto expMsFromParams = params["idTokenExpiresAtMs"].trim().getLargeIntValue();
  if (expMsFromParams > 0)
    activeSession.idTokenExpiresAtMs = expMsFromParams;
  const auto sessionJsonRaw = params["session"].trim();
  if (sessionJsonRaw.isNotEmpty()) {
    const auto parsed = juce::JSON::parse(sessionJsonRaw);
    activeSession.uid =
        readStringFromObject(parsed, "uid").isNotEmpty()
            ? readStringFromObject(parsed, "uid")
            : activeSession.uid;
    activeSession.email =
        readStringFromObject(parsed, "email").isNotEmpty()
            ? readStringFromObject(parsed, "email")
            : activeSession.email;
    activeSession.plan =
        readStringFromObject(parsed, "plan").isNotEmpty()
            ? readStringFromObject(parsed, "plan")
            : activeSession.plan;
    activeSession.subscriptionStatus =
        readStringFromObject(parsed, "subscriptionStatus").isNotEmpty()
            ? readStringFromObject(parsed, "subscriptionStatus")
            : activeSession.subscriptionStatus;
    activeSession.expiresAt =
        readStringFromObject(parsed, "expiresAt").isNotEmpty()
            ? readStringFromObject(parsed, "expiresAt")
            : activeSession.expiresAt;
    activeSession.allowed = parseBooleanLike(
        readStringFromObject(parsed, "allowed"), activeSession.allowed);
    activeSession.reasonCode = readStringFromObject(parsed, "reasonCode")
                                   .isNotEmpty()
                               ? readStringFromObject(parsed, "reasonCode")
                               : activeSession.reasonCode;
    activeSession.reason = readStringFromObject(parsed, "reason").isNotEmpty()
                               ? readStringFromObject(parsed, "reason")
                               : activeSession.reason;
    activeSession.refreshToken =
        readStringFromObject(parsed, "refreshToken").isNotEmpty()
            ? readStringFromObject(parsed, "refreshToken")
            : activeSession.refreshToken;
    activeSession.firebaseApiKey =
        readStringFromObject(parsed, "firebaseApiKey").isNotEmpty()
            ? readStringFromObject(parsed, "firebaseApiKey")
            : activeSession.firebaseApiKey;
    if (readStringFromObject(parsed, "licenseKey").isNotEmpty())
      activeSession.licenseKey = readStringFromObject(parsed, "licenseKey");
    const auto expMsRaw = readStringFromObject(parsed, "idTokenExpiresAtMs");
    const auto expMs = expMsRaw.getLargeIntValue();
    if (expMs > 0)
      activeSession.idTokenExpiresAtMs = expMs;

    const auto userVar = parsed.getProperty("user", juce::var{});
    if (userVar.isObject()) {
      const auto userUid = readStringFromObject(userVar, "uid");
      const auto userEmail = readStringFromObject(userVar, "email");
      const auto userPlan = readStringFromObject(userVar, "plan");
      const auto userStatus = readStringFromObject(userVar, "subscriptionStatus");
      if (userUid.isNotEmpty())
        activeSession.uid = userUid;
      if (userEmail.isNotEmpty())
        activeSession.email = userEmail;
      if (userPlan.isNotEmpty())
        activeSession.plan = userPlan;
      if (userStatus.isNotEmpty())
        activeSession.subscriptionStatus = userStatus;
    }
  }

  if (activeSession.allowed &&
      !hasValidAccessBySubscriptionState(activeSession.subscriptionStatus,
                                         activeSession.plan)) {
    activeSession.allowed = false;
    if (activeSession.reasonCode.isEmpty())
      activeSession.reasonCode = "INVALID_SUBSCRIPTION_STATE";
    if (activeSession.reason.isEmpty()) {
      activeSession.reason =
          "Subscription status is not trial/active with a valid plan";
    }
  }

  if (!activeSession.allowed) {
    juce::String message =
        userMessageForAccessDenial(activeSession.reasonCode, activeSession.reason);
    reauthLoginInProgress = false;
    if (mainUiInitialized) {
      handleLogout();
    }
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(message, true);
    return;
  }

  lastSuccessfulLoginMs = juce::Time::currentTimeMillis();
  AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
  reauthGraceDeadlineMs = 0;
  AppSettings::setReauthGraceDeadlineMs(0);
  reauthPromptDismissed = false;
  reauthLoginInProgress = false;
  hideReauthPromptModal();
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
    reauth->showReadyState();
  saveSessionSnapshotToDisk();
  SessionAuthLogger::write(
      "completeBootstrapLogin: web login OK, snapshot saved, calling "
      "initializeMainUi");
  initializeMainUi();
}

juce::String MTControlsAudioProcessorEditor::consumePendingAuthCallbackUrl() {
  const juce::ScopedLock lock(authCallbackLock);
  auto url = pendingAuthCallbackUrl;
  pendingAuthCallbackUrl.clear();
  return url;
}

void MTControlsAudioProcessorEditor::enqueueAuthCallbackUrl(
    const juce::String &callbackUrl) {
  const juce::ScopedLock lock(authCallbackLock);
  pendingAuthCallbackUrl = callbackUrl;
}

void MTControlsAudioProcessorEditor::showSettings(juce::Rectangle<int>,
                                                  juce::Component *parent) {
  if (!mainUiInitialized)
    return;
  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr("Configuracion", "Settings");
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
  auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
  const auto routingDefaults = AppSettings::getRoutingDefaults();
  const auto audioCtx = getStandaloneAudioContext();
  auto *panel = new SettingsPanel(
      static_cast<ThemePreset>(currentThemeId),
      [this](ThemePreset preset) { applyThemePreset(static_cast<int>(preset)); },
      AppSettings::getLanguage(),
      [this](AppLanguage language) {
        AppSettings::setLanguage(language);
        if (songListPanelRaw != nullptr)
          songListPanelRaw->refreshLanguageTexts();
        repaint();
      },
      AppSettings::getExpandWindowOnLaunch(),
      [](const bool expandOnLaunch) {
        AppSettings::setExpandWindowOnLaunch(expandOnLaunch);
      },
      AppSettings::getPrimeImportEnabled(),
      [](const bool enabled) { AppSettings::setPrimeImportEnabled(enabled); },
      AppSettings::getStemOptimizationEnabled(),
      [](const bool enabled) { AppSettings::setStemOptimizationEnabled(enabled); },
      [] {},
      SettingsPanel::SessionSummary{activeSession.uid, activeSession.email,
                                    activeSession.plan,
                                    activeSession.subscriptionStatus,
                                    activeSession.expiresAt,
                                    activeSession.allowed,
                                    activeSession.reasonCode,
                                    activeSession.reason,
                                    activeSession.licenseKey},
      [this] { handleLogout(); }, routingDefaults,
      proc.getAvailableOutputChannelCount(),
      [this](const juce::String &deviceKey,
             const std::vector<CategoryRoutingDefaults> &next) {
        AppSettings::setRoutingDefaultsForDevice(deviceKey, next);
        auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
        processorRef.refreshPadsRoutingFromSettings();
        if (hasCurrentLoadedSong)
          applyRoutingDefaultsToSong(currentLoadedSong);
      },
      audioCtx.manager, audioCtx.maxInputs, audioCtx.maxOutputs);
  panel->setSize(760, 620);
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    settingsWindow = window;
    window->centreAroundComponent(parent != nullptr ? parent : this, 760, 620);
  }
}

void MTControlsAudioProcessorEditor::showWelcomeOnlyMode() {
  setResizable(false, false);
  setResizeLimits(kAuthWindowW, kAuthWindowH, kAuthWindowW, kAuthWindowH);
  setSize(kAuthWindowW, kAuthWindowH);
  if (topBar)
    topBar->setVisible(false);
  if (waveformBar)
    waveformBar->setVisible(false);
  if (channelGridPanel)
    channelGridPanel->setVisible(false);
  if (songListPanel)
    songListPanel->setVisible(false);
  if (setlistHandlePanel)
    setlistHandlePanel->setVisible(false);
  if (welcomeModal)
    welcomeModal->setVisible(true);
  if (reauthPromptModal)
    reauthPromptModal->setVisible(false);
  mainUiInitialized = false;
  autoSetlistCollapseInitialized = false;
  resized();
}

void MTControlsAudioProcessorEditor::handleLogout() {
  SessionAuthLogger::write("handleLogout");
  if (settingsWindow != nullptr) {
    settingsWindow->setVisible(false);
    settingsWindow->exitModalState(0);
    settingsWindow = nullptr;
  }

  if (localAuthServer != nullptr)
    localAuthServer->stop();
  authCallbackPort = 0;

  activeSession = {};
  lastOnlineValidationMs = 0;
  SessionPersistence::clear();
  lastSuccessfulLoginMs = 0;
  AppSettings::setLastSuccessfulLoginMs(0);
  reauthGraceDeadlineMs = 0;
  AppSettings::setReauthGraceDeadlineMs(0);
  reauthPromptDismissed = false;
  reauthLoginInProgress = false;
  hideReauthPromptModal();
  authExpectedState.clear();
  {
    const juce::ScopedLock lock(authCallbackLock);
    pendingAuthCallbackUrl.clear();
  }
  if (auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
      proc.isSongPlaying()) {
    proc.stopSong();
  }
  showWelcomeOnlyMode();
  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
    modal->resetStatusToDefault();
}

void MTControlsAudioProcessorEditor::showReauthPromptModal() {
  if (!mainUiInitialized || reauthPromptModal == nullptr)
    return;
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get())) {
    if (reauthLoginInProgress)
      reauth->showWaitingForCallback();
    else
      reauth->showReadyState();
  }
  reauthPromptModal->setVisible(true);
  reauthPromptModal->toFront(false);
  resized();
}

void MTControlsAudioProcessorEditor::hideReauthPromptModal() {
  if (reauthPromptModal == nullptr)
    return;
  reauthPromptModal->setVisible(false);
}

PersistedSessionSnapshot
MTControlsAudioProcessorEditor::persistedSnapshotFromActiveSession(
    const juce::int64 lastOnlineMs) const {
  PersistedSessionSnapshot p;
  p.uid = activeSession.uid;
  p.email = activeSession.email;
  p.plan = activeSession.plan;
  p.subscriptionStatus = activeSession.subscriptionStatus;
  p.expiresAt = activeSession.expiresAt;
  p.licenseKey = activeSession.licenseKey;
  p.allowed = activeSession.allowed;
  p.reasonCode = activeSession.reasonCode;
  p.reason = activeSession.reason;
  p.refreshToken = activeSession.refreshToken;
  p.firebaseApiKey = activeSession.firebaseApiKey;
  p.idTokenExpiresAtMs = activeSession.idTokenExpiresAtMs;
  p.lastOnlineValidationMs = lastOnlineMs;
  return p;
}

void MTControlsAudioProcessorEditor::applyPersistedSnapshotToActive(
    const PersistedSessionSnapshot &snap) {
  activeSession.uid = snap.uid;
  activeSession.email = snap.email;
  activeSession.plan = snap.plan;
  activeSession.subscriptionStatus = snap.subscriptionStatus;
  activeSession.expiresAt = snap.expiresAt;
  activeSession.licenseKey = snap.licenseKey;
  activeSession.allowed = snap.allowed;
  activeSession.reasonCode = snap.reasonCode;
  activeSession.reason = snap.reason;
  activeSession.refreshToken = snap.refreshToken;
  activeSession.firebaseApiKey = snap.firebaseApiKey;
  activeSession.idTokenExpiresAtMs = snap.idTokenExpiresAtMs;
  lastOnlineValidationMs = snap.lastOnlineValidationMs;
}

void MTControlsAudioProcessorEditor::saveSessionSnapshotToDisk() {
  const auto nowMs = juce::Time::currentTimeMillis();
  lastOnlineValidationMs = nowMs;
  SessionPersistence::save(persistedSnapshotFromActiveSession(nowMs));
}

void MTControlsAudioProcessorEditor::tryResumeSessionAsync() {
  SessionAuthLogger::write("tryResumeSessionAsync: begin");
  PersistedSessionSnapshot snap;
  if (!SessionPersistence::load(snap)) {
    SessionAuthLogger::write(
        "tryResumeSessionAsync: no valid snapshot, user must use web login");
    return;
  }
  if (!snap.allowed ||
      !hasValidAccessBySubscriptionState(snap.subscriptionStatus, snap.plan)) {
    SessionAuthLogger::write(
        "tryResumeSessionAsync: snapshot invalid allowed=" +
        juce::String(static_cast<int>(snap.allowed)) + " plan=" + snap.plan +
        " status=" + snap.subscriptionStatus + " -> clearing snapshot");
    SessionPersistence::clear();
    return;
  }

  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
    modal->setStatusText(L10n::tr("Comprobando sesion...", "Checking session..."),
                         false);
    modal->setChecking(true);
  }

  juce::Component::SafePointer<MTControlsAudioProcessorEditor> safe(this);
  const juce::String apiBase = getApiBaseUrl();
  const juce::String deviceId = buildStableDeviceId();
  SessionAuthLogger::write("tryResumeSessionAsync: apiBase=" + apiBase +
                           " deviceIdPrefix=" + deviceId.substring(0, 12) + "...");

  std::thread([safe, snap, apiBase, deviceId] {
    bool success = false;
    bool validatedOnline = false;
    bool clearSnapshot = false;
    juce::String welcomeError;
    PersistedSessionSnapshot out = snap;

    const auto tryOfflineWithinTtl = [&]() {
      const juce::int64 nowMs = juce::Time::currentTimeMillis();
      const juce::int64 ageMs = nowMs - snap.lastOnlineValidationMs;
      if (snap.lastOnlineValidationMs > 0 &&
          ageMs <= kMaxOfflineMs && snap.allowed &&
          hasValidAccessBySubscriptionState(snap.subscriptionStatus, snap.plan)) {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): offline fallback OK ageMs=" +
            juce::String(ageMs) + " maxOfflineMs=" + juce::String(kMaxOfflineMs));
        success = true;
        validatedOnline = false;
        out = snap;
        return true;
      }
      SessionAuthLogger::write(
          "tryResumeSessionAsync(bg): offline fallback FAIL ageMs=" +
          juce::String(ageMs) + " lastOnlineMs=" +
          juce::String(snap.lastOnlineValidationMs) + " nowMs=" +
          juce::String(nowMs));
      welcomeError =
          L10n::tr("Se requiere conexion para validar la suscripcion.",
                   "An internet connection is required to validate your subscription.");
      clearSnapshot = false;
      return false;
    };

    const auto fr =
        AuthSessionApi::refreshFirebaseIdToken(snap.firebaseApiKey, snap.refreshToken);
    if (fr.ok) {
      out.refreshToken =
          fr.refreshToken.isNotEmpty() ? fr.refreshToken : snap.refreshToken;

      const auto va = AuthSessionApi::fetchValidateAccess(
          apiBase, fr.idToken, deviceId, snap.licenseKey);
      if (va.rawError == "network" || va.rawError == "parse") {
        SessionAuthLogger::write(
            va.rawError == "network"
                ? "tryResumeSessionAsync(bg): validate-access network error, trying "
                  "offline TTL"
                : "tryResumeSessionAsync(bg): validate-access non-JSON/infra "
                  "(parse), trying offline TTL");
        if (!tryOfflineWithinTtl())
          success = false;
      } else if (va.allowed && hasValidAccessBySubscriptionState(va.subscriptionStatus,
                                                                   va.plan)) {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): validate-access OK (online)");
        success = true;
        validatedOnline = true;
        out.allowed = true;
        out.plan = va.plan;
        out.subscriptionStatus = va.subscriptionStatus;
        out.expiresAt = va.expiresAt;
        if (va.licenseKey.isNotEmpty())
          out.licenseKey = va.licenseKey;
        out.reasonCode.clear();
        out.reason.clear();
        out.lastOnlineValidationMs = juce::Time::currentTimeMillis();
      } else {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): validate-access denied allowed=" +
            juce::String(static_cast<int>(va.allowed)) + " reasonCode=" +
            va.reasonCode);
        clearSnapshot = true;
        welcomeError =
            userMessageForAccessDenial(va.reasonCode, va.reason);
      }
    } else {
      if (fr.errorMessage == "network" || fr.errorMessage == "parse") {
        SessionAuthLogger::write(
            fr.errorMessage == "network"
                ? "tryResumeSessionAsync(bg): Firebase refresh network, trying "
                  "offline TTL"
                : "tryResumeSessionAsync(bg): Firebase refresh parse/infra, trying "
                  "offline TTL");
        if (!tryOfflineWithinTtl())
          success = false;
      } else {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): Firebase refresh failed error=" +
            fr.errorMessage);
        clearSnapshot = true;
        welcomeError =
            L10n::tr("Sesion expirada. Vuelve a iniciar sesion.",
                     "Session expired. Please sign in again.");
      }
    }

    SessionAuthLogger::write(
        juce::String("tryResumeSessionAsync(bg): done success=") +
        juce::String(static_cast<int>(success)) + " validatedOnline=" +
        juce::String(static_cast<int>(validatedOnline)) + " clearSnapshot=" +
        juce::String(static_cast<int>(clearSnapshot)));

    juce::MessageManager::callAsync([safe, success, validatedOnline, out, welcomeError,
                                     clearSnapshot] {
      if (safe != nullptr)
        safe->applySessionResumeOnMessageThread(success, validatedOnline, out,
                                                welcomeError, clearSnapshot);
    });
  }).detach();
}

void MTControlsAudioProcessorEditor::applySessionResumeOnMessageThread(
    const bool success, const bool validatedOnline, PersistedSessionSnapshot snapshot,
    const juce::String &welcomeError, const bool clearSnapshotFile) {
  if (mainUiInitialized) {
    SessionAuthLogger::write(
        "applySessionResumeOnMessageThread: skipped (mainUiInitialized already)");
    return;
  }
  if (authExpectedState.isNotEmpty()) {
    SessionAuthLogger::write(
        "applySessionResumeOnMessageThread: skipped (user started web login, "
        "authExpectedState set)");
    return;
  }

  if (clearSnapshotFile)
    SessionPersistence::clear();

  if (!success) {
    SessionAuthLogger::write(
        "applySessionResumeOnMessageThread: failure welcomeError=" + welcomeError);
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
      modal->setChecking(false);
      if (welcomeError.isNotEmpty())
        modal->setStatusText(welcomeError, true);
      else
        modal->resetStatusToDefault();
    }
    return;
  }

  SessionAuthLogger::write(
      juce::String("applySessionResumeOnMessageThread: entering main UI ") +
      "validatedOnline=" + juce::String(static_cast<int>(validatedOnline)));

  applyPersistedSnapshotToActive(snapshot);
  if (validatedOnline) {
    SessionPersistence::save(snapshot);
    lastSuccessfulLoginMs = juce::Time::currentTimeMillis();
    lastOnlineValidationMs = snapshot.lastOnlineValidationMs;
    AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
  } else {
    lastSuccessfulLoginMs =
        juce::jmax(lastSuccessfulLoginMs, snapshot.lastOnlineValidationMs);
    AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
  }
  reauthGraceDeadlineMs = 0;
  AppSettings::setReauthGraceDeadlineMs(0);
  reauthPromptDismissed = false;
  reauthLoginInProgress = false;

  initializeMainUi();
}

void MTControlsAudioProcessorEditor::evaluateReauthPolicy() {
  // lastSuccessfulLoginMs: ultima confirmacion util (login web o validate-access al
  // arrancar). Se actualiza en disco via AppSettings; lastOnlineValidationMs duplica
  // el valor persistido en session_snapshot para ventana offline.
  if (!mainUiInitialized || lastSuccessfulLoginMs <= 0)
    return;

  if (!hasValidAccessBySubscriptionState(activeSession.subscriptionStatus,
                                         activeSession.plan)) {
    activeSession.allowed = false;
    activeSession.reasonCode =
        activeSession.subscriptionStatus.trim().toLowerCase() == "cancelled"
            ? "CANCELLED_SUBSCRIPTION"
            : "INVALID_SUBSCRIPTION_STATE";
    activeSession.reason =
        "Subscription status is not trial/active with a valid plan";
    SessionAuthLogger::write(
        "evaluateReauthPolicy: invalid subscription state -> handleLogout code=" +
        activeSession.reasonCode);
    handleLogout();
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(userMessageForAccessDenial(activeSession.reasonCode,
                                                      activeSession.reason),
                           true);
    return;
  }

  const auto nowMs = juce::Time::currentTimeMillis();
  const auto sessionDueAtMs = lastSuccessfulLoginMs + kSessionReauthIntervalMs;
  if (nowMs < sessionDueAtMs) {
    if (reauthGraceDeadlineMs != 0) {
      reauthGraceDeadlineMs = 0;
      AppSettings::setReauthGraceDeadlineMs(0);
    }
    hideReauthPromptModal();
    return;
  }

  // La gracia inicia en el momento en que solicitamos re-login por primera vez.
  if (reauthGraceDeadlineMs <= 0) {
    reauthGraceDeadlineMs = nowMs + kReauthGraceWindowMs;
    AppSettings::setReauthGraceDeadlineMs(reauthGraceDeadlineMs);
    reauthPromptDismissed = false;
  }

  if (reauthGraceDeadlineMs > 0 && nowMs >= reauthGraceDeadlineMs) {
    SessionAuthLogger::write(
        "evaluateReauthPolicy: reauth grace expired -> handleLogout");
    handleLogout();
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(
          "Tu periodo de gracia termino. Debes iniciar sesion para continuar.",
          true);
    return;
  }

  // Si ya hay gracia activa (el usuario pulso "Mas tarde"), no molestamos
  // hasta que venza esa gracia.
  if (reauthGraceDeadlineMs > 0 && reauthPromptDismissed) {
    hideReauthPromptModal();
    return;
  }

  showReauthPromptModal();
}

void MTControlsAudioProcessorEditor::applyThemePreset(int presetId) {
  currentThemeId = juce::jlimit(1, 3, presetId);
  AppSettings::setThemePresetId(currentThemeId);
  applyTheme(static_cast<ThemePreset>(currentThemeId));
  if (refreshTopBarTheme)
    refreshTopBarTheme();
  if (songLoadOverlay != nullptr)
    songLoadOverlay->refreshThemeColours();
  repaint();
}

void MTControlsAudioProcessorEditor::applyRoutingDefaultsToSong(
    const ImportedSongData &song) {
  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const int stemCount = processorRef.getLoadedStemCount();
  const auto audioCtx = getStandaloneAudioContext();
  const auto deviceKey = getCurrentDeviceKey(audioCtx);
  const auto defaults = AppSettings::getRoutingDefaultsForDevice(deviceKey);
  const auto clickRouting = routingForCategory(defaults, "Click");
  processorRef.setMetronomeOutputRouting(clickRouting.baseChannel,
                                         clickRouting.width);

  int stemIndex = 0;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    if (!juce::isPositiveAndBelow(stemIndex, stemCount))
      break;

    const auto routing = routingForCategory(defaults, stem.category);
    processorRef.setStemOutputWidth(stemIndex, routing.width);
    processorRef.setStemOutputChannel(stemIndex, routing.baseChannel);
    ++stemIndex;
  }
  processorRef.refreshPadsRoutingFromSettings();
}

void MTControlsAudioProcessorEditor::timerCallback() {
  const auto callbackUrl = consumePendingAuthCallbackUrl();
  if (callbackUrl.isNotEmpty()) {
    completeBootstrapLogin(callbackUrl);
    if (!mainUiInitialized)
      return;
  }

  if (!mainUiInitialized) {
    return;
  }
  evaluateReauthPolicy();
  if (!mainUiInitialized)
    return;

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const bool isPlayingNow = processorRef.isSongPlaying();
  const double currentPosSec = processorRef.getSongCurrentPositionSeconds();

  if (songListPanelRaw != nullptr) {
    const bool hasPlayingSong = isPlayingNow && hasCurrentLoadedSong &&
                                currentLoadedSong.id.isNotEmpty();
    songListPanelRaw->setNowPlayingSong(
        hasPlayingSong ? currentLoadedSong.id : juce::String(), hasPlayingSong);
  }

  if (wasSongPlaying && !isPlayingNow && songListPanelRaw != nullptr &&
      hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
    const double startSec = processorRef.getSongStartSeconds();
    const double endSec = processorRef.getSongEndSeconds();
    const bool reachedSongEnd =
        endSec > startSec && lastObservedSongPositionSec >= (endSec - 0.12);
    const bool wrappedToStart = currentPosSec <= (startSec + 0.12);

    if (reachedSongEnd && wrappedToStart &&
        songListPanelRaw->isAutoPlayNextEnabledForSong(currentLoadedSong.id)) {
      const int currentIndex =
          songListPanelRaw->getSongIndexById(currentLoadedSong.id);
      const int nextIndex = currentIndex + 1;
      if (juce::isPositiveAndBelow(nextIndex, songListPanelRaw->getSongCount()) &&
          loadSongByIndexAction) {
        loadSongByIndexAction(nextIndex, true);
      }
    }
  }

  if (isPlayingNow)
    lastObservedSongPositionSec = currentPosSec;
  else if (!wasSongPlaying)
    lastObservedSongPositionSec = currentPosSec;

  wasSongPlaying = isPlayingNow;
}

MTControlsAudioProcessorEditor::~MTControlsAudioProcessorEditor() {
  stopTimer();
  if (localAuthServer)
    localAuthServer->stop();
  localAuthServer.reset();
  songLoadOverlay.reset();
  loadSongByIndexAction = {};
  songListPanelRaw = nullptr;
  auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
  proc.getAudioDeviceBridge().setOnDeviceConnected({});
  proc.getAudioDeviceBridge().setOnDeviceDisconnected({});
}

void MTControlsAudioProcessorEditor::paint(juce::Graphics &g) {
  drawStudioBackground(g, getLocalBounds().toFloat());
}

void MTControlsAudioProcessorEditor::resized() {
  if (!mainUiInitialized) {
    if (welcomeModal != nullptr)
      welcomeModal->setBounds(getLocalBounds());
    return;
  }

  auto area = getLocalBounds();
  updateResponsiveLayoutProfile();

  if (topBar == nullptr || waveformBar == nullptr || channelGridPanel == nullptr ||
      songListPanel == nullptr || setlistHandlePanel == nullptr)
    return;

  int topBarH = kDefaultTopBarH;
  int waveformH = kDefaultWaveformH;
  int waveformExpandedH = kDefaultWaveformExpandedH;
  float setlistRatio = 0.35f;
  int setlistMinW = 280;
  int setlistMaxW = 460;
  int contentInset = 8;

  if (responsiveLayoutProfile == ResponsiveLayoutProfile::Compact) {
    topBarH = 54;
    waveformH = kCompactWaveformH;
    waveformExpandedH = kCompactWaveformExpandedH;
    setlistRatio = 0.32f;
    setlistMinW = 220;
    setlistMaxW = 360;
    contentInset = 6;
  } else if (responsiveLayoutProfile == ResponsiveLayoutProfile::Large) {
    topBarH = 62;
    waveformH = kLargeWaveformH;
    waveformExpandedH = kDefaultWaveformExpandedH;
    setlistRatio = 0.36f;
    setlistMinW = 320;
    setlistMaxW = 520;
    contentInset = 9;
  }

  topBar->setBounds(area.removeFromTop(topBarH));
  const int waveH = isWaveformExpanded ? waveformExpandedH : waveformH;
  auto waveformArea = area.removeFromTop(waveH);
  waveformBar->setBounds(waveformArea);

  auto mainArea = area;
  constexpr int handleW = 28;
  int setlistWidth =
      juce::jlimit(setlistMinW, setlistMaxW,
                   static_cast<int>(mainArea.getWidth() * setlistRatio));
  if (isSetlistCollapsed)
    setlistWidth = 0;

  auto leftSetlistArea = mainArea.removeFromLeft(setlistWidth);
  auto handleArea = mainArea.removeFromLeft(handleW);
  auto rightChannelsArea = mainArea.reduced(contentInset);

  if (padsHandlePanel) {
    constexpr int kHandleH = 22;
    constexpr int kPadsH   = 280;
    constexpr int kPadsW   = 584; // 484 (grid) + 90 (strip) + 4 (margin) + 6 (padding)

    const int totalPadsH = isPadsCollapsed ? kHandleH : kHandleH + kPadsH;
    auto padsBar = rightChannelsArea.removeFromBottom(totalPadsH);

    padsHandlePanel->setBounds(padsBar.removeFromTop(kHandleH));

    if (!isPadsCollapsed && padsComponent) {
      const int padsW = juce::jmin(kPadsW, padsBar.getWidth());
      padsComponent->setBounds(padsBar.withWidth(padsW));
    }
  }

  channelGridPanel->setBounds(rightChannelsArea);

  if (isSetlistCollapsed) {
    songListPanel->setVisible(false);
    songListPanel->setBounds({});
  } else {
    songListPanel->setVisible(true);
    songListPanel->setBounds(leftSetlistArea.reduced(contentInset));
  }

  setlistHandlePanel->setBounds(handleArea.reduced(2, 8));
  setlistHandlePanel->setCollapsed(isSetlistCollapsed);

  if (reauthPromptModal != nullptr) {
    reauthPromptModal->setBounds(
        juce::Rectangle<int>(560, 230).withCentre(getLocalBounds().getCentre()));
    reauthPromptModal->toFront(false);
  }

  if (songLoadOverlay != nullptr) {
    songLoadOverlay->setBounds(getLocalBounds());
    if (songLoadOverlay->isVisible())
      songLoadOverlay->toFront(false);
  }
}

bool MTControlsAudioProcessorEditor::keyPressed(const juce::KeyPress &key) {
  if (!mainUiInitialized)
    return true;

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);

  if (key == juce::KeyPress::spaceKey) {
    processorRef.toggleSongPlayPause();
    return true;
  }

  if (key == juce::KeyPress::leftKey) {
    if (prevSongAction)
      prevSongAction();
    return true;
  }

  if (key == juce::KeyPress::rightKey) {
    if (nextSongAction)
      nextSongAction();
    return true;
  }

  if (key == juce::KeyPress::escapeKey) {
    processorRef.stopSong();
    return true;
  }

  return false;
}
