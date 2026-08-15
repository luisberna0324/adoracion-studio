#pragma once

#include "../UiCommon.h"
#include "../../data/SongLibraryStore.h"
#include "../../data/SongData.h"
#include <JuceHeader.h>

#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class ChannelStripLookAndFeel;
class ChannelStripCard;
class MTControlsAudioProcessor;

class ChannelGridPanel final : public juce::Component, private juce::Timer {
public:
  enum class GuideSourceMode {
    SongGuide = 0,
    DynamicBySections
  };

  ChannelGridPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                   MTControlsAudioProcessor &processorRef);
  ~ChannelGridPanel() override;

  void setSong(const ImportedSongData &song);
  void nudgeFaders(float delta);
  void refreshThemeColours();
  void setResponsiveProfile(ResponsiveLayoutProfile profile);
  void refreshLibraryFromStorage();
  void setLibraryNowPlayingSong(const juce::String &songId, bool isPlaying);
  void setOnLibrarySongLoadRequested(
      std::function<void(const ImportedSongData &, int)> cb);
  void setOnLibraryAddSongRequested(
      std::function<void(const juce::StringArray &)> cb);
  void setOnLibraryImportRequested(std::function<void()> cb);
  void setMidiLearnActive(bool active);
  void setMidiLearnPendingTarget(int targetId);
  void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
  void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
  void triggerMidiNudgeUp();
  void triggerMidiNudgeDown();
  void triggerMidiApplyRouting();
  void setGuideSourceMode(GuideSourceMode mode);
  void setOnGuideSourceModeChanged(std::function<void(GuideSourceMode)> cb);
  void setOnExportMixRequested(std::function<void(juce::String)> cb);
  void setExportBusy(bool busy);
  void setGuideWarningMessage(const juce::String &warningMessage);
  GuideSourceMode getGuideSourceMode() const noexcept { return guideSourceMode; }

  /** Tras banner "Configurar": sugerencias en borrador hasta pulsar Aplicar. */
  void beginAutomaticRoutingDraft();

  /** Actualiza visibilidad de combos y nombres de canal tras cambio de interfaz / buffer. */
  void refreshStemRoutingVisibility();
  void flushPendingPersistenceNow();

  void paint(juce::Graphics &g) override;
  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;

private:
  class StripSlot;
  class ExportMixButton;
  void timerCallback() override;
  void layoutHeaderAndViewport();
  void applyRoutingDraft();
  void onStemOutputComboChanged(size_t slotIndex, int selectedChannel);
  void addDivisionSlot();
  void addDynamicGuideVirtualSlot();
  void syncMasterSlotFromProcessor();
  void syncDivisionSlotFromProcessor();
  void syncDynamicGuideVirtualSlotFromProcessor();
  void syncStemSlotsFromProcessor();
  void queueStemVolumePersistence(const juce::String &stemId, float volume);
  void queueMasterVolumePersistence(float volume);
  void queueDynamicGuideVolumePersistence(float volume);
  void queueClickDynVolumePersistence(float volume);
  void persistDynamicGuideMixForCurrentSong();
  void persistClickDynMixForCurrentSong();
  void maybePersistClickDynMixStateChange();
  ClickDynMixSnapshot makeCurrentClickDynMixSnapshot() const;
  void flushPendingVolumePersistence();
  void showGuideSourcePopover(juce::Component *anchorComponent);
  void applyGuideSourceModeToVisibleGuideSlots();

  ChannelStripLookAndFeel &lookAndFeel;
  MTControlsAudioProcessor &processor;
  juce::Label nudgeHintLabel;
  juce::Label guideWarningLabel;
  juce::TextButton nudgeUpButton{"VOL +"};
  juce::TextButton nudgeDownButton{"VOL -"};
  std::unique_ptr<ExportMixButton> exportMixButton;
  juce::ToggleButton dynamicGuideToggle;
  juce::Label routingHintLabel;
  juce::TextButton applyRoutingButton{"Aplicar"};
  juce::Viewport viewport;
  juce::Component content;
  std::vector<std::unique_ptr<StripSlot>> slots;
  std::vector<bool> adjustableStems;
  std::vector<bool> clickGuideStems;
  std::vector<bool> guideStems;
  std::vector<int> slotStemIndices;
  std::vector<juce::String> slotStemIds;
  std::function<void(const ImportedSongData &, int)> onLibrarySongLoadRequested;
  std::function<void(const juce::StringArray &)> onLibraryAddSongRequested;
  std::function<void()> onLibraryImportRequested;
  std::function<void(int)> onMidiLearnTargetSelected;
  std::function<juce::String(int)> midiLearnBindingLabelProvider;
  std::function<void(GuideSourceMode)> onGuideSourceModeChanged;
  std::function<void(juce::String)> onExportMixRequested;
  bool exportBusy = false;
  juce::String currentSongId;
  bool midiLearnActive = false;
  int midiLearnPendingTargetId = -1;

  bool routingDraftActive = false;
  std::vector<int> pendingOutputBySlotIndex;
  std::map<int, bool> guideStemMuteBeforeDynamicByStemIndex;
  std::map<juce::String, float> pendingStemVolumeWrites;
  bool hasPendingMasterVolumeWrite = false;
  float pendingMasterVolumeValue = 1.0f;
  bool hasPendingDynamicGuideVolumeWrite = false;
  float pendingDynamicGuideVolumeValue = 1.0f;
  bool hasPendingClickDynVolumeWrite = false;
  float pendingClickDynVolumeValue = 1.0f;
  bool hasLastObservedClickDynMix = false;
  ClickDynMixSnapshot lastObservedClickDynMix;
  double lastVolumeInteractionMs = 0.0;
  int slowTickCounter = 0;
  int lastSeenOutputChannelCount = -1;
  int masterSlotIndex = -1;
  int divisionSlotIndex = -1;
  int dynamicGuideVirtualSlotIndex = -1;
  GuideSourceMode guideSourceMode = GuideSourceMode::SongGuide;
  ResponsiveLayoutProfile responsiveProfile = ResponsiveLayoutProfile::Standard;
  const float nudgeStep = 0.05f;
  static constexpr double volumePersistenceDebounceMs = 120.0;
  static constexpr int timerHz = 20;
  static constexpr int slowTaskHz = 2;

  void addMasterSlot();
};
