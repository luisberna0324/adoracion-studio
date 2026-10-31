#pragma once

#include "../../data/AppSettings.h"
#include "../../data/Localization.h"
#include "../../integrations/FreeShowClient.h"
#include "../UiCommon.h"

// Connection controls live in Settings. Construction and navigation never
// configure the client, write preferences, connect or send a projection command.
class FreeShowConnectionPanel final : public juce::Component, private juce::Timer {
public:
  FreeShowConnectionPanel() {
    for (auto *label : {&heading, &hostLabel, &portLabel, &tokenLabel, &help, &status}) {
      addAndMakeVisible(*label); label->setFont(makeAppFont(13.0f));
    }
    heading.setText("FreeShow", juce::dontSendNotification);
    heading.setFont(makeAppFont(20.0f, AppFontWeight::SemiBold));
    hostLabel.setText(L10n::tr("IP local / LAN", "Local / LAN IP"), juce::dontSendNotification);
    portLabel.setText(L10n::tr("Puerto REST", "REST port"), juce::dontSendNotification);
    tokenLabel.setText(L10n::tr(juce::String::fromUTF8("Token de sesión (opcional)"), "Session token (optional)"), juce::dontSendNotification);
    help.setText(L10n::tr(juce::String::fromUTF8("La IP y el puerto se guardan en este equipo. El token permanece solo en esta sesión."),
        "The IP and port are saved on this computer. The token stays in this session only."), juce::dontSendNotification);
    help.setJustificationType(juce::Justification::topLeft);
    help.setMinimumHorizontalScale(1.0f);
    for (auto *editor : {&hostEditor, &portEditor, &tokenEditor}) {
      addAndMakeVisible(*editor);
      editor->setMultiLine(false);
      editor->setReturnKeyStartsNewLine(false);
    }
    hostEditor.setTitle("IP FreeShow"); portEditor.setTitle("Puerto REST FreeShow");
    tokenEditor.setTitle("Token FreeShow"); tokenEditor.setPasswordCharacter(0x2022);
    portEditor.setInputRestrictions(5, "0123456789");
    applyButton.setButtonText(L10n::tr("Guardar y probar", "Save and test"));
    addAndMakeVisible(applyButton); applyButton.setEnabled(false);
    applyButton.onClick = [this] { apply(); };
    refreshThemeColours();
  }
  ~FreeShowConnectionPanel() override { stopTimer(); }
  void setClient(playworship::lyrics::FreeShowClient *next) {
    client = next;
    validationMessage.clear();
    const auto current = client != nullptr ? client->getSettings() : playworship::lyrics::ConnectionSettings{};
    hostEditor.setText(current.host, false); portEditor.setText(juce::String(current.port), false);
    tokenEditor.setText(current.authToken, false);
    applyButton.setEnabled(client != nullptr); timerCallback(); startTimerHz(2);
  }
  void refreshThemeColours() {
    for (auto *label : {&heading, &hostLabel, &portLabel, &tokenLabel}) label->setColour(juce::Label::textColourId, UITheme::textPrimary);
    for (auto *label : {&help, &status}) label->setColour(juce::Label::textColourId, UITheme::textSecondary);
    for (auto *editor : {&hostEditor, &portEditor, &tokenEditor}) applyTextEditorTheme(*editor);
    applyButton.setColour(juce::TextButton::buttonColourId, UITheme::accent.withAlpha(0.16f));
    applyButton.setColour(juce::TextButton::textColourOffId, UITheme::accent);
    repaint();
  }
  void paint(juce::Graphics &g) override {
    g.setColour(UITheme::cardBg); g.fillRoundedRectangle(card.toFloat(), 16.0f);
  }
  void resized() override {
    card = getLocalBounds().reduced(16).withHeight(juce::jmax(0, juce::jmin(448, getHeight() - 32)));
    card = card.withSizeKeepingCentre(juce::jmin(640, card.getWidth()), card.getHeight());
    auto r = card.reduced(22); heading.setBounds(r.removeFromTop(32)); r.removeFromTop(14);
    hostLabel.setBounds(r.removeFromTop(22)); hostEditor.setBounds(r.removeFromTop(36)); r.removeFromTop(12);
    portLabel.setBounds(r.removeFromTop(22)); portEditor.setBounds(r.removeFromTop(36).withWidth(100)); r.removeFromTop(12);
    tokenLabel.setBounds(r.removeFromTop(22)); tokenEditor.setBounds(r.removeFromTop(36)); r.removeFromTop(14);
    help.setBounds(r.removeFromTop(44)); r.removeFromTop(8);
    applyButton.setBounds(r.removeFromTop(36).withWidth(170)); r.removeFromTop(8);
    status.setBounds(r.removeFromTop(28));
  }
private:
  friend struct FreeShowConnectionPanelTestAccess;
  void apply() {
    if (client == nullptr) return;
    validationMessage.clear();
    playworship::lyrics::ConnectionSettings next{hostEditor.getText().trim(), portEditor.getText().getIntValue(), tokenEditor.getText()};
    juce::String error;
    if (!playworship::lyrics::FreeShowClient::validateSettings(next, &error)) {
      validationMessage = error; timerCallback(); return;
    }
    if (!AppSettings::setFreeShowEndpoint({next.host, next.port})) {
      validationMessage = L10n::tr(juce::String::fromUTF8("No se pudo guardar la conexión en este equipo."), "Could not save the connection on this computer.");
      timerCallback(); return;
    }
    client->configure(std::move(next));
    const juce::Component::SafePointer<FreeShowConnectionPanel> safe(this);
    client->testConnection([safe](bool, juce::String) {
      juce::MessageManager::callAsync([safe] { if (safe) safe->timerCallback(); });
    });
    timerCallback();
  }
  void timerCallback() override {
    if (client != nullptr) status.setText(validationMessage.isNotEmpty() ? validationMessage : client->getStatus().message, juce::dontSendNotification);
  }
  playworship::lyrics::FreeShowClient *client = nullptr;
  juce::Label heading, hostLabel, portLabel, tokenLabel, help, status;
  juce::TextEditor hostEditor, portEditor, tokenEditor;
  juce::TextButton applyButton;
  juce::Rectangle<int> card;
  juce::String validationMessage;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeShowConnectionPanel)
};
