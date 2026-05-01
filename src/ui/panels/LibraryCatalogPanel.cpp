#include "LibraryCatalogPanel.h"

#include "../../data/Localization.h"
#include "../../data/SongLibraryStore.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kCatalogItemSideInset = 10;

const std::array<juce::Colour, 5> kSongAccentPalette = {
    juce::Colour::fromString("ff5EEAD4"), juce::Colour::fromString("ffFACC15"),
    juce::Colour::fromString("ffA855F7"), juce::Colour::fromString("ffF472B6"),
    juce::Colour::fromString("ff22C55E")};

juce::String normalizedTitle(const ImportedSongData &song) {
  return song.title.isNotEmpty() ? song.title
                                 : L10n::tr("Cancion sin titulo", "Untitled Song");
}

juce::String toSentenceCase(const juce::String &text) {
  auto normalized = text.trim().toLowerCase();
  if (normalized.isEmpty())
    return {};

  for (int i = 0; i < normalized.length(); ++i) {
    const auto ch = normalized[i];
    if (!juce::CharacterFunctions::isLetter(ch))
      continue;

    normalized = normalized.replaceSection(
        i, 1,
        juce::String::charToString(juce::CharacterFunctions::toUpperCase(ch)));
    break;
  }

  return normalized;
}

juce::String normalizedArtist(const ImportedSongData &song) {
  return song.artist.isNotEmpty() ? song.artist
                                  : L10n::tr("Sin artista", "Unknown artist");
}

int stemCountForSong(const ImportedSongData &song) {
  return static_cast<int>(std::count_if(
      song.stems.begin(), song.stems.end(),
      [](const ImportedStemData &stem) { return stem.include; }));
}

juce::String dominantCategoryForSong(const ImportedSongData &song) {
  for (const auto &stem : song.stems) {
    const auto category = stem.category.trim();
    if (category.isNotEmpty())
      return category;
  }
  return {};
}

juce::String formatDurationMMSS(const double totalSeconds) {
  const int roundedSeconds =
      juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
  const int mm = roundedSeconds / 60;
  const int ss = roundedSeconds % 60;
  return juce::String(mm).paddedLeft('0', 2) + ":" +
         juce::String(ss).paddedLeft('0', 2);
}

bool containsAnyToken(const juce::String &haystack,
                      std::initializer_list<const char *> needles) {
  const auto lower = haystack.toLowerCase();
  for (const auto *needle : needles) {
    if (lower.contains(needle))
      return true;
  }
  return false;
}

bool looksLikeWorshipSong(const ImportedSongData &song) {
  const auto searchable =
      (normalizedTitle(song) + " " + normalizedArtist(song) + " " +
       dominantCategoryForSong(song))
          .toLowerCase();
  return containsAnyToken(searchable,
                          {"ador", "worship", "gloria", "gracia", "jesus",
                           "dios", "holy", "praise", "bethel", "hillsong",
                           "elevation", "planetshakers", "miel san marcos"});
}

juce::Colour accentColourForToken(const juce::String &key,
                                  const juce::String &category,
                                  const juce::String &songId) {
  juce::String token = key.trim();
  if (token.isEmpty())
    token = category.trim();
  if (token.isEmpty())
    token = songId;
  size_t hash = 0;
  for (int i = 0; i < token.length(); ++i)
    hash = (hash * 131u) + static_cast<size_t>(token[i]);
  return kSongAccentPalette[hash % kSongAccentPalette.size()];
}

juce::String formatDurationHMSLocal(const double totalSeconds) {
  const int seconds =
      juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
  const int hh = seconds / 3600;
  const int mm = (seconds % 3600) / 60;
  const int ss = seconds % 60;
  return juce::String::formatted("%02d:%02d:%02d", hh, mm, ss);
}

} // namespace

LibraryCatalogPanel::IconButton::IconButton(const juce::String &name,
                                            const IconGlyph glyphToUse)
    : juce::Button(name), glyph(glyphToUse) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LibraryCatalogPanel::IconButton::setStyle(const bool filled,
                                               const juce::Colour iconColour) {
  filledStyle = filled;
  glyphColour = iconColour;
  repaint();
}

void LibraryCatalogPanel::IconButton::paintButton(juce::Graphics &g,
                                                  bool isMouseOverButton,
                                                  bool isButtonDown) {
  auto area = getLocalBounds().toFloat();
  auto fill = juce::Colours::transparentBlack;
  auto border = juce::Colours::transparentWhite;

  if (filledStyle) {
    fill = UITheme::cardBg.withAlpha(isButtonDown ? 0.96f : 1.0f);
    border = UITheme::cardBorder.withAlpha(isMouseOverButton ? 1.0f : 0.82f);
    if (isMouseOverButton)
      fill = UITheme::cardBgHover;
  } else if (isMouseOverButton || isButtonDown) {
    fill = UITheme::cardBgHover.withAlpha(isButtonDown ? 0.96f : 0.88f);
    border = UITheme::cardBorder.withAlpha(0.88f);
  }

  if (!fill.isTransparent()) {
    g.setColour(fill);
    g.fillRoundedRectangle(area, 10.0f);
  }
  if (!border.isTransparent()) {
    g.setColour(border);
    g.drawRoundedRectangle(area.reduced(0.5f), 10.0f, filledStyle ? 1.0f : 0.9f);
  }

  drawIconGlyph(g, area.reduced(8.0f), glyph, glyphColour.withAlpha(isEnabled() ? 1.0f : 0.45f),
                1.6f);
}

LibraryCatalogPanel::FilterChipButton::FilterChipButton(const juce::String &name,
                                                        const FilterChip chipToUse,
                                                        juce::String textToUse)
    : juce::Button(name), chip(chipToUse), labelText(std::move(textToUse)) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LibraryCatalogPanel::FilterChipButton::setLabelText(const juce::String &text) {
  if (labelText == text)
    return;
  labelText = text;
  repaint();
}

void LibraryCatalogPanel::FilterChipButton::setActive(const bool shouldBeActive) {
  if (active == shouldBeActive)
    return;
  active = shouldBeActive;
  repaint();
}

LibraryCatalogPanel::FilterChip
LibraryCatalogPanel::FilterChipButton::getChip() const {
  return chip;
}

int LibraryCatalogPanel::FilterChipButton::getPreferredWidth() const {
  return juce::jmax(82, juce::roundToInt(makeAppFont(15.0f, AppFontWeight::SemiBold)
                                             .getStringWidthFloat(labelText)) +
                             32);
}

void LibraryCatalogPanel::FilterChipButton::paintButton(juce::Graphics &g,
                                                        bool isMouseOverButton,
                                                        bool isButtonDown) {
  auto area = getLocalBounds().toFloat();
  auto fill = UITheme::cardBg;
  auto border = UITheme::cardBorder.withAlpha(0.90f);
  auto textColour = juce::Colour::fromString("ffB0B0B0");

  if (active) {
    fill = UITheme::accent.withAlpha(0.16f);
    border = UITheme::accent;
    textColour = UITheme::textPrimary;
  } else if (isMouseOverButton || isButtonDown) {
    fill = UITheme::cardBgHover;
    textColour = UITheme::textPrimary;
  }

  g.setColour(fill);
  g.fillRoundedRectangle(area, 10.0f);
  g.setColour(border);
  g.drawRoundedRectangle(area.reduced(0.5f), 10.0f, active ? 2.0f : 1.0f);
  g.setColour(textColour.withAlpha(isEnabled() ? 1.0f : 0.45f));
  g.setFont(makeAppFont(15.0f, AppFontWeight::SemiBold));
  g.drawFittedText(labelText, getLocalBounds(), juce::Justification::centred, 1);
}

LibraryCatalogPanel::FooterActionButton::FooterActionButton()
    : juce::Button("confirmSelection") {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LibraryCatalogPanel::FooterActionButton::setCount(const int nextCount) {
  if (count == nextCount)
    return;
  count = nextCount;
  repaint();
}

void LibraryCatalogPanel::FooterActionButton::paintButton(juce::Graphics &g,
                                                          bool isMouseOverButton,
                                                          bool isButtonDown) {
  auto area = getLocalBounds().toFloat();
  auto fill = UITheme::accent;
  if (!isEnabled())
    fill = fill.withAlpha(0.38f);
  else if (isButtonDown)
    fill = fill.darker(0.20f);
  else if (isMouseOverButton)
    fill = fill.brighter(0.05f);

  if (isEnabled() && isButtonDown)
    area = area.withSizeKeepingCentre(area.getWidth() * 0.985f,
                                      area.getHeight() * 0.985f);

  g.setColour(fill);
  g.fillRoundedRectangle(area, 10.0f);

  const auto buttonText =
      count > 0
          ? L10n::tr("Agregar al setlist", "Add to setlist") + " (" +
                juce::String(count) + ")"
          : L10n::tr("Agregar al setlist", "Add to setlist");

  const auto contentArea = area.reduced(12.0f, 0.0f);
  const auto font = makeAppFont(14.0f, AppFontWeight::SemiBold);
  const float plusSize = 16.0f;
  const float textWidth = font.getStringWidthFloat(buttonText);
  const float groupWidth = plusSize + 8.0f + textWidth;
  const float startX = contentArea.getCentreX() - groupWidth * 0.5f;
  const float centerY = contentArea.getCentreY();

  g.setColour(UITheme::onAccent.withAlpha(isEnabled() ? 1.0f : 0.78f));
  g.fillRoundedRectangle(startX + (plusSize * 0.5f) - 1.0f, centerY - 6.0f, 2.0f,
                         12.0f, 0.8f);
  g.fillRoundedRectangle(startX, centerY - 1.0f, plusSize, 2.0f, 0.8f);

  g.setFont(font);
  g.drawText(buttonText,
             juce::Rectangle<float>(startX + plusSize + 8.0f, centerY - 12.0f,
                                    textWidth, 24.0f),
             juce::Justification::centredLeft);
}

LibraryCatalogPanel::ChipsRow::ChipsRow(
    std::array<std::unique_ptr<FilterChipButton>, 5> &buttons,
    IconButton &advancedButton)
    : chips(buttons), filterButton(advancedButton) {
  for (auto &chip : chips)
    addAndMakeVisible(*chip);
  addAndMakeVisible(filterButton);
}

int LibraryCatalogPanel::ChipsRow::getPreferredWidth() const {
  int width = 0;
  for (const auto &chip : chips)
    width += chip->getPreferredWidth();
  width += 10 * (static_cast<int>(chips.size()) - 1);
  width += 10 + 40;
  return width;
}

void LibraryCatalogPanel::ChipsRow::resized() {
  int x = 0;
  for (auto &chip : chips) {
    const int chipWidth = chip->getPreferredWidth();
    chip->setBounds(x, 2, chipWidth, 40);
    x += chipWidth + 10;
  }
  filterButton.setBounds(x, 2, 40, 40);
}

void LibraryCatalogPanel::CatalogContent::setRows(std::vector<CatalogItem> nextRows,
                                                  const juce::StringArray &nextSelectedSongIds,
                                                  const bool nextHideOptionsButtons) {
  rows = std::move(nextRows);
  selectedSongIds = nextSelectedSongIds;
  hideOptionsButtons = nextHideOptionsButtons;
  repaint();
}

void LibraryCatalogPanel::CatalogContent::setNowPlayingSong(const juce::String &songId,
                                                            const bool isPlaying) {
  nowPlayingSongId = songId;
  playbackActive = isPlaying;
  repaint();
}

void LibraryCatalogPanel::CatalogContent::setOnLoadRequested(LoadCallback cb) {
  onLoadRequested = std::move(cb);
}

void LibraryCatalogPanel::CatalogContent::setOnToggleRequested(ToggleCallback cb) {
  onToggleRequested = std::move(cb);
}

void LibraryCatalogPanel::CatalogContent::setOnOptionsRequested(OptionsCallback cb) {
  onOptionsRequested = std::move(cb);
}

int LibraryCatalogPanel::CatalogContent::getPreferredHeight(const int width) const {
  juce::ignoreUnused(width);
  if (rows.empty())
    return 88;
  constexpr int rowStart = 8;
  constexpr int rowHeight = 72;
  constexpr int rowGap = 8;
  return rowStart + static_cast<int>(rows.size()) * (rowHeight + rowGap);
}

juce::Rectangle<int>
LibraryCatalogPanel::CatalogContent::itemBoundsForIndex(const int index) const {
  if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
    return {};
  constexpr int rowStart = 8;
  constexpr int rowHeight = 72;
  constexpr int rowGap = 8;
  return {kCatalogItemSideInset, rowStart + index * (rowHeight + rowGap),
          juce::jmax(1, getWidth() - (kCatalogItemSideInset * 2)), rowHeight};
}

juce::Rectangle<int>
LibraryCatalogPanel::CatalogContent::toggleBoundsForIndex(const int index) const {
  auto row = itemBoundsForIndex(index).reduced(16, 8);
  const int buttonSize = 32;
  return {row.getX(), row.getCentreY() - buttonSize / 2, buttonSize, buttonSize};
}

juce::Rectangle<int>
LibraryCatalogPanel::CatalogContent::menuBoundsForIndex(const int index) const {
  if (hideOptionsButtons)
    return {};
  auto row = itemBoundsForIndex(index).reduced(16, 8);
  return {row.getRight() - 40, row.getCentreY() - 20, 40, 40};
}

int LibraryCatalogPanel::CatalogContent::rowIndexAt(const juce::Point<int> point) const {
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (itemBoundsForIndex(i).contains(point))
      return i;
  }
  return -1;
}

void LibraryCatalogPanel::CatalogContent::paint(juce::Graphics &g) {
  if (rows.empty()) {
    g.setColour(UITheme::textSecondary.withAlpha(0.92f));
    g.setFont(makeAppFont(16.0f, AppFontWeight::Medium));
    g.drawFittedText(L10n::tr("No encontramos canciones con esa busqueda.",
                              "No songs matched that search."),
                     getLocalBounds().reduced(18, 32), juce::Justification::centred, 3);
    return;
  }

  const bool ultraCompact = getWidth() < 320;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const auto &item = rows[static_cast<size_t>(i)];
    auto rowBounds = itemBoundsForIndex(i);
    const bool hovered = i == hoveredRowIndex;
    const bool pendingSelected = selectedSongIds.contains(item.songId);
    const bool alreadyAdded = item.inSetlist;
    const bool nowPlaying = playbackActive && nowPlayingSongId == item.songId;

    auto fill = UITheme::cardBgHover;
    if (fill == UITheme::cardBgHover)
      fill = UITheme::cardBg;
    if (pendingSelected)
      fill = fill.interpolatedWith(UITheme::accent, 0.14f);
    if (nowPlaying)
      fill = fill.interpolatedWith(UITheme::accent, 0.11f);
    if (hovered)
      fill = fill.interpolatedWith(UITheme::cardBgHover, 0.28f);

    auto border = UITheme::cardBorder.withAlpha(0.58f * 0.42f);
    if (pendingSelected)
      border = UITheme::accent.withAlpha(0.76f);
    else if (nowPlaying)
      border = UITheme::accent.withAlpha(0.80f);

    const float rowAlpha = alreadyAdded ? 0.75f : 1.0f;
    auto rowRect = rowBounds.toFloat();
    if (hovered)
      rowRect = rowRect.withSizeKeepingCentre(rowRect.getWidth() * 0.995f, rowRect.getHeight() * 0.995f);

    juce::Path rowPath;
    rowPath.addRoundedRectangle(rowRect, 12.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.25f), 8, {0, 2})
        .drawForPath(g, rowPath);

    g.setColour(fill.withAlpha(rowAlpha));
    g.fillRoundedRectangle(rowRect, 12.0f);
    g.setColour(border.withAlpha(rowAlpha));
    g.drawRoundedRectangle(rowRect.reduced(0.5f), 12.0f, nowPlaying ? 1.5f : 1.0f);

    auto contentArea = rowBounds.reduced(16, 8);
    const auto toggleBounds = toggleBoundsForIndex(i);
    auto toggleArea = toggleBounds.toFloat();

    g.setColour(alreadyAdded ? UITheme::accent.withAlpha(0.12f)
                             : pendingSelected ? UITheme::accent.withAlpha(0.18f)
                                               : juce::Colours::transparentBlack);
    g.fillEllipse(toggleArea);
    g.setColour(alreadyAdded ? UITheme::accent.withAlpha(0.70f) : UITheme::accent);
    g.drawEllipse(toggleArea.reduced(1.0f), 2.0f);
    drawIconGlyph(g, toggleArea.reduced(7.0f),
                  (alreadyAdded || pendingSelected) ? IconGlyph::Check : IconGlyph::Add,
                  UITheme::accent, 1.9f);

    contentArea.removeFromLeft(toggleBounds.getWidth() + 12);
    auto accentBar =
        juce::Rectangle<int>(contentArea.getX(), rowBounds.getCentreY() - 20, 4, 40);
    g.setColour(accentColourForToken(item.key, item.dominantCategory, item.songId)
                    .withAlpha(rowAlpha));
    g.fillRoundedRectangle(accentBar.toFloat(), 2.0f);
    contentArea.removeFromLeft(4);
    contentArea.removeFromLeft(12);

    const auto menuBounds = menuBoundsForIndex(i);
    if (!menuBounds.isEmpty())
      contentArea.removeFromRight(menuBounds.getWidth() + 6);

    constexpr int titleHeight = 18;
    constexpr int artistHeight = 14;
    constexpr int metaHeight = 13;
    constexpr int lineGap = 2;
    const int totalTextHeight =
        titleHeight + artistHeight + metaHeight + (lineGap * 2);
    const int textStartY =
        contentArea.getY() +
        juce::jmax(0, (contentArea.getHeight() - totalTextHeight) / 2);
    auto titleArea = juce::Rectangle<int>(contentArea.getX(), textStartY,
                                          contentArea.getWidth(), titleHeight);
    auto artistArea =
        juce::Rectangle<int>(contentArea.getX(), titleArea.getBottom() + lineGap,
                             contentArea.getWidth(), artistHeight);
    auto metaArea = juce::Rectangle<int>(contentArea.getX(),
                                         artistArea.getBottom() + lineGap,
                                         contentArea.getWidth(), metaHeight);

    g.setColour(UITheme::textPrimary.withAlpha(rowAlpha));
    g.setFont(makeAppFont(17.0f, AppFontWeight::SemiBold));
    g.drawFittedText(item.title, titleArea, juce::Justification::centredLeft, 1);

    g.setColour(UITheme::textSecondary.withAlpha(0.95f * rowAlpha));
    g.setFont(makeAppFont(11.6f, AppFontWeight::Medium));
    g.drawFittedText(item.artist.toUpperCase(), artistArea,
                     juce::Justification::centredLeft, 1);

    const auto metadataText =
        (item.key.isNotEmpty() ? item.key : "-") + " • " +
        (item.bpm > 0 ? juce::String(item.bpm) : "--") + " BPM • " +
        juce::String(item.stemCount) + " stems • " + formatDurationMMSS(item.durationSeconds);
    g.setColour(UITheme::textSecondary.withAlpha(0.76f * rowAlpha));
    g.setFont(makeAppFont(11.6f, AppFontWeight::Regular));
    g.drawFittedText(metadataText, metaArea, juce::Justification::centredLeft, 1);

    if (!menuBounds.isEmpty()) {
      auto menuRect = menuBounds.toFloat();
      if (hovered)
        g.setColour(UITheme::cardBgHover.withAlpha(0.72f));
      else
        g.setColour(juce::Colours::transparentBlack);
      if (hovered)
        g.fillRoundedRectangle(menuRect, 10.0f);
      drawIconGlyph(g, menuRect.reduced(9.0f), IconGlyph::More,
                    juce::Colour::fromString("ffB0B0B0").withAlpha(rowAlpha), 1.5f);
    } else if (ultraCompact) {
      juce::ignoreUnused(ultraCompact);
    }
  }
}

void LibraryCatalogPanel::CatalogContent::mouseMove(const juce::MouseEvent &event) {
  const int nextHovered = rowIndexAt(event.position.toInt());
  if (nextHovered == hoveredRowIndex)
    return;
  hoveredRowIndex = nextHovered;
  repaint();
}

void LibraryCatalogPanel::CatalogContent::mouseExit(const juce::MouseEvent &) {
  if (hoveredRowIndex < 0)
    return;
  hoveredRowIndex = -1;
  repaint();
}

void LibraryCatalogPanel::CatalogContent::mouseUp(const juce::MouseEvent &event) {
  const auto point = event.position.toInt();
  const int rowIndex = rowIndexAt(point);
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(rows.size())))
    return;

  if (toggleBoundsForIndex(rowIndex).contains(point)) {
    if (onToggleRequested)
      onToggleRequested(rowIndex);
    return;
  }

  if (const auto menuBounds = menuBoundsForIndex(rowIndex); menuBounds.contains(point)) {
    if (onOptionsRequested)
      onOptionsRequested(rowIndex, menuBounds);
    return;
  }
}

void LibraryCatalogPanel::CatalogContent::mouseDoubleClick(
    const juce::MouseEvent &event) {
  const auto point = event.position.toInt();
  const int rowIndex = rowIndexAt(point);
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(rows.size())))
    return;

  if (toggleBoundsForIndex(rowIndex).contains(point))
    return;
  if (const auto menuBounds = menuBoundsForIndex(rowIndex);
      menuBounds.contains(point))
    return;

  if (onLoadRequested)
    onLoadRequested(rowIndex);
}

LibraryCatalogPanel::LibraryCatalogPanel() {
  backButton.setStyle(false, juce::Colours::white);
  backButton.setTooltip(L10n::tr("Volver a canciones del setlist",
                                 "Back to setlist songs"));
  backButton.onClick = [this] {
    if (onBackRequested)
      onBackRequested();
  };
  addAndMakeVisible(backButton);

  menuButton.setStyle(false, juce::Colours::white);
  menuButton.setTooltip(L10n::tr("Opciones del panel", "Panel options"));
  menuButton.onClick = [this] { openHeaderMenu(); };
  addAndMakeVisible(menuButton);

  importSongsButton.setTooltip(
      L10n::tr("Importar canciones nuevas a la biblioteca",
               "Import new songs to the library"));
  importSongsButton.onClick = [this] {
    if (onImportRequested)
      onImportRequested();
  };
  addAndMakeVisible(importSongsButton);

  searchField.setPlaceholderText(L10n::tr("Buscar cancion...", "Search song..."));
  searchField.setIconGlyph(IconGlyph::Search);
  searchField.editor().setName(
      L10n::tr("Buscar cancion en biblioteca", "Search song in library"));
  searchField.editor().setTooltip(
      L10n::tr("Buscar cancion en biblioteca", "Search song in library"));
  searchField.setOnTextChange([this](const juce::String &) { rebuildRows(); });
  addAndMakeVisible(searchField);

  content.setOnLoadRequested([this](int rowIndex) {
    if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(visibleRows.size())))
      return;
    const auto &item = visibleRows[static_cast<size_t>(rowIndex)];
    if (!juce::isPositiveAndBelow(item.songIndex, static_cast<int>(librarySongs.size())))
      return;
    selectedSongId = item.songId;
    if (onSongLoadRequested)
      onSongLoadRequested(librarySongs[static_cast<size_t>(item.songIndex)], item.songIndex);
    repaint();
  });
  content.setOnToggleRequested([this](int rowIndex) { togglePendingSelection(rowIndex); });
  content.setOnOptionsRequested([this](int rowIndex, juce::Rectangle<int> targetBounds) {
    openRowMenu(rowIndex, targetBounds);
  });

  contentViewport.setViewedComponent(&content, false);
  contentViewport.setScrollBarsShown(true, false, false, false);
  contentViewport.setScrollBarThickness(1);
  contentViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(contentViewport);

  confirmButton.setTooltip(
      L10n::tr("Agregar canciones seleccionadas", "Add selected songs"));
  confirmButton.onClick = [this] { commitPendingSelection(); };
  addAndMakeVisible(confirmButton);

  refreshThemeColours();
  refreshTexts();
  refreshFooterState();
  refreshFromStorage();
}

LibraryCatalogPanel::~LibraryCatalogPanel() = default;

void LibraryCatalogPanel::setOnSongLoadRequested(SongLoadRequested cb) {
  onSongLoadRequested = std::move(cb);
}

void LibraryCatalogPanel::setOnSongEditRequested(SongEditRequested cb) {
  onSongEditRequested = std::move(cb);
}

void LibraryCatalogPanel::setOnAddSongsToSetlist(AddSongsRequested cb) {
  onAddSongsToSetlist = std::move(cb);
}

void LibraryCatalogPanel::setOnImportRequested(ImportRequested cb) {
  onImportRequested = std::move(cb);
}

void LibraryCatalogPanel::setOnBackRequested(BackRequested cb) {
  onBackRequested = std::move(cb);
}

void LibraryCatalogPanel::setActiveSetlistId(const juce::String &setlistId) {
  const auto nextId = setlistId.trim();
  if (activeSetlistId == nextId)
    return;
  activeSetlistId = nextId;
  pendingSelectedSongIds.clear();
  refreshFooterState();
  refreshFromStorage();
}

void LibraryCatalogPanel::setNowPlayingSong(const juce::String &songId,
                                            const bool isPlaying) {
  nowPlayingSongId = songId;
  playbackActive = isPlaying;
  content.setNowPlayingSong(songId, isPlaying);
}

void LibraryCatalogPanel::refreshFromStorage() {
  const auto library = SongLibraryStore::loadLibrarySnapshot();
  librarySongs = library.songs;
  setlists = library.setlists;

  if (activeSetlistId.isEmpty())
    activeSetlistId = library.activeSetlistId;

  const SetlistData *activeSetlist = nullptr;
  for (const auto &setlist : setlists) {
    if (setlist.id == activeSetlistId) {
      activeSetlist = &setlist;
      break;
    }
  }
  if (activeSetlist == nullptr && !setlists.empty()) {
    activeSetlistId = setlists.front().id;
    activeSetlist = &setlists.front();
  }

  activeSetlistName =
      activeSetlist != nullptr ? activeSetlist->name : L10n::tr("Setlist", "Setlist");
  activeSetlistSongIds.clear();
  if (activeSetlist != nullptr) {
    for (const auto &item : activeSetlist->items)
      activeSetlistSongIds.addIfNotAlreadyThere(item.songId);
  }

  for (int i = pendingSelectedSongIds.size(); --i >= 0;) {
    const auto id = pendingSelectedSongIds[i];
    const bool stillExists = std::any_of(
        librarySongs.begin(), librarySongs.end(),
        [&id](const ImportedSongData &song) { return song.id == id; });
    if (!stillExists || activeSetlistSongIds.contains(id))
      pendingSelectedSongIds.remove(i);
  }

  rebuildRows();
}

void LibraryCatalogPanel::refreshLanguageTexts() {
  refreshTexts();
  rebuildRows();
}

void LibraryCatalogPanel::refreshThemeColours() {
  backButton.setStyle(false, juce::Colours::white);
  menuButton.setStyle(false, juce::Colours::white);
  searchField.refreshThemeColours();
  importSongsButton.setColour(juce::TextButton::buttonColourId,
                              UITheme::accent.withAlpha(0.90f));
  importSongsButton.setColour(juce::TextButton::buttonOnColourId,
                              UITheme::accentSoft.withAlpha(0.95f));
  importSongsButton.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  importSongsButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  repaint();
}

void LibraryCatalogPanel::paint(juce::Graphics &g) {
  g.setColour(UITheme::textPrimary);
  g.setFont(makeAppFont(20.0f, AppFontWeight::Bold));
  g.drawFittedText(titleTextForActiveSetlist(), headerTitleBounds,
                   juce::Justification::centredLeft, 1);

  g.setColour(UITheme::textSecondary.withAlpha(0.95f));
  g.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
  g.drawFittedText(subtitleTextForActiveSetlist(), headerSubtitleBounds,
                   juce::Justification::centredLeft, 1);
}

void LibraryCatalogPanel::resized() {
  auto bounds = getLocalBounds();

  constexpr int horizontalPadding = 0;
  constexpr int topPadding = 0;
  int headerHeight = bounds.getHeight() < 560 ? 56 : 64;
  int sectionGap = bounds.getHeight() < 560 ? 10 : 14;
  int searchHeight = bounds.getHeight() < 560 ? 36 : 40;
  int importButtonHeight = bounds.getHeight() < 560 ? 38 : 42;
  int searchToImportGap = bounds.getHeight() < 560 ? 8 : 10;
  int footerButtonHeight = bounds.getHeight() < 560 ? 42 : 44;
  int footerTopGap = bounds.getHeight() < 560 ? 8 : 10;
  int footerBottomInset = bounds.getHeight() < 560 ? 10 : 12;
  int footerHeight = footerTopGap + footerButtonHeight + footerBottomInset;
  int footerHorizontalInset = kCatalogItemSideInset;
  constexpr int minContentHeight = 120;

  int estimatedContentHeight = bounds.getHeight() - topPadding - headerHeight -
                               searchHeight - searchToImportGap -
                               importButtonHeight - footerHeight - (sectionGap * 2);
  if (estimatedContentHeight < minContentHeight) {
    int deficit = minContentHeight - estimatedContentHeight;
    const auto consumeDeficit = [&deficit](int &value, const int minValue) {
      if (deficit <= 0)
        return;
      const int delta = juce::jmin(deficit, juce::jmax(0, value - minValue));
      value -= delta;
      deficit -= delta;
    };
    consumeDeficit(footerButtonHeight, 36);
    consumeDeficit(footerTopGap, 6);
    consumeDeficit(footerBottomInset, 8);
    footerHeight = footerTopGap + footerButtonHeight + footerBottomInset;
    consumeDeficit(importButtonHeight, 34);
    consumeDeficit(searchToImportGap, 6);
    consumeDeficit(searchHeight, 44);
    consumeDeficit(headerHeight, 44);
    consumeDeficit(sectionGap, 6);
  }

  auto footerArea = bounds.removeFromBottom(footerHeight);
  auto area = bounds;
  area.reduce(horizontalPadding, 0);
  area.removeFromTop(topPadding);

  auto header = area.removeFromTop(headerHeight);
  auto headerCenterColumn = header;
  const auto backColumn = headerCenterColumn.removeFromLeft(40);
  const auto menuColumn = headerCenterColumn.removeFromRight(40);
  backButton.setBounds(backColumn.withSizeKeepingCentre(40, 40));
  menuButton.setBounds(menuColumn.withSizeKeepingCentre(40, 40));

  constexpr int titleHeight = 24;
  constexpr int subtitleHeight = 16;
  constexpr int titleSubtitleGap = 2;
  constexpr int totalTextHeight = titleHeight + subtitleHeight + titleSubtitleGap;
  const int textY = headerCenterColumn.getY() +
                    juce::jmax(0, (headerCenterColumn.getHeight() - totalTextHeight) / 2);
  headerTitleBounds = juce::Rectangle<int>(
      headerCenterColumn.getX(), textY, headerCenterColumn.getWidth(), titleHeight);
  headerSubtitleBounds = juce::Rectangle<int>(
      headerCenterColumn.getX(), textY + titleHeight + titleSubtitleGap,
      headerCenterColumn.getWidth(), subtitleHeight);

  area.removeFromTop(sectionGap);
  importSongsButton.setBounds(
      area.removeFromTop(importButtonHeight).reduced(kCatalogItemSideInset, 0));
  area.removeFromTop(searchToImportGap);
  searchField.setBounds(
      area.removeFromTop(searchHeight).reduced(kCatalogItemSideInset, 0));

  area.removeFromTop(sectionGap);
  contentViewport.setBounds(area);
  updateContentSize();

  footerArea.removeFromTop(footerTopGap);
  footerArea.removeFromBottom(footerBottomInset);
  confirmButton.setBounds(footerArea.withTrimmedLeft(footerHorizontalInset)
                              .withTrimmedRight(footerHorizontalInset)
                              .withHeight(footerButtonHeight));
}

void LibraryCatalogPanel::rebuildRows() {
  visibleRows.clear();
  visibleRows.reserve(librarySongs.size());

  for (size_t i = 0; i < librarySongs.size(); ++i) {
    const auto &song = librarySongs[i];
    if (!matchesSearch(song))
      continue;

    CatalogItem item;
    item.songIndex = static_cast<int>(i);
    item.songId = song.id;
    item.title = toSentenceCase(normalizedTitle(song));
    item.artist = normalizedArtist(song);
    item.key = song.key;
    item.dominantCategory = dominantCategoryForSong(song);
    item.durationSeconds = estimateSongLengthSeconds(song);
    item.createdMs = song.createdMs;
    item.bpm = song.bpm;
    item.stemCount = stemCountForSong(song);
    item.setlistUsageCount = usageCountForSongId(song.id);
    item.inSetlist = item.songId.isNotEmpty() && activeSetlistSongIds.contains(item.songId);
    item.isNowPlaying =
        playbackActive && nowPlayingSongId.isNotEmpty() && item.songId == nowPlayingSongId;
    visibleRows.push_back(std::move(item));
  }

  const auto selectionPriority = [](const CatalogItem &item) {
    return item.inSetlist ? 1 : 0;
  };

  const auto compareWithinGroup = [this](const CatalogItem &a,
                                         const CatalogItem &b) {
    switch (sortMode) {
    case SortMode::TitleDesc:
      return a.title.compareIgnoreCase(b.title) > 0;
    case SortMode::RecentFirst:
      if (a.createdMs != b.createdMs)
        return a.createdMs > b.createdMs;
      return a.title.compareIgnoreCase(b.title) < 0;
    case SortMode::BpmAsc:
      if (a.bpm != b.bpm)
        return a.bpm < b.bpm;
      return a.title.compareIgnoreCase(b.title) < 0;
    case SortMode::BpmDesc:
      if (a.bpm != b.bpm)
        return a.bpm > b.bpm;
      return a.title.compareIgnoreCase(b.title) < 0;
    case SortMode::TitleAsc:
    default:
      return a.title.compareIgnoreCase(b.title) < 0;
    }
  };

  std::sort(visibleRows.begin(), visibleRows.end(),
            [&](const CatalogItem &a, const CatalogItem &b) {
              const int aPriority = selectionPriority(a);
              const int bPriority = selectionPriority(b);
              if (aPriority != bPriority)
                return aPriority < bPriority;
              return compareWithinGroup(a, b);
            });

  content.setRows(visibleRows, pendingSelectedSongIds, false);
  content.setNowPlayingSong(nowPlayingSongId, playbackActive);
  refreshFooterState();
  updateContentSize();
  repaint();
}

void LibraryCatalogPanel::updateContentSize() {
  const int width = juce::jmax(1, contentViewport.getWidth());
  const int height = juce::jmax(contentViewport.getHeight(), content.getPreferredHeight(width));
  content.setSize(width, height);
}

void LibraryCatalogPanel::refreshFilterButtonStates() {
  activeFilter = FilterChip::All;
}

void LibraryCatalogPanel::refreshFooterState() {
  const int pendingCount = pendingSelectedSongIds.size();
  confirmButton.setCount(pendingCount);
  confirmButton.setEnabled(pendingCount > 0 && activeSetlistId.isNotEmpty());
  confirmButton.setTooltip(
      pendingCount > 0 ? L10n::tr("Agregar canciones seleccionadas",
                                  "Add selected songs")
                       : L10n::tr("Selecciona al menos una cancion",
                                  "Select at least one song"));
}

void LibraryCatalogPanel::refreshTexts() {
  searchField.setPlaceholderText(L10n::tr("Buscar cancion...", "Search song..."));
  importSongsButton.setButtonText(
      L10n::tr("+ Importar canciones", "+ Import songs"));
}

void LibraryCatalogPanel::openHeaderMenu() {
  juce::PopupMenu menu;
  menu.addItem(1, L10n::tr("Importar canciones", "Import songs"));
  menu.addSeparator();
  menu.addItem(2, L10n::tr("Ordenar A-Z", "Sort A-Z"), true,
               sortMode == SortMode::TitleAsc);
  menu.addItem(3, L10n::tr("Ordenar Z-A", "Sort Z-A"), true,
               sortMode == SortMode::TitleDesc);
  menu.addItem(4, L10n::tr("Mas recientes", "Most recent"), true,
               sortMode == SortMode::RecentFirst);
  menu.addItem(5, L10n::tr("BPM ascendente", "BPM ascending"), true,
               sortMode == SortMode::BpmAsc);
  menu.addItem(6, L10n::tr("BPM descendente", "BPM descending"), true,
               sortMode == SortMode::BpmDesc);
  menu.addSeparator();
  menu.addItem(7, L10n::tr("Limpiar seleccion", "Clear selection"),
               pendingSelectedSongIds.size() > 0);

  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton),
                     [this](int result) {
                       if (result == 1) {
                         if (onImportRequested)
                           onImportRequested();
                         return;
                       }
                       if (result >= 2 && result <= 6) {
                         sortMode = static_cast<SortMode>(result - 2);
                         rebuildRows();
                         return;
                       }
                       if (result == 7) {
                         pendingSelectedSongIds.clear();
                         rebuildRows();
                       }
                     });
}

void LibraryCatalogPanel::openAdvancedFilterMenu() {
  juce::PopupMenu menu;
  menu.addSectionHeader(L10n::tr("Orden", "Sorting"));
  menu.addItem(10, L10n::tr("Titulo A-Z", "Title A-Z"), true,
               sortMode == SortMode::TitleAsc);
  menu.addItem(11, L10n::tr("Titulo Z-A", "Title Z-A"), true,
               sortMode == SortMode::TitleDesc);
  menu.addItem(12, L10n::tr("Mas recientes", "Most recent"), true,
               sortMode == SortMode::RecentFirst);
  menu.addItem(13, L10n::tr("BPM ascendente", "BPM ascending"), true,
               sortMode == SortMode::BpmAsc);
  menu.addItem(14, L10n::tr("BPM descendente", "BPM descending"), true,
               sortMode == SortMode::BpmDesc);
  menu.addSeparator();
  menu.addItem(20, L10n::tr("Limpiar chips", "Clear chips"),
               activeFilter != FilterChip::All);

  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(&advancedFilterButton),
      [this](int result) {
        if (result >= 10 && result <= 14) {
          sortMode = static_cast<SortMode>(result - 10);
          rebuildRows();
        } else if (result == 20) {
          activeFilter = FilterChip::All;
          refreshFilterButtonStates();
          rebuildRows();
        }
      });
}

void LibraryCatalogPanel::openRowMenu(const int rowIndex,
                                      juce::Rectangle<int> targetBounds) {
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(visibleRows.size())))
    return;
  const auto &item = visibleRows[static_cast<size_t>(rowIndex)];

  juce::PopupMenu menu;
  menu.addItem(1, L10n::tr("Ver detalles", "View details"));
  menu.addItem(2, L10n::tr("Escuchar preview", "Listen preview"));
  menu.addItem(3, L10n::tr("Ver stems", "View stems"));
  menu.addItem(4, L10n::tr("Editar cancion", "Edit song"));
  if (item.inSetlist)
    menu.addSeparator();
  if (item.inSetlist)
    menu.addItem(5, L10n::tr("Ya agregada", "Already added"), false, true);

  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(targetBounds)),
      [this, rowIndex](int result) {
        if (result == 4 && onSongEditRequested != nullptr &&
            juce::isPositiveAndBelow(rowIndex, static_cast<int>(visibleRows.size()))) {
          const auto &targetItem = visibleRows[static_cast<size_t>(rowIndex)];
          if (targetItem.songId.isNotEmpty())
            onSongEditRequested(targetItem.songId);
          return;
        }

        if (result >= 1 && result <= 3 && onSongLoadRequested != nullptr &&
            juce::isPositiveAndBelow(rowIndex, static_cast<int>(visibleRows.size()))) {
          const auto &targetItem = visibleRows[static_cast<size_t>(rowIndex)];
          if (juce::isPositiveAndBelow(targetItem.songIndex,
                                       static_cast<int>(librarySongs.size())))
            onSongLoadRequested(
                librarySongs[static_cast<size_t>(targetItem.songIndex)],
                targetItem.songIndex);
        }
      });
}

void LibraryCatalogPanel::togglePendingSelection(const int rowIndex) {
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(visibleRows.size())))
    return;
  const auto &item = visibleRows[static_cast<size_t>(rowIndex)];
  if (item.songId.isEmpty() || item.inSetlist)
    return;

  if (pendingSelectedSongIds.contains(item.songId))
    pendingSelectedSongIds.removeString(item.songId);
  else
    pendingSelectedSongIds.addIfNotAlreadyThere(item.songId);

  rebuildRows();
}

void LibraryCatalogPanel::commitPendingSelection() {
  if (pendingSelectedSongIds.isEmpty() || onAddSongsToSetlist == nullptr)
    return;
  onAddSongsToSetlist(pendingSelectedSongIds);
  pendingSelectedSongIds.clear();
  refreshFooterState();
}

bool LibraryCatalogPanel::matchesFilter(const ImportedSongData &song,
                                        const int usageCount) const {
  switch (activeFilter) {
  case FilterChip::Recent:
    return song.createdMs > 0 &&
           song.createdMs >= (juce::Time::getCurrentTime().toMilliseconds() -
                              static_cast<int64_t>(1000LL * 60 * 60 * 24 * 45));
  case FilterChip::Favorites:
    return usageCount >= 1;
  case FilterChip::Worship:
    return looksLikeWorshipSong(song);
  case FilterChip::Fast:
    return song.bpm >= 120;
  case FilterChip::All:
  default:
    return true;
  }
}

bool LibraryCatalogPanel::matchesSearch(const ImportedSongData &song) const {
  const auto query = searchField.getText().trim().toLowerCase();
  if (query.isEmpty())
    return true;

  const auto searchable =
      (normalizedTitle(song) + " " + normalizedArtist(song) + " " + song.key +
       " " + dominantCategoryForSong(song))
          .toLowerCase();
  return searchable.contains(query);
}

int LibraryCatalogPanel::usageCountForSongId(const juce::String &songId) const {
  int count = 0;
  for (const auto &setlist : setlists) {
    const bool contains = std::any_of(
        setlist.items.begin(), setlist.items.end(),
        [&songId](const SetlistItem &item) { return item.songId == songId; });
    if (contains)
      ++count;
  }
  return count;
}

juce::String LibraryCatalogPanel::titleTextForActiveSetlist() const {
  return L10n::tr("Agregar a: ", "Add to: ") + activeSetlistName;
}

juce::String LibraryCatalogPanel::subtitleTextForActiveSetlist() const {
  double totalSeconds = 0.0;
  for (const auto &song : librarySongs) {
    if (!activeSetlistSongIds.contains(song.id))
      continue;
    totalSeconds += estimateSongLengthSeconds(song);
  }

  return juce::String(activeSetlistSongIds.size()) + " " +
         L10n::tr("canciones actuales", "current songs") + " • " +
         formatDurationHMSLocal(totalSeconds);
}
