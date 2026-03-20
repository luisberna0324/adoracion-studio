#include "SongListPanel.h"

#include "SongImportWizardComponent.h"

#include <algorithm>
#include <utility>

SongListPanel::AddSongButton::AddSongButton() : juce::Button("addSong") {}

void SongListPanel::AddSongButton::paintButton(juce::Graphics &g,
                                               bool isMouseOverButton,
                                               bool isButtonDown) {
  auto b = getLocalBounds().toFloat();

  juce::Colour fill = UITheme::cardBg;
  if (isMouseOverButton)
    fill = UITheme::cardBgHover;
  if (isButtonDown)
    fill = UITheme::cardBgPressed;

  g.setColour(fill);
  g.fillRoundedRectangle(b, 5.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.82f));
  g.drawRoundedRectangle(b, 5.0f, 1.0f);

  auto iconRect = b.removeFromLeft(24.0f).reduced(4.0f);
  drawIconGlyph(g, iconRect, IconGlyph::Add, UITheme::accent, 1.6f);

  g.setColour(UITheme::textPrimary);
  g.setFont(juce::FontOptions(10.5f).withKerningFactor(0.08f));
  g.drawText("Add Song", b.reduced(4.0f, 0.0f).toNearestInt(),
             juce::Justification::centredLeft);
}

SongListPanel::SongListPanel() {
  listViewport.setViewedComponent(&listContent, false);
  listViewport.setScrollBarsShown(true, false, false, false);
  listViewport.setScrollBarThickness(1);
  listViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(listViewport);
  addAndMakeVisible(addSongButton);

  addSongButton.onClick = [this] { openSongImportWizard(); };
  listContent.setOnSongActivated([this](int songIndex) {
    if (!juce::isPositiveAndBelow(songIndex,
                                  static_cast<int>(librarySongs.size())))
      return;
    lastActivatedIndex = songIndex;
    if (onSongActivated)
      onSongActivated(librarySongs[static_cast<size_t>(songIndex)], songIndex);
  });
  refreshFromStorage();
}

SongListPanel::~SongListPanel() = default;

void SongListPanel::setOnSongActivated(
    std::function<void(const ImportedSongData &, int)> cb) {
  onSongActivated = std::move(cb);
}

int SongListPanel::getSongCount() const {
  return static_cast<int>(librarySongs.size());
}

const ImportedSongData *SongListPanel::getSongPtr(int index) const {
  if (!juce::isPositiveAndBelow(index, static_cast<int>(librarySongs.size())))
    return nullptr;
  return &librarySongs[static_cast<size_t>(index)];
}

int SongListPanel::getLastActivatedIndex() const { return lastActivatedIndex; }

void SongListPanel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.75f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  g.setColour(UITheme::textPrimary);
  g.setFont(juce::FontOptions(18.0f));
  g.drawText("Setlist A", 14, 8, getWidth() - 190, 24,
             juce::Justification::centredLeft);

  const float pencilX = static_cast<float>(addSongButton.getX() - 24);
  drawIconGlyph(g, juce::Rectangle<float>(pencilX, 8.0f, 20.0f, 22.0f),
                IconGlyph::Pencil, UITheme::accent, 1.6f);
}

void SongListPanel::resized() {
  auto area = getLocalBounds().reduced(10);
  auto header = area.removeFromTop(30);

  const int addW = 126;
  addSongButton.setBounds(header.getRight() - addW, header.getY() + 3, addW,
                          24);

  area.removeFromTop(8);
  listViewport.setBounds(area);
  listContent.setSize(juce::jmax(1, listViewport.getWidth()),
                      juce::jmax(listViewport.getHeight(),
                                 listContent.getPreferredHeight()));
}

void SongListPanel::ListContent::setSongs(std::vector<SongRow> rows) {
  songs = std::move(rows);
  repaint();
}

void SongListPanel::ListContent::setOnSongActivated(
    std::function<void(int)> cb) {
  onSongActivated = std::move(cb);
}

int SongListPanel::ListContent::getPreferredHeight() const {
  if (songs.empty())
    return 88;
  const int rowH = 52;
  const int gap = 6;
  return 8 + static_cast<int>(songs.size()) * (rowH + gap);
}

void SongListPanel::ListContent::paint(juce::Graphics &g) {
  if (songs.empty()) {
    g.setColour(UITheme::textSecondary.withAlpha(0.88f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText("No songs yet. Use Add Song to import stems.",
                     getLocalBounds().reduced(10),
                     juce::Justification::centred, 2);
    return;
  }

  const int rowH = 52;
  const int gap = 6;

  for (size_t i = 0; i < songs.size(); ++i) {
    auto row = juce::Rectangle<int>(
        8, 8 + static_cast<int>(i) * (rowH + gap), getWidth() - 16, rowH);
    g.setColour(UITheme::cardBgHover);
    g.fillRoundedRectangle(row.toFloat(), 4.0f);

    g.setColour(UITheme::cardBorder.withAlpha(0.58f));
    g.drawRoundedRectangle(row.toFloat(), 4.0f, 1.0f);

    auto c = row.reduced(8, 6);
    auto orderRect = c.removeFromLeft(24);
    g.setColour(UITheme::textSecondary);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(juce::String(songs[i].order), orderRect,
               juce::Justification::centred);

    auto artRect = c.removeFromLeft(40).reduced(2);
    g.setColour(UITheme::accent.withAlpha(0.68f));
    g.fillRoundedRectangle(artRect.toFloat(), 3.0f);
    g.setColour(UITheme::cardBg.withAlpha(0.75f));
    g.drawLine(static_cast<float>(artRect.getX() + 6),
               static_cast<float>(artRect.getCentreY()),
               static_cast<float>(artRect.getRight() - 6),
               static_cast<float>(artRect.getCentreY()), 1.2f);

    c.removeFromLeft(8);
    auto optionsRect = c.removeFromRight(18);
    auto checkRect = c.removeFromRight(20).reduced(2);
    c.removeFromRight(8);

    g.setColour(UITheme::textPrimary.darker(0.16f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(songs[i].title, c.removeFromTop(16),
                     juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textSecondary);
    g.setFont(juce::FontOptions(9.0f));
    g.drawFittedText(songs[i].meta, c, juce::Justification::centredLeft, 1);

    g.setColour(UITheme::cardBorder.darker(0.16f).withAlpha(0.85f));
    g.drawRect(checkRect, 1);
    drawIconGlyph(g, optionsRect.toFloat(), IconGlyph::More,
                  UITheme::textSecondary, 1.4f);
  }
}

void SongListPanel::ListContent::mouseDoubleClick(
    const juce::MouseEvent &event) {
  const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
  if (!juce::isPositiveAndBelow(idx, static_cast<int>(songs.size())))
    return;

  if (onSongActivated)
    onSongActivated(songs[static_cast<size_t>(idx)].songIndex);
}

int SongListPanel::ListContent::rowIndexAtY(int y) const {
  constexpr int rowStart = 8;
  constexpr int rowH = 52;
  constexpr int gap = 6;

  if (y < rowStart)
    return -1;

  const int relY = y - rowStart;
  const int step = rowH + gap;
  const int row = relY / step;
  if (!juce::isPositiveAndBelow(row, static_cast<int>(songs.size())))
    return -1;

  if ((relY % step) >= rowH)
    return -1;

  return row;
}

void SongListPanel::openSongImportWizard() {
  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);

  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = "Import Song";
  options.dialogBackgroundColour = juce::Colour::fromRGB(24, 26, 30);
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.componentToCentreAround = this;
  options.content.setOwned(new SongImportWizardComponent(
      [safeThis](ImportedSongData song) {
        if (safeThis == nullptr)
          return;

        if (!SongLibraryStore::appendSong(std::move(song))) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon, "Save error",
              "Couldn't persist the imported song.");
          return;
        }

        // Pre-generate waveform cache for the newly imported song
        const auto allSongs = SongLibraryStore::loadSongs();
        if (!allSongs.empty()) {
          const auto &saved = allSongs.back();
          auto bins = buildMixedWaveformBins(saved);
          SongLibraryStore::saveWaveformCache(saved.id, bins);
        }

        safeThis->refreshFromStorage();
      }));

  if (auto *window = options.launchAsync()) {
    window->setResizable(true, true);
    window->centreAroundComponent(this, 780, 580);
  }
}

void SongListPanel::refreshFromStorage() {
  librarySongs = SongLibraryStore::loadSongs();
  if (!juce::isPositiveAndBelow(lastActivatedIndex,
                                static_cast<int>(librarySongs.size())))
    lastActivatedIndex = librarySongs.empty() ? -1 : 0;

  std::vector<SongRow> rows;
  rows.reserve(librarySongs.size());

  int order = 1;
  for (size_t i = 0; i < librarySongs.size(); ++i) {
    const auto &song = librarySongs[i];
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

    rows.push_back({order++,
                    song.title.isNotEmpty() ? song.title : "Untitled Song",
                    meta, static_cast<int>(i)});
  }

  listContent.setSongs(std::move(rows));
  resized();
}
