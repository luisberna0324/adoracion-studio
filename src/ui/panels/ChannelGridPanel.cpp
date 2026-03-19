#include "ChannelGridPanel.h"

#include "../../PluginProcessor.h"
#include "../../PluginEditor.h"

#include <algorithm>

class ChannelGridPanel::StripSlot : public juce::Component {
public:
  StripSlot(juce::String groupName, ChannelStripLookAndFeel &lnf,
            StereoVUMeter::LevelProvider levelProvider)
      : strip(std::make_unique<ChannelStripCard>(std::move(groupName), lnf,
                                                 std::move(levelProvider),
                                                 false)) {
    addAndMakeVisible(*strip);
  }

  void resized() override {
    const int cardW = strip->getLayout().width;
    const int cardH = strip->getLayout().height;
    const int cardX = (getWidth() - cardW) / 2;
    const int cardY = 0;
    strip->setBounds(cardX, cardY, cardW, cardH);
  }

  ChannelStripCard &getStrip() { return *strip; }

private:
  std::unique_ptr<ChannelStripCard> strip;
};

ChannelGridPanel::ChannelGridPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                                   MTControlsAudioProcessor &processorRef)
    : lookAndFeel(lookAndFeelRef), processor(processorRef) {
  viewport.setViewedComponent(&content, false);
  viewport.setScrollBarsShown(true, false, false, false);
  viewport.setScrollBarThickness(1);
  viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(viewport);
}

ChannelGridPanel::~ChannelGridPanel() = default;

void ChannelGridPanel::setSong(const ImportedSongData &song) {
  slots.clear();
  adjustableStems.clear();
  clickGuideStems.clear();
  content.removeAllChildren();
  const int stemCount = processor.getLoadedStemCount();
  int mappedStemIndex = 0;

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
    const bool isAdjustable = stemPriorityForStripOrder(stem) > 1;
    const bool isClickGuide = stemPriorityForStripOrder(stem) <= 1;
    auto slot = std::make_unique<StripSlot>(
        channelName.toUpperCase(), lookAndFeel, [this, stemIndex] {
          const float level = processor.getStemLevel(stemIndex);
          return std::pair<float, float>{level, level};
        });
    if (isClickGuide)
      slot->getStrip().setCardBgColour(UITheme::cardBgClickGuide);
    slot->getStrip().setChannelBandColours(UITheme::accent, UITheme::onAccent);
    slot->getStrip().refreshThemeColours();
    slot->getStrip().setVolume(processor.getStemVolume(stemIndex));
    slot->getStrip().setGain(processor.getStemGain(stemIndex));
    slot->getStrip().setMuteState(processor.isStemMuted(stemIndex));
    slot->getStrip().setSoloState(processor.isStemSoloed(stemIndex));
    slot->getStrip().setOnVolumeChanged([this, stemIndex](float value) {
      processor.setStemVolume(stemIndex, value);
    });
    slot->getStrip().setOnGainChanged([this, stemIndex](float value) {
      processor.setStemGain(stemIndex, value);
    });
    slot->getStrip().setOnMuteChanged([this, stemIndex](bool muted) {
      processor.setStemMute(stemIndex, muted);
    });
    slot->getStrip().setOnSoloChanged([this, stemIndex](bool soloed) {
      processor.setStemSolo(stemIndex, soloed);
    });
    content.addAndMakeVisible(*slot);
    adjustableStems.push_back(isAdjustable);
    clickGuideStems.push_back(isClickGuide);
    slots.push_back(std::move(slot));
  }

  resized();
  repaint();
}

void ChannelGridPanel::nudgeFaders(float delta) {
  if (slots.empty())
    return;

  const int stemCount = static_cast<int>(slots.size());
  for (int i = 0; i < stemCount; ++i) {
    if (i >= static_cast<int>(adjustableStems.size()) ||
        !adjustableStems[static_cast<size_t>(i)]) {
      continue;
    }
    const float current = processor.getStemVolume(i);
    const float next = juce::jlimit(0.0f, 1.0f, current + delta);
    if (juce::approximatelyEqual(current, next))
      continue;
    processor.setStemVolume(i, next);
    slots[static_cast<size_t>(i)]->getStrip().setVolume(next);
  }
}

void ChannelGridPanel::refreshThemeColours() {
  for (size_t i = 0; i < slots.size(); ++i) {
    auto &strip = slots[i]->getStrip();
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
    g.drawFittedText("Double-click a song\nfrom setlist", getLocalBounds(),
                     juce::Justification::centred, 2);
  }
}

void ChannelGridPanel::resized() {
  viewport.setBounds(getLocalBounds());
  const int contentWidth = juce::jmax(1, viewport.getWidth());

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
  const int cellH = stripLayout.height + 4;

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
