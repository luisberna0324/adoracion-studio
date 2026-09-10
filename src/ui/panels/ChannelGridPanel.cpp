#include "ChannelGridPanel.h"

#include "../../audio/RoutingSuggestionEngine.h"
#include "../components/ChannelStripCard.h"
#include "../components/ClickDivisionPanel.h"
#include "../../PluginProcessor.h"
#include "../../data/Localization.h"
#include "../../data/SongLibraryStore.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace {
bool clickDynMixControlStateEquals(const ClickDynMixSnapshot &a,
                                   const ClickDynMixSnapshot &b) {
  return a.divisionMask == b.divisionMask && a.enabled == b.enabled &&
         a.metronomeEnabled == b.metronomeEnabled &&
         juce::approximatelyEqual(a.volume, b.volume) &&
         juce::approximatelyEqual(a.gain, b.gain) && a.mute == b.mute &&
         a.solo == b.solo && a.outputChannel == b.outputChannel &&
         a.outputWidth == b.outputWidth;
}

bool dynamicGuideMixStateEquals(const DynamicGuideMixSnapshot &a,
                                const DynamicGuideMixSnapshot &b) {
  return juce::approximatelyEqual(a.volume, b.volume) &&
         juce::approximatelyEqual(a.gain, b.gain) && a.mute == b.mute &&
         a.solo == b.solo && a.outputChannel == b.outputChannel &&
         a.outputWidth == b.outputWidth;
}

bool masterMixStateEquals(const MasterMixSnapshot &a,
                          const MasterMixSnapshot &b) {
  return juce::approximatelyEqual(a.volume, b.volume) &&
         juce::approximatelyEqual(a.gain, b.gain) && a.mute == b.mute;
}

bool stemMixStateEquals(const StemMixSnapshot &a, const StemMixSnapshot &b) {
  return a.stemId == b.stemId &&
         juce::approximatelyEqual(a.volume, b.volume) &&
         juce::approximatelyEqual(a.gain, b.gain) && a.mute == b.mute &&
         a.solo == b.solo && a.outputChannel == b.outputChannel;
}

bool songMixControlStateEquals(const SongMixSnapshot &a,
                               const SongMixSnapshot &b) {
  if (!masterMixStateEquals(a.master, b.master) ||
      a.stems.size() != b.stems.size()) {
    return false;
  }

  for (size_t i = 0; i < a.stems.size(); ++i)
    if (!stemMixStateEquals(a.stems[i], b.stems[i]))
      return false;

  return true;
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
                                                 levelProvider, false)),
        stripLookAndFeel(lnf),
        divisionLevelProvider(std::move(levelProvider)),
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
      const int cardW = isDivisionPanelVisible() ? getDivisionPanelWidth()
                                                 : strip->getLayout().width;
      const int cardH = fillAvailableHeight ? getHeight()
                                             : strip->getLayout().height;
      const int cardX = (getWidth() - cardW) / 2;
      stripBounds = {cardX, fillAvailableHeight ? 0 : 4, cardW, cardH};
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

    strip->setVisible(!isDivisionPanelVisible());
    outputChannelLabel.setVisible(showOutputRouting);
    outputChannelSelector.setVisible(showOutputRouting);

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

    if (divisionPanel != nullptr && divisionPanel->isVisible()) {
      divisionPanel->setBounds(stripBounds);
      divisionPanel->toFront(false);
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
    stripScale = scale;
    strip->setLayoutScale(scale);
    if (divisionPanel != nullptr)
      divisionPanel->setLayoutScale(scale);
    resized();
  }

  void setFillAvailableHeight(const bool shouldFill) {
    if (fillAvailableHeight == shouldFill)
      return;
    fillAvailableHeight = shouldFill;
    resized();
  }

  void setBottomContentInset(const int inset) {
    bottomContentInset = juce::jmax(0, inset);
    strip->setBottomContentInset(bottomContentInset);
    if (divisionPanel != nullptr)
      divisionPanel->setBottomContentInset(bottomContentInset);
  }

  int getPreferredWidth() const {
    return (isDivisionPanelVisible() ? getDivisionPanelWidth()
                                     : strip->getLayout().width) +
           2;
  }

  void setDivisionPanelVisible(const bool visible) {
    if (visible && divisionPanel == nullptr) {
      divisionPanel = std::make_unique<ClickDivisionPanel>(
          stripLookAndFeel, divisionLevelProvider);
      divisionPanel->setLayoutScale(stripScale);
      divisionPanel->setBottomContentInset(fillAvailableHeight
                                               ? bottomContentInset
                                               : 0);
      divisionPanel->refreshThemeColours();
      addAndMakeVisible(*divisionPanel);
    }

    if (divisionPanel != nullptr)
      divisionPanel->setVisible(visible);

    resized();
  }

  void setDivision(const int division) {
    if (divisionPanel != nullptr)
      divisionPanel->setDivision(division);
  }

  void setDivisionVolume(const float volume) {
    if (divisionPanel != nullptr)
      divisionPanel->setVolume(volume);
  }

  void setDivisionPowerOn(const bool enabled) {
    if (divisionPanel != nullptr)
      divisionPanel->setPowerOn(enabled);
  }

  void setDivisionMuteState(const bool muted) {
    if (divisionPanel != nullptr)
      divisionPanel->setMuteState(muted);
  }

  void setDivisionSoloState(const bool soloed) {
    if (divisionPanel != nullptr)
      divisionPanel->setSoloState(soloed);
  }

  void setOnDivisionChanged(std::function<void(int)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setOnDivisionChanged(std::move(callback));
  }

  void setOnDivisionVolumeChanged(std::function<void(float)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setOnVolumeChanged(std::move(callback));
  }

  void setOnDivisionVolumeDragEnded(std::function<void(float)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setOnVolumeDragEnded(std::move(callback));
  }

  void setOnDivisionPowerChanged(std::function<void(bool)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setOnPowerChanged(std::move(callback));
  }

  void setOnDivisionMuteChanged(std::function<void(bool)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setOnMuteChanged(std::move(callback));
  }

  void setOnDivisionSoloChanged(std::function<void(bool)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setOnSoloChanged(std::move(callback));
  }

  void setDivisionMidiLearnTargetIds(const int quarterTargetId,
                                     const int eighthTargetId,
                                     const int sixteenthTargetId,
                                     const int muteTargetId,
                                     const int soloTargetId) {
    if (divisionPanel != nullptr)
      divisionPanel->setMidiLearnTargetIds(quarterTargetId, eighthTargetId,
                                           sixteenthTargetId, muteTargetId,
                                           soloTargetId);
  }

  void setDivisionMidiLearnSelectable(const bool selectable) {
    if (divisionPanel != nullptr)
      divisionPanel->setMidiLearnSelectable(selectable);
  }

  void setDivisionMidiLearnPendingTarget(const int targetId) {
    if (divisionPanel != nullptr)
      divisionPanel->setMidiLearnPendingTarget(targetId);
  }

  void setDivisionMidiLearnBindingLabelProvider(
      std::function<juce::String(int)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setMidiLearnBindingLabelProvider(std::move(callback));
  }

  void setOnDivisionMidiLearnTargetSelected(std::function<void(int)> callback) {
    if (divisionPanel != nullptr)
      divisionPanel->setOnMidiLearnTargetSelected(std::move(callback));
  }

  void refreshThemeColours() {
    strip->refreshThemeColours();
    if (divisionPanel != nullptr)
      divisionPanel->refreshThemeColours();
  }

  ChannelStripCard &getStrip() { return *strip; }
  const ChannelStripCard &getStrip() const { return *strip; }

  bool isUserInteracting() const {
    return strip->isMouseButtonDown(true) ||
           (divisionPanel != nullptr && divisionPanel->isMouseButtonDown(true));
  }

private:
  bool isDivisionPanelVisible() const {
    return divisionPanel != nullptr && divisionPanel->isVisible();
  }

  int getDivisionPanelWidth() const {
    return strip->getLayout().width;
  }

  std::unique_ptr<ChannelStripCard> strip;
  ChannelStripLookAndFeel &stripLookAndFeel;
  StereoVUMeter::LevelProvider divisionLevelProvider;
  std::unique_ptr<GuideSettingsButton> guideSettingsButton;
  std::unique_ptr<ClickDivisionPanel> divisionPanel;
  juce::Label outputChannelLabel;
  IpadNativeComboBox outputChannelSelector;
  std::function<void(int)> onOutputChannelChanged;
  std::function<void(juce::Component &)> onGuideSettingsRequested;
  bool showOutputRouting = true;
  bool fillAvailableHeight = false;
  int bottomContentInset = 0;
  float stripScale = 1.0f;
};

ChannelGridPanel::ChannelGridPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                                   MTControlsAudioProcessor &processorRef)
    : lookAndFeel(lookAndFeelRef), processor(processorRef) {
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
}

void ChannelGridPanel::setResponsiveProfile(
    const ResponsiveLayoutProfile profile) {
  if (responsiveProfile == profile)
    return;
  responsiveProfile = profile;
  resized();
  repaint();
}

void ChannelGridPanel::setMinimalPhoneMode(const bool enabled) {
  if (minimalPhoneMode == enabled)
    return;
  minimalPhoneMode = enabled;
  // El teléfono usa una cinta horizontal de una sola fila; el desplazamiento
  // se realiza con drag y no necesita consumir espacio con scrollbars.
  viewport.setScrollBarsShown(!enabled, false, false, false);
  resized();
  repaint();
}

void ChannelGridPanel::setPhoneBottomContentInset(const int inset) {
  const int nextInset = juce::jmax(0, inset);
  if (phoneBottomContentInset == nextInset)
    return;
  phoneBottomContentInset = nextInset;
  for (auto &slot : slots)
    slot->setBottomContentInset(minimalPhoneMode ? phoneBottomContentInset : 0);
  resized();
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
    slot->setDivisionMidiLearnSelectable(midiLearnActive);
  }
  setMidiLearnPendingTarget(midiLearnPendingTargetId);
  repaint();
}

void ChannelGridPanel::setMidiLearnPendingTarget(const int targetId) {
  midiLearnPendingTargetId = targetId;
  for (auto &slot : slots) {
    auto &strip = slot->getStrip();
    strip.setMidiLearnPendingTarget(targetId);
    slot->setDivisionMidiLearnPendingTarget(targetId);
  }
  repaint();
}

void ChannelGridPanel::setMidiLearnBindingLabelProvider(
    std::function<juce::String(int)> cb) {
  midiLearnBindingLabelProvider = std::move(cb);
  for (auto &slot : slots)
    slot->getStrip().setMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
  for (auto &slot : slots)
    slot->setDivisionMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
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

  guideSourceMode = mode;
  dynamicGuideToggle.setToggleState(
      guideSourceMode == GuideSourceMode::DynamicBySections,
      juce::dontSendNotification);
  applyGuideSourceModeToVisibleGuideSlots();
  if (onGuideSourceModeChanged != nullptr)
    onGuideSourceModeChanged(guideSourceMode);
}

void ChannelGridPanel::setOnGuideSourceModeChanged(
    std::function<void(GuideSourceMode)> cb) {
  onGuideSourceModeChanged = std::move(cb);
}

void ChannelGridPanel::applyGuideSourceModeToVisibleGuideSlots() {
  const bool dynamicSelected =
      guideSourceMode == GuideSourceMode::DynamicBySections;
  const auto modeLabel = guideSourceModeLabel(guideSourceMode);
  const auto modeDescription = guideSourceModeDescription(guideSourceMode);
  if (juce::isPositiveAndBelow(dynamicGuideVirtualSlotIndex,
                               static_cast<int>(slots.size()))) {
    const auto tooltip = L10n::tr(u8"Fuente de guía: ", "Guide source: ") +
                         modeLabel + " (" + modeDescription + ")";
    auto &slot = slots[static_cast<size_t>(dynamicGuideVirtualSlotIndex)];
    slot->setGuideSettingsActive(dynamicSelected);
    slot->setGuideSettingsTooltip(tooltip);
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
  presentActionPopover(std::move(panel), anchor, parentComp);
}

void ChannelGridPanel::timerCallback() {
  syncMasterSlotFromProcessor();
  syncDivisionSlotFromProcessor();
  syncDynamicGuideVirtualSlotFromProcessor();
  syncStemSlotsFromProcessor();
  maybePersistSongMixStateChange();
  maybePersistClickDynMixStateChange();
  maybePersistDynamicGuideMixStateChange();

  const double nowMs = juce::Time::getMillisecondCounterHiRes();
  const bool hasPendingVolumeWrites =
      hasPendingMasterVolumeWrite || hasPendingDynamicGuideVolumeWrite ||
      hasPendingClickDynVolumeWrite ||
      !pendingStemVolumeWrites.empty();
  if (hasPendingVolumeWrites &&
      (nowMs - lastVolumeInteractionMs) >= volumePersistenceDebounceMs) {
    flushPendingVolumePersistence();
  }

  // Run slow background checks at the previous cadence (2 Hz).
  ++slowTickCounter;
  const int slowDivider = juce::jmax(1, timerHz / slowTaskHz);
  if (slowTickCounter >= slowDivider) {
    slowTickCounter = 0;
    const int n = processor.getAvailableOutputChannelCount();
    if (n != lastSeenOutputChannelCount) {
      lastSeenOutputChannelCount = n;
      if (!slots.empty())
        refreshStemRoutingVisibility();
    }
  }
}

void ChannelGridPanel::layoutHeaderAndViewport() {
  if (minimalPhoneMode) {
    viewport.setBounds(getLocalBounds());
    return;
  }

  const int panelPad = responsiveProfile == ResponsiveLayoutProfile::Compact ? 10 : 14;
  auto r = getLocalBounds();
  r.reduce(panelPad, 0);
  r.removeFromTop(responsiveProfile == ResponsiveLayoutProfile::Compact ? 8 : 10);
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

void ChannelGridPanel::flushPendingPersistenceNow() {
  flushPendingVolumePersistence();
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
  hasPendingDynamicGuideVolumeWrite = false;
  hasPendingClickDynVolumeWrite = false;
  hasLastObservedSongMix = false;
  hasLastObservedClickDynMix = false;
  hasLastObservedDynamicGuideMix = false;
  masterSlotIndex = -1;
  divisionSlotIndex = -1;
  dynamicGuideVirtualSlotIndex = -1;
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
  addDivisionSlot();

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
  bool dynamicGuideSlotAdded = false;
  if (!orderedStems.empty() && !hasRealClickStem) {
    addDynamicGuideVirtualSlot();
    dynamicGuideSlotAdded = true;
    // CLICK DYN is already the virtual metronome in this case. Adding the
    // legacy CLICK strip would expose a second fader for the same
    // metronomeVolumeState, so both controls visibly followed each other.
    // Keep a CLICK strip only when it is an actual song stem below.
  }

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
    });
    slot->getStrip().setOnVolumeDragEnded(
        [this, stemId](float value) {
      if (currentSongId.isNotEmpty() && stemId.isNotEmpty())
        queueStemVolumePersistence(stemId, value);
    });
    slot->getStrip().setOnGainChanged(
        [this, stemIndex, stemId](float value) {
      processor.setStemGain(stemIndex, value);
    });
    slot->getStrip().setOnGainDragEnded(
        [this, stemId](float value) {
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

    if (!dynamicGuideSlotAdded && stemPriority == 0) {
      addDynamicGuideVirtualSlot();
      dynamicGuideSlotAdded = true;
    }
  }

  if (!orderedStems.empty() && !dynamicGuideSlotAdded)
    addDynamicGuideVirtualSlot();

  lastObservedSongMix = makeCurrentSongMixSnapshot();
  hasLastObservedSongMix = true;
  lastObservedClickDynMix = makeCurrentClickDynMixSnapshot();
  hasLastObservedClickDynMix = true;
  lastObservedDynamicGuideMix = makeCurrentDynamicGuideMixSnapshot();
  hasLastObservedDynamicGuideMix = true;

  pendingOutputBySlotIndex.assign(slots.size(), 1);
  for (size_t i = 0; i < slots.size(); ++i) {
    const int si = slotStemIndices[i];
    if (si >= 0)
      pendingOutputBySlotIndex[i] = processor.getStemOutputChannel(si);
  }

  lastSeenOutputChannelCount = processor.getAvailableOutputChannelCount();
  applyGuideSourceModeToVisibleGuideSlots();
  dynamicGuideToggle.setVisible(dynamicGuideVirtualSlotIndex >= 0);
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

void ChannelGridPanel::queueDynamicGuideVolumePersistence(const float volume) {
  pendingDynamicGuideVolumeValue = juce::jlimit(0.0f, 1.0f, volume);
  hasPendingDynamicGuideVolumeWrite = true;
  lastVolumeInteractionMs = juce::Time::getMillisecondCounterHiRes();
}

void ChannelGridPanel::queueClickDynVolumePersistence(const float volume) {
  pendingClickDynVolumeValue = juce::jlimit(0.0f, 1.0f, volume);
  hasPendingClickDynVolumeWrite = true;
  lastVolumeInteractionMs = juce::Time::getMillisecondCounterHiRes();
}

void ChannelGridPanel::persistDynamicGuideMixForCurrentSong() {
  if (currentSongId.isEmpty())
    return;

  const auto mix = makeCurrentDynamicGuideMixSnapshot();
  SongLibraryStore::updateDynamicGuideMixForActiveSetlist(currentSongId, mix);
  lastObservedDynamicGuideMix = mix;
  hasLastObservedDynamicGuideMix = true;
}

void ChannelGridPanel::persistClickDynMixForCurrentSong() {
  if (currentSongId.isEmpty())
    return;

  const auto mix = makeCurrentClickDynMixSnapshot();
  SongLibraryStore::updateClickDynMixForActiveSetlist(currentSongId, mix);
  lastObservedClickDynMix = mix;
  hasLastObservedClickDynMix = true;
}

SongMixSnapshot ChannelGridPanel::makeCurrentSongMixSnapshot() const {
  SongMixSnapshot mix;
  mix.master.volume = processor.getMasterVolume();
  mix.master.gain = processor.getMasterGain();
  mix.master.mute = processor.isMasterMuted();

  for (size_t i = 0; i < slotStemIndices.size(); ++i) {
    const int stemIndex = slotStemIndices[i];
    if (stemIndex < 0 || i >= slotStemIds.size() || slotStemIds[i].isEmpty())
      continue;

    StemMixSnapshot stem;
    stem.stemId = slotStemIds[i];
    stem.volume = processor.getStemVolume(stemIndex);
    stem.gain = processor.getStemGain(stemIndex);
    stem.mute = processor.isStemMuted(stemIndex);
    stem.solo = processor.isStemSoloed(stemIndex);
    stem.outputChannel = processor.getStemOutputChannel(stemIndex);
    mix.stems.push_back(std::move(stem));
  }

  return mix;
}

ClickDynMixSnapshot ChannelGridPanel::makeCurrentClickDynMixSnapshot() const {
  ClickDynMixSnapshot mix;
  mix.divisionMask = processor.getClickDivision();
  mix.enabled = processor.isClickDivisionEnabled();
  mix.metronomeEnabled = processor.isMetronomeEnabled();
  mix.volume = processor.getMetronomeVolume();
  mix.gain = processor.getMetronomeGain();
  mix.mute = processor.isMetronomeMuted();
  mix.solo = processor.isMetronomeSolo();
  mix.outputChannel = processor.getMetronomeOutputChannel();
  mix.outputWidth = processor.getMetronomeOutputWidth();
  return mix;
}

DynamicGuideMixSnapshot
ChannelGridPanel::makeCurrentDynamicGuideMixSnapshot() const {
  DynamicGuideMixSnapshot mix;
  mix.volume = processor.getDynamicGuideVolume();
  mix.gain = processor.getDynamicGuideGain();
  mix.mute = processor.isDynamicGuideMuted();
  mix.solo = processor.isDynamicGuideSolo();
  mix.outputChannel = processor.getDynamicGuideOutputChannel();
  mix.outputWidth = processor.getDynamicGuideOutputWidth();
  return mix;
}

void ChannelGridPanel::maybePersistSongMixStateChange() {
  if (currentSongId.isEmpty()) {
    hasLastObservedSongMix = false;
    return;
  }

  if (hasActivePointerInteraction())
    return;

  const auto current = makeCurrentSongMixSnapshot();
  if (hasLastObservedSongMix &&
      songMixControlStateEquals(lastObservedSongMix, current)) {
    return;
  }

  SongLibraryStore::updateMixForActiveSetlist(currentSongId, current);
  lastObservedSongMix = current;
  hasLastObservedSongMix = true;
}

void ChannelGridPanel::maybePersistClickDynMixStateChange() {
  if (currentSongId.isEmpty()) {
    hasLastObservedClickDynMix = false;
    return;
  }

  const auto current = makeCurrentClickDynMixSnapshot();
  if (hasLastObservedClickDynMix &&
      clickDynMixControlStateEquals(lastObservedClickDynMix, current)) {
    return;
  }

  SongLibraryStore::updateClickDynMixForActiveSetlist(currentSongId, current);
  lastObservedClickDynMix = current;
  hasLastObservedClickDynMix = true;
}

void ChannelGridPanel::maybePersistDynamicGuideMixStateChange() {
  if (currentSongId.isEmpty()) {
    hasLastObservedDynamicGuideMix = false;
    return;
  }

  const auto current = makeCurrentDynamicGuideMixSnapshot();
  if (hasLastObservedDynamicGuideMix &&
      dynamicGuideMixStateEquals(lastObservedDynamicGuideMix, current)) {
    return;
  }

  SongLibraryStore::updateDynamicGuideMixForActiveSetlist(currentSongId,
                                                           current);
  lastObservedDynamicGuideMix = current;
  hasLastObservedDynamicGuideMix = true;
}

void ChannelGridPanel::flushPendingVolumePersistence() {
  if (currentSongId.isEmpty()) {
    pendingStemVolumeWrites.clear();
    hasPendingMasterVolumeWrite = false;
    hasPendingDynamicGuideVolumeWrite = false;
    hasPendingClickDynVolumeWrite = false;
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

  if (hasPendingDynamicGuideVolumeWrite) {
    DynamicGuideMixSnapshot mix;
    mix.volume = pendingDynamicGuideVolumeValue;
    mix.gain = processor.getDynamicGuideGain();
    mix.mute = processor.isDynamicGuideMuted();
    mix.solo = processor.isDynamicGuideSolo();
    mix.outputChannel = processor.getDynamicGuideOutputChannel();
    mix.outputWidth = processor.getDynamicGuideOutputWidth();
    SongLibraryStore::updateDynamicGuideMixForActiveSetlist(currentSongId, mix);
    hasPendingDynamicGuideVolumeWrite = false;
  }

  if (hasPendingClickDynVolumeWrite) {
    auto mix = makeCurrentClickDynMixSnapshot();
    mix.volume = pendingClickDynVolumeValue;
    SongLibraryStore::updateClickDynMixForActiveSetlist(currentSongId, mix);
    lastObservedClickDynMix = mix;
    hasLastObservedClickDynMix = true;
    hasPendingClickDynVolumeWrite = false;
  }

  lastObservedSongMix = makeCurrentSongMixSnapshot();
  hasLastObservedSongMix = true;
}

bool ChannelGridPanel::hasActivePointerInteraction() const {
  for (const auto &slot : slots)
    if (slot != nullptr && slot->isUserInteracting())
      return true;
  return false;
}

void ChannelGridPanel::refreshThemeColours() {
  for (size_t i = 0; i < slots.size(); ++i) {
    auto &slot = *slots[i];
    auto &strip = slot.getStrip();
    const int stemIndex =
        i < slotStemIndices.size() ? slotStemIndices[i] : -1;
    if (stemIndex == -1) {
      strip.setCardBgColour(UITheme::cardBgMaster);
      strip.setChannelBandColours(UITheme::accentMaster, UITheme::onAccent);
      slot.refreshThemeColours();
      continue;
    }
    if (stemIndex == -2) {
      strip.setCardBgColour(UITheme::cardBgClickGuide);
      strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
      slot.refreshThemeColours();
      continue;
    }
    if (stemIndex == -3) {
      strip.setCardBgColour(UITheme::cardBgClickGuide);
      strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
      slot.refreshThemeColours();
      continue;
    }
    const bool isClickGuide =
        i < clickGuideStems.size() ? clickGuideStems[i] : false;
    strip.setCardBgColour(isClickGuide ? UITheme::cardBgClickGuide
                                       : UITheme::cardBg);
    strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
    slot.refreshThemeColours();
  }
  repaint();
}

void ChannelGridPanel::paint(juce::Graphics &g) {
  if (!minimalPhoneMode) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.38f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.86f));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
  }

  const auto viewportBounds = viewport.getBounds();

  if (slots.empty()) {
    g.setColour(UITheme::textSecondary.withAlpha(0.85f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(
        L10n::tr(u8"Doble click en una canción\ndel setlist",
                 "Double-click a song\nfrom setlist"),
        viewportBounds, juce::Justification::centred, 2);
  }
}

void ChannelGridPanel::resized() {
  layoutHeaderAndViewport();

  const int contentWidth = juce::jmax(1, viewport.getWidth());
  // Mantener altura estable: actualmente el ruteo por canal esta oculto.
  const bool gridShowsRouting = false;

  const int pad = minimalPhoneMode ? 0 :
      (responsiveProfile == ResponsiveLayoutProfile::Compact ? 4 : 8);
  const int gapX = responsiveProfile == ResponsiveLayoutProfile::Compact ? 4 : 6;
  const int gapY = minimalPhoneMode ? 4 :
      (responsiveProfile == ResponsiveLayoutProfile::Compact ? 8 : 10);
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

  for (auto &slot : slots) {
    slot->setStripScale(stripScale);
    slot->setFillAvailableHeight(minimalPhoneMode);
    slot->setBottomContentInset(minimalPhoneMode ? phoneBottomContentInset : 0);
  }

  const auto stripLayout =
      slots.empty() ? baseLayout : slots.front()->getStrip().getLayout();
  const int naturalCellH = stripLayout.height + (gridShowsRouting ? 34 : 8);
  const int cellH = minimalPhoneMode
                        ? juce::jmax(naturalCellH, viewport.getHeight())
                        : naturalCellH;

  int x = pad;
  int y = pad;
  int rows = visibleSlots > 0 ? 1 : 0;
  const int rightEdge = juce::jmax(pad + 1, contentWidth - pad);
  for (auto &slot : slots) {
    const int slotW = juce::jmax(stripLayout.width + 2, slot->getPreferredWidth());
    if (!minimalPhoneMode && x > pad && x + slotW > rightEdge) {
      x = pad;
      y += cellH + gapY;
      ++rows;
    }

    slot->setBounds(x, y, slotW, cellH);
    x += slotW + gapX;
  }

  if (minimalPhoneMode) {
    const int singleRowWidth = visibleSlots > 0 ? x - gapX + pad : contentWidth;
    content.setSize(juce::jmax(contentWidth, singleRowWidth),
                    viewport.getHeight());
    return;
  }

  const int neededH = pad + (rows * cellH) +
                      (juce::jmax(0, rows - 1) * gapY) + pad;
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
  });
  strip.setOnVolumeDragEnded([this](float value) {
    if (currentSongId.isNotEmpty())
      queueMasterVolumePersistence(value);
  });
  strip.setOnGainChanged([this](float value) {
    processor.setMasterGain(value);
  });
  strip.setOnGainDragEnded([this](float value) {
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

void ChannelGridPanel::addDivisionSlot() {
  divisionSlotIndex = static_cast<int>(slots.size());
  auto slot = std::make_unique<StripSlot>(
      "CLICK DYN", lookAndFeel,
      [this] {
        float left = 0.0f;
        float right = 0.0f;
        processor.getMetronomeMeterLevels(left, right);
        return std::pair<float, float>{left, right};
      });

  slot->setShowOutputRouting(false);
  slot->setGuideSettingsVisible(false);
  slot->setDivisionPanelVisible(true);
  slot->setDivision(processor.getClickDivision());
  slot->setDivisionVolume(processor.getMetronomeVolume());
  slot->setDivisionPowerOn(processor.isClickDivisionEnabled());
  slot->setDivisionMuteState(processor.isMetronomeMuted());
  slot->setDivisionSoloState(processor.isMetronomeSolo());
  slot->setOnDivisionChanged([this](const int division) {
    processor.setClickDivision(division);
    persistClickDynMixForCurrentSong();
  });
  slot->setOnDivisionVolumeChanged([this](const float volume) {
    processor.setMetronomeVolume(volume);
  });
  slot->setOnDivisionVolumeDragEnded([this](const float volume) {
    queueClickDynVolumePersistence(volume);
  });
  slot->setOnDivisionPowerChanged([this](const bool enabled) {
    processor.setClickDivisionEnabled(enabled);
    persistClickDynMixForCurrentSong();
  });
  slot->setOnDivisionMuteChanged([this](const bool muted) {
    processor.setMetronomeMuted(muted);
    persistClickDynMixForCurrentSong();
  });
  slot->setOnDivisionSoloChanged([this](const bool soloed) {
    processor.setMetronomeSolo(soloed);
    persistClickDynMixForCurrentSong();
  });
  slot->setDivisionMidiLearnTargetIds(
      processor.midiTargetClickDynDivisionQuarter,
      processor.midiTargetClickDynDivisionEighth,
      processor.midiTargetClickDynDivisionSixteenth,
      processor.midiTargetMetronomeMute, processor.midiTargetMetronomeSolo);
  slot->setDivisionMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
  slot->setOnDivisionMidiLearnTargetSelected([this](const int targetId) {
    if (onMidiLearnTargetSelected)
      onMidiLearnTargetSelected(targetId);
  });
  slot->setDivisionMidiLearnSelectable(midiLearnActive);
  slot->setDivisionMidiLearnPendingTarget(midiLearnPendingTargetId);
  slot->getStrip().setMidiLearnTargetIds(-1, -1, -1, -1, -1);
  slot->getStrip().setMidiLearnSelectable(false);
  slot->getStrip().setMidiLearnPendingTarget(-1);

  content.addAndMakeVisible(*slot);
  slots.push_back(std::move(slot));
  adjustableStems.push_back(false);
  clickGuideStems.push_back(false);
  guideStems.push_back(false);
  slotStemIndices.push_back(-4);
  slotStemIds.push_back({});
}

void ChannelGridPanel::addDynamicGuideVirtualSlot() {
  auto slot = std::make_unique<StripSlot>(
      "GUIA DYN", lookAndFeel,
      [this] {
        float left = 0.0f;
        float right = 0.0f;
        processor.getDynamicGuideMeterLevels(left, right);
        return std::pair<float, float>{left, right};
      });
  auto &strip = slot->getStrip();
  strip.setCardBgColour(UITheme::cardBgClickGuide);
  strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
  strip.setMuteAlignedToFader(true);
  slot->setShowOutputRouting(false);
  slot->setOutputOptions(processor.getAvailableOutputChannelNames(),
                         processor.getDynamicGuideOutputChannel());
  slot->setOnOutputChannelChanged([this](const int channel) {
    processor.setDynamicGuideOutputRouting(channel,
                                           processor.getDynamicGuideOutputWidth());
    persistDynamicGuideMixForCurrentSong();
  });
  slot->setGuideSettingsVisible(false);
  slot->setGuideSettingsActive(false);
  slot->setGuideSettingsTooltip({});
  slot->setOnGuideSettingsRequested({});

  strip.setVolume(processor.getDynamicGuideVolume());
  strip.setGain(processor.getDynamicGuideGain());
  strip.setMuteState(processor.isDynamicGuideMuted());
  strip.setSoloState(processor.isDynamicGuideSolo());
  strip.setOnVolumeChanged([this](float value) {
    processor.setDynamicGuideVolume(value);
  });
  strip.setOnVolumeDragEnded([this](float value) {
    queueDynamicGuideVolumePersistence(value);
  });
  strip.setOnGainChanged([this](float value) {
    processor.setDynamicGuideGain(value);
  });
  strip.setOnGainDragEnded([this](float) {
    persistDynamicGuideMixForCurrentSong();
  });
  strip.setOnMuteChanged([this](bool muted) {
    processor.setDynamicGuideMuted(muted);
    persistDynamicGuideMixForCurrentSong();
  });
  strip.setOnSoloChanged([this](bool soloed) {
    processor.setDynamicGuideSolo(soloed);
    persistDynamicGuideMixForCurrentSong();
  });
  strip.setMidiLearnTargetIds(-1, -1, -1, -1, -1);
  strip.setMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
  strip.setOnMidiLearnTargetSelected([this](const int targetId) {
    if (onMidiLearnTargetSelected)
      onMidiLearnTargetSelected(targetId);
  });
  strip.setMidiLearnSelectable(false);
  strip.setMidiLearnPendingTarget(-1);
  strip.refreshThemeColours();

  content.addAndMakeVisible(*slot);
  dynamicGuideVirtualSlotIndex = static_cast<int>(slots.size());
  slots.push_back(std::move(slot));
  adjustableStems.push_back(false);
  clickGuideStems.push_back(true);
  guideStems.push_back(false);
  slotStemIndices.push_back(-3);
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

void ChannelGridPanel::syncDivisionSlotFromProcessor() {
  if (!juce::isPositiveAndBelow(divisionSlotIndex,
                                static_cast<int>(slots.size())))
    return;

  slots[static_cast<size_t>(divisionSlotIndex)]->setDivision(
      processor.getClickDivision());
  slots[static_cast<size_t>(divisionSlotIndex)]->setDivisionVolume(
      processor.getMetronomeVolume());
  slots[static_cast<size_t>(divisionSlotIndex)]->setDivisionPowerOn(
      processor.isClickDivisionEnabled());
  slots[static_cast<size_t>(divisionSlotIndex)]->setDivisionMuteState(
      processor.isMetronomeMuted());
  slots[static_cast<size_t>(divisionSlotIndex)]->setDivisionSoloState(
      processor.isMetronomeSolo());
}

void ChannelGridPanel::syncDynamicGuideVirtualSlotFromProcessor() {
  if (!juce::isPositiveAndBelow(dynamicGuideVirtualSlotIndex,
                                static_cast<int>(slots.size())))
    return;

  auto &strip =
      slots[static_cast<size_t>(dynamicGuideVirtualSlotIndex)]->getStrip();
  if (!strip.isMouseButtonDown()) {
    strip.setVolume(processor.getDynamicGuideVolume());
    strip.setGain(processor.getDynamicGuideGain());
  }
  strip.setMuteState(processor.isDynamicGuideMuted());
  strip.setSoloState(processor.isDynamicGuideSolo());
}

void ChannelGridPanel::syncStemSlotsFromProcessor() {
  for (size_t i = 0; i < slots.size(); ++i) {
    if (static_cast<int>(i) == masterSlotIndex ||
        static_cast<int>(i) == dynamicGuideVirtualSlotIndex) {
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
