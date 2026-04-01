#pragma once

#include "../UiCommon.h"
#include "../../data/SongData.h"
#include <JuceHeader.h>

#include <functional>
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
  juce::String currentSongId;

  bool routingDraftActive = false;
  std::vector<int> pendingOutputBySlotIndex;
  int lastSeenOutputChannelCount = -1;
  int metronomeVirtualSlotIndex = -1;
  const float nudgeStep = 0.05f;

  void addMasterSlot();
};
