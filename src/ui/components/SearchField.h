#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>

class SearchField final : public juce::Component, private juce::Timer {
public:
  SearchField();

  void setPlaceholderText(const juce::String &text);
  void setText(const juce::String &text,
               juce::NotificationType notification =
                   juce::dontSendNotification);
  juce::String getText() const;
  void clear();

  void setIconGlyph(IconGlyph glyph);
  void setLoading(bool isLoading);
  void setOnTextChange(std::function<void(const juce::String &)> cb);
  void refreshThemeColours();

  juce::TextEditor &editor();
  const juce::TextEditor &editor() const;

  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;

private:
  class RoundedLookAndFeel final : public juce::LookAndFeel_V4 {
  public:
    void fillTextEditorBackground(juce::Graphics &, int width, int height,
                                  juce::TextEditor &) override;
    void drawTextEditorOutline(juce::Graphics &, int width, int height,
                               juce::TextEditor &) override;
  } roundedLookAndFeel;

  juce::TextEditor textEditor;
  IconGlyph iconGlyph = IconGlyph::Search;
  std::function<void(const juce::String &)> onTextChange;
  bool loading = false;
  int loadingFrame = 0;

  int iconInsetX = 10;
  int iconSizePx = 14;

  void timerCallback() override;
};
