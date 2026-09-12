#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <array>
#include <functional>

class SetlistSongsHeaderPanel final : public juce::Component {
public:
  struct Summary {
    juce::String duration = "00:00:00";
    int songsCount = 0;
    int averageBpm = 0;
  };

  enum class BlockId { Duration = 0, Songs = 1, Bpm = 2 };

  SetlistSongsHeaderPanel();

  void refreshLanguageTexts();
  void setSummary(const Summary &nextSummary);

  void setOnAddSongsClicked(std::function<void()> cb);
  void setOnPadsClicked(std::function<void()> cb);
  void setOnDurationClicked(std::function<void()> cb);
  void setOnSongsClicked(std::function<void()> cb);
  void setOnBpmClicked(std::function<void()> cb);

  void setActionsEnabled(bool enabled);
  void setLibraryActionVisible(bool visible);

  void paint(juce::Graphics &g) override;
  void resized() override;

  void mouseMove(const juce::MouseEvent &event) override;
  void mouseExit(const juce::MouseEvent &event) override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;

private:
  void updateHoverState(juce::Point<int> p);
  bool isAddActionEnabled() const;
  bool isPadsActionEnabled() const;
  int blockIndexAt(juce::Point<int> p) const;
  bool isBlockClickable(int index) const;
  void triggerBlock(int index);

  Summary summary;

  std::function<void()> onAddSongsClicked;
  std::function<void()> onPadsClicked;
  std::function<void()> onDurationClicked;
  std::function<void()> onSongsClicked;
  std::function<void()> onBpmClicked;

  bool actionsEnabled = true;
  bool libraryActionVisible = true;
  bool hoveredAddAction = false;
  bool pressedAddAction = false;
  bool hoveredPadsAction = false;
  bool pressedPadsAction = false;
  int hoveredBlock = -1;
  int pressedBlock = -1;

  juce::Rectangle<int> addSongsButtonBounds;
  juce::Rectangle<int> padsButtonBounds;
  juce::Rectangle<int> metricsPanelBounds;
  std::array<juce::Component, 3> metricColumns;
  std::array<juce::Rectangle<int>, 3> metricBlockBounds;
};
