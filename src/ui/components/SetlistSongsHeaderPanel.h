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

  void setAddText(const juce::String &text);
  void setSummary(const Summary &nextSummary);

  void setOnAddClicked(std::function<void()> cb);
  void setOnDurationClicked(std::function<void()> cb);
  void setOnSongsClicked(std::function<void()> cb);
  void setOnBpmClicked(std::function<void()> cb);

  void setAddEnabled(bool enabled);
  juce::Rectangle<int> getAddButtonScreenBounds() const;

  void paint(juce::Graphics &g) override;
  void resized() override;

  void mouseMove(const juce::MouseEvent &event) override;
  void mouseExit(const juce::MouseEvent &event) override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;

private:
  void updateHoverState(juce::Point<int> p);
  int blockIndexAt(juce::Point<int> p) const;
  bool isBlockClickable(int index) const;
  void triggerBlock(int index);

  juce::String addText;
  Summary summary;

  std::function<void()> onAddClicked;
  std::function<void()> onDurationClicked;
  std::function<void()> onSongsClicked;
  std::function<void()> onBpmClicked;

  bool addEnabled = true;
  bool addHovered = false;
  bool addPressed = false;
  int hoveredBlock = -1;
  int pressedBlock = -1;

  juce::Rectangle<int> addButtonBounds;
  juce::Rectangle<int> metricsPanelBounds;
  std::array<juce::Rectangle<int>, 3> metricBlockBounds;
};
