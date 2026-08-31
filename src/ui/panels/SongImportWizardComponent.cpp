#include "SongImportWizardComponent.h"

#include "../../data/AppSettings.h"
#include "../../data/AssetBootstrapManager.h"
#include "../../data/Localization.h"
#include "../../data/SessionAuthLogger.h"
#include "../../data/SongLibraryStore.h"
#include "../UiCommon.h"
#include "../components/SongLoadProgressOverlay.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <set>
#include <thread>

class SongImportWizardComponent::MappingRow final : public juce::Component {
public:
  explicit MappingRow(const StemDraft &stem) {
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

  void resized() override {
    auto r = getLocalBounds().reduced(4, 3);
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
    displayNameEditor.setColour(juce::TextEditor::backgroundColourId,
                                UITheme::cardBg);
    displayNameEditor.setColour(juce::TextEditor::textColourId,
                                UITheme::textPrimary);
    displayNameEditor.setColour(juce::TextEditor::outlineColourId,
                                UITheme::cardBorder.withAlpha(0.7f));
    categoryBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBg);
    categoryBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
    categoryBox.setColour(juce::ComboBox::outlineColourId,
                          UITheme::cardBorder.withAlpha(0.7f));
    categoryBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
    includeToggle.setColour(juce::ToggleButton::textColourId,
                            UITheme::textPrimary);
  }

private:
  juce::Label fileLabel;
  juce::TextEditor displayNameEditor;
  juce::ComboBox categoryBox;
  juce::ToggleButton includeToggle;
};

class SongImportWizardComponent::PrimeSongListModel final
    : public juce::ListBoxModel {
public:
  explicit PrimeSongListModel(SongImportWizardComponent &ownerIn)
      : owner(ownerIn) {}

  int getNumRows() override {
    return static_cast<int>(owner.primeSongs.size());
  }

  void selectedRowsChanged(int row) override {
    owner.usePrimeSongButton.setEnabled(
        juce::isPositiveAndBelow(row, static_cast<int>(owner.primeSongs.size())));
  }

  void paintListBoxItem(int row, juce::Graphics &g, int width, int height,
                        bool rowIsSelected) override {
    if (!juce::isPositiveAndBelow(row, static_cast<int>(owner.primeSongs.size())))
      return;

    const auto &song = owner.primeSongs[static_cast<size_t>(row)];
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(6, 4);

    g.setColour(rowIsSelected ? UITheme::accent.withAlpha(0.28f)
                              : UITheme::cardBgHover.withAlpha(0.92f));
    g.fillRoundedRectangle(area.toFloat(), 4.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.7f));
    g.drawRoundedRectangle(area.toFloat(), 4.0f, 1.0f);

    auto textArea = area.reduced(8, 5);
    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(12.5f, AppFontWeight::Medium));
    g.drawFittedText(song.title.isNotEmpty()
                         ? song.title
                         : L10n::tr(u8"Canción sin título", "Untitled Song"),
                     textArea.removeFromTop(16),
                     juce::Justification::centredLeft, 1);

    g.setColour(UITheme::textSecondary);
    g.setFont(makeAppFont(10.0f, AppFontWeight::Regular));
    juce::String meta;
    if (song.artist.isNotEmpty())
      meta << song.artist << u8" • ";
    if (song.bpm > 0)
      meta << juce::String(song.bpm) << " BPM" << u8" • ";
    meta << juce::String(static_cast<int>(song.tracks.size())) << " "
         << L10n::tr("stems", "stems");
    g.drawFittedText(meta, textArea, juce::Justification::centredLeft, 1);
  }

private:
  SongImportWizardComponent &owner;
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
    const bool primeImportEnabledIn, const Mode modeIn,
    ImportedSongData initialSongIn,
    std::function<bool(const ImportedSongData &, juce::File &, juce::String &)>
        projectDestinationResolver,
    std::function<bool(ImportedSongData, juce::String &)> onSaveMetadataOnly)
    : onSubmit(std::move(onSubmitSong)),
      resolveProjectDestination(std::move(projectDestinationResolver)),
      onMetadataOnlySubmit(std::move(onSaveMetadataOnly)),
      mode(modeIn), initialSong(std::move(initialSongIn)),
      primeImportEnabled(primeImportEnabledIn) {
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

  primeSectionLabel.setText(
      L10n::tr("Importar desde disco local", "Import from local disk"),
      juce::dontSendNotification);
  primeSectionLabel.setFont(makeAppFont(12.0f, AppFontWeight::SemiBold));
  primeSectionLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(primeSectionLabel);

  primeStatusLabel.setText(L10n::tr("Listo", "Ready"),
                           juce::dontSendNotification);
  primeStatusLabel.setFont(makeAppFont(10.0f, AppFontWeight::Regular));
  primeStatusLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(primeStatusLabel);

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

  bpmLabel.setText("BPM", juce::dontSendNotification);
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

  songTitleEditor.onTextChange = [this] {
    if (suppressTitleAutocomplete || currentStep != Step::Metadata)
      return;

    secuenciasSearchToken++;
    const auto localToken = secuenciasSearchToken;
    selectedSongDetail.reset();
    secuenciasDetailLabel.setText({}, juce::dontSendNotification);

    const auto text = songTitleEditor.getText().trim();
    if (text.length() < 2) {
      secuenciasResults.clear();
      secuenciasStatusLabel.setText(
          L10n::tr("Escribe al menos 2 caracteres", "Type at least 2 characters"),
          juce::dontSendNotification);
      secuenciasList.updateContent();
      secuenciasList.selectRow(-1);
      secuenciasList.setVisible(false);
      return;
    }

    secuenciasStatusLabel.setText(L10n::tr("Buscando...", "Searching..."),
                                  juce::dontSendNotification);
    secuenciasList.setVisible(true);
    auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
    juce::Timer::callAfterDelay(500, [safeThis, localToken] {
      if (safeThis == nullptr)
        return;
      if (safeThis->secuenciasSearchToken != localToken)
        return;
      safeThis->triggerSecuenciasSearch();
    });
  };
  songTitleEditor.onReturnKey = [this] { hideSecuenciasSuggestions(true); };
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

  clearSongSearchButton.setButtonText(L10n::tr("Limpiar", "Clear"));
  clearSongSearchButton.onClick = [this] {
    ++secuenciasSearchToken;
    setMetadataReadOnlyFromLocal(false);
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
  setMetadataReadOnlyFromLocal(false);

  addFilesButton.setButtonText(L10n::tr(u8"Agregar stems...", u8"Add stems..."));
  addFilesButton.onClick = [this] { openStemChooser(false); };
  addAndMakeVisible(addFilesButton);

  replaceFilesButton.setButtonText(
      L10n::tr(u8"Reemplazar stems...", u8"Replace stems..."));
  replaceFilesButton.onClick = [this] { openStemChooser(true); };
  addAndMakeVisible(replaceFilesButton);

  refreshPrimeButton.setButtonText(
      L10n::tr("Actualizar biblioteca local", "Refresh local library"));
  refreshPrimeButton.onClick = [this] { reloadPrimeSongs(); };
  addAndMakeVisible(refreshPrimeButton);

  usePrimeSongButton.setButtonText(L10n::tr("Usar seleccionada", "Use Selected"));
  usePrimeSongButton.onClick = [this] { useSelectedPrimeSong(); };
  usePrimeSongButton.setEnabled(false);
  addAndMakeVisible(usePrimeSongButton);

  primeSongListModel = std::make_unique<PrimeSongListModel>(*this);
  primeSongsList.setRowHeight(42);
  primeSongsList.setModel(primeSongListModel.get());
  primeSongsList.setColour(juce::ListBox::backgroundColourId, UITheme::cardBg);
  primeSongsList.setColour(juce::ListBox::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.7f));
  addAndMakeVisible(primeSongsList);

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

  summaryEditor.setReadOnly(true);
  summaryEditor.setMultiLine(true);
  addAndMakeVisible(summaryEditor);

  cancelButton.setButtonText(L10n::tr("Cancelar", "Cancel"));
  cancelButton.onClick = [this] { closeDialog(); };
  backButton.setButtonText(L10n::tr(u8"Atrás", "Back"));
  backButton.onClick = [this] {
    if (currentStep == Step::Metadata)
      return;
    changeStep(static_cast<Step>(static_cast<int>(currentStep) - 1));
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

  for (auto *btn : {&cancelButton, &backButton, &nextButton, &submitButton,
                    &saveMetadataButton})
    addAndMakeVisible(*btn);

  refreshThemeColours();

  if (mode == Mode::Edit && initialSong.id.isNotEmpty()) {
    preloadFromExistingSong();
  } else if (primeImportEnabled && mode == Mode::Import) {
    reloadPrimeSongs();
  }

  changeStep(Step::Metadata);
}

SongImportWizardComponent::~SongImportWizardComponent() = default;

void SongImportWizardComponent::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(b, 8.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.8f));
  g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.0f);
}

void SongImportWizardComponent::paintOverChildren(juce::Graphics &g) {
  if (currentStep != Step::Metadata || artworkOverlayButtonBounds.isEmpty())
    return;

  auto area = artworkOverlayButtonBounds.toFloat();
  auto bg = UITheme::accent.withAlpha(artworkOverlayHovered ? 0.95f : 0.82f);
  g.setColour(bg);
  g.fillEllipse(area);
  g.setColour(UITheme::cardBorder.withAlpha(0.8f));
  g.drawEllipse(area, 1.0f);
  drawSvgIcon(g, area.reduced(6.0f), SvgIconName::ImageEdit, UITheme::onAccent);
}

void SongImportWizardComponent::resized() {
  auto area = getLocalBounds().reduced(16);
  auto header = area.removeFromTop(74);
  titleLabel.setBounds(header.removeFromTop(42));
  subtitleLabel.setBounds(header);

  auto footer = area.removeFromBottom(46);
  auto content = area;

  cancelButton.setBounds(footer.removeFromLeft(96));

  auto rightEdge = footer.getRight();
  const int buttonH = footer.getHeight();
  const int y = footer.getY();
  const int gap = 8;
  auto placeRight = [&](juce::Component &button, const int width) {
    button.setBounds(rightEdge - width, y, width, buttonH);
    rightEdge -= (width + gap);
  };

  if (submitButton.isVisible())
    placeRight(submitButton, 156);
  else
    placeRight(nextButton, 96);

  if (backButton.isVisible())
    placeRight(backButton, 96);
  if (saveMetadataButton.isVisible())
    placeRight(saveMetadataButton, 112);

  layoutStepImport(content);
  layoutStepMetadata(content);
  layoutStepMapping(content);
  layoutStepReview(content);
}

void SongImportWizardComponent::mouseMove(const juce::MouseEvent &event) {
  const bool nextHovered =
      currentStep == Step::Metadata &&
      artworkOverlayButtonBounds.contains(event.getPosition());
  if (nextHovered != artworkOverlayHovered) {
    artworkOverlayHovered = nextHovered;
    repaint(artworkOverlayButtonBounds.expanded(2));
  }
}

void SongImportWizardComponent::mouseExit(const juce::MouseEvent &) {
  if (!artworkOverlayHovered)
    return;
  artworkOverlayHovered = false;
  repaint(artworkOverlayButtonBounds.expanded(2));
}

void SongImportWizardComponent::mouseUp(const juce::MouseEvent &event) {
  if (currentStep != Step::Metadata)
    return;
  if (!artworkOverlayButtonBounds.contains(event.getPosition()))
    return;
  openArtworkChooser();
}

void SongImportWizardComponent::refreshThemeColours() {
  titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  subtitleLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  for (auto *label :
       {&stemsSectionLabel, &primeSectionLabel, &infoSectionLabel,
        &songTitleLabel, &artistLabel, &bpmLabel, &keyLabel,
        &timeSignatureLabel, &mapFileHeader, &mapNameHeader, &mapCategoryHeader,
        &mapLoadHeader, &secuenciasStatusLabel,
        &secuenciasDetailLabel, &secuenciasArtworkCaption}) {
    label->setColour(juce::Label::textColourId, UITheme::textSecondary);
  }
  primeStatusLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);

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

  summaryEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::cardBgHover);
  applyTextEditorTheme(summaryEditor, UITheme::cardBgHover,
                       UITheme::cardBorder.withAlpha(0.7f));

  configureCombo(keyBox);
  configureCombo(timeSignatureBox);

  styleSecondaryButton(addFilesButton);
  styleSecondaryButton(replaceFilesButton);
  styleSecondaryButton(refreshPrimeButton);
  stylePrimaryButton(usePrimeSongButton);
  styleSecondaryButton(clearSongSearchButton);
  styleSecondaryButton(cancelButton);
  styleSecondaryButton(backButton);
  stylePrimaryButton(saveMetadataButton);
  stylePrimaryButton(nextButton);
  stylePrimaryButton(submitButton);

  for (auto &row : mappingRows)
    row->refreshThemeColours();

  repaint();
}

void SongImportWizardComponent::configureTextEditor(
    juce::TextEditor &editor, const juce::String &placeholder) {
  applyEditorTheme(editor, placeholder);
}

void SongImportWizardComponent::configureCombo(juce::ComboBox &combo) {
  applyComboBoxTheme(combo, UITheme::cardBgHover,
                     UITheme::cardBorder.withAlpha(0.7f));
}

void SongImportWizardComponent::layoutStepImport(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == Step::Stems;
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
  const bool showPrime = visible && primeImportEnabled && mode == Mode::Import;
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
                  static_cast<juce::Component *>(&secuenciasStatusLabel),
                  static_cast<juce::Component *>(&secuenciasDetailLabel),
                  static_cast<juce::Component *>(&secuenciasArtworkPreview),
                  static_cast<juce::Component *>(&secuenciasArtworkCaption),
                  static_cast<juce::Component *>(&clearSongSearchButton),
                  static_cast<juce::Component *>(&refreshPrimeButton),
                  static_cast<juce::Component *>(&usePrimeSongButton),
                  static_cast<juce::Component *>(&primeSongsList),
                  static_cast<juce::Component *>(&primeSectionLabel),
                  static_cast<juce::Component *>(&primeStatusLabel)}) {
    const bool isPrimeControl =
        c == static_cast<juce::Component *>(&refreshPrimeButton) ||
        c == static_cast<juce::Component *>(&usePrimeSongButton) ||
        c == static_cast<juce::Component *>(&primeSongsList) ||
        c == static_cast<juce::Component *>(&primeSectionLabel) ||
        c == static_cast<juce::Component *>(&primeStatusLabel);
    if (isPrimeControl) {
      c->setVisible(showPrime);
      continue;
    }
    c->setVisible(visible);
  }
  secuenciasList.setVisible(visible && !secuenciasResults.empty());
  if (!visible) {
    artworkOverlayButtonBounds = {};
    return;
  }

  const int sectionLabelH = 18;
  const int fieldLabelH = 16;
  const int fieldH = touchOptimisedControlHeight();

  if (showPrime) {
    auto primeHeader = bounds.removeFromTop(sectionLabelH);
    primeSectionLabel.setBounds(primeHeader.removeFromLeft(190));
    auto primeButtons = primeHeader.removeFromRight(258);
    refreshPrimeButton.setBounds(primeButtons.removeFromLeft(122));
    primeButtons.removeFromLeft(8);
    usePrimeSongButton.setBounds(primeButtons.removeFromLeft(128));

    bounds.removeFromTop(4);
    primeSongsList.setBounds(bounds.removeFromTop(118));
    bounds.removeFromTop(2);
    primeStatusLabel.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(10);
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
  auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
  stemChooser = std::make_unique<juce::FileChooser>(
      L10n::tr("Selecciona stems", "Select stems"),
      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
      "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");

  const auto chooserFlags = juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles |
                            juce::FileBrowserComponent::canSelectMultipleItems;

  stemChooser->launchAsync(chooserFlags,
                           [safeThis, replaceCurrent](const juce::FileChooser &chooser) {
                             if (safeThis == nullptr)
                               return;
                             safeThis->handleSelectedStemFiles(chooser.getResults(),
                                                               replaceCurrent);
                             safeThis->stemChooser.reset();
                           });
}

void SongImportWizardComponent::handleSelectedStemFiles(
    const juce::Array<juce::File> &files, const bool replaceCurrent) {
  if (replaceCurrent) {
    stems.clear();
  } else {
    // Si solo hay placeholders remotos (sin archivo real), al agregar stems
    // locales evitamos mezclar referencias de otra carga.
    const bool hasOnlyPlaceholderStems =
        !stems.empty() &&
        std::all_of(stems.begin(), stems.end(), [](const StemDraft &stem) {
          return stem.sourcePath.trim().isEmpty() &&
                 stem.storedPath.trim().isEmpty() && !stem.file.existsAsFile();
        });
    if (hasOnlyPlaceholderStems)
      stems.clear();
  }
  if (files.isEmpty()) {
    updateSelectedFilesText();
    rebuildMappingRows();
    return;
  }

  for (const auto &file : files) {
    if (!file.existsAsFile())
      continue;
    const auto normalizedPath = file.getFullPathName();
    const bool alreadyThere =
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

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(file))) {
      draft.sampleRate = static_cast<int>(reader->sampleRate);
      draft.channels = static_cast<int>(reader->numChannels);
      if (reader->sampleRate > 0.0) {
        draft.durationSec =
            static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
      }
    }

    stems.push_back(std::move(draft));
  }

  if (songTitleEditor.getText().trim().isEmpty() && !files.isEmpty()) {
    songTitleEditor.setText(files[0].getParentDirectory().getFileName(),
                            juce::dontSendNotification);
  }

  updateSelectedFilesText();
  rebuildMappingRows();
}

void SongImportWizardComponent::preloadFromExistingSong() {
  setMetadataReadOnlyFromLocal(false);
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
}

void SongImportWizardComponent::removeStemAtIndex(const int rowIndex) {
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(stems.size())))
    return;

  stems.erase(stems.begin() + rowIndex);
  updateSelectedFilesText();
  rebuildMappingRows();
}

void SongImportWizardComponent::rebuildMappingRows() {
  mappingRows.clear();
  mapContent.removeAllChildren();

  for (const auto &stem : stems) {
    auto row = std::make_unique<MappingRow>(stem);
    mapContent.addAndMakeVisible(*row);
    mappingRows.push_back(std::move(row));
  }

  layoutMappingRows();
}

void SongImportWizardComponent::layoutMappingRows() {
  const int rowH = 42;
  const int gap = 5;
  const int width = juce::jmax(1, mapViewport.getWidth() - 10);
  int y = 6;
  for (auto &row : mappingRows) {
    row->setBounds(4, y, width, rowH);
    y += rowH + gap;
  }
  mapContent.setSize(width + 8, juce::jmax(mapViewport.getHeight(), y + 4));
}

void SongImportWizardComponent::reloadPrimeSongs() {
  if (!primeImportEnabled || mode != Mode::Import)
    return;

  primeSongs.clear();
  usePrimeSongButton.setEnabled(false);
  primeSongsList.updateContent();
  primeSongsList.selectRow(-1);

  if (!PrimeLibrarySource::isSupportedOnCurrentPlatform()) {
    primeStatusLabel.setText(
        L10n::tr(u8"La importación de disco local solo esta disponible en macOS.",
                 "Local disk import is available only on macOS."),
        juce::dontSendNotification);
    return;
  }

  juce::String error;
  primeSongs = PrimeLibrarySource::loadSongs(error);
  primeSongsList.updateContent();

  if (primeSongs.empty()) {
    primeStatusLabel.setText(
        error.isNotEmpty() ? error
                           : L10n::tr(u8"No hay canciónes disponibles para importar.",
                                      "No songs available to import."),
        juce::dontSendNotification);
    return;
  }

  primeStatusLabel.setText(
      juce::String(static_cast<int>(primeSongs.size())) + " " +
          L10n::tr(u8"canciónes disponibles.", "songs available."),
      juce::dontSendNotification);
}

void SongImportWizardComponent::useSelectedPrimeSong() {
  const int row = primeSongsList.getSelectedRow();
  if (!juce::isPositiveAndBelow(row, static_cast<int>(primeSongs.size()))) {
    primeStatusLabel.setText(L10n::tr(u8"Primero selecciona una canción.",
                                      "Select a song first."),
                             juce::dontSendNotification);
    return;
  }

  applyPrimeSong(static_cast<size_t>(row));
}

void SongImportWizardComponent::applyPrimeSong(const size_t index) {
  if (mode != Mode::Import)
    return;
  if (index >= primeSongs.size())
    return;

  const auto &song = primeSongs[index];
  setMetadataReadOnlyFromLocal(true);
  // Mantener el campo de titulo editable para permitir busqueda de artwork
  // sin desbloquear el resto de metadata importada desde disco local.
  songTitleEditor.setReadOnly(false);
  suppressTitleAutocomplete = true;
  songTitleEditor.setText(song.title, juce::dontSendNotification);
  artistEditor.setText(song.artist, juce::dontSendNotification);
  bpmEditor.setText(song.bpm > 0 ? juce::String(song.bpm) : juce::String(),
                    juce::dontSendNotification);
  if (song.key.isNotEmpty())
    keyBox.setText(song.key, juce::dontSendNotification);
  if (song.timeSignature.isNotEmpty())
    timeSignatureBox.setText(song.timeSignature, juce::dontSendNotification);
  suppressTitleAutocomplete = false;
  selectedArtworkUrl.clear();
  secuenciasArtworkPreview.setImage(juce::Image(),
                                    juce::RectanglePlacement::centred);
  secuenciasArtworkCaption.setText({}, juce::dontSendNotification);

  secuenciasResults.clear();
  secuenciasList.updateContent();
  secuenciasList.selectRow(-1);
  secuenciasList.setVisible(false);
  secuenciasStatusLabel.setText(L10n::tr("Buscando...", "Searching..."),
                                juce::dontSendNotification);
  triggerSecuenciasSearch();

  // Elegir una canción de Prime sólo autocompleta metadata. Los stems deben
  // ser seleccionados explícitamente por la persona usuaria en el paso
  // siguiente; no prellenamos tracks ni referencias de la biblioteca externa.
  stems.clear();
  updateSelectedFilesText();
  rebuildMappingRows();
  primeStatusLabel.setText(
      L10n::tr("Cargada \"", "Loaded \"") + song.title +
          L10n::tr("\" desde disco local para importar.",
                   "\" from local disk for import."),
      juce::dontSendNotification);
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
                           : juce::String("Multitrack ALAC")));
  lines.add(L10n::tr("Stems a cargar: ", "Stems to load: ") +
            juce::String(static_cast<int>(included)) + " / " +
            juce::String(static_cast<int>(stems.size())));
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

  summaryEditor.setText(lines.joinIntoString("\n"),
                        juce::dontSendNotification);
}

void SongImportWizardComponent::goToNextStep() {
  switch (currentStep) {
  case Step::Metadata:
    if (!validateStepMetadata())
      return;
    hideSecuenciasSuggestions(false);
    changeStep(Step::Stems);
    return;
  case Step::Stems:
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
  if (mode != Mode::Edit || currentStep != Step::Metadata)
    return;
  if (!validateStepMetadata())
    return;
  if (!onMetadataOnlySubmit) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("No se pudo guardar", "Couldn't save"),
        L10n::tr("El guardado rápido no está disponible para esta canción.",
                 "Quick save is not available for this song."));
    return;
  }

  auto songData = buildSongData();
  songData.id = initialSong.id;
  juce::String errorMessage;
  const bool ok = onMetadataOnlySubmit(std::move(songData), errorMessage);
  if (!ok) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Error al guardar", "Save error"),
        errorMessage.isNotEmpty()
            ? errorMessage
            : L10n::tr(u8"No se pudo guardar la metadata de la canción.",
                       "Couldn't save the song metadata."));
    return;
  }

  closeDialog();
}

bool SongImportWizardComponent::validateStepImport() {
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
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Stems no disponibles", "Stems unavailable"),
        L10n::tr(u8"No se encontraron archivos de audio válidos para importar.",
                 "No valid audio files were found for import."));
    return false;
  }
  return true;
}

bool SongImportWizardComponent::validateStepMetadata() {
  if (songTitleEditor.getText().trim().isEmpty()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr(u8"Falta el título", "Missing song title"),
        L10n::tr(u8"Agrega un título de canción antes de continuar.",
                 "Add a song title before continuing."));
    return false;
  }
  return true;
}

bool SongImportWizardComponent::validateStepMapping() {
  syncMappingsToDrafts();
  if (stems.empty())
    return true;

  const bool hasAnyLoaded =
      std::any_of(stems.begin(), stems.end(),
                  [](const StemDraft &stem) { return stem.include; });
  if (!hasAnyLoaded) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("No hay stems seleccionados", "No stems selected"),
        L10n::tr("Activa al menos un stem para cargar o deja la lista vacía para metadata-only.",
                 "Enable at least one stem or leave the list empty for metadata-only."));
    return false;
  }
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
  song.fileType = stems.empty() ? "remote-reference" : "Multitrack ALAC";
  song.artworkSource = selectedArtworkUrl.trim();
  if (song.artworkSource.isEmpty())
    song.artworkPath.clear();
  if (song.createdMs <= 0)
    song.createdMs = juce::Time::getCurrentTime().toMilliseconds();

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
  if (currentStep != Step::Review)
    return;

  if (!onSubmit) {
    SessionAuthLogger::write(
        "[SongImportUI] submitSong ignored: missing onSubmit callback");
    closeDialog();
    return;
  }

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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            L10n::tr("Ubicación del proyecto", "Project location"),
            destinationError);
      }
      return;
    }
    songData.projectPath = destination.getFullPathName();
  }
  bool temporarilyDisableOptimization = false;

  const bool optimizationEnabled = AppSettings::getStemOptimizationEnabled();
  const bool hasLocalStemsToStore = std::any_of(
      songData.stems.begin(), songData.stems.end(),
      [](const ImportedStemData &stem) {
        return stem.include &&
               (stem.sourcePath.trim().isNotEmpty() ||
                stem.storedPath.trim().isNotEmpty());
      });

  const bool isPortableProjectCreation =
      mode == Mode::Import && static_cast<bool>(resolveProjectDestination);
  const bool projectNeedsFlacConversion =
      isPortableProjectCreation &&
      std::any_of(songData.stems.begin(), songData.stems.end(),
                  [](const ImportedStemData &stem) {
                    if (!stem.include)
                      return false;
                    const juce::File source(
                        stem.storedPath.trim().isNotEmpty()
                            ? stem.storedPath
                            : stem.sourcePath);
                    return source.existsAsFile() &&
                           !source.hasFileExtension(
                               SongLibraryStore::projectStorageStemExtension());
                  });
  const bool legacyOptimizationNeedsEncoder =
      !isPortableProjectCreation && optimizationEnabled &&
      hasLocalStemsToStore;

  if (projectNeedsFlacConversion || legacyOptimizationNeedsEncoder) {
    const auto availableEncoder = AssetBootstrapManager::getAvailableEncoderBinary();
    const bool encoderReadyLocally =
        availableEncoder.existsAsFile() && availableEncoder.getSize() > 0;

    if (!encoderReadyLocally) {
      juce::AlertWindow prompt(
          L10n::tr("Descargar encoder", "Download encoder"),
          projectNeedsFlacConversion
              ? L10n::tr(
                    u8"Se necesita el encoder para convertir los stems al formato FLAC portable.\n\n"
                    u8"¿Deseas descargarlo ahora?",
                    "The encoder is required to convert stems to the portable FLAC format.\n\n"
                    "Do you want to download it now?")
              : L10n::tr(
                    u8"No se encontró el encoder para optimizar almacenamiento.\n\n"
                    u8"¿Deseas descargarlo ahora?",
                    "Encoder for storage optimization wasn't found.\n\n"
                    "Do you want to download it now?"),
          juce::AlertWindow::QuestionIcon);
      prompt.addButton(L10n::tr("Descargar", "Download"), 1);
      if (!projectNeedsFlacConversion)
        prompt.addButton(
            L10n::tr("Continuar sin optimizar", "Continue without optimization"),
            2);
      prompt.addButton(L10n::tr("Cancelar", "Cancel"), 0);

      const int decision = prompt.runModalLoop();
      if (decision == 0)
        return;

      if (decision == 2) {
        temporarilyDisableOptimization = true;
      } else {
        SongLoadProgressOverlay downloadOverlay;
        addAndMakeVisible(downloadOverlay);
        downloadOverlay.refreshThemeColours();
        downloadOverlay.setTitleText(
            L10n::tr("Descargando encoder", "Downloading encoder"));
        downloadOverlay.setPhaseText(
            L10n::tr("Preparando descarga...", "Preparing download..."));
        downloadOverlay.setProgress(0.0);
        downloadOverlay.setAlwaysOnTop(true);
        downloadOverlay.setBounds(getLocalBounds());
        downloadOverlay.setVisible(true);
        downloadOverlay.toFront(true);

        auto pumpUi = [] {
          if (auto *mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(14);
        };

        juce::String bootstrapError;
        const bool encoderReady = AssetBootstrapManager::ensureEncoderReady(
            &bootstrapError,
            [&downloadOverlay, &pumpUi](double progress01, const juce::String &phase) {
              downloadOverlay.setProgress(juce::jlimit(0.0, 1.0, progress01));
              if (phase.isNotEmpty())
                downloadOverlay.setPhaseText(
                    L10n::tr("Descargando encoder...", "Downloading encoder..."));
              pumpUi();
            });

        removeChildComponent(&downloadOverlay);

        if (!encoderReady) {
          if (projectNeedsFlacConversion) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                L10n::tr("No se pudo preparar el encoder",
                         "Couldn't prepare encoder"),
                (bootstrapError.isNotEmpty()
                     ? bootstrapError + "\n\n"
                     : juce::String()) +
                    L10n::tr(
                        "El proyecto necesita stems FLAC para funcionar en Mac y Windows.",
                        "The project requires FLAC stems to work on Mac and Windows."));
            return;
          }
          juce::AlertWindow failedPrompt(
              L10n::tr("No se pudo descargar encoder",
                       "Couldn't download encoder"),
              (bootstrapError.isNotEmpty() ? bootstrapError + "\n\n" : juce::String()) +
                  L10n::tr("¿Deseas continuar sin optimización de almacenamiento?",
                           "Do you want to continue without storage optimization?"),
              juce::AlertWindow::WarningIcon);
          failedPrompt.addButton(
              L10n::tr("Continuar sin optimizar", "Continue without optimization"),
              1);
          failedPrompt.addButton(L10n::tr("Cancelar", "Cancel"), 0);

          if (failedPrompt.runModalLoop() != 1)
            return;
          temporarilyDisableOptimization = true;
        }
      }
    }
  }

  submitButton.setEnabled(false);
  backButton.setEnabled(false);
  nextButton.setEnabled(false);
  cancelButton.setEnabled(false);

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
      mm->runDispatchLoopUntil(14);
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

  struct OptimizationOverrideScope final {
    bool restore = false;
    bool previous = false;
    ~OptimizationOverrideScope() {
      if (restore)
        AppSettings::setStemOptimizationEnabled(previous);
    }
  };

  OptimizationOverrideScope optimizationScope;
  if (temporarilyDisableOptimization && optimizationEnabled) {
    optimizationScope.restore = true;
    optimizationScope.previous = optimizationEnabled;
    AppSettings::setStemOptimizationEnabled(false);
  }

  juce::String errorMessage;
  const bool ok = onSubmit(std::move(songData), errorMessage, report);

  SessionAuthLogger::write(
      "[SongImportUI] submitSong finished ok=" + juce::String(ok ? "true" : "false") +
      (errorMessage.isNotEmpty() ? " error=\"" + errorMessage + "\"" : ""));

  removeChildComponent(&overlay);

  submitButton.setEnabled(true);
  backButton.setEnabled(true);
  nextButton.setEnabled(true);
  cancelButton.setEnabled(true);

  if (!ok) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        mode == Mode::Edit
            ? L10n::tr("Error al actualizar", "Update error")
            : L10n::tr(u8"Error al crear", "Create error"),
        errorMessage.isNotEmpty()
            ? errorMessage
            : L10n::tr(u8"No se pudo guardar la canción seleccionada.",
                       "Couldn't save the selected song."));
    return;
  }

  closeDialog();
}

void SongImportWizardComponent::changeStep(const Step step) {
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
  if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
    dw->exitModalState(0);
    dw->setVisible(false);
  }
}

void SongImportWizardComponent::applyEditorTheme(
    juce::TextEditor &editor, const juce::String &placeholder) {
  editor.setFont(makeAppFont(14.0f, AppFontWeight::Regular));
  editor.setTextToShowWhenEmpty(placeholder,
                                UITheme::textSecondary.withAlpha(0.6f));
  editor.setColour(juce::TextEditor::backgroundColourId, UITheme::cardBgHover);
  editor.setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
  editor.setColour(juce::TextEditor::outlineColourId,
                   UITheme::cardBorder.withAlpha(0.7f));
}

void SongImportWizardComponent::stylePrimaryButton(juce::TextButton &button) {
  button.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  button.setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);
  button.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  button.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
}

void SongImportWizardComponent::styleSecondaryButton(juce::TextButton &button) {
  button.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  button.setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
  button.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  button.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
}

void SongImportWizardComponent::setMetadataReadOnlyFromLocal(
    const bool shouldBeReadOnly) {
  metadataReadOnlyFromLocal = shouldBeReadOnly;
  songTitleEditor.setReadOnly(shouldBeReadOnly);
  artistEditor.setReadOnly(shouldBeReadOnly);
  bpmEditor.setReadOnly(shouldBeReadOnly);
  keyBox.setEnabled(!shouldBeReadOnly);
  timeSignatureBox.setEnabled(!shouldBeReadOnly);
}

void SongImportWizardComponent::openArtworkChooser() {
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
  const auto trimmed = url.trim();
  if (trimmed.isEmpty())
    return;

  const auto key = trimmed.toStdString();
  {
    const juce::ScopedLock sl(artworkLock);
    if (artworkCache.find(key) != artworkCache.end())
      return;
    if (artworkInFlight.find(key) != artworkInFlight.end())
      return;
    artworkInFlight.insert(key);
  }

  auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
  std::thread([safeThis, trimmed, key] {
    juce::Image loaded;
    juce::URL artworkUrl(trimmed);
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders("User-Agent: Mozilla/5.0\r\n")
                       .withConnectionTimeoutMs(7000)
                       .withHttpRequestCmd("GET");
    if (auto stream = artworkUrl.createInputStream(options)) {
      juce::MemoryBlock bytes;
      stream->readIntoMemoryBlock(bytes);
      if (bytes.getSize() > 0)
        loaded = juce::ImageFileFormat::loadFrom(bytes.getData(), bytes.getSize());
    }

    juce::MessageManager::callAsync([safeThis, loaded, key] {
      if (safeThis == nullptr)
        return;
      {
        const juce::ScopedLock sl(safeThis->artworkLock);
        safeThis->artworkInFlight.erase(key);
        if (loaded.isValid())
          safeThis->artworkCache[key] = loaded;
      }
      safeThis->secuenciasList.repaint();
      if (safeThis->selectedArtworkUrl.toStdString() == key && loaded.isValid())
        safeThis->secuenciasArtworkPreview.setImage(
            loaded, juce::RectanglePlacement::centred);
    });
  }).detach();
}

void SongImportWizardComponent::triggerSecuenciasSearch() {
  const auto query = songTitleEditor.getText().trim();
  if (query.length() < 2)
    return;

  const int requestId = ++latestSearchRequestId;
  secuenciasClient.searchSongs(query, 1,
                               [safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this),
                                requestId](std::vector<SongSearchResult> results,
                                           SecuenciasError err) {
                                 if (safeThis == nullptr)
                                   return;
                                 if (requestId != safeThis->latestSearchRequestId)
                                   return;
                                 safeThis->handleSecuenciasResult(results, err);
                               });
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
  if (!juce::isPositiveAndBelow(secuenciasSelectedRow,
                                static_cast<int>(secuenciasResults.size()))) {
    selectedSongDetail.reset();
    return;
  }

  const auto selected =
      secuenciasResults[static_cast<size_t>(secuenciasSelectedRow)];

  selectedArtworkUrl = selected.artworkUrl;
  secuenciasArtworkCaption.setText(
      selected.artist.isNotEmpty() ? selected.artist : selected.title,
      juce::dontSendNotification);
  if (const auto image = getCachedArtwork(selected.artworkUrl); image.isValid())
    secuenciasArtworkPreview.setImage(image, juce::RectanglePlacement::centred);
  else {
    secuenciasArtworkPreview.setImage(juce::Image(), juce::RectanglePlacement::centred);
    requestArtworkAsync(selected.artworkUrl);
  }

  applySecuenciasMetadata(selected, nullptr);

  secuenciasDetailLabel.setText(L10n::tr("Cargando detalle técnico...",
                                         "Loading technical details..."),
                                juce::dontSendNotification);

  const int requestId = ++latestDetailRequestId;
  secuenciasClient.getSongDetails(
      selected.songId, 15,
      [safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this),
       requestId, selected](SongDetail detail, SecuenciasError err) {
        if (safeThis == nullptr)
          return;
        if (requestId != safeThis->latestDetailRequestId)
          return;
        if (err.type != SecuenciasError::Type::None) {
          safeThis->selectedSongDetail.reset();
          safeThis->secuenciasDetailLabel.setText(
              L10n::tr("Detalle no disponible. Puedes usar metadata básica.",
                       "Detail unavailable. You can still use basic metadata."),
              juce::dontSendNotification);
          return;
        }

        safeThis->selectedSongDetail = detail;
        juce::StringArray names;
        for (size_t i = 0; i < detail.tracks.size() && i < 8; ++i)
          names.add(detail.tracks[i].name);

        juce::String info =
            "BPM " + juce::String(static_cast<int>(std::round(detail.bpm))) +
            " • " + L10n::tr("Duración", "Duration") + " " +
            juce::String(static_cast<int>(std::round(detail.durationSeconds))) + "s";
        if (names.size() > 0)
          info << " • " << L10n::tr("Tracks", "Tracks") << ": "
               << names.joinIntoString(", ");

        safeThis->secuenciasDetailLabel.setText(info, juce::dontSendNotification);
        safeThis->applySecuenciasMetadata(selected, &detail);
      });
}

void SongImportWizardComponent::applySecuenciasMetadata(const SongSearchResult &result,
                                                         const SongDetail *detail) {
  if (metadataReadOnlyFromLocal) {
    secuenciasStatusLabel.setText(
        L10n::tr("Artwork aplicado. Metadata de canción local permanece en solo lectura.",
                 "Artwork applied. Local song metadata remains read-only."),
        juce::dontSendNotification);

    secuenciasResults.clear();
    secuenciasList.updateContent();
    secuenciasList.selectRow(-1);
    secuenciasList.setVisible(false);
    resized();
    return;
  }

  setMetadataReadOnlyFromLocal(false);
  suppressTitleAutocomplete = true;
  songTitleEditor.setText(result.title, juce::dontSendNotification);
  suppressTitleAutocomplete = false;

  if (artistEditor.getText().trim().isEmpty() || result.artist.isNotEmpty())
    artistEditor.setText(result.artist, juce::dontSendNotification);

  if (detail != nullptr && detail->bpm > 0.0)
    bpmEditor.setText(juce::String(static_cast<int>(std::round(detail->bpm))),
                      juce::dontSendNotification);

  if (result.originalKey.isNotEmpty())
    keyBox.setText(result.originalKey, juce::dontSendNotification);

  // Los resultados de Secuencias describen tracks remotos, pero no son stems
  // importables. No se crean placeholders: la lista queda vacía hasta que el
  // usuario elija archivos reales en el paso de stems.
  stems.erase(std::remove_if(stems.begin(), stems.end(), [](const StemDraft &stem) {
                return !stem.file.existsAsFile() && stem.sourcePath.trim().isEmpty() &&
                       stem.storedPath.trim().isEmpty();
              }), stems.end());
  updateSelectedFilesText();
  rebuildMappingRows();

  secuenciasStatusLabel.setText(
      L10n::tr("Metadata aplicada. Puedes editarla manualmente.",
               "Metadata applied. You can edit it manually."),
      juce::dontSendNotification);

  secuenciasResults.clear();
  secuenciasList.updateContent();
  secuenciasList.selectRow(-1);
  secuenciasList.setVisible(false);
  resized();
}
