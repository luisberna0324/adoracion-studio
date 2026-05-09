#include "ChannelGridPanel.h"

#include "../../audio/RoutingSuggestionEngine.h"
#include "../components/ChannelStripCard.h"
#include "../../PluginProcessor.h"
#include "../../data/Localization.h"
#include "../../data/SongLibraryStore.h"

#include <algorithm>

class ChannelGridPanel::StripSlot : public juce::Component {
public:
  StripSlot(juce::String groupName, ChannelStripLookAndFeel &lnf,
            StereoVUMeter::LevelProvider levelProvider)
      : strip(std::make_unique<ChannelStripCard>(std::move(groupName), lnf,
                                                 std::move(levelProvider),
                                                 false)),
        outputChannelLabel({}, "Canal") {
    addAndMakeVisible(*strip);
    outputChannelLabel.setJustificationType(juce::Justification::centred);
    outputChannelLabel.setFont(juce::FontOptions(10.0f));
    outputChannelLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
    addAndMakeVisible(outputChannelLabel);
    outputChannelSelector.onChange = [this] {
      if (onOutputChannelChanged == nullptr)
        return;
      const int selectedId = outputChannelSelector.getSelectedId();
      if (selectedId > 0)
        onOutputChannelChanged(selectedId);
    };
    addAndMakeVisible(outputChannelSelector);
  }

  void resized() override {
    if (!showOutputRouting) {
      const int cardW = strip->getLayout().width;
      const int cardH = strip->getLayout().height;
      const int cardX = (getWidth() - cardW) / 2;
      strip->setBounds(cardX, 4, cardW, cardH);
      return;
    }

    const int topInset = 4;
    const int controlH = 20;
    const int gap = 2;
    const int controlW = getWidth() - 12;
    const int controlX = 6;
    outputChannelLabel.setBounds(controlX, topInset, controlW, 14);
    outputChannelSelector.setBounds(controlX, outputChannelLabel.getBottom() + gap,
                                    controlW, controlH);

    const int cardW = strip->getLayout().width;
    const int cardH = strip->getLayout().height;
    const int cardX = (getWidth() - cardW) / 2;
    const int cardY = outputChannelSelector.getBottom() + 4;
    strip->setBounds(cardX, cardY, cardW, cardH);
  }

  void setOutputOptions(const juce::StringArray &names, const int selectedChannel) {
    outputChannelSelector.clear(juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
      outputChannelSelector.addItem(names[i], i + 1);

    outputChannelSelector.setSelectedId(juce::jmax(1, selectedChannel),
                                        juce::dontSendNotification);
  }

  void setShowOutputRouting(const bool show) {
    showOutputRouting = show;
    outputChannelLabel.setVisible(show);
    outputChannelSelector.setVisible(show);
    resized();
  }

  void setOnOutputChannelChanged(std::function<void(int)> callback) {
    onOutputChannelChanged = std::move(callback);
  }

  void setStripScale(const float scale) {
    strip->setLayoutScale(scale);
    resized();
  }

  ChannelStripCard &getStrip() { return *strip; }

private:
  std::unique_ptr<ChannelStripCard> strip;
  juce::Label outputChannelLabel;
  juce::ComboBox outputChannelSelector;
  std::function<void(int)> onOutputChannelChanged;
  bool showOutputRouting = true;
};

ChannelGridPanel::ChannelGridPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                                   MTControlsAudioProcessor &processorRef)
    : lookAndFeel(lookAndFeelRef), processor(processorRef) {
  nudgeHintLabel.setText("Volumen stems", juce::dontSendNotification);
  nudgeHintLabel.setJustificationType(juce::Justification::centredLeft);
  nudgeHintLabel.setFont(juce::FontOptions(10.0f));
  nudgeHintLabel.setVisible(false);
  addAndMakeVisible(nudgeHintLabel);

  routingHintLabel.setJustificationType(juce::Justification::centredLeft);
  routingHintLabel.setFont(juce::FontOptions(11.0f));
  routingHintLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  routingHintLabel.setText(
      "Routing sugerido automaticamente — puedes ajustarlo antes de aplicar",
      juce::dontSendNotification);
  routingHintLabel.setVisible(false);
  addAndMakeVisible(routingHintLabel);

  applyRoutingButton.onClick = [this] { applyRoutingDraft(); };
  applyRoutingButton.setVisible(false);
  applyRoutingButton.setColour(juce::TextButton::buttonColourId,
                               UITheme::accent.withAlpha(0.9f));
  applyRoutingButton.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  addAndMakeVisible(applyRoutingButton);

  nudgeUpButton.setClickingTogglesState(false);
  nudgeUpButton.setLookAndFeel(&lookAndFeel);
  nudgeUpButton.onClick = [this] { nudgeFaders(nudgeStep); };
  nudgeUpButton.setTooltip("Sube todos los stems (excepto master/click/guia)");
  addAndMakeVisible(nudgeUpButton);

  nudgeDownButton.setClickingTogglesState(false);
  nudgeDownButton.setLookAndFeel(&lookAndFeel);
  nudgeDownButton.onClick = [this] { nudgeFaders(-nudgeStep); };
  nudgeDownButton.setTooltip(
      "Baja todos los stems (excepto master/click/guia)");
  addAndMakeVisible(nudgeDownButton);

  viewport.setViewedComponent(&content, false);
  viewport.setScrollBarsShown(true, false, false, false);
  viewport.setScrollBarThickness(1);
  viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(viewport);

  lastVolumeInteractionMs = juce::Time::getMillisecondCounterHiRes();
  startTimerHz(timerHz);
  refreshThemeColours();
}

ChannelGridPanel::~ChannelGridPanel() {
  flushPendingVolumePersistence();
  nudgeUpButton.setLookAndFeel(nullptr);
  nudgeDownButton.setLookAndFeel(nullptr);
}

void ChannelGridPanel::setResponsiveProfile(
    const ResponsiveLayoutProfile profile) {
  if (responsiveProfile == profile)
    return;
  responsiveProfile = profile;
  resized();
  repaint();
}

void ChannelGridPanel::setOnLibrarySongLoadRequested(
    std::function<void(const ImportedSongData &, int)> cb) {
  onLibrarySongLoadRequested = std::move(cb);
}

void ChannelGridPanel::setOnLibraryAddSongRequested(
    std::function<void(const juce::StringArray &)> cb) {
  onLibraryAddSongRequested = std::move(cb);
}

void ChannelGridPanel::setOnLibraryImportRequested(std::function<void()> cb) {
  onLibraryImportRequested = std::move(cb);
}

void ChannelGridPanel::setMidiLearnActive(const bool active) {
  midiLearnActive = active;
  if (!midiLearnActive)
    midiLearnPendingTargetId = -1;
  for (auto &slot : slots) {
    auto &strip = slot->getStrip();
    const bool canLearn = strip.hasMidiLearnTarget(processor.midiTargetMasterVolume) ||
                          strip.hasMidiLearnTarget(processor.midiTargetMasterMute);
    strip.setMidiLearnSelectable(midiLearnActive && canLearn);
  }
  setMidiLearnPendingTarget(midiLearnPendingTargetId);
}

void ChannelGridPanel::setMidiLearnPendingTarget(const int targetId) {
  midiLearnPendingTargetId = targetId;
  for (auto &slot : slots) {
    auto &strip = slot->getStrip();
    strip.setMidiLearnPending(strip.hasMidiLearnTarget(targetId));
  }
}

void ChannelGridPanel::setOnMidiLearnTargetSelected(std::function<void(int)> cb) {
  onMidiLearnTargetSelected = std::move(cb);
}

void ChannelGridPanel::refreshLibraryFromStorage() {
  // La biblioteca se resolvio en el panel lateral (SongListPanel).
}

void ChannelGridPanel::setLibraryNowPlayingSong(const juce::String &songId,
                                                const bool isPlaying) {
  juce::ignoreUnused(songId, isPlaying);
}

void ChannelGridPanel::timerCallback() {
  const double nowMs = juce::Time::getMillisecondCounterHiRes();
  const bool hasPendingVolumeWrites =
      hasPendingMasterVolumeWrite || !pendingStemVolumeWrites.empty();
  if (hasPendingVolumeWrites &&
      (nowMs - lastVolumeInteractionMs) >= volumePersistenceDebounceMs) {
    flushPendingVolumePersistence();
  }

  // Run slow background checks at the previous cadence (2 Hz).
  ++slowTickCounter;
  const int slowDivider = juce::jmax(1, timerHz / slowTaskHz);
  if (slowTickCounter >= slowDivider) {
    slowTickCounter = 0;
    syncMetronomeVirtualSlotFromProcessor();

    const int n = processor.getAvailableOutputChannelCount();
    if (n != lastSeenOutputChannelCount) {
      lastSeenOutputChannelCount = n;
      if (!slots.empty())
        refreshStemRoutingVisibility();
    }
  }
}

void ChannelGridPanel::layoutHeaderAndViewport() {
  const int panelPad = responsiveProfile == ResponsiveLayoutProfile::Compact ? 10 : 14;
  auto r = getLocalBounds().reduced(panelPad);
  const int headerH = responsiveProfile == ResponsiveLayoutProfile::Compact
                          ? 38
                          : (responsiveProfile == ResponsiveLayoutProfile::Large ? 46
                                                                                 : 38);
  auto header = r.removeFromTop(headerH);

  auto headerContent = header.reduced(
      10, responsiveProfile == ResponsiveLayoutProfile::Compact ? 6 : 7);
  const int nudgeBtnW = responsiveProfile == ResponsiveLayoutProfile::Compact ? 66 : 78;
  const int nudgeGap = responsiveProfile == ResponsiveLayoutProfile::Compact ? 6 : 8;
  const int gridPad = responsiveProfile == ResponsiveLayoutProfile::Compact ? 4 : 8;
  const int masterAlignedX = panelPad + gridPad + 1;
  nudgeHintLabel.setBounds({});
  nudgeHintLabel.setFont(juce::FontOptions(
      responsiveProfile == ResponsiveLayoutProfile::Compact ? 10.5f : 11.5f));
  nudgeHintLabel.setVisible(false);

  auto nudgeButtonsArea =
      juce::Rectangle<int>(masterAlignedX, headerContent.getY(),
                           (nudgeBtnW * 2) + nudgeGap, headerContent.getHeight());
  nudgeUpButton.setBounds(nudgeButtonsArea.removeFromLeft(nudgeBtnW));
  nudgeButtonsArea.removeFromLeft(nudgeGap);
  nudgeDownButton.setBounds(nudgeButtonsArea.removeFromLeft(nudgeBtnW));

  const int routingLeft = nudgeDownButton.getRight() + 12;
  if (routingLeft > headerContent.getX())
    headerContent.removeFromLeft(juce::jmin(headerContent.getWidth(),
                                            routingLeft - headerContent.getX()));

  if (routingDraftActive && processor.getAvailableOutputChannelCount() > 2) {
    applyRoutingButton.setVisible(true);
    routingHintLabel.setVisible(true);
    applyRoutingButton.setBounds(headerContent.removeFromRight(96));
    headerContent.removeFromRight(8);
    routingHintLabel.setBounds(headerContent);
  } else {
    applyRoutingButton.setVisible(false);
    routingHintLabel.setVisible(false);
  }

  r.removeFromTop(responsiveProfile == ResponsiveLayoutProfile::Compact ? 4 : 6);
  viewport.setBounds(r);
}

void ChannelGridPanel::onStemOutputComboChanged(const size_t slotIndex,
                                                const int selectedChannel) {
  if (slotIndex >= slotStemIndices.size())
    return;
  const int stemIdx = slotStemIndices[slotIndex];
  if (stemIdx < 0)
    return;

  if (routingDraftActive && slotIndex < pendingOutputBySlotIndex.size()) {
    pendingOutputBySlotIndex[slotIndex] = selectedChannel;
    return;
  }

  processor.setStemOutputChannel(stemIdx, selectedChannel);
  if (currentSongId.isNotEmpty() && slotIndex < slotStemIds.size())
    SongLibraryStore::updateStemOutputChannelForActiveSetlist(
        currentSongId, slotStemIds[slotIndex], selectedChannel);
}

void ChannelGridPanel::applyRoutingDraft() {
  if (!routingDraftActive)
    return;
  for (size_t i = 0; i < slots.size(); ++i) {
    const int stemIdx =
        i < slotStemIndices.size() ? slotStemIndices[i] : -1;
    if (stemIdx < 0)
      continue;
    const int ch =
        i < pendingOutputBySlotIndex.size()
            ? pendingOutputBySlotIndex[i]
            : processor.getStemOutputChannel(stemIdx);
    processor.setStemOutputChannel(stemIdx, ch);
    if (currentSongId.isNotEmpty() && i < slotStemIds.size())
      SongLibraryStore::updateStemOutputChannelForActiveSetlist(
          currentSongId, slotStemIds[i], ch);
  }
  routingDraftActive = false;
  refreshStemRoutingVisibility();
}

void ChannelGridPanel::refreshStemRoutingVisibility() {
  const bool showStemRouting = false;
  const auto names = processor.getAvailableOutputChannelNames();

  for (size_t i = 0; i < slots.size(); ++i) {
    const int stemIdx = slotStemIndices[i];
    if (stemIdx < 0) {
      slots[i]->setShowOutputRouting(false);
      continue;
    }
    const int ch =
        (routingDraftActive &&
         juce::isPositiveAndBelow(i, pendingOutputBySlotIndex.size()))
            ? pendingOutputBySlotIndex[i]
            : processor.getStemOutputChannel(stemIdx);
    slots[i]->setOutputOptions(names, ch);
    slots[i]->setShowOutputRouting(showStemRouting);
  }
  resized();
  repaint();
}

void ChannelGridPanel::beginAutomaticRoutingDraft() {
  if (processor.getAvailableOutputChannelCount() <= 2)
    return;

  struct Row {
    size_t slotIndex = 0;
    int stemIndex = -1;
    juce::String name;
  };
  std::vector<Row> rows;
  for (size_t i = 0; i < slots.size(); ++i) {
    if (i >= slotStemIndices.size() || slotStemIndices[i] < 0)
      continue;
    rows.push_back({i, slotStemIndices[i], slots[i]->getStrip().getChannelName()});
  }
  if (rows.empty())
    return;

  std::vector<juce::String> names;
  std::vector<int> currents;
  names.reserve(rows.size());
  currents.reserve(rows.size());
  for (const auto &row : rows) {
    names.push_back(row.name);
    currents.push_back(processor.getStemOutputChannel(row.stemIndex));
  }

  const auto suggestions = RoutingSuggestionEngine::suggest(
      processor.getAvailableOutputChannelCount(), names, currents);

  pendingOutputBySlotIndex.assign(slots.size(), 1);
  for (size_t i = 0; i < slots.size(); ++i) {
    const int si = i < slotStemIndices.size() ? slotStemIndices[i] : -1;
    pendingOutputBySlotIndex[i] =
        si >= 0 ? processor.getStemOutputChannel(si) : 1;
  }

  for (size_t j = 0; j < suggestions.size() && j < rows.size(); ++j) {
    const size_t slotIdx = rows[j].slotIndex;
    pendingOutputBySlotIndex[slotIdx] = suggestions[j].suggestedChannel;
    slots[slotIdx]->setOutputOptions(processor.getAvailableOutputChannelNames(),
                                      suggestions[j].suggestedChannel);
  }

  routingDraftActive = true;
  resized();
  repaint();
}

void ChannelGridPanel::setSong(const ImportedSongData &song) {
  flushPendingVolumePersistence();
  routingDraftActive = false;
  metronomeVirtualSlotIndex = -1;
  slots.clear();
  adjustableStems.clear();
  clickGuideStems.clear();
  slotStemIndices.clear();
  slotStemIds.clear();
  pendingOutputBySlotIndex.clear();
  content.removeAllChildren();
  const int stemCount = processor.getLoadedStemCount();
  int mappedStemIndex = 0;
  currentSongId = song.id;

  addMasterSlot();

  const bool showStemRouting = false;

  std::vector<const ImportedStemData *> orderedStems;
  orderedStems.reserve(song.stems.size());
  for (const auto &stem : song.stems) {
    if (stem.include)
      orderedStems.push_back(&stem);
  }
  std::stable_sort(orderedStems.begin(), orderedStems.end(),
                   [](const ImportedStemData *a, const ImportedStemData *b) {
                     return stemPriorityForStripOrder(*a) <
                            stemPriorityForStripOrder(*b);
                   });

  const bool hasRealClickStem = std::any_of(
      orderedStems.begin(), orderedStems.end(),
      [](const ImportedStemData *stemPtr) {
        return stemPtr != nullptr && stemPriorityForStripOrder(*stemPtr) == 0;
      });
  if (!orderedStems.empty() && !hasRealClickStem)
    addMetronomeVirtualSlot();

  for (const auto *stemPtr : orderedStems) {
    if (stemPtr == nullptr)
      continue;
    if (!juce::isPositiveAndBelow(mappedStemIndex, stemCount))
      break;
    const auto &stem = *stemPtr;

    juce::String channelName = stem.displayName.trim();
    if (channelName.isEmpty())
      channelName = stem.category.trim();
    if (channelName.isEmpty())
      channelName = juce::File(stem.fileName).getFileNameWithoutExtension();
    if (channelName.isEmpty())
      channelName = "STEM";

    const int stemIndex = mappedStemIndex++;
    const auto stemId = stem.stemId;
    const int stemPriority = stemPriorityForStripOrder(stem);
    const bool isAdjustable = stemPriority > 1;
    const bool isClickGuide = stemPriority <= 1;
    const size_t slotIndex = slots.size();

    auto slot = std::make_unique<StripSlot>(
        channelName.toUpperCase(), lookAndFeel, [this, stemIndex] {
          const float level = processor.getStemLevel(stemIndex);
          return std::pair<float, float>{level, level};
        });
    if (isClickGuide)
      slot->getStrip().setCardBgColour(UITheme::cardBgClickGuide);
    slot->getStrip().setChannelBandColours(UITheme::accent, UITheme::onAccent);
    slot->getStrip().refreshThemeColours();
    slot->setOutputOptions(processor.getAvailableOutputChannelNames(),
                           processor.getStemOutputChannel(stemIndex));
    slot->setShowOutputRouting(showStemRouting);
    slot->setOnOutputChannelChanged(
        [this, slotIndex](const int ch) { onStemOutputComboChanged(slotIndex, ch); });
    slot->getStrip().setVolume(processor.getStemVolume(stemIndex));
    slot->getStrip().setGain(processor.getStemGain(stemIndex));
    slot->getStrip().setMuteState(processor.isStemMuted(stemIndex));
    slot->getStrip().setSoloState(processor.isStemSoloed(stemIndex));
    slot->getStrip().setOnVolumeChanged(
        [this, stemIndex, stemId](float value) {
      processor.setStemVolume(stemIndex, value);
      if (currentSongId.isNotEmpty() && stemId.isNotEmpty())
        queueStemVolumePersistence(stemId, value);
    });
    slot->getStrip().setOnGainChanged(
        [this, stemIndex, stemId](float value) {
      processor.setStemGain(stemIndex, value);
      if (currentSongId.isNotEmpty())
        SongLibraryStore::updateStemGainForActiveSetlist(currentSongId, stemId,
                                                         value);
    });
    slot->getStrip().setOnMuteChanged(
        [this, stemIndex, stemId](bool muted) {
      processor.setStemMute(stemIndex, muted);
      if (currentSongId.isNotEmpty())
        SongLibraryStore::updateStemMuteForActiveSetlist(currentSongId, stemId,
                                                         muted);
    });
    slot->getStrip().setOnSoloChanged(
        [this, stemIndex, stemId](bool soloed) {
      processor.setStemSolo(stemIndex, soloed);
      if (currentSongId.isNotEmpty())
        SongLibraryStore::updateStemSoloForActiveSetlist(currentSongId, stemId,
                                                         soloed);
    });
    slot->getStrip().setMidiLearnTargetIds(-1, -1);
    slot->getStrip().setMidiLearnSelectable(false);
    slot->getStrip().setMidiLearnPending(false);
    content.addAndMakeVisible(*slot);
    adjustableStems.push_back(isAdjustable);
    clickGuideStems.push_back(isClickGuide);
    slotStemIndices.push_back(stemIndex);
    slotStemIds.push_back(stemId);
    slots.push_back(std::move(slot));
  }

  pendingOutputBySlotIndex.assign(slots.size(), 1);
  for (size_t i = 0; i < slots.size(); ++i) {
    const int si = slotStemIndices[i];
    if (si >= 0)
      pendingOutputBySlotIndex[i] = processor.getStemOutputChannel(si);
  }

  lastSeenOutputChannelCount = processor.getAvailableOutputChannelCount();
  resized();
  repaint();
}

void ChannelGridPanel::nudgeFaders(float delta) {
  if (slots.empty())
    return;

  const int slotCount = static_cast<int>(slots.size());
  for (int i = 0; i < slotCount; ++i) {
    const int stemIndex =
        i < static_cast<int>(slotStemIndices.size())
            ? slotStemIndices[static_cast<size_t>(i)]
            : -1;
    if (stemIndex < 0)
      continue;
    if (i >= static_cast<int>(adjustableStems.size()) ||
        !adjustableStems[static_cast<size_t>(i)]) {
      continue;
    }
    const float current = processor.getStemVolume(stemIndex);
    const float next = juce::jlimit(0.0f, 1.0f, current + delta);
    if (juce::approximatelyEqual(current, next))
      continue;
    processor.setStemVolume(stemIndex, next);
    slots[static_cast<size_t>(i)]->getStrip().setVolume(next);
    if (currentSongId.isNotEmpty() &&
        i < static_cast<int>(slotStemIds.size()) &&
        slotStemIds[static_cast<size_t>(i)].isNotEmpty()) {
      queueStemVolumePersistence(slotStemIds[static_cast<size_t>(i)], next);
    }
  }
}

void ChannelGridPanel::queueStemVolumePersistence(const juce::String &stemId,
                                                  const float volume) {
  if (stemId.isEmpty())
    return;
  pendingStemVolumeWrites[stemId] = juce::jlimit(0.0f, 1.0f, volume);
  lastVolumeInteractionMs = juce::Time::getMillisecondCounterHiRes();
}

void ChannelGridPanel::queueMasterVolumePersistence(const float volume) {
  pendingMasterVolumeValue = juce::jlimit(0.0f, 1.0f, volume);
  hasPendingMasterVolumeWrite = true;
  lastVolumeInteractionMs = juce::Time::getMillisecondCounterHiRes();
}

void ChannelGridPanel::flushPendingVolumePersistence() {
  if (currentSongId.isEmpty()) {
    pendingStemVolumeWrites.clear();
    hasPendingMasterVolumeWrite = false;
    return;
  }

  if (!pendingStemVolumeWrites.empty()) {
    std::vector<std::pair<juce::String, float>> batched;
    batched.reserve(pendingStemVolumeWrites.size());
    for (const auto &entry : pendingStemVolumeWrites)
      batched.emplace_back(entry.first, entry.second);
    SongLibraryStore::updateStemVolumesForActiveSetlist(currentSongId, batched);
    pendingStemVolumeWrites.clear();
  }

  if (hasPendingMasterVolumeWrite) {
    SongLibraryStore::updateMasterVolumeForActiveSetlist(currentSongId,
                                                         pendingMasterVolumeValue);
    hasPendingMasterVolumeWrite = false;
  }
}

void ChannelGridPanel::refreshThemeColours() {
  nudgeHintLabel.setColour(juce::Label::textColourId,
                           UITheme::textPrimary.withAlpha(0.92f));

  nudgeUpButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  nudgeUpButton.setColour(juce::TextButton::buttonOnColourId,
                          UITheme::cardBgPressed);
  nudgeUpButton.setColour(juce::TextButton::textColourOffId,
                          UITheme::textPrimary);
  nudgeUpButton.setColour(juce::TextButton::textColourOnId,
                          UITheme::textPrimary);

  nudgeDownButton.setColour(juce::TextButton::buttonColourId,
                            UITheme::cardBgHover);
  nudgeDownButton.setColour(juce::TextButton::buttonOnColourId,
                            UITheme::cardBgPressed);
  nudgeDownButton.setColour(juce::TextButton::textColourOffId,
                            UITheme::textPrimary);
  nudgeDownButton.setColour(juce::TextButton::textColourOnId,
                            UITheme::textPrimary);

  for (size_t i = 0; i < slots.size(); ++i) {
    auto &strip = slots[i]->getStrip();
    const int stemIndex =
        i < slotStemIndices.size() ? slotStemIndices[i] : -1;
    if (stemIndex == -1) {
      strip.setCardBgColour(UITheme::cardBgMaster);
      strip.setChannelBandColours(UITheme::accentMaster, UITheme::onAccent);
      strip.refreshThemeColours();
      continue;
    }
    if (stemIndex == -2) {
      strip.setCardBgColour(UITheme::cardBgClickGuide);
      strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
      strip.refreshThemeColours();
      continue;
    }
    const bool isClickGuide =
        i < clickGuideStems.size() ? clickGuideStems[i] : false;
    strip.setCardBgColour(isClickGuide ? UITheme::cardBgClickGuide
                                       : UITheme::cardBg);
    strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
    strip.refreshThemeColours();
  }
  repaint();
}

void ChannelGridPanel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.38f));
  g.fillRoundedRectangle(bounds, 8.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.86f));
  g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

  const int panelPad =
      responsiveProfile == ResponsiveLayoutProfile::Compact ? 10 : 14;
  auto headerArea = getLocalBounds().reduced(panelPad);
  const int headerH = responsiveProfile == ResponsiveLayoutProfile::Compact
                          ? 38
                          : (responsiveProfile == ResponsiveLayoutProfile::Large ? 46
                                                                                 : 38);
  auto mixerHeaderBounds = headerArea.removeFromTop(headerH).toFloat();
  g.setColour(
      UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.20f).withAlpha(0.96f));
  g.fillRoundedRectangle(mixerHeaderBounds, 6.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.44f));
  g.drawRoundedRectangle(mixerHeaderBounds.reduced(0.5f), 6.0f, 1.0f);

  const auto upBounds = nudgeUpButton.getBounds();
  const auto downBounds = nudgeDownButton.getBounds();
  const float verticalSeparatorX = static_cast<float>(
      juce::jmax(upBounds.getRight(), downBounds.getRight()) + 8);
  const float verticalTop = mixerHeaderBounds.getY() + 8.0f;
  const float verticalBottom = mixerHeaderBounds.getBottom() - 8.0f;
  const auto separatorBase =
      UITheme::cardBorder.interpolatedWith(UITheme::accent, 0.24f);
  const auto separatorLight = UITheme::accentSoft.withAlpha(0.32f);
  g.setColour(separatorBase.withAlpha(0.62f));
  g.drawLine(verticalSeparatorX, verticalTop, verticalSeparatorX, verticalBottom,
             1.0f);
  g.setColour(separatorLight);
  g.drawLine(verticalSeparatorX + 1.0f, verticalTop + 1.0f,
             verticalSeparatorX + 1.0f, verticalBottom - 1.0f, 0.7f);

  const auto viewportBounds = viewport.getBounds();
  const float separatorY = static_cast<float>(viewportBounds.getY() - 3);
  g.setColour(separatorBase.withAlpha(0.66f));
  g.drawLine(mixerHeaderBounds.getX() + 2.0f, separatorY,
             mixerHeaderBounds.getRight() - 2.0f, separatorY, 0.95f);
  g.setColour(separatorLight.withAlpha(0.65f));
  g.drawLine(mixerHeaderBounds.getX() + 2.0f, separatorY + 1.0f,
             mixerHeaderBounds.getRight() - 2.0f, separatorY + 1.0f, 0.6f);

  if (slots.empty()) {
    g.setColour(UITheme::textSecondary.withAlpha(0.85f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(
        L10n::tr("Doble click en una cancion\ndel setlist",
                 "Double-click a song\nfrom setlist"),
        viewportBounds, juce::Justification::centred, 2);
  }
}

void ChannelGridPanel::resized() {
  layoutHeaderAndViewport();

  const int contentWidth = juce::jmax(1, viewport.getWidth());
  // Mantener altura estable: actualmente el ruteo por canal esta oculto.
  const bool gridShowsRouting = false;

  const int pad = responsiveProfile == ResponsiveLayoutProfile::Compact ? 4 : 8;
  const int gapX = responsiveProfile == ResponsiveLayoutProfile::Compact ? 4 : 6;
  const int gapY = responsiveProfile == ResponsiveLayoutProfile::Compact ? 8 : 10;
  const auto baseLayout = ChannelStripCard::Layout{};
  const int baseCellW = baseLayout.width + 2;
  const int maxColumnsAtBase =
      juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (baseCellW + gapX));
  const int visibleSlots = static_cast<int>(slots.size());
  const int scaleColumns =
      juce::jmax(1, juce::jmin(juce::jmax(1, visibleSlots), maxColumnsAtBase));
  const int availableCellWidth =
      juce::jmax(1, contentWidth - (pad * 2) - (juce::jmax(0, scaleColumns - 1) * gapX));
  const float widthScale =
      static_cast<float>(availableCellWidth) /
      static_cast<float>(juce::jmax(1, scaleColumns * baseCellW));
  float profileScale = 1.0f;
  if (responsiveProfile == ResponsiveLayoutProfile::Compact)
    profileScale = 0.9f;
  else if (responsiveProfile == ResponsiveLayoutProfile::Large)
    profileScale = 1.08f;

  const bool isWideViewport = contentWidth >= 1500;
  const float widthAdaptiveScale =
      isWideViewport ? juce::jlimit(0.82f, 1.22f, widthScale) : juce::jlimit(0.82f, 1.08f, widthScale);
  const float stripScale = juce::jlimit(0.82f, 1.25f, widthAdaptiveScale * profileScale);

  for (auto &slot : slots)
    slot->setStripScale(stripScale);

  const auto stripLayout =
      slots.empty() ? baseLayout : slots.front()->getStrip().getLayout();
  const int cellW = stripLayout.width + 2;
  const int columns =
      juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (cellW + gapX));
  const int rows = (visibleSlots + columns - 1) / columns;
  const int cellH = stripLayout.height + (gridShowsRouting ? 34 : 8);

  for (size_t i = 0; i < slots.size(); ++i) {
    const int row = static_cast<int>(i) / columns;
    const int col = static_cast<int>(i) % columns;
    const int x = pad + (col * (cellW + gapX));
    const int y = pad + (row * (cellH + gapY));
    slots[i]->setBounds(x, y, cellW, cellH);
  }

  const int neededH =
      pad + (rows * cellH) + (juce::jmax(0, rows - 1) * gapY) + pad;
  content.setSize(contentWidth, juce::jmax(viewport.getHeight(), neededH));
}

void ChannelGridPanel::addMasterSlot() {
  auto slot = std::make_unique<StripSlot>(
      "MASTER", lookAndFeel,
      [this] {
        float l = 0.0f;
        float r = 0.0f;
        processor.getOutputMeterLevels(l, r);
        return std::pair<float, float>{l, r};
      });
  auto &strip = slot->getStrip();
  strip.setCardBgColour(UITheme::cardBgMaster);
  strip.setChannelBandColours(UITheme::accentMaster, UITheme::onAccent);
  strip.setSoloEnabled(false);
  strip.setSoloVisible(false);
  strip.setGainVisible(true);
  strip.setMuteAlignedToFader(true);
  slot->setShowOutputRouting(false);
  strip.setVolume(processor.getMasterVolume());
  strip.setGain(processor.getMasterGain());
  strip.setMuteState(processor.isMasterMuted());
  strip.setOnVolumeChanged([this](float value) {
    processor.setMasterVolume(value);
    if (currentSongId.isNotEmpty())
      queueMasterVolumePersistence(value);
  });
  strip.setOnGainChanged([this](float value) {
    processor.setMasterGain(value);
    if (currentSongId.isNotEmpty())
      SongLibraryStore::updateMasterGainForActiveSetlist(currentSongId, value);
  });
  strip.setOnMuteChanged([this](bool muted) {
    processor.setMasterMute(muted);
    if (currentSongId.isNotEmpty())
      SongLibraryStore::updateMasterMuteForActiveSetlist(currentSongId, muted);
  });
  strip.setMidiLearnTargetIds(processor.midiTargetMasterVolume,
                              processor.midiTargetMasterMute);
  strip.setOnMidiLearnTargetSelected([this](const int targetId) {
    if (onMidiLearnTargetSelected)
      onMidiLearnTargetSelected(targetId);
  });
  strip.setMidiLearnSelectable(midiLearnActive);
  strip.setMidiLearnPending(strip.hasMidiLearnTarget(midiLearnPendingTargetId));
  strip.refreshThemeColours();

  content.addAndMakeVisible(*slot);
  slots.push_back(std::move(slot));
  adjustableStems.push_back(false);
  clickGuideStems.push_back(false);
  slotStemIndices.push_back(-1);
  slotStemIds.push_back({});
}

void ChannelGridPanel::addMetronomeVirtualSlot() {
  auto slot = std::make_unique<StripSlot>(
      "CLICK", lookAndFeel,
      [] {
        return std::pair<float, float>{0.0f, 0.0f};
      });
  auto &strip = slot->getStrip();
  strip.setCardBgColour(UITheme::cardBgClickGuide);
  strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
  strip.setMuteAlignedToFader(true);
  slot->setShowOutputRouting(false);

  strip.setVolume(processor.getMetronomeVolume());
  strip.setGain(processor.getMetronomeGain());
  strip.setMuteState(processor.isMetronomeMuted());
  strip.setSoloState(processor.isMetronomeSolo());
  strip.setOnVolumeChanged([this](float value) {
    processor.setMetronomeVolume(value);
  });
  strip.setOnGainChanged([this](float value) {
    processor.setMetronomeGain(value);
  });
  strip.setOnMuteChanged([this](bool muted) {
    processor.setMetronomeMuted(muted);
  });
  strip.setOnSoloChanged([this](bool soloed) {
    processor.setMetronomeSolo(soloed);
  });
  strip.setMidiLearnTargetIds(-1, -1);
  strip.setMidiLearnSelectable(false);
  strip.setMidiLearnPending(false);
  strip.refreshThemeColours();

  content.addAndMakeVisible(*slot);
  metronomeVirtualSlotIndex = static_cast<int>(slots.size());
  slots.push_back(std::move(slot));
  adjustableStems.push_back(false);
  clickGuideStems.push_back(true);
  slotStemIndices.push_back(-2);
  slotStemIds.push_back({});
}

void ChannelGridPanel::syncMetronomeVirtualSlotFromProcessor() {
  if (!juce::isPositiveAndBelow(metronomeVirtualSlotIndex,
                                static_cast<int>(slots.size())))
    return;

  auto &strip =
      slots[static_cast<size_t>(metronomeVirtualSlotIndex)]->getStrip();
  if (!strip.isMouseButtonDown()) {
    strip.setVolume(processor.getMetronomeVolume());
    strip.setGain(processor.getMetronomeGain());
  }
  strip.setMuteState(processor.isMetronomeMuted());
  strip.setSoloState(processor.isMetronomeSolo());
}
