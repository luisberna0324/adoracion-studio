#include "SongListPanel.h"

#include "SongImportWizardComponent.h"
#include "../../PluginEditor.h"
#include "../../data/AppSettings.h"
#include "../../data/Localization.h"
#include "../../data/SessionAuthLogger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
juce::String makeAutoPlayNextMarkKey(const juce::String &setlistId,
                                     const juce::String &songId) {
  if (setlistId.isEmpty() || songId.isEmpty())
    return {};
  return setlistId + "|" + songId;
}

const std::array<juce::Colour, 8> kSetlistAccentPalette = {
    juce::Colour::fromRGB(129, 214, 98),  juce::Colour::fromRGB(251, 208, 70),
    juce::Colour::fromRGB(172, 121, 255), juce::Colour::fromRGB(125, 165, 255),
    juce::Colour::fromRGB(153, 225, 125), juce::Colour::fromRGB(255, 170, 95),
    juce::Colour::fromRGB(237, 122, 205), juce::Colour::fromRGB(118, 227, 222)};

juce::String toCapitalCase(const juce::String &text) {
  const auto normalized = text.trim().toLowerCase();
  if (normalized.isEmpty())
    return {};

  juce::String result;
  result.preallocateBytes(normalized.getNumBytesAsUTF8());

  bool capitalizeNext = true;
  for (int i = 0; i < normalized.length(); ++i) {
    const auto ch = normalized[i];
    if (juce::CharacterFunctions::isLetter(ch)) {
      result << juce::String::charToString(
          capitalizeNext ? juce::CharacterFunctions::toUpperCase(ch) : ch);
      capitalizeNext = false;
      continue;
    }

    result << juce::String::charToString(ch);
    capitalizeNext = !juce::CharacterFunctions::isLetterOrDigit(ch);
  }

  return result;
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
} // namespace

SongListPanel::HeaderIconButton::HeaderIconButton(const juce::String &name,
                                                   const IconGlyph glyphToUse)
    : juce::Button(name), glyph(glyphToUse) {}

void SongListPanel::HeaderIconButton::setAccent(const bool shouldAccent) {
  accent = shouldAccent;
}

void SongListPanel::HeaderIconButton::paintButton(juce::Graphics &g,
                                                  bool isMouseOverButton,
                                                  bool isButtonDown) {
  auto b = getLocalBounds().toFloat();

  if (isMouseOverButton || isButtonDown) {
    auto fill = UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.20f);
    if (isButtonDown)
      fill = fill.darker(0.04f);
    g.setColour(fill.withAlpha(0.88f));
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.65f));
    g.drawRoundedRectangle(b, 5.0f, 1.0f);
  }

  const auto glyphColour = accent ? UITheme::accent : UITheme::textPrimary;
  drawIconGlyph(g, b.withSizeKeepingCentre(24.0f, 24.0f), glyph, glyphColour,
                1.8f);
}

SongListPanel::SwipeHintHandle::SwipeHintHandle(
    const Direction directionToUse)
    : direction(directionToUse) {
  setRepaintsOnMouseActivity(true);
  setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void SongListPanel::SwipeHintHandle::setHintVisible(const bool shouldShowHint) {
  if (hintVisible == shouldShowHint)
    return;
  hintVisible = shouldShowHint;
  repaint();
}

void SongListPanel::SwipeHintHandle::setOnGestureBegin(
    std::function<void(const juce::MouseEvent &)> cb) {
  onGestureBegin = std::move(cb);
}

void SongListPanel::SwipeHintHandle::setOnGestureDrag(
    std::function<void(const juce::MouseEvent &)> cb) {
  onGestureDrag = std::move(cb);
}

void SongListPanel::SwipeHintHandle::setOnGestureEnd(
    std::function<void(const juce::MouseEvent &)> cb) {
  onGestureEnd = std::move(cb);
}

void SongListPanel::SwipeHintHandle::paint(juce::Graphics &g) {
  if (!hintVisible && !isMouseOverOrDragging())
    return;

  const float alpha = isMouseOverOrDragging() ? 0.80f : 0.48f;
  auto area = getLocalBounds().toFloat().reduced(1.5f);
  g.setColour(UITheme::cardBgHover.withAlpha(alpha * 0.18f));
  g.fillRoundedRectangle(area, 6.0f);
  g.setColour(UITheme::cardBorder.withAlpha(alpha * 0.34f));
  g.drawRoundedRectangle(area, 6.0f, 1.0f);

  const auto glyph = direction == Direction::Right ? IconGlyph::Next
                                                   : IconGlyph::SeekBack;
  drawIconGlyph(g, area.reduced(4.0f, 14.0f),
                glyph, UITheme::textSecondary.withAlpha(alpha), 1.45f);
}

void SongListPanel::SwipeHintHandle::mouseDown(const juce::MouseEvent &event) {
  if (onGestureBegin)
    onGestureBegin(event.getEventRelativeTo(getParentComponent()));
}

void SongListPanel::SwipeHintHandle::mouseDrag(const juce::MouseEvent &event) {
  if (onGestureDrag)
    onGestureDrag(event.getEventRelativeTo(getParentComponent()));
}

void SongListPanel::SwipeHintHandle::mouseUp(const juce::MouseEvent &event) {
  if (onGestureEnd)
    onGestureEnd(event.getEventRelativeTo(getParentComponent()));
}

class SongListPanel::SetlistsContent final : public juce::Component {
public:
  struct Row {
    juce::String id;
    juce::String name;
    int songCount = 0;
    double totalDurationSeconds = 0.0;
  };

  void setRows(std::vector<Row> nextRows, const juce::String &activeId) {
    rows = std::move(nextRows);
    selectedSetlistId = activeId;
    repaint();
  }

  int getPreferredHeight() const {
    if (rows.empty())
      return 120;
    constexpr int rowStart = 8;
    constexpr int rowHeight = 72;
    constexpr int rowGap = 8;
    return rowStart + static_cast<int>(rows.size()) * (rowHeight + rowGap);
  }

  std::function<void(const juce::String &)> onSetlistChosen;
  std::function<void(const juce::String &, juce::Rectangle<int>)>
      onSetlistOptionsRequested;

  void paint(juce::Graphics &g) override {
    if (rows.empty()) {
      g.setColour(UITheme::textSecondary.withAlpha(0.9f));
      g.setFont(makeAppFont(13.0f, AppFontWeight::Regular));
      g.drawFittedText(L10n::tr("Aun no hay setlists.", "No setlists yet."),
                       juce::Rectangle<int>(12, 12, getWidth() - 24, 96),
                       juce::Justification::centred, 2);
    }

    constexpr int rowHeight = 72;
    constexpr int rowGap = 8;
    constexpr int rowStart = 8;
    constexpr int rowSideInset = 10;

    for (size_t i = 0; i < rows.size(); ++i) {
      auto row = juce::Rectangle<int>(
          rowSideInset, rowStart + static_cast<int>(i) * (rowHeight + rowGap),
          getWidth() - (rowSideInset * 2), rowHeight);
      const bool active = rows[i].id == selectedSetlistId;
      auto fill = UITheme::cardBg;
      if (active)
        fill = fill.interpolatedWith(UITheme::accent, 0.14f);

      juce::Path rowPath;
      rowPath.addRoundedRectangle(row.toFloat(), 12.0f);
      juce::DropShadow(juce::Colours::black.withAlpha(0.25f), 8, {0, 2})
          .drawForPath(g, rowPath);

      g.setColour(fill.withAlpha(1.0f));
      g.fillRoundedRectangle(row.toFloat(), 12.0f);

      g.setColour((active ? UITheme::accent.withAlpha(0.82f)
                          : UITheme::cardBorder.withAlpha(0.58f * 0.42f)));
      g.drawRoundedRectangle(row.toFloat(), 12.0f, 1.0f);

      auto c = row.reduced(16, 8);
      auto accentRect = juce::Rectangle<int>(c.getX(), row.getCentreY() - 20, 4, 40);
      c.removeFromLeft(4);
      c.removeFromLeft(10);

      const auto accentColour =
          kSetlistAccentPalette[i % kSetlistAccentPalette.size()];
      g.setColour(accentColour.withAlpha(0.92f));
      g.fillRoundedRectangle(accentRect.toFloat(), 2.0f);

      c.removeFromLeft(2);
      auto optionsRect = c.removeFromRight(40);
      c.removeFromRight(6);

      constexpr int titleHeight = 18;
      constexpr int artistHeight = 14;
      constexpr int metaHeight = 13;
      constexpr int lineGap = 2;
      const int totalTextHeight =
          titleHeight + artistHeight + metaHeight + (lineGap * 2);
      const int textStartY =
          c.getY() + juce::jmax(0, (c.getHeight() - totalTextHeight) / 2);

      auto titleRect =
          juce::Rectangle<int>(c.getX(), textStartY, c.getWidth(), titleHeight);
      auto artistRect = juce::Rectangle<int>(c.getX(), titleRect.getBottom() + lineGap,
                                             c.getWidth(), artistHeight);
      auto metaRect = juce::Rectangle<int>(c.getX(), artistRect.getBottom() + lineGap,
                                           c.getWidth(), metaHeight);

      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(17.0f, AppFontWeight::SemiBold));
      g.drawFittedText(rows[i].name, titleRect, juce::Justification::centredLeft, 1);

      g.setColour(UITheme::textSecondary.withAlpha(0.95f));
      g.setFont(makeAppFont(11.6f, AppFontWeight::Medium));
      g.drawFittedText(
          juce::String(rows[i].songCount) + " " +
              L10n::tr("canciones", "songs").toUpperCase(),
          artistRect, juce::Justification::centredLeft, 1);

      g.setColour(UITheme::textSecondary.withAlpha(0.76f));
      g.setFont(makeAppFont(11.6f, AppFontWeight::Regular));
      g.drawFittedText(
          L10n::tr("Duracion ", "Duration ") +
              formatDurationHMS(rows[i].totalDurationSeconds),
          metaRect, juce::Justification::centredLeft, 1);

      drawOptionsButton(g, optionsRect.withSizeKeepingCentre(20, 20));
    }

  }

  void mouseUp(const juce::MouseEvent &event) override {
    const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
    if (!juce::isPositiveAndBelow(idx, static_cast<int>(rows.size())))
      return;
    const auto rowBounds = rowBoundsAt(idx);
    const auto optionsBounds = optionsButtonBoundsForRow(rowBounds);
    if (optionsBounds.contains(event.position.toInt())) {
      if (onSetlistOptionsRequested) {
        onSetlistOptionsRequested(
            rows[static_cast<size_t>(idx)].id,
            getLocalArea(this, optionsBounds.expanded(2)));
      }
      return;
    }
  }

  void mouseDoubleClick(const juce::MouseEvent &event) override {
    const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
    if (!juce::isPositiveAndBelow(idx, static_cast<int>(rows.size())))
      return;

    const auto rowBounds = rowBoundsAt(idx);
    if (optionsButtonBoundsForRow(rowBounds).contains(event.position.toInt()))
      return;

    if (onSetlistChosen)
      onSetlistChosen(rows[static_cast<size_t>(idx)].id);
  }

private:
  juce::Rectangle<int> rowBoundsAt(const int rowIndex) const {
    constexpr int rowHeight = 72;
    constexpr int rowGap = 8;
    constexpr int rowStart = 8;
    constexpr int rowSideInset = 10;
    return juce::Rectangle<int>(
        rowSideInset, rowStart + rowIndex * (rowHeight + rowGap),
        getWidth() - (rowSideInset * 2), rowHeight);
  }

  juce::Rectangle<int>
  optionsButtonBoundsForRow(const juce::Rectangle<int> &rowBounds) const {
    auto c = rowBounds.reduced(16, 8);
    c.removeFromLeft(4);
    c.removeFromLeft(12);
    auto optionsRect = c.removeFromRight(40);
    return optionsRect;
  }

  static void drawOptionsButton(juce::Graphics &g, juce::Rectangle<int> bounds) {
    auto b = bounds.toFloat();
    drawIconGlyph(g, b, IconGlyph::More, UITheme::textSecondary, 1.4f);
  }

  static juce::String formatDurationHMS(const double totalSeconds) {
    const int seconds =
        juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
    const int hh = seconds / 3600;
    const int mm = (seconds % 3600) / 60;
    const int ss = seconds % 60;
    return juce::String::formatted("%02d:%02d:%02d", hh, mm, ss);
  }

  int rowIndexAtY(const int y) const {
    if (y < 8)
      return -1;
    const int rel = y - 8;
    constexpr int rowHeight = 72;
    constexpr int rowGap = 8;
    const int step = rowHeight + rowGap;
    const int index = rel / step;
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
      return -1;
    if ((rel % step) >= rowHeight)
      return -1;
    return index;
  }

  std::vector<Row> rows;
  juce::String selectedSetlistId;
};


SongListPanel::SongListPanel() {
  selectedSongId = AppSettings::getLastSelectedSongId();

  titleLabel.setJustificationType(juce::Justification::centredLeft);
  titleLabel.setFont(makeAppFont(18.5f, AppFontWeight::SemiBold));
  titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  addAndMakeVisible(titleLabel);

  setlistsHeaderLabel.setJustificationType(juce::Justification::centredLeft);
  setlistsHeaderLabel.setFont(makeAppFont(20.0f, AppFontWeight::Bold));
  setlistsHeaderLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  setlistsHeaderLabel.setText(L10n::tr("Repertorio", "Repertoire"),
                              juce::dontSendNotification);
  addAndMakeVisible(setlistsHeaderLabel);

  setlistsSubtitleLabel.setJustificationType(juce::Justification::centredLeft);
  setlistsSubtitleLabel.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
  setlistsSubtitleLabel.setColour(juce::Label::textColourId,
                                  UITheme::textSecondary.withAlpha(0.95f));
  setlistsSubtitleLabel.setText(L10n::tr("Mis setlists", "My setlists"),
                                juce::dontSendNotification);
  addAndMakeVisible(setlistsSubtitleLabel);

  backButton.onClick = [this] { showSetlistsView(); };
  addAndMakeVisible(backButton);

  headerMenuButton.onClick = [this] { openHeaderMenu(); };
  addAndMakeVisible(headerMenuButton);

  createSetlistButton.onClick = [this] { createSetlist(); };
  addAndMakeVisible(createSetlistButton);

  setlistSearchField.setPlaceholderText(
      L10n::tr("Buscar setlist...", "Search setlist..."));
  setlistSearchField.setIconGlyph(IconGlyph::Search);
  setlistSearchField.setOnTextChange([this](const juce::String &text) {
    setlistSearchQuery = text.trim().toLowerCase();
    refreshFromStorage();
  });
  addAndMakeVisible(setlistSearchField);

  songsHeaderPanel.setOnAddSongsClicked([this] { openLibraryBrowser(); });
  songsHeaderPanel.setOnSongsClicked([this] { listViewport.setViewPosition(0, 0); });
  addAndMakeVisible(songsHeaderPanel);

  listViewport.setViewedComponent(&listContent, false);
  listViewport.setScrollBarsShown(true, false, false, false);
  listViewport.setScrollBarThickness(1);
  listViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(listViewport);

  libraryCatalogPanel.setOnSongLoadRequested(
      [this](const ImportedSongData &song, int songIndex) {
        juce::ignoreUnused(songIndex);
        selectedSongId = song.id;
        AppSettings::setLastSelectedSongId(selectedSongId);
        if (onSongActivated != nullptr) {
          const int indexForSetlist = getSongIndexById(song.id);
          onSongActivated(song, indexForSetlist >= 0 ? indexForSetlist : -1);
        }
      });
  libraryCatalogPanel.setOnAddSongsToSetlist([this](const juce::StringArray &songIds) {
    if (activeSetlistId.isEmpty() || songIds.isEmpty())
      return;
    for (const auto &songId : songIds) {
      if (songId.isEmpty())
        continue;
      SongLibraryStore::addSongToSetlist(activeSetlistId, songId);
    }

    refreshFromStorage();
    showSongsSubview(true);
  });
  libraryCatalogPanel.setOnImportRequested([this] { openSongImportWizard(); });
  libraryCatalogPanel.setOnSongEditRequested(
      [this](const juce::String &songId) { openSongEditWizard(songId); });
  libraryCatalogPanel.setOnBackRequested([this] { showSongsSubview(true); });
  addAndMakeVisible(libraryCatalogPanel);
  setlistsContent = std::make_unique<SetlistsContent>();
  setlistsContent->onSetlistChosen = [this](const juce::String &setlistId) {
    showSongsView(setlistId);
  };
  setlistsContent->onSetlistOptionsRequested =
      [this](const juce::String &setlistId, juce::Rectangle<int> targetArea) {
        openSetlistOptionsMenu(setlistId, targetArea);
      };
  setlistsViewport.setViewedComponent(setlistsContent.get(), false);
  setlistsViewport.setScrollBarsShown(true, false, false, false);
  setlistsViewport.setScrollBarThickness(1);
  addAndMakeVisible(setlistsViewport);

  listContent.setOnSongActivated([this](int songIndex) {
    if (!juce::isPositiveAndBelow(songIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    setSelectedSongIndex(songIndex);

    const int libraryIndex = activeSongIndices[static_cast<size_t>(songIndex)];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      return;
    if (onSongActivated)
      onSongActivated(librarySongs[static_cast<size_t>(libraryIndex)],
                      songIndex);
  });
  listContent.setOnSongsReordered([this](int fromIndex, int toIndex) {
    if (activeSetlistId.isEmpty())
      return;
    if (fromIndex == toIndex)
      return;
    if (!juce::isPositiveAndBelow(fromIndex,
                                  static_cast<int>(activeSongIndices.size())) ||
        !juce::isPositiveAndBelow(toIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    if (SongLibraryStore::moveSongInSetlist(activeSetlistId, fromIndex,
                                            toIndex)) {
      refreshFromStorage();
    }
  });
  listContent.setOnAutoPlayNextToggled([this](int songIndex) {
    if (!juce::isPositiveAndBelow(songIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    const int libraryIndex = activeSongIndices[static_cast<size_t>(songIndex)];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      return;
    const auto songId = librarySongs[static_cast<size_t>(libraryIndex)].id;
    const auto key = makeAutoPlayNextMarkKey(activeSetlistId, songId);
    if (key.isEmpty())
      return;
    if (autoPlayNextMarks.contains(key)) {
      autoPlayNextMarks.removeString(key);
    } else {
      autoPlayNextMarks.addIfNotAlreadyThere(key);
    }
    refreshFromStorage();
  });
  listContent.setOnSongRemoveRequested([this](int songIndex) {
    if (activeSetlistId.isEmpty())
      return;
    if (!juce::isPositiveAndBelow(songIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    const int libraryIndex = activeSongIndices[static_cast<size_t>(songIndex)];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      return;
    const auto songId = librarySongs[static_cast<size_t>(libraryIndex)].id;
    if (songId.isEmpty())
      return;
    SongLibraryStore::removeSongFromSetlist(activeSetlistId, songId);
    autoPlayNextMarks.removeString(
        makeAutoPlayNextMarkKey(activeSetlistId, songId));
    refreshFromStorage();
  });
  listContent.setOnAddSongsRequested([this] { openAddFromLibraryMenu(); });
  refreshThemeColours();
  refreshFromStorage();
}

SongListPanel::~SongListPanel() = default;

void SongListPanel::setOnSongActivated(
    std::function<void(const ImportedSongData &, int)> cb) {
  onSongActivated = std::move(cb);
}

void SongListPanel::setOnSetlistNameChanged(
    std::function<void(const juce::String &)> cb) {
  onSetlistNameChanged = std::move(cb);
}

int SongListPanel::getSongCount() const {
  return static_cast<int>(activeSongIndices.size());
}

const ImportedSongData *SongListPanel::getSongPtr(int index) const {
  if (!juce::isPositiveAndBelow(index,
                                static_cast<int>(activeSongIndices.size())))
    return nullptr;
  const int libraryIndex = activeSongIndices[static_cast<size_t>(index)];
  if (!juce::isPositiveAndBelow(libraryIndex,
                                static_cast<int>(librarySongs.size())))
    return nullptr;
  return &librarySongs[static_cast<size_t>(libraryIndex)];
}

int SongListPanel::getSongIndexById(const juce::String &songId) const {
  if (songId.isEmpty())
    return -1;

  for (size_t i = 0; i < activeSongIndices.size(); ++i) {
    const int libraryIndex = activeSongIndices[i];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      continue;
    if (librarySongs[static_cast<size_t>(libraryIndex)].id == songId)
      return static_cast<int>(i);
  }

  return -1;
}

void SongListPanel::setSelectedSongIndex(const int songIndex) {
  if (!juce::isPositiveAndBelow(songIndex,
                                static_cast<int>(activeSongIndices.size())))
    return;

  const int libraryIndex = activeSongIndices[static_cast<size_t>(songIndex)];
  if (!juce::isPositiveAndBelow(libraryIndex,
                                static_cast<int>(librarySongs.size())))
    return;

  selectedSongId = librarySongs[static_cast<size_t>(libraryIndex)].id;
  lastActivatedIndex = songIndex;
  AppSettings::setLastSelectedSongId(selectedSongId);
  listContent.setSelectedSongId(selectedSongId);
}

bool SongListPanel::isAutoPlayNextEnabledForSong(
    const juce::String &songId) const {
  const auto key = makeAutoPlayNextMarkKey(activeSetlistId, songId);
  return key.isNotEmpty() && autoPlayNextMarks.contains(key);
}

int SongListPanel::resolveAutoPlayNextIndex(
    const juce::String &currentSongId) const {
  if (currentSongId.isEmpty() || activeSetlistId.isEmpty() || activeSongIndices.empty())
    return -1;

  const int currentIndex = getSongIndexById(currentSongId);
  if (currentIndex < 0)
    return -1;

  int firstMarkedAfterCurrent = -1;
  bool currentSongMarked = false;

  for (size_t i = 0; i < activeSongIndices.size(); ++i) {
    const int libraryIndex = activeSongIndices[i];
    if (!juce::isPositiveAndBelow(libraryIndex, static_cast<int>(librarySongs.size())))
      continue;

    const auto &song = librarySongs[static_cast<size_t>(libraryIndex)];
    const auto key = makeAutoPlayNextMarkKey(activeSetlistId, song.id);
    const bool marked = key.isNotEmpty() && autoPlayNextMarks.contains(key);
    if (!marked)
      continue;

    const int rowIndex = static_cast<int>(i);
    if (rowIndex == currentIndex) {
      currentSongMarked = true;
      continue;
    }
    if (rowIndex > currentIndex && firstMarkedAfterCurrent < 0)
      firstMarkedAfterCurrent = rowIndex;
  }

  // Preferred behavior: the marked song itself is the "next" target.
  if (firstMarkedAfterCurrent >= 0)
    return firstMarkedAfterCurrent;

  // Backward compatibility: if current song is marked, fall through to next index.
  if (currentSongMarked) {
    const int immediateNext = currentIndex + 1;
    if (juce::isPositiveAndBelow(immediateNext,
                                 static_cast<int>(activeSongIndices.size())))
      return immediateNext;
  }

  return -1;
}

void SongListPanel::setNowPlayingSong(const juce::String &songId,
                                      bool isPlaying) {
  if (nowPlayingSongId == songId && isPlaybackActive == isPlaying)
    return;
  nowPlayingSongId = songId;
  isPlaybackActive = isPlaying;
  listContent.setNowPlayingSong(songId, isPlaying);
  libraryCatalogPanel.setNowPlayingSong(songId, isPlaying);
}

int SongListPanel::getLastActivatedIndex() const { return lastActivatedIndex; }

juce::String SongListPanel::getActiveSetlistName() const {
  return activeSetlistName;
}

void SongListPanel::refreshFromStorageView() { refreshFromStorage(); }

void SongListPanel::openSongImportWizardFromExternal() { openSongImportWizard(); }

void SongListPanel::refreshLanguageTexts() {
  setlistsHeaderLabel.setText(L10n::tr("Repertorio", "Repertoire"),
                              juce::dontSendNotification);
  setlistsSubtitleLabel.setText(L10n::tr("Mis setlists", "My setlists"),
                                juce::dontSendNotification);
  createSetlistButton.setButtonText(
      L10n::tr("+ Crear setlist", "+ Create setlist"));
  setlistSearchField.setPlaceholderText(
      L10n::tr("Buscar setlist...", "Search setlist..."));
  songsHeaderPanel.refreshLanguageTexts();
  libraryCatalogPanel.refreshLanguageTexts();
  refreshFromStorage();
}

void SongListPanel::refreshThemeColours() {
  titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  setlistsHeaderLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  setlistsSubtitleLabel.setColour(juce::Label::textColourId,
                                  UITheme::textSecondary.withAlpha(0.95f));

  createSetlistButton.setColour(juce::TextButton::buttonColourId,
                                UITheme::accent.withAlpha(0.90f));
  createSetlistButton.setColour(juce::TextButton::buttonOnColourId,
                                UITheme::accentSoft.withAlpha(0.95f));
  createSetlistButton.setColour(juce::TextButton::textColourOffId,
                                UITheme::onAccent);
  createSetlistButton.setColour(juce::TextButton::textColourOnId,
                                UITheme::onAccent);

  setlistSearchField.refreshThemeColours();
  libraryCatalogPanel.refreshThemeColours();
  repaint();
}

void SongListPanel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.38f));
  g.fillRoundedRectangle(bounds, 7.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.86f));
  g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);

  if (viewMode == ViewMode::Songs)
    return;

  const auto setlistsLayout = computeSetlistsLayout();
  const auto searchRow = setlistsLayout.searchRow;
  g.setColour(UITheme::cardBorder.withAlpha(0.20f));
  g.drawLine(static_cast<float>(searchRow.getX()),
             static_cast<float>(searchRow.getBottom() + 8),
             static_cast<float>(searchRow.getRight()),
             static_cast<float>(searchRow.getBottom() + 8), 1.0f);
}

void SongListPanel::resized() {
  if (transitionInFlight) {
    juce::Desktop::getInstance().getAnimator().cancelAllAnimations(true);
    transitionInFlight = false;
    stopTimer();
  }

  auto songsLayout = computeSongsLayout();
  applySongsLayout(songsLayout);
  applySetlistsLayout(computeSetlistsLayout());

  listContent.setSize(juce::jmax(1, listViewport.getWidth()),
                      juce::jmax(listViewport.getHeight(),
                                 listContent.getPreferredHeight()));
  if (setlistsContent != nullptr) {
    setlistsContent->setSize(juce::jmax(1, setlistsViewport.getWidth()),
                             juce::jmax(setlistsViewport.getHeight(),
                                        setlistsContent->getPreferredHeight()));
  }
}

SongListPanel::SongsLayout SongListPanel::computeSongsLayout() const {
  SongsLayout layout;
  auto area = getLocalBounds().reduced(16);
  layout.workspace = area;

  int headerRowHeight = 56;
  int headerPanelHeight = area.getHeight() < 560 ? 96 : 112;
  int gapAfterHeader = area.getHeight() < 560 ? 6 : 8;
  int gapAfterPanel = area.getHeight() < 560 ? 8 : 12;
  constexpr int minListHeight = 140;

  int availableListHeight = area.getHeight() - headerRowHeight -
                            headerPanelHeight - gapAfterHeader - gapAfterPanel;
  if (availableListHeight < minListHeight) {
    int deficit = minListHeight - availableListHeight;
    const int reducePanel = juce::jmin(deficit, juce::jmax(0, headerPanelHeight - 76));
    headerPanelHeight -= reducePanel;
    deficit -= reducePanel;
    const int reduceHeader = juce::jmin(deficit, juce::jmax(0, headerRowHeight - 52));
    headerRowHeight -= reduceHeader;
    deficit -= reduceHeader;
    const int reduceGap2 = juce::jmin(deficit, juce::jmax(0, gapAfterPanel - 4));
    gapAfterPanel -= reduceGap2;
    deficit -= reduceGap2;
    const int reduceGap1 = juce::jmin(deficit, juce::jmax(0, gapAfterHeader - 4));
    gapAfterHeader -= reduceGap1;
  }

  layout.headerRow = area.removeFromTop(headerRowHeight);
  area.removeFromTop(gapAfterHeader);
  layout.headerPanel = area.removeFromTop(headerPanelHeight);
  area.removeFromTop(gapAfterPanel);
  layout.listArea = area;

  auto headerGrid = layout.headerRow;
  const auto leftColumn = headerGrid.removeFromLeft(40);
  const auto rightColumn = headerGrid.removeFromRight(40);
  layout.back = leftColumn.withSizeKeepingCentre(40, 40);
  layout.menu = rightColumn.withSizeKeepingCentre(40, 40);
  layout.edit = {};
  layout.title = headerGrid;
  return layout;
}

SongListPanel::SetlistsLayout SongListPanel::computeSetlistsLayout() const {
  SetlistsLayout layout;
  auto area = getLocalBounds().reduced(16);

  int headerHeight = area.getHeight() < 560 ? 56 : 64;
  int createHeight = area.getHeight() < 560 ? 40 : 44;
  int searchHeight = area.getHeight() < 560 ? 36 : 40;
  int gap1 = area.getHeight() < 560 ? 8 : 10;
  int gap2 = area.getHeight() < 560 ? 8 : 10;
  int gap3 = area.getHeight() < 560 ? 10 : 12;
  constexpr int minViewportHeight = 140;

  int availableViewportHeight =
      area.getHeight() - headerHeight - createHeight - searchHeight - gap1 - gap2 - gap3;
  if (availableViewportHeight < minViewportHeight) {
    int deficit = minViewportHeight - availableViewportHeight;
    const int reduceHeader = juce::jmin(deficit, juce::jmax(0, headerHeight - 48));
    headerHeight -= reduceHeader;
    deficit -= reduceHeader;
    const int reduceCreate = juce::jmin(deficit, juce::jmax(0, createHeight - 36));
    createHeight -= reduceCreate;
    deficit -= reduceCreate;
    const int reduceSearch = juce::jmin(deficit, juce::jmax(0, searchHeight - 34));
    searchHeight -= reduceSearch;
    deficit -= reduceSearch;
    const int reduceGap3 = juce::jmin(deficit, juce::jmax(0, gap3 - 6));
    gap3 -= reduceGap3;
    deficit -= reduceGap3;
    const int reduceGap2 = juce::jmin(deficit, juce::jmax(0, gap2 - 6));
    gap2 -= reduceGap2;
    deficit -= reduceGap2;
    const int reduceGap1 = juce::jmin(deficit, juce::jmax(0, gap1 - 6));
    gap1 -= reduceGap1;
  }

  auto headerRow = area.removeFromTop(headerHeight);
  area.removeFromTop(gap1);
  layout.createRow = area.removeFromTop(createHeight);
  area.removeFromTop(gap2);
  layout.searchRow = area.removeFromTop(searchHeight);
  area.removeFromTop(gap3);

  auto headerCenterColumn = headerRow;
  headerCenterColumn.removeFromLeft(40);
  headerCenterColumn.removeFromRight(40);

  constexpr int titleHeight = 24;
  constexpr int subtitleHeight = 16;
  constexpr int titleSubtitleGap = 2;
  constexpr int totalTextHeight = titleHeight + subtitleHeight + titleSubtitleGap;
  const int textY = headerCenterColumn.getY() +
                    juce::jmax(0, (headerCenterColumn.getHeight() - totalTextHeight) / 2);
  layout.headerTitle = juce::Rectangle<int>(
      headerCenterColumn.getX(), textY, headerCenterColumn.getWidth(), titleHeight);
  layout.headerSubtitle = juce::Rectangle<int>(
      headerCenterColumn.getX(), textY + titleHeight + titleSubtitleGap,
      headerCenterColumn.getWidth(), subtitleHeight);
  layout.viewport = area;
  return layout;
}

void SongListPanel::applySongsLayout(const SongsLayout &layout) {
  updateSongsSubviewLayout(layout);
  applySongsSubviewOffset(songsSubviewOffsetPx);
}

void SongListPanel::updateSongsSubviewLayout(const SongsLayout &layout) {
  cachedSongsLayout = layout;
}

void SongListPanel::applySongsSubviewOffset(float offsetPx) {
  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  songsSubviewOffsetPx =
      juce::jlimit(-static_cast<float>(workspaceWidth), 0.0f, offsetPx);
  const int shiftX = juce::roundToInt(songsSubviewOffsetPx);

  backButton.setBounds(cachedSongsLayout.back.translated(shiftX, 0));
  headerMenuButton.setBounds(cachedSongsLayout.menu.translated(shiftX, 0));
  titleLabel.setBounds(cachedSongsLayout.title.translated(shiftX, 0));
  songsHeaderPanel.setBounds(cachedSongsLayout.headerPanel.translated(shiftX, 0));
  listViewport.setBounds(cachedSongsLayout.listArea.translated(shiftX, 0));

  libraryCatalogPanel.setBounds(
      cachedSongsLayout.workspace.translated(workspaceWidth + shiftX, 0));
}

void SongListPanel::animateSongsSubviewToCurrent() {
  if (viewMode != ViewMode::Songs || cachedSongsLayout.workspace.isEmpty())
    return;

  auto &animator = juce::Desktop::getInstance().getAnimator();
  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  const float targetOffset =
      songsSubview == SongsSubview::Library ? -static_cast<float>(workspaceWidth)
                                            : 0.0f;
  if (std::abs(songsSubviewOffsetPx - targetOffset) < 0.6f) {
    applySongsSubviewOffset(targetOffset);
    applyModeVisibility();
    repaint();
    return;
  }

  auto animate = [&animator, this](juce::Component &component,
                                   juce::Rectangle<int> target) {
    component.setVisible(true);
    animator.animateComponent(&component, target, 1.0f, songsSwipeDurationMs,
                              false, 1.0, 1.0);
  };

  const int shiftX = juce::roundToInt(targetOffset);
  animate(backButton, cachedSongsLayout.back.translated(shiftX, 0));
  animate(headerMenuButton, cachedSongsLayout.menu.translated(shiftX, 0));
  animate(titleLabel, cachedSongsLayout.title.translated(shiftX, 0));
  animate(songsHeaderPanel, cachedSongsLayout.headerPanel.translated(shiftX, 0));
  animate(listViewport, cachedSongsLayout.listArea.translated(shiftX, 0));
  animate(libraryCatalogPanel, cachedSongsLayout.workspace.translated(
                                  workspaceWidth + shiftX, 0));

  songsSubviewOffsetPx = targetOffset;
  songsSubviewTransitionInFlight = true;
  transitionInFlight = true;
  startTimer(songsSwipeDurationMs + 24);
  applyModeVisibility();
  repaint();
}

void SongListPanel::finishSongsSubviewAnimation() {
  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  const float targetOffset =
      songsSubview == SongsSubview::Library ? -static_cast<float>(workspaceWidth)
                                            : 0.0f;
  applySongsSubviewOffset(targetOffset);
}

void SongListPanel::beginSongsSwipeGesture(const juce::MouseEvent &event,
                                           const bool fromSongsToLibrary) {
  if (viewMode != ViewMode::Songs || cachedSongsLayout.workspace.isEmpty())
    return;
  if (fromSongsToLibrary && songsSubview != SongsSubview::SetlistSongs)
    return;
  if (!fromSongsToLibrary && songsSubview != SongsSubview::Library)
    return;

  juce::Desktop::getInstance().getAnimator().cancelAllAnimations(true);
  songsSwipeGestureActive = true;
  songsSubviewTransitionInFlight = false;
  transitionInFlight = false;
  stopTimer();
  songsSwipeStartOffsetPx = songsSubviewOffsetPx;
  songsSwipeStartPosition = event.position;
  applyModeVisibility();
}

void SongListPanel::updateSongsSwipeGesture(const juce::MouseEvent &event) {
  if (!songsSwipeGestureActive || viewMode != ViewMode::Songs)
    return;

  const float deltaX = event.position.x - songsSwipeStartPosition.x;
  applySongsSubviewOffset(songsSwipeStartOffsetPx + deltaX);
  repaint();
}

void SongListPanel::endSongsSwipeGesture(const juce::MouseEvent &event) {
  if (!songsSwipeGestureActive || viewMode != ViewMode::Songs)
    return;

  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  const float progress =
      juce::jlimit(0.0f, 1.0f, -songsSubviewOffsetPx / static_cast<float>(workspaceWidth));
  const float threshold = swipeThresholdRatio();

  const bool movedLeft = (event.position.x - songsSwipeStartPosition.x) < 0.0f;
  if (songsSubview == SongsSubview::SetlistSongs) {
    songsSubview = (progress >= threshold || (progress > 0.12f && movedLeft))
                       ? SongsSubview::Library
                       : SongsSubview::SetlistSongs;
  } else {
    songsSubview = (progress <= (1.0f - threshold) ||
                    (progress < 0.88f && !movedLeft))
                       ? SongsSubview::SetlistSongs
                       : SongsSubview::Library;
  }

  songsSwipeGestureActive = false;
  animateSongsSubviewToCurrent();
}

float SongListPanel::swipeThresholdRatio() { return 0.25f; }

void SongListPanel::applySetlistsLayout(const SetlistsLayout &layout) {
  setlistsHeaderLabel.setBounds(layout.headerTitle);
  setlistsSubtitleLabel.setBounds(layout.headerSubtitle);
  createSetlistButton.setBounds(layout.createRow);
  setlistSearchField.setBounds(layout.searchRow);
  setlistsViewport.setBounds(layout.viewport);
}

void SongListPanel::openHeaderMenu() {
  juce::PopupMenu menu;
  if (viewMode == ViewMode::Setlists) {
    menu.addItem(L10n::tr("Crear setlist", "Create setlist"),
                 [this] { createSetlist(); });
    menu.addSeparator();
    menu.addItem(L10n::tr("Importar cancion", "Import song"),
                 [this] { openSongImportWizard(); });
    menu.addItem(
        L10n::tr("Limpiar busqueda", "Clear search"),
        setlistSearchQuery.isNotEmpty(), false,
        [this] {
          setlistSearchField.clear();
          setlistSearchQuery.clear();
          refreshFromStorage();
        });
  } else {
    menu.addItem(L10n::tr("Biblioteca", "Library"),
                 [this] { openLibraryBrowser(); });
    menu.addSeparator();
    menu.addItem(L10n::tr("Setlists...", "Setlists..."),
                 [this] { showSetlistsView(); });
    menu.addSeparator();
    menu.addItem(L10n::tr("Eliminar setlist actual", "Delete current setlist"),
                 [this] { deleteActiveSetlist(); });
  }
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(headerMenuButton));
}

void SongListPanel::openLibraryBrowser() {
  libraryCatalogPanel.setHeaderMode(LibraryCatalogPanel::HeaderMode::Library);
  showLibraryView(true);
}

void SongListPanel::openAddFromLibraryMenu() {
  libraryCatalogPanel.setHeaderMode(
      LibraryCatalogPanel::HeaderMode::SetlistTarget);
  showLibraryView(true);
}

void SongListPanel::openSetlistOptionsMenu(const juce::String &setlistId,
                                           juce::Rectangle<int> targetArea) {
  if (setlistId.isEmpty())
    return;

  juce::PopupMenu menu;
  menu.addItem(L10n::tr("Abrir setlist", "Open setlist"),
               [this, setlistId] { showSongsView(setlistId); });
  menu.addItem(L10n::tr("Renombrar", "Rename"),
               [this, setlistId] { renameSetlistById(setlistId); });
  menu.addSeparator();
  menu.addItem(L10n::tr("Eliminar", "Delete"),
               [this, setlistId] { deleteSetlistById(setlistId); });
  if (auto *viewed = setlistsViewport.getViewedComponent()) {
    if (targetArea.isEmpty())
      targetArea = viewed->getLocalBounds().withSizeKeepingCentre(10, 10);
    const auto screenArea = viewed->localAreaToGlobal(targetArea);
    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetScreenArea(screenArea)
            .withParentComponent(this));
    return;
  }
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void SongListPanel::createSetlist() {
  promptTextAsync(
      L10n::tr("Crear Setlist", "Create setlist"),
      L10n::tr("Nombre del setlist:", "Setlist name:"), "",
      [this](juce::String name) {
        name = name.trim();
        if (name.isEmpty())
          return;
        const auto newId = SongLibraryStore::createSetlist(name);
        if (newId.isNotEmpty())
          SongLibraryStore::setActiveSetlistId(newId);
        refreshFromStorage();
      });
}

void SongListPanel::renameSetlistById(const juce::String &setlistId) {
  if (setlistId.isEmpty())
    return;
  auto library = SongLibraryStore::loadLibrarySnapshot();
  auto it = std::find_if(library.setlists.begin(), library.setlists.end(),
                         [&setlistId](const SetlistData &s) {
                           return s.id == setlistId;
                         });
  if (it == library.setlists.end())
    return;

  promptTextAsync(
      L10n::tr("Renombrar Setlist", "Rename setlist"),
      L10n::tr("Nombre del setlist:", "Setlist name:"), it->name,
      [this, setlistId](juce::String name) {
        name = name.trim();
        if (name.isEmpty())
          return;
        SongLibraryStore::renameSetlist(setlistId, name);
        refreshFromStorage();
      });
}

void SongListPanel::deleteSetlistById(const juce::String &setlistId) {
  if (setlistId.isEmpty())
    return;

  const auto library = SongLibraryStore::loadLibrarySnapshot();
  if (library.setlists.size() <= 1) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("No se puede eliminar", "Can't delete"),
        L10n::tr("Se requiere al menos un setlist.",
                 "At least one setlist is required."));
    return;
  }

  juce::AlertWindow::showOkCancelBox(
      juce::AlertWindow::WarningIcon,
      L10n::tr("Eliminar setlist", "Delete setlist"),
      L10n::tr("Eliminar este setlist? Esto no quitara canciones de la biblioteca.",
               "Delete this setlist? This won't remove songs from the library."),
      L10n::tr("Eliminar", "Delete"), L10n::tr("Cancelar", "Cancel"), this,
      juce::ModalCallbackFunction::create([this, setlistId](int result) {
        if (result != 1)
          return;
        SongLibraryStore::deleteSetlist(setlistId);
        refreshFromStorage();
      }));
}

void SongListPanel::promptTextAsync(const juce::String &title,
                                    const juce::String &message,
                                    const juce::String &initial,
                                    std::function<void(juce::String)> onComplete) {
  auto *window =
      new juce::AlertWindow(title, message, juce::AlertWindow::NoIcon);
  window->addTextEditor("value", initial, {});
  window->addButton(L10n::tr("Cancelar", "Cancel"), 0);
  window->addButton(L10n::tr("Aceptar", "OK"), 1);
  window->enterModalState(
      true,
      juce::ModalCallbackFunction::create(
          [window, callback = std::move(onComplete)](int result) {
            if (result == 1 && callback)
              callback(window->getTextEditorContents("value").trim());
          }),
      true);
}

void SongListPanel::renameActiveSetlist() {
  renameSetlistById(activeSetlistId);
}

void SongListPanel::deleteActiveSetlist() {
  deleteSetlistById(activeSetlistId);
}

juce::String SongListPanel::formatDurationHMS(const double totalSeconds) {
  const int seconds =
      juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
  const int hh = seconds / 3600;
  const int mm = (seconds % 3600) / 60;
  const int ss = seconds % 60;
  return juce::String::formatted("%02d:%02d:%02d", hh, mm, ss);
}

void SongListPanel::refreshSongsHeaderSummary() {
  double total = 0.0;
  int bpmCount = 0;
  int bpmAccum = 0;
  for (const int idx : activeSongIndices) {
    if (!juce::isPositiveAndBelow(idx, static_cast<int>(librarySongs.size())))
      continue;
    const auto &song = librarySongs[static_cast<size_t>(idx)];
    total += estimateSongLengthSeconds(song);
    if (song.bpm > 0) {
      bpmAccum += song.bpm;
      ++bpmCount;
    }
  }

  SetlistSongsHeaderPanel::Summary summary;
  summary.duration = formatDurationHMS(total);
  summary.songsCount = static_cast<int>(activeSongIndices.size());
  summary.averageBpm =
      bpmCount > 0 ? juce::roundToInt(static_cast<double>(bpmAccum) /
                                      static_cast<double>(bpmCount))
                   : 0;
  songsHeaderPanel.setSummary(summary);
}

void SongListPanel::ListContent::setSongs(std::vector<SongRow> rows) {
  songs = std::move(rows);
  dragCandidate = false;
  isDragging = false;
  dragSourceIndex = -1;
  dragInsertionIndex = -1;
  pendingActionsMenuRow = -1;
  hoveredRow = -1;
  repaint();
}

void SongListPanel::ListContent::setNowPlayingSong(const juce::String &songId,
                                                   bool isPlaying) {
  bool changed = false;
  for (auto &row : songs) {
    const bool next = isPlaying && row.songId == songId;
    if (row.isNowPlaying == next)
      continue;
    row.isNowPlaying = next;
    changed = true;
  }
  if (changed)
    repaint();
}

void SongListPanel::ListContent::setSelectedSongId(const juce::String &songId) {
  bool changed = false;
  for (auto &row : songs) {
    const bool next = songId.isNotEmpty() && row.songId == songId;
    if (row.isSelected == next)
      continue;
    row.isSelected = next;
    changed = true;
  }
  if (changed)
    repaint();
}

void SongListPanel::ListContent::setOnSongActivated(
    std::function<void(int)> cb) {
  onSongActivated = std::move(cb);
}

void SongListPanel::ListContent::setOnSongsReordered(
    std::function<void(int, int)> cb) {
  onSongsReordered = std::move(cb);
}

void SongListPanel::ListContent::setOnAutoPlayNextToggled(
    std::function<void(int)> cb) {
  onAutoPlayNextToggled = std::move(cb);
}

void SongListPanel::ListContent::setOnSongRemoveRequested(
    std::function<void(int)> cb) {
  onSongRemoveRequested = std::move(cb);
}

void SongListPanel::ListContent::setOnAddSongsRequested(
    std::function<void()> cb) {
  onAddSongsRequested = std::move(cb);
}

int SongListPanel::ListContent::getPreferredHeight() const {
  const int songCount = static_cast<int>(songs.size());
  const int rowsHeight = rowStart + songCount * (rowHeight + rowGap);
  return juce::jmax(88, rowsHeight + rowHeight + rowGap);
}

void SongListPanel::ListContent::paint(juce::Graphics &g) {
  const bool hasActiveDrag =
      isDragging && juce::isPositiveAndBelow(dragSourceIndex,
                                             static_cast<int>(songs.size()));

  auto paintSongRow = [this, &g](const SongRow &rowData,
                                 juce::Rectangle<int> row, const int rowIndex,
                                 juce::Colour fill, float fillAlpha,
                                 float borderAlpha) {
    auto formatDurationMMSS = [](double totalSeconds) {
      const int seconds =
          juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
      const int mm = seconds / 60;
      const int ss = seconds % 60;
      return juce::String::formatted("%02d:%02d", mm, ss);
    };

    const auto playingStateColour = UITheme::accent;
    const auto nextStateColour = juce::Colour::fromRGB(250, 204, 21);
    const bool showPlayingState = rowData.isNowPlaying;
    const bool showNextState = rowData.autoPlayNext && !showPlayingState;
    const bool showSelectedState =
        rowData.isSelected && !showPlayingState && !showNextState;

    auto rowFill = fill;
    if (rowFill == UITheme::cardBgHover)
      rowFill = UITheme::cardBg;
    if (rowIndex == hoveredRow)
      rowFill = rowFill.interpolatedWith(UITheme::cardBgHover, 0.28f);

    juce::Path rowPath;
    rowPath.addRoundedRectangle(row.toFloat(), 12.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.25f), 8, {0, 2})
        .drawForPath(g, rowPath);

    g.setColour(rowFill.withAlpha(fillAlpha));
    g.fillRoundedRectangle(row.toFloat(), 12.0f);

    const auto borderColour = showPlayingState
                                  ? playingStateColour.withAlpha(
                                        juce::jlimit(0.0f, 1.0f,
                                                     borderAlpha + 0.25f))
                                  : showSelectedState
                                              ? UITheme::accent.withAlpha(
                                                    juce::jlimit(0.0f, 1.0f,
                                                                 borderAlpha +
                                                                     0.18f))
                                              : UITheme::cardBorder.withAlpha(
                                                    borderAlpha * 0.42f);
    g.setColour(borderColour);
    g.drawRoundedRectangle(row.toFloat(), 12.0f,
                           showPlayingState ? 1.5f : 1.0f);

    auto c = row.reduced(16, 8);
    auto accentRect =
        juce::Rectangle<int>(c.getX(), row.getCentreY() - 20, 4, 40);
    c.removeFromLeft(4);
    c.removeFromLeft(12);

    const auto accentColour = kSetlistAccentPalette[static_cast<size_t>(rowIndex) %
                                                    kSetlistAccentPalette.size()];
    g.setColour(accentColour.withAlpha(0.92f));
    g.fillRoundedRectangle(accentRect.toFloat(), 2.0f);

    auto orderBadge = c.removeFromLeft(20);
    if (showPlayingState) {
      drawIconGlyph(g, orderBadge.toFloat().withSizeKeepingCentre(18.0f, 18.0f),
                    IconGlyph::Play, playingStateColour.withAlpha(0.95f), 1.4f);
    } else if (showNextState) {
      drawIconGlyph(g, orderBadge.toFloat().withSizeKeepingCentre(18.0f, 18.0f),
                    IconGlyph::Next, nextStateColour.withAlpha(0.95f), 1.35f);
    } else {
      g.setColour(UITheme::textSecondary.withAlpha(0.88f));
      g.setFont(makeAppFont(16.0f, AppFontWeight::Medium));
      g.drawText(juce::String(rowData.order), orderBadge,
                 juce::Justification::centred);
    }

    c.removeFromLeft(12);
    auto dragHitArea = c.removeFromRight(40);
    c.removeFromRight(4);
    auto optionsHitArea = c.removeFromRight(40);
    constexpr int titleHeight = 18;
    constexpr int artistHeight = 14;
    constexpr int metaHeight = 13;
    constexpr int lineGap = 2;
    const int totalTextHeight =
        titleHeight + artistHeight + metaHeight + (lineGap * 2);
    const int textStartY =
        c.getY() + juce::jmax(0, (c.getHeight() - totalTextHeight) / 2);

    auto titleRect =
        juce::Rectangle<int>(c.getX(), textStartY, c.getWidth(), titleHeight);
    auto artistRect = juce::Rectangle<int>(c.getX(), titleRect.getBottom() + lineGap,
                                           c.getWidth(), artistHeight);
    auto metaRect = juce::Rectangle<int>(c.getX(), artistRect.getBottom() + lineGap,
                                         c.getWidth(), metaHeight);

    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(17.0f, AppFontWeight::SemiBold));
    g.drawFittedText(rowData.title, titleRect, juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textSecondary.withAlpha(0.95f));
    g.setFont(makeAppFont(11.6f, AppFontWeight::Medium));
    g.drawFittedText(
        rowData.artist.isNotEmpty()
            ? rowData.artist
            : L10n::tr("Sin artista", "Unknown artist").toUpperCase(),
        artistRect, juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textSecondary.withAlpha(0.76f));
    g.setFont(makeAppFont(11.6f, AppFontWeight::Regular));
    const auto detail = (rowData.key.isNotEmpty() ? rowData.key : "-") + " • " +
                        (rowData.bpm > 0 ? juce::String(rowData.bpm) : "--") +
                        " BPM • " + juce::String(rowData.stemCount) + " stems" +
                        " • " + formatDurationMMSS(rowData.durationSeconds);
    g.drawFittedText(detail, metaRect, juce::Justification::centredLeft, 1);

    drawIconGlyph(g, optionsHitArea.toFloat().withSizeKeepingCentre(20.0f, 20.0f),
                  IconGlyph::More, UITheme::textSecondary.withAlpha(0.95f), 1.4f);

    g.setColour(UITheme::textSecondary.withAlpha(0.78f));
    const auto d = dragHitArea.toFloat().withSizeKeepingCentre(20.0f, 20.0f);
    const float x = d.getCentreX();
    const float y = d.getCentreY();
    g.drawLine(x - 5.8f, y - 4.6f, x + 5.8f, y - 4.6f, 1.7f);
    g.drawLine(x - 5.8f, y, x + 5.8f, y, 1.7f);
    g.drawLine(x - 5.8f, y + 4.6f, x + 5.8f, y + 4.6f, 1.7f);
  };

  for (size_t i = 0; i < songs.size(); ++i) {
    auto row = juce::Rectangle<int>(
        rowSideInset, rowStart + static_cast<int>(i) * (rowHeight + rowGap),
        getWidth() - (rowSideInset * 2), rowHeight);
    if (hasActiveDrag && static_cast<int>(i) == dragSourceIndex) {
      paintSongRow(songs[i], row, static_cast<int>(i), UITheme::cardBgHover, 0.24f,
                   0.35f);
      continue;
    }
    paintSongRow(songs[i], row, static_cast<int>(i), UITheme::cardBgHover, 1.0f,
                 0.58f);
  }

  if (songs.empty()) {
    g.setColour(UITheme::textSecondary.withAlpha(0.88f));
    g.setFont(makeAppFont(12.0f, AppFontWeight::Regular));
    g.drawFittedText(L10n::tr("Aun no hay canciones en este setlist.",
                              "No songs in this setlist yet."),
                     getLocalBounds().withTrimmedBottom(rowHeight + rowGap).reduced(10),
                     juce::Justification::centred, 2);
  }

  const auto addRow = addSongsRowBounds();
  const bool addRowHovered = addRow.contains(getMouseXYRelative());
  const auto addStroke =
      UITheme::cardBorder.interpolatedWith(UITheme::accent, addRowHovered ? 0.56f : 0.36f)
          .withAlpha(addRowHovered ? 0.86f : 0.62f);
  const auto addFill = UITheme::panelDark
                           .interpolatedWith(UITheme::cardBg, addRowHovered ? 0.20f : 0.14f)
                           .withAlpha(0.96f);
  g.setColour(addFill);
  g.fillRoundedRectangle(addRow.toFloat(), 12.0f);

  juce::Path addBorder;
  addBorder.addRoundedRectangle(addRow.toFloat().reduced(0.5f), 12.0f);
  juce::Path dashedBorder;
  const float dashLengths[] = {5.0f, 4.0f};
  juce::PathStrokeType(1.2f).createDashedStroke(dashedBorder, addBorder, dashLengths, 2);
  g.setColour(addStroke);
  g.strokePath(dashedBorder, juce::PathStrokeType(1.0f));

  const auto center = addRow.getCentre().toFloat();
  const auto plusColour = UITheme::accent.withAlpha(addRowHovered ? 0.96f : 0.88f);
  drawSvgIcon(g,
              juce::Rectangle<float>(center.x - 9.0f, center.y - 14.0f, 18.0f, 18.0f),
              SvgIconName::Plus, plusColour);
  g.setColour(UITheme::textSecondary.withAlpha(addRowHovered ? 0.96f : 0.90f));
  g.setFont(makeAppFont(12.0f, AppFontWeight::SemiBold));
  g.drawFittedText(
      L10n::tr("Agregar canciones desde biblioteca", "Add songs from library"),
      addRow.withTrimmedTop(22).reduced(10, 0), juce::Justification::centred, 1);

  if (hasActiveDrag) {
    const int dragTop = juce::roundToInt(dragMouseY - dragRowOffsetY);
    const int clampedTop =
        juce::jlimit(rowStart,
                     juce::jmax(rowStart, getHeight() - rowHeight - rowStart),
                     dragTop);
    const auto dragRow =
        juce::Rectangle<int>(rowSideInset, clampedTop,
                             getWidth() - (rowSideInset * 2), rowHeight);
    paintSongRow(songs[static_cast<size_t>(dragSourceIndex)], dragRow,
                 dragSourceIndex, UITheme::cardBgPressed, 0.95f, 0.86f);

    const int lineY = insertionLineY(dragInsertionIndex);
    const auto lineBounds =
        juce::Rectangle<float>(static_cast<float>(rowSideInset),
                               static_cast<float>(lineY - 1),
                               static_cast<float>(getWidth() - (rowSideInset * 2)),
                               2.0f);
    g.setColour(UITheme::accent.withAlpha(0.95f));
    g.fillRoundedRectangle(lineBounds, 1.0f);
  }
}

void SongListPanel::ListContent::mouseDown(const juce::MouseEvent &event) {
  dragCandidate = false;
  isDragging = false;
  didDragSinceMouseDown = false;
  dragSourceIndex = -1;
  dragInsertionIndex = -1;
  pendingActionsMenuRow = -1;
  dragRowOffsetY = 0.0f;
  dragMouseY = event.position.y;

  const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
  if (idx < 0 && addSongsRowBounds().contains(event.position.toInt()))
    return;
  if (!juce::isPositiveAndBelow(idx, static_cast<int>(songs.size())))
    return;

  auto row =
      juce::Rectangle<int>(rowSideInset, rowTopAtIndex(idx),
                           getWidth() - (rowSideInset * 2), rowHeight);
  if (optionsButtonBoundsForRow(row).contains(event.position.toInt())) {
    pendingActionsMenuRow = idx;
    return;
  }

  dragCandidate = true;
  dragSourceIndex = idx;
  dragInsertionIndex = idx;
  mouseDownPosition = event.position;
  dragRowOffsetY = event.position.y - static_cast<float>(rowTopAtIndex(idx));
}

void SongListPanel::ListContent::mouseDrag(const juce::MouseEvent &event) {
  if (pendingActionsMenuRow >= 0)
    return;

  if (!dragCandidate || !juce::isPositiveAndBelow(dragSourceIndex,
                                                  static_cast<int>(songs.size())))
    return;

  constexpr float dragStartDistancePx = 4.0f;
  if (!isDragging &&
      event.position.getDistanceFrom(mouseDownPosition) < dragStartDistancePx) {
    return;
  }

  isDragging = true;
  didDragSinceMouseDown = true;
  dragMouseY = event.position.y;
  dragInsertionIndex = insertionIndexAtY(juce::roundToInt(event.position.y));
  repaint();
}

void SongListPanel::ListContent::mouseUp(const juce::MouseEvent &event) {
  if (addSongsRowBounds().contains(event.position.toInt())) {
    if (onAddSongsRequested)
      onAddSongsRequested();
    return;
  }

  if (pendingActionsMenuRow >= 0) {
    const int row = pendingActionsMenuRow;
    pendingActionsMenuRow = -1;
    if (juce::isPositiveAndBelow(row, static_cast<int>(songs.size()))) {
      const auto rowBounds = juce::Rectangle<int>(
          rowSideInset, rowTopAtIndex(row), getWidth() - (rowSideInset * 2),
          rowHeight);
      if (optionsButtonBoundsForRow(rowBounds).contains(event.position.toInt()))
        showActionsMenuForRow(row);
    }
    return;
  }

  if (!dragCandidate) {
    didDragSinceMouseDown = false;
    return;
  }

  if (isDragging && juce::isPositiveAndBelow(dragSourceIndex,
                                             static_cast<int>(songs.size()))) {
    int insertion = dragInsertionIndex;
    if (!juce::isPositiveAndBelow(insertion, static_cast<int>(songs.size()) + 1))
      insertion = insertionIndexAtY(juce::roundToInt(dragMouseY));

    int toIndex = insertion;
    if (insertion > dragSourceIndex)
      --toIndex;
    toIndex = juce::jlimit(0, static_cast<int>(songs.size()) - 1, toIndex);

    if (onSongsReordered && dragSourceIndex != toIndex)
      onSongsReordered(dragSourceIndex, toIndex);
  }

  dragCandidate = false;
  isDragging = false;
  dragSourceIndex = -1;
  dragInsertionIndex = -1;
  dragRowOffsetY = 0.0f;
  dragMouseY = 0.0f;
  repaint();
}

void SongListPanel::ListContent::mouseDoubleClick(
    const juce::MouseEvent &event) {
  if (didDragSinceMouseDown) {
    didDragSinceMouseDown = false;
    return;
  }

  if (addSongsRowBounds().contains(event.position.toInt())) {
    if (onAddSongsRequested)
      onAddSongsRequested();
    return;
  }

  const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
  if (!juce::isPositiveAndBelow(idx, static_cast<int>(songs.size())))
    return;
  const auto rowBounds =
      juce::Rectangle<int>(rowSideInset, rowTopAtIndex(idx),
                           getWidth() - (rowSideInset * 2), rowHeight);
  if (optionsButtonBoundsForRow(rowBounds).contains(event.position.toInt()))
    return;

  for (int i = 0; i < static_cast<int>(songs.size()); ++i)
    songs[static_cast<size_t>(i)].isSelected = (i == idx);
  repaint();

  if (onSongActivated)
    onSongActivated(songs[static_cast<size_t>(idx)].songIndex);
}

void SongListPanel::ListContent::mouseMove(const juce::MouseEvent &event) {
  const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
  if (hoveredRow == idx)
    return;
  hoveredRow = idx;
  repaint();
}

void SongListPanel::ListContent::mouseExit(const juce::MouseEvent &) {
  if (hoveredRow < 0)
    return;
  hoveredRow = -1;
  repaint();
}

int SongListPanel::ListContent::rowIndexAtY(int y) const {
  if (y < rowStart)
    return -1;

  const int relY = y - rowStart;
  const int step = rowHeight + rowGap;
  const int row = relY / step;
  if (!juce::isPositiveAndBelow(row, static_cast<int>(songs.size())))
    return -1;

  if ((relY % step) >= rowHeight)
    return -1;

  return row;
}

int SongListPanel::ListContent::insertionIndexAtY(int y) const {
  const int step = rowHeight + rowGap;
  const int count = static_cast<int>(songs.size());

  if (count <= 0)
    return 0;

  for (int i = 0; i < count; ++i) {
    const int rowCenterY = rowStart + i * step + (rowHeight / 2);
    if (y < rowCenterY)
      return i;
  }

  return count;
}

int SongListPanel::ListContent::rowTopAtIndex(int index) const {
  const int step = rowHeight + rowGap;
  return rowStart + index * step;
}

int SongListPanel::ListContent::insertionLineY(int insertionIndex) const {
  const int step = rowHeight + rowGap;
  const int count = static_cast<int>(songs.size());
  const int idx = juce::jlimit(0, count, insertionIndex);

  if (idx == 0)
    return rowStart;
  if (idx >= count)
    return rowStart + (count - 1) * step + rowHeight;
  return rowStart + idx * step - (rowGap / 2);
}

juce::Rectangle<int> SongListPanel::ListContent::addSongsRowBounds() const {
  const int top = rowStart + static_cast<int>(songs.size()) * (rowHeight + rowGap);
  return juce::Rectangle<int>(rowSideInset, top, getWidth() - (rowSideInset * 2),
                              rowHeight);
}

juce::Rectangle<int> SongListPanel::ListContent::optionsButtonBoundsForRow(
    const juce::Rectangle<int> &rowBounds) const {
  auto c = rowBounds.reduced(16, 8);
  c.removeFromLeft(4);
  c.removeFromLeft(12);
  c.removeFromLeft(20);
  c.removeFromLeft(12);
  c.removeFromRight(40);
  c.removeFromRight(4);
  auto optionsRect = c.removeFromRight(40);
  return optionsRect;
}

void SongListPanel::ListContent::showActionsMenuForRow(int rowIndex) {
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(songs.size())))
    return;

  class ActionChip final : public juce::Button {
  public:
    ActionChip(juce::String labelText, bool emphasized, bool destructive)
        : juce::Button(labelText), label(std::move(labelText)),
          isEmphasized(emphasized), isDestructive(destructive) {}

    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override {
      auto b = getLocalBounds().toFloat();
      auto fill = UITheme::cardBgHover;
      if (isEmphasized)
        fill = fill.interpolatedWith(UITheme::accentSoft, 0.22f);
      if (isDestructive)
        fill = fill.interpolatedWith(UITheme::accentMuted, 0.30f);
      if (isMouseOverButton)
        fill = fill.brighter(0.06f);
      if (isButtonDown)
        fill = fill.darker(0.05f);

      g.setColour(fill);
      g.fillRoundedRectangle(b, 14.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.65f));
      g.drawRoundedRectangle(b, 14.0f, 1.0f);

      const auto textColour = isDestructive ? UITheme::onAccent
                                            : UITheme::textPrimary;
      g.setColour(textColour);
      g.setFont(juce::FontOptions(12.0f));
      g.drawFittedText(label, getLocalBounds().reduced(8, 0),
                       juce::Justification::centred, 1);
    }

  private:
    juce::String label;
    bool isEmphasized = false;
    bool isDestructive = false;
  };

  class SongActionsPopover final : public juce::Component {
  public:
    SongActionsPopover(bool autoPlayEnabled, std::function<void()> onToggleFn,
                       std::function<void()> onRemoveFn)
        : toggleChip(autoPlayEnabled
                         ? L10n::tr("Desactivar auto-play siguiente",
                                    "Disable auto-play next")
                         : L10n::tr("Activar auto-play siguiente",
                                    "Enable auto-play next"),
                     autoPlayEnabled, false),
          removeChip(L10n::tr("Quitar del setlist", "Remove from setlist"),
                     false, true),
          onToggle(std::move(onToggleFn)), onRemove(std::move(onRemoveFn)) {
      toggleChip.onClick = [this] {
        if (onToggle)
          onToggle();
        closeSelf();
      };
      removeChip.onClick = [this] {
        if (onRemove)
          onRemove();
        closeSelf();
      };
      addAndMakeVisible(toggleChip);
      addAndMakeVisible(removeChip);
    }

    void paint(juce::Graphics &g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(b, 8.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.82f));
      g.drawRoundedRectangle(b, 8.0f, 1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(10);
      toggleChip.setBounds(area.removeFromTop(30));
      area.removeFromTop(8);
      removeChip.setBounds(area.removeFromTop(30));
    }

  private:
    ActionChip toggleChip;
    ActionChip removeChip;
    std::function<void()> onToggle;
    std::function<void()> onRemove;

    void closeSelf() {
      if (auto *callout = findParentComponentOfClass<juce::CallOutBox>()) {
        callout->exitModalState(0);
        callout->setVisible(false);
      }
    }
  };

  const auto songIndex = songs[static_cast<size_t>(rowIndex)].songIndex;
  const bool autoPlayEnabled = songs[static_cast<size_t>(rowIndex)].autoPlayNext;
  const auto rowBounds =
      juce::Rectangle<int>(rowSideInset, rowTopAtIndex(rowIndex),
                           getWidth() - (rowSideInset * 2), rowHeight);
  const auto optionsBounds = optionsButtonBoundsForRow(rowBounds);

  auto safeThis = juce::Component::SafePointer<ListContent>(this);
  auto panel = std::make_unique<SongActionsPopover>(
      autoPlayEnabled,
      [safeThis, songIndex] {
        if (safeThis == nullptr)
          return;
        if (safeThis->onAutoPlayNextToggled)
          safeThis->onAutoPlayNextToggled(songIndex);
      },
      [safeThis, songIndex] {
        if (safeThis == nullptr)
          return;
        if (safeThis->onSongRemoveRequested)
          safeThis->onSongRemoveRequested(songIndex);
      });
  panel->setSize(240, 86);

  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor =
      parentComp->getLocalArea(this, optionsBounds.expanded(2));
  auto &callout = juce::CallOutBox::launchAsynchronously(std::move(panel),
                                                         anchor, parentComp);
  auto fitRightArea = parentComp->getLocalBounds();
  fitRightArea.setX(anchor.getRight() + 2);
  fitRightArea.setWidth(parentComp->getWidth() - fitRightArea.getX());
  if (fitRightArea.getWidth() > 24)
    callout.updatePosition(anchor, fitRightArea);
  callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

class SongListPanel::SetlistManagerPanel final : public juce::Component {
public:
  using SetlistLoadedCallback = std::function<void(const juce::String &)>;

  explicit SetlistManagerPanel(SetlistLoadedCallback onLoaded)
      : onSetlistLoaded(std::move(onLoaded)) {
    setSize(860, 560);

    titleLabel.setText(L10n::tr("Setlists", "Setlists"),
                       juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(18.0f));
    addAndMakeVisible(titleLabel);

    createButton.setButtonText(L10n::tr("Crear Setlist", "Create a Setlist"));
    createButton.onClick = [this] { createSetlist(); };
    addAndMakeVisible(createButton);

    searchEditor.setTextToShowWhenEmpty(
        L10n::tr("Buscar Setlists", "Search Setlists"), UITheme::textSecondary);
    searchEditor.onTextChange = [this] { refreshSetlistRows(); };
    addAndMakeVisible(searchEditor);

    setlistViewport.setViewedComponent(&setlistContent, false);
    setlistViewport.setScrollBarsShown(true, false, false, false);
    setlistViewport.setScrollBarThickness(1);
    addAndMakeVisible(setlistViewport);

    songsViewport.setViewedComponent(&songsContent, false);
    songsViewport.setScrollBarsShown(true, false, false, false);
    songsViewport.setScrollBarThickness(1);
    addAndMakeVisible(songsViewport);

    addSongButton.setButtonText(L10n::tr("Agregar cancion", "Add Song"));
    addSongButton.onClick = [this] { showAddSongMenu(); };
    addAndMakeVisible(addSongButton);

    loadButton.setButtonText(L10n::tr("Cargar setlist", "Load setlist"));
    loadButton.onClick = [this] { loadSelectedSetlist(); };
    addAndMakeVisible(loadButton);

    setlistContent.setOnAction(
        [this](int index, SetlistListContent::Action action) {
          handleSetlistAction(index, action);
        });

    songsContent.setOnAction(
        [this](int row, SongsListContent::Action action) {
          handleSongAction(row, action);
        });

    applyTheme();
    reloadLibrary();
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.7f));
    g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(18);
    auto header = area.removeFromTop(36);
    titleLabel.setBounds(header);

    const int createW = 160;
    auto headerRight = header.removeFromRight(createW);
    createButton.setBounds(headerRight.removeFromLeft(createW));

    area.removeFromTop(8);
    auto topRow = area.removeFromTop(30);
    searchEditor.setBounds(
        topRow.removeFromLeft(juce::jmin(320, topRow.getWidth() / 2)));

    area.removeFromTop(10);
    auto contentArea = area;
    const int leftW =
        juce::jlimit(240, 320, static_cast<int>(contentArea.getWidth() * 0.38f));
    auto left = contentArea.removeFromLeft(leftW);
    auto right = contentArea;

    left.removeFromTop(6);
    setlistViewport.setBounds(left);

    auto rightHeader = right.removeFromTop(32);
    addSongButton.setBounds(rightHeader.removeFromLeft(110));
    rightHeader.removeFromLeft(8);
    loadButton.setBounds(rightHeader.removeFromLeft(130));
    right.removeFromTop(6);
    songsViewport.setBounds(right);

    setlistContent.setSize(
        juce::jmax(1, setlistViewport.getWidth()),
        juce::jmax(setlistViewport.getHeight(),
                   setlistContent.getPreferredHeight()));
    songsContent.setSize(juce::jmax(1, songsViewport.getWidth()),
                         juce::jmax(songsViewport.getHeight(),
                                    songsContent.getPreferredHeight()));
  }

private:
  struct SetlistRow {
    juce::String id;
    juce::String name;
    int songCount = 0;
  };

  class SetlistListContent final : public juce::Component {
  public:
    enum class Action { Select, Rename, Delete };

    void setRows(std::vector<SetlistRow> nextRows, int selected) {
      rows = std::move(nextRows);
      selectedIndex = selected;
      repaint();
    }

    void setOnAction(std::function<void(int, Action)> cb) {
      onAction = std::move(cb);
    }

    int getPreferredHeight() const {
      if (rows.empty())
        return 88;
      constexpr int rowH = 44;
      constexpr int gap = 6;
      return 8 + static_cast<int>(rows.size()) * (rowH + gap);
    }

    void paint(juce::Graphics &g) override {
      if (rows.empty()) {
        g.setColour(UITheme::textSecondary.withAlpha(0.8f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(L10n::tr("Aun no hay setlists.", "No setlists yet."),
                         getLocalBounds().reduced(10),
                         juce::Justification::centred, 1);
        return;
      }

      constexpr int rowH = 44;
      constexpr int gap = 6;
      for (size_t i = 0; i < rows.size(); ++i) {
        auto row = juce::Rectangle<int>(
            6, 8 + static_cast<int>(i) * (rowH + gap), getWidth() - 12, rowH);
        const bool selected = static_cast<int>(i) == selectedIndex;
        g.setColour(selected ? UITheme::cardBgHover : UITheme::cardBg);
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        g.setColour(UITheme::cardBorder.withAlpha(0.6f));
        g.drawRoundedRectangle(row.toFloat(), 4.0f, 1.0f);

        auto textArea = row.reduced(10, 6);
        const int actionSize = 16;
        const int actionGap = 6;
        const int actionRightPad = 10;
        const int actionY = row.getCentreY() - (actionSize / 2);
        const int deleteX = row.getRight() - actionRightPad - actionSize;
        const int renameX = deleteX - actionGap - actionSize;
        auto renameRect =
            juce::Rectangle<int>(renameX, actionY, actionSize, actionSize);
        auto deleteRect =
            juce::Rectangle<int>(deleteX, actionY, actionSize, actionSize);
        g.setColour(UITheme::textPrimary);
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(rows[i].name, textArea.removeFromTop(16),
                         juce::Justification::centredLeft, 1);
        g.setColour(UITheme::textSecondary);
        g.setFont(juce::FontOptions(9.0f));
        g.drawFittedText(juce::String(rows[i].songCount) + " " +
                             L10n::tr("canciones", "songs"),
                         textArea,
                         juce::Justification::centredLeft, 1);

        drawActionButton(g, renameRect, IconGlyph::Pencil,
                         UITheme::textSecondary);
        drawDeleteButton(g, deleteRect, UITheme::accent);
      }
    }

    void mouseUp(const juce::MouseEvent &event) override {
      const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
      if (!juce::isPositiveAndBelow(idx, static_cast<int>(rows.size())))
        return;

      auto row = juce::Rectangle<int>(
          6, 8 + static_cast<int>(idx) * (44 + 6), getWidth() - 12, 44);
      const int actionSize = 16;
      const int actionGap = 6;
      const int actionRightPad = 10;
      const int actionY = row.getCentreY() - (actionSize / 2);
      const int deleteX = row.getRight() - actionRightPad - actionSize;
      const int renameX = deleteX - actionGap - actionSize;
      auto renameRect =
          juce::Rectangle<int>(renameX, actionY, actionSize, actionSize);
      auto deleteRect =
          juce::Rectangle<int>(deleteX, actionY, actionSize, actionSize);

      const auto pos = event.position.toInt();
      if (renameRect.contains(pos)) {
        if (onAction)
          onAction(idx, Action::Rename);
        return;
      }
      if (deleteRect.contains(pos)) {
        if (onAction)
          onAction(idx, Action::Delete);
        return;
      }
      if (onAction)
        onAction(idx, Action::Select);
    }

  private:
    int rowIndexAtY(int y) const {
      constexpr int rowStart = 8;
      constexpr int rowH = 44;
      constexpr int gap = 6;
      if (y < rowStart)
        return -1;
      const int relY = y - rowStart;
      const int step = rowH + gap;
      const int row = relY / step;
      if (!juce::isPositiveAndBelow(row, static_cast<int>(rows.size())))
        return -1;
      if ((relY % step) >= rowH)
        return -1;
      return row;
    }

    std::vector<SetlistRow> rows;
    int selectedIndex = -1;
    std::function<void(int, Action)> onAction;

    void drawActionButton(juce::Graphics &g, juce::Rectangle<int> bounds,
                          IconGlyph glyph, juce::Colour iconColour) {
      auto b = bounds.toFloat();
      g.setColour(UITheme::cardBgHover);
      g.fillRoundedRectangle(b, 3.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.6f));
      g.drawRoundedRectangle(b, 3.0f, 1.0f);
      drawIconGlyph(g, b.reduced(2.0f), glyph, iconColour, 1.4f);
    }

    void drawDeleteButton(juce::Graphics &g, juce::Rectangle<int> bounds,
                          juce::Colour iconColour) {
      auto b = bounds.toFloat();
      g.setColour(UITheme::cardBgHover);
      g.fillRoundedRectangle(b, 3.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.6f));
      g.drawRoundedRectangle(b, 3.0f, 1.0f);

      const auto pad = 5.0f;
      g.setColour(iconColour);
      g.drawLine(b.getX() + pad, b.getY() + pad, b.getRight() - pad,
                 b.getBottom() - pad, 1.6f);
      g.drawLine(b.getRight() - pad, b.getY() + pad, b.getX() + pad,
                 b.getBottom() - pad, 1.6f);
    }
  };

  class SongsListContent final : public juce::Component {
  public:
    enum class Action { Remove };

    struct SongRow {
      juce::String songId;
      juce::String title;
      juce::String meta;
    };

    void setRows(std::vector<SongRow> nextRows) {
      rows = std::move(nextRows);
      repaint();
    }

    void setOnAction(std::function<void(int, Action)> cb) {
      onAction = std::move(cb);
    }

    int getPreferredHeight() const {
      if (rows.empty())
        return 120;
      constexpr int rowH = 52;
      constexpr int gap = 6;
      return 8 + static_cast<int>(rows.size()) * (rowH + gap);
    }

    void paint(juce::Graphics &g) override {
      if (rows.empty()) {
        g.setColour(UITheme::textSecondary.withAlpha(0.8f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(L10n::tr("Aun no hay canciones.", "No songs yet."),
                         getLocalBounds().reduced(10),
                         juce::Justification::centred, 1);
        return;
      }

      constexpr int rowH = 52;
      constexpr int gap = 6;
      for (size_t i = 0; i < rows.size(); ++i) {
        auto row = juce::Rectangle<int>(
            6, 8 + static_cast<int>(i) * (rowH + gap), getWidth() - 12, rowH);
        g.setColour(UITheme::cardBgHover);
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        g.setColour(UITheme::cardBorder.withAlpha(0.6f));
        g.drawRoundedRectangle(row.toFloat(), 4.0f, 1.0f);

        auto c = row.reduced(8, 6);
        c.removeFromLeft(24);
        auto buttonArea = c.removeFromRight(26);
        auto removeRect = buttonArea.removeFromRight(22).reduced(2);
        drawActionButton(g, removeRect, "X", UITheme::accent);

        g.setColour(UITheme::textPrimary.darker(0.16f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(rows[i].title, c.removeFromTop(16),
                         juce::Justification::centredLeft, 1);
        g.setColour(UITheme::textSecondary);
        g.setFont(juce::FontOptions(9.0f));
        g.drawFittedText(rows[i].meta, c, juce::Justification::centredLeft, 1);
      }
    }

    void mouseUp(const juce::MouseEvent &event) override {
      const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
      if (!juce::isPositiveAndBelow(idx, static_cast<int>(rows.size())))
        return;

      auto row = juce::Rectangle<int>(
          6, 8 + idx * (52 + 6), getWidth() - 12, 52);
      auto c = row.reduced(8, 6);
      c.removeFromLeft(24);
      auto buttonArea = c.removeFromRight(26);
      auto removeRect = buttonArea.removeFromRight(22).reduced(2);

      const auto pos = event.position.toInt();
      if (removeRect.contains(pos)) {
        if (onAction)
          onAction(idx, Action::Remove);
        return;
      }
    }

  private:
    int rowIndexAtY(int y) const {
      constexpr int rowStart = 8;
      constexpr int rowH = 52;
      constexpr int gap = 6;
      if (y < rowStart)
        return -1;
      const int relY = y - rowStart;
      const int step = rowH + gap;
      const int row = relY / step;
      if (!juce::isPositiveAndBelow(row, static_cast<int>(rows.size())))
        return -1;
      if ((relY % step) >= rowH)
        return -1;
      return row;
    }

    void drawActionButton(juce::Graphics &g, juce::Rectangle<int> bounds,
                          const juce::String &label, juce::Colour colour) {
      g.setColour(UITheme::cardBorder.withAlpha(0.5f));
      g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);
      g.setColour(colour);
      g.setFont(juce::FontOptions(10.0f));
      g.drawFittedText(label, bounds, juce::Justification::centred, 1);
    }

    std::vector<SongRow> rows;
    std::function<void(int, Action)> onAction;
  };

  void applyTheme() {
    titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
    searchEditor.setColour(juce::TextEditor::backgroundColourId,
                           UITheme::cardBgHover);
    searchEditor.setColour(juce::TextEditor::textColourId,
                           UITheme::textPrimary);
    searchEditor.setColour(juce::TextEditor::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.6f));
    styleButton(createButton, UITheme::cardBgHover);
    styleButton(loadButton, UITheme::accent);
    styleButton(addSongButton, UITheme::cardBgHover);
  }

  void styleButton(juce::TextButton &button, juce::Colour base) {
    button.setColour(juce::TextButton::buttonColourId, base);
    button.setColour(juce::TextButton::buttonOnColourId, base.darker(0.08f));
    button.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    button.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
  }

  void reloadLibrary() {
    library = SongLibraryStore::loadLibrarySnapshot();
    if (selectedSetlistId.isEmpty() || !setlistExists(selectedSetlistId)) {
      selectedSetlistId = library.activeSetlistId;
    }
    if (selectedSetlistId.isEmpty() && !library.setlists.empty())
      selectedSetlistId = library.setlists.front().id;
    refreshSetlistRows();
    refreshSongsRows();
  }

  bool setlistExists(const juce::String &id) const {
    for (const auto &setlist : library.setlists) {
      if (setlist.id == id)
        return true;
    }
    return false;
  }

  std::vector<SetlistRow> buildSetlistRows() const {
    const auto query = searchEditor.getText().trim().toLowerCase();
    std::vector<SetlistRow> rows;
    rows.reserve(library.setlists.size());
    for (const auto &setlist : library.setlists) {
      if (query.isNotEmpty() && !setlist.name.toLowerCase().contains(query))
        continue;
      rows.push_back({setlist.id, setlist.name,
                      static_cast<int>(setlist.items.size())});
    }
    return rows;
  }

  void refreshSetlistRows() {
    auto rows = buildSetlistRows();

    int selectedIndex = -1;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].id == selectedSetlistId) {
        selectedIndex = static_cast<int>(i);
        break;
      }
    }
    setlistContent.setRows(std::move(rows), selectedIndex);
    resized();
  }

  void refreshSongsRows() {
    std::vector<SongsListContent::SongRow> rows;
    const auto *setlist = findSetlist(selectedSetlistId);
    if (setlist != nullptr) {
      rows.reserve(setlist->items.size());
      for (size_t i = 0; i < setlist->items.size(); ++i) {
        const auto &item = setlist->items[i];
        const auto *song = findSong(item.songId);
        if (song == nullptr)
          continue;
        rows.push_back(buildSongRow(*song, static_cast<int>(i)));
      }
    }
    songsContent.setRows(std::move(rows));
    resized();
  }

  SongsListContent::SongRow buildSongRow(const ImportedSongData &song,
                                         int index) const {
    juce::ignoreUnused(index);
    const auto includedStems = static_cast<int>(std::count_if(
        song.stems.begin(), song.stems.end(),
        [](const ImportedStemData &stem) { return stem.include; }));

    juce::String meta;
    if (song.artist.isNotEmpty())
      meta << song.artist << " • ";
    if (song.key.isNotEmpty())
      meta << song.key << " • ";
    if (song.bpm > 0)
      meta << juce::String(song.bpm) << " BPM • ";
    meta << juce::String(includedStems) << " stems";

    SongsListContent::SongRow row;
    row.songId = song.id;
    row.title = song.title.isNotEmpty()
                    ? song.title
                    : L10n::tr("Cancion sin titulo", "Untitled Song");
    row.meta = meta;
    return row;
  }

  const SetlistData *findSetlist(const juce::String &id) const {
    for (const auto &setlist : library.setlists) {
      if (setlist.id == id)
        return &setlist;
    }
    return nullptr;
  }

  const ImportedSongData *findSong(const juce::String &id) const {
    for (const auto &song : library.songs) {
      if (song.id == id)
        return &song;
    }
    return nullptr;
  }

  void selectSetlistByIndex(int index) {
    auto rows = buildSetlistRows();
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
      return;
    selectedSetlistId = rows[static_cast<size_t>(index)].id;
    refreshSetlistRows();
    refreshSongsRows();
  }

  void handleSetlistAction(int index, SetlistListContent::Action action) {
    auto rows = buildSetlistRows();
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
      return;
    const auto id = rows[static_cast<size_t>(index)].id;
    switch (action) {
    case SetlistListContent::Action::Select:
      selectedSetlistId = id;
      refreshSetlistRows();
      refreshSongsRows();
      break;
    case SetlistListContent::Action::Rename:
      renameSetlist(id);
      break;
    case SetlistListContent::Action::Delete:
      deleteSetlist(id);
      break;
    }
  }

  void handleSongAction(int row, SongsListContent::Action action) {
    const auto *setlist = findSetlist(selectedSetlistId);
    if (setlist == nullptr)
      return;
    if (!juce::isPositiveAndBelow(row,
                                  static_cast<int>(setlist->items.size())))
      return;
    const auto &item = setlist->items[static_cast<size_t>(row)];
    switch (action) {
    case SongsListContent::Action::Remove:
      SongLibraryStore::removeSongFromSetlist(selectedSetlistId, item.songId);
      dirty = true;
      break;
    }
    reloadLibrary();
  }

  void showAddSongMenu() {
    const auto *setlist = findSetlist(selectedSetlistId);
    if (setlist == nullptr)
      return;
    juce::PopupMenu menu;
    for (const auto &song : library.songs) {
      const bool exists =
          std::any_of(setlist->items.begin(), setlist->items.end(),
                      [&song](const SetlistItem &item) {
                        return item.songId == song.id;
                      });
      if (exists)
        continue;
      menu.addItem(song.title.isNotEmpty()
                       ? song.title
                       : L10n::tr("Cancion sin titulo", "Untitled Song"),
                   [this, id = song.id] {
                     SongLibraryStore::addSongToSetlist(selectedSetlistId, id);
                     dirty = true;
                     reloadLibrary();
                   });
    }
    if (menu.getNumItems() == 0) {
      menu.addItem(L10n::tr("No hay canciones disponibles",
                            "No songs available"),
                   false, false, nullptr);
    }
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(addSongButton));
  }

  void createSetlist() {
    promptTextAsync(
        L10n::tr("Crear Setlist", "Create Setlist"),
        L10n::tr("Nombre del setlist:", "Setlist name:"), "",
        [this](juce::String name) {
          name = name.trim();
          if (name.isEmpty())
            return;
          const auto id = SongLibraryStore::createSetlist(name);
          if (id.isNotEmpty())
            selectedSetlistId = id;
          dirty = true;
          reloadLibrary();
        });
  }

  void renameSetlist() { renameSetlist(selectedSetlistId); }

  void renameSetlist(const juce::String &id) {
    if (id.isEmpty())
      return;
    const auto *setlist = findSetlist(id);
    if (setlist == nullptr)
      return;
    promptTextAsync(
        L10n::tr("Renombrar Setlist", "Rename Setlist"),
        L10n::tr("Nombre del setlist:", "Setlist name:"), setlist->name,
        [this, id](juce::String name) {
          name = name.trim();
          if (name.isEmpty())
            return;
          SongLibraryStore::renameSetlist(id, name);
          dirty = true;
          reloadLibrary();
        });
  }

  void deleteSetlist() { deleteSetlist(selectedSetlistId); }

  void deleteSetlist(const juce::String &id) {
    if (id.isEmpty())
      return;
    if (library.setlists.size() <= 1) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon,
          L10n::tr("No se puede eliminar", "Can't delete"),
          L10n::tr("Se requiere al menos un setlist.",
                   "At least one setlist is required."));
      return;
    }
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Eliminar setlist", "Delete setlist"),
        L10n::tr("Eliminar este setlist? Esto no quitara canciones de la biblioteca.",
                 "Delete this setlist? This won't remove songs from the library."),
        L10n::tr("Eliminar", "Delete"), L10n::tr("Cancelar", "Cancel"), this,
        juce::ModalCallbackFunction::create([this, id](int result) {
          if (result != 1)
            return;
          SongLibraryStore::deleteSetlist(id);
          selectedSetlistId = {};
          dirty = true;
          reloadLibrary();
        }));
  }

  void loadSelectedSetlist() {
    if (selectedSetlistId.isEmpty())
      return;
    if (!SongLibraryStore::setActiveSetlistId(selectedSetlistId))
      return;
    if (onSetlistLoaded)
      onSetlistLoaded(selectedSetlistId);
    dirty = false;
    closeSelf();
  }

  void promptTextAsync(const juce::String &title, const juce::String &message,
                       const juce::String &initial,
                       std::function<void(juce::String)> onComplete) {
    auto *window =
        new juce::AlertWindow(title, message, juce::AlertWindow::NoIcon);
    window->addTextEditor("value", initial, {});
    window->addButton(L10n::tr("Cancelar", "Cancel"), 0);
    window->addButton(L10n::tr("Aceptar", "OK"), 1);
    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [window, callback = std::move(onComplete)](int result) {
              if (result == 1 && callback)
                callback(window->getTextEditorContents("value").trim());
            }),
        true);
  }

  void closeSelf() {
    if (dirty && onSetlistLoaded) {
      const auto active =
          SongLibraryStore::loadLibrarySnapshot().activeSetlistId;
      onSetlistLoaded(active);
    }
    if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
      dw->exitModalState(0);
      dw->setVisible(false);
    }
  }

  juce::Label titleLabel;
  juce::TextButton createButton;
  juce::TextEditor searchEditor;
  juce::TextButton addSongButton;
  juce::TextButton loadButton;

  juce::Viewport setlistViewport;
  SetlistListContent setlistContent;
  juce::Viewport songsViewport;
  SongsListContent songsContent;

  LibraryState library;
  juce::String selectedSetlistId;
  SetlistLoadedCallback onSetlistLoaded;
  bool dirty = false;
};

void SongListPanel::openSongImportWizard() {
  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
  auto *centerTarget =
      editor != nullptr ? static_cast<juce::Component *>(editor)
                        : static_cast<juce::Component *>(this);

  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr("Importar cancion", "Import Song");
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.componentToCentreAround = centerTarget;
  auto *panel = new SongImportWizardComponent(
      [safeThis](ImportedSongData song, juce::String &errorMessage,
                 const SongImportProgressFn &progress) {
        if (safeThis == nullptr) {
          SessionAuthLogger::write(
              "[SongImport] abort: SongListPanel no longer available");
          errorMessage =
              L10n::tr("La vista de importacion ya no esta disponible.",
                       "Import UI is no longer available.");
          return false;
        }

        SessionAuthLogger::write(
            "[SongImport] appendSong begin title=\"" + song.title.trim() +
            "\" id=" + song.id + " stems=" +
            juce::String(static_cast<int>(song.stems.size())));

        juce::String importError;
        const bool success =
            SongLibraryStore::appendSong(std::move(song), &importError, progress);

        if (!success) {
          SessionAuthLogger::write("[SongImport] appendSong failed error=\"" +
                                   importError + "\"");
          errorMessage = importError.isNotEmpty() ? importError
                                                  : L10n::tr(
                                                        "No se pudo guardar la cancion importada.",
                                                        "Couldn't persist the imported song.");
          return false;
        }

        SessionAuthLogger::write(
            "[SongImport] appendSong ok, refreshing storage and waveform cache");

        if (progress)
          progress(0.97, L10n::tr("Actualizando lista de canciones…",
                                  "Refreshing song list…"));
        const auto allSongs = SongLibraryStore::loadSongs();
        if (!allSongs.empty()) {
          const auto &saved = allSongs.back();
          if (progress)
            progress(0.98, L10n::tr("Generando vista de forma de onda…",
                                    "Generating waveform preview…"));
          auto bins = buildMixedWaveformBins(
              saved, 1400,
              [&progress](const double wf01) {
                if (progress)
                  progress(0.98 + 0.02 * juce::jlimit(0.0, 1.0, wf01),
                           L10n::tr("Generando vista de forma de onda…",
                                    "Generating waveform preview…"));
              });
          SongLibraryStore::saveWaveformCache(saved.id, bins);
        }

        if (progress)
          progress(1.0, L10n::tr("Importacion completa", "Import complete"));

        safeThis->refreshFromStorage();
        SessionAuthLogger::write("[SongImport] import flow completed successfully");
        return true;
      },
      AppSettings::getPrimeImportEnabled());
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    window->setResizable(true, true);
    window->centreAroundComponent(centerTarget, 860, 640);
  }
}

void SongListPanel::openSongEditWizard(const juce::String &songId) {
  const auto trimmedSongId = songId.trim();
  if (trimmedSongId.isEmpty())
    return;

  ImportedSongData baseSong;
  bool found = false;
  for (const auto &song : librarySongs) {
    if (song.id == trimmedSongId) {
      baseSong = song;
      found = true;
      break;
    }
  }
  if (!found) {
    const auto snapshotSongs = SongLibraryStore::loadSongs();
    for (const auto &song : snapshotSongs) {
      if (song.id == trimmedSongId) {
        baseSong = song;
        found = true;
        break;
      }
    }
  }
  if (!found)
    return;

  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
  auto *centerTarget =
      editor != nullptr ? static_cast<juce::Component *>(editor)
                        : static_cast<juce::Component *>(this);

  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr("Editar cancion", "Edit Song");
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.componentToCentreAround = centerTarget;

  auto *panel = new SongImportWizardComponent(
      [safeThis, editedSongId = trimmedSongId](ImportedSongData song,
                                               juce::String &errorMessage,
                                               const SongImportProgressFn &progress) {
        if (safeThis == nullptr) {
          errorMessage = L10n::tr("La vista de edicion ya no esta disponible.",
                                  "Edit UI is no longer available.");
          return false;
        }

        song.id = editedSongId;
        juce::String updateError;
        const bool success =
            SongLibraryStore::updateSong(std::move(song), &updateError, progress);
        if (!success) {
          errorMessage = updateError.isNotEmpty()
                             ? updateError
                             : L10n::tr("No se pudo actualizar la cancion.",
                                        "Couldn't update the song.");
          return false;
        }

        safeThis->refreshFromStorage();

        const auto shouldReloadLiveSong =
            editedSongId == safeThis->selectedSongId ||
            editedSongId == safeThis->nowPlayingSongId;

        if (!shouldReloadLiveSong || safeThis->onSongActivated == nullptr)
          return true;

        const auto allSongs = SongLibraryStore::loadSongs();
        const auto it =
            std::find_if(allSongs.begin(), allSongs.end(),
                         [&editedSongId](const ImportedSongData &candidate) {
                           return candidate.id == editedSongId;
                         });
        if (it == allSongs.end())
          return true;

        const int indexForSetlist = safeThis->getSongIndexById(editedSongId);
        safeThis->onSongActivated(*it, indexForSetlist >= 0 ? indexForSetlist : -1);
        return true;
      },
      AppSettings::getPrimeImportEnabled(), SongImportWizardComponent::Mode::Edit,
      baseSong);
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    window->setResizable(true, true);
    window->centreAroundComponent(centerTarget, 860, 640);
  }
}

void SongListPanel::openSetlistManager() {
  showSetlistsView();
}

void SongListPanel::showSetlistsView() {
  songsSwipeGestureActive = false;
  songsSubview = SongsSubview::SetlistSongs;
  songsSubviewOffsetPx = 0.0f;
  const auto previousMode = viewMode;
  viewMode = ViewMode::Setlists;
  initialViewApplied = true;
  refreshFromStorage();
  animateTransitionIfNeeded(previousMode);
}

void SongListPanel::showSongsView(const juce::String &setlistId) {
  const auto previousMode = viewMode;
  if (setlistId.isNotEmpty())
    SongLibraryStore::setActiveSetlistId(setlistId);
  viewMode = ViewMode::Songs;
  songsSubview = SongsSubview::SetlistSongs;
  songsSubviewOffsetPx = 0.0f;
  songsSwipeGestureActive = false;
  initialViewApplied = true;
  refreshFromStorage();
  animateTransitionIfNeeded(previousMode);
}

void SongListPanel::showLibraryView(const bool animate) {
  if (viewMode != ViewMode::Songs)
    return;
  if (songsSubview == SongsSubview::Library && !songsSwipeGestureActive)
    return;

  songsSwipeGestureActive = false;
  songsSubview = SongsSubview::Library;
  if (animate)
    animateSongsSubviewToCurrent();
  else {
    const auto width = static_cast<float>(juce::jmax(1, cachedSongsLayout.workspace.getWidth()));
    applySongsSubviewOffset(-width);
    applyModeVisibility();
    repaint();
  }
}

void SongListPanel::showSongsSubview(const bool animate) {
  if (viewMode != ViewMode::Songs)
    return;
  if (songsSubview == SongsSubview::SetlistSongs && !songsSwipeGestureActive)
    return;

  songsSwipeGestureActive = false;
  songsSubview = SongsSubview::SetlistSongs;
  if (animate)
    animateSongsSubviewToCurrent();
  else {
    applySongsSubviewOffset(0.0f);
    applyModeVisibility();
    repaint();
  }
}

void SongListPanel::applyInitialViewFromStorage() {
  if (initialViewApplied && !(viewMode == ViewMode::Setlists && activeSetlistId.isEmpty()))
    return;
  if (activeSetlistId.isNotEmpty()) {
    viewMode = ViewMode::Songs;
    songsSubview = SongsSubview::SetlistSongs;
  } else {
    viewMode = ViewMode::Setlists;
    songsSubview = SongsSubview::SetlistSongs;
  }
  initialViewApplied = true;
}

void SongListPanel::applyModeVisibility() {
  const bool showSongs = viewMode == ViewMode::Songs;
  const bool hasActiveSetlist = activeSetlistId.isNotEmpty();
  const bool showSetlistSongsSubview =
      showSongs && songsSubview == SongsSubview::SetlistSongs;
  const bool showLibrarySubview =
      showSongs && songsSubview == SongsSubview::Library;

  titleLabel.setVisible(showSongs);
  backButton.setVisible(showSongs);
  editSetlistButton.setVisible(false);
  headerMenuButton.setVisible(showSongs);
  songsHeaderPanel.setVisible(showSongs);
  listViewport.setVisible(showSetlistSongsSubview);
  libraryCatalogPanel.setVisible(showLibrarySubview);

  setlistsHeaderLabel.setVisible(!showSongs);
  setlistsSubtitleLabel.setVisible(!showSongs);
  createSetlistButton.setVisible(!showSongs);
  setlistSearchField.setVisible(!showSongs);
  setlistsViewport.setVisible(!showSongs);

  editSetlistButton.setEnabled(false);
  headerMenuButton.setEnabled(hasActiveSetlist);
  songsHeaderPanel.setActionsEnabled(hasActiveSetlist &&
                                     songsSubview == SongsSubview::SetlistSongs);
  createSetlistButton.setEnabled(!showSongs);
  setlistSearchField.setEnabled(!showSongs);
  backButton.setEnabled(true);

  for (juce::Component *c : std::array<juce::Component *, 11>{
           &titleLabel,         &backButton,          &headerMenuButton,
           &songsHeaderPanel,    &listViewport,       &libraryCatalogPanel,
           &setlistsHeaderLabel,&setlistsSubtitleLabel,
           &createSetlistButton,&setlistSearchField,
           &setlistsViewport}) {
    c->setAlpha(1.0f);
  }
}

void SongListPanel::animateTransitionIfNeeded(const ViewMode previousMode) {
  if (previousMode == viewMode)
    return;
  if (getWidth() <= 0 || getHeight() <= 0)
    return;

  auto &animator = juce::Desktop::getInstance().getAnimator();
  animator.cancelAllAnimations(true);
  stopTimer();

  applySongsLayout(computeSongsLayout());
  applySetlistsLayout(computeSetlistsLayout());

  std::array<juce::Component *, 6> songsGroup = {
      &titleLabel, &backButton, &headerMenuButton,
      &songsHeaderPanel, &listViewport, &libraryCatalogPanel};
  std::array<juce::Component *, 5> setlistsGroup = {
      &setlistsHeaderLabel, &setlistsSubtitleLabel, &createSetlistButton,
      &setlistSearchField, &setlistsViewport};

  const int slideDistance = juce::jmax(60, getWidth() - 20);
  const bool toSongs = viewMode == ViewMode::Songs;

  for (auto *c : songsGroup)
    c->setVisible(true);
  for (auto *c : setlistsGroup)
    c->setVisible(true);

  if (toSongs) {
    for (auto *c : songsGroup) {
      const auto target = c->getBounds();
      c->setBounds(target.translated(slideDistance, 0));
      c->setAlpha(0.0f);
      animator.animateComponent(c, target, 1.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
    for (auto *c : setlistsGroup) {
      const auto target = c->getBounds().translated(-slideDistance, 0);
      animator.animateComponent(c, target, 0.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
  } else {
    for (auto *c : setlistsGroup) {
      const auto target = c->getBounds();
      c->setBounds(target.translated(-slideDistance, 0));
      c->setAlpha(0.0f);
      animator.animateComponent(c, target, 1.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
    for (auto *c : songsGroup) {
      const auto target = c->getBounds().translated(slideDistance, 0);
      animator.animateComponent(c, target, 0.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
  }

  transitionInFlight = true;
  startTimer(transitionDurationMs + 24);
}

void SongListPanel::timerCallback() {
  stopTimer();
  if (!transitionInFlight)
    return;
  transitionInFlight = false;
  songsSubviewTransitionInFlight = false;
  juce::Desktop::getInstance().getAnimator().cancelAllAnimations(true);
  finishSongsSubviewAnimation();
  applyModeVisibility();
  resized();
  repaint();
}

void SongListPanel::refreshFromStorage() {
  auto library = SongLibraryStore::loadLibrarySnapshot();
  librarySongs = library.songs;
  activeSetlistId = library.activeSetlistId;
  activeSetlistName = L10n::tr("Setlist", "Setlist");
  activeSongIndices.clear();

  const SetlistData *activeSetlist = nullptr;
  for (const auto &setlist : library.setlists) {
    if (setlist.id == activeSetlistId) {
      activeSetlist = &setlist;
      break;
    }
  }
  if (activeSetlist == nullptr && !library.setlists.empty()) {
    activeSetlist = &library.setlists.front();
    activeSetlistId = activeSetlist->id;
  }
  if (activeSetlist != nullptr)
    activeSetlistName = activeSetlist->name;
  applyInitialViewFromStorage();
  if (setlistsContent != nullptr) {
    std::vector<SetlistsContent::Row> setlistRows;
    setlistRows.reserve(library.setlists.size());
    const auto query = setlistSearchQuery.trim().toLowerCase();
    for (const auto &setlist : library.setlists) {
      if (query.isNotEmpty() &&
          !setlist.name.toLowerCase().contains(query))
        continue;

      double totalSeconds = 0.0;
      for (const auto &item : setlist.items) {
        const auto it =
            std::find_if(librarySongs.begin(), librarySongs.end(),
                         [&item](const ImportedSongData &song) {
                           return song.id == item.songId;
                         });
        if (it == librarySongs.end())
          continue;
        totalSeconds += estimateSongLengthSeconds(*it);
      }

      setlistRows.push_back({setlist.id, toCapitalCase(setlist.name),
                             static_cast<int>(setlist.items.size()),
                             totalSeconds});
    }
    setlistsContent->setRows(std::move(setlistRows), activeSetlistId);
  }

  std::vector<SongRow> rows;
  if (activeSetlist != nullptr)
    rows.reserve(activeSetlist->items.size());

  int order = 1;
  if (activeSetlist != nullptr) {
    for (size_t i = 0; i < activeSetlist->items.size(); ++i) {
      const auto &item = activeSetlist->items[i];
      const auto it =
          std::find_if(librarySongs.begin(), librarySongs.end(),
                       [&item](const ImportedSongData &song) {
                         return song.id == item.songId;
                       });
      if (it == librarySongs.end())
        continue;
      const auto &song = *it;
      const int libraryIndex =
          static_cast<int>(std::distance(librarySongs.begin(), it));
      activeSongIndices.push_back(libraryIndex);

      const auto includedStems = static_cast<int>(std::count_if(
          song.stems.begin(), song.stems.end(),
          [](const ImportedStemData &stem) { return stem.include; }));

      const int rowIndex = static_cast<int>(rows.size());
      const auto autoPlayKey = makeAutoPlayNextMarkKey(activeSetlistId, song.id);
      rows.push_back({order++,
                      toSentenceCase(song.title.isNotEmpty()
                                         ? song.title
                                         : L10n::tr("Cancion sin titulo",
                                                    "Untitled Song")),
                      song.artist.trim().toUpperCase(),
                      song.key,
                      song.bpm,
                      includedStems,
                      song.id,
                      autoPlayNextMarks.contains(autoPlayKey),
                      isPlaybackActive && song.id == nowPlayingSongId,
                      song.id == selectedSongId,
                      rowIndex,
                      estimateSongLengthSeconds(song)});
    }
  }

  if (!juce::isPositiveAndBelow(lastActivatedIndex,
                                static_cast<int>(activeSongIndices.size())))
    lastActivatedIndex = activeSongIndices.empty() ? -1 : 0;

  if (selectedSongId.isEmpty() &&
      juce::isPositiveAndBelow(lastActivatedIndex,
                               static_cast<int>(activeSongIndices.size()))) {
    const int libIndex = activeSongIndices[static_cast<size_t>(lastActivatedIndex)];
    if (juce::isPositiveAndBelow(libIndex, static_cast<int>(librarySongs.size())))
      selectedSongId = librarySongs[static_cast<size_t>(libIndex)].id;
  }

  listContent.setSongs(std::move(rows));
  listContent.setSelectedSongId(selectedSongId);
  libraryCatalogPanel.setActiveSetlistId(activeSetlistId);
  libraryCatalogPanel.setNowPlayingSong(nowPlayingSongId, isPlaybackActive);
  libraryCatalogPanel.refreshFromStorage();
  refreshSongsHeaderSummary();
  titleLabel.setText(toCapitalCase(activeSetlistName),
                     juce::dontSendNotification);
  if (!transitionInFlight)
    applyModeVisibility();
  if (!transitionInFlight)
    resized();
  if (onSetlistNameChanged)
    onSetlistNameChanged(activeSetlistName);
}
