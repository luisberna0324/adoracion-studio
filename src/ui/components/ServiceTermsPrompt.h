#pragma once
#include <JuceHeader.h>
#include "../../data/Localization.h"
#include <functional>

// One explicit button, no preselected checkbox or implicit closing acceptance.
class ServiceTermsLinks final : public juce::Component {
public:
  juce::HyperlinkButton terms{L10n::tr("Leer términos", "Read terms"), juce::URL("https://playworship.app/legal/terminos.html")};
  juce::HyperlinkButton privacy{L10n::tr("Leer privacidad", "Read privacy"), juce::URL("https://playworship.app/legal/privacidad.html")};
  juce::HyperlinkButton copyright{L10n::tr("Leer copyright y reclamaciones", "Read copyright and claims"), juce::URL("https://playworship.app/legal/copyright.html")};
  ServiceTermsLinks() { for (auto* c : {&terms, &privacy, &copyright}) addAndMakeVisible(c); setSize(280, 108); }
  void resized() override { auto r = getLocalBounds(); for (auto* c : {&terms, &privacy, &copyright}) c->setBounds(r.removeFromTop(36)); }
};
namespace ServiceTermsPrompt {
inline juce::String explanation() {
  return L10n::tr("Tu contenido sigue siendo tuyo. Cloud personal almacena y sincroniza los archivos que eliges entre tus dispositivos. Debes tener los permisos necesarios; PlayWorship no concede licencias musicales. Las reclamaciones se atienden según la política de copyright. Puedes continuar con uso local sin aceptar ni subir archivos. Si no puedes leer los documentos, elige Ahora no y léelos al reconectar.",
    "Your content remains yours. Personal Cloud stores and syncs files you choose between your devices. You need the necessary permissions; PlayWorship does not grant music licenses. Claims follow the copyright policy. You can continue locally without accepting or uploading files. If you cannot read the documents, choose Not now and read them after reconnecting.");
}
inline bool promptOpen = false;
inline juce::Component::SafePointer<juce::AlertWindow> show(const juce::String& accountEmail, std::function<void(bool)> done, bool preview = false) {
  if (promptOpen) { if (done) done(false); return {}; }
  promptOpen = true;
  auto links = std::make_shared<ServiceTermsLinks>();
  auto* alert = new juce::AlertWindow(L10n::tr("Condiciones de PlayWorship", "PlayWorship terms"),
    (preview ? L10n::tr("Vista previa local para revisar esta interfaz. No registra aceptación de cuenta Cloud. Los enlaces muestran los documentos públicos actuales.\n\n", "Local preview to review this interface. It does not record Cloud account acceptance. Links show the current public documents.\n\n") : juce::String{}) + explanation() + (accountEmail.isNotEmpty() ? "\n\n" + L10n::tr("Cuenta: ", "Account: ") + accountEmail : ""), juce::MessageBoxIconType::NoIcon);
  alert->addCustomComponent(links.get());
  alert->addButton(L10n::tr("Acepto los términos", "I accept the terms"), 1);
  alert->addButton(L10n::tr("Ahora no · usar local", "Not now · use locally"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
  alert->enterModalState(true, juce::ModalCallbackFunction::create([links, done = std::move(done)](int result) { promptOpen = false; if (done) done(result == 1); }), true);
  return alert;
}
}
