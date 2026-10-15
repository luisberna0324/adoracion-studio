#include "SongImportWizardComponent.h"

#include "../../data/Localization.h"
#include "../../data/SessionAuthLogger.h"
#include "../../data/SongLibraryStore.h"
#include "../../data/ProjectStore.h"
#include "../UiCommon.h"
#include "../components/SongLoadProgressOverlay.h"

#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
#include "../../data/MobileImportStaging.h"
#endif

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <utility>
#include <set>
#include <thread>

namespace {
#if JUCE_IOS || JUCE_ANDROID
constexpr auto kCreationExtensions = "wav;aif;aiff;flac;mp3";
constexpr auto kCreationFormatNames = "WAV, AIFF, FLAC, MP3";
// Mobile decodes WAV/AIFF/FLAC via JUCE in-process. MP3 is added on 2026-09-22
// thanks to JUCE's built-in MinMP3 decoder (no external lib, no FFmpeg). The
// PCM source is fed to the same FLAC packager the desktop uses.
constexpr auto kStemImportExtensions = "wav;aif;aiff;flac;mp3";
constexpr auto kStemImportFormatNames = "WAV, AIFF, FLAC, MP3";
#else
constexpr auto kCreationExtensions = "wav;aif;aiff;flac;mp3;m4a";
constexpr auto kCreationFormatNames = "WAV, AIFF, FLAC, MP3, M4A";
// Desktop project saving decodes these inputs with FFmpeg and packages FLAC.
// The picker and validation must use the same list, including formats that
// JUCE cannot inspect natively on every desktop OS (e.g. WMA or AAC).
constexpr auto kStemImportExtensions = "wav;aif;aiff;flac;mp3;m4a;aac;ogg;wma";
constexpr auto kStemImportFormatNames = "WAV, AIFF, FLAC, MP3, M4A, AAC, OGG, WMA";
#endif

juce::String stemImportWildcard(bool creating) {
#if JUCE_ANDROID
  // Android SAF filters strictly by MIME at the provider level. The picker
  // is wired from the extension wildcard via
  // juce_FileChooser_android.cpp:convertFiltersToMimeTypes(), so passing the
  // real extension list emits setType("audio/*") plus an EXTRA_MIME_TYPES
  // array with audio/wav, audio/aiff, audio/flac, audio/mpeg, etc. Several
  // providers (Samsung My Files, DocumentsUI fronting Google Drive, generic
  // MediaStore folders where the indexer has not tagged the file) report
  // audio as application/octet-stream or as audio/* generically, and the
  // picker silently hides them. Returning "*" makes JUCE's converter skip
  // every token so the intent ends up with setType("*/*") and no MIME extra;
  // every file becomes visible. Format validation still happens later in
  // MobileImportStaging::stage() and PortableFlacConversion's reader, so the
  // wizard never persists a non-audio source.
  juce::ignoreUnused(creating);
  return "*";
#else
  auto extensions = juce::StringArray::fromTokens(
      creating ? kCreationExtensions : kStemImportExtensions, ";", "");
  for (auto& extension : extensions)
    extension = "*." + extension;
  return extensions.joinIntoString(";");
#endif
}

bool hasKeyboardInsets(const juce::BorderSize<int> &insets) {
  return insets.getTop() > 0 || insets.getLeft() > 0 ||
         insets.getBottom() > 0 || insets.getRight() > 0;
}

juce::BorderSize<int>
combinedPhoneInsets(const juce::BorderSize<int> &safe,
                    const juce::BorderSize<int> &keyboard) {
#if JUCE_ANDROID
  // JUCE reports Android IME insets after removing system/cutout insets, so
  // both regions are additive.
  return {safe.getTop() + keyboard.getTop(),
          safe.getLeft() + keyboard.getLeft(),
          safe.getBottom() + keyboard.getBottom(),
          safe.getRight() + keyboard.getRight()};
#else
  // iOS keyboard frames are measured from the physical screen edge and already
  // cover the home-indicator region. Use the larger obstruction per edge.
  return {juce::jmax(safe.getTop(), keyboard.getTop()),
          juce::jmax(safe.getLeft(), keyboard.getLeft()),
          juce::jmax(safe.getBottom(), keyboard.getBottom()),
          juce::jmax(safe.getRight(), keyboard.getRight())};
#endif
}

class MobileWizardButtonLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool isHighlighted, bool isDown) override {
    auto area = button.getLocalBounds().toFloat().reduced(0.5f);
    auto colour = backgroundColour;
    if (isDown)
      colour = colour.darker(0.16f);
    else if (isHighlighted)
      colour = colour.brighter(0.08f);
    g.setColour(colour);
    g.fillRoundedRectangle(area, 14.0f);
  }

  juce::Font getTextButtonFont(juce::TextButton &, int) override {
    return makeAppFont(17.0f, AppFontWeight::Bold);
  }
};
} // namespace

class SongImportWizardComponent::BlankSectionRow : public juce::Component {
public:
  juce::TextEditor name, bars;
  juce::TextButton remove;
  SongArrangementPartSnapshot original;
  explicit BlankSectionRow(SongImportWizardComponent &owner) {
    owner.configureTextEditor(name, L10n::tr("Nombre de sección", "Section name"));
    owner.configureTextEditor(bars, L10n::tr("Compases", "Bars"));
    bars.setInputRestrictions(5, "0123456789");
    bars.setKeyboardType(juce::TextInputTarget::numericKeyboard);
    name.setText("Intro"); bars.setText("4");
    name.onTextChange = [&owner] { owner.blankArrangementEdited = true; };
    bars.onTextChange = [&owner] { owner.blankArrangementEdited = true; };
    name.onReturnKey = [&owner] { owner.advanceTextEditor(); };
    bars.onReturnKey = [&owner] { owner.advanceTextEditor(); };
    remove.setButtonText(L10n::tr("Quitar", "Remove"));
    owner.styleSecondaryButton(remove);
    for (auto *c : {static_cast<juce::Component *>(&name), static_cast<juce::Component *>(&bars), static_cast<juce::Component *>(&remove)})
      addAndMakeVisible(c);
  }
  void resized() override {
    auto row = getLocalBounds().reduced(0, 5);
    remove.setBounds(row.removeFromRight(68)); row.removeFromRight(6);
    bars.setBounds(row.removeFromRight(66)); row.removeFromRight(6);
    name.setBounds(row);
  }
};

void SongImportWizardComponent::addBlankSection(const SongArrangementPartSnapshot *part) {
  if (blankSections.size() >= 128) return;
  auto row = std::make_unique<BlankSectionRow>(*this);
  if (part != nullptr) {
    row->original = *part;
    row->name.setText(part->name, false);
    const auto bar = songBarDurationSeconds(initialSong);
    row->bars.setText(juce::String(juce::jmax(1, static_cast<int>(std::lround(
        bar > 0.0 ? (part->endSec - part->startSec) / bar : 4.0)))), false);
  }
  if (row->original.id.isEmpty()) row->original.id = juce::Uuid().toString();
  auto *identity = row.get();
  const auto safe = juce::Component::SafePointer<SongImportWizardComponent>(this);
  row->remove.onClick = [safe, identity] {
    juce::MessageManager::callAsync([safe, identity] {
      if (!safe || safe->isSubmitting) return;
      auto &rows = safe->blankSections;
      rows.erase(std::remove_if(rows.begin(), rows.end(), [identity](const auto &r) {
        return r.get() == identity;
      }), rows.end());
      safe->blankArrangementEdited = true;
      safe->layoutBlankSections();
    });
  };
  blankSectionsContent.addAndMakeVisible(*row);
  blankSections.push_back(std::move(row));
  blankArrangementEdited = true;
  layoutBlankSections();
}

void SongImportWizardComponent::layoutBlankSections() {
  const int width = juce::jmax(1, blankSectionsViewport.getWidth() - 14);
  blankSectionsContent.setSize(width, static_cast<int>(blankSections.size()) * 58);
  int y = 0;
  for (auto &row : blankSections) { row->setBounds(0, y, width, 58); y += 58; }
}

bool SongImportWizardComponent::buildBlankArrangement(ImportedSongData &song) const {
  song.blankSong = true;
  song.blankClickEnabled = blankClickButton.getToggleState();
  song.blankPadEnabled = blankPadButton.getToggleState();
  if (mode == Mode::Edit && !blankArrangementEdited) {
    song.blankSongBars = initialSong.blankSongBars;
    song.arrangement = initialSong.arrangement;
    const double previousBar = songBarDurationSeconds(initialSong);
    const double scale = previousBar > 0.0 ? songBarDurationSeconds(song) / previousBar : 1.0;
    for (auto &part : song.arrangement.parts) {
      part.startSec *= scale; part.endSec *= scale;
      part.sourceStartSec *= scale; part.sourceEndSec *= scale;
    }
    for (auto &cue : song.arrangement.cues) {
      cue.positionSec *= scale; cue.offsetSec *= scale; cue.countIntervalSec *= scale;
      for (auto &offset : cue.countOffsetsSec) offset *= scale;
    }
    song.fileType = "blank";
    return isValidBlankSong(song);
  }
  song.blankSongBars = 0;
  song.arrangement = {};
  song.tempoMap = {};
  song.stems.clear();
  if (blankFormBox.getSelectedId() == 2) {
    if (blankSections.empty()) return false;
    const double bar = songBarDurationSeconds(song);
    if (bar <= 0.0) return false;
    for (const auto &row : blankSections) {
      const int count = row->bars.getText().getIntValue();
      if (row->name.getText().trim().isEmpty() || count < 1 || count > 10000 ||
          song.blankSongBars + count > 100000) return false;
      auto part = row->original;
      if (part.id.isEmpty()) part.id = juce::Uuid().toString();
      part.name = row->name.getText().trim();
      part.color = fallbackArrangementPartColorForName(part.name);
      part.startSec = song.blankSongBars * bar;
      song.blankSongBars += count;
      part.endSec = song.blankSongBars * bar;
      part.sourcePartId.clear();
      part.sourceStartSec = part.startSec;
      part.sourceEndSec = part.endSec;
      song.arrangement.parts.push_back(std::move(part));
    }
  }
  song.fileType = "blank";
  return isValidBlankSong(song);
}

class SongImportWizardComponent::MappingRow final : public juce::Component {
public:
  explicit MappingRow(const StemDraft &stem, const bool usePhoneLayout)
      : phoneLayout(usePhoneLayout) {
    auto fileName = stem.file.getFileName();
    if (fileName.isEmpty()) {
      if (stem.storedPath.isNotEmpty())
        fileName = juce::File(stem.storedPath).getFileName();
      if (fileName.isEmpty() && stem.sourcePath.isNotEmpty())
        fileName = juce::File(stem.sourcePath).getFileName();
    }
    if (fileName.isEmpty())
      fileName = L10n::tr("Stem sin archivo", "Stem with no file");

    fileLabel.setText(fileName, juce::dontSendNotification);
    fileLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
    addAndMakeVisible(fileLabel);

    displayNameEditor.setText(stem.displayName, juce::dontSendNotification);
    displayNameEditor.onReturnKey = [this] {
      if (phoneLayout)
        displayNameEditor.giveAwayKeyboardFocus();
    };
    addAndMakeVisible(displayNameEditor);

    for (size_t i = 0; i < kTrackCategories.size(); ++i)
      categoryBox.addItem(kTrackCategories[i], static_cast<int>(i) + 1);
    categoryBox.setText(stem.category, juce::dontSendNotification);
    addAndMakeVisible(categoryBox);

    includeToggle.setButtonText(L10n::tr("Cargar", "Load"));
    includeToggle.setToggleState(stem.include, juce::dontSendNotification);
    addAndMakeVisible(includeToggle);

    refreshThemeColours();
  }

  void applyTo(StemDraft &stem) const {
    stem.displayName = displayNameEditor.getText().trim();
    stem.category = categoryBox.getText().trim();
    stem.include = includeToggle.getToggleState();
  }

  juce::TextEditor &nameEditor() { return displayNameEditor; }

  void setPhoneLayout(const bool usePhoneLayout) {
    if (phoneLayout == usePhoneLayout)
      return;
    phoneLayout = usePhoneLayout;
    refreshThemeColours();
    resized();
  }

  void resized() override {
    auto r = getLocalBounds().reduced(4, 3);
    if (phoneLayout) {
      auto heading = r.removeFromTop(getHeight() >= 104 ? 44 : 32);
      includeToggle.setBounds(heading.removeFromRight(90));
      fileLabel.setBounds(heading);
      r.removeFromTop(4);
      auto fields = r.removeFromTop(getHeight() >= 104 ? 48 : 36);
      const int gap = 8;
      const int firstWidth = (fields.getWidth() - gap) / 2;
      displayNameEditor.setBounds(fields.removeFromLeft(firstWidth));
      fields.removeFromLeft(gap);
      categoryBox.setBounds(fields);
      return;
    }

    auto fileCol = r.removeFromLeft(230);
    auto nameCol = r.removeFromLeft(170);
    auto catCol = r.removeFromLeft(160);

    fileLabel.setBounds(fileCol);
    displayNameEditor.setBounds(nameCol.reduced(2, 0));
    categoryBox.setBounds(catCol.reduced(2, 0));
    includeToggle.setBounds(r.reduced(8, 0));
  }

  void paint(juce::Graphics &g) override {
    g.setColour(UITheme::cardBgHover.withAlpha(0.9f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.8f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f,
                           1.0f);
  }

  void refreshThemeColours() {
    fileLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
    applyTextEditorTheme(displayNameEditor, UITheme::cardBg,
                         UITheme::cardBorder.withAlpha(0.7f));
    displayNameEditor.setJustification(juce::Justification::centredLeft);
    if (phoneLayout)
      displayNameEditor.setClicksOutsideDismissVirtualKeyboard(false);
    displayNameEditor.getProperties().set("playworshipContinuousTextInput", true);
    applyComboBoxTheme(categoryBox, UITheme::cardBg,
                       UITheme::cardBorder.withAlpha(0.7f));
    categoryBox.setMobileFormMode(useTouchOptimisedControls());
    applyToggleSwitchTheme(includeToggle);
  }

private:
  bool phoneLayout = false;
  juce::Label fileLabel;
  juce::TextEditor displayNameEditor;
  AdaptiveComboBox categoryBox;
  juce::ToggleButton includeToggle;
};

class SongImportWizardComponent::StemListRowComponent final
    : public juce::Component {
public:
  explicit StemListRowComponent(SongImportWizardComponent &ownerIn)
      : owner(ownerIn) {
    titleLabel.setFont(makeAppFont(12.5f, AppFontWeight::Medium));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    metaLabel.setFont(makeAppFont(10.0f, AppFontWeight::Regular));
    metaLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(metaLabel);

    removeButton.setButtonText(L10n::tr("Quitar", "Remove"));
    removeButton.onClick = [this] {
      if (rowIndex >= 0)
        owner.removeStemAtIndex(rowIndex);
    };
    addAndMakeVisible(removeButton);

    refreshThemeColours();
  }

  void setRow(int index) {
    rowIndex = index;
    const bool valid =
        juce::isPositiveAndBelow(rowIndex, static_cast<int>(owner.stems.size()));

    if (!valid) {
      titleLabel.setText({}, juce::dontSendNotification);
      metaLabel.setText({}, juce::dontSendNotification);
      removeButton.setEnabled(false);
      return;
    }

    const auto &stem = owner.stems[static_cast<size_t>(rowIndex)];

    juce::String fileName = stem.file.getFileName();
    if (fileName.isEmpty() && stem.storedPath.isNotEmpty())
      fileName = juce::File(stem.storedPath).getFileName();
    if (fileName.isEmpty() && stem.sourcePath.isNotEmpty())
      fileName = juce::File(stem.sourcePath).getFileName();
    if (fileName.isEmpty())
      fileName = L10n::tr("Archivo no disponible", "File unavailable");

    titleLabel.setText(fileName, juce::dontSendNotification);

    juce::String meta = stem.category.isNotEmpty() ? stem.category : "Other";
    if (stem.sampleRate > 0)
      meta << u8" • " << stem.sampleRate << " Hz";
    if (stem.channels > 0)
      meta << u8" • " << stem.channels << " "
           << L10n::tr("canales", "channels");
    if (stem.fromExistingSong)
      meta << u8" • " << L10n::tr("existente", "existing");
    metaLabel.setText(meta, juce::dontSendNotification);
    removeButton.setEnabled(true);
  }

  void refreshThemeColours() {
    titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
    metaLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
    owner.styleSecondaryButton(removeButton);
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto area = getLocalBounds().toFloat().reduced(1.0f);
    const bool selected = rowIndex == owner.stemsList.getSelectedRow();
    g.setColour(selected ? UITheme::accent.withAlpha(0.20f)
                         : UITheme::cardBgHover.withAlpha(0.92f));
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour((selected ? UITheme::accent : UITheme::cardBorder).withAlpha(0.8f));
    g.drawRoundedRectangle(area, 5.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(9, 6);
    auto buttonArea = area.removeFromRight(96);
    removeButton.setBounds(buttonArea.withTrimmedTop(2).withTrimmedBottom(2));
    titleLabel.setBounds(area.removeFromTop(16));
    metaLabel.setBounds(area);
  }

private:
  SongImportWizardComponent &owner;
  int rowIndex = -1;
  juce::Label titleLabel;
  juce::Label metaLabel;
  juce::TextButton removeButton;
};

class SongImportWizardComponent::StemImportListModel final
    : public juce::ListBoxModel {
public:
  explicit StemImportListModel(SongImportWizardComponent &ownerIn)
      : owner(ownerIn) {}

  int getNumRows() override {
    return static_cast<int>(owner.stems.size());
  }

  void paintListBoxItem(int, juce::Graphics &, int, int, bool) override {}

  juce::Component *refreshComponentForRow(
      int rowNumber, bool, juce::Component *existingComponentToUpdate) override {
    auto *rowComp =
        dynamic_cast<StemListRowComponent *>(existingComponentToUpdate);
    if (rowComp == nullptr)
      rowComp = new StemListRowComponent(owner);
    rowComp->setRow(rowNumber);
    return rowComp;
  }

private:
  SongImportWizardComponent &owner;
};

class SongImportWizardComponent::SecuenciasResultListModel final
    : public juce::ListBoxModel {
public:
  explicit SecuenciasResultListModel(SongImportWizardComponent &ownerIn)
      : owner(ownerIn) {}

  int getNumRows() override {
    return static_cast<int>(owner.secuenciasResults.size());
  }

  void selectedRowsChanged(int row) override {
    owner.secuenciasSelectedRow = row;
    owner.fetchSecuenciasDetailForSelected();
  }

  void paintListBoxItem(int row, juce::Graphics &g, int width, int height,
                        bool rowIsSelected) override {
    if (!juce::isPositiveAndBelow(row,
                                  static_cast<int>(owner.secuenciasResults.size())))
      return;

    const auto &song = owner.secuenciasResults[static_cast<size_t>(row)];
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(4, 4);
    g.setColour(rowIsSelected ? UITheme::accent.withAlpha(0.22f)
                              : UITheme::cardBgHover.withAlpha(0.9f));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    g.setColour((rowIsSelected ? UITheme::accent : UITheme::cardBorder)
                    .withAlpha(0.75f));
    g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);

    auto imageArea = area.removeFromLeft(48).reduced(4);
    auto image = owner.getCachedArtwork(song.artworkUrl);
    if (image.isValid()) {
      g.drawImageWithin(image, imageArea.getX(), imageArea.getY(),
                        imageArea.getWidth(), imageArea.getHeight(),
                        juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    } else {
      owner.requestArtworkAsync(song.artworkUrl);
      g.setColour(UITheme::cardBorder.withAlpha(0.5f));
      g.fillRoundedRectangle(imageArea.toFloat(), 4.0f);
      g.setColour(UITheme::textSecondary.withAlpha(0.65f));
      g.setFont(makeAppFont(9.0f, AppFontWeight::SemiBold));
      g.drawFittedText("IMG", imageArea, juce::Justification::centred, 1);
    }

    auto textArea = area.reduced(8, 4);
    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(12.5f, AppFontWeight::SemiBold));
    g.drawFittedText(song.title.isNotEmpty() ? song.title : L10n::tr("Canción", "Song"),
                     textArea.removeFromTop(18), juce::Justification::centredLeft, 1);

    juce::String meta;
    if (song.artist.isNotEmpty())
      meta << song.artist;
    if (song.album.isNotEmpty())
      meta << (meta.isNotEmpty() ? " • " : "") << song.album;
    if (song.originalKey.isNotEmpty())
      meta << (meta.isNotEmpty() ? " • " : "") << L10n::tr("Tono ", "Key ") << song.originalKey;

    g.setColour(UITheme::textSecondary);
    g.setFont(makeAppFont(10.0f, AppFontWeight::Regular));
    g.drawFittedText(meta, textArea, juce::Justification::centredLeft, 1);
  }

private:
  SongImportWizardComponent &owner;
};

SongImportWizardComponent::SongImportWizardComponent(
    std::function<bool(ImportedSongData, juce::String &,
                       const SongImportProgressFn &)>
        onSubmitSong,
    const Mode modeIn,
    ImportedSongData initialSongIn,
    std::function<bool(const ImportedSongData &, juce::File &, juce::String &)>
        projectDestinationResolver,
    std::function<bool(ImportedSongData, juce::String &)> onSaveMetadataOnly)
    : onSubmit(std::move(onSubmitSong)),
      resolveProjectDestination(std::move(projectDestinationResolver)),
      onMetadataOnlySubmit(std::move(onSaveMetadataOnly)),
      mode(modeIn), initialSong(std::move(initialSongIn)) {
  if (initialSong.blankSong) {
    ProjectSongSettings saved;
    if (ProjectStore::getCachedSettings(projectIdentityForSong(initialSong), saved))
      initialSong.arrangement = saved.arrangement;
  }
  setSize(820, 620);

  titleLabel.setText(mode == Mode::Edit
                         ? L10n::tr(u8"Editar canción", "Edit song")
                         : L10n::tr(u8"Agregar canción", "Add Song"),
                     juce::dontSendNotification);
  titleLabel.setFont(makeAppFont(30.0f, AppFontWeight::Bold));
  addAndMakeVisible(titleLabel);

  subtitleLabel.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
  addAndMakeVisible(subtitleLabel);

  stemsSectionLabel.setText(L10n::tr("Stems", "Stems"),
                            juce::dontSendNotification);
  stemsSectionLabel.setFont(makeAppFont(12.0f, AppFontWeight::SemiBold));
  stemsSectionLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(stemsSectionLabel);

  infoSectionLabel.setText(L10n::tr(u8"Info de canción", "Song Info"),
                           juce::dontSendNotification);
  infoSectionLabel.setFont(makeAppFont(12.0f, AppFontWeight::SemiBold));
  infoSectionLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(infoSectionLabel);

  songTitleLabel.setText(L10n::tr(u8"Título", "Title"),
                         juce::dontSendNotification);
  songTitleLabel.setFont(makeAppFont(11.0f, AppFontWeight::Regular));
  songTitleLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(songTitleLabel);

  artistLabel.setText(L10n::tr("Artista", "Artist"),
                      juce::dontSendNotification);
  artistLabel.setFont(makeAppFont(11.0f, AppFontWeight::Regular));
  artistLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(artistLabel);

  bpmLabel.setText(mode == Mode::Edit ? L10n::tr("BPM base", "Original BPM")
                                       : juce::String("BPM"),
                   juce::dontSendNotification);
  bpmLabel.setFont(makeAppFont(11.0f, AppFontWeight::Regular));
  bpmLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(bpmLabel);

  keyLabel.setText(L10n::tr("Tono", "Key"), juce::dontSendNotification);
  keyLabel.setFont(makeAppFont(11.0f, AppFontWeight::Regular));
  keyLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(keyLabel);

  timeSignatureLabel.setText(L10n::tr(u8"Compás", "Time"),
                             juce::dontSendNotification);
  timeSignatureLabel.setFont(makeAppFont(11.0f, AppFontWeight::Regular));
  timeSignatureLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(timeSignatureLabel);

  mapFileHeader.setText(L10n::tr("Archivo", "File"),
                        juce::dontSendNotification);
  mapFileHeader.setFont(makeAppFont(11.0f, AppFontWeight::SemiBold));
  mapFileHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapFileHeader);

  mapNameHeader.setText(L10n::tr("Nombre visible", "Display name"),
                        juce::dontSendNotification);
  mapNameHeader.setFont(makeAppFont(11.0f, AppFontWeight::SemiBold));
  mapNameHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapNameHeader);

  mapCategoryHeader.setText(L10n::tr(u8"Categoría", "Category"),
                            juce::dontSendNotification);
  mapCategoryHeader.setFont(makeAppFont(11.0f, AppFontWeight::SemiBold));
  mapCategoryHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapCategoryHeader);

  mapLoadHeader.setText(L10n::tr("Cargar", "Load"),
                        juce::dontSendNotification);
  mapLoadHeader.setFont(makeAppFont(11.0f, AppFontWeight::SemiBold));
  mapLoadHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapLoadHeader);

  configureTextEditor(songTitleEditor,
                      L10n::tr(u8"Título de canción", "Song title"));
  configureTextEditor(artistEditor, L10n::tr("Artista", "Artist"));
  configureTextEditor(bpmEditor, "BPM");
  bpmEditor.setInputRestrictions(3, "0123456789");
  bpmEditor.setKeyboardType(juce::TextInputTarget::numericKeyboard);
  bpmEditor.setTooltip(L10n::tr(
      "BPM original de la canción: ajusta la referencia musical. El tempo de reproducción se cambia en el reproductor.",
      "Original song BPM: changes the musical reference. Playback tempo is adjusted in the player."));
  addAndMakeVisible(songTitleEditor);
  addAndMakeVisible(artistEditor);
  addAndMakeVisible(bpmEditor);

  keyBox.addItemList(
      {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"},
      1);
  keyBox.setSelectedId(1);
  configureCombo(keyBox);

  timeSignatureBox.addItemList({"4/4", "3/4", "6/8"}, 1);
  timeSignatureBox.setSelectedId(1);
  configureCombo(timeSignatureBox);

  addAndMakeVisible(keyBox);
  addAndMakeVisible(timeSignatureBox);
  songKindBox.addItem(L10n::tr("Nueva canción · Con pistas", "New song · With tracks"), 1);
  songKindBox.addItem(L10n::tr("Nueva canción · En blanco", "New song · Blank"), 2);
  configureCombo(songKindBox);
  songKindBox.setSelectedId(initialSong.blankSong ? 2 : 1, juce::dontSendNotification);
  songKindBox.onChange = [this] {
    if (isBlankMode() && bpmEditor.getText().isEmpty()) bpmEditor.setText("120");
    changeStep(Step::Metadata);
  };
  blankClickButton.setButtonText(L10n::tr("Click", "Click"));
  blankPadButton.setButtonText(L10n::tr("Pad (pack local)", "Pad (local pack)"));
  blankClickButton.setToggleState(initialSong.blankSong ? initialSong.blankClickEnabled : true, juce::dontSendNotification);
  blankPadButton.setToggleState(initialSong.blankPadEnabled, juce::dontSendNotification);
  for (auto *button : {&blankClickButton, &blankPadButton})
    button->setColour(juce::ToggleButton::textColourId, UITheme::textPrimary);
  blankFormBox.addItem(L10n::tr("Libre · hasta Detener", "Free · until Stop"), 1);
  blankFormBox.addItem(L10n::tr("Con secciones", "With sections"), 2);
  configureCombo(blankFormBox);
  blankFormBox.setSelectedId(initialSong.blankSongBars > 0 || !initialSong.arrangement.parts.empty() ? 2 : 1, juce::dontSendNotification);
  blankFormBox.onChange = [this] { blankArrangementEdited = true; resized(); };
  blankSectionHelp.setText(L10n::tr("Sección y número de compases. Ir y Loop estarán disponibles al reproducir.",
      "Section name and bar count. Go and Loop are available during playback."), juce::dontSendNotification);
  blankSectionHelp.setColour(juce::Label::textColourId, UITheme::textPrimary);
  addBlankSectionButton.setButtonText(L10n::tr("+ Sección", "+ Section"));
  styleSecondaryButton(addBlankSectionButton);
  addBlankSectionButton.onClick = [this] { if (!isSubmitting) addBlankSection(); };
  blankSectionsViewport.setViewedComponent(&blankSectionsContent, false);
  blankSectionsViewport.setScrollBarsShown(true, false);
  for (auto *c : {static_cast<juce::Component *>(&blankFormBox), static_cast<juce::Component *>(&blankSectionHelp),
       static_cast<juce::Component *>(&addBlankSectionButton), static_cast<juce::Component *>(&blankSectionsViewport)}) addChildComponent(c);
  for (const auto &part : initialSong.arrangement.parts)
    if (initialSong.blankSong) addBlankSection(&part);
  if (blankSections.empty()) addBlankSection();
  blankArrangementEdited = false;
  songTitleEditor.setExplicitFocusOrder(1);
  artistEditor.setExplicitFocusOrder(2);
  bpmEditor.setExplicitFocusOrder(3);
  keyBox.setExplicitFocusOrder(4);
  timeSignatureBox.setExplicitFocusOrder(5);

  songTitleEditor.onTextChange = [this] {
    // Metadata is entered manually; no remote catalog autocomplete.
};
  songTitleEditor.onReturnKey = [this] {
    hideSecuenciasSuggestions(true);
    if (useTouchOptimisedControls())
      advanceTextEditor();
    else
      songTitleEditor.giveAwayKeyboardFocus();
  };
  songTitleEditor.onEscapeKey = [this] { hideSecuenciasSuggestions(true); };
  songTitleEditor.onFocusLost = [this] {
    auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
    juce::Timer::callAfterDelay(80, [safeThis] {
      if (safeThis == nullptr)
        return;
      if (safeThis->secuenciasList.hasKeyboardFocus(true))
        return;
      safeThis->hideSecuenciasSuggestions(true);
    });
  };
  artistEditor.onReturnKey = [this] {
    if (useTouchOptimisedControls())
      advanceTextEditor();
    else
      artistEditor.giveAwayKeyboardFocus();
  };
  bpmEditor.onReturnKey = [this] { finishTextEditing(); };
  songTitleEditor.getProperties().set("playworshipReturnKeyNext", true);
  artistEditor.getProperties().set("playworshipReturnKeyNext", true);

  secuenciasListModel = std::make_unique<SecuenciasResultListModel>(*this);
  secuenciasList.setModel(secuenciasListModel.get());
  secuenciasList.setRowHeight(54);
  secuenciasList.setColour(juce::ListBox::backgroundColourId, UITheme::cardBg);
  secuenciasList.setColour(juce::ListBox::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.7f));
  addAndMakeVisible(secuenciasList);
  secuenciasList.setVisible(false);

  secuenciasStatusLabel.setText(
      L10n::tr("Busca una canción para autocompletar metadata",
               "Search a song to auto-fill metadata"),
      juce::dontSendNotification);
  secuenciasStatusLabel.setFont(makeAppFont(10.5f, AppFontWeight::Regular));
  addAndMakeVisible(secuenciasStatusLabel);

  secuenciasDetailLabel.setFont(makeAppFont(10.5f, AppFontWeight::Regular));
  secuenciasDetailLabel.setJustificationType(juce::Justification::topLeft);
  secuenciasDetailLabel.setMinimumHorizontalScale(0.9f);
  addAndMakeVisible(secuenciasDetailLabel);

  secuenciasArtworkPreview.setImage(juce::Image(), juce::RectanglePlacement::centred);
  secuenciasArtworkPreview.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(secuenciasArtworkPreview);

  secuenciasArtworkCaption.setText({}, juce::dontSendNotification);
  secuenciasArtworkCaption.setFont(makeAppFont(10.5f, AppFontWeight::Regular));
  secuenciasArtworkCaption.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(secuenciasArtworkCaption);

  changeArtworkLabel.setText(
      L10n::tr("Cambiar portada", "Change artwork"),
      juce::dontSendNotification);
  changeArtworkLabel.setFont(makeAppFont(15.0f, AppFontWeight::Medium));
  changeArtworkLabel.setJustificationType(juce::Justification::centred);
  changeArtworkLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(changeArtworkLabel);
  changeArtworkLabel.setVisible(false);

  clearSongSearchButton.setButtonText(L10n::tr("Limpiar", "Clear"));
  clearSongSearchButton.onClick = [this] {
    ++secuenciasSearchToken;
    suppressTitleAutocomplete = true;
    songTitleEditor.clear();
    artistEditor.clear();
    bpmEditor.clear();
    keyBox.setSelectedId(1, juce::dontSendNotification);
    timeSignatureBox.setSelectedId(1, juce::dontSendNotification);
    suppressTitleAutocomplete = false;

    selectedArtworkUrl.clear();
    secuenciasArtworkPreview.setImage(juce::Image(),
                                      juce::RectanglePlacement::centred);
    secuenciasArtworkCaption.setText({}, juce::dontSendNotification);
    secuenciasDetailLabel.setText({}, juce::dontSendNotification);
    selectedSongDetail.reset();
    secuenciasSelectedRow = -1;
    secuenciasResults.clear();
    secuenciasList.updateContent();
    secuenciasList.selectRow(-1);
    secuenciasList.setVisible(false);
    secuenciasStatusLabel.setText(
        L10n::tr("Busca una canción para autocompletar metadata",
                 "Search a song to auto-fill metadata"),
        juce::dontSendNotification);
    resized();
    repaint();
  };
  addAndMakeVisible(clearSongSearchButton);

  addFilesButton.setButtonText(L10n::tr(u8"Agregar stems...", u8"Add stems..."));
  addFilesButton.setTooltip(mode == Mode::Import ? kCreationFormatNames : kStemImportFormatNames);
  addFilesButton.onClick = [this] { openStemChooser(false); };
  addAndMakeVisible(addFilesButton);

  replaceFilesButton.setButtonText(
      L10n::tr(u8"Reemplazar stems...", u8"Replace stems..."));
  replaceFilesButton.setTooltip(mode == Mode::Import ? kCreationFormatNames : kStemImportFormatNames);
  replaceFilesButton.onClick = [this] { openStemChooser(true); };
  addAndMakeVisible(replaceFilesButton);

  selectedFilesLabel.setText(
      L10n::tr("No hay stems seleccionados", "No stems selected"),
      juce::dontSendNotification);
  selectedFilesLabel.setFont(makeAppFont(11.0f, AppFontWeight::Regular));
  addAndMakeVisible(selectedFilesLabel);

  stemImportListModel = std::make_unique<StemImportListModel>(*this);
  stemsList.setRowHeight(44);
  stemsList.setModel(stemImportListModel.get());
  stemsList.setMultipleSelectionEnabled(false);
  stemsList.setColour(juce::ListBox::backgroundColourId, UITheme::cardBg);
  stemsList.setColour(juce::ListBox::outlineColourId,
                      UITheme::cardBorder.withAlpha(0.7f));
  secuenciasList.setColour(juce::ListBox::backgroundColourId, UITheme::cardBg);
  secuenciasList.setColour(juce::ListBox::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.7f));
  addAndMakeVisible(stemsList);

  mapViewport.setViewedComponent(&mapContent, false);
  mapViewport.setScrollBarsShown(true, false, false, false);
  mapViewport.setScrollBarThickness(1);
  mapViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(mapViewport);

  metadataViewport.setViewedComponent(&metadataContent, false);
  metadataViewport.setScrollBarsShown(true, false, false, false);
  metadataViewport.setScrollBarThickness(2);
  metadataViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  metadataViewport.onScroll = [this] { repaint(); };
  metadataContent.addMouseListener(this, true);
  addChildComponent(metadataViewport);
  keyboardDoneButton.setButtonText(L10n::tr("Listo", "Done"));
  keyboardPreviousButton.setButtonText(L10n::tr("Anterior", "Previous"));
  keyboardNextButton.setButtonText(L10n::tr("Siguiente", "Next"));
  for (auto *button : {&keyboardPreviousButton, &keyboardNextButton,
                       &keyboardDoneButton}) {
    button->setWantsKeyboardFocus(false);
    button->setMouseClickGrabsKeyboardFocus(false);
    addChildComponent(*button);
  }
  keyboardDoneButton.onClick = [this] { finishTextEditing(); };
  keyboardPreviousButton.onClick = [this] { focusAdjacentEditor(-1); };
  keyboardNextButton.onClick = [this] { focusAdjacentEditor(1); };

  summaryEditor.setReadOnly(true);
  summaryEditor.setMultiLine(true);
  addAndMakeVisible(summaryEditor);

  cancelButton.setButtonText(L10n::tr("Cancelar", "Cancel"));
  cancelButton.onClick = [this] { closeDialog(); };
  backButton.setButtonText(L10n::tr(u8"Atrás", "Back"));
  backButton.onClick = [this] {
    if (isSubmitting || currentStep == Step::Metadata)
      return;
    changeStep(isBlankMode() && currentStep == Step::Review ? Step::Stems
        : static_cast<Step>(static_cast<int>(currentStep) - 1));
  };
  nextButton.setButtonText(L10n::tr("Siguiente", "Next"));
  nextButton.onClick = [this] { goToNextStep(); };
  submitButton.setButtonText(mode == Mode::Edit
                                 ? L10n::tr("Guardar cambios",
                                            "Save changes")
                                 : L10n::tr(u8"Guardar canción",
                                            "Save song"));
  submitButton.onClick = [this] { submitSong(); };
  saveMetadataButton.setButtonText(L10n::tr("Guardar", "Save"));
  saveMetadataButton.onClick = [this] { saveMetadataOnly(); };

  mobileCloseButton.setBare(true);
  mobileCloseButton.setTooltip(L10n::tr("Cerrar", "Close"));
  mobileCloseButton.onClick = [this] { closeDialog(); };
  addAndMakeVisible(mobileCloseButton);
  mobileCloseButton.setVisible(false);

  mobilePrimaryButtonLookAndFeel =
      std::make_unique<MobileWizardButtonLookAndFeel>();

  for (auto *btn : {&cancelButton, &backButton, &nextButton, &submitButton,
                    &saveMetadataButton})
    addAndMakeVisible(*btn);

  refreshThemeColours();

  if (mode == Mode::Edit && initialSong.id.isNotEmpty())
    preloadFromExistingSong();

  changeStep(Step::Metadata);
  if (useTouchOptimisedControls())
    startTimerHz(30);
}

SongImportWizardComponent::~SongImportWizardComponent() {
  stopTimer();
  metadataContent.removeMouseListener(this);
  metadataViewport.onScroll = {};
  metadataViewport.setViewedComponent(nullptr, false);
  nextButton.setLookAndFeel(nullptr);
  submitButton.setLookAndFeel(nullptr);
  saveMetadataButton.setLookAndFeel(nullptr);
  backButton.setLookAndFeel(nullptr);
}

void SongImportWizardComponent::setOnDismissRequested(std::function<void()> callback) {
  onDismissRequested = std::move(callback);
}

void SongImportWizardComponent::setEmbeddedLibraryPage(const bool enabled) {
  if (embeddedLibraryPage == enabled)
    return;
  embeddedLibraryPage = enabled;
  if (!getLocalBounds().isEmpty())
    resized();
}

void SongImportWizardComponent::timerCallback() {
  if (!isShowing() || !useTouchOptimisedControls())
    return;

  const auto nextInsets = keyboardInsetsForLayout();
  updateKeyboardNavigation();

  if (nextInsets != phoneKeyboardInsets) {
    phoneKeyboardInsets = nextInsets;
    resized();
    return;
  }

  if (phoneCreationLayout && currentStep == Step::Stems && isBlankMode())
    layoutStepImport(blankFormBox.getBounds().getUnion(blankSectionsViewport.getBounds()).getUnion(addBlankSectionButton.getBounds()));
  if (phoneCreationLayout && currentStep == Step::Mapping) {
    auto *focused = juce::Component::getCurrentlyFocusedComponent();
    if (lastMappingFocus.getComponent() != focused) {
      lastMappingFocus = focused;
      if (phoneKeyboardVisible)
        revealFocusedMappingEditor();
    }
  }
  if (phoneCreationLayout && currentStep == Step::Metadata) {
    auto *focused = juce::Component::getCurrentlyFocusedComponent();
    if (lastMetadataFocus.getComponent() != focused) {
      lastMetadataFocus = focused;
      revealFocusedMetadataEditor();
    }
    if (metadataScrollAnimating) {
      if (juce::Desktop::getInstance().getMainMouseSource().isDragging()) {
        metadataScrollAnimating = false;
      } else {
        const int current = metadataViewport.getViewPositionY();
        const int distance = metadataScrollTarget - current;
        const int next = std::abs(distance) <= 2 ? metadataScrollTarget
                          : current + juce::roundToInt(distance * 0.35f);
        metadataViewport.setViewPosition(0, next);
        metadataScrollAnimating = next != metadataScrollTarget;
      }
    }
  }
}

juce::BorderSize<int> SongImportWizardComponent::keyboardInsetsForLayout() {
  juce::BorderSize<int> nextInsets;
  if (const auto *display = juce::Desktop::getInstance()
                                .getDisplays()
                                .getDisplayForRect(getScreenBounds()))
    nextInsets = display->keyboardInsets;

  const auto now = juce::Time::getMillisecondCounter();
  if (hasKeyboardInsets(nextInsets))
    keyboardLastVisibleTime = now;
  else if (auto *editor = dynamic_cast<juce::TextEditor *>(
               juce::Component::getCurrentlyFocusedComponent());
           editor != nullptr && isParentOf(editor) && !editor->isReadOnly() &&
           hasKeyboardInsets(phoneKeyboardInsets) &&
           now - keyboardLastVisibleTime < 200)
    return phoneKeyboardInsets; // Ignore the brief hide when changing keyboards.
  return nextInsets;
}

std::vector<juce::TextEditor *> SongImportWizardComponent::currentFormEditors() {
  std::vector<juce::TextEditor *> editors;
  if (currentStep == Step::Metadata)
    editors = {&songTitleEditor, &artistEditor, &bpmEditor};
  else if (currentStep == Step::Stems && isBlankMode())
    for (auto &row : blankSections) { editors.push_back(&row->name); editors.push_back(&row->bars); }
  else if (currentStep == Step::Mapping)
    for (auto &row : mappingRows)
      editors.push_back(&row->nameEditor());
  editors.erase(std::remove_if(editors.begin(), editors.end(), [](auto *editor) {
    return !editor->isVisible() || !editor->isEnabled() || editor->isReadOnly();
  }), editors.end());
  return editors;
}

bool SongImportWizardComponent::focusAdjacentEditor(const int direction) {
  const auto editors = currentFormEditors();
  auto *focused = juce::Component::getCurrentlyFocusedComponent();
  const auto current = std::find(editors.begin(), editors.end(), focused);
  if (current == editors.end())
    return false;
  const int next = static_cast<int>(std::distance(editors.begin(), current)) + direction;
  if (!juce::isPositiveAndBelow(next, static_cast<int>(editors.size())))
    return false;
  // Transfer directly: an intermediate unfocus would hide the native keyboard.
  editors[static_cast<size_t>(next)]->grabKeyboardFocus();
  updateKeyboardNavigation();
  revealFocusedMetadataEditor();
  revealFocusedMappingEditor();
  return true;
}

void SongImportWizardComponent::advanceTextEditor() {
  if (!focusAdjacentEditor(1))
    finishTextEditing();
}

void SongImportWizardComponent::finishTextEditing() {
  if (hasKeyboardFocus(true))
    juce::Component::unfocusAllComponents();
  updateKeyboardNavigation();
}

void SongImportWizardComponent::updateKeyboardNavigation() {
  const auto editors = currentFormEditors();
  auto *focused = juce::Component::getCurrentlyFocusedComponent();
  const auto current = std::find(editors.begin(), editors.end(), focused);
  const bool editing = current != editors.end() && !isSubmitting;
  keyboardPreviousButton.setEnabled(editing && current != editors.begin());
  keyboardNextButton.setEnabled(editing && std::next(current) != editors.end());
}

void SongImportWizardComponent::revealFocusedMetadataEditor() {
  if (!phoneCreationLayout || !metadataViewport.isVisible()) return;
  auto *focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused != &songTitleEditor && focused != &artistEditor &&
      focused != &bpmEditor) return;
  auto target = focused->getBounds().expanded(0, 12).withTrimmedTop(-20);
  const auto visible = metadataViewport.getViewArea();
  int y = visible.getY();
  if (target.getY() < y) y = target.getY();
  else if (target.getBottom() > visible.getBottom())
    y += target.getBottom() - visible.getBottom();
  metadataScrollTarget = juce::jlimit(0,
      juce::jmax(0, metadataContent.getHeight() - visible.getHeight()), y);
  metadataScrollAnimating = metadataScrollTarget != visible.getY();
}

void SongImportWizardComponent::revealFocusedMappingEditor() {
  if (!phoneCreationLayout || currentStep != Step::Mapping ||
      !mapViewport.isVisible() ||
      juce::Desktop::getInstance().getMainMouseSource().isDragging())
    return;

  auto *focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused == nullptr ||
      (focused != &mapContent && !mapContent.isParentOf(focused)))
    return;

  auto target = mapContent.getLocalArea(focused, focused->getLocalBounds())
                    .expanded(0, 12);
  const auto visible = mapViewport.getViewArea();
  int nextY = visible.getY();
  if (target.getY() < visible.getY())
    nextY = target.getY();
  else if (target.getBottom() > visible.getBottom())
    nextY += target.getBottom() - visible.getBottom();

  if (nextY != visible.getY())
    mapViewport.setViewPosition(mapViewport.getViewPositionX(),
                                juce::jmax(0, nextY));
}

bool SongImportWizardComponent::usesPhoneCreationLayout() const {
  return getWidth() > 0 && getWidth() < 600 && isPhoneSizedTouchLayout(*this);
}

juce::String SongImportWizardComponent::mobileMetadataSummary() const {
  if (isBlankMode()) return L10n::tr("Sin pistas de audio · click y pad independientes", "No audio tracks · independent click and pad");
  int trackCount = 0;
  double duration = 0.0;

  if (selectedSongDetail.has_value()) {
    trackCount = static_cast<int>(selectedSongDetail->tracks.size());
    duration = selectedSongDetail->durationSeconds;
  }

  if (trackCount == 0)
    trackCount = static_cast<int>(std::count_if(
        stems.begin(), stems.end(),
        [](const StemDraft &stem) { return stem.include; }));
  if (duration <= 0.0) {
    for (const auto &stem : stems)
      duration = juce::jmax(duration, stem.durationSec);
  }

  if (trackCount == 0 && duration <= 0.0)
    return L10n::tr("Se completarán al agregar los archivos",
                    "Completed after adding files");

  juce::String text;
  if (trackCount > 0)
    text << trackCount << " " << L10n::tr("pistas", "tracks");
  if (duration > 0.0)
    text << (text.isNotEmpty() ? u8" • " : "")
         << formatTimeSeconds(duration);
  return text;
}

void SongImportWizardComponent::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  if (usesCreationLayout()) {
    drawStudioBackground(g, b);

    if (!stepperBounds.isEmpty()) {
      const std::vector<juce::String> labels = isBlankMode()
          ? std::vector<juce::String>{L10n::tr("Detalles", "Details"), L10n::tr("Forma", "Form"), L10n::tr("Revisar", "Review")}
          : std::vector<juce::String>{
          L10n::tr("Detalles", "Details"),
          L10n::tr("Archivos", "Files"),
          L10n::tr("Secciones", "Sections"),
          L10n::tr("Revisar", "Review")};
      const int active = isBlankMode() && currentStep == Step::Review ? 2 : static_cast<int>(currentStep);
      auto stepper = stepperBounds.toFloat();
      const float left = stepper.getX() + 31.0f;
      const float right = stepper.getRight() - 31.0f;
      const float circleY = stepper.getY() + 22.0f;
      const float spacing = (right - left) / static_cast<float>(labels.size() - 1);

      g.setColour(UITheme::cardBorder.withAlpha(0.95f));
      g.drawLine(left, circleY, right, circleY, 1.5f);
      if (active > 0) {
        g.setColour(UITheme::accent.withAlpha(0.85f));
        g.drawLine(left, circleY,
                   left + spacing * static_cast<float>(active), circleY, 1.8f);
      }

      for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        const float x = left + spacing * static_cast<float>(index);
        const bool reached = index <= active;
        auto circle = juce::Rectangle<float>(x - 14.0f, circleY - 14.0f,
                                             28.0f, 28.0f);
        g.setColour(reached ? UITheme::accent : UITheme::textSecondary);
        g.drawEllipse(circle, reached ? 1.8f : 1.3f);
        g.setColour(UITheme::panelDark.withAlpha(0.96f));
        g.fillEllipse(circle.reduced(2.0f));
        g.setColour(reached ? UITheme::accent : UITheme::textSecondary);
        g.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
        g.drawText(juce::String(index + 1), circle,
                   juce::Justification::centred, false);

        auto labelArea = juce::Rectangle<float>(x - spacing * 0.47f,
                                                circle.getBottom() + 7.0f,
                                                spacing * 0.94f, 20.0f);
        g.setFont(makeAppFont(12.5f, index == active
                                        ? AppFontWeight::SemiBold
                                        : AppFontWeight::Regular));
        g.drawFittedText(labels[static_cast<size_t>(index)],
                         labelArea.toNearestInt(),
                         juce::Justification::centred, 1);
      }
    }

    if (metadataViewport.isVisible()) {
      juce::Graphics::ScopedSaveState state(g);
      g.reduceClipRegion(metadataViewport.getBounds());
      g.setOrigin(metadataViewport.getX(), metadataViewport.getY() -
                  metadataViewport.getViewPositionY());
      paintPhoneMetadata(g);
    }
    return;
  }

  constexpr bool usesNavigationToolbar = true;
  g.setColour(usesNavigationToolbar ? UITheme::panelDark : UITheme::cardBg);
  g.fillRoundedRectangle(b, 8.0f);
  g.setColour(UITheme::cardBorder.withAlpha(usesNavigationToolbar ? 0.62f : 0.8f));
  g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.72f));
  g.drawLine(12.0f, 68.0f, b.getRight() - 12.0f, 68.0f, 1.0f);
}

void SongImportWizardComponent::paintPhoneMetadata(juce::Graphics &g) {
    if (currentStep == Step::Metadata &&
        !secuenciasArtworkPreview.getBounds().isEmpty()) {
      auto artwork = secuenciasArtworkPreview.getBounds().toFloat();
      g.setColour(UITheme::cardBgHover);
      g.fillRoundedRectangle(artwork, 11.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.52f));
      g.drawRoundedRectangle(artwork.reduced(0.5f), 11.0f, 1.0f);
      const auto image = secuenciasArtworkPreview.getImage();
      if (image.isValid()) {
        juce::Path clip;
        clip.addRoundedRectangle(artwork, 11.0f);
        g.saveState();
        g.reduceClipRegion(clip);
        g.drawImageWithin(
            image, juce::roundToInt(artwork.getX()),
            juce::roundToInt(artwork.getY()),
            juce::roundToInt(artwork.getWidth()),
            juce::roundToInt(artwork.getHeight()),
            juce::RectanglePlacement::centred |
                juce::RectanglePlacement::fillDestination);
        g.restoreState();
      } else {
        drawSvgIcon(g, artwork.withSizeKeepingCentre(44.0f, 44.0f),
                    SvgIconName::Music, UITheme::textSecondary.withAlpha(0.7f));
      }
    }

    if (currentStep == Step::Metadata && !metadataCardBounds.isEmpty()) {
      auto card = metadataCardBounds.toFloat();
      juce::ColourGradient cardGradient(
          UITheme::accent.withAlpha(0.10f), card.getX(), card.getCentreY(),
          UITheme::cardBgHover.withAlpha(0.96f), card.getRight(),
          card.getCentreY(), false);
      g.setGradientFill(cardGradient);
      g.fillRoundedRectangle(card, 14.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.62f));
      g.drawRoundedRectangle(card.reduced(0.5f), 14.0f, 1.0f);

      auto iconTile = card.withWidth(48.0f).reduced(8.0f);
      g.setColour(UITheme::accent.withAlpha(0.10f));
      g.fillRoundedRectangle(iconTile.expanded(5.0f), 10.0f);
      drawSvgIcon(g, iconTile, SvgIconName::AudioWaveform, UITheme::accent);

      auto text = card.reduced(66.0f, 9.0f);
      text.setRight(card.getRight() - 38.0f);
      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(15.5f, AppFontWeight::SemiBold));
      g.drawFittedText(isBlankMode() ? L10n::tr("Acompañamiento", "Accompaniment") : mode == Mode::Edit
                           ? L10n::tr("BPM original", "Original BPM")
                           : L10n::tr("Metadatos detectados", "Detected metadata"),
                       text.removeFromTop(24.0f).toNearestInt(),
                       juce::Justification::centredLeft, 1);
      g.setColour(UITheme::textSecondary);
      g.setFont(makeAppFont(13.0f, AppFontWeight::Regular));
      g.drawFittedText(mode == Mode::Edit
                           ? L10n::tr("Tempo de reproducción: en el reproductor.",
                                      "Playback tempo: in the player.")
                           : mobileMetadataSummary(), text.toNearestInt(),
                       juce::Justification::centredLeft, 2);

      const float cx = card.getRight() - 25.0f;
      const float cy = card.getCentreY();
      juce::Path chevron;
      chevron.startNewSubPath(cx - 4.0f, cy - 6.0f);
      chevron.lineTo(cx + 2.0f, cy);
      chevron.lineTo(cx - 4.0f, cy + 6.0f);
      g.setColour(UITheme::textSecondary);
      g.strokePath(chevron, juce::PathStrokeType(2.0f));
    }
}

void SongImportWizardComponent::paintOverChildren(juce::Graphics &g) {
  if (currentStep != Step::Metadata || artworkOverlayButtonBounds.isEmpty())
    return;

  juce::Graphics::ScopedSaveState state(g);
  if (usesCreationLayout()) {
    g.reduceClipRegion(metadataViewport.getBounds());
    g.setOrigin(metadataViewport.getX(), metadataViewport.getY() -
                metadataViewport.getViewPositionY());
  }

  auto area = artworkOverlayButtonBounds.toFloat();
  auto bg = usesCreationLayout()
                ? juce::Colours::black.withAlpha(artworkOverlayHovered ? 0.96f
                                                                       : 0.86f)
                : UITheme::accent.withAlpha(artworkOverlayHovered ? 0.95f
                                                                  : 0.82f);
  g.setColour(bg);
  g.fillEllipse(area);
  g.setColour(UITheme::cardBorder.withAlpha(0.8f));
  g.drawEllipse(area, 1.0f);
  if (usesCreationLayout()) {
    const auto colour = UITheme::textPrimary;
    auto camera = area.reduced(10.0f, 12.0f);
    camera.removeFromTop(2.0f);
    g.setColour(colour);
    g.drawRoundedRectangle(camera, 2.5f, 2.0f);
    auto lens = camera.withSizeKeepingCentre(9.0f, 9.0f);
    g.drawEllipse(lens, 2.0f);
    juce::Path top;
    top.startNewSubPath(camera.getX() + 5.0f, camera.getY());
    top.lineTo(camera.getX() + 8.0f, camera.getY() - 4.0f);
    top.lineTo(camera.getRight() - 8.0f, camera.getY() - 4.0f);
    top.lineTo(camera.getRight() - 5.0f, camera.getY());
    g.strokePath(top, juce::PathStrokeType(2.0f));
  } else {
    drawSvgIcon(g, area.reduced(6.0f), SvgIconName::ImageEdit,
                UITheme::onAccent);
  }
}

void SongImportWizardComponent::resized() {
  if (submissionOverlay != nullptr)
    submissionOverlay->setBounds(getLocalBounds());
  metadataScrollAnimating = false;
  phoneCreationLayout = usesPhoneCreationLayout();
  if (usesCreationLayout()) {
    auto bounds = getLocalBounds();
    auto safeBounds = bounds;
    phoneKeyboardInsets = phoneCreationLayout ? keyboardInsetsForLayout()
                                               : juce::BorderSize<int>{};
    if (const auto *display = phoneCreationLayout ? juce::Desktop::getInstance()
                                  .getDisplays()
                                  .getDisplayForRect(getScreenBounds()) : nullptr) {
      safeBounds = combinedPhoneInsets(display->safeAreaInsets,
                                       phoneKeyboardInsets)
                       .subtractedFrom(safeBounds);
    }
    phoneKeyboardVisible = hasKeyboardInsets(phoneKeyboardInsets);
    auto content = safeBounds.reduced(17, 0);
    content.removeFromTop(mobileTopBarHeight());
    auto header = safeBounds.reduced(mobileTopBarHorizontalMargin(), 0)
                      .removeFromTop(mobileTopBarHeight());
    mobileCloseButton.setVisible(true);
    const int actionSize = mobileTopBarActionSize();
    mobileCloseButton.setBounds(header.removeFromRight(actionSize)
                                    .withSizeKeepingCentre(actionSize,
                                                           actionSize));
    header.removeFromRight(mobileTopBarActionGap());
    titleLabel.setFont(makeAppFont(18.0f, AppFontWeight::SemiBold));
    titleLabel.setJustificationType(
        phoneCreationLayout ? juce::Justification::centredLeft
                            : juce::Justification::centred);
    titleLabel.setBounds(header.reduced(8, 0));
    subtitleLabel.setVisible(false);
    cancelButton.setVisible(false);

    stepperBounds = content.removeFromTop(phoneCreationLayout ? 86 : 72);
    content.removeFromTop(4);

    auto *primary = submitButton.isVisible()
                        ? static_cast<juce::TextButton *>(&submitButton)
                        : static_cast<juce::TextButton *>(&nextButton);
    for (auto *button : {&nextButton, &submitButton, &saveMetadataButton,
                         &backButton})
      button->setLookAndFeel(mobilePrimaryButtonLookAndFeel.get());

    primary->setColour(juce::TextButton::buttonColourId, UITheme::accent);
    primary->setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
    backButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
    backButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    saveMetadataButton.setColour(juce::TextButton::buttonColourId,
                                 UITheme::cardBgHover);
    saveMetadataButton.setColour(juce::TextButton::textColourOffId,
                                 UITheme::accent);

    if (phoneKeyboardVisible) {
      auto accessory = content.removeFromBottom(44);
      keyboardDoneButton.setBounds(accessory.removeFromRight(80));
      keyboardPreviousButton.setBounds(accessory.removeFromLeft(88));
      accessory.removeFromLeft(4);
      keyboardNextButton.setBounds(accessory.removeFromLeft(90));
      for (auto *button : {&nextButton, &submitButton, &saveMetadataButton,
                           &backButton})
        button->setBounds({});
    } else {
      auto footer = content.removeFromBottom(phoneCreationLayout ? 66 : 54).reduced(0, 9);
      if (backButton.isVisible()) {
        backButton.setBounds(footer.removeFromLeft(92));
        footer.removeFromLeft(8);
      }
      if (saveMetadataButton.isVisible()) {
        saveMetadataButton.setBounds(footer.removeFromLeft(82));
        footer.removeFromLeft(8);
      }
      primary->setBounds(footer);
    }
    keyboardDoneButton.setVisible(phoneKeyboardVisible);
    keyboardPreviousButton.setVisible(phoneKeyboardVisible);
    keyboardNextButton.setVisible(phoneKeyboardVisible);
    updateKeyboardNavigation();

    for (auto *label : {&songTitleLabel, &artistLabel, &bpmLabel, &keyLabel,
                        &timeSignatureLabel})
      label->setFont(makeAppFont(14.0f, AppFontWeight::Regular));

    layoutStepImport(content);
    layoutStepMetadata(content);
    layoutStepMapping(content);
    layoutStepReview(content);
    if (phoneKeyboardVisible)
      revealFocusedMappingEditor();
    revealFocusedMetadataEditor();
    repaint();
    return;
  }

  phoneKeyboardVisible = false;
  keyboardDoneButton.setVisible(false);
  keyboardPreviousButton.setVisible(false);
  keyboardNextButton.setVisible(false);
  phoneKeyboardInsets = {};
  mobileCloseButton.setVisible(false);
  subtitleLabel.setVisible(true);
  cancelButton.setVisible(true);
  stepperBounds = {};
  metadataCardBounds = {};
  changeArtworkLabel.setVisible(false);
  titleLabel.setFont(makeAppFont(30.0f, AppFontWeight::Bold));
  titleLabel.setJustificationType(juce::Justification::centred);
  for (auto *label : {&songTitleLabel, &artistLabel, &bpmLabel, &keyLabel,
                      &timeSignatureLabel})
    label->setFont(makeAppFont(11.0f, AppFontWeight::Regular));
  for (auto *button : {&nextButton, &submitButton, &saveMetadataButton,
                       &backButton})
    button->setLookAndFeel(nullptr);

  constexpr bool usesNavigationToolbar = true;
  auto area = getLocalBounds().reduced(usesNavigationToolbar ? 12 : 16);

  if (usesNavigationToolbar) {
    // The same navigation model is used across platforms so dismissal and
    // progression stay in a predictable, stable position.
    auto toolbar = area.removeFromTop(56);
    const int touchTarget = 44;
    const int buttonY = toolbar.getY() + (toolbar.getHeight() - touchTarget) / 2;

    cancelButton.setBounds(toolbar.getX(), buttonY, 88, touchTarget);
    auto leftEdge = cancelButton.getRight() + 4;
    if (backButton.isVisible()) {
      backButton.setBounds(leftEdge, buttonY, 70, touchTarget);
      leftEdge = backButton.getRight() + 4;
    }

    auto rightEdge = toolbar.getRight();
    auto placeTrailing = [&](juce::Component &button, const int width) {
      button.setBounds(rightEdge - width, buttonY, width, touchTarget);
      rightEdge -= width + 4;
    };
    if (submitButton.isVisible())
      placeTrailing(submitButton, 144);
    else if (nextButton.isVisible())
      placeTrailing(nextButton, 104);
    if (saveMetadataButton.isVisible())
      placeTrailing(saveMetadataButton, 82);

    const int centredTitleInset = juce::jmax(
        leftEdge - toolbar.getX(), toolbar.getRight() - rightEdge);
    titleLabel.setBounds(toolbar.withTrimmedLeft(centredTitleInset)
                             .withTrimmedRight(centredTitleInset));
    subtitleLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
  } else {
    auto header = area.removeFromTop(74);
    titleLabel.setBounds(header.removeFromTop(42));
    subtitleLabel.setBounds(header);

    auto footer = area.removeFromBottom(46);
    cancelButton.setBounds(footer.removeFromLeft(96));

    auto rightEdge = footer.getRight();
    const int buttonH = footer.getHeight();
    const int y = footer.getY();
    const int gap = 8;
    auto placeRight = [&](juce::Component &button, const int width) {
      button.setBounds(rightEdge - width, y, width, buttonH);
      rightEdge -= width + gap;
    };

    if (submitButton.isVisible())
      placeRight(submitButton, 156);
    else
      placeRight(nextButton, 96);

    if (backButton.isVisible())
      placeRight(backButton, 96);
    if (saveMetadataButton.isVisible())
      placeRight(saveMetadataButton, 112);
  }

  layoutStepImport(area);
  layoutStepMetadata(area);
  layoutStepMapping(area);
  layoutStepReview(area);
}

void SongImportWizardComponent::mouseDown(const juce::MouseEvent &event) {
  if (phoneKeyboardVisible && event.originalComponent == this)
    juce::Component::unfocusAllComponents();
}

juce::Point<int> SongImportWizardComponent::metadataMousePosition(
    const juce::MouseEvent &event) const {
  return event.getEventRelativeTo(usesCreationLayout()
      ? const_cast<juce::Component *>(&metadataContent)
      : const_cast<SongImportWizardComponent *>(this)).getPosition();
}

void SongImportWizardComponent::mouseMove(const juce::MouseEvent &event) {
  const bool nextHovered =
      currentStep == Step::Metadata &&
      artworkOverlayButtonBounds.contains(metadataMousePosition(event));
  if (nextHovered != artworkOverlayHovered) {
    artworkOverlayHovered = nextHovered;
    repaint();
  }
}

void SongImportWizardComponent::mouseExit(const juce::MouseEvent &) {
  if (!artworkOverlayHovered)
    return;
  artworkOverlayHovered = false;
  repaint();
}

void SongImportWizardComponent::mouseUp(const juce::MouseEvent &event) {
  if (currentStep != Step::Metadata)
    return;
  if (event.mouseWasDraggedSinceMouseDown()) return;
  const auto position = metadataMousePosition(event);
  const bool phoneArtworkAction =
      usesCreationLayout() &&
      (secuenciasArtworkPreview.getBounds().contains(position) ||
       changeArtworkLabel.getBounds().contains(position));
  if (!artworkOverlayButtonBounds.contains(position) &&
      !phoneArtworkAction)
    return;
  openArtworkChooser();
}

void SongImportWizardComponent::refreshThemeColours() {
  if (submissionOverlay != nullptr)
    submissionOverlay->refreshThemeColours();
  titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  titleLabel.setJustificationType(juce::Justification::centred);
  subtitleLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  subtitleLabel.setJustificationType(juce::Justification::centred);
  for (auto *label :
       {&stemsSectionLabel, &infoSectionLabel,
        &songTitleLabel, &artistLabel, &bpmLabel, &keyLabel,
        &timeSignatureLabel, &mapFileHeader, &mapNameHeader, &mapCategoryHeader,
        &mapLoadHeader, &secuenciasStatusLabel,
        &secuenciasDetailLabel, &secuenciasArtworkCaption,
        &changeArtworkLabel}) {
    label->setColour(juce::Label::textColourId, UITheme::textSecondary);
  }
  changeArtworkLabel.setColour(juce::Label::textColourId, UITheme::accent);

  applyEditorTheme(songTitleEditor, L10n::tr(u8"Título de canción", "Song title"));
  applyEditorTheme(artistEditor, L10n::tr("Artista", "Artist"));
  applyEditorTheme(bpmEditor, "BPM");

  selectedFilesLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  stemsList.setColour(juce::ListBox::backgroundColourId, UITheme::cardBg);
  stemsList.setColour(juce::ListBox::outlineColourId,
                      UITheme::cardBorder.withAlpha(0.7f));
  secuenciasList.setColour(juce::ListBox::backgroundColourId, UITheme::cardBg);
  secuenciasList.setColour(juce::ListBox::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.7f));
  for (int row = 0; row < static_cast<int>(stems.size()); ++row) {
    if (auto *rowComp = dynamic_cast<StemListRowComponent *>(
            stemsList.getComponentForRowNumber(row))) {
      rowComp->refreshThemeColours();
    }
  }

  applyTextEditorTheme(summaryEditor, UITheme::cardBgHover,
                       UITheme::cardBorder.withAlpha(0.7f));
  summaryEditor.setJustification(juce::Justification::topLeft);

  configureCombo(keyBox);
  configureCombo(timeSignatureBox);
  configureCombo(songKindBox);
  configureCombo(blankFormBox);
  for (auto *button : {&blankClickButton, &blankPadButton})
    button->setColour(juce::ToggleButton::textColourId, UITheme::textPrimary);
  blankSectionHelp.setColour(juce::Label::textColourId, UITheme::textPrimary);
  styleSecondaryButton(addBlankSectionButton);
  for (auto &row : blankSections) {
    applyEditorTheme(row->name, L10n::tr("Nombre de sección", "Section name"));
    applyEditorTheme(row->bars, L10n::tr("Compases", "Bars"));
    styleSecondaryButton(row->remove);
  }


  styleSecondaryButton(addFilesButton);
  styleSecondaryButton(replaceFilesButton);
  styleSecondaryButton(clearSongSearchButton);
  {
    const auto styleToolbarButton = [](juce::TextButton &button,
                                       const bool emphasised) {
      button.setColour(juce::TextButton::buttonColourId,
                      UITheme::panelDark);
      button.setColour(juce::TextButton::buttonOnColourId,
                      UITheme::cardBgPressed);
      button.setColour(juce::TextButton::textColourOffId,
                      emphasised ? UITheme::accent : UITheme::textPrimary);
      button.setColour(juce::TextButton::textColourOnId, UITheme::accent);
    };
    styleToolbarButton(cancelButton, false);
    styleToolbarButton(backButton, false);
    styleToolbarButton(saveMetadataButton, true);
    styleToolbarButton(nextButton, true);
    styleToolbarButton(submitButton, true);
    styleToolbarButton(keyboardPreviousButton, false);
    styleToolbarButton(keyboardNextButton, false);
    styleToolbarButton(keyboardDoneButton, true);
    for (auto *button : {&keyboardPreviousButton, &keyboardNextButton,
                         &keyboardDoneButton}) {
      button->setColour(juce::TextButton::buttonColourId,
                        juce::Colours::transparentBlack);
      button->setColour(juce::ComboBox::outlineColourId,
                        juce::Colours::transparentBlack);
    }
  }

  for (auto &row : mappingRows)
    row->refreshThemeColours();

  repaint();
}

void SongImportWizardComponent::configureTextEditor(
    juce::TextEditor &editor, const juce::String &placeholder) {
  applyEditorTheme(editor, placeholder);
}

void SongImportWizardComponent::configureCombo(AdaptiveComboBox &combo) {
  applyComboBoxTheme(combo, UITheme::cardBgHover,
                     UITheme::cardBorder.withAlpha(0.7f));
  combo.setMobileFormMode(useTouchOptimisedControls());
}

void SongImportWizardComponent::layoutStepImport(juce::Rectangle<int> bounds) {
  const bool blankPage = currentStep == Step::Stems && isBlankMode();
  blankFormBox.setVisible(blankPage);
  blankSectionHelp.setVisible(blankPage);
  const bool structured = blankPage && blankFormBox.getSelectedId() == 2;
  blankSectionsViewport.setVisible(structured);
  addBlankSectionButton.setVisible(structured);
  if (blankPage) {
    blankFormBox.setBounds(bounds.removeFromTop(44)); bounds.removeFromTop(8);
    blankSectionHelp.setBounds(bounds.removeFromTop(52)); bounds.removeFromTop(8);
    addBlankSectionButton.setBounds(bounds.removeFromBottom(44));
    bounds.removeFromBottom(6);
    blankSectionsViewport.setBounds(bounds);
    layoutBlankSections();
  }
  if (blankPage && structured && phoneCreationLayout) {
    if (auto *focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr && blankSectionsContent.isParentOf(focused)) {
      const auto target = blankSectionsContent.getLocalArea(focused, focused->getLocalBounds());
      const int top = blankSectionsViewport.getViewPositionY();
      const int bottom = top + blankSectionsViewport.getViewHeight();
      if (target.getBottom() + 8 > bottom) blankSectionsViewport.setViewPosition(0, target.getBottom() + 8 - blankSectionsViewport.getViewHeight());
      else if (target.getY() < top) blankSectionsViewport.setViewPosition(0, target.getY());
    }
  }
  const bool visible = currentStep == Step::Stems && !isBlankMode();
  const bool showReplace = visible && mode == Mode::Edit;

  for (auto *c :
       {static_cast<juce::Component *>(&addFilesButton),
        static_cast<juce::Component *>(&replaceFilesButton),
        static_cast<juce::Component *>(&selectedFilesLabel),
        static_cast<juce::Component *>(&stemsList),
        static_cast<juce::Component *>(&stemsSectionLabel)}) {
    const bool isReplaceButton =
        c == static_cast<juce::Component *>(&replaceFilesButton);

    if (isReplaceButton)
      c->setVisible(showReplace);
    else
      c->setVisible(visible);
  }

  if (!visible)
    return;

  if (usesCreationLayout()) {
    stemsSectionLabel.setBounds(bounds.removeFromTop(22));
    bounds.removeFromTop(8);
    if (showReplace) {
      auto actions = bounds.removeFromTop(phoneCreationLayout ? 48 : 36);
      const int gap = 8;
      const int width = (actions.getWidth() - gap) / 2;
      addFilesButton.setBounds(actions.removeFromLeft(width));
      actions.removeFromLeft(gap);
      replaceFilesButton.setBounds(actions);
    } else {
      addFilesButton.setBounds(bounds.removeFromTop(phoneCreationLayout ? 48 : 36));
    }
    bounds.removeFromTop(8);
    selectedFilesLabel.setBounds(bounds.removeFromTop(22));
    bounds.removeFromTop(8);
    stemsList.setRowHeight(phoneCreationLayout ? 62 : 48);
    stemsList.setBounds(bounds);
    return;
  }

  const int sectionLabelH = 18;
  const int rowGap = 8;

  auto stemsHeader = bounds.removeFromTop(sectionLabelH);
  stemsSectionLabel.setBounds(stemsHeader.removeFromLeft(220));
  if (showReplace) {
    auto buttons = stemsHeader.removeFromRight(292);
    addFilesButton.setBounds(buttons.removeFromLeft(140));
    buttons.removeFromLeft(12);
    replaceFilesButton.setBounds(buttons.removeFromLeft(140));
  } else {
    addFilesButton.setBounds(stemsHeader.removeFromRight(140));
  }

  bounds.removeFromTop(4);
  selectedFilesLabel.setBounds(bounds.removeFromTop(18));
  bounds.removeFromTop(6);
  stemsList.setBounds(bounds.removeFromTop(juce::jmax(150, bounds.getHeight() - 16)));
  juce::ignoreUnused(rowGap);
}

void SongImportWizardComponent::layoutStepMetadata(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == Step::Metadata;
  metadataViewport.setVisible(visible && usesCreationLayout());
  auto *metadataParent = usesCreationLayout() ? &metadataContent
                                             : static_cast<juce::Component *>(this);
  for (auto *c : {static_cast<juce::Component *>(&infoSectionLabel),
                  static_cast<juce::Component *>(&songTitleLabel),
                  static_cast<juce::Component *>(&artistLabel),
                  static_cast<juce::Component *>(&bpmLabel),
                  static_cast<juce::Component *>(&keyLabel),
                  static_cast<juce::Component *>(&timeSignatureLabel),
                  static_cast<juce::Component *>(&songTitleEditor),
                  static_cast<juce::Component *>(&artistEditor),
                  static_cast<juce::Component *>(&bpmEditor),
                  static_cast<juce::Component *>(&keyBox),
                  static_cast<juce::Component *>(&timeSignatureBox),
                  static_cast<juce::Component *>(&songKindBox),
                  static_cast<juce::Component *>(&blankClickButton),
                  static_cast<juce::Component *>(&blankPadButton),
                  static_cast<juce::Component *>(&secuenciasStatusLabel),
                  static_cast<juce::Component *>(&secuenciasDetailLabel),
                  static_cast<juce::Component *>(&secuenciasArtworkPreview),
                  static_cast<juce::Component *>(&secuenciasArtworkCaption),
                  static_cast<juce::Component *>(&changeArtworkLabel),
                  static_cast<juce::Component *>(&clearSongSearchButton)}) {
    if (c->getParentComponent() != metadataParent)
      metadataParent->addChildComponent(c);
    c->setVisible(visible);
  }
  songKindBox.setVisible(visible && mode == Mode::Import);
  blankClickButton.setVisible(visible && isBlankMode());
  blankPadButton.setVisible(visible && isBlankMode());
  if (secuenciasList.getParentComponent() != metadataParent)
    metadataParent->addChildComponent(secuenciasList);
  secuenciasList.setVisible(false);
  secuenciasStatusLabel.setVisible(false);
  secuenciasDetailLabel.setVisible(false);
  secuenciasArtworkCaption.setVisible(false);
  clearSongSearchButton.setVisible(false);
  if (!visible) {
    artworkOverlayButtonBounds = {};
    metadataCardBounds = {};
    return;
  }

  if (usesCreationLayout()) {
    infoSectionLabel.setVisible(false);
    secuenciasStatusLabel.setVisible(false);
    secuenciasDetailLabel.setVisible(false);
    secuenciasArtworkCaption.setVisible(false);
    clearSongSearchButton.setVisible(false);
    changeArtworkLabel.setVisible(true);

    const int fieldLabelH = 20;
    const int fieldH = phoneCreationLayout ? 48 : 36;
    const int gap = 8;
    const int groupGap = 9;

    metadataViewport.setBounds(bounds);
    // Content geometry is independent of keyboard height. Only its visible
    // window changes, so editing never swaps the form for a different layout.
    metadataContent.setSize(juce::jmax(1, bounds.getWidth() - 2), (phoneCreationLayout ? 550 : 460) + 60 + (isBlankMode() ? 54 : 0));
    bounds = metadataContent.getLocalBounds();
    if (mode == Mode::Import) {
      songKindBox.setBounds(bounds.removeFromTop(44));
      bounds.removeFromTop(12);
    }
    const int artworkSize = phoneCreationLayout ? 128 : 104;
    auto artworkLine = bounds.removeFromTop(artworkSize);
    auto artwork = artworkLine.withSizeKeepingCentre(artworkSize, artworkSize);
    secuenciasArtworkPreview.setBounds(artwork);
    secuenciasArtworkPreview.setVisible(false);
    artworkOverlayButtonBounds = juce::Rectangle<int>(
        artwork.getRight() - 46, artwork.getBottom() - 46, 44, 44);
    bounds.removeFromTop(4);
    changeArtworkLabel.setBounds(bounds.removeFromTop(25));
    bounds.removeFromTop(groupGap);

    songTitleLabel.setBounds(bounds.removeFromTop(fieldLabelH));
    songTitleEditor.setBounds(bounds.removeFromTop(fieldH));
    bounds.removeFromTop(groupGap);
    artistLabel.setBounds(bounds.removeFromTop(fieldLabelH));
    artistEditor.setBounds(bounds.removeFromTop(fieldH));
    bounds.removeFromTop(groupGap);

    auto row = bounds.removeFromTop(fieldLabelH + fieldH);
    const int colW = (row.getWidth() - 2 * gap) / 3;
    auto col = row.removeFromLeft(colW);
    bpmLabel.setBounds(col.removeFromTop(fieldLabelH));
    bpmEditor.setBounds(col.removeFromTop(fieldH));
    row.removeFromLeft(gap);
    col = row.removeFromLeft(colW);
    keyLabel.setBounds(col.removeFromTop(fieldLabelH));
    keyBox.setBounds(col.removeFromTop(fieldH));
    row.removeFromLeft(gap);
    col = row;
    timeSignatureLabel.setBounds(col.removeFromTop(fieldLabelH));
    timeSignatureBox.setBounds(col.removeFromTop(fieldH));

    bounds.removeFromTop(12);
    if (isBlankMode()) {
      auto options = bounds.removeFromTop(44);
      blankClickButton.setBounds(options.removeFromLeft(options.getWidth() / 3));
      blankPadButton.setBounds(options);
      bounds.removeFromTop(8);
    }
    metadataCardBounds = bounds.removeFromTop(70);

    if (!secuenciasResults.empty()) {
      const int listH = juce::jmin(
          phoneKeyboardVisible ? 108 : 220,
          8 + static_cast<int>(secuenciasResults.size()) *
                      secuenciasList.getRowHeight());
      secuenciasList.setBounds(songTitleEditor.getX(),
                               songTitleEditor.getBottom() + 4,
                               songTitleEditor.getWidth(), listH);
      secuenciasList.toFront(false);
    }
    return;
  }

  changeArtworkLabel.setVisible(false);
  metadataCardBounds = {};

  const int sectionLabelH = 18;
  const int fieldLabelH = 16;
  const int fieldH = touchOptimisedControlHeight();

  if (mode == Mode::Import) {
    songKindBox.setBounds(bounds.removeFromTop(44));
    bounds.removeFromTop(12);
  }
  infoSectionLabel.setBounds(bounds.removeFromTop(sectionLabelH));
  bounds.removeFromTop(8);

  auto body = bounds;
  auto left = body.removeFromLeft(juce::jlimit(360, 560, (body.getWidth() * 65) / 100));
  body.removeFromLeft(14);
  auto right = body;

  auto labelRow = left.removeFromTop(fieldLabelH);
  songTitleLabel.setBounds(labelRow);
  auto titleRow = left.removeFromTop(fieldH);
  auto clearArea = titleRow.removeFromRight(92);
  clearSongSearchButton.setBounds(clearArea);
  titleRow.removeFromRight(8);
  songTitleEditor.setBounds(titleRow);

  // Dropdown de sugerencias en overlay: no desplaza otros campos.
  if (!secuenciasResults.empty()) {
    const int listH = juce::jmin(220, 8 + (int)secuenciasResults.size() * secuenciasList.getRowHeight());
    secuenciasList.setBounds(songTitleEditor.getX(), songTitleEditor.getBottom() + 4,
                             songTitleEditor.getWidth(), listH);
    secuenciasList.toFront(false);
  }

  left.removeFromTop(10);
  labelRow = left.removeFromTop(fieldLabelH);
  artistLabel.setBounds(labelRow);
  artistEditor.setBounds(left.removeFromTop(fieldH));

  left.removeFromTop(10);
  auto row = left.removeFromTop(fieldLabelH + fieldH);
  const int gap = 10;
  const int colW = (row.getWidth() - (2 * gap)) / 3;

  auto col = row.removeFromLeft(colW);
  bpmLabel.setBounds(col.removeFromTop(fieldLabelH));
  bpmEditor.setBounds(col.removeFromTop(fieldH));

  row.removeFromLeft(gap);
  col = row.removeFromLeft(colW);
  keyLabel.setBounds(col.removeFromTop(fieldLabelH));
  keyBox.setBounds(col.removeFromTop(fieldH));

  row.removeFromLeft(gap);
  col = row.removeFromLeft(colW);
  timeSignatureLabel.setBounds(col.removeFromTop(fieldLabelH));
  timeSignatureBox.setBounds(col.removeFromTop(fieldH));

  left.removeFromTop(10);
  secuenciasStatusLabel.setBounds(left.removeFromTop(16));
  left.removeFromTop(4);
  secuenciasDetailLabel.setBounds(left.removeFromTop(40));

  auto artworkBounds = right.removeFromTop(260);
  secuenciasArtworkPreview.setBounds(artworkBounds);
  artworkOverlayButtonBounds = artworkBounds.withSizeKeepingCentre(44, 44);
  right.removeFromTop(8);
  secuenciasArtworkCaption.setBounds(right.removeFromTop(36));
}

void SongImportWizardComponent::layoutStepMapping(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == Step::Mapping;
  mapViewport.setVisible(visible);
  for (auto *c : {static_cast<juce::Component *>(&mapFileHeader),
                  static_cast<juce::Component *>(&mapNameHeader),
                  static_cast<juce::Component *>(&mapCategoryHeader),
                  static_cast<juce::Component *>(&mapLoadHeader)}) {
    c->setVisible(visible);
  }
  if (!visible)
    return;

  if (usesCreationLayout()) {
    for (auto *c : {static_cast<juce::Component *>(&mapFileHeader),
                    static_cast<juce::Component *>(&mapNameHeader),
                    static_cast<juce::Component *>(&mapCategoryHeader),
                    static_cast<juce::Component *>(&mapLoadHeader)})
      c->setVisible(false);
    mapViewport.setBounds(bounds);
    layoutMappingRows();
    return;
  }

  auto header = bounds.removeFromTop(20);
  auto fileCol = header.removeFromLeft(230);
  auto nameCol = header.removeFromLeft(170);
  auto catCol = header.removeFromLeft(160);
  mapFileHeader.setBounds(fileCol);
  mapNameHeader.setBounds(nameCol);
  mapCategoryHeader.setBounds(catCol);
  mapLoadHeader.setBounds(header.reduced(8, 0));

  bounds.removeFromTop(6);
  mapViewport.setBounds(bounds);
  layoutMappingRows();
}

void SongImportWizardComponent::layoutStepReview(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == Step::Review;
  summaryEditor.setVisible(visible);
  if (!visible)
    return;
  summaryEditor.setBounds(bounds);
}

void SongImportWizardComponent::openStemChooser(const bool replaceCurrent) {
  if (isSubmitting)
    return;
  auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
  stemChooser = std::make_unique<juce::FileChooser>(
      L10n::tr("Selecciona stems", "Select stems"),
      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
      stemImportWildcard(mode == Mode::Import));

  const auto chooserFlags = juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles |
                            juce::FileBrowserComponent::canSelectMultipleItems;

  stemChooser->launchAsync(chooserFlags,
                           [safeThis, replaceCurrent](const juce::FileChooser &chooser) {
                             if (safeThis == nullptr)
                               return;
#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
                             safeThis->stageSelectedMobileFiles(
                                 chooser.getURLResults(), replaceCurrent, false);
#else
                             safeThis->handleSelectedStemFiles(chooser.getResults(),
                                                               replaceCurrent);
#endif
                             safeThis->stemChooser.reset();
                           });
}

#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
void SongImportWizardComponent::stageSelectedMobileFiles(
    juce::Array<juce::URL> urls, const bool replaceCurrent, const bool artwork) {
  if (isSubmitting || urls.isEmpty())
    return;
  if (mobileImportStaging == nullptr)
    mobileImportStaging = std::make_shared<MobileImportStaging>();

  setSubmitting(true);
  juce::Component::unfocusAllComponents();
  submissionOverlay = std::make_unique<SongLoadProgressOverlay>();
  addAndMakeVisible(*submissionOverlay);
  submissionOverlay->refreshThemeColours();
  submissionOverlay->setTitleText(
      artwork ? L10n::tr("Importando portada", "Importing artwork")
              : L10n::tr("Importando archivos", "Importing files"));
  submissionOverlay->setPhaseText(
      L10n::tr("Preparando copia local...", "Preparing local copy..."));
  submissionOverlay->setProgress(0.0);
  submissionOverlay->setAlwaysOnTop(true);
  submissionOverlay->setBounds(getLocalBounds());
  submissionOverlay->toFront(true);

  auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
  const bool started = juce::Thread::launch(
      [urls = std::move(urls), staging = mobileImportStaging,
       cachedFiles = stagedMobileFiles, safeThis, replaceCurrent, artwork]() mutable {
        // Keep every original URL (including its security bookmark or content
        // provider permission) alive until the complete batch has been copied.
        juce::Array<juce::File> localFiles;
        juce::Image artworkImage;
        juce::String error;
        juce::uint32 lastReportTime = 0;
        int lastReportedFile = -1;
        try {
          for (int index = 0; index < urls.size(); ++index) {
            const auto report = [safeThis, index, count = urls.size(),
                                 &lastReportTime, &lastReportedFile](double progress) {
              const auto now = juce::Time::getMillisecondCounter();
              if (lastReportedFile == index && progress < 1.0 &&
                  now - lastReportTime < 33)
                return;
              lastReportTime = now;
              lastReportedFile = index;
              const auto totalProgress =
                  (index + juce::jlimit(0.0, 1.0, progress)) / count;
              const auto phase =
                  L10n::tr("Copiando archivo ", "Copying file ") +
                  juce::String(index + 1) + " / " + juce::String(count);
              juce::MessageManager::callAsync([safeThis, totalProgress, phase] {
                if (safeThis == nullptr || safeThis->submissionOverlay == nullptr)
                  return;
                safeThis->submissionOverlay->setProgress(totalProgress);
                safeThis->submissionOverlay->setPhaseText(phase);
              });
            };
            const auto sourceKey = urls[index].toString(true).toStdString();
            const auto previous = cachedFiles.find(sourceKey);
            juce::File local;
            if (!replaceCurrent && !artwork && previous != cachedFiles.end() &&
                previous->second.existsAsFile() &&
                previous->second.getSize() > 0) {
              local = previous->second;
              report(1.0);
            } else {
              local = staging->stage(urls[index], error, report);
            }
            if (!local.existsAsFile() || local.getSize() <= 0) {
              if (error.isEmpty())
                error = L10n::tr("No se pudo copiar el archivo seleccionado.",
                                 "Couldn't copy the selected file.");
              break;
            }
            cachedFiles[sourceKey] = local;
            localFiles.add(local);
          }
          if (error.isEmpty() && artwork && !localFiles.isEmpty()) {
            artworkImage = juce::ImageFileFormat::loadFrom(localFiles.getFirst());
            if (!artworkImage.isValid())
              error = L10n::tr("No se pudo leer la imagen seleccionada.",
                               "Couldn't read the selected image.");
          }
        } catch (const std::exception &exception) {
          error = juce::String::fromUTF8(exception.what());
        } catch (...) {
          error = L10n::tr("No se pudieron importar los archivos.",
                           "Couldn't import the files.");
        }

        const bool complete = error.isEmpty() && localFiles.size() == urls.size();
        juce::MessageManager::callAsync(
            [safeThis, staging, localFiles = std::move(localFiles),
             cachedFiles = std::move(cachedFiles), artworkImage, error, complete,
             replaceCurrent, artwork]() mutable {
              juce::ignoreUnused(staging); // Keep private sources until UI commit.
              // The owner may have disappeared while a provider downloaded a
              // file; only this UI callback may inspect the component pointer.
              if (safeThis == nullptr)
                return;
              safeThis->submissionOverlay.reset();
              if (complete) {
                safeThis->stagedMobileFiles = std::move(cachedFiles);
                if (artwork) {
                  safeThis->selectedArtworkUrl =
                      "manual:" + localFiles.getFirst().getFullPathName();
                  safeThis->secuenciasArtworkPreview.setImage(
                      artworkImage, juce::RectanglePlacement::centred);
                  safeThis->secuenciasArtworkCaption.setText(
                      L10n::tr("Imagen manual", "Manual image"),
                      juce::dontSendNotification);
                  safeThis->repaint();
                } else {
                  // Commit a replacement only after every source is local;
                  // partial/failed provider copies never erase the old draft.
                  safeThis->handleSelectedStemFiles(localFiles, replaceCurrent);
                }
              }
              safeThis->setSubmitting(false);
              if (!complete)
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    L10n::tr("Error al importar", "Import error"),
                    error.isNotEmpty()
                        ? error
                        : L10n::tr("No se pudieron copiar todos los archivos.",
                                   "Couldn't copy all files."));
            });
      });
  if (!started) {
    submissionOverlay.reset();
    setSubmitting(false);
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Error al importar", "Import error"),
        L10n::tr("No se pudo iniciar la copia. Intenta de nuevo.",
                 "Couldn't start copying. Please try again."));
  }
}
#endif

void SongImportWizardComponent::handleSelectedStemFiles(
    const juce::Array<juce::File> &files, const bool replaceCurrent) {
  // Cancelling or selecting only unsupported files must preserve the draft.
  if (files.isEmpty())
    return;

  std::vector<StemDraft> selectedStems;
  juce::StringArray unsupportedFiles;
  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  for (const auto &file : files) {
    if (!file.existsAsFile())
      continue;
    if (!file.hasFileExtension(mode == Mode::Import ? kCreationExtensions : kStemImportExtensions)) {
      unsupportedFiles.add(file.getFileName());
      continue;
    }
    const auto normalizedPath = file.getFullPathName();
    const bool alreadyThere = !replaceCurrent &&
        std::any_of(stems.begin(), stems.end(), [&normalizedPath](const StemDraft &s) {
          return s.file.existsAsFile() &&
                 s.file.getFullPathName().equalsIgnoreCase(normalizedPath);
        });
    if (alreadyThere)
      continue;
    StemDraft draft;
    draft.file = file;
    draft.sourcePath = file.getFullPathName();
    draft.storedPath.clear();
    draft.stemId = juce::Uuid().toString();
    draft.displayName = makeDisplayNameFromFile(file);
    draft.category = suggestCategoryFromFilename(file.getFileNameWithoutExtension());
    draft.fromExistingSong = false;
    draft.include = true;

    // Metadata is best-effort here; FFmpeg validates and decodes the actual
    // stream when saving. Some desktop inputs have no native JUCE reader.
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(file))) {
      draft.sampleRate = static_cast<int>(reader->sampleRate);
      draft.channels = static_cast<int>(reader->numChannels);
      if (reader->sampleRate > 0.0) {
        draft.durationSec =
            static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
      }
    }

    selectedStems.push_back(std::move(draft));
  }

  if (!unsupportedFiles.isEmpty()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Formato no compatible", "Unsupported format"),
        L10n::tr(
            "Formatos de audio admitidos: ", "Supported audio formats: ") +
            (mode == Mode::Import ? kCreationFormatNames : kStemImportFormatNames) +
            L10n::tr(". Archivos omitidos:\n", ". Skipped files:\n") +
            unsupportedFiles.joinIntoString("\n"));
  }

  if (selectedStems.empty())
    return;

  // Preserve edited names/categories before changing the collection and
  // rebuilding rows. The old rows still map one-to-one to these drafts here.
  syncMappingsToDrafts();

  const bool hasOnlyPlaceholderStems =
      !stems.empty() &&
      std::all_of(stems.begin(), stems.end(), [](const StemDraft &stem) {
        return stem.sourcePath.trim().isEmpty() &&
               stem.storedPath.trim().isEmpty() && !stem.file.existsAsFile();
      });
  if (replaceCurrent || hasOnlyPlaceholderStems)
    stems.clear();
  for (auto& draft : selectedStems)
    stems.push_back(std::move(draft));

  if (songTitleEditor.getText().trim().isEmpty() && !files.isEmpty()) {
    songTitleEditor.setText(files[0].getParentDirectory().getFileName(),
                            juce::dontSendNotification);
  }

  updateSelectedFilesText();
  rebuildMappingRows();
}

void SongImportWizardComponent::preloadFromExistingSong() {
  if (initialSong.blankSong) {
    ProjectSongSettings saved;
    if (ProjectStore::getCachedSettings(projectIdentityForSong(initialSong), saved)) {
      blankClickButton.setToggleState(saved.clickDynMix.metronomeEnabled, juce::dontSendNotification);
      blankPadButton.setToggleState(saved.blankPadEnabled.value_or(initialSong.blankPadEnabled), juce::dontSendNotification);
    }
  }
  songTitleEditor.setText(initialSong.title, juce::dontSendNotification);
  artistEditor.setText(initialSong.artist, juce::dontSendNotification);
  bpmEditor.setText(initialSong.bpm > 0 ? juce::String(initialSong.bpm)
                                        : juce::String(),
                    juce::dontSendNotification);
  if (initialSong.key.isNotEmpty())
    keyBox.setText(initialSong.key, juce::dontSendNotification);
  if (initialSong.timeSignature.isNotEmpty()) {
    timeSignatureBox.setText(initialSong.timeSignature,
                             juce::dontSendNotification);
  }

  selectedArtworkUrl = initialSong.artworkSource.trim();
  juce::Image persistedArtwork;
  if (initialSong.artworkPath.trim().isNotEmpty()) {
    const juce::File artworkFile(initialSong.artworkPath.trim());
    if (artworkFile.existsAsFile())
      persistedArtwork = juce::ImageFileFormat::loadFrom(artworkFile);
  }
  if (persistedArtwork.isValid()) {
    if (selectedArtworkUrl.trim().isEmpty())
      selectedArtworkUrl = "manual:" + initialSong.artworkPath.trim();
    secuenciasArtworkPreview.setImage(persistedArtwork,
                                      juce::RectanglePlacement::centred);
    secuenciasArtworkCaption.setText(
        L10n::tr("Imagen guardada", "Saved image"),
        juce::dontSendNotification);
  } else if (selectedArtworkUrl.startsWithIgnoreCase("http")) {
    secuenciasArtworkPreview.setImage(juce::Image(),
                                      juce::RectanglePlacement::centred);
    secuenciasArtworkCaption.setText(
        L10n::tr("Artwork remoto", "Remote artwork"),
        juce::dontSendNotification);
    requestArtworkAsync(selectedArtworkUrl);
  } else {
    selectedArtworkUrl.clear();
    secuenciasArtworkPreview.setImage(juce::Image(),
                                      juce::RectanglePlacement::centred);
    secuenciasArtworkCaption.setText({}, juce::dontSendNotification);
  }

  stems.clear();
  stems.reserve(initialSong.stems.size());

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  for (const auto &stem : initialSong.stems) {
    StemDraft draft;
    juce::File sourceFile(stem.storedPath);
    if (!sourceFile.existsAsFile())
      sourceFile = juce::File(stem.sourcePath);

    draft.file = sourceFile;
    draft.sourcePath = stem.sourcePath;
    draft.storedPath = stem.storedPath;
    draft.stemId = stem.stemId.isNotEmpty() ? stem.stemId : juce::Uuid().toString();
    draft.displayName =
        stem.displayName.isNotEmpty() ? stem.displayName : makeDisplayNameFromFile(sourceFile);
    draft.category = stem.category.isNotEmpty()
                         ? stem.category
                         : suggestCategoryFromFilename(sourceFile.getFileNameWithoutExtension());
    draft.fromExistingSong = true;
    draft.include = stem.include;
    draft.durationSec = stem.durationSec;
    draft.sampleRate = stem.sampleRate;
    draft.channels = stem.channels;

    if ((draft.durationSec <= 0.0 || draft.sampleRate <= 0 || draft.channels <= 0) &&
        sourceFile.existsAsFile()) {
      if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
              formatManager.createReaderFor(sourceFile))) {
        draft.sampleRate = static_cast<int>(reader->sampleRate);
        draft.channels = static_cast<int>(reader->numChannels);
        if (reader->sampleRate > 0.0) {
          draft.durationSec =
              static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        }
      }
    }

    stems.push_back(std::move(draft));
  }

  updateSelectedFilesText();
  rebuildMappingRows();
}

void SongImportWizardComponent::updateSelectedFilesText() {
  if (stems.empty()) {
    selectedFilesLabel.setText(
        L10n::tr("No hay stems seleccionados", "No stems selected"),
        juce::dontSendNotification);
    stemsList.updateContent();
    stemsList.selectRow(-1);
    stemsList.repaint();
    repaint(metadataCardBounds);
    return;
  }

  const int previousRow = stemsList.getSelectedRow();
  const auto included =
      std::count_if(stems.begin(), stems.end(),
                    [](const StemDraft &stem) { return stem.include; });
  selectedFilesLabel.setText(
      juce::String(static_cast<int>(included)) + " / " +
          juce::String(static_cast<int>(stems.size())) + " " +
          L10n::tr("stems listos para mapear", "stems ready to map"),
      juce::dontSendNotification);

  stemsList.updateContent();
  if (stems.empty())
    stemsList.selectRow(-1);
  else
    stemsList.selectRow(
        juce::jlimit(0, static_cast<int>(stems.size()) - 1,
                     previousRow >= 0 ? previousRow : 0));
  stemsList.repaint();
  repaint(metadataCardBounds);
}

void SongImportWizardComponent::removeStemAtIndex(const int rowIndex) {
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(stems.size())))
    return;

  syncMappingsToDrafts();
  stems.erase(stems.begin() + rowIndex);
  updateSelectedFilesText();
  rebuildMappingRows();
}

void SongImportWizardComponent::rebuildMappingRows() {
  lastMappingFocus = nullptr;
  mappingRows.clear();
  mapContent.removeAllChildren();

  for (const auto &stem : stems) {
    auto row = std::make_unique<MappingRow>(stem, usesCreationLayout());
    if (useTouchOptimisedControls())
      row->nameEditor().onReturnKey = [this] { advanceTextEditor(); };
    row->nameEditor().getProperties().set(
        "playworshipReturnKeyNext", &stem != &stems.back());
    mapContent.addAndMakeVisible(*row);
    mappingRows.push_back(std::move(row));
  }

  layoutMappingRows();
}

void SongImportWizardComponent::layoutMappingRows() {
  const int rowH = usesCreationLayout() ? (phoneCreationLayout ? 104 : 80) : 42;
  const int gap = 5;
  const int width = juce::jmax(1, mapViewport.getWidth() - 10);
  int y = 6;
  for (auto &row : mappingRows) {
    row->setPhoneLayout(usesCreationLayout());
    row->setBounds(4, y, width, rowH);
    y += rowH + gap;
  }
  mapContent.setSize(width + 8, juce::jmax(mapViewport.getHeight(), y + 4));
}

void SongImportWizardComponent::syncMappingsToDrafts() {
  const auto count = juce::jmin(mappingRows.size(), stems.size());
  for (size_t i = 0; i < count; ++i)
    mappingRows[i]->applyTo(stems[i]);
}

void SongImportWizardComponent::updateSummaryText() {
  syncMappingsToDrafts();

  const auto included =
      std::count_if(stems.begin(), stems.end(),
                    [](const StemDraft &stem) { return stem.include; });
  const auto excluded = static_cast<int>(stems.size()) - static_cast<int>(included);

  juce::StringArray lines;
  lines.add(mode == Mode::Edit ? L10n::tr(u8"Modo: Edición", "Mode: Edit")
                               : L10n::tr(u8"Modo: Creación", "Mode: Create"));
  lines.add(L10n::tr(u8"Canción: ", "Song: ") + songTitleEditor.getText().trim());
  lines.add(L10n::tr("Artista: ", "Artist: ") + artistEditor.getText().trim());
  lines.add("BPM: " + bpmEditor.getText().trim() + "   " +
            L10n::tr("Tono: ", "Key: ") + keyBox.getText() + "   " +
            L10n::tr(u8"Compás: ", "Time: ") + timeSignatureBox.getText());
  lines.add(L10n::tr("Tipo de archivo: ", "File type: ") +
            (stems.empty() ? juce::String("remote-reference")
                           : juce::String("FLAC")));
  lines.add(L10n::tr("Stems a cargar: ", "Stems to load: ") +
            juce::String(static_cast<int>(included)) + " / " +
            juce::String(static_cast<int>(stems.size())));
  if (mode == Mode::Import && resolveProjectDestination) {
    lines.add(L10n::tr(u8"Se creará un proyecto .pwproj en la biblioteca local.",
                      "A .pwproj project will be created in the local library."));
  }
  if (mode == Mode::Edit && excluded > 0) {
    lines.add(L10n::tr("Stems removidos al guardar: ",
                       "Stems removed on save: ") +
              juce::String(excluded));
  }
  lines.add("");
  lines.add(L10n::tr("Stems mapeados:", "Mapped stems:"));

  for (const auto &stem : stems) {
    juce::String fileName = stem.file.getFileName();
    if (fileName.isEmpty() && stem.storedPath.isNotEmpty())
      fileName = juce::File(stem.storedPath).getFileName();
    if (fileName.isEmpty() && stem.sourcePath.isNotEmpty())
      fileName = juce::File(stem.sourcePath).getFileName();
    if (fileName.isEmpty())
      fileName = L10n::tr("sin archivo", "no file");

    lines.add((stem.include ? "[x] " : "[ ] ") + stem.displayName + " -> " +
              stem.category + " (" + fileName + ")");
  }

  if (isBlankMode()) {
    const auto song = buildSongData();
    lines.clear();
    lines.add(song.title);
    lines.add(juce::String(song.bpm) + " BPM · " + song.timeSignature + " · " + song.key);
    lines.add(L10n::tr("En blanco", "Blank") + juce::String(" · ") +
        (song.blankSongBars == 0 ? L10n::tr("Libre hasta Detener", "Free until Stop")
          : juce::String(song.blankSongBars) + L10n::tr(" compases", " bars")));
    lines.add(juce::String("Click: ") + (song.blankClickEnabled ? "On" : "Off") +
        " · Pad: " + (song.blankPadEnabled ? "On" : "Off"));
    for (size_t i = 0; i < song.arrangement.parts.size(); ++i)
      lines.add(song.arrangement.parts[i].name + " · " + blankSections[i]->bars.getText() + L10n::tr(" compases", " bars"));
    lines.add(L10n::tr("Se guardará en Biblioteca como proyecto .pwproj.", "Saved in Library as a .pwproj project."));
  }
  summaryEditor.setText(lines.joinIntoString("\n"),
                        juce::dontSendNotification);
}

void SongImportWizardComponent::goToNextStep() {
  if (isSubmitting)
    return;
  switch (currentStep) {
  case Step::Metadata:
    if (!validateStepMetadata())
      return;
    hideSecuenciasSuggestions(false);
    changeStep(Step::Stems);
    return;
  case Step::Stems:
    if (isBlankMode()) {
      auto song = buildSongData();
      if (!buildBlankArrangement(song)) {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
          L10n::tr("Revisa las secciones", "Check the sections"),
          L10n::tr("Agrega un nombre y entre 1 y 10000 compases por sección (máximo 100000 en total).",
                   "Enter a name and 1–10000 bars per section (100000 total maximum)."));
        return;
      }
      changeStep(Step::Review);
      return;
    }
    if (!validateStepImport())
      return;
    changeStep(Step::Mapping);
    return;
  case Step::Mapping:
    if (!validateStepMapping())
      return;
    changeStep(Step::Review);
    return;
  case Step::Review:
  default:
    return;
  }
}

void SongImportWizardComponent::saveMetadataOnly() {
  if (isSubmitting || mode != Mode::Edit || currentStep != Step::Metadata)
    return;
  if (!validateStepMetadata() || (isBlankMode() && !validateStepImport()))
    return;
  if (isProjectBackedSong(initialSong)) {
    // Updating a portable project repacks its manifest and artwork. Keep that
    // disk work off the UI thread and retain staged mobile picker files.
    setSubmitting(true);
    if (!submitSongInBackground(buildSongData()))
      setSubmitting(false);
    return;
  }
  if (!onMetadataOnlySubmit) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("No se pudo guardar", "Couldn't save"),
        L10n::tr("El guardado rápido no está disponible para esta canción.",
                 "Quick save is not available for this song."));
    return;
  }

  setSubmitting(true);
  const juce::ScopeGuard resetSubmitting{
      [safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this)] {
        if (safeThis != nullptr)
          safeThis->setSubmitting(false);
      }};

  auto songData = buildSongData();
  songData.id = initialSong.id;
  juce::String errorMessage;
  const bool ok = onMetadataOnlySubmit(std::move(songData), errorMessage);
  if (!ok) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Error al guardar", "Save error"),
        errorMessage.isNotEmpty()
            ? errorMessage
            : L10n::tr(u8"No se pudo guardar la metadata de la canción.",
                       "Couldn't save the song metadata."));
    return;
  }

  setSubmitting(false);
  closeDialog();
}

bool SongImportWizardComponent::validateStepImport() {
  if (isBlankMode()) {
    auto song = buildSongData();
    return buildBlankArrangement(song);
  }
  const bool creatingPortableProject =
      mode == Mode::Import && static_cast<bool>(resolveProjectDestination);
  if (creatingPortableProject) {
    // Archivos must let the user return to Mapping to re-enable a track.
    // Inclusion is enforced in Mapping/Review, where Cargar can be changed.
    const bool choosingMapping = currentStep == Step::Stems;
    const auto eligibleCount = std::count_if(
        stems.begin(), stems.end(), [choosingMapping](const StemDraft &stem) {
          return choosingMapping || stem.include;
        });
    if (eligibleCount == 0) {
      juce::NativeMessageBox::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon,
          L10n::tr("Agrega audio al proyecto", "Add audio to the project"),
          choosingMapping
              ? L10n::tr(u8"Selecciona al menos un archivo de audio para continuar.",
                         "Select at least one audio file to continue.")
              : L10n::tr(u8"Activa Cargar en al menos una pista para crear el proyecto.",
                         "Enable Load on at least one track to create the project."));
      return false;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    juce::StringArray unreadableFiles;
    for (const auto &stem : stems) {
      if (!choosingMapping && !stem.include)
        continue;
      juce::File source;
      for (const auto &candidate : {stem.file, juce::File(stem.storedPath),
                                    juce::File(stem.sourcePath)}) {
        if (candidate.existsAsFile() && candidate.getSize() > 0) {
          source = candidate;
          break;
        }
      }
      bool readable = source.existsAsFile() &&
                      source.hasFileExtension(kCreationExtensions) &&
                      source.createInputStream() != nullptr;
      if (readable) {
        const auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(source));
        readable = reader != nullptr && reader->sampleRate > 0.0 &&
                   reader->lengthInSamples > 0 && reader->numChannels > 0;
#if !JUCE_IOS && !JUCE_ANDROID && !MTCONTROLS_MAC_APP_STORE
        // M4A has no portable JUCE decoder on Windows/Linux. The creation
        // worker validates and converts it before computing metadata/waveform.
        // Keep other formats' native-reader validation intact.
        if (!readable && source.hasFileExtension("m4a"))
          readable = true;
#endif
      }
      if (!readable)
        unreadableFiles.add(stem.displayName.isNotEmpty()
                                ? stem.displayName
                                : stem.file.getFileName());
    }
    const bool hasReadableSource = eligibleCount > unreadableFiles.size();
    if ((!choosingMapping && !unreadableFiles.isEmpty()) || !hasReadableSource) {
      juce::NativeMessageBox::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon,
          L10n::tr("Audio no disponible", "Audio unavailable"),
          (choosingMapping
               ? L10n::tr(u8"Vuelve a seleccionar al menos un archivo de audio legible:\n",
                          "Select at least one readable audio file again:\n")
               : L10n::tr(u8"Vuelve a seleccionar estos archivos o desactiva Cargar antes de continuar:\n",
                          "Select these files again or disable Load before continuing:\n")) +
              unreadableFiles.joinIntoString("\n"));
      return false;
    }
    return true;
  }
  if (stems.empty())
    return true;

  const bool hasAnyRealSource =
      std::any_of(stems.begin(), stems.end(), [](const StemDraft &stem) {
        if (stem.file.existsAsFile())
          return true;
        if (juce::File(stem.storedPath).existsAsFile())
          return true;
        if (juce::File(stem.sourcePath).existsAsFile())
          return true;
        return false;
      });
  if (!hasAnyRealSource) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Stems no disponibles", "Stems unavailable"),
        L10n::tr(u8"No se encontraron archivos de audio válidos para importar.",
                 "No valid audio files were found for import."));
    return false;
  }
  return true;
}

bool SongImportWizardComponent::validateStepMetadata() {
  if (songTitleEditor.getText().trim().isEmpty()) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr(u8"Falta el título", "Missing song title"),
        L10n::tr(u8"Agrega un título de canción antes de continuar.",
                 "Add a song title before continuing."));
    return false;
  }
  const auto bpmText = bpmEditor.getText().trim();
  if ((isBlankMode() || bpmText.isNotEmpty()) &&
      (bpmText.getIntValue() < 20 || bpmText.getIntValue() > 300)) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("BPM fuera de rango", "BPM out of range"),
        L10n::tr("Usa un BPM original entre 20 y 300, o déjalo vacío si es desconocido.",
                 "Use an original BPM between 20 and 300, or leave it empty if unknown."));
    return false;
  }
  return true;
}

bool SongImportWizardComponent::validateStepMapping() {
  if (isBlankMode()) return validateStepImport();
  syncMappingsToDrafts();
  if (stems.empty())
    return true;

  const bool hasAnyLoaded =
      std::any_of(stems.begin(), stems.end(),
                  [](const StemDraft &stem) { return stem.include; });
  if (!hasAnyLoaded) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("No hay stems seleccionados", "No stems selected"),
        mode == Mode::Import && resolveProjectDestination
            ? L10n::tr("Activa Cargar en al menos una pista para crear el proyecto.",
                       "Enable Load on at least one track to create the project.")
            : L10n::tr("Activa al menos un stem para cargar o deja la lista vacía para metadata-only.",
                       "Enable at least one stem or leave the list empty for metadata-only."));
    return false;
  }
  if (currentStep == Step::Mapping && mode == Mode::Import &&
      resolveProjectDestination)
    return validateStepImport();
  return true;
}

ImportedSongData SongImportWizardComponent::buildSongData() {
  syncMappingsToDrafts();

  ImportedSongData song = (mode == Mode::Edit ? initialSong : ImportedSongData{});
  if (mode == Mode::Import || song.id.isEmpty())
    song.id = juce::Uuid().toString();

  song.title = songTitleEditor.getText().trim();
  song.artist = artistEditor.getText().trim();
  song.bpm = bpmEditor.getText().getIntValue();
  song.key = keyBox.getText().trim();
  song.timeSignature = timeSignatureBox.getText().trim();
  song.fileType = stems.empty() ? "remote-reference" : "FLAC";
  song.artworkSource = selectedArtworkUrl.trim();
  // A newly selected cover must not be shadowed by the old local image when
  // the project package is rebuilt. An unchanged source keeps its local copy.
  if (song.artworkSource.isEmpty() ||
      (mode == Mode::Edit && song.artworkSource != initialSong.artworkSource.trim()))
    song.artworkPath.clear();
  if (song.createdMs <= 0)
    song.createdMs = juce::Time::getCurrentTime().toMilliseconds();

  if (isBlankMode()) {
    buildBlankArrangement(song);
    return song;
  }
  song.stems.clear();
  song.stems.reserve(stems.size());
  std::set<juce::String> usedStemIds;
  for (const auto &stem : stems) {
    if (!stem.include)
      continue;

    ImportedStemData data;
    auto candidateStemId = stem.stemId.trim();
    if (candidateStemId.isEmpty() || usedStemIds.count(candidateStemId) > 0)
      candidateStemId = juce::Uuid().toString();
    data.stemId = candidateStemId;
    usedStemIds.insert(data.stemId);
    data.displayName = stem.displayName.trim();
    if (data.displayName.isEmpty())
      data.displayName = makeDisplayNameFromFile(stem.file);
    data.category = stem.category.trim();
    if (data.category.isEmpty())
      data.category = "Other";
    data.include = true;
    data.durationSec = stem.durationSec;
    data.sampleRate = stem.sampleRate;
    data.channels = stem.channels;

    juce::String fileName = stem.file.getFileName();
    if (fileName.isEmpty() && stem.storedPath.isNotEmpty())
      fileName = juce::File(stem.storedPath).getFileName();
    if (fileName.isEmpty() && stem.sourcePath.isNotEmpty())
      fileName = juce::File(stem.sourcePath).getFileName();
    data.fileName = fileName;

    data.sourcePath = stem.sourcePath.trim();
    if (data.sourcePath.isEmpty() && stem.file.existsAsFile())
      data.sourcePath = stem.file.getFullPathName();

    if (stem.fromExistingSong && stem.storedPath.trim().isNotEmpty())
      data.storedPath = stem.storedPath.trim();
    else
      data.storedPath.clear();

    song.stems.push_back(std::move(data));
  }

  sortStemsForStripOrder(song.stems);

  return song;
}

void SongImportWizardComponent::submitSong() {
  if (isSubmitting || currentStep != Step::Review)
    return;

  // Sources may disappear between the file picker and the final review.
  // Re-check the complete draft before choosing a destination or writing it.
  if (!validateStepMetadata() || !validateStepMapping() || !validateStepImport())
    return;

  if (!onSubmit) {
    SessionAuthLogger::write(
        "[SongImportUI] submitSong ignored: missing onSubmit callback");
    closeDialog();
    return;
  }

  // Destination prompts may pump the message loop too. Guard the whole
  // transaction, not only the final persistence callback.
  setSubmitting(true);
  bool keepSubmissionActive = false;
  const juce::ScopeGuard resetSubmitting{
      [safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this),
       &keepSubmissionActive] {
        if (!keepSubmissionActive && safeThis != nullptr)
          safeThis->setSubmitting(false);
      }};

  SessionAuthLogger::write(
      "[SongImportUI] submitSong begin mode=" +
      juce::String(mode == Mode::Edit ? "edit" : "import") + " title=\"" +
      songTitleEditor.getText().trim() + "\" stems=" +
      juce::String(static_cast<int>(stems.size())));

  auto songData = buildSongData();
  if (mode == Mode::Import && resolveProjectDestination) {
    juce::File destination;
    juce::String destinationError;
    if (!resolveProjectDestination(songData, destination, destinationError)) {
      if (destinationError.isNotEmpty()) {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            L10n::tr("Ubicación del proyecto", "Project location"),
            destinationError);
      }
      return;
    }
    songData.projectPath = destination.getFullPathName();
  }

  if (mode == Mode::Import && resolveProjectDestination) {
    keepSubmissionActive = submitSongInBackground(std::move(songData));
    return;
  }
  if (mode == Mode::Edit && isProjectBackedSong(initialSong)) {
    keepSubmissionActive = submitSongInBackground(std::move(songData));
    return;
  }
  SongLoadProgressOverlay overlay;
  addAndMakeVisible(overlay);
  overlay.refreshThemeColours();
  overlay.setTitleText(mode == Mode::Edit
                           ? L10n::tr(u8"Actualizando canción", "Updating song")
                           : L10n::tr(u8"Creando canción", "Creating song"));
  overlay.setAlwaysOnTop(true);
  overlay.setBounds(getLocalBounds());
  overlay.setVisible(true);
  overlay.toFront(true);

  auto pumpUi = [] {
      if (auto *mm = juce::MessageManager::getInstanceWithoutCreating())
#if ! JUCE_ANDROID
#if ! JUCE_ANDROID
      mm->runDispatchLoopUntil(14);
#else
      juce::ignoreUnused(mm);
#endif
#else
      juce::ignoreUnused(mm);
#endif
  };

  int lastLoggedPct = -5;
  juce::String lastLoggedPhase;
  const SongImportProgressFn report =
      [&overlay, pumpUi, &lastLoggedPct, &lastLoggedPhase](
          const double progress01, const juce::String &phase) {
        overlay.setProgress(juce::jlimit(0.0, 1.0, progress01));
        overlay.setPhaseText(phase);
        const int pct =
            static_cast<int>(std::round(100.0 * juce::jlimit(0.0, 1.0, progress01)));
        if (phase != lastLoggedPhase || pct >= (lastLoggedPct + 5)) {
          lastLoggedPhase = phase;
          lastLoggedPct = pct;
          SessionAuthLogger::write(
              "[SongImportUI] progress " + juce::String(pct) + "% phase=\"" +
              phase + "\"");
        }
        pumpUi();
      };

  juce::String errorMessage;
  const bool ok = onSubmit(std::move(songData), errorMessage, report);

  SessionAuthLogger::write(
      "[SongImportUI] submitSong finished ok=" + juce::String(ok ? "true" : "false") +
      (errorMessage.isNotEmpty() ? " error=\"" + errorMessage + "\"" : ""));

  removeChildComponent(&overlay);

  if (!ok) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        mode == Mode::Edit
            ? L10n::tr("Error al actualizar", "Update error")
            : L10n::tr(u8"Error al crear", "Create error"),
        errorMessage.isNotEmpty()
            ? errorMessage
            : L10n::tr(u8"No se pudo guardar la canción seleccionada.",
                       "Couldn't save the selected song."));
    return;
  }

  setSubmitting(false);
  closeDialog();
}

bool SongImportWizardComponent::submitSongInBackground(ImportedSongData song) {
  submissionOverlay = std::make_unique<SongLoadProgressOverlay>();
  addAndMakeVisible(*submissionOverlay);
  submissionOverlay->refreshThemeColours();
  submissionOverlay->setTitleText(mode == Mode::Edit
                                      ? L10n::tr(u8"Actualizando canción", "Updating song")
                                      : L10n::tr(u8"Creando canción", "Creating song"));
  submissionOverlay->setPhaseText(L10n::tr("Preparando proyecto...", "Preparing project..."));
  submissionOverlay->setProgress(0.0);
  submissionOverlay->setAlwaysOnTop(true);
  submissionOverlay->setBounds(getLocalBounds());
  submissionOverlay->toFront(true);

  // Neither the wizard nor other components are accessed on this worker.
  // The persistence callback owns its inputs; all UI work is posted back.
  auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
  const bool started = juce::Thread::launch(
      [save = onSubmit, song = std::move(song), safeThis,
       staging = mobileImportStaging]() mutable {
        // Retain staged sources if the wizard is destroyed externally while
        // the project still packages them on this worker.
        juce::ignoreUnused(staging);
        juce::uint32 lastReportTime = 0;
        juce::String lastPhase;
        const SongImportProgressFn report =
            [safeThis, &lastReportTime, &lastPhase](double progress,
                                                  const juce::String &phase) {
              const auto now = juce::Time::getMillisecondCounter();
              if (phase == lastPhase && progress < 1.0 &&
                  now - lastReportTime < 33)
                return;
              lastReportTime = now;
              lastPhase = phase;
              const auto clampedProgress = juce::jlimit(0.0, 1.0, progress);
              juce::MessageManager::callAsync([safeThis, clampedProgress, phase] {
                if (safeThis == nullptr || safeThis->submissionOverlay == nullptr)
                  return;
                safeThis->submissionOverlay->setProgress(clampedProgress);
                safeThis->submissionOverlay->setPhaseText(phase);
              });
            };

        juce::String error;
        bool success = false;
        try {
          success = save(std::move(song), error, report);
        } catch (const std::exception &exception) {
          error = juce::String::fromUTF8(exception.what());
        } catch (...) {
          error = L10n::tr(u8"No se pudo guardar la canción.",
                            "Couldn't save the song.");
        }
        SessionAuthLogger::write(
            "[SongImportUI] background submit finished ok=" +
            juce::String(success ? "true" : "false") +
            (error.isNotEmpty() ? " error=\"" + error + "\"" : juce::String()));
        juce::MessageManager::callAsync([safeThis, success, error] {
          if (safeThis == nullptr)
            return;
          safeThis->submissionOverlay.reset();
          safeThis->setSubmitting(false);
          if (success) {
            safeThis->closeDialog();
            return;
          }
          juce::NativeMessageBox::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon,
              safeThis->mode == Mode::Edit
                  ? L10n::tr("Error al actualizar", "Update error")
                  : L10n::tr(u8"Error al crear", "Create error"),
              error.isNotEmpty()
                  ? error
                  : L10n::tr(u8"No se pudo guardar la canción seleccionada.",
                             "Couldn't save the selected song."));
        });
      });
  if (!started) {
    submissionOverlay.reset();
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr(u8"Error al crear", "Create error"),
        L10n::tr(u8"No se pudo iniciar el guardado. Inténtalo de nuevo.",
                 "Couldn't start saving. Please try again."));
  }
  return started;
}

void SongImportWizardComponent::setSubmitting(const bool submitting) {
  isSubmitting = submitting;
  for (auto *button : {&submitButton, &backButton, &nextButton, &cancelButton,
                       &saveMetadataButton})
    button->setEnabled(!submitting);
  mobileCloseButton.setEnabled(!submitting);
  keyboardDoneButton.setEnabled(!submitting);
}

void SongImportWizardComponent::changeStep(const Step step) {
  if (isSubmitting)
    return;
  if (currentStep != step)
    finishTextEditing();
  if (currentStep == Step::Mapping && step != Step::Mapping)
    syncMappingsToDrafts();
  currentStep = static_cast<Step>(
      juce::jlimit(static_cast<int>(Step::Metadata), static_cast<int>(Step::Review),
                   static_cast<int>(step)));

  subtitleLabel.setText(stepSubtitle(currentStep), juce::dontSendNotification);
  if (currentStep == Step::Review)
    updateSummaryText();

  backButton.setVisible(currentStep != Step::Metadata);
  nextButton.setVisible(currentStep != Step::Review);
  submitButton.setVisible(currentStep == Step::Review);
  saveMetadataButton.setVisible(mode == Mode::Edit &&
                                currentStep == Step::Metadata &&
                                static_cast<bool>(onMetadataOnlySubmit));
  resized();
  repaint();
}

juce::String SongImportWizardComponent::stepSubtitle(const Step step) const {
  switch (step) {
  case Step::Metadata:
    return L10n::tr(u8"Paso 1/4 - Metadatos de la canción",
                    "Step 1/4 - Song metadata");
  case Step::Stems:
    return L10n::tr(u8"Paso 2/4 - Importación de stems",
                    "Step 2/4 - Import stems");
  case Step::Mapping:
    return L10n::tr("Paso 3/4 - Mapeo de stems y categorias",
                    "Step 3/4 - Map stems and categories");
  case Step::Review:
  default:
    return mode == Mode::Edit
               ? L10n::tr(u8"Paso 4/4 - Confirmar edición",
                          "Step 4/4 - Confirm update")
               : L10n::tr(u8"Paso 4/4 - Confirmar importación",
                          "Step 4/4 - Confirm import");
  }
}

void SongImportWizardComponent::closeDialog() {
  if (isSubmitting)
    return;
  finishTextEditing();
  if (onDismissRequested) {
    onDismissRequested();
    return;
  }
  if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
    dw->exitModalState(0);
    dw->setVisible(false);
  }
}

void SongImportWizardComponent::applyEditorTheme(
    juce::TextEditor &editor, const juce::String &placeholder) {
  applyTextEditorTheme(editor, UITheme::cardBgHover,
                       UITheme::cardBorder.withAlpha(0.7f));
  // Scrolling the form must not dismiss the keyboard midway through a drag.
  // Direct field-to-field focus preserves the native keyboard session.
  editor.setClicksOutsideDismissVirtualKeyboard(false);
  editor.getProperties().set("playworshipContinuousTextInput", true);
  editor.setJustification(juce::Justification::centredLeft);
  editor.setTextToShowWhenEmpty(placeholder,
                                UITheme::textSecondary.withAlpha(0.6f));
}

void SongImportWizardComponent::styleSecondaryButton(juce::TextButton &button) {
  button.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  button.setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
  button.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  button.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
}

void SongImportWizardComponent::openArtworkChooser() {
  if (isSubmitting)
    return;
  auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
  artworkChooser = std::make_unique<juce::FileChooser>(
      L10n::tr("Selecciona imagen", "Select image"),
      juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
      "*.png;*.jpg;*.jpeg;*.webp");

  const auto flags = juce::FileBrowserComponent::openMode |
                     juce::FileBrowserComponent::canSelectFiles;
  artworkChooser->launchAsync(flags, [safeThis](const juce::FileChooser &chooser) {
    if (safeThis == nullptr)
      return;
#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
    safeThis->stageSelectedMobileFiles(chooser.getURLResults(), false, true);
#else
    const auto file = chooser.getResult();
    if (!file.existsAsFile()) {
      safeThis->artworkChooser.reset();
      return;
    }

    const auto image = juce::ImageFileFormat::loadFrom(file);
    if (image.isValid()) {
      safeThis->selectedArtworkUrl = "manual:" + file.getFullPathName();
      safeThis->secuenciasArtworkPreview.setImage(
          image, juce::RectanglePlacement::centred);
      safeThis->secuenciasArtworkCaption.setText(
          L10n::tr("Imagen manual", "Manual image"),
          juce::dontSendNotification);
    }
#endif
    safeThis->artworkChooser.reset();
  });
}


juce::Image SongImportWizardComponent::getCachedArtwork(const juce::String &url) const {
  if (url.trim().isEmpty())
    return {};

  const juce::ScopedLock sl(artworkLock);
  auto it = artworkCache.find(url.toStdString());
  if (it == artworkCache.end())
    return {};
  return it->second;
}

void SongImportWizardComponent::requestArtworkAsync(const juce::String &url) {
  juce::ignoreUnused(url); // Remote artwork extraction has been retired.
}

void SongImportWizardComponent::hideSecuenciasSuggestions(const bool resetStatus) {
  const bool hadSuggestions = !secuenciasResults.empty() || secuenciasList.isVisible();
  ++secuenciasSearchToken;
  ++latestSearchRequestId;
  secuenciasResults.clear();
  secuenciasSelectedRow = -1;
  secuenciasList.updateContent();
  secuenciasList.selectRow(-1);
  secuenciasList.setVisible(false);

  if (resetStatus && hadSuggestions && !selectedSongDetail.has_value()) {
    secuenciasStatusLabel.setText(
        L10n::tr("Metadata manual. Puedes continuar sin seleccionar sugerencia.",
                 "Manual metadata. You can continue without selecting a suggestion."),
        juce::dontSendNotification);
  }

  resized();
  repaint();
}

void SongImportWizardComponent::handleSecuenciasResult(
    const std::vector<SongSearchResult> &results, const SecuenciasError &err) {
  selectedSongDetail.reset();
  secuenciasSelectedRow = -1;
  secuenciasDetailLabel.setText({}, juce::dontSendNotification);

  if (err.type != SecuenciasError::Type::None) {
    secuenciasResults.clear();
    secuenciasList.updateContent();
    secuenciasList.selectRow(-1);
    secuenciasList.setVisible(false);
    secuenciasStatusLabel.setText(
        L10n::tr("Sin resultados en línea. Puedes ingresar metadata manualmente.",
                 "No online results. You can continue with manual metadata."),
        juce::dontSendNotification);
    return;
  }

  secuenciasResults = results;
  secuenciasList.updateContent();
  secuenciasList.setVisible(!secuenciasResults.empty());
  resized();

  for (size_t i = 0; i < secuenciasResults.size() && i < 10; ++i)
    requestArtworkAsync(secuenciasResults[i].artworkUrl);

  if (secuenciasResults.empty()) {
    secuenciasStatusLabel.setText(
        L10n::tr("No encontramos coincidencias. Continúa con ingreso manual.",
                 "No matches found. Continue with manual entry."),
        juce::dontSendNotification);
    return;
  }

  secuenciasStatusLabel.setText(
      juce::String(static_cast<int>(secuenciasResults.size())) + " " +
          L10n::tr("coincidencias", "matches") +
          L10n::tr(". Selecciona una canción.", ". Select a song."),
      juce::dontSendNotification);
  secuenciasList.selectRow(-1);
}

void SongImportWizardComponent::fetchSecuenciasDetailForSelected() {
  hideSecuenciasSuggestions(false);
}

void SongImportWizardComponent::applySecuenciasMetadata(const SongSearchResult &result,
                                                         const SongDetail *detail) {
  juce::ignoreUnused(result, detail);
}
