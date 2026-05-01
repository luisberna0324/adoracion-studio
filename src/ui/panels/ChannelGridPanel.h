#pragma once

#include "../UiCommon.h"
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

  /** Tras banner "Configurar": sugerencias en borrador hasta pulsar Aplicar. */
  void beginAutomaticRoutingDraft();

  /** Actualiza visibilidad de combos y nombres de canal tras cambio de interfaz / buffer. */
  void refreshStemRoutingVisibility();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  class StripSlot;
  void timerCallback() override;
  void layoutHeaderAndViewport();
  void applyRoutingDraft();
  void onStemOutputComboChanged(size_t slotIndex, int selectedChannel);
  void addMetronomeVirtualSlot();
  void syncMetronomeVirtualSlotFromProcessor();
  void queueStemVolumePersistence(const juce::String &stemId, float volume);
  void queueMasterVolumePersistence(float volume);
  void flushPendingVolumePersistence();

  ChannelStripLookAndFeel &lookAndFeel;
  MTControlsAudioProcessor &processor;
  juce::Label nudgeHintLabel;
  juce::TextButton nudgeUpButton{"VOL +"};
  juce::TextButton nudgeDownButton{"VOL -"};
  juce::Label routingHintLabel;
  juce::TextButton applyRoutingButton{"Aplicar"};
  juce::Viewport viewport;
  juce::Component content;
  std::vector<std::unique_ptr<StripSlot>> slots;
  std::vector<bool> adjustableStems;
  std::vector<bool> clickGuideStems;
  std::vector<int> slotStemIndices;
  std::vector<juce::String> slotStemIds;
  std::function<void(const ImportedSongData &, int)> onLibrarySongLoadRequested;
  std::function<void(const juce::StringArray &)> onLibraryAddSongRequested;
  std::function<void()> onLibraryImportRequested;
  juce::String currentSongId;

  bool routingDraftActive = false;
  std::vector<int> pendingOutputBySlotIndex;
  std::map<juce::String, float> pendingStemVolumeWrites;
  bool hasPendingMasterVolumeWrite = false;
  float pendingMasterVolumeValue = 1.0f;
  double lastVolumeInteractionMs = 0.0;
  int slowTickCounter = 0;
  int lastSeenOutputChannelCount = -1;
  int metronomeVirtualSlotIndex = -1;
  ResponsiveLayoutProfile responsiveProfile = ResponsiveLayoutProfile::Standard;
  const float nudgeStep = 0.05f;
  static constexpr double volumePersistenceDebounceMs = 120.0;
  static constexpr int timerHz = 20;
  static constexpr int slowTaskHz = 2;

  void addMasterSlot();
};
