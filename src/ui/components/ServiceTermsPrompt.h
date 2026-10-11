#pragma once
#include "AuthenticationStyle.h"
#include "../../data/Localization.h"
#include <functional>

class ServiceTermsPanel final : public juce::Component, private juce::ComponentListener {
public:
  explicit ServiceTermsPanel(juce::String accountEmail = {}, bool informationOnly = false)
    : information(informationOnly) {
    setWantsKeyboardFocus(true);
    AuthUi::label(title, 20, true); AuthUi::label(body, 15);
    title.setText(information ? L10n::tr("Tu contenido es tuyo", "Your content is yours")
                             : L10n::tr("Condiciones de Cloud", "Cloud terms"), juce::dontSendNotification);
    title.setJustificationType(juce::Justification::topLeft);
    body.setJustificationType(juce::Justification::topLeft);
    auto copy = L10n::tr("Tus archivos siguen siendo tuyos. Cloud personal guarda y sincroniza tus proyectos entre tus dispositivos.\n\nUsa material con los permisos necesarios. PlayWorship no concede licencias musicales. Las reclamaciones siguen la política de copyright.",
      "Your files remain yours. Personal Cloud stores and syncs your projects between your devices.\n\nUse material with the necessary permissions. PlayWorship does not grant music licenses. Claims follow the copyright policy.");
    copy += information ? L10n::tr("\n\nPuedes seguir en local. La aceptación para Cloud se pide con tu cuenta.", "\n\nYou can continue locally. Cloud acceptance is requested with your account.")
                        : L10n::tr("\n\nAceptar registra estos términos para tu cuenta. Ahora no conserva el uso local.", "\n\nAccepting records these terms for your account. Not now keeps local use available.");
    if (accountEmail.isNotEmpty()) copy += "\n\n" + L10n::tr("Cuenta: ", "Account: ") + accountEmail;
    body.setText(copy, juce::dontSendNotification);
    addAndMakeVisible(title); content.addAndMakeVisible(body);
    viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    terms.setButtonText(L10n::tr("Términos", "Terms")); privacy.setButtonText(L10n::tr("Privacidad", "Privacy"));
    copyright.setButtonText(L10n::tr("Copyright y reclamaciones", "Copyright and claims"));
    terms.onClick = [] { juce::URL("https://playworship.app/legal/terminos.html").launchInDefaultBrowser(); };
    privacy.onClick = [] { juce::URL("https://playworship.app/legal/privacidad.html").launchInDefaultBrowser(); };
    copyright.onClick = [] { juce::URL("https://playworship.app/legal/copyright.html").launchInDefaultBrowser(); };
    for (auto* button : {&terms, &privacy, &copyright}) content.addAndMakeVisible(button);
    primary.setComponentID("terms-primary"); secondary.setComponentID("terms-secondary");
    primary.setButtonText(information ? L10n::tr("Entendido", "Got it") : L10n::tr("Aceptar y continuar", "Accept and continue"));
    secondary.setButtonText(information ? L10n::tr("Cerrar", "Close") : L10n::tr("Ahora no", "Not now"));
    primary.onClick = [this] { exitModalState(1); };
    secondary.onClick = [this] { exitModalState(0); };
    addAndMakeVisible(primary); addAndMakeVisible(secondary);
  }
  ~ServiceTermsPanel() override { if (host) host->removeComponentListener(this); if (isCurrentlyModal()) exitModalState(0); }
  void attach(juce::Component& source) { host = &source; source.addComponentListener(this); source.addAndMakeVisible(*this); setBounds(source.getLocalBounds()); toFront(false); }
  void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black.withAlpha(0.58f)); AuthUi::drawSurface(g, card.toFloat()); }
  void resized() override {
    auto available = getLocalBounds();
   #if JUCE_IOS
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
      available = display->safeAreaInsets.subtractedFrom(available);
   #endif
    const bool compact = available.getHeight() < 300;
    available = available.reduced(compact ? 8 : 12);
    card = available.withSizeKeepingCentre(juce::jmin(520, available.getWidth()), juce::jmin(620, available.getHeight()));
    auto inner = card.reduced(compact ? 12 : 16);
    title.setBounds(inner.removeFromTop(compact ? 28 : 32)); inner.removeFromTop(compact ? 6 : 10);
    const int actionHeight = compact ? 44 : 48;
    auto footer = inner.removeFromBottom(actionHeight * 2 + 8);
    primary.setBounds(footer.removeFromTop(actionHeight)); footer.removeFromTop(8); secondary.setBounds(footer.removeFromTop(actionHeight));
    inner.removeFromBottom(compact ? 6 : 12); viewport.setBounds(inner);
    const int width = juce::jmax(1, inner.getWidth() - viewport.getScrollBarThickness() - 2);
    const int textHeight = body.heightForWidth(width);
    content.setSize(width, textHeight + 12 + 3 * 44);
    body.setBounds(0, 0, width, textHeight);
    int y = textHeight + 12;
    for (auto* button : {&terms, &privacy, &copyright}) { button->setBounds(0, y, width, 44); y += 44; }
  }
  bool keyPressed(const juce::KeyPress& key) override { if (key == juce::KeyPress::escapeKey) { exitModalState(0); return true; } return false; }
  void inputAttemptWhenModal() override { secondary.grabKeyboardFocus(); }
  juce::Rectangle<int> cardBounds() const { return card; }
  juce::Viewport& bodyViewport() { return viewport; }
  AuthUi::Button primary{AuthUi::Role::primary}, secondary{AuthUi::Role::secondary};
private:
  void componentMovedOrResized(juce::Component&, bool, bool) override { if (host) setBounds(host->getLocalBounds()); }
  juce::Component::SafePointer<juce::Component> host;
  bool information = false;
  juce::Rectangle<int> card;
  juce::Label title;
  AuthUi::Paragraph body;
  juce::Component content;
  juce::Viewport viewport;
  AuthUi::Button terms{AuthUi::Role::link}, privacy{AuthUi::Role::link}, copyright{AuthUi::Role::link};
};
namespace ServiceTermsPrompt {
inline bool promptOpen = false;
inline juce::Component::SafePointer<ServiceTermsPanel> show(const juce::String& email, std::function<void(bool)> done, bool informationOnly = false) {
  if (promptOpen) { if (done) done(false); return {}; }
  juce::Component* host = nullptr;
  auto& desktop = juce::Desktop::getInstance();
  for (int i = desktop.getNumComponents() - 1; i >= 0; --i)
    if (desktop.getComponent(i)->isShowing()) { host = desktop.getComponent(i); break; }
  if (!host) { if (done) done(false); return {}; }
  auto* panel = new ServiceTermsPanel(email, informationOnly); panel->attach(*host);
  promptOpen = true;
  panel->enterModalState(true, juce::ModalCallbackFunction::create([done = std::move(done)](int result) { promptOpen = false; if (done) done(result == 1); }), true);
  panel->secondary.grabKeyboardFocus();
  return panel;
}
}
