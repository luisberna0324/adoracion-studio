#pragma once

#include <JuceHeader.h>
#include <vector>

class StemOutputRoutingState {
public:
  static constexpr const char *stateTag = "stemOutputRouting";
  static constexpr const char *stemTag = "stem";
  static constexpr const char *stemIndexAttr = "index";
  static constexpr const char *channelAttr = "channel";
  static constexpr const char *widthAttr = "width";

  void resetToDefaults(int stemCount);
  void setStemOutputChannel(int stemIndex, int oneBasedChannel);
  int getStemOutputChannel(int stemIndex) const;
  void setStemOutputWidth(int stemIndex, int width);
  int getStemOutputWidth(int stemIndex) const;
  void normalizeForDevice(int maxOutputChannels);

  juce::ValueTree toValueTree() const;
  void fromValueTree(const juce::ValueTree &tree);

private:
  std::vector<int> stemOutputChannels;
  std::vector<int> stemOutputWidths;
};
