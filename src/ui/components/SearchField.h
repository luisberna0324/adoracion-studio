#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>

class SearchField final : public juce::Component {
public:
  SearchField();

  void setPlaceholderText(const juce::String &text);
  void setText(const juce::String &text,
               juce::NotificationType notification =
                   juce::dontSendNotification);
  juce::String getText() const;
  void clear();

  void setIconGlyph(IconGlyph glyph);
  void setOnTextChange(std::function<void(const juce::String &)> cb);
  void refreshThemeColours();

  juce::TextEditor &editor();
  const juce::TextEditor &editor() const;

  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;

private:
  juce::TextEditor textEditor;
  IconGlyph iconGlyph = IconGlyph::Search;
  std::function<void(const juce::String &)> onTextChange;

  int iconInsetX = 10;
  int iconSizePx = 14;
};
