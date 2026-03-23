#include "StemOutputRoutingState.h"

void StemOutputRoutingState::resetToDefaults(const int stemCount) {
  const int safeCount = juce::jmax(0, stemCount);
  stemOutputChannels.assign(static_cast<size_t>(safeCount), 1);
}

void StemOutputRoutingState::setStemOutputChannel(const int stemIndex,
                                                  const int oneBasedChannel) {
  if (!juce::isPositiveAndBelow(stemIndex, static_cast<int>(stemOutputChannels.size())))
    return;

  stemOutputChannels[static_cast<size_t>(stemIndex)] = juce::jmax(1, oneBasedChannel);
}

int StemOutputRoutingState::getStemOutputChannel(const int stemIndex) const {
  if (!juce::isPositiveAndBelow(stemIndex, static_cast<int>(stemOutputChannels.size())))
    return 1;

  return juce::jmax(1, stemOutputChannels[static_cast<size_t>(stemIndex)]);
}

void StemOutputRoutingState::normalizeForDevice(const int maxOutputChannels) {
  const int safeMax = juce::jmax(1, maxOutputChannels);
  for (auto &channel : stemOutputChannels)
    channel = juce::jlimit(1, safeMax, channel);
}

juce::ValueTree StemOutputRoutingState::toValueTree() const {
  juce::ValueTree tree(stateTag);

  for (int i = 0; i < static_cast<int>(stemOutputChannels.size()); ++i) {
    juce::ValueTree stemNode(stemTag);
    stemNode.setProperty(stemIndexAttr, i, nullptr);
    stemNode.setProperty(channelAttr, getStemOutputChannel(i), nullptr);
    tree.appendChild(stemNode, nullptr);
  }

  return tree;
}

void StemOutputRoutingState::fromValueTree(const juce::ValueTree &tree) {
  if (!tree.isValid() || !tree.hasType(stateTag))
    return;

  int maxIndex = -1;
  for (int i = 0; i < tree.getNumChildren(); ++i) {
    const auto child = tree.getChild(i);
    if (!child.hasType(stemTag))
      continue;

    maxIndex = juce::jmax(maxIndex,
                          static_cast<int>(child.getProperty(stemIndexAttr, -1)));
  }

  if (maxIndex < 0)
    return;

  stemOutputChannels.assign(static_cast<size_t>(maxIndex + 1), 1);
  for (int i = 0; i < tree.getNumChildren(); ++i) {
    const auto child = tree.getChild(i);
    if (!child.hasType(stemTag))
      continue;

    const int index = static_cast<int>(child.getProperty(stemIndexAttr, -1));
    if (!juce::isPositiveAndBelow(index, static_cast<int>(stemOutputChannels.size())))
      continue;

    const int channel = static_cast<int>(child.getProperty(channelAttr, 1));
    stemOutputChannels[static_cast<size_t>(index)] = juce::jmax(1, channel);
  }
}
