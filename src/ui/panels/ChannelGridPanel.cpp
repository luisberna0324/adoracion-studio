#include "ChannelGridPanel.h"

#include "../../audio/RoutingSuggestionEngine.h"
#include "../components/ChannelStripCard.h"
#include "../../PluginProcessor.h"
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
  modeBadge.setJustificationType(juce::Justification::centredLeft);
  modeBadge.setFont(juce::FontOptions(11.0f));
  modeBadge.setColour(juce::Label::textColourId, UITheme::textSecondary);
  addAndMakeVisible(modeBadge);

  routingHintLabel.setJustificationType(juce::Justification::centredLeft);
  routingHintLabel.setFont(juce::FontOptions(11.0f));
  routingHintLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  routingHintLabel.setText(
      "Routing sugerido automáticamente — puedes ajustarlo antes de aplicar",
      juce::dontSendNotification);
  routingHintLabel.setVisible(false);
  addAndMakeVisible(routingHintLabel);

  applyRoutingButton.onClick = [this] { applyRoutingDraft(); };
  applyRoutingButton.setVisible(false);
  applyRoutingButton.setColour(juce::TextButton::buttonColourId,
                               UITheme::accent.withAlpha(0.9f));
  applyRoutingButton.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  addAndMakeVisible(applyRoutingButton);

  viewport.setViewedComponent(&content, false);
  viewport.setScrollBarsShown(true, false, false, false);
  viewport.setScrollBarThickness(1);
  viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(viewport);

  startTimerHz(2);
  updateModeBadge();
}

ChannelGridPanel::~ChannelGridPanel() = default;

void ChannelGridPanel::timerCallback() {
  updateModeBadge();
  const int n = processor.getAvailableOutputChannelCount();
  if (n != lastSeenOutputChannelCount) {
    lastSeenOutputChannelCount = n;
    if (!slots.empty())
      refreshStemRoutingVisibility();
  }
}

void ChannelGridPanel::updateModeBadge() {
  const bool stage = processor.getCurrentAudioMode() == AppAudioMode::Stage;
  const juce::String next =
      stage ? juce::String::fromUTF8(u8"\U0001F39B Modo escena")
            : juce::String::fromUTF8(u8"\U0001F3A7 Modo ensayo");
  if (modeBadge.getText() != next)
    modeBadge.setText(next, juce::dontSendNotification);
}

void ChannelGridPanel::layoutHeaderAndViewport() {
  auto r = getLocalBounds();
  const int headerH = 34;
  auto header = r.removeFromTop(headerH);

  modeBadge.setBounds(header.removeFromLeft(200).reduced(6, 6));
  if (routingDraftActive && processor.getAvailableOutputChannelCount() > 2) {
    applyRoutingButton.setVisible(true);
    routingHintLabel.setVisible(true);
    applyRoutingButton.setBounds(header.removeFromRight(96).reduced(4, 5));
    header.removeFromRight(6);
    routingHintLabel.setBounds(header.reduced(4, 6));
  } else {
    applyRoutingButton.setVisible(false);
    routingHintLabel.setVisible(false);
  }

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
  routingDraftActive = false;
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
    const bool isAdjustable = stemPriorityForStripOrder(stem) > 1;
    const bool isClickGuide = stemPriorityForStripOrder(stem) <= 1;
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
      if (currentSongId.isNotEmpty())
        SongLibraryStore::updateStemVolumeForActiveSetlist(currentSongId,
                                                           stemId, value);
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
      SongLibraryStore::updateStemVolumeForActiveSetlist(
          currentSongId, slotStemIds[static_cast<size_t>(i)], next);
    }
  }
}

void ChannelGridPanel::refreshThemeColours() {
  for (size_t i = 0; i < slots.size(); ++i) {
    auto &strip = slots[i]->getStrip();
    const int stemIndex =
        i < slotStemIndices.size() ? slotStemIndices[i] : -1;
    if (stemIndex < 0) {
      strip.setCardBgColour(UITheme::cardBgMaster);
      strip.setChannelBandColours(UITheme::accentMaster, UITheme::onAccent);
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
  g.fillAll(UITheme::cardBg);
  if (slots.empty()) {
    g.setColour(UITheme::textSecondary.withAlpha(0.85f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText("Double-click a song\nfrom setlist", viewport.getBounds(),
                     juce::Justification::centred, 2);
  }
}

void ChannelGridPanel::resized() {
  layoutHeaderAndViewport();

  const int contentWidth = juce::jmax(1, viewport.getWidth());
  const bool gridShowsRouting = processor.getAvailableOutputChannelCount() > 2;

  const int pad = 8;
  const int gapX = 4;
  const int gapY = 10;
  const auto stripLayout = ChannelStripCard::Layout{};
  const int cellW = stripLayout.width + 2;
  const int rows =
      (static_cast<int>(slots.size()) +
       juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (cellW + gapX)) - 1) /
      juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (cellW + gapX));
  const int columns =
      juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (cellW + gapX));
  const int cellH =
      stripLayout.height + (gridShowsRouting ? 34 : 8);

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
      SongLibraryStore::updateMasterVolumeForActiveSetlist(currentSongId,
                                                           value);
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
  strip.refreshThemeColours();

  content.addAndMakeVisible(*slot);
  slots.push_back(std::move(slot));
  adjustableStems.push_back(false);
  clickGuideStems.push_back(false);
  slotStemIndices.push_back(-1);
  slotStemIds.push_back({});
}
