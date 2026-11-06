#include "SectionDetectionPanel.h"
#include "PracticeTimelineRuler.h"
#include "../components/AppModal.h"
#include "../../data/Localization.h"
#include <cmath>

using namespace playworship::analysis;

struct SectionDetectionPanel::RoleRow final : juce::Component {
  juce::Label name;
  juce::ComboBox role;
  explicit RoleRow(const ImportedStemData &stem) {
    name.setText(stem.displayName.isNotEmpty() ? stem.displayName : stem.fileName, juce::dontSendNotification);
    addAndMakeVisible(name); addAndMakeVisible(role);
    role.addItem(juce::String::fromUTF8("Música"), 1); role.addItem(juce::String::fromUTF8("Guía"), 2); role.addItem("Click", 3); role.addItem("Excluir", 4);
    // Metadata only. Filename guesses are deliberately not used for this choice.
    const auto category = stem.category.trim().toLowerCase();
    int id = 1;
    if (!stem.include) id = 4;
    else if (category == "cues" || category == "guide" || category == "guia" || category == juce::String::fromUTF8("guía")) id = 2;
    else if (category == "click" || category == "clic") id = 3;
    role.setSelectedId(id, juce::dontSendNotification);
    role.setTooltip("Confirma el rol de la pista; no se modifica el proyecto.");
  }
  void resized() override { auto b = getLocalBounds().reduced(4); role.setBounds(b.removeFromRight(160)); name.setBounds(b); }
};

struct SectionDetectionPanel::SuggestionRow final : juce::Component {
  juce::ToggleButton chosen;
  juce::TextEditor name, startTime, endTime;
  juce::Label evidence;
  juce::TextButton listen{"Escuchar"}, snap{"Compases"}, remove{"Eliminar"};
  void resized() override {
    auto b = getLocalBounds().reduced(4);
    auto top = b.removeFromTop(30);
    chosen.setBounds(top.removeFromLeft(28)); remove.setBounds(top.removeFromRight(72));
    snap.setBounds(top.removeFromRight(86)); listen.setBounds(top.removeFromRight(76));
    endTime.setBounds(top.removeFromRight(80).reduced(2, 0));
    startTime.setBounds(top.removeFromRight(80).reduced(2, 0)); name.setBounds(top.reduced(2, 0));
    evidence.setBounds(b);
  }
};

SectionDetectionPanel::SectionDetectionPanel(ImportedSongData s, Actions a,
    std::shared_ptr<IMusicStructureAnalyzer> provider,
    std::shared_ptr<IGuideTranscriber> guideProvider)
    : song(std::move(s)), actions(std::move(a)), service(std::move(provider), std::move(guideProvider)),
      guideModelFile(defaultWhisperGuideModelFile()) {
  analysisCacheDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MTControls/cache/section-detection");
  setComponentID("section-detection-review");
  setWantsKeyboardFocus(true);
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
  setInterceptsMouseClicks(true, true);
  for (auto *component : std::initializer_list<juce::Component *>{&title,&status,&help,&gridConfirmed,&analyze,&cancel,&add,&acceptSelected,&selectAll,&viewport,&spinner,&transcribe,&chooseModel,&guideLanguage,&modelStatus,&showTracks})
    addAndMakeVisible(component);
  addChildComponent(transcript);
  title.setText(actions.reviewGuide ? L10n::tr("Detectar secciones", "Detect sections")
                                   : juce::String::fromUTF8("Detectar secciones · ") + song.title,
                juce::dontSendNotification);
  title.setFont(makeAppFont(20.0f, AppFontWeight::SemiBold));
  help.setText(juce::String::fromUTF8(actions.reviewGuide
      ? "Elige la pista Guía. Al terminar, revisa las sugerencias sobre la onda y arrastra sus marcas antes de confirmar."
      : "Elige una pista como Guía para transcribir su voz localmente. Los tiempos corresponden a los anuncios; todavía no son límites de sección."), juce::dontSendNotification);
  help.setFont(makeAppFont(13.0f));
  status.setFont(makeAppFont(13.5f));
  status.setJustificationType(juce::Justification::topLeft);
  for (auto *label : {&title, &status, &help, &modelStatus}) {
    label->setColour(juce::Label::textColourId,
        label == &title ? UITheme::textPrimary : UITheme::textSecondary);
    label->setMinimumHorizontalScale(1.0f);
    label->setInterceptsMouseClicks(false, false);
  }
  spinner.setComponentID("section-detection-progress");
  spinner.setColour(juce::ProgressBar::backgroundColourId, UITheme::panelDark);
  spinner.setColour(juce::ProgressBar::foregroundColourId, UITheme::accent);
  spinner.setPercentageDisplay(true);
  gridConfirmed.setButtonText(juce::String::fromUTF8("Confirmo que el mapa de tempo y sus compases están sincronizados"));
  gridConfirmed.setToggleState(false, juce::dontSendNotification);
  gridConfirmed.setEnabled(song.bpm > 0 && song.timeSignature.isNotEmpty());
  analyze.setButtonText("Analizar"); cancel.setButtonText("Cerrar"); add.setButtonText(juce::String::fromUTF8("Añadir"));
  acceptSelected.setButtonText("Confirmar seleccionadas"); selectAll.setButtonText("Seleccionar todas");
  for (auto *legacy : std::initializer_list<juce::Component *>{&gridConfirmed, &analyze, &add, &acceptSelected, &selectAll})
    legacy->setVisible(!actions.reviewGuide);
  transcribe.setButtonText(actions.reviewGuide ? L10n::tr("Detectar secciones", "Detect sections")
                                             : juce::String::fromUTF8("Transcribir guía"));
  transcribe.setComponentID("section-guide-transcribe");
  chooseModel.setButtonText(juce::String::fromUTF8("Modelo de voz…"));
  chooseModel.setComponentID("section-guide-model");
  guideLanguage.addItem(juce::String::fromUTF8("Español e inglés"), 3);
  guideLanguage.addItem(juce::String::fromUTF8("Español"), 1);
  guideLanguage.addItem(juce::String::fromUTF8("Inglés"), 2);
  guideLanguage.setSelectedId(3, juce::dontSendNotification);
  guideLanguage.setComponentID("section-guide-language");
  guideLanguage.setTooltip(juce::String::fromUTF8(
      "Español e inglés compara ambos idiomas en cada anuncio de la guía, localmente."));
  modelStatus.setFont(makeAppFont(12.0f));
  transcript.setMultiLine(true); transcript.setReadOnly(true);
  transcript.setScrollbarsShown(true); transcript.setCaretVisible(false);
  transcript.setComponentID("section-guide-transcript");
  showTracks.setButtonText("Ver pistas"); showTracks.setVisible(false);
  showTracks.onClick = [this] {
    transcriptReview = false; transcript.setVisible(false); viewport.setVisible(true);
    for (auto &role : roles) role->setVisible(true);
    setBusy(false); resized();
  };
  chooseModel.onClick = [this] { chooseGuideModel(); };
  transcribe.onClick = [this] { start(true); };
  viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(true, false);
  for (const auto &stem : song.stems) { auto row = std::make_unique<RoleRow>(stem); content.addAndMakeVisible(*row); roles.push_back(std::move(row)); }
  analyze.onClick = [this] { start(); };
  cancel.onClick = [this] { requestCancel(); };
  for (auto *button : {&transcribe, &chooseModel, &cancel, &showTracks}) {
    button->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
    button->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  }
  transcribe.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  transcribe.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  applyComboBoxTheme(guideLanguage, UITheme::cardBgHover,
                    UITheme::cardBorder.withAlpha(0.75f));
  add.onClick = [this] { addProposal(); };
  acceptSelected.onClick = [this] { accept(); };
  selectAll.onClick = [this] { std::fill(selected.begin(), selected.end(), true); rebuildRows(); updatePreview(); };
  status.setText(service.guideAvailable()
      ? (guideModelFile.existsAsFile()
          ? juce::String::fromUTF8(actions.reviewGuide ? "Preparando sugerencias · comprueba la pista Guía" : "Listo para transcribir · selecciona una pista Guía")
          : juce::String::fromUTF8("Descarga una vez el modelo de voz (57 MiB) para continuar"))
      : service.available() ? juce::String::fromUTF8("Listo para analizar · inicio y fin se muestran en segundos") : service.unavailableReason(), juce::dontSendNotification);
  setBusy(false);
}
SectionDetectionPanel::~SectionDetectionPanel() {
  ++operationGeneration;
  releaseModalPresentation();
  if (task) task->cancel();
  if (modelTask) modelTask->cancel();
}
void SectionDetectionPanel::showModal(juce::Component &source) {
  auto *host = source.getTopLevelComponent();
  if (host == nullptr) host = &source;
  if (modalHost.getComponent() != host) {
    if (modalHost) modalHost->removeComponentListener(this);
    modalHost = host;
    modalHost->addComponentListener(this);
  }
  modalPresented = true;
  host->addAndMakeVisible(*this);
  setBounds(host->getLocalBounds());
  setBusy(busy);
  toFront(false);
  if (!isCurrentlyModal(false)) enterModalState(true);
  startTimerHz(10);
  if (busy) cancel.grabKeyboardFocus();
  else if (transcribe.isEnabled()) transcribe.grabKeyboardFocus();
  else chooseModel.grabKeyboardFocus();
}
void SectionDetectionPanel::releaseModalPresentation() {
  stopTimer();
  if (modalHost) modalHost->removeComponentListener(this);
  modalHost = nullptr;
  if (isCurrentlyModal(false)) exitModalState(0);
  if (modalPresented) setVisible(false);
  modalPresented = false;
}
void SectionDetectionPanel::requestCancel() {
  if (busy) {
    if (cancelling) return;
    cancelling = true;
    if (task) task->cancel();
    if (modelTask) modelTask->cancel();
    status.setText(juce::String::fromUTF8("Cancelando…"), juce::dontSendNotification);
    cancel.setEnabled(false);
    return;
  }
  const auto close = actions.close;
  releaseModalPresentation();
  if (close) close();
}
bool SectionDetectionPanel::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) {
    requestCancel(); return true;
  }
  return modalPresented;
}
void SectionDetectionPanel::inputAttemptWhenModal() {}
juce::Rectangle<int> SectionDetectionPanel::modalAvailableBounds() const {
  auto available = getLocalBounds();
  if (auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds())) {
    auto insets = display->safeAreaInsets;
    insets.setBottom(juce::jmax(insets.getBottom(), display->keyboardInsets.getBottom()));
    available = insets.subtractedFrom(available);
  }
  return available.reduced(juce::jmin(16, juce::jmax(0, available.getWidth() / 4)),
                           juce::jmin(16, juce::jmax(0, available.getHeight() / 4)));
}
void SectionDetectionPanel::componentMovedOrResized(juce::Component &component, bool, bool) {
  if (modalHost.getComponent() != &component) return;
  setBounds(component.getLocalBounds());
  resized(); repaint();
}
void SectionDetectionPanel::componentBeingDeleted(juce::Component &component) {
  if (modalHost.getComponent() != &component) return;
  releaseModalPresentation();
}
void SectionDetectionPanel::timerCallback() {
  if (modalPresented && modalAvailableBounds() != lastAvailableBounds) {
    resized(); repaint();
  }
}
void SectionDetectionPanel::beginSuggestionFlow() {
  if (busy) return;
  if (!service.guideAvailable()) {
    status.setText(service.guideUnavailableReason(), juce::dontSendNotification); return;
  }
  const auto guideCount = std::count_if(roles.begin(), roles.end(), [](const auto &row) {
    return row->role.getSelectedId() == 2;
  });
  if (guideCount != 1) {
    status.setText(L10n::tr("Elige una sola pista como Guía para detectar las secciones.",
                           "Choose one Guide track to detect sections."), juce::dontSendNotification); return;
  }
  if (!guideModelFile.existsAsFile()) {
    modelDownloadPrompt = bool(actions.reviewGuide);
    setBusy(false);
    status.setText(L10n::tr("¿Descargar el modelo para detectar las secciones?\nSolo se necesita una vez · 57 MiB.",
                           "Download the model to detect sections?\nOnly needed once · 57 MiB."), juce::dontSendNotification);
    return;
  }
  start(true);
}
void SectionDetectionPanel::paint(juce::Graphics &g) {
  if (modalPresented) {
    AppModal::paintSurface(g, cardBounds);
    return;
  }
  g.fillAll(UITheme::panelDark);
  g.setColour(juce::Colours::white.withAlpha(0.2f)); g.drawRect(getLocalBounds());
}
void SectionDetectionPanel::resized() {
  const bool compact = modalPresented && (busy || modelDownloadPrompt);
  for (auto *component : std::initializer_list<juce::Component *>{&help, &chooseModel,
       &guideLanguage, &transcribe, &modelStatus}) component->setVisible(!compact);
  if (compact && !busy) transcribe.setVisible(true);
  for (auto *legacy : std::initializer_list<juce::Component *>{&gridConfirmed, &analyze,
       &add, &acceptSelected, &selectAll}) legacy->setVisible(!compact && !actions.reviewGuide);
  spinner.setVisible(busy);
  viewport.setVisible(!compact && !transcriptReview);
  transcript.setVisible(!compact && transcriptReview);
  showTracks.setVisible(!compact && transcriptReview);
  for (auto &role : roles) role->setVisible(!compact && !reviewing && !transcriptReview);
  for (auto &row : rows) row->setVisible(!compact && reviewing);
  if (modalPresented) {
    const auto available = modalAvailableBounds();
    lastAvailableBounds = available;
    cardBounds = juce::Rectangle<int>(juce::jmax(0, juce::jmin(compact ? 440 : 740, available.getWidth())),
                                      juce::jmax(0, juce::jmin(compact ? 240 : 640, available.getHeight())))
                     .withCentre(available.getCentre());
    auto inner = cardBounds.reduced(juce::jmin(22, cardBounds.getWidth() / 10),
                                    juce::jmin(20, cardBounds.getHeight() / 10));
    auto footer = inner.removeFromBottom(juce::jmin(48, juce::jmax(0, inner.getHeight() / (compact ? 3 : 5))));
    if (compact) {
      if (busy) cancel.setBounds(footer.removeFromRight(juce::jmin(140, footer.getWidth())));
      else {
        transcribe.setBounds(footer.removeFromRight(juce::jmin(190, footer.getWidth() / 2)));
        footer.removeFromRight(10);
        cancel.setBounds(footer.removeFromRight(juce::jmin(140, footer.getWidth())));
      }
      title.setBounds(inner.removeFromTop(juce::jmin(30, inner.getHeight())));
      inner.removeFromTop(juce::jmin(8, inner.getHeight()));
      status.setBounds(inner.removeFromTop(juce::jmin(busy ? 48 : 84, inner.getHeight())));
      inner.removeFromTop(juce::jmin(14, inner.getHeight()));
      spinner.setBounds(inner.removeFromTop(juce::jmin(22, inner.getHeight())));
      return;
    }
    cancel.setBounds(footer.removeFromLeft(juce::jmin(130, footer.getWidth() / 3)));
    footer.removeFromLeft(juce::jmin(10, footer.getWidth()));
    transcribe.setBounds(footer.removeFromRight(juce::jmin(240, footer.getWidth())));
    if (transcriptReview) showTracks.setBounds(footer.reduced(3, 0));
    inner.removeFromBottom(juce::jmin(10, inner.getHeight()));
    title.setBounds(inner.removeFromTop(juce::jmin(32, inner.getHeight())));
    inner.removeFromTop(juce::jmin(8, inner.getHeight()));
    help.setBounds(inner.removeFromTop(juce::jmin(48, inner.getHeight())));
    inner.removeFromTop(juce::jmin(8, inner.getHeight()));
    auto voice = inner.removeFromTop(juce::jmin(40, inner.getHeight()));
    guideLanguage.setBounds(voice.removeFromRight(juce::jmin(170, voice.getWidth() / 2)).reduced(2, 0));
    chooseModel.setBounds(voice.removeFromLeft(juce::jmin(200, voice.getWidth())).reduced(2, 0));
    modelStatus.setBounds(inner.removeFromTop(juce::jmin(24, inner.getHeight())));
    if (!actions.reviewGuide) gridConfirmed.setBounds(inner.removeFromTop(juce::jmin(30, inner.getHeight())));
    status.setBounds(inner.removeFromTop(juce::jmin(48, inner.getHeight())));
    if (!actions.reviewGuide) {
      auto legacy = inner.removeFromBottom(juce::jmin(38, inner.getHeight()));
      acceptSelected.setBounds(legacy.removeFromRight(juce::jmin(210, legacy.getWidth() / 3)).reduced(2));
      analyze.setBounds(legacy.removeFromRight(juce::jmin(130, legacy.getWidth() / 2)).reduced(2));
      add.setBounds(legacy.removeFromLeft(juce::jmin(85, legacy.getWidth() / 3)).reduced(2));
      selectAll.setBounds(legacy.reduced(2));
    }
    viewport.setBounds(inner);
    transcript.setBounds(inner);
    const auto count = reviewing ? rows.size() : roles.size();
    const int height = reviewing ? 80 : 40;
    content.setSize(std::max(1, inner.getWidth() - 16), std::max(inner.getHeight(), int(count) * height));
    int y = 0;
    if (reviewing) for (auto &row : rows) { row->setBounds(0, y, content.getWidth(), height); y += height; }
    else for (auto &row : roles) { row->setBounds(0, y, content.getWidth(), height); y += height; }
    return;
  }
  auto b = getLocalBounds().reduced(14);
  auto heading = b.removeFromTop(30); cancel.setBounds(heading.removeFromRight(100)); title.setBounds(heading);
  help.setBounds(b.removeFromTop(40));
  auto voice = b.removeFromTop(30);
  chooseModel.setBounds(voice.removeFromLeft(145).reduced(2));
  guideLanguage.setBounds(voice.removeFromLeft(155).reduced(2));
  transcribe.setBounds(voice.removeFromRight(155).reduced(2));
  showTracks.setBounds(voice.removeFromRight(transcriptReview ? 95 : 0).reduced(2));
  modelStatus.setBounds(voice);
  if (!actions.reviewGuide) gridConfirmed.setBounds(b.removeFromTop(30));
  auto state = b.removeFromTop(30); spinner.setBounds(state.removeFromRight(90).reduced(4)); status.setBounds(state);
  if (!actions.reviewGuide) {
    auto footer = b.removeFromBottom(38);
    acceptSelected.setBounds(footer.removeFromRight(210).reduced(2)); analyze.setBounds(footer.removeFromRight(140).reduced(2));
    add.setBounds(footer.removeFromLeft(85).reduced(2)); selectAll.setBounds(footer.removeFromLeft(165).reduced(2));
  }
  viewport.setBounds(b);
  transcript.setBounds(b);
  const auto count = reviewing ? rows.size() : roles.size();
  const int height = reviewing ? 80 : 40;
  content.setSize(std::max(200, b.getWidth() - 16), std::max(b.getHeight(), int(count) * height));
  int y = 0;
  if (reviewing) for (auto &row : rows) { row->setBounds(0, y, content.getWidth(), height); y += height; }
  else for (auto &row : roles) { row->setBounds(0, y, content.getWidth(), height); y += height; }
}
void SectionDetectionPanel::setBusy(bool value) {
  if (!value || !busy) cancelling = false;
  busy = value;
  spinner.setVisible(value);
  const bool modelPresent = guideModelFile.existsAsFile();
  transcribe.setEnabled(!busy && service.guideAvailable());
  transcribe.setButtonText(actions.reviewGuide
      ? !modelPresent ? L10n::tr("Descargar modelo", "Download model")
                      : L10n::tr("Detectar secciones", "Detect sections")
      : juce::String::fromUTF8(!modelPresent ? "Descargar y continuar" : "Transcribir guía"));
  chooseModel.setEnabled(!busy); guideLanguage.setEnabled(!busy);
  showTracks.setVisible(transcriptReview); showTracks.setEnabled(!busy);
  modelStatus.setText(modelPresent ? "Modelo local listo" : juce::String::fromUTF8("Descarga única · 57 MiB"), juce::dontSendNotification);
  modelStatus.setTooltip(guideModelFile.getFullPathName());
  transcribe.setTooltip(service.guideAvailable()
      ? juce::String::fromUTF8(!modelPresent ? "Descargar una vez el modelo de voz y continuar. Tu audio permanece en este equipo." :
          actions.reviewGuide ? "Preparar sugerencias locales para revisar sobre la onda"
          : "Transcribir voz sin enviar el audio a Internet")
      : service.guideUnavailableReason());
  analyze.setEnabled(!busy && service.available());
  analyze.setTooltip(service.available() ? "Analizar estructura musical" : service.unavailableReason());
  analyze.setButtonText(reviewing ? "Analizar de nuevo" : "Analizar");
  cancel.setButtonText(!busy && modelDownloadPrompt ? L10n::tr("Ahora no", "Not now")
      : busy ? L10n::tr("Cancelar", "Cancel") : modalPresented ? "Cerrar" : "Descartar / cerrar");
  cancel.setEnabled(!cancelling);
  add.setEnabled(!busy && reviewing); acceptSelected.setEnabled(!busy && reviewing); selectAll.setEnabled(!busy && reviewing);
  gridConfirmed.setEnabled(!busy && !reviewing && !transcriptReview && song.bpm > 0 && song.timeSignature.isNotEmpty());
  for (auto &role : roles) role->setEnabled(!busy);
  content.setEnabled(!busy);
  resized();
  repaint();
}
void SectionDetectionPanel::chooseGuideModel() {
  if (busy) return;
  modelChooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("Elegir modelo de voz multilingüe"), guideModelFile, "*.bin");
  modelChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
      [safe = juce::Component::SafePointer<SectionDetectionPanel>(this)](const juce::FileChooser &chooser) {
        if (!safe) return;
        const auto chosen = chooser.getResult();
        if (chosen.existsAsFile()) { safe->guideModelFile = chosen; safe->setBusy(false); }
      });
}
void SectionDetectionPanel::prepareGuideModel() {
  if (busy) return;
  const auto generation = ++operationGeneration;
  progress = 0; setBusy(true);
  status.setText(L10n::tr("Descargando modelo…", "Downloading model…"), juce::dontSendNotification);
  auto safe = juce::Component::SafePointer<SectionDetectionPanel>(this);
  modelTask = prepareWhisperGuideModel(guideModelFile, [safe, generation](double value) {
    juce::MessageManager::callAsync([safe, generation, value] {
      if (safe && safe->operationGeneration == generation && safe->busy && !safe->cancelling) {
        safe->progress = juce::jlimit(0.0, 1.0, value);
        safe->spinner.repaint();
      }
    });
  }, [safe, generation](GuideModelPreparationResult outcome) {
    juce::MessageManager::callAsync([safe, generation, outcome = std::move(outcome)]() mutable {
      if (!safe || safe->operationGeneration != generation) return;
      const bool cancelled = safe->cancelling || outcome.cancelled ||
                             (safe->modelTask && safe->modelTask->isCancelled());
      safe->modelTask.reset();
      if (cancelled || outcome.error.isNotEmpty()) {
        ++safe->operationGeneration;
        safe->modelDownloadPrompt = safe->actions.reviewGuide && !safe->guideModelFile.existsAsFile();
        safe->setBusy(false);
        auto message = cancelled ? juce::String("Cancelado") : outcome.error;
        if (safe->actions.reviewGuide)
          message = cancelled ? L10n::tr("Descarga cancelada.", "Download cancelled.")
                              : L10n::tr("No se pudo descargar el modelo. Inténtalo de nuevo.",
                                         "Couldn't download the model. Please try again.");
        safe->status.setText(message, juce::dontSendNotification);
        safe->status.setTooltip(outcome.error);
        return;
      }
      safe->guideModelFile = outcome.modelFile;
      safe->modelDownloadPrompt = false;
      safe->status.setTooltip({});
      // Keep the compact modal throughout download -> local transcription.
      // No editable setup state is exposed between the two workers.
      safe->start(true, true);
    });
  }, modelDownloadSource, modelAsset);
}
void SectionDetectionPanel::showTranscript(const GuideTranscript &value) {
  const auto timeLabel = [](double seconds) {
    const auto milliseconds = juce::int64(std::llround(seconds * 1000));
    return juce::String(milliseconds / 60000).paddedLeft('0', 2) + ":" +
        juce::String((milliseconds / 1000) % 60).paddedLeft('0', 2) + "." +
        juce::String(milliseconds % 1000).paddedLeft('0', 3);
  };
  juce::String text;
  for (const auto &segment : value.segments)
    text << "[" << timeLabel(segment.startSeconds) << " - " << timeLabel(segment.endSeconds) << "]  " << segment.text << "\n\n";
  if (text.isEmpty()) text = juce::String::fromUTF8("No se reconocieron anuncios de voz. Comprueba la pista y el idioma elegidos.");
  transcript.setText(text, false);
  transcript.setVisible(true); viewport.setVisible(false);
  transcriptReview = true; reviewing = false; selected.clear(); rows.clear();
  if (actions.preview) actions.preview({});
  status.setText(juce::String::fromUTF8("Transcripción local · ") + juce::String(int(value.segments.size())) +
      juce::String::fromUTF8(" fragmentos · tiempos de voz, sin aplicar secciones"), juce::dontSendNotification);
  setBusy(false); resized();
}
std::vector<double> SectionDetectionPanel::confirmedBars() const {
  std::vector<double> bars;
  if (!gridConfirmed.getToggleState()) return bars;
  const auto length = result.durationSeconds > 0 ? result.durationSeconds : estimateSongLengthSeconds(song);
  for (const auto &mark : PracticeTimelineRuler::build({0, length}, float(std::min(20000000.0, length * 500)), song.bpm, song.timeSignature, song.tempoMap))
    if (mark.strong) bars.push_back(mark.timeSeconds);
  return bars;
}
void SectionDetectionPanel::start(bool guideOnly, bool continuePreparation) {
  if (busy && !continuePreparation) return;
  // A fresh analysis discards only untouched transient proposals. Edited ones
  // remain reviewable until the user explicitly closes this review.
  if (reviewing && (reviewModified || std::any_of(result.suggestions.begin(), result.suggestions.end(), [](const auto &s) { return s.userEdited; }))) {
    if (continuePreparation) setBusy(false);
    status.setText(juce::String::fromUTF8("Hay correcciones en la revisión. Confírmalas o descarta esta revisión antes de analizar de nuevo."), juce::dontSendNotification); return;
  }
  if (guideOnly && !guideModelFile.existsAsFile()) {
    const auto guideCount = std::count_if(roles.begin(), roles.end(), [](const auto &row) { return row->role.getSelectedId() == 2; });
    if (guideCount != 1) {
      status.setText(juce::String::fromUTF8("Elige una sola pista como Guía antes de descargar."), juce::dontSendNotification); return;
    }
    prepareGuideModel(); return;
  }
  SectionDetectionRequest request;
  request.songId = song.id; request.fusion.confirmedBars = confirmedBars();
  request.cacheDirectory = analysisCacheDirectory;
  request.transcribeGuideOnly = guideOnly;
  request.guideModelFile = guideModelFile;
  request.guideLanguage = guideLanguage.getSelectedId() == 1 ? "es" :
      guideLanguage.getSelectedId() == 2 ? "en" : "es-en";
  for (size_t i = 0; i < song.stems.size(); ++i) {
    const auto &stem = song.stems[i]; juce::File file(stem.storedPath);
    if (!file.existsAsFile()) file = juce::File(stem.sourcePath);
    request.stems.push_back({file, AnalysisStemRole(roles[i]->role.getSelectedId() - 1), 0.0});
  }
  const auto generation = ++operationGeneration;
  progress = -1; setBusy(true);
  status.setText(detectionStageName(DetectionStage::preparingAudio), juce::dontSendNotification);
  auto safe = juce::Component::SafePointer<SectionDetectionPanel>(this);
  task = service.start(std::move(request), [safe, generation](DetectionStage stage) {
    juce::MessageManager::callAsync([safe, generation, stage] {
      if (safe && safe->operationGeneration == generation && safe->busy && !safe->cancelling)
        safe->status.setText(detectionStageName(stage), juce::dontSendNotification);
    });
  }, [safe, generation](DetectionOutcome outcome) {
    juce::MessageManager::callAsync([safe, generation, outcome = std::move(outcome)]() mutable {
      if (safe && safe->operationGeneration == generation) safe->completeDetection(std::move(outcome));
    });
  }, [safe, generation](double value) {
    juce::MessageManager::callAsync([safe, generation, value] {
      if (safe && safe->operationGeneration == generation && safe->busy && !safe->cancelling) {
        safe->progress = juce::jlimit(0.0, 1.0, value);
        safe->spinner.repaint();
      }
    });
  });
}
void SectionDetectionPanel::completeDetection(DetectionOutcome outcome) {
  // The worker may have queued success just before the user presses Cancel.
  // Honour the current UI cancellation as well as the worker's final snapshot.
  const auto completedTask = std::move(task);
  const bool cancelled = cancelling || outcome.cancelled || (completedTask && completedTask->isCancelled());
  ++operationGeneration;
  setBusy(false);
  if (cancelled || outcome.error.isNotEmpty()) {
    status.setText(cancelled ? "Cancelado" : outcome.error, juce::dontSendNotification); return;
  }
  if (outcome.guideTranscript) {
    result = std::move(outcome.result);
    if (actions.reviewGuide) {
      reviewing = false; transcriptReview = false;
      transcript.setVisible(false); selected.clear(); rows.clear();
      setBusy(false);
      status.setText(juce::String::fromUTF8("Revisa y arrastra las sugerencias sobre la onda antes de confirmar."), juce::dontSendNotification);
      const auto reviewGuide = actions.reviewGuide;
      const auto close = actions.close;
      const auto safe = juce::Component::SafePointer<SectionDetectionPanel>(this);
      // The owner may destroy this panel while installing the waveform review.
      // Capture everything first and never access members after the handoff.
      releaseModalPresentation();
      reviewGuide(*outcome.guideTranscript);
      if (close) juce::MessageManager::callAsync([safe, close, completedTask] {
        if (safe && (!completedTask || !completedTask->isCancelled())) close();
      });
      return;
    }
    showTranscript(*outcome.guideTranscript);
    return;
  }
  transcriptReview = false; transcript.setVisible(false); viewport.setVisible(true);
  result = std::move(outcome.result); reviewing = true;
  selected.assign(result.suggestions.size(), true);
  for (auto &role : roles) role->setVisible(false);
  status.setText(juce::String::fromUTF8("Revisión · confianza heurística propia · inicio / fin en segundos") + juce::String(outcome.cacheHit ? juce::String::fromUTF8(" · caché local") : ""), juce::dontSendNotification);
  rebuildRows(); setBusy(false); updatePreview();
}
void SectionDetectionPanel::rebuildRows() {
  rows.clear();
  auto safe = juce::Component::SafePointer<SectionDetectionPanel>(this);
  for (size_t i = 0; i < result.suggestions.size(); ++i) {
    auto row = std::make_unique<SuggestionRow>(); auto &s = result.suggestions[i];
    for (auto *c : std::initializer_list<juce::Component *>{&row->chosen,&row->name,&row->startTime,&row->endTime,&row->evidence,&row->listen,&row->snap,&row->remove}) row->addAndMakeVisible(c);
    row->chosen.setToggleState(selected[i], juce::dontSendNotification);
    row->chosen.onClick = [safe,i,p=row.get()] { if (safe) { safe->selected[i] = p->chosen.getToggleState(); safe->reviewModified = true; safe->updatePreview(); } };
    row->name.setText(s.displayName, false); row->startTime.setText(juce::String(s.startSeconds, 6), false); row->endTime.setText(juce::String(s.endSeconds, 6), false);
    row->startTime.setTooltip("Inicio en segundos"); row->endTime.setTooltip("Fin en segundos");
    row->name.onTextChange = [safe,i,p=row.get()] { if (safe) { safe->result.suggestions[i].displayName = p->name.getText(); safe->result.suggestions[i].userEdited = true; safe->updatePreview(); } };
    for (bool start : {true,false}) {
      auto *editor = start ? &row->startTime : &row->endTime;
      editor->setInputRestrictions(16, "0123456789.");
      editor->onReturnKey = [safe,i,start,editor] { if (safe) safe->changeBoundary(i,start,editor->getText().getDoubleValue()); };
      editor->onFocusLost = editor->onReturnKey;
    }
    row->evidence.setFont(juce::Font(12.0f));
    row->evidence.setText(s.sources.joinIntoString(" + ") + juce::String::fromUTF8(" · ") + confidenceName(s.confidenceLevel) + juce::String::fromUTF8(" · ") + s.reason, juce::dontSendNotification);
    row->evidence.setTooltip(s.reason + juce::String::fromUTF8(" · límite original ") + juce::String(s.rawStartSeconds, 6) + juce::String::fromUTF8(" s · ajuste ") + juce::String(s.quantizationDistanceSeconds, 6) + " s");
    row->listen.onClick = [safe,i] { if (safe && safe->actions.listen) safe->actions.listen(std::max(0.0, safe->result.suggestions[i].startSeconds - 2.0)); };
    row->snap.setEnabled(!result.structure.tempo.bars.empty() || gridConfirmed.getToggleState());
    row->snap.onClick = [safe,i] {
      if (!safe) return;
      auto bars = safe->confirmedBars(); if (bars.empty()) bars = safe->result.structure.tempo.bars;
      if (bars.empty()) return;
      const auto &s = safe->result.suggestions[i];
      const auto nearest = [&](double t) { return *std::min_element(bars.begin(), bars.end(), [t](double a,double b) { return std::abs(a-t)<std::abs(b-t); }); };
      const double end = nearest(s.endSeconds), begin = nearest(s.startSeconds);
      safe->changeBoundary(i,true,begin); safe->changeBoundary(i,false,end);
    };
    row->remove.onClick = [safe,i] { juce::MessageManager::callAsync([safe,i] { if (!safe) return; safe->reviewModified = true; safe->result.suggestions.erase(safe->result.suggestions.begin()+int(i)); safe->selected.erase(safe->selected.begin()+int(i)); safe->rebuildRows(); safe->updatePreview(); }); };
    content.addAndMakeVisible(*row); rows.push_back(std::move(row));
  }
  resized();
}
void SectionDetectionPanel::changeBoundary(size_t i, bool start, double value) {
  if (!std::isfinite(value) || i >= result.suggestions.size()) return;
  auto &s = result.suggestions[i];
  if (std::abs(value - (start ? s.startSeconds : s.endSeconds)) <= 0.00000051) return;
  const double low = start ? (i ? result.suggestions[i-1].startSeconds + 0.25 : 0) : s.startSeconds + 0.25;
  const double high = start ? s.endSeconds - 0.25 : (i+1 < result.suggestions.size() ? result.suggestions[i+1].endSeconds - 0.25 : result.durationSeconds);
  if (value < low || value > high) { status.setText(juce::String::fromUTF8("El límite debe conservar secciones positivas dentro de la canción."), juce::dontSendNotification); return; }
  if (start) { s.startSeconds = value; if (i) { result.suggestions[i-1].endSeconds = value; result.suggestions[i-1].userEdited = true; } }
  else { s.endSeconds = value; if (i+1 < result.suggestions.size()) { result.suggestions[i+1].startSeconds = value; result.suggestions[i+1].userEdited = true; } }
  s.userEdited = true;
  reviewModified = true;
  auto bars=confirmedBars(); if(bars.empty()) bars=result.structure.tempo.bars;
  const auto indexOf=[&](double time)->std::optional<int> {
    for(size_t n=0;n<bars.size();++n) if(std::abs(bars[n]-time)<0.0000001) return int(n);
    return {};
  };
  for(auto &proposal:result.suggestions) {
    proposal.startBarIndex=indexOf(proposal.startSeconds); proposal.endBarIndex=indexOf(proposal.endSeconds);
    proposal.quantizationDistanceSeconds=proposal.startSeconds-proposal.rawStartSeconds;
    if(proposal.matchedCueTimeSeconds) proposal.guideAnticipationSeconds=proposal.startSeconds-*proposal.matchedCueTimeSeconds;
  }
  updatePreview();
  // Defer destruction of the TextEditor that delivered focus/Return callbacks.
  juce::MessageManager::callAsync([safe=juce::Component::SafePointer<SectionDetectionPanel>(this)] { if (safe) safe->rebuildRows(); });
}
void SectionDetectionPanel::updatePreview() {
  std::vector<SongArrangementPartSnapshot> preview;
  for (size_t i=0;i<result.suggestions.size();++i) if (selected[i]) {
    const auto &s=result.suggestions[i]; SongArrangementPartSnapshot p;
    p.id=s.id; p.name=s.displayName; p.startSec=s.startSeconds; p.endSec=s.endSeconds; preview.push_back(std::move(p));
  }
  if(actions.preview) actions.preview(std::move(preview));
}
void SectionDetectionPanel::addProposal() {
  if (!result.suggestions.empty() && result.suggestions.back().endSeconds-result.suggestions.back().startSeconds < 0.5) {
    status.setText(juce::String::fromUTF8("La sección debe tener al menos 0,5 s para dividirla en dos propuestas."), juce::dontSendNotification); return;
  }
  reviewModified = true;
  if (result.suggestions.empty()) {
    SectionSuggestion s; s.id=juce::Uuid().toString(); s.displayName=juce::String::fromUTF8("Sección sugerida"); s.endSeconds=result.durationSeconds;
    s.type=SectionType::custom; s.userEdited=true; s.sources.add(juce::String::fromUTF8("edición manual")); result.suggestions.push_back(s); selected.push_back(true);
  } else {
    auto last=result.suggestions.back(); const auto midpoint=(last.startSeconds+last.endSeconds)*0.5;
    result.suggestions.back().endSeconds=midpoint; result.suggestions.back().userEdited=true;
    last.id=juce::Uuid().toString(); last.startSeconds=midpoint; last.displayName="Personalizada"; last.type=SectionType::custom; last.userEdited=true; last.sources={juce::String::fromUTF8("edición manual")};
    result.suggestions.push_back(last); selected.push_back(true);
  }
  rebuildRows(); updatePreview();
}
void SectionDetectionPanel::accept() {
  std::vector<SectionSuggestion> chosen;
  for(size_t i=0;i<result.suggestions.size();++i) if(selected[i]) chosen.push_back(result.suggestions[i]);
  juce::String error;
  if(chosen.empty()) error="Selecciona al menos una propuesta.";
  else if(actions.accept && actions.accept(chosen,result.durationSeconds,error)) { if(actions.close) actions.close(); return; }
  status.setText(error.isEmpty()?"No se pudieron confirmar las propuestas.":error,juce::dontSendNotification);
}
