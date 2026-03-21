#include "SongListPanel.h"

#include "SongImportWizardComponent.h"
#include "../../PluginEditor.h"

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

SongListPanel::ManageSetlistsButton::ManageSetlistsButton()
    : juce::Button("setlistsButton") {}

void SongListPanel::ManageSetlistsButton::paintButton(
    juce::Graphics &g, bool isMouseOverButton, bool isButtonDown) {
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
  drawIconGlyph(g, iconRect, IconGlyph::Settings, UITheme::accent, 1.6f);

  g.setColour(UITheme::textPrimary);
  g.setFont(juce::FontOptions(10.5f).withKerningFactor(0.08f));
  g.drawText("Setlists", b.reduced(4.0f, 0.0f).toNearestInt(),
             juce::Justification::centredLeft);
}

SongListPanel::SongListPanel() {
  listViewport.setViewedComponent(&listContent, false);
  listViewport.setScrollBarsShown(true, false, false, false);
  listViewport.setScrollBarThickness(1);
  listViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(listViewport);
  addAndMakeVisible(addSongButton);
  addAndMakeVisible(manageSetlistsButton);

  addSongButton.onClick = [this] { openSongImportWizard(); };
  manageSetlistsButton.onClick = [this] { openSetlistManager(); };

  listContent.setOnSongActivated([this](int songIndex) {
    if (!juce::isPositiveAndBelow(songIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    lastActivatedIndex = songIndex;
    const int libraryIndex =
        activeSongIndices[static_cast<size_t>(songIndex)];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      return;
    if (onSongActivated)
      onSongActivated(librarySongs[static_cast<size_t>(libraryIndex)],
                      songIndex);
  });
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

int SongListPanel::getLastActivatedIndex() const { return lastActivatedIndex; }

juce::String SongListPanel::getActiveSetlistName() const {
  return activeSetlistName;
}

void SongListPanel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.75f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  g.setColour(UITheme::textPrimary);
  g.setFont(juce::FontOptions(18.0f));
  const auto title =
      activeSetlistName.isNotEmpty() ? activeSetlistName : "Setlist";
  g.drawText(title, 14, 8, getWidth() - 220, 24,
             juce::Justification::centredLeft);
}

void SongListPanel::resized() {
  auto area = getLocalBounds().reduced(10);
  auto header = area.removeFromTop(30);

  const int addW = 126;
  addSongButton.setBounds(header.getRight() - addW, header.getY() + 3, addW,
                          24);
  const int manageW = 110;
  manageSetlistsButton.setBounds(addSongButton.getX() - manageW - 8,
                                 header.getY() + 3, manageW, 24);

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
    g.drawFittedText("No songs in this setlist yet.",
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

class SongListPanel::SetlistManagerPanel final : public juce::Component {
public:
  using SetlistLoadedCallback = std::function<void(const juce::String &)>;

  explicit SetlistManagerPanel(SetlistLoadedCallback onLoaded)
      : onSetlistLoaded(std::move(onLoaded)) {
    setSize(860, 560);

    titleLabel.setText("Setlists", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(18.0f));
    addAndMakeVisible(titleLabel);

    createButton.setButtonText("Create a Setlist");
    createButton.onClick = [this] { createSetlist(); };
    addAndMakeVisible(createButton);

    searchEditor.setTextToShowWhenEmpty("Search Setlists",
                                        UITheme::textSecondary);
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

    addSongButton.setButtonText("Add Song");
    addSongButton.onClick = [this] { showAddSongMenu(); };
    addAndMakeVisible(addSongButton);

    loadButton.setButtonText("Load setlist");
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
        g.drawFittedText("No setlists yet.", getLocalBounds().reduced(10),
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
        g.drawFittedText(juce::String(rows[i].songCount) + " songs", textArea,
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
        g.drawFittedText("No songs yet.", getLocalBounds().reduced(10),
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
    row.title = song.title.isNotEmpty() ? song.title : "Untitled Song";
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
      menu.addItem(song.title.isNotEmpty() ? song.title : "Untitled Song",
                   [this, id = song.id] {
                     SongLibraryStore::addSongToSetlist(selectedSetlistId, id);
                     dirty = true;
                     reloadLibrary();
                   });
    }
    if (menu.getNumItems() == 0) {
      menu.addItem("No songs available", false, false, nullptr);
    }
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(addSongButton));
  }

  void createSetlist() {
    promptTextAsync(
        "Create Setlist", "Setlist name:", "",
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
        "Rename Setlist", "Setlist name:", setlist->name,
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
          juce::AlertWindow::WarningIcon, "Can't delete",
          "At least one setlist is required.");
      return;
    }
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon, "Delete setlist",
        "Delete this setlist? This won't remove songs from the library.",
        "Delete", "Cancel", this,
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
    window->addButton("Cancel", 0);
    window->addButton("OK", 1);
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
  options.dialogTitle = "Import Song";
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.componentToCentreAround = centerTarget;
  auto *panel = new SongImportWizardComponent(
      [safeThis](ImportedSongData song) {
        if (safeThis == nullptr)
          return;

        if (!SongLibraryStore::appendSong(std::move(song))) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon, "Save error",
              "Couldn't persist the imported song.");
          return;
        }

        const auto allSongs = SongLibraryStore::loadSongs();
        if (!allSongs.empty()) {
          const auto &saved = allSongs.back();
          auto bins = buildMixedWaveformBins(saved);
          SongLibraryStore::saveWaveformCache(saved.id, bins);
        }

        safeThis->refreshFromStorage();
      });
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    window->setResizable(true, true);
    window->centreAroundComponent(centerTarget, 780, 580);
  }
}

void SongListPanel::openSetlistManager() {
  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
  auto *centerTarget =
      editor != nullptr ? static_cast<juce::Component *>(editor)
                        : static_cast<juce::Component *>(this);

  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = "Setlists";
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.componentToCentreAround = centerTarget;

  auto *panel = new SetlistManagerPanel(
      [safeThis](const juce::String &) {
        if (safeThis == nullptr)
          return;
        safeThis->refreshFromStorage();
      });
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    window->setResizable(true, true);
    window->centreAroundComponent(centerTarget, 900, 600);
  }
}

void SongListPanel::refreshFromStorage() {
  auto library = SongLibraryStore::loadLibrarySnapshot();
  librarySongs = library.songs;
  activeSetlistId = library.activeSetlistId;
  activeSetlistName = "Setlist";
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

      juce::String meta;
      if (song.artist.isNotEmpty())
        meta << song.artist << " • ";
      if (song.key.isNotEmpty())
        meta << song.key << " • ";
      if (song.bpm > 0)
        meta << juce::String(song.bpm) << " BPM • ";
      meta << juce::String(includedStems) << " stems";

      const int rowIndex = static_cast<int>(rows.size());
      rows.push_back({order++,
                      song.title.isNotEmpty() ? song.title : "Untitled Song",
                      meta, rowIndex});
    }
  }

  if (!juce::isPositiveAndBelow(lastActivatedIndex,
                                static_cast<int>(activeSongIndices.size())))
    lastActivatedIndex = activeSongIndices.empty() ? -1 : 0;

  listContent.setSongs(std::move(rows));
  resized();
  if (onSetlistNameChanged)
    onSetlistNameChanged(activeSetlistName);
}
