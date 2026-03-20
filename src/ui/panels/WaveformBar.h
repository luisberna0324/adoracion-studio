#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <utility>
#include <vector>

class WaveformBar final : public juce::Component, private juce::Timer {
public:
  using PositionProvider = std::function<double()>;
  using LengthProvider = std::function<double()>;
  using SeekCallback = std::function<void(double)>;
  using EditToggleCallback = std::function<void(bool)>;

  WaveformBar();

  void setWaveformBins(std::vector<float> bins);
  void setTransportProviders(PositionProvider positionCb,
                             LengthProvider lengthCb);
  void setSeekCallback(SeekCallback cb);
  void setOnEditToggle(EditToggleCallback cb);
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;
  void mouseWheelMove(const juce::MouseEvent &event,
                      const juce::MouseWheelDetails &wheel) override;
  void mouseMagnify(const juce::MouseEvent &, float magnifyAmount) override;

private:
  void timerCallback() override;
  void handleSeek(float x);

  std::vector<float> waveformBins;
  PositionProvider positionProvider;
  LengthProvider lengthProvider;
  SeekCallback onSeekTo;
  EditToggleCallback onEditToggle;
  juce::TextButton editButton;
  float zoom = 1.0f;

  std::pair<float, float> getVisibleWindow() const;
  void applyZoom(float factor);
};
