#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <utility>

class SetlistHandlePanel final : public juce::Component {
public:
  explicit SetlistHandlePanel(juce::String setlistName);

  std::function<void()> onToggle;

  void setTitle(juce::String setlistName);
  void setCollapsed(bool collapsedState);
  void setPhoneOverlayMode(bool enabled);

  void paint(juce::Graphics &g) override;
  void mouseEnter(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseUp(const juce::MouseEvent &) override;

private:
  juce::String title;
  bool isCollapsed = false;
  bool phoneOverlayMode = false;
  bool isHovered = false;
  bool isMouseDown = false;
};
