#include "SectionDetectionPanel.h"
#include "PracticeTimelineRuler.h"
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
    std::shared_ptr<IMusicStructureAnalyzer> provider)
    : song(std::move(s)), actions(std::move(a)), service(std::move(provider)) {
  analysisCacheDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MTControls/cache/section-detection");
  setComponentID("section-detection-review");
  for (auto *component : std::initializer_list<juce::Component *>{&title,&status,&help,&gridConfirmed,&analyze,&cancel,&add,&acceptSelected,&selectAll,&viewport,&spinner})
    addAndMakeVisible(component);
  title.setText(juce::String::fromUTF8("Detectar secciones · ") + song.title, juce::dontSendNotification);
  title.setFont(juce::Font(19.0f, juce::Font::bold));
  help.setText(juce::String::fromUTF8("Confirma los roles. Solo se analiza la mezcla musical local. La guía se excluye de la mezcla y aún no se transcribe."), juce::dontSendNotification);
  help.setFont(juce::Font(13.0f));
  gridConfirmed.setButtonText(juce::String::fromUTF8("Confirmo que el mapa de tempo y sus compases están sincronizados"));
  gridConfirmed.setToggleState(false, juce::dontSendNotification);
  gridConfirmed.setEnabled(song.bpm > 0 && song.timeSignature.isNotEmpty());
  analyze.setButtonText("Analizar"); cancel.setButtonText("Cerrar"); add.setButtonText(juce::String::fromUTF8("Añadir"));
  acceptSelected.setButtonText("Confirmar seleccionadas"); selectAll.setButtonText("Seleccionar todas");
  viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(true, false);
  for (const auto &stem : song.stems) { auto row = std::make_unique<RoleRow>(stem); content.addAndMakeVisible(*row); roles.push_back(std::move(row)); }
  analyze.onClick = [this] { start(); };
  cancel.onClick = [this] {
    if (busy && task) { task->cancel(); status.setText(juce::String::fromUTF8("Cancelando…"), juce::dontSendNotification); cancel.setEnabled(false); }
    else if (actions.close) actions.close();
  };
  add.onClick = [this] { addProposal(); };
  acceptSelected.onClick = [this] { accept(); };
  selectAll.onClick = [this] { std::fill(selected.begin(), selected.end(), true); rebuildRows(); updatePreview(); };
  status.setText(service.available() ? juce::String::fromUTF8("Listo para analizar · inicio y fin se muestran en segundos") : service.unavailableReason(), juce::dontSendNotification);
  setBusy(false);
}
SectionDetectionPanel::~SectionDetectionPanel() { if (task) task->cancel(); }
void SectionDetectionPanel::paint(juce::Graphics &g) {
  g.fillAll(UITheme::panelDark);
  g.setColour(juce::Colours::white.withAlpha(0.2f)); g.drawRect(getLocalBounds());
}
void SectionDetectionPanel::resized() {
  auto b = getLocalBounds().reduced(14);
  auto heading = b.removeFromTop(30); cancel.setBounds(heading.removeFromRight(100)); title.setBounds(heading);
  help.setBounds(b.removeFromTop(40));
  gridConfirmed.setBounds(b.removeFromTop(30));
  auto state = b.removeFromTop(30); spinner.setBounds(state.removeFromRight(90).reduced(4)); status.setBounds(state);
  auto footer = b.removeFromBottom(38);
  acceptSelected.setBounds(footer.removeFromRight(210).reduced(2)); analyze.setBounds(footer.removeFromRight(140).reduced(2));
  add.setBounds(footer.removeFromLeft(85).reduced(2)); selectAll.setBounds(footer.removeFromLeft(165).reduced(2));
  viewport.setBounds(b);
  const auto count = reviewing ? rows.size() : roles.size();
  const int height = reviewing ? 80 : 40;
  content.setSize(std::max(200, b.getWidth() - 16), std::max(b.getHeight(), int(count) * height));
  int y = 0;
  if (reviewing) for (auto &row : rows) { row->setBounds(0, y, content.getWidth(), height); y += height; }
  else for (auto &row : roles) { row->setBounds(0, y, content.getWidth(), height); y += height; }
}
void SectionDetectionPanel::setBusy(bool value) {
  busy = value;
  spinner.setVisible(value);
  analyze.setEnabled(!busy && service.available());
  analyze.setButtonText(reviewing ? "Analizar de nuevo" : "Analizar");
  cancel.setButtonText(busy ? "Cancelar" : "Descartar / cerrar"); cancel.setEnabled(true);
  add.setEnabled(!busy && reviewing); acceptSelected.setEnabled(!busy && reviewing); selectAll.setEnabled(!busy && reviewing);
  gridConfirmed.setEnabled(!busy && !reviewing && song.bpm > 0 && song.timeSignature.isNotEmpty());
  for (auto &role : roles) role->setEnabled(!busy);
  content.setEnabled(!busy);
}
std::vector<double> SectionDetectionPanel::confirmedBars() const {
  std::vector<double> bars;
  if (!gridConfirmed.getToggleState()) return bars;
  const auto length = result.durationSeconds > 0 ? result.durationSeconds : estimateSongLengthSeconds(song);
  for (const auto &mark : PracticeTimelineRuler::build({0, length}, float(std::min(20000000.0, length * 500)), song.bpm, song.timeSignature, song.tempoMap))
    if (mark.strong) bars.push_back(mark.timeSeconds);
  return bars;
}
void SectionDetectionPanel::start() {
  if (busy) return;
  // A fresh analysis discards only untouched transient proposals. Edited ones
  // remain reviewable until the user explicitly closes this review.
  if (reviewing && (reviewModified || std::any_of(result.suggestions.begin(), result.suggestions.end(), [](const auto &s) { return s.userEdited; }))) {
    status.setText(juce::String::fromUTF8("Hay correcciones en la revisión. Confírmalas o descarta esta revisión antes de analizar de nuevo."), juce::dontSendNotification); return;
  }
  SectionDetectionRequest request;
  request.songId = song.id; request.fusion.confirmedBars = confirmedBars();
  request.cacheDirectory = analysisCacheDirectory;
  for (size_t i = 0; i < song.stems.size(); ++i) {
    const auto &stem = song.stems[i]; juce::File file(stem.storedPath);
    if (!file.existsAsFile()) file = juce::File(stem.sourcePath);
    request.stems.push_back({file, AnalysisStemRole(roles[i]->role.getSelectedId() - 1), 0.0});
  }
  setBusy(true);
  auto safe = juce::Component::SafePointer<SectionDetectionPanel>(this);
  task = service.start(std::move(request), [safe](DetectionStage stage) {
    juce::MessageManager::callAsync([safe, stage] { if (safe) safe->status.setText(detectionStageName(stage), juce::dontSendNotification); });
  }, [safe](DetectionOutcome outcome) {
    juce::MessageManager::callAsync([safe, outcome = std::move(outcome)]() mutable {
      if (!safe) return;
      safe->setBusy(false);
      if (outcome.cancelled || outcome.error.isNotEmpty()) {
        safe->status.setText(outcome.cancelled ? "Cancelado" : outcome.error, juce::dontSendNotification); return;
      }
      safe->result = std::move(outcome.result); safe->reviewing = true;
      safe->selected.assign(safe->result.suggestions.size(), true);
      for (auto &role : safe->roles) role->setVisible(false);
      safe->status.setText(juce::String::fromUTF8("Revisión · confianza heurística propia · inicio / fin en segundos") + juce::String(outcome.cacheHit ? juce::String::fromUTF8(" · caché local") : ""), juce::dontSendNotification);
      safe->rebuildRows(); safe->setBusy(false); safe->updatePreview();
    });
  });
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
