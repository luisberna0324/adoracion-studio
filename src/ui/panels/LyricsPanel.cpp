#include "LyricsPanel.h"
#include "../components/AppModal.h"
#include <algorithm>
#include <cmath>
#include <limits>

#if !JUCE_IOS && !JUCE_ANDROID
using namespace playworship::lyrics;

namespace {
constexpr float mapInset = 18.0f;
constexpr float rulerHeight = 46.0f;
constexpr float laneHeight = 82.0f;
constexpr float previewWidth = 184.0f;
constexpr int markLimit = 1024;

juce::String firstLines(const juce::String &text, int count = 2) {
  auto lines = juce::StringArray::fromLines(text);
  lines.removeEmptyStrings(true);
  if (lines.size() > count) lines.removeRange(count, lines.size() - count);
  return lines.joinIntoString("\n");
}

juce::String markTitle(const Mark &mark) {
  if (mark.target.hide) return "Ocultar letras";
  return mark.target.title.isNotEmpty() ? mark.target.title : "Destino sin nombre";
}

void styleButton(juce::TextButton &button, bool accent = false) {
  button.setColour(juce::TextButton::buttonColourId,
                   accent ? UITheme::accent.withAlpha(0.15f) : UITheme::cardBgHover);
  button.setColour(juce::TextButton::buttonOnColourId, UITheme::accentMuted);
  button.setColour(juce::TextButton::textColourOffId,
                   accent ? UITheme::accent : UITheme::textPrimary);
}
}

class LyricsPanel::SongMap final : public juce::Component {
public:
  explicit SongMap(LyricsPanel &panel) : owner(panel) {
    setWantsKeyboardFocus(true);
    setTitle("Mapa completo de letras");
    setDescription("Marcas independientes. Arrastra el punto para cambiar el tiempo. Seleccionar no proyecta.");
  }
  void paint(juce::Graphics &g) override { owner.paintMap(g); }
  bool keyPressed(const juce::KeyPress &key) override { return owner.keyPressed(key); }
  void mouseDown(const juce::MouseEvent &e) override {
    if (isShowing()) grabKeyboardFocus();
    const auto hit = owner.hitMark(e.position);
    if (e.mods.isPopupMenu()) { owner.showMapMenu(e.position, hit); return; }
    owner.selectMark(hit);
    owner.dragId = owner.hitMark(e.position, true);
    owner.dragChanged = false;
    if (const auto *mark = owner.findMark(owner.dragId)) owner.dragStartSeconds = mark->seconds;
  }
  void mouseDrag(const juce::MouseEvent &e) override {
    if (owner.dragId.isEmpty() || !e.mouseWasDraggedSinceMouseDown()) return;
    owner.dragChanged = true;
    owner.moveMark(owner.dragId, owner.xToTime(e.position.x), false);
  }
  void mouseUp(const juce::MouseEvent &) override {
    if (owner.dragChanged) owner.changed();
    owner.dragId.clear();
    owner.dragChanged = false;
    owner.rebuildMapLayout();
  }
  void mouseMove(const juce::MouseEvent &e) override {
    setMouseCursor(owner.hitMark(e.position, true).isNotEmpty()
                     ? juce::MouseCursor::LeftRightResizeCursor
                     : juce::MouseCursor::NormalCursor);
  }
  void mouseDoubleClick(const juce::MouseEvent &e) override {
    const auto id = owner.hitMark(e.position);
    if (id.isEmpty()) owner.showDestinationPicker(owner.xToTime(e.position.x));
    else owner.showDestinationPicker(0.0, id);
  }
private:
  LyricsPanel &owner;
};

class LyricsPanel::DestinationPicker final : public juce::Component,
                                             private juce::ListBoxModel {
public:
  DestinationPicker(std::vector<SlideDestination> options,
                    std::function<void(Target)> callback)
      : destinations(std::move(options)), onUse(std::move(callback)), list("Destinos FreeShow", this) {
    title.setText("Elegir destino FreeShow", juce::dontSendNotification);
    title.setFont(makeAppFont(15.0f, AppFontWeight::SemiBold));
    title.setColour(juce::Label::textColourId, UITheme::textPrimary);
    addAndMakeVisible(title);
    search.setTextToShowWhenEmpty(juce::String::fromUTF8("Buscar nombre o texto…"), UITheme::textSecondary);
    applyTextEditorTheme(search);
    search.onTextChange = [this] { filter(); };
    addAndMakeVisible(search);
    list.setRowHeight(71);
    list.setColour(juce::ListBox::backgroundColourId, UITheme::panelDark);
    list.setColour(juce::ListBox::outlineColourId, UITheme::dividerSoft);
    list.setOutlineThickness(1);
    addAndMakeVisible(list);
    helper.setText(destinations.empty() ? juce::String::fromUTF8("Conecta FreeShow y elige un show/arreglo para consultar sus letras.")
                                     : juce::String::fromUTF8("Aparición = repetición del grupo · Pantalla = slide del grupo"),
                   juce::dontSendNotification);
    helper.setFont(makeAppFont(10.5f));
    helper.setColour(juce::Label::textColourId, UITheme::textSecondary);
    addAndMakeVisible(helper);
    use.setButtonText("Usar destino");
    styleButton(use, true);
    use.onClick = [this] {
      const int row = list.getSelectedRow();
      if (!juce::isPositiveAndBelow(row, static_cast<int>(filtered.size()))) return;
      Target result;
      if (filtered[static_cast<size_t>(row)] < 0) { result.hide = true; result.title = "Ocultar letras"; }
      else result = destinations[static_cast<size_t>(filtered[static_cast<size_t>(row)])].target;
      if (onUse) onUse(result);
      if (auto *callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
    };
    addAndMakeVisible(use);
    filter();
    setSize(560, 414);
  }
  void paint(juce::Graphics &g) override { g.fillAll(UITheme::panelDark); }
  void resized() override {
    auto area = getLocalBounds().reduced(12);
    title.setBounds(area.removeFromTop(27));
    search.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);
    auto bottom = area.removeFromBottom(35);
    use.setBounds(bottom.removeFromRight(120));
    helper.setBounds(area.removeFromBottom(28));
    list.setBounds(area);
  }
private:
  int getNumRows() override { return static_cast<int>(filtered.size()); }
  void paintListBoxItem(int row, juce::Graphics &g, int width, int height, bool selected) override {
    if (!juce::isPositiveAndBelow(row, static_cast<int>(filtered.size()))) return;
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(10, 6);
    if (selected) { g.setColour(UITheme::accent.withAlpha(0.14f)); g.fillRect(0, 0, width, height); }
    const int index = filtered[static_cast<size_t>(row)];
    g.setColour(selected ? UITheme::accent : UITheme::textPrimary);
    g.setFont(makeAppFont(12.5f, AppFontWeight::SemiBold));
    if (index < 0) {
      g.drawText("Ocultar letras", area.removeFromTop(20), juce::Justification::centredLeft);
      g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(11.0f));
      g.drawText("Limpiar la letra proyectada", area, juce::Justification::topLeft);
    } else {
      const auto &d = destinations[static_cast<size_t>(index)];
      g.drawFittedText(d.displayName, area.removeFromTop(20), juce::Justification::centredLeft, 1);
      g.setColour(UITheme::textPrimary.withAlpha(0.82f)); g.setFont(makeAppFont(11.5f));
      g.drawFittedText(firstLines(d.target.text, 1), area.removeFromTop(20), juce::Justification::centredLeft, 1);
      g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(9.5f));
      g.drawFittedText("Grupo " + d.target.groupId + juce::String::fromUTF8(" · Slide ") + d.target.slideId
                      + juce::String::fromUTF8(" · Índice ") + juce::String(d.absoluteIndex), area, juce::Justification::centredLeft, 1);
    }
    g.setColour(UITheme::dividerSoft); g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
  }
  void selectedRowsChanged(int row) override { use.setEnabled(row >= 0); }
  void listBoxItemDoubleClicked(int, const juce::MouseEvent &) override { use.triggerClick(); }
  void filter() {
    filtered.clear();
    const auto query = search.getText().trim();
    if (query.isEmpty() || juce::String("Ocultar letras").containsIgnoreCase(query)) filtered.push_back(-1);
    for (size_t i = 0; i < destinations.size(); ++i)
      if (query.isEmpty() || (destinations[i].displayName + " " + destinations[i].target.text).containsIgnoreCase(query))
        filtered.push_back(static_cast<int>(i));
    list.updateContent(); list.selectRow(filtered.empty() ? -1 : 0); repaint();
  }
  std::vector<SlideDestination> destinations;
  std::vector<int> filtered;
  std::function<void(Target)> onUse;
  juce::Label title, helper;
  juce::TextEditor search;
  juce::ListBox list;
  juce::TextButton use;
};

LyricsPanel::LyricsPanel() : map(std::make_unique<SongMap>(*this)) {
  setOpaque(true);
  setWantsKeyboardFocus(true);
  viewport.setViewedComponent(map.get(), false);
  viewport.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport);
  for (auto *button : {&connectionButton, &testConnectionButton, &refreshButton,
                       &addButton, &testButton, &detailMenuButton}) addAndMakeVisible(*button);
  addAndMakeVisible(enabledToggle);
  addAndMakeVisible(showCombo); addAndMakeVisible(layoutCombo);
  for (auto *label : {&hostLabel, &portLabel, &tokenLabel, &showLabel, &layoutLabel}) {
    label->setFont(makeAppFont(10.0f, AppFontWeight::Medium));
    addAndMakeVisible(*label);
  }
  hostLabel.setText("Host local / LAN", juce::dontSendNotification);
  portLabel.setText("REST", juce::dontSendNotification);
  tokenLabel.setText(juce::String::fromUTF8("Token de sesión (opcional)"), juce::dontSendNotification);
  showLabel.setText("SHOW", juce::dontSendNotification);
  layoutLabel.setText("ARREGLO", juce::dontSendNotification);
  for (auto *editor : {&hostEditor, &portEditor, &tokenEditor}) {
    editor->setFont(makeAppFont(12.0f));
    addAndMakeVisible(*editor);
  }
  hostEditor.setTitle("Host FreeShow"); portEditor.setTitle("Puerto REST FreeShow");
  tokenEditor.setTitle(juce::String::fromUTF8("Token de sesión FreeShow")); tokenEditor.setPasswordCharacter(0x2022);
  portEditor.setInputRestrictions(5, "0123456789");
  hostEditor.setText("127.0.0.1"); portEditor.setText("5506");
  showCombo.setTextWhenNothingSelected(juce::String::fromUTF8("Elegir show…"));
  layoutCombo.setTextWhenNothingSelected(juce::String::fromUTF8("Elegir arreglo…"));
  connectionButton.onClick = [this] { settingsVisible = !settingsVisible; resized(); repaint(); };
  testConnectionButton.onClick = [this] { testConnection(); };
  refreshButton.onClick = [this] { refreshCatalogue(); };
  addButton.onClick = [this] { showDestinationPicker(positionProvider ? positionProvider() : 0.0); };
  testButton.onClick = [this] { testSelected(); };
  detailMenuButton.onClick = [this] {
    if (const auto *m = findMark(selectedId)) showMapMenu({timeToX(m->seconds), 50.0f}, selectedId);
  };
  enabledToggle.onClick = [this] { lyrics.enabled = enabledToggle.getToggleState(); changed(); };
  showCombo.onChange = [this] {
    if (updating) return;
    const int index = showCombo.getSelectedId() - 1;
    if (!juce::isPositiveAndBelow(index, static_cast<int>(shows.size()))) return;
    loadShow(shows[static_cast<size_t>(index)].id, true);
  };
  layoutCombo.onChange = [this] {
    if (updating) return;
    const int index = layoutCombo.getSelectedId() - 1;
    if (!juce::isPositiveAndBelow(index, static_cast<int>(catalogue.layouts.size()))) return;
    const auto &layout = catalogue.layouts[static_cast<size_t>(index)];
    lyrics.layoutId = layout.id; lyrics.layoutName = layout.name;
    changed(); updateDetail(); map->repaint();
  };
  refreshThemeColours();
  startTimerHz(12);
}

LyricsPanel::~LyricsPanel() { stopTimer(); viewport.setViewedComponent(nullptr); }

void LyricsPanel::setClient(FreeShowClient *next) {
  client = next;
  lastReviewRevision = 0;
  if (client != nullptr) {
    const auto settings = client->getSettings();
    hostEditor.setText(settings.host, false); portEditor.setText(juce::String(settings.port), false);
    tokenEditor.setText(settings.authToken, false);
    if (client->getStatus().state == ConnectionStatus::State::connected) refreshCatalogue();
  }
  timerCallback();
}

void LyricsPanel::setSongLyrics(const SongLyrics &next) {
  const auto previousShow = lyrics.showId;
  ++catalogueRequest; // A pending user selection must never bind a subsequently loaded song.
  loadingCatalogue = false;
  if (previousShow != next.showId || next.showId.isEmpty()) {
    catalogue = {};
    catalogueError.clear();
    loadingCatalogue = false;
  }
  lyrics = next;
  if (findMark(selectedId) == nullptr) selectedId.clear();
  enabledToggle.setToggleState(lyrics.enabled, juce::dontSendNotification);
  rebuildShowSelectors(); rebuildMapLayout(); updateDetail(); resized(); repaint();
  if (client != nullptr && next.showId.isNotEmpty()
      && (previousShow != next.showId || catalogue.id != next.showId)) loadShow(next.showId, false);
}

void LyricsPanel::setArrangement(const SongArrangementSnapshot &next) { arrangement = next; map->repaint(); }
void LyricsPanel::setDuration(double seconds) { duration = std::isfinite(seconds) ? juce::jmax(1.0, seconds) : 1.0; rebuildMapLayout(); }
void LyricsPanel::setTransportProviders(std::function<double()> position, std::function<bool()> playing) {
  positionProvider = std::move(position); playingProvider = std::move(playing);
}

void LyricsPanel::refreshThemeColours() {
  for (auto *button : {&connectionButton, &testConnectionButton, &refreshButton, &detailMenuButton}) styleButton(*button);
  styleButton(addButton, true); styleButton(testButton, true);
  for (auto *label : {&hostLabel, &portLabel, &tokenLabel, &showLabel, &layoutLabel})
    label->setColour(juce::Label::textColourId, UITheme::textSecondary);
  for (auto *editor : {&hostEditor, &portEditor, &tokenEditor}) applyTextEditorTheme(*editor);
  applyComboBoxTheme(showCombo); applyComboBoxTheme(layoutCombo);
  applyToggleSwitchTheme(enabledToggle);
  repaint(); if (map != nullptr) map->repaint();
}

void LyricsPanel::paint(juce::Graphics &g) {
  g.fillAll(UITheme::panelDark);
  g.setColour(UITheme::textPrimary); g.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
  g.drawText("Mapa de letras", titleBounds, juce::Justification::centredLeft);
  g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(10.5f));
  g.drawText(statusText, statusBounds, juce::Justification::centredLeft, true);
  g.drawText(juce::String::fromUTF8("Canción completa · Arrastra los puntos para ajustar el tiempo · Clic derecho para editar"), helperBounds,
             juce::Justification::centredLeft, true);
  g.setColour(UITheme::dividerSoft);
  g.drawHorizontalLine(viewport.getY() - 1, 12.0f, static_cast<float>(getWidth() - 12));
  g.drawHorizontalLine(detailBounds.getY(), 12.0f, static_cast<float>(getWidth() - 12));
  auto detail = detailBounds.reduced(12, 8);
  detail.removeFromRight(179);
  if (const auto *mark = findMark(selectedId)) {
    g.setColour(mark->enabled ? UITheme::accent : UITheme::textSecondary);
    g.setFont(makeAppFont(12.0f, AppFontWeight::SemiBold));
    g.drawFittedText(formatTimeSeconds(mark->seconds) + "  " + markTitle(*mark)
                    + (mark->enabled ? "" : juce::String::fromUTF8(" · Desactivada")), detail.removeFromTop(19), juce::Justification::centredLeft, 1);
    const auto issue = reviewReason(*mark);
    g.setColour(issue.isNotEmpty() ? juce::Colour(0xffe6ad59) : UITheme::textSecondary);
    g.setFont(makeAppFont(10.0f));
    g.drawFittedText(issue.isNotEmpty() ? "Revisar: " + issue : destinationDescription(*mark),
                    detail.removeFromTop(17), juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textPrimary.withAlpha(0.86f)); g.setFont(makeAppFont(11.5f));
    g.drawFittedText(mark->target.hide ? "Oculta la letra del output." : previewText(*mark),
                    detail, juce::Justification::topLeft, 3);
  } else {
    g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(11.0f));
    g.drawFittedText(juce::String::fromUTF8("Selecciona una marca para ver el detalle. Probar en FreeShow requiere un clic explícito."),
                    detail, juce::Justification::centredLeft, 2);
  }
}

void LyricsPanel::resized() {
  const int width = getWidth();
  auto row = juce::Rectangle<int>(12, 8, juce::jmax(0, width - 24), 28);
  titleBounds = row.removeFromLeft(136);
  connectionButton.setBounds(row.removeFromRight(175)); row.removeFromRight(8);
  enabledToggle.setBounds(row.removeFromRight(162));
  row = juce::Rectangle<int>(12, 43, juce::jmax(0, width - 24), 30);
  addButton.setBounds(row.removeFromRight(82)); row.removeFromRight(8);
  refreshButton.setBounds(row.removeFromRight(88)); row.removeFromRight(12);
  showLabel.setBounds(row.removeFromLeft(38));
  const int comboWidth = juce::jmax(100, (row.getWidth() - 84) / 2);
  showCombo.setBounds(row.removeFromLeft(comboWidth)); row.removeFromLeft(12);
  layoutLabel.setBounds(row.removeFromLeft(53)); layoutCombo.setBounds(row);
  int top = 80;
  for (auto *component : std::initializer_list<juce::Component *>{&hostLabel, &portLabel, &tokenLabel,
                         &hostEditor, &portEditor, &tokenEditor, &testConnectionButton}) component->setVisible(settingsVisible);
  if (settingsVisible) {
    auto fields = juce::Rectangle<int>(12, top, juce::jmax(0, width - 24), 49);
    auto test = fields.removeFromRight(140); testConnectionButton.setBounds(test.withTrimmedTop(19));
    fields.removeFromRight(10);
    auto host = fields.removeFromLeft(juce::jlimit(130, 230, fields.getWidth() / 3));
    hostLabel.setBounds(host.removeFromTop(18)); hostEditor.setBounds(host);
    fields.removeFromLeft(9); auto port = fields.removeFromLeft(65);
    portLabel.setBounds(port.removeFromTop(18)); portEditor.setBounds(port);
    fields.removeFromLeft(9); tokenLabel.setBounds(fields.removeFromTop(18)); tokenEditor.setBounds(fields);
    top += 56;
  }
  statusBounds = {14, top, juce::jmax(0, width - 28), 18}; top += 22;
  helperBounds = {14, top, juce::jmax(0, width - 28), 18}; top += 22;
  const int detailHeight = selectedId.isNotEmpty() ? 100 : 46;
  detailBounds = {0, juce::jmax(top + 80, getHeight() - detailHeight), width, detailHeight};
  viewport.setBounds(6, top, juce::jmax(0, width - 12), juce::jmax(0, detailBounds.getY() - top));
  const bool selected = findMark(selectedId) != nullptr;
  testButton.setVisible(selected); detailMenuButton.setVisible(selected);
  testButton.setBounds(width - 174, detailBounds.getY() + 12, 160, 29);
  detailMenuButton.setBounds(width - 46, detailBounds.getY() + 49, 32, 25);
  rebuildMapLayout();
}

float LyricsPanel::timeToX(double seconds) const {
  return mapInset + static_cast<float>(juce::jlimit(0.0, duration, seconds) / duration)
                      * juce::jmax(1.0f, static_cast<float>(map->getWidth()) - 2.0f * mapInset);
}
double LyricsPanel::xToTime(float x) const {
  return juce::jlimit(0.0, duration, static_cast<double>((x - mapInset)
                  / juce::jmax(1.0f, static_cast<float>(map->getWidth()) - 2.0f * mapInset)) * duration);
}

void LyricsPanel::rebuildMapLayout() {
  const int mapWidth = juce::jmax(100, viewport.getWidth() - viewport.getScrollBarThickness());
  map->setSize(mapWidth, juce::jmax(100, map->getHeight()));
  std::vector<const Mark *> ordered;
  for (const auto &mark : lyrics.marks) ordered.push_back(&mark);
  std::stable_sort(ordered.begin(), ordered.end(), [](const Mark *a, const Mark *b) { return a->seconds < b->seconds; });
  std::vector<float> laneEnds;
  placements.clear();
  for (const auto *mark : ordered) {
    const auto x = timeToX(mark->seconds);
    const float cardWidth = juce::jmin(previewWidth, static_cast<float>(mapWidth) - 2.0f * mapInset);
    const float left = juce::jlimit(mapInset, static_cast<float>(mapWidth) - mapInset - cardWidth, x - 12.0f);
    int lane = 0;
    while (lane < static_cast<int>(laneEnds.size()) && laneEnds[static_cast<size_t>(lane)] + 10.0f > left) ++lane;
    if (lane == static_cast<int>(laneEnds.size())) laneEnds.push_back(0.0f);
    laneEnds[static_cast<size_t>(lane)] = left + cardWidth;
    const float y = rulerHeight + 10.0f + static_cast<float>(lane) * laneHeight;
    placements.push_back({mark->id, {x, y}, {left, y + 14.0f, cardWidth, 57.0f}, lane});
  }
  const int required = static_cast<int>(rulerHeight + juce::jmax(1, static_cast<int>(laneEnds.size())) * laneHeight + 14.0f);
  map->setSize(mapWidth, juce::jmax(required, viewport.getHeight()));
  map->repaint();
}

void LyricsPanel::paintMap(juce::Graphics &g) {
  const auto area = map->getLocalBounds().toFloat();
  g.fillAll(UITheme::panelDark);
  for (const auto &section : arrangement.parts) {
    const float left = timeToX(section.startSec), right = timeToX(section.endSec);
    juce::Colour colour = section.color.isNotEmpty() ? juce::Colour::fromString(section.color) : UITheme::textSecondary;
    g.setColour(colour.withAlpha(0.045f)); g.fillRect(left, 25.0f, juce::jmax(0.0f, right-left), area.getHeight()-25.0f);
    g.setColour(colour.withAlpha(0.13f)); g.drawVerticalLine(juce::roundToInt(left), 24.0f, area.getBottom());
    g.setColour(UITheme::textSecondary.withAlpha(0.48f)); g.setFont(makeAppFont(10.0f, AppFontWeight::Medium));
    g.drawFittedText(section.name, juce::Rectangle<float>(left + 5.0f, 25.0f, juce::jmax(0.0f, right-left-10.0f), 16.0f).toNearestInt(),
                    juce::Justification::centredLeft, 1);
  }
  const int ticks = juce::jlimit(2, 12, map->getWidth() / 110);
  g.setFont(makeAppFont(9.5f));
  for (int i = 0; i <= ticks; ++i) {
    const double seconds = duration * static_cast<double>(i) / ticks;
    const float x = timeToX(seconds);
    g.setColour(UITheme::dividerSoft.withAlpha(0.5f)); g.drawVerticalLine(juce::roundToInt(x), 19.0f, area.getBottom());
    g.setColour(UITheme::textSecondary.withAlpha(0.8f));
    auto tickArea = juce::Rectangle<int>(juce::roundToInt(x)-24, 1, 48, 17);
    if (i == 0) tickArea.setX(juce::roundToInt(x));
    if (i == ticks) tickArea.setRight(juce::roundToInt(x));
    g.drawText(formatTimeSeconds(seconds), tickArea, i == 0 ? juce::Justification::centredLeft :
               (i == ticks ? juce::Justification::centredRight : juce::Justification::centred), false);
  }
  if (positionProvider) {
    const float x = timeToX(positionProvider());
    g.setColour(UITheme::textPrimary.withAlpha(playingProvider && playingProvider() ? 0.65f : 0.25f));
    g.drawLine(x, 17.0f, x, area.getBottom(), 1.0f);
    juce::Path flag; flag.addTriangle(x-4, 18, x+4, 18, x, 23); g.fillPath(flag);
  }
  if (placements.empty()) {
    g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(13.0f, AppFontWeight::Medium));
    g.drawText(juce::String::fromUTF8("Añade marcas de letra en cualquier momento de la canción"), area.toNearestInt().withTrimmedTop(50).withHeight(24),
               juce::Justification::centred);
    g.setFont(makeAppFont(11.0f));
    g.drawText("Clic derecho sobre el mapa o + Marca. Las secciones solo sirven de referencia.",
               area.toNearestInt().withTrimmedTop(80).withHeight(22), juce::Justification::centred);
  }
  for (const auto &placement : placements) {
    const auto *mark = findMark(placement.id); if (mark == nullptr) continue;
    const bool selected = mark->id == selectedId;
    const bool review = reviewReason(*mark).isNotEmpty();
    auto colour = !mark->enabled ? UITheme::textSecondary.withAlpha(0.48f)
                  : review ? juce::Colour(0xffe6ad59) : UITheme::accent;
    const auto card = placement.preview;
    g.setColour(colour.withAlpha(selected ? 0.75f : 0.42f));
    g.drawLine(placement.point.x, placement.point.y+4.0f, placement.point.x, card.getY(), selected ? 1.6f : 1.0f);
    if (selected) { g.setColour(colour.withAlpha(0.14f)); g.fillEllipse(placement.point.x-10, placement.point.y-10, 20, 20); }
    g.setColour(UITheme::panelDark); g.fillEllipse(placement.point.x-6, placement.point.y-6, 12, 12);
    g.setColour(colour); g.fillEllipse(placement.point.x-4, placement.point.y-4, 8, 8);
    if (mark->enabled) g.drawEllipse(placement.point.x-6, placement.point.y-6, 12, 12, 1.1f);
    g.setColour(selected ? UITheme::cardBgHover : UITheme::cardBg.withAlpha(0.94f));
    g.fillRoundedRectangle(card, 5.0f);
    g.setColour(colour.withAlpha(selected ? 0.9f : 0.26f)); g.drawRoundedRectangle(card.reduced(0.5f), 5.0f, selected ? 1.2f : 0.7f);
    auto content = card.reduced(8, 5).toNearestInt();
    auto title = content.removeFromTop(17);
    auto time = title.removeFromRight(41);
    g.setColour(colour); g.setFont(makeAppFont(9.0f));
    g.drawText(formatTimeSeconds(mark->seconds), time, juce::Justification::centredRight);
    g.setColour(mark->enabled ? UITheme::textPrimary : UITheme::textSecondary);
    g.setFont(makeAppFont(11.0f, AppFontWeight::SemiBold));
    g.drawFittedText(markTitle(*mark), title, juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textSecondary.withAlpha(mark->enabled ? 1.0f : 0.55f));
    g.setFont(makeAppFont(10.5f));
    const auto text = mark->target.hide ? "Pantalla sin letra" : firstLines(previewText(*mark));
    g.drawFittedText(text.isNotEmpty() ? text : "Sin texto de preview", content, juce::Justification::topLeft, 2);
  }
}

Mark *LyricsPanel::findMark(const juce::String &id) {
  for (auto &mark : lyrics.marks) if (mark.id == id) return &mark;
  return nullptr;
}
const Mark *LyricsPanel::findMark(const juce::String &id) const {
  for (const auto &mark : lyrics.marks) if (mark.id == id) return &mark;
  return nullptr;
}
const Layout *LyricsPanel::selectedLayout() const {
  for (const auto &layout : catalogue.layouts) if (layout.id == lyrics.layoutId) return &layout;
  return nullptr;
}
juce::String LyricsPanel::reviewReason(const Mark &mark) const {
  if (mark.target.hide) return {};
  if (catalogueError.isNotEmpty()) return catalogueError;
  if (loadingCatalogue) return juce::String::fromUTF8("verificando catálogo FreeShow");
  if (mark.target.showId != lyrics.showId || mark.target.layoutId != lyrics.layoutId)
    return "el destino pertenece a otro show o arreglo; vuelve a elegirlo";
  if (catalogue.id.isEmpty()) return juce::String::fromUTF8("catálogo pendiente de verificar");
  return resolve(mark.target, catalogue).error;
}
juce::String LyricsPanel::destinationDescription(const Mark &mark) const {
  if (reviewReason(mark).isEmpty() && !mark.target.hide) {
    const auto destination = resolve(mark.target, catalogue);
    for (const auto &layout : catalogue.layouts) if (layout.id == mark.target.layoutId)
      for (const auto &slide : layout.slides) if (slide.absoluteIndex == destination.absoluteIndex)
        return slide.displayName;
  }
  return targetLabel(mark.target);
}
juce::String LyricsPanel::previewText(const Mark &mark) const {
  if (reviewReason(mark).isEmpty() && !mark.target.hide) {
    const auto destination = resolve(mark.target, catalogue);
    for (const auto &layout : catalogue.layouts) if (layout.id == mark.target.layoutId)
      for (const auto &slide : layout.slides) if (slide.absoluteIndex == destination.absoluteIndex)
        return slide.target.text;
  }
  return mark.target.text;
}
juce::String LyricsPanel::hitMark(juce::Point<float> point, bool pointOnly) const {
  for (auto it = placements.rbegin(); it != placements.rend(); ++it)
    if (it->point.getDistanceFrom(point) <= 11.0f || (!pointOnly && it->preview.contains(point))) return it->id;
  return {};
}
juce::Point<int> LyricsPanel::markerScreenPoint(const juce::String &id) const {
  for (const auto &p : placements) if (p.id == id) return map->localPointToGlobal(p.point.toInt());
  return {};
}

void LyricsPanel::selectMark(const juce::String &id) {
  selectedId = id; updateDetail(); resized();
  for (const auto &p : placements) if (p.id == id) {
    const int top = viewport.getViewPositionY();
    if (p.preview.getBottom() > top + viewport.getHeight())
      viewport.setViewPosition(0, juce::roundToInt(p.preview.getBottom()) - viewport.getHeight() + 7);
    else if (p.point.y - 10.0f < top)
      viewport.setViewPosition(0, juce::jmax(0, juce::roundToInt(p.point.y) - 15));
    break;
  }
  repaint(); map->repaint();
}
void LyricsPanel::updateDetail() {
  const auto *mark = findMark(selectedId);
  const bool connected = client != nullptr && client->getStatus().state == ConnectionStatus::State::connected;
  testButton.setEnabled(mark != nullptr && mark->enabled && connected && reviewReason(*mark).isEmpty());
  repaint();
}
void LyricsPanel::changed() {
  if (onChanged) onChanged(lyrics);
  updateDetail(); map->repaint(); repaint();
}
void LyricsPanel::moveMark(const juce::String &id, double seconds, bool notify) {
  if (auto *mark = findMark(id)) mark->seconds = juce::jlimit(0.0, duration, seconds);
  rebuildMapLayout(); updateDetail(); if (notify) changed();
}

void LyricsPanel::showDestinationPicker(double seconds, const juce::String &id) {
  if (id.isEmpty() && lyrics.marks.size() >= markLimit) return;
  std::vector<SlideDestination> options;
  if (const auto *layout = selectedLayout()) options = layout->slides;
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  const auto request = catalogueRequest;
  auto picker = std::make_unique<DestinationPicker>(std::move(options), [safe, id, seconds, request](Target target) {
    if (safe == nullptr || request != safe->catalogueRequest) return;
    if (id.isEmpty() && safe->lyrics.marks.size() >= markLimit) return;
    if (id.isNotEmpty() && safe->findMark(id) == nullptr) return;
    if (auto *mark = safe->findMark(id)) mark->target = std::move(target);
    else {
      Mark newMark; newMark.id = juce::Uuid().toString(); newMark.seconds = juce::jlimit(0.0, safe->duration, seconds);
      newMark.target = std::move(target); safe->selectedId = newMark.id; safe->lyrics.marks.push_back(std::move(newMark));
    }
    safe->rebuildMapLayout(); safe->changed(); safe->resized();
  });
  auto anchor = addButton.getBounds();
  if (const auto *mark = findMark(id))
    anchor = getLocalArea(map.get(), juce::Rectangle<int>(juce::roundToInt(timeToX(mark->seconds)), 46, 1, 1));
  auto *parent = getTopLevelComponent();
  presentActionPopover(std::move(picker), parent->getLocalArea(this, anchor), parent);
}

void LyricsPanel::editMarkTime(const juce::String &id) {
  const auto *mark = findMark(id); if (mark == nullptr) return;
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  inputModal = std::make_unique<AppModal>(*this, AppModal::Kind::Input, "Tiempo de la marca",
      juce::String::fromUTF8("Posición en segundos de la canción. No modifica las secciones musicales."), "Guardar", "Cancelar",
      [safe, id](bool accepted, const juce::String &value) {
        if (safe == nullptr || !accepted) return;
        const auto text = value.trim().replaceCharacter(',', '.');
        const int dot = text.indexOfChar('.');
        if (text.containsAnyOf("0123456789") && text.containsOnly("0123456789.")
            && (dot < 0 || text.indexOfChar(dot + 1, '.') < 0))
          safe->moveMark(id, text.getDoubleValue(), true);
      }, "Segundos", juce::String(mark->seconds, 3));
  inputModal->show();
}
void LyricsPanel::editMarkName(const juce::String &id) {
  const auto *mark = findMark(id); if (mark == nullptr) return;
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  inputModal = std::make_unique<AppModal>(*this, AppModal::Kind::Input, "Nombre de la marca",
      "El nombre es una referencia visual; el destino mantiene su identidad FreeShow.", "Guardar", "Cancelar",
      [safe, id](bool accepted, const juce::String &value) {
        if (safe == nullptr || !accepted || value.trim().isEmpty()) return;
        if (auto *mark = safe->findMark(id)) mark->target.title = value.trim().substring(0, 160);
        safe->changed();
      }, "Nombre", markTitle(*mark));
  inputModal->show();
}

void LyricsPanel::showMapMenu(juce::Point<float> point, const juce::String &id) {
  juce::PopupMenu menu;
  const auto *mark = findMark(id);
  if (mark != nullptr) selectMark(id);
  const double seconds = xToTime(point.x);
  menu.addSectionHeader(mark != nullptr ? formatTimeSeconds(mark->seconds) + juce::String::fromUTF8(" · ") + markTitle(*mark)
                                       : juce::String::fromUTF8("Letras · ") + formatTimeSeconds(seconds));
  menu.addItem(1, juce::String::fromUTF8("Añadir marca aquí…"), lyrics.marks.size() < markLimit);
  if (mark != nullptr) {
    menu.addSeparator();
    menu.addItem(2, juce::String::fromUTF8("Cambiar destino FreeShow…"));
    menu.addItem(3, juce::String::fromUTF8("Cambiar tiempo…"));
    menu.addItem(4, juce::String::fromUTF8("Cambiar nombre…"));
    menu.addItem(5, "Duplicar marca", lyrics.marks.size() < markLimit);
    menu.addItem(6, mark->enabled ? "Desactivar marca" : "Activar marca");
    menu.addSeparator();
    menu.addItem(7, "Probar en FreeShow", testButton.isEnabled());
    menu.addItem(8, "Eliminar marca");
  }
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  const auto screen = map->localPointToGlobal(point.toInt());
  menu.showMenuAsync(makeTouchPopupMenuOptions(nullptr).withTargetScreenArea({screen.x, screen.y, 1, 1})
      .withParentComponent(getTopLevelComponent()), [safe, id, seconds](int result) {
    if (safe == nullptr || result == 0) return;
    if (result == 1) { safe->showDestinationPicker(seconds); return; }
    auto *mark = safe->findMark(id); if (mark == nullptr) return;
    if (result == 2) { safe->showDestinationPicker(0.0, id); return; }
    if (result == 3) { safe->editMarkTime(id); return; }
    if (result == 4) { safe->editMarkName(id); return; }
    if (result == 5) {
      if (safe->lyrics.marks.size() >= markLimit) return;
      Mark copy = *mark; copy.id = juce::Uuid().toString();
      copy.seconds = juce::jmin(safe->duration, copy.seconds + 1.0);
      safe->selectedId = copy.id; safe->lyrics.marks.push_back(std::move(copy));
    }
    if (result == 6) mark->enabled = !mark->enabled;
    if (result == 7) { safe->selectMark(id); safe->testSelected(); return; }
    if (result == 8) {
      safe->lyrics.marks.erase(std::remove_if(safe->lyrics.marks.begin(), safe->lyrics.marks.end(),
          [&id](const Mark &m) { return m.id == id; }), safe->lyrics.marks.end());
      if (safe->selectedId == id) safe->selectedId.clear();
    }
    safe->rebuildMapLayout(); safe->changed(); safe->resized();
  });
}

void LyricsPanel::testConnection() {
  if (client == nullptr) return;
  ConnectionSettings settings;
  settings.host = hostEditor.getText().trim(); settings.port = portEditor.getText().getIntValue();
  settings.authToken = tokenEditor.getText();
  juce::String error;
  if (!FreeShowClient::validateSettings(settings, &error)) { catalogueError = error; timerCallback(); return; }
  catalogueError.clear();
  client->configure(std::move(settings));
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  client->testConnection([safe](bool ok, juce::String message) {
    juce::MessageManager::callAsync([safe, ok, message] {
      if (safe == nullptr) return;
      safe->statusText = message;
      if (ok) safe->refreshCatalogue();
      safe->timerCallback();
    });
  });
  timerCallback();
}
void LyricsPanel::refreshCatalogue() {
  if (client == nullptr) return;
  loadingCatalogue = true;
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  client->requestShows([safe](std::vector<ShowSummary> result, juce::String error) {
    juce::MessageManager::callAsync([safe, result = std::move(result), error = std::move(error)]() mutable {
      if (safe == nullptr) return;
      safe->shows = std::move(result); safe->catalogueError = std::move(error);
      safe->loadingCatalogue = false; safe->rebuildShowSelectors();
      if (safe->lyrics.showId.isNotEmpty()) safe->loadShow(safe->lyrics.showId, false);
      safe->timerCallback();
    });
  });
}
void LyricsPanel::loadShow(const juce::String &id, bool updateBinding) {
  if (client == nullptr || id.isEmpty()) return;
  const unsigned int request = ++catalogueRequest;
  loadingCatalogue = true;
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  client->requestShow(id, [safe, request, updateBinding](ShowCatalog result) {
    juce::MessageManager::callAsync([safe, request, updateBinding, result = std::move(result)]() mutable {
      if (safe == nullptr || request != safe->catalogueRequest) return;
      safe->loadingCatalogue = false;
      if (!result.ok()) {
        safe->catalogueError = result.error.isNotEmpty() ? result.error : "El show no contiene un arreglo compatible";
        safe->rebuildShowSelectors(); safe->timerCallback(); return;
      }
      safe->catalogueError.clear(); safe->catalogue = std::move(result);
      if (updateBinding) {
        safe->lyrics.showId = safe->catalogue.id; safe->lyrics.showName = safe->catalogue.name;
        safe->lyrics.layoutId = safe->catalogue.activeLayoutId;
        if (safe->lyrics.layoutId.isEmpty()) safe->lyrics.layoutId = safe->catalogue.layouts.front().id;
        for (const auto &layout : safe->catalogue.layouts)
          if (layout.id == safe->lyrics.layoutId) safe->lyrics.layoutName = layout.name;
      }
      safe->rebuildShowSelectors(); safe->updateDetail(); safe->map->repaint();
      if (updateBinding) safe->changed();
    });
  });
}
void LyricsPanel::rebuildShowSelectors() {
  updating = true;
  showCombo.clear(juce::dontSendNotification);
  int selection = 0;
  for (size_t i = 0; i < shows.size(); ++i) {
    showCombo.addItem(shows[i].name + "  [" + shows[i].id + "]", static_cast<int>(i) + 1);
    if (shows[i].id == lyrics.showId) selection = static_cast<int>(i) + 1;
  }
  if (selection > 0) showCombo.setSelectedId(selection, juce::dontSendNotification);
  else if (lyrics.showId.isNotEmpty()) showCombo.setText(lyrics.showName + "  [" + lyrics.showId + "]", juce::dontSendNotification);
  layoutCombo.clear(juce::dontSendNotification); selection = 0;
  for (size_t i = 0; i < catalogue.layouts.size(); ++i) {
    const auto &layout = catalogue.layouts[i];
    layoutCombo.addItem(layout.name + "  [" + layout.id + "]", static_cast<int>(i) + 1);
    if (layout.id == lyrics.layoutId) selection = static_cast<int>(i) + 1;
  }
  if (selection > 0) layoutCombo.setSelectedId(selection, juce::dontSendNotification);
  else if (lyrics.layoutId.isNotEmpty()) layoutCombo.setText(lyrics.layoutName + "  [" + lyrics.layoutId + "]", juce::dontSendNotification);
  updating = false;
}
void LyricsPanel::testSelected() {
  const auto *mark = findMark(selectedId);
  if (client == nullptr || mark == nullptr || !testButton.isEnabled()) return;
  client->queueTarget(mark->target);
}

void LyricsPanel::timerCallback() {
  if (client != nullptr) {
    const auto status = client->getStatus();
    const bool connected = status.state == ConnectionStatus::State::connected;
    if (status.reviewRevision != lastReviewRevision && status.reviewShowId == lyrics.showId
        && !loadingCatalogue) {
      lastReviewRevision = status.reviewRevision;
      refreshCatalogue();
    }
    connectionButton.setButtonText(connected ? juce::String::fromUTF8("● FreeShow conectado") : juce::String::fromUTF8("○ FreeShow · Conexión"));
    connectionButton.setColour(juce::TextButton::textColourOffId, connected ? AppColours::accentSuccess : UITheme::textSecondary);
    statusText = status.message;
    if (connected && status.lastSent.isNotEmpty())
      statusText = status.outputConfirmed
          ? juce::String::fromUTF8("Proyectado · confirmado por FreeShow · ") + status.lastProjected
          : juce::String::fromUTF8("Enviado · proyección sin confirmar · ") + status.lastSent;
    if (loadingCatalogue) statusText += juce::String::fromUTF8(" · Consultando catálogo…");
    else if (catalogueError.isNotEmpty()) statusText += juce::String::fromUTF8(" · ") + catalogueError;
    refreshButton.setEnabled(connected && !loadingCatalogue);
    testConnectionButton.setEnabled(status.state != ConnectionStatus::State::connecting);
  } else {
    statusText = juce::String::fromUTF8("FreeShow desconectado · Configura una conexión local para consultar shows y previews reales");
    testConnectionButton.setEnabled(false); refreshButton.setEnabled(false);
  }
  updateDetail(); map->repaint(); repaint(statusBounds);
}

bool LyricsPanel::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) { selectMark({}); return false; }
  const auto *selected = findMark(selectedId);
  if (selected != nullptr && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)) {
    const double amount = key.getModifiers().isShiftDown() ? 1.0 : 0.1;
    moveMark(selectedId, selected->seconds + (key.getKeyCode() == juce::KeyPress::leftKey ? -amount : amount), true);
    return true;
  }
  return false;
}
#endif
