#pragma once

#include "../../data/AppSettings.h"
#include "../../data/Localization.h"
#include "../../integrations/FreeShowClient.h"
#include "../UiCommon.h"
#include "BinaryData.h"
#include "../../data/IntegrationTokenStore.h"

// Connection controls live in Settings. Construction and navigation never
// configure the client, write preferences, connect or send a projection command.
class FreeShowConnectionPanel final : public juce::Component, private juce::Timer {
  class ProviderCard final : public juce::Button {
  public:
    ProviderCard(const juce::String& name, const void* data, int bytes) : juce::Button(name), logo(juce::ImageCache::getFromMemory(data, bytes)) {
      setClickingTogglesState(false); setTitle(name); setTooltip(name + juce::String::fromUTF8(" · Aplicación externa"));
    }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
      auto r = getLocalBounds().toFloat().reduced(1);
      const bool selected = getToggleState();
      g.setColour(selected ? UITheme::cardBg.interpolatedWith(UITheme::accent, 0.10f) : UITheme::cardBg.brighter(over ? 0.08f : 0.02f));
      g.fillRoundedRectangle(r, 12);
      g.setColour(selected ? UITheme::accent : UITheme::textSecondary.withAlpha(0.2f));
      g.drawRoundedRectangle(r, 12, selected ? 1.5f : 1.0f);
      auto content = getLocalBounds().reduced(15, 12);
      auto icon = content.removeFromLeft(44); content.removeFromLeft(13);
      g.setOpacity(1.0f);
      g.drawImageWithin(logo, icon.getX(), icon.getY(), icon.getWidth(), icon.getHeight(), juce::RectanglePlacement::centred);
      g.setColour(UITheme::textPrimary); g.setFont(makeAppFont(16, AppFontWeight::SemiBold));
      g.drawText(getName(), content.removeFromTop(24), juce::Justification::centredLeft);
      g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(11));
      g.drawText(L10n::tr(juce::String::fromUTF8("Proyección de letras"), "Lyrics projection"), content.removeFromTop(19), juce::Justification::centredLeft);
      if (selected) {
        auto dot = r.removeFromRight(18).removeFromTop(18).getCentre();
        g.setColour(UITheme::accent); g.fillEllipse(dot.x-3, dot.y-3, 6, 6);
      }
      juce::ignoreUnused(down);
    }
  private:
    juce::Image logo;
  };
public:
  FreeShowConnectionPanel() {
    for (auto *label : {&heading, &description, &connectionHeading, &hostLabel, &portLabel, &tokenLabel, &help, &status}) {
      addAndMakeVisible(*label); label->setFont(makeAppFont(13.0f));
    }
    heading.setText(L10n::tr("Integraciones externas", "External integrations"), juce::dontSendNotification);
    description.setText(L10n::tr(juce::String::fromUTF8("Sincroniza las letras con una aplicación de proyección en este equipo o en tu red local."), "Sync lyrics with a projection app on this computer or your local network."), juce::dontSendNotification);
    description.setJustificationType(juce::Justification::topLeft);
    description.setMinimumHorizontalScale(1.0f);
    connectionHeading.setFont(makeAppFont(14, AppFontWeight::SemiBold));
    addAndMakeVisible(freeShowCard); addAndMakeVisible(holyricsCard);
    freeShowCard.onClick = [this] { providerCombo.setSelectedId(1, juce::sendNotificationSync); };
    holyricsCard.onClick = [this] { providerCombo.setSelectedId(2, juce::sendNotificationSync); };
    heading.setFont(makeAppFont(20.0f, AppFontWeight::SemiBold));
    addAndMakeVisible(rememberToken);
    rememberToken.setButtonText(L10n::tr("Recordar token en este equipo", "Remember token on this computer"));
    rememberToken.setEnabled(IntegrationTokenStore::available());
    addChildComponent(providerCombo);
    providerCombo.addItem("FreeShow", 1); providerCombo.addItem("Holyrics", 2);
    providerCombo.setSelectedId(1, juce::dontSendNotification);
    providerCombo.onChange = [this] {
      const auto provider = providerCombo.getSelectedId() == 2 ? "holyrics" : "freeshow";
      const auto endpoint = providerCombo.getSelectedId() == 2 ? AppSettings::getHolyricsEndpoint() : AppSettings::getFreeShowEndpoint();
      hostEditor.setText(endpoint.host, false); portEditor.setText(juce::String(endpoint.port), false);
      const auto savedToken = IntegrationTokenStore::load(provider, endpoint.host, endpoint.port);
      tokenEditor.setText(savedToken, false);
      rememberedEndpoint = savedToken.isNotEmpty() ? IntegrationTokenStore::account(provider, endpoint.host, endpoint.port) : juce::String{};
      rememberToken.setToggleState(savedToken.isNotEmpty(), juce::dontSendNotification);
      tokenLabel.setText(providerCombo.getSelectedId() == 2 ? juce::String::fromUTF8("Token de sesión") : juce::String::fromUTF8("Token de sesión (opcional)"), juce::dontSendNotification);
      if (client && client->getSettings().provider == provider) tokenEditor.setText(client->getSettings().authToken, false);
      updateProviderCards();
    };
    hostLabel.setText(L10n::tr("IP local / LAN", "Local / LAN IP"), juce::dontSendNotification);
    portLabel.setText(L10n::tr("Puerto REST", "REST port"), juce::dontSendNotification);
    tokenLabel.setText(L10n::tr(juce::String::fromUTF8("Token de sesión (opcional)"), "Session token (optional)"), juce::dontSendNotification);
    help.setText(L10n::tr(juce::String::fromUTF8("El token se guarda en el llavero de macOS si activas Recordar. De lo contrario, solo se usa en esta sesión."),
        "On macOS, Remember stores the token in Keychain. Otherwise it is used for this session only."), juce::dontSendNotification);
    help.setJustificationType(juce::Justification::topLeft);
    help.setMinimumHorizontalScale(1.0f);
    for (auto *editor : {&hostEditor, &portEditor, &tokenEditor}) {
      addAndMakeVisible(*editor);
      editor->setMultiLine(false);
      editor->setReturnKeyStartsNewLine(false);
    }
    hostEditor.setTitle(juce::String::fromUTF8("IP de la aplicación externa")); portEditor.setTitle(juce::String::fromUTF8("Puerto de la aplicación externa"));
    tokenEditor.setTitle(juce::String::fromUTF8("Token de la aplicación externa")); tokenEditor.setPasswordCharacter(0x2022);
    portEditor.setInputRestrictions(5, "0123456789");
    applyButton.setButtonText(L10n::tr("Guardar y probar", "Save and test"));
    addAndMakeVisible(applyButton); applyButton.setEnabled(false);
    applyButton.onClick = [this] { apply(); };
    updateProviderCards(); refreshThemeColours();
  }
  ~FreeShowConnectionPanel() override { stopTimer(); }
  void setClient(playworship::lyrics::FreeShowClient *next) {
    client = next;
    validationMessage.clear();
    const auto current = client != nullptr ? client->getSettings() : playworship::lyrics::ConnectionSettings{};
    providerCombo.setSelectedId(current.provider == "holyrics" ? 2 : 1, juce::dontSendNotification);
    hostEditor.setText(current.host, false); portEditor.setText(juce::String(current.port), false);
    tokenEditor.setText(current.authToken, false);
    const auto savedToken = IntegrationTokenStore::load(current.provider, current.host, current.port);
    rememberedEndpoint = savedToken.isNotEmpty() ? IntegrationTokenStore::account(current.provider, current.host, current.port) : juce::String{};
    rememberToken.setToggleState(savedToken.isNotEmpty(), juce::dontSendNotification);
    tokenLabel.setText(current.provider == "holyrics" ? juce::String::fromUTF8("Token de sesión") : juce::String::fromUTF8("Token de sesión (opcional)"), juce::dontSendNotification);
    updateProviderCards(); applyButton.setEnabled(client != nullptr); timerCallback(); startTimerHz(2);
  }
  void refreshThemeColours() {
    applyComboBoxTheme(providerCombo);
    for (auto *label : {&heading, &hostLabel, &portLabel, &tokenLabel}) label->setColour(juce::Label::textColourId, UITheme::textPrimary);
    for (auto *label : {&description, &help, &status}) label->setColour(juce::Label::textColourId, UITheme::textSecondary);
    for (auto *editor : {&hostEditor, &portEditor, &tokenEditor}) applyTextEditorTheme(*editor);
    applyButton.setColour(juce::TextButton::buttonColourId, UITheme::accent.withAlpha(0.16f));
    applyButton.setColour(juce::TextButton::textColourOffId, UITheme::accent);
    repaint();
  }
  void paint(juce::Graphics &g) override {
    g.setColour(UITheme::cardBg); g.fillRoundedRectangle(card.toFloat(), 14.0f);
    g.setColour(UITheme::textSecondary.withAlpha(0.13f));
    g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 14.0f, 1.0f);
  }
  void resized() override {
    auto r = getLocalBounds().reduced(20, 12);
    r = r.withSizeKeepingCentre(juce::jmin(720, r.getWidth()), r.getHeight());
    heading.setBounds(r.removeFromTop(30)); r.removeFromTop(3);
    description.setBounds(r.removeFromTop(38)); r.removeFromTop(10);
    auto choices = r.removeFromTop(84);
    auto first = choices.removeFromLeft((choices.getWidth()-12)/2);
    choices.removeFromLeft(12); freeShowCard.setBounds(first); holyricsCard.setBounds(choices);
    r.removeFromTop(16); card = r.withHeight(juce::jmin(330, r.getHeight()));
    auto body = card.reduced(18, 12);
    connectionHeading.setBounds(body.removeFromTop(24)); body.removeFromTop(8);
    auto row = body.removeFromTop(64);
    auto port = row.removeFromRight(110); row.removeFromRight(14);
    hostLabel.setBounds(row.removeFromTop(22)); hostEditor.setBounds(row.removeFromTop(36));
    portLabel.setBounds(port.removeFromTop(22)); portEditor.setBounds(port.removeFromTop(36));
    body.removeFromTop(6); tokenLabel.setBounds(body.removeFromTop(22)); tokenEditor.setBounds(body.removeFromTop(36));
    body.removeFromTop(4); rememberToken.setBounds(body.removeFromTop(26));
    body.removeFromTop(4); help.setBounds(body.removeFromTop(34)); body.removeFromTop(4);
    auto actions = body.removeFromTop(36);
    applyButton.setBounds(actions.removeFromLeft(170)); actions.removeFromLeft(14); status.setBounds(actions);
  }

private:
  friend struct FreeShowConnectionPanelTestAccess;
  void updateProviderCards() {
    const bool holyrics = providerCombo.getSelectedId() == 2;
    freeShowCard.setToggleState(!holyrics, juce::dontSendNotification);
    holyricsCard.setToggleState(holyrics, juce::dontSendNotification);
    connectionHeading.setText((holyrics ? juce::String("Holyrics") : juce::String("FreeShow"))
        + L10n::tr(juce::String::fromUTF8(" · Conexión local / LAN"), juce::String::fromUTF8(" · Local / LAN connection")), juce::dontSendNotification);
    freeShowCard.repaint(); holyricsCard.repaint();
  }
  void apply() {
    if (client == nullptr) return;
    validationMessage.clear();
    playworship::lyrics::ConnectionSettings next{hostEditor.getText().trim(), portEditor.getText().getIntValue(), tokenEditor.getText()};
    next.provider = providerCombo.getSelectedId() == 2 ? "holyrics" : "freeshow";
    juce::String error;
    if (!playworship::lyrics::FreeShowClient::validateSettings(next, &error)) {
      validationMessage = error; timerCallback(); return;
    }
    if (!(next.provider == "holyrics" ? AppSettings::setHolyricsEndpoint({next.host, next.port}) : AppSettings::setFreeShowEndpoint({next.host, next.port}))
        || !AppSettings::setLyricsProvider(next.provider)) {
      validationMessage = L10n::tr(juce::String::fromUTF8("No se pudo guardar la conexión en este equipo."), "Could not save the connection on this computer.");
      timerCallback(); return;
    }
    if (rememberToken.getToggleState()) {
      if (!IntegrationTokenStore::save(next.provider, next.host, next.port, next.authToken)) {
        validationMessage = L10n::tr(juce::String::fromUTF8("No se pudo guardar el token en el llavero. La conexión no se ha cambiado."), "Could not save token in Keychain. Connection unchanged.");
        timerCallback(); return;
      }
      rememberedEndpoint = IntegrationTokenStore::account(next.provider, next.host, next.port);
    } else if (rememberedEndpoint == IntegrationTokenStore::account(next.provider, next.host, next.port)) {
      if (!IntegrationTokenStore::forget(next.provider, next.host, next.port)) {
        validationMessage = "No se pudo quitar el token del llavero"; timerCallback(); return;
      }
      rememberedEndpoint.clear();
    }
    client->configure(std::move(next));
    const juce::Component::SafePointer<FreeShowConnectionPanel> safe(this);
    client->testConnection([safe](bool, juce::String) {
      juce::MessageManager::callAsync([safe] { if (safe) safe->timerCallback(); });
    });
    timerCallback();
  }
  void timerCallback() override {
    if (client != nullptr) {
      const auto selected = providerCombo.getSelectedId() == 2 ? "holyrics" : "freeshow";
      const auto state = client->getStatus();
      const bool active = client->getSettings().provider == selected;
      status.setText(validationMessage.isNotEmpty() ? validationMessage : active ? state.message
          : L10n::tr("Pendiente de conectar", "Ready to connect"), juce::dontSendNotification);
      status.setColour(juce::Label::textColourId, active && validationMessage.isEmpty()
          && state.state == playworship::lyrics::ConnectionStatus::State::connected ? UITheme::accent : UITheme::textSecondary);
    }
  }
  playworship::lyrics::FreeShowClient *client = nullptr;
  juce::Label heading, description, connectionHeading, hostLabel, portLabel, tokenLabel, help, status;
  ProviderCard freeShowCard{"FreeShow", BinaryData::freeshowlogo_png, BinaryData::freeshowlogo_pngSize};
  ProviderCard holyricsCard{"Holyrics", BinaryData::holyricslogo_png, BinaryData::holyricslogo_pngSize};
  juce::TextEditor hostEditor, portEditor, tokenEditor;
  AdaptiveComboBox providerCombo;
  juce::TextButton applyButton;
  juce::ToggleButton rememberToken;
  juce::String rememberedEndpoint;
  juce::Rectangle<int> card;
  juce::String validationMessage;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeShowConnectionPanel)
};
