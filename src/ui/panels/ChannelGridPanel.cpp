#include "ChannelGridPanel.h"

#include "../../audio/RoutingSuggestionEngine.h"
#include "../components/ChannelStripCard.h"
#include "../../PluginProcessor.h"
#include "../../data/Localization.h"
#include "../../data/SongLibraryStore.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace {
float midiLearnBlinkStrength() {
  const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
  const double pulse = 0.5 + (0.5 * std::sin(seconds *
                                              juce::MathConstants<double>::twoPi *
                                              2.4));
  return static_cast<float>(juce::jmap(pulse, 0.36, 1.0));
}

float midiLearnSoftBlinkStrength() {
  const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
  const double pulse = 0.5 + (0.5 * std::sin(seconds *
                                              juce::MathConstants<double>::twoPi *
                                              1.1));
  return static_cast<float>(juce::jmap(pulse, 0.82, 1.0));
}

juce::String guideSourceModeLabel(const ChannelGridPanel::GuideSourceMode mode) {
  switch (mode) {
  case ChannelGridPanel::GuideSourceMode::DynamicBySections:
    return L10n::tr(u8"Guía dinámica", "Dynamic guide");
  case ChannelGridPanel::GuideSourceMode::SongGuide:
  default:
    return L10n::tr(u8"Guía original", "Original guide");
  }
}

juce::String guideSourceModeDescription(const ChannelGridPanel::GuideSourceMode mode) {
  switch (mode) {
  case ChannelGridPanel::GuideSourceMode::DynamicBySections:
    return L10n::tr(u8"Generada desde secciones", "Generated from sections");
  case ChannelGridPanel::GuideSourceMode::SongGuide:
  default:
    return L10n::tr(u8"Incluida en la canción", "Embedded in song");
  }
}

class GuideSettingsButton final : public juce::Button {
public:
  GuideSettingsButton() : juce::Button("guideSettings") {}

  void paintButton(juce::Graphics &g, bool isMouseOverButton,
                   bool isButtonDown) override {
    auto b = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = UITheme::cardBgHover.withAlpha(0.78f);
    if (getToggleState())
      fill = UITheme::accent.withAlpha(0.90f);
    if (isMouseOverButton)
      fill = fill.brighter(0.08f);
    if (isButtonDown)
      fill = fill.darker(0.08f);

    const float radius = juce::jmax(3.0f, b.getWidth() * 0.24f);
    g.setColour(juce::Colours::black.withAlpha(0.20f));
    g.fillRoundedRectangle(b.translated(0.0f, 1.0f), radius);
    g.setColour(fill);
    g.fillRoundedRectangle(b, radius);
    g.setColour(UITheme::cardBorder.withAlpha(0.70f));
    g.drawRoundedRectangle(b, radius, 1.0f);

    const auto iconArea = b.reduced(3.0f);
    const auto iconColour = getToggleState() ? UITheme::onAccent
                                             : UITheme::textPrimary;
    if (!drawSvgIcon(g, iconArea, SvgIconName::Settings, iconColour))
      drawIconGlyph(g, iconArea, IconGlyph::Settings, iconColour, 1.4f);
  }
};
} // namespace

class ChannelGridPanel::StripSlot : public juce::Component {
public:
  StripSlot(juce::String groupName, ChannelStripLookAndFeel &lnf,
            StereoVUMeter::LevelProvider levelProvider)
      : strip(std::make_unique<ChannelStripCard>(std::move(groupName), lnf,
                                                 std::move(levelProvider),
                                                 false)),
        guideSettingsButton(std::make_unique<GuideSettingsButton>()),
        outputChannelLabel({}, "Canal") {
    addAndMakeVisible(*strip);
    addAndMakeVisible(*guideSettingsButton);
    guideSettingsButton->setVisible(false);
    guideSettingsButton->setViewportIgnoreDragFlag(true);
    guideSettingsButton->onClick = [this] {
      if (onGuideSettingsRequested != nullptr && guideSettingsButton->isVisible())
        onGuideSettingsRequested(*guideSettingsButton);
    };
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
    juce::Rectangle<int> stripBounds;
    if (!showOutputRouting) {
      const int cardW = strip->getLayout().width;
      const int cardH = strip->getLayout().height;
      const int cardX = (getWidth() - cardW) / 2;
      stripBounds = {cardX, 4, cardW, cardH};
      strip->setBounds(stripBounds);
    } else {
      const int topInset = 4;
      const int controlH = 20;
      const int gap = 2;
      const int controlW = getWidth() - 12;
      const int controlX = 6;
      outputChannelLabel.setBounds(controlX, topInset, controlW, 14);
      outputChannelSelector.setBounds(controlX,
                                      outputChannelLabel.getBottom() + gap,
                                      controlW, controlH);

      const int cardW = strip->getLayout().width;
      const int cardH = strip->getLayout().height;
      const int cardX = (getWidth() - cardW) / 2;
      const int cardY = outputChannelSelector.getBottom() + 4;
      stripBounds = {cardX, cardY, cardW, cardH};
      strip->setBounds(stripBounds);
    }

    if (guideSettingsButton->isVisible()) {
      const auto &l = strip->getLayout();
      const int buttonSize = juce::jlimit(14, 22, l.soloHeight + 1);
      const int buttonX = stripBounds.getX() + juce::jmax(6, l.paddingX - 2);
      const int buttonsTopY = stripBounds.getY() + l.height -
                              l.channelBandHeight - l.buttonToBandGap -
                              l.soloHeight;
      const int buttonY = juce::jmax(stripBounds.getY() + 8,
                                     buttonsTopY - buttonSize - 6);
      guideSettingsButton->setBounds(buttonX, buttonY, buttonSize, buttonSize);
      guideSettingsButton->toFront(false);
    } else {
      guideSettingsButton->setBounds({});
    }
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

  void setGuideSettingsVisible(const bool visible) {
    guideSettingsButton->setVisible(visible);
    resized();
  }

  void setGuideSettingsActive(const bool active) {
    guideSettingsButton->setToggleState(active, juce::dontSendNotification);
  }

  void setGuideSettingsTooltip(const juce::String &tooltip) {
    guideSettingsButton->setTooltip(tooltip);
  }

  void setOnGuideSettingsRequested(
      std::function<void(juce::Component &)> callback) {
    onGuideSettingsRequested = std::move(callback);
  }

  void setStripScale(const float scale) {
    strip->setLayoutScale(scale);
    resized();
  }

  ChannelStripCard &getStrip() { return *strip; }

private:
  std::unique_ptr<ChannelStripCard> strip;
  std::unique_ptr<GuideSettingsButton> guideSettingsButton;
  juce::Label outputChannelLabel;
  juce::ComboBox outputChannelSelector;
  std::function<void(int)> onOutputChannelChanged;
  std::function<void(juce::Component &)> onGuideSettingsRequested;
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
      "Routing sugerido automaticamente --- puedes ajustarlo antes de aplicar",
      juce::dontSendNotification);
  routingHintLabel.setVisible(false);
  addAndMakeVisible(routingHintLabel);

  applyRoutingButton.onClick = [this] {
    if (midiLearnActive) {
      midiLearnPendingTargetId = processor.midiTargetMixerApplyRouting;
      if (onMidiLearnTargetSelected)
        onMidiLearnTargetSelected(processor.midiTargetMixerApplyRouting);
      repaint();
      return;
    }
    applyRoutingDraft();
  };
  applyRoutingButton.setVisible(false);
  applyRoutingButton.setColour(juce::TextButton::buttonColourId,
                               UITheme::accent.withAlpha(0.9f));
  applyRoutingButton.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  addAndMakeVisible(applyRoutingButton);

  nudgeUpButton.setClickingTogglesState(false);
  nudgeUpButton.setLookAndFeel(&lookAndFeel);
  nudgeUpButton.onClick = [this] {
    if (midiLearnActive) {
      midiLearnPendingTargetId = processor.midiTargetMixerNudgeUp;
      if (onMidiLearnTargetSelected)
        onMidiLearnTargetSelected(processor.midiTargetMixerNudgeUp);
      repaint();
      return;
    }
    nudgeFaders(nudgeStep);
  };
  nudgeUpButton.setTooltip("Sube todos los stems (excepto master/click/guia)");
  addAndMakeVisible(nudgeUpButton);

  nudgeDownButton.setClickingTogglesState(false);
  nudgeDownButton.setLookAndFeel(&lookAndFeel);
  nudgeDownButton.onClick = [this] {
    if (midiLearnActive) {
      midiLearnPendingTargetId = processor.midiTargetMixerNudgeDown;
      if (onMidiLearnTargetSelected)
        onMidiLearnTargetSelected(processor.midiTargetMixerNudgeDown);
      repaint();
      return;
    }
    nudgeFaders(-nudgeStep);
  };
  nudgeDownButton.setTooltip(
      "Baja todos los stems (excepto master/click/guia)");
  addAndMakeVisible(nudgeDownButton);

  dynamicGuideToggle.setVisible(false);

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
    const bool canLearn = strip.hasAnyMidiLearnTarget();
    strip.setMidiLearnSelectable(midiLearnActive && canLearn);
  }
  setMidiLearnPendingTarget(midiLearnPendingTargetId);
  repaint();
}

void ChannelGridPanel::setMidiLearnPendingTarget(const int targetId) {
  midiLearnPendingTargetId = targetId;
  for (auto &slot : slots) {
    auto &strip = slot->getStrip();
    strip.setMidiLearnPendingTarget(targetId);
  }
  repaint();
}

void ChannelGridPanel::setMidiLearnBindingLabelProvider(
    std::function<juce::String(int)> cb) {
  midiLearnBindingLabelProvider = std::move(cb);
  for (auto &slot : slots)
    slot->getStrip().setMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
  repaint();
}

void ChannelGridPanel::setOnMidiLearnTargetSelected(std::function<void(int)> cb) {
  onMidiLearnTargetSelected = std::move(cb);
}

void ChannelGridPanel::triggerMidiNudgeUp() { nudgeFaders(nudgeStep); }

void ChannelGridPanel::triggerMidiNudgeDown() { nudgeFaders(-nudgeStep); }

void ChannelGridPanel::triggerMidiApplyRouting() { applyRoutingDraft(); }

void ChannelGridPanel::setGuideSourceMode(const GuideSourceMode mode) {
  if (guideSourceMode == mode)
    return;

  if (mode == GuideSourceMode::DynamicBySections) {
    guideStemMuteBeforeDynamicByStemIndex.clear();
    for (size_t i = 0; i < slots.size(); ++i) {
      const bool isGuideStem = i < guideStems.size() ? guideStems[i] : false;
      if (!isGuideStem)
        continue;
      const int stemIndex =
          i < slotStemIndices.size() ? slotStemIndices[i] : -1;
      if (stemIndex < 0)
        continue;
      const bool wasMuted = processor.isStemMuted(stemIndex);
      guideStemMuteBeforeDynamicByStemIndex[stemIndex] = wasMuted;
      if (!wasMuted) {
        processor.setStemMute(stemIndex, true);
        slots[i]->getStrip().setMuteState(true);
      }
    }
  } else {
    for (size_t i = 0; i < slots.size(); ++i) {
      const bool isGuideStem = i < guideStems.size() ? guideStems[i] : false;
      if (!isGuideStem)
        continue;
      const int stemIndex =
          i < slotStemIndices.size() ? slotStemIndices[i] : -1;
      if (stemIndex < 0)
        continue;
      const auto it = guideStemMuteBeforeDynamicByStemIndex.find(stemIndex);
      if (it != guideStemMuteBeforeDynamicByStemIndex.end()) {
        processor.setStemMute(stemIndex, it->second);
        slots[i]->getStrip().setMuteState(it->second);
      }
    }
    guideStemMuteBeforeDynamicByStemIndex.clear();
  }

  guideSourceMode = mode;
  dynamicGuideToggle.setToggleState(
      guideSourceMode == GuideSourceMode::DynamicBySections,
      juce::dontSendNotification);
  applyGuideSourceModeToVisibleGuideSlots();
}

void ChannelGridPanel::applyGuideSourceModeToVisibleGuideSlots() {
  const bool dynamicSelected =
      guideSourceMode == GuideSourceMode::DynamicBySections;
  const auto modeLabel = guideSourceModeLabel(guideSourceMode);
  const auto modeDescription = guideSourceModeDescription(guideSourceMode);
  for (size_t i = 0; i < slots.size(); ++i) {
    const bool isGuideStem = i < guideStems.size() ? guideStems[i] : false;
    if (!isGuideStem)
      continue;
    const auto tooltip = L10n::tr(u8"Fuente de guía: ",
                                  "Guide source: ") +
                         modeLabel + " (" + modeDescription + ")";
    slots[i]->setGuideSettingsActive(dynamicSelected);
    slots[i]->setGuideSettingsTooltip(tooltip);
  }
  dynamicGuideToggle.setToggleState(dynamicSelected, juce::dontSendNotification);
}

void ChannelGridPanel::refreshLibraryFromStorage() {
  // La biblioteca se resolvio en el panel lateral (SongListPanel).
}

void ChannelGridPanel::setLibraryNowPlayingSong(const juce::String &songId,
                                                const bool isPlaying) {
  juce::ignoreUnused(songId, isPlaying);
}

void ChannelGridPanel::showGuideSourcePopover(juce::Component *anchorComponent) {
  if (anchorComponent == nullptr)
    return;

  class ModeChip final : public juce::Button {
  public:
    explicit ModeChip(juce::String labelText)
        : juce::Button(labelText), label(std::move(labelText)) {}

    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override {
      auto b = getLocalBounds().toFloat();
      auto fill = getToggleState() ? UITheme::accent : UITheme::cardBgHover;
      if (isMouseOverButton)
        fill = fill.brighter(0.06f);
      if (isButtonDown)
        fill = fill.darker(0.05f);
      g.setColour(fill);
      const float radius = b.getHeight() * 0.50f;
      g.fillRoundedRectangle(b, radius);
      g.setColour(UITheme::cardBorder.withAlpha(0.62f));
      g.drawRoundedRectangle(b, radius, 1.0f);
      g.setColour(getToggleState() ? UITheme::onAccent : UITheme::textPrimary);
      g.setFont(juce::FontOptions(12.0f));
      g.drawFittedText(label, getLocalBounds(), juce::Justification::centred, 1);
    }

  private:
    juce::String label;
  };

  class GuideSourcePopover final : public juce::Component {
  public:
    GuideSourcePopover(
        const GuideSourceMode activeMode,
        std::function<void(GuideSourceMode)> onSelectCallback)
        : onSelect(std::move(onSelectCallback)) {
      title.setText(L10n::tr(u8"Tipo de guía", "Guide source"),
                    juce::dontSendNotification);
      title.setJustificationType(juce::Justification::centred);
      title.setColour(juce::Label::textColourId, UITheme::textSecondary);
      title.setFont(juce::FontOptions(11.0f));
      addAndMakeVisible(title);

      const std::array<GuideSourceMode, 2> modes = {
          GuideSourceMode::SongGuide, GuideSourceMode::DynamicBySections};
      chips.reserve(modes.size());
      for (size_t i = 0; i < modes.size(); ++i) {
        auto chip =
            std::make_unique<ModeChip>(guideSourceModeLabel(modes[i]));
        chip->setClickingTogglesState(true);
        chip->setToggleState(modes[i] == activeMode, juce::dontSendNotification);
        chip->setTooltip(guideSourceModeDescription(modes[i]));
        chip->onClick = [this, i, modes] {
          setActiveIndex(static_cast<int>(i));
          if (onSelect)
            onSelect(modes[i]);
        };
        addAndMakeVisible(*chip);
        chips.push_back(std::move(chip));
      }
    }

    void paint(juce::Graphics &g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(b, 8.0f);
      g.setColour(UITheme::cardBorder);
      g.drawRoundedRectangle(b, 8.0f, 1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(12, 10);
      title.setBounds(area.removeFromTop(18));
      area.removeFromTop(8);

      const int count = static_cast<int>(chips.size());
      const int gap = 6;
      const int chipH = 28;
      const int totalGap = gap * juce::jmax(0, count - 1);
      const int chipW = juce::jmax(56, (area.getWidth() - totalGap) / count);
      int x = area.getX();
      const int y = area.getY();
      for (auto &chip : chips) {
        chip->setBounds(x, y, chipW, chipH);
        x += chipW + gap;
      }
    }

  private:
    juce::Label title;
    std::vector<std::unique_ptr<ModeChip>> chips;
    std::function<void(GuideSourceMode)> onSelect;

    void setActiveIndex(const int index) {
      for (int i = 0; i < static_cast<int>(chips.size()); ++i) {
        chips[static_cast<size_t>(i)]->setToggleState(
            i == index, juce::dontSendNotification);
      }
    }
  };

  auto safe = juce::Component::SafePointer<ChannelGridPanel>(this);
  auto panel = std::make_unique<GuideSourcePopover>(
      guideSourceMode, [safe](const GuideSourceMode mode) {
        if (safe != nullptr)
          safe->setGuideSourceMode(mode);
      });
  panel->setSize(302, 74);

  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor =
      parentComp->getLocalArea(anchorComponent, anchorComponent->getLocalBounds());
  auto &callout = juce::CallOutBox::launchAsynchronously(std::move(panel), anchor,
                                                         parentComp);
  callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void ChannelGridPanel::timerCallback() {
  syncMasterSlotFromProcessor();
  syncStemSlotsFromProcessor();

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
  guideStemMuteBeforeDynamicByStemIndex.clear();
  masterSlotIndex = -1;
  metronomeVirtualSlotIndex = -1;
  slots.clear();
  adjustableStems.clear();
  clickGuideStems.clear();
  guideStems.clear();
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
    const bool isGuideStem = stemPriority == 1;
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
    slot->setGuideSettingsVisible(false);
    slot->setGuideSettingsActive(false);
    slot->setGuideSettingsTooltip({});
    slot->setOnGuideSettingsRequested({});
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
    slot->getStrip().setMidiLearnTargetIds(
        processor.midiTargetStemVolumeBase + stemIndex,
        processor.midiTargetStemGainBase + stemIndex,
        processor.midiTargetStemMuteBase + stemIndex,
        processor.midiTargetStemSoloBase + stemIndex, -1);
    slot->getStrip().setMidiLearnBindingLabelProvider(
        midiLearnBindingLabelProvider);
    slot->getStrip().setOnMidiLearnTargetSelected([this](const int targetId) {
      if (onMidiLearnTargetSelected)
        onMidiLearnTargetSelected(targetId);
    });
    slot->getStrip().setMidiLearnSelectable(false);
    slot->getStrip().setMidiLearnPendingTarget(-1);
    content.addAndMakeVisible(*slot);
    adjustableStems.push_back(isAdjustable);
    clickGuideStems.push_back(isClickGuide);
    guideStems.push_back(isGuideStem);
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
  applyGuideSourceModeToVisibleGuideSlots();
  dynamicGuideToggle.setVisible(false);
  if (guideSourceMode == GuideSourceMode::DynamicBySections) {
    setGuideSourceMode(GuideSourceMode::SongGuide);
    setGuideSourceMode(GuideSourceMode::DynamicBySections);
  }
  setMidiLearnActive(midiLearnActive);
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

  const float verticalSeparatorX =
      static_cast<float>(nudgeDownButton.getRight() + 6);
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
        L10n::tr(u8"Doble click en una canción\ndel setlist",
                 "Double-click a song\nfrom setlist"),
        viewportBounds, juce::Justification::centred, 2);
  }
}

void ChannelGridPanel::paintOverChildren(juce::Graphics &g) {
  if (!midiLearnActive)
    return;

  const auto selectableColour = juce::Colour::fromRGB(78, 170, 255);
  const auto pendingColour = juce::Colour::fromRGB(255, 196, 92);
  auto drawLearnFrame = [this, &g, selectableColour, pendingColour](
                            juce::Component &component,
                            const int targetId) {
    if (!component.isVisible() || targetId < 0)
      return;
    const auto frameBounds = component.getBounds().toFloat().reduced(1.0f);
    const bool pending = (midiLearnPendingTargetId == targetId);
    const juce::String label =
        (pending && midiLearnBindingLabelProvider != nullptr)
            ? midiLearnBindingLabelProvider(targetId).trim()
            : juce::String{};
    const bool assigned = pending && label.isNotEmpty();
    const float blink =
        pending ? midiLearnBlinkStrength() : midiLearnSoftBlinkStrength();
    const auto frameColour =
        assigned ? selectableColour : (pending ? pendingColour : selectableColour);
    g.setColour(frameColour.withAlpha(pending ? (0.20f + (0.16f * blink))
                                              : (0.14f + (0.06f * blink))));
    g.fillRoundedRectangle(frameBounds, 7.0f);
    g.setColour(frameColour.withAlpha(pending ? (0.54f + (0.40f * blink))
                                              : (0.52f + (0.20f * blink))));
    g.drawRoundedRectangle(frameBounds, 7.0f,
                           pending ? (1.5f + (0.9f * blink))
                                   : (1.0f + (0.35f * blink)));

    if (!assigned)
      return;

    const juce::Font labelFont(
        juce::FontOptions(11.0f).withStyle("Bold").withKerningFactor(0.03f));
    const auto textBounds = frameBounds.reduced(5.0f, 5.0f);
    const float textWidth =
        static_cast<float>(label.length()) * labelFont.getHeight() * 0.62f;
    const float pillW =
        juce::jlimit(22.0f, textBounds.getWidth(), textWidth + 14.0f);
    const float pillH = juce::jlimit(14.0f, textBounds.getHeight(), 18.0f);
    auto pill = juce::Rectangle<float>(pillW, pillH).withCentre(textBounds.getCentre());
    g.setColour(juce::Colours::black.withAlpha(0.56f));
    g.fillRoundedRectangle(pill, pillH * 0.45f);
    g.setColour(selectableColour.withAlpha(0.90f));
    g.drawRoundedRectangle(pill, pillH * 0.45f, 0.9f);
    g.setColour(UITheme::textPrimary);
    g.setFont(labelFont);
    g.drawFittedText(label, pill.toNearestInt(), juce::Justification::centred,
                     1);
  };

  drawLearnFrame(nudgeUpButton, processor.midiTargetMixerNudgeUp);
  drawLearnFrame(nudgeDownButton, processor.midiTargetMixerNudgeDown);
  if (applyRoutingButton.isVisible())
    drawLearnFrame(applyRoutingButton, processor.midiTargetMixerApplyRouting);
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
  masterSlotIndex = static_cast<int>(slots.size());
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
  slot->setGuideSettingsVisible(false);
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
                              processor.midiTargetMasterGain,
                              processor.midiTargetMasterMute, -1, -1);
  strip.setMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
  strip.setOnMidiLearnTargetSelected([this](const int targetId) {
    if (onMidiLearnTargetSelected)
      onMidiLearnTargetSelected(targetId);
  });
  strip.setMidiLearnSelectable(midiLearnActive);
  strip.setMidiLearnPendingTarget(midiLearnPendingTargetId);
  strip.refreshThemeColours();

  content.addAndMakeVisible(*slot);
  slots.push_back(std::move(slot));
  adjustableStems.push_back(false);
  clickGuideStems.push_back(false);
  guideStems.push_back(false);
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
  slot->setGuideSettingsVisible(false);

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
  strip.setMidiLearnTargetIds(-1, -1, processor.midiTargetMetronomeMute,
                              processor.midiTargetMetronomeSolo, -1);
  strip.setMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
  strip.setOnMidiLearnTargetSelected([this](const int targetId) {
    if (onMidiLearnTargetSelected)
      onMidiLearnTargetSelected(targetId);
  });
  strip.setMidiLearnSelectable(false);
  strip.setMidiLearnPendingTarget(-1);
  strip.refreshThemeColours();

  content.addAndMakeVisible(*slot);
  metronomeVirtualSlotIndex = static_cast<int>(slots.size());
  slots.push_back(std::move(slot));
  adjustableStems.push_back(false);
  clickGuideStems.push_back(true);
  guideStems.push_back(false);
  slotStemIndices.push_back(-2);
  slotStemIds.push_back({});
}

void ChannelGridPanel::syncMasterSlotFromProcessor() {
  if (!juce::isPositiveAndBelow(masterSlotIndex, static_cast<int>(slots.size())))
    return;

  auto &strip = slots[static_cast<size_t>(masterSlotIndex)]->getStrip();
  if (!strip.isMouseButtonDown()) {
    strip.setVolume(processor.getMasterVolume());
    strip.setGain(processor.getMasterGain());
  }
  strip.setMuteState(processor.isMasterMuted());
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

void ChannelGridPanel::syncStemSlotsFromProcessor() {
  for (size_t i = 0; i < slots.size(); ++i) {
    if (static_cast<int>(i) == masterSlotIndex ||
        static_cast<int>(i) == metronomeVirtualSlotIndex) {
      continue;
    }

    const int stemIndex =
        i < slotStemIndices.size() ? slotStemIndices[i] : -1;
    if (stemIndex < 0)
      continue;

    auto &strip = slots[i]->getStrip();
    if (!strip.isMouseButtonDown()) {
      strip.setVolume(processor.getStemVolume(stemIndex));
      strip.setGain(processor.getStemGain(stemIndex));
    }
    strip.setMuteState(processor.isStemMuted(stemIndex));
    strip.setSoloState(processor.isStemSoloed(stemIndex));
  }
}
