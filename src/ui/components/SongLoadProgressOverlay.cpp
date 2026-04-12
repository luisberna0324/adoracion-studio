#include "SongLoadProgressOverlay.h"
#include "../../data/Localization.h"

SongLoadProgressOverlay::SongLoadProgressOverlay()
    : progressBar(progressValue) {
  progressBar.setPercentageDisplay(true);
  addAndMakeVisible(progressBar);
  titleLabel.setText(L10n::tr("Cargando canción", "Loading song"),
                     juce::dontSendNotification);
  titleLabel.setJustificationType(juce::Justification::centredLeft);
  titleLabel.setFont(
      juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
  addAndMakeVisible(titleLabel);

  phaseLabel.setJustificationType(juce::Justification::centredLeft);
  phaseLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
  addAndMakeVisible(phaseLabel);
  refreshThemeColours();
}

void SongLoadProgressOverlay::refreshThemeColours() {
  titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  phaseLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
}

void SongLoadProgressOverlay::setProgress(const double progressZeroToOne) {
  progressValue = juce::jlimit(0.0, 1.0, progressZeroToOne);
  progressBar.repaint();
}

void SongLoadProgressOverlay::setPhaseText(const juce::String &text) {
  phaseLabel.setText(text, juce::dontSendNotification);
}

void SongLoadProgressOverlay::setTitleText(const juce::String &text) {
  titleLabel.setText(text, juce::dontSendNotification);
}

void SongLoadProgressOverlay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black.withAlpha(0.42f));

  constexpr int cardW = 440;
  constexpr int cardH = 132;
  auto card =
      juce::Rectangle<float>(static_cast<float>(cardW), static_cast<float>(cardH))
          .withCentre(getLocalBounds().toFloat().getCentre());

  juce::Path rounded;
  rounded.addRoundedRectangle(card, 10.0f, 10.0f);
  g.setColour(UITheme::cardBg);
  g.fillPath(rounded);
  g.setColour(UITheme::cardBorder.withAlpha(0.65f));
  g.strokePath(rounded, juce::PathStrokeType(1.0f));
}

void SongLoadProgressOverlay::resized() {
  constexpr int cardW = 440;
  constexpr int cardH = 132;
  const auto card =
      juce::Rectangle<int>(cardW, cardH).withCentre(getLocalBounds().getCentre());
  auto content = card.reduced(22, 18);
  titleLabel.setBounds(content.removeFromTop(26));
  content.removeFromTop(6);
  phaseLabel.setBounds(content.removeFromTop(20));
  content.removeFromTop(12);
  progressBar.setBounds(content.removeFromTop(22));
}
