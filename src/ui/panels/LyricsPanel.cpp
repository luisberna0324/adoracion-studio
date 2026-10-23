#include "LyricsPanel.h"
#include "TimelineRulerStyle.h"
#include "TimelineNavigationGestures.h"
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
    if (e.mods.isPopupMenu()) {
      if (hit.isNotEmpty()) owner.showMapMenu(e.position, hit);
      return;
    }
    owner.selectMark(hit);
    owner.draggingInsertionAnchor = hit.isEmpty();
    if (owner.draggingInsertionAnchor)
      owner.placeInsertionAnchor(owner.xToTime(e.position.x));
    else {
      owner.insertionAnchor.clear();
      owner.rebuildMapLayout();
    }
    owner.dragId = owner.hitMark(e.position, true);
    owner.dragChanged = false;
    if (const auto *mark = owner.findMark(owner.dragId)) owner.dragStartSeconds = mark->seconds;
  }
  void mouseDrag(const juce::MouseEvent &e) override {
    if (!e.mouseWasDraggedSinceMouseDown()) return;
    if (owner.draggingInsertionAnchor) {
      owner.placeInsertionAnchor(owner.xToTime(e.position.x));
      return;
    }
    if (owner.dragId.isEmpty()) return;
    owner.dragChanged = true;
    owner.moveMark(owner.dragId, owner.xToTime(e.position.x), false);
  }
  void mouseUp(const juce::MouseEvent &) override {
    if (owner.dragChanged) owner.changed();
    owner.dragId.clear();
    owner.draggingInsertionAnchor = false;
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
    if (id.isNotEmpty()) owner.showDestinationMenu(0.0, id);
  }
  void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel) override {
    if (!hasNavigationTarget(e) || owner.draggingInsertionAnchor || owner.dragId.isNotEmpty()) return;
    const auto gesture = timelineNavigation::fromWheel(wheel.deltaX, wheel.deltaY);
    if (gesture.action == timelineNavigation::WheelAction::pan) {
      owner.panTimeline(gesture.value);
    } else if (gesture.action == timelineNavigation::WheelAction::zoom) {
      owner.applyZoom(gesture.value, e.position.x);
    }
  }
  void mouseMagnify(const juce::MouseEvent &e, float amount) override {
    if (!hasNavigationTarget(e) || owner.draggingInsertionAnchor || owner.dragId.isNotEmpty()) return;
    if (amount != 0.0f) owner.applyZoom(timelineNavigation::magnifyFactor(amount), e.position.x);
  }
private:
  bool hasNavigationTarget(const juce::MouseEvent &e) const {
    const float x = e.position.x - owner.viewport.getViewPositionX();
    return x >= mapInset && x < owner.viewport.getMaximumVisibleWidth() - mapInset;
  }
  LyricsPanel &owner;
};

LyricsPanel::LyricsPanel() : map(std::make_unique<SongMap>(*this)) {
  setOpaque(true);
  setWantsKeyboardFocus(true);
  viewport.setViewedComponent(map.get(), false);
  viewport.setScrollBarsShown(true, true);
  // Keep the existing scrollbar gutter stable: changing collision lanes while
  // zooming must not change the ruler width or shift the cursor anchor.
  viewport.getVerticalScrollBar().setAutoHide(false);
  addAndMakeVisible(viewport);
  for (auto *button : {&connectionButton, &refreshButton}) addAndMakeVisible(*button);
  map->addChildComponent(addButton);
  addButton.setTooltip("Elegir grupo y diapositiva en el tiempo marcado");
  refreshButton.setTooltip("Actualizar shows y destinos FreeShow");
  addAndMakeVisible(enabledToggle);
  addAndMakeVisible(showCombo);
  showLabel.setFont(makeAppFont(10.0f, AppFontWeight::Medium));
  showLabel.setText("SHOW", juce::dontSendNotification); addAndMakeVisible(showLabel);
  showCombo.setTextWhenNothingSelected(juce::String::fromUTF8("Elegir show…"));
  connectionButton.onClick = [this] { if (onConnectionSettingsRequested) onConnectionSettingsRequested(); };
  refreshButton.onClick = [this] { refreshCatalogue(); };
  addButton.onClick = [this] {
    if (insertionAnchor.valid) showDestinationMenu(insertionAnchor.seconds);
  };
  enabledToggle.onClick = [this] { lyrics.enabled = enabledToggle.getToggleState(); changed(); };
  showCombo.onChange = [this] {
    if (updating) return;
    const int index = showCombo.getSelectedId() - 1;
    if (!juce::isPositiveAndBelow(index, static_cast<int>(shows.size()))) return;
    loadShow(shows[static_cast<size_t>(index)].id, true);
  };
  refreshThemeColours();
  startTimerHz(12);
}

LyricsPanel::~LyricsPanel() { stopTimer(); viewport.setViewedComponent(nullptr); }

void LyricsPanel::setClient(FreeShowClient *next) {
  client = next;
  lastReviewRevision = 0;
  lastConnectionEpoch = 0; wasConnected = false;
  if (client != nullptr) {
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
  insertionAnchor.clear();
  draggingInsertionAnchor = false;
  if (findMark(selectedId) == nullptr) selectedId.clear();
  enabledToggle.setToggleState(lyrics.enabled, juce::dontSendNotification);
  rebuildShowSelectors(); rebuildMapLayout(); resized(); repaint();
  if (client != nullptr && next.showId.isNotEmpty()
      && (previousShow != next.showId || catalogue.id != next.showId)) loadShow(next.showId, false);
}

void LyricsPanel::setArrangement(const SongArrangementSnapshot &next) { arrangement = next; map->repaint(); }
void LyricsPanel::setDuration(double seconds) { duration = std::isfinite(seconds) ? juce::jmax(1.0, seconds) : 1.0; rebuildMapLayout(); }
void LyricsPanel::setTransportProviders(std::function<double()> position, std::function<bool()> playing) {
  positionProvider = std::move(position); playingProvider = std::move(playing);
}
void LyricsPanel::setTimelineMarksProvider(std::function<std::vector<PracticeTimelineMark>(juce::Range<double>, float)> provider) {
  timelineMarksProvider = std::move(provider); map->repaint();
}

void LyricsPanel::refreshThemeColours() {
  for (auto *bar : {&viewport.getHorizontalScrollBar(), &viewport.getVerticalScrollBar()})
    bar->setColour(juce::ScrollBar::thumbColourId, UITheme::textSecondary.withAlpha(0.35f));
  for (auto *button : {&connectionButton, &refreshButton}) styleButton(*button);
  styleButton(addButton, true);
  showLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  applyComboBoxTheme(showCombo);
  applyToggleSwitchTheme(enabledToggle);
  repaint(); if (map != nullptr) map->repaint();
}

void LyricsPanel::paint(juce::Graphics &g) {
  g.fillAll(UITheme::panelDark);
  g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(10.5f));
  g.setColour(UITheme::dividerSoft);
  g.drawHorizontalLine(viewport.getY() - 1, 12.0f, static_cast<float>(getWidth() - 12));
}

void LyricsPanel::resized() {
  const int width = getWidth();
  auto row = juce::Rectangle<int>(12, 8, juce::jmax(0, width - 24), 28);
  connectionButton.setBounds(row.removeFromRight(175)); row.removeFromRight(8);
  enabledToggle.setBounds(row.removeFromRight(162)); row.removeFromRight(8);
  refreshButton.setBounds(row.removeFromRight(30)); row.removeFromRight(10);
  showLabel.setBounds(row.removeFromLeft(38));
  showCombo.setBounds(row.removeFromLeft(juce::jmin(row.getWidth(), 220)));
  const int top = 44;
  viewport.setBounds(6, top, juce::jmax(0, width - 12), juce::jmax(0, getHeight() - top - 6));
  rebuildMapLayout();
}

float LyricsPanel::timeToX(double seconds) const {
  return mapInset + mapOriginOffset + static_cast<float>(juce::jlimit(0.0, duration, seconds) / duration) * timelineWidth;
}
double LyricsPanel::xToTime(float x) const {
  return juce::jlimit(0.0, duration, static_cast<double>((x - mapInset - mapOriginOffset) / timelineWidth) * duration);
}

void LyricsPanel::applyZoom(double factor, float mapAnchorX) {
  if (!std::isfinite(factor) || factor <= 0.0 || factor == 1.0) return;
  syncNavigationFromViewport();
  const float anchor = (mapAnchorX - viewport.getViewPositionX() - mapInset)
      / juce::jmax(1.0f, viewport.getMaximumVisibleWidth() - 2.0f * mapInset);
  const auto next = timelineNavigation::zoomWindow({navigationStart, navigationSpan}, static_cast<float>(factor), anchor);
  setTimelineWindow(next.start, next.span);
}
void LyricsPanel::panTimeline(float deltaX) {
  syncNavigationFromViewport();
  setTimelineWindow(navigationStart + timelineNavigation::panDelta(deltaX, navigationSpan), navigationSpan);
}
void LyricsPanel::syncNavigationFromViewport() {
  const int x = viewport.getViewPositionX();
  if (lastNavigationViewX >= 0 && x != lastNavigationViewX) {
    const auto next = timelineNavigation::constrainWindow(
        navigationStart + (x - lastNavigationViewX) / timelineWidth, navigationSpan);
    navigationStart = next.start;
  }
  lastNavigationViewX = x;
}
void LyricsPanel::setTimelineWindow(float start, float span) {
  syncNavigationFromViewport();
  const auto next = timelineNavigation::constrainWindow(start, span);
  navigationStart = next.start; navigationSpan = next.span;
  zoomFactor = navigationSpan == timelineNavigation::minimumSpan ? timelineNavigation::maximumZoom : 1.0 / navigationSpan;
  rebuildMapLayout();
}
void LyricsPanel::rebuildMapLayout() {
  syncNavigationFromViewport();
  const int viewWidth = juce::jmax(100, viewport.getMaximumVisibleWidth());
  timelineWidth = (viewWidth - 2.0f * mapInset) / navigationSpan;
  const int mapWidth = static_cast<int>(std::ceil(timelineWidth + 2.0f * mapInset));
  const int oldY = viewport.getViewPositionY();
  map->setSize(mapWidth, juce::jmax(100, map->getHeight()));
  viewport.setViewPosition(juce::roundToInt(navigationStart * timelineWidth), oldY);
  lastNavigationViewX = viewport.getViewPositionX();
  // Preserve fractional navigation in the drawing and hit geometry while JUCE's
  // Viewport itself scrolls in integer pixels. Tiny trackpad events accumulate.
  mapOriginOffset = lastNavigationViewX - navigationStart * timelineWidth;
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
  map->setSize(mapWidth, juce::jmax(required, viewport.getHeight()
      - (zoomFactor > 1.0 ? viewport.getScrollBarThickness() : 0)));
  addButton.setVisible(insertionAnchor.valid && lyrics.marks.size() < markLimit);
  if (insertionAnchor.valid) {
    auto bounds = TimelineInsertionAnchor::addButtonBounds(
        timeToX(insertionAnchor.seconds), rulerHeight + 8.0f);
    bounds.setX(juce::jlimit(mapInset, static_cast<float>(mapWidth) - mapInset - bounds.getWidth(), bounds.getX()));
    addButton.setBounds(bounds.toNearestInt());
    addButton.toFront(false);
  }
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
  const float rulerLeft = juce::jmax(mapInset, static_cast<float>(viewport.getViewPositionX()));
  const float rulerRight = juce::jmin(area.getRight() - mapInset,
      static_cast<float>(viewport.getViewPositionX() + viewport.getViewWidth()));
  if (rulerRight > rulerLeft) {
    const juce::Range<double> visible{xToTime(rulerLeft), xToTime(rulerRight)};
    const auto marks = timelineMarksProvider ? timelineMarksProvider(visible, rulerRight - rulerLeft)
        : PracticeTimelineRuler::build(visible, rulerRight - rulerLeft, 120.0, "4/4");
    drawMusicalTimelineRuler(g, {rulerLeft, 0.0f, rulerRight - rulerLeft, 24.0f}, visible, duration, marks);
  }
  if (insertionAnchor.valid)
    TimelineInsertionAnchor::paint(g, timeToX(insertionAnchor.seconds), 0.0f, area.getBottom());
  if (positionProvider) {
    const float x = timeToX(positionProvider());
    drawTimelinePlayhead(g, x, 17.0f, area.getBottom(), playingProvider && playingProvider() ? 0.65f : 0.25f);
  }
  if (placements.empty()) {
    g.setColour(UITheme::textSecondary); g.setFont(makeAppFont(13.0f, AppFontWeight::Medium));
    g.drawText(juce::String::fromUTF8("Añade marcas de letra en cualquier momento de la canción"), area.toNearestInt().withTrimmedTop(50).withHeight(24),
               juce::Justification::centred);
    g.setFont(makeAppFont(11.0f));
    g.drawText("Marca un tiempo y pulsa su +. Las secciones solo sirven de referencia.",
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
juce::String LyricsPanel::reviewReason(const Mark &mark) const {
  if (mark.target.hide) return {};
  if (catalogueError.isNotEmpty()) return catalogueError;
  if (loadingCatalogue) return juce::String::fromUTF8("verificando catálogo FreeShow");
  if (mark.target.showId != lyrics.showId || (!mark.target.useActiveLayout && mark.target.layoutId != lyrics.layoutId))
    return "el destino pertenece a otro show o arreglo; vuelve a elegirlo";
  if (catalogue.id.isEmpty()) return juce::String::fromUTF8("catálogo pendiente de verificar");
  return resolve(mark.target, catalogue).error;
}
juce::String LyricsPanel::previewText(const Mark &mark) const {
  if (mark.target.useActiveLayout && catalogue.id == mark.target.showId)
    for (const auto &group : catalogue.groups) if (group.id == mark.target.groupId)
      for (const auto &slide : group.slides) if (slide.target.slideId == mark.target.slideId)
        return slide.target.text;
  if (reviewReason(mark).isEmpty() && !mark.target.hide) {
    const auto destination = resolve(mark.target, catalogue);
    for (const auto &layout : catalogue.layouts) if (layout.id == (mark.target.useActiveLayout ? catalogue.activeLayoutId : mark.target.layoutId))
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
  selectedId = id;
  for (const auto &p : placements) if (p.id == id) {
    const int top = viewport.getViewPositionY();
    if (p.preview.getBottom() > top + viewport.getViewHeight())
      viewport.setViewPosition(viewport.getViewPositionX(), juce::roundToInt(p.preview.getBottom()) - viewport.getViewHeight() + 7);
    else if (p.point.y - 10.0f < top)
      viewport.setViewPosition(viewport.getViewPositionX(), juce::jmax(0, juce::roundToInt(p.point.y) - 15));
    break;
  }
  repaint(); map->repaint();
}
void LyricsPanel::changed() {
  if (onChanged) onChanged(lyrics);
  map->repaint(); repaint();
}
void LyricsPanel::moveMark(const juce::String &id, double seconds, bool notify) {
  if (auto *mark = findMark(id)) mark->seconds = juce::jlimit(0.0, duration, seconds);
  rebuildMapLayout(); if (notify) changed();
}

void LyricsPanel::placeInsertionAnchor(double seconds) {
  insertionAnchor.place(juce::jlimit(0.0, duration, seconds));
  rebuildMapLayout();
}

juce::PopupMenu LyricsPanel::buildDestinationMenu(std::vector<Target> &targets) const {
  juce::PopupMenu menu;
  Target hide; hide.hide = true; hide.title = "Ocultar letras";
  targets.push_back(hide);
  menu.addItem(1, "Ocultar letras");
  if (catalogue.groups.empty()) {
    menu.addItem(0, "Conecta FreeShow y elige un show para consultar sus grupos", false);
    return menu;
  }
  for (const auto &group : catalogue.groups) {
    juce::PopupMenu screens;
    for (const auto &slide : group.slides) {
      targets.push_back(slide.target);
      auto label = juce::String::fromUTF8("Pantalla ") + juce::String(slide.screen);
      const auto text = firstLines(slide.target.text, 1).substring(0, 70);
      if (text.isNotEmpty()) label += juce::String::fromUTF8(" · ") + text;
      screens.addItem(static_cast<int>(targets.size()), label);
    }
    auto name = group.name.isNotEmpty() ? group.name : group.id;
    const auto sameName = std::count_if(catalogue.groups.begin(), catalogue.groups.end(),
        [&group](const SlideGroup &other) { return other.name == group.name; });
    if (sameName > 1) name += " [" + group.id.substring(0, 8) + "]";
    menu.addSubMenu(name, screens);
  }
  return menu;
}

void LyricsPanel::applyDestination(const Target &target, double seconds, const juce::String &id) {
  if (id.isEmpty() && lyrics.marks.size() >= markLimit) return;
  if (id.isNotEmpty() && findMark(id) == nullptr) return;
  if (auto *mark = findMark(id)) mark->target = target;
  else {
    Mark newMark; newMark.id = juce::Uuid().toString();
    newMark.seconds = juce::jlimit(0.0, duration, seconds); newMark.target = target;
    selectedId = newMark.id; lyrics.marks.push_back(std::move(newMark));
    insertionAnchor.clear();
  }
  rebuildMapLayout(); changed(); resized();
}

void LyricsPanel::showDestinationMenu(double seconds, const juce::String &id) {
  if (id.isEmpty() && (!insertionAnchor.valid || lyrics.marks.size() >= markLimit)) return;
  std::vector<Target> targets;
  auto menu = buildDestinationMenu(targets);
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  const auto request = catalogueRequest;
  auto anchor = getLocalArea(map.get(), addButton.getBounds());
  if (const auto *mark = findMark(id))
    anchor = getLocalArea(map.get(), juce::Rectangle<int>(juce::roundToInt(timeToX(mark->seconds)), 46, 1, 1));
  auto *parent = getTopLevelComponent();
  auto options = makeTouchPopupMenuOptions(nullptr)
      .withTargetScreenArea(parent->localAreaToGlobal(parent->getLocalArea(this, anchor)))
      .withParentComponent(parent);
  menu.showMenuAsync(options,
      [safe, id, seconds, request, targets = std::move(targets)](int result) {
        if (safe == nullptr || request != safe->catalogueRequest || result < 1
            || result > static_cast<int>(targets.size())) return;
        safe->applyDestination(targets[static_cast<size_t>(result - 1)], seconds, id);
      });
}

bool LyricsPanel::canTestMark(const Mark &mark) const {
  return mark.enabled && client != nullptr
      && client->getStatus().state == ConnectionStatus::State::connected
      && reviewReason(mark).isEmpty();
}
juce::PopupMenu LyricsPanel::buildMarkMenu(const Mark &mark) const {
  juce::PopupMenu menu;
  menu.addSectionHeader(formatTimeSeconds(mark.seconds) + juce::String::fromUTF8(" · ") + markTitle(mark));
  menu.addItem(2, juce::String::fromUTF8("Cambiar destino FreeShow…"));
  menu.addItem(7, "Probar en FreeShow", canTestMark(mark));
  menu.addSeparator();
  menu.addItem(8, "Eliminar marca");
  return menu;
}
void LyricsPanel::showMapMenu(juce::Point<float> point, const juce::String &id) {
  const auto *mark = findMark(id);
  if (mark == nullptr) return;
  selectMark(id);
  auto menu = buildMarkMenu(*mark);
  const auto safe = juce::Component::SafePointer<LyricsPanel>(this);
  const auto screen = map->localPointToGlobal(point.toInt());
  menu.showMenuAsync(makeTouchPopupMenuOptions(nullptr).withTargetScreenArea({screen.x, screen.y, 1, 1})
      .withParentComponent(getTopLevelComponent()), [safe, id](int result) {
    if (safe == nullptr || result == 0) return;
    auto *mark = safe->findMark(id); if (mark == nullptr) return;
    if (result == 2) { safe->showDestinationMenu(0.0, id); return; }
    if (result == 7) { safe->selectMark(id); safe->testSelected(); return; }
    if (result == 8) {
      safe->lyrics.marks.erase(std::remove_if(safe->lyrics.marks.begin(), safe->lyrics.marks.end(),
          [&id](const Mark &m) { return m.id == id; }), safe->lyrics.marks.end());
      if (safe->selectedId == id) safe->selectedId.clear();
    }
    safe->rebuildMapLayout(); safe->changed(); safe->resized();
  });
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
      safe->rebuildShowSelectors(); safe->map->repaint();
      if (updateBinding) safe->changed();
    });
  });
}
void LyricsPanel::rebuildShowSelectors() {
  updating = true;
  showCombo.clear(juce::dontSendNotification);
  int selection = 0;
  for (size_t i = 0; i < shows.size(); ++i) {
    showCombo.addItem(shows[i].name.isNotEmpty() ? shows[i].name : "Show sin nombre", static_cast<int>(i) + 1);
    if (shows[i].id == lyrics.showId) selection = static_cast<int>(i) + 1;
  }
  if (selection > 0) showCombo.setSelectedId(selection, juce::dontSendNotification);
  else if (lyrics.showId.isNotEmpty()) showCombo.setText(lyrics.showName.isNotEmpty() ? lyrics.showName : "Show sin nombre", juce::dontSendNotification);
  updating = false;
}
void LyricsPanel::testSelected() {
  const auto *mark = findMark(selectedId);
  if (mark == nullptr || !canTestMark(*mark)) return;
  client->queueTarget(mark->target);
}

void LyricsPanel::timerCallback() {
  if (client != nullptr) {
    const auto status = client->getStatus();
    const bool connected = status.state == ConnectionStatus::State::connected;
    const bool connectionChanged = connected && (!wasConnected || status.connectionEpoch != lastConnectionEpoch);
    wasConnected = connected; lastConnectionEpoch = status.connectionEpoch;
    if (connectionChanged && !loadingCatalogue) refreshCatalogue();
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
  } else {
    statusText = juce::String::fromUTF8("FreeShow desconectado · Configura una conexión local para consultar shows y previews reales");
    refreshButton.setEnabled(false);
  }
  connectionButton.setTooltip(statusText);
  map->repaint(); repaint();
}

bool LyricsPanel::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) {
    insertionAnchor.clear(); rebuildMapLayout(); selectMark({}); return false;
  }
  const auto *selected = findMark(selectedId);
  if (selected != nullptr && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)) {
    const double amount = key.getModifiers().isShiftDown() ? 1.0 : 0.1;
    moveMark(selectedId, selected->seconds + (key.getKeyCode() == juce::KeyPress::leftKey ? -amount : amount), true);
    return true;
  }
  return false;
}
#endif
