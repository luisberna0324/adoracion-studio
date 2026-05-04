#include "ChannelStripLookAndFeel.h"

#include <cmath>

void ChannelStripLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                               int width, int height,
                                               float sliderPosProportional,
                                               float rotaryStartAngle,
                                               float rotaryEndAngle,
                                               juce::Slider &) {
  const auto bounds = juce::Rectangle<float>(
      static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
      static_cast<float>(height));
  const auto centre = bounds.getCentre();
  const float knobRadius =
      juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.41f;
  const float outerRadius = knobRadius + 3.0f;
  const float angle =
      rotaryStartAngle +
      sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

  juce::Path outerArc;
  outerArc.addCentredArc(centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                         rotaryStartAngle + 0.22f, rotaryEndAngle - 0.22f,
                         true);
  g.setColour(UITheme::cardBorder);
  g.strokePath(outerArc,
               juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded));

  g.setColour(UITheme::cardBg);
  g.fillEllipse(centre.x - knobRadius, centre.y - knobRadius,
                knobRadius * 2.0f, knobRadius * 2.0f);
  g.setColour(UITheme::textSecondary);
  g.drawEllipse(centre.x - knobRadius, centre.y - knobRadius,
                knobRadius * 2.0f, knobRadius * 2.0f, 2.0f);

  g.setColour(juce::Colours::black.withAlpha(0.08f));
  g.drawEllipse(centre.x - knobRadius + 2.0f, centre.y - knobRadius + 2.0f,
                knobRadius * 2.0f - 4.0f, knobRadius * 2.0f - 4.0f, 1.0f);

  const float tickStart = knobRadius - 8.0f;
  const float tickEnd = knobRadius - 2.0f;
  const juce::Point<float> p1{centre.x + std::sin(angle) * tickStart,
                              centre.y - std::cos(angle) * tickStart};
  const juce::Point<float> p2{centre.x + std::sin(angle) * tickEnd,
                              centre.y - std::cos(angle) * tickEnd};
  g.setColour(UITheme::textPrimary);
  g.drawLine(p1.x, p1.y, p2.x, p2.y, 2.2f);
}

void ChannelStripLookAndFeel::drawLinearSlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float /* minSliderPos */, float /* maxSliderPos */, const juce::Slider::SliderStyle /* style */,
    juce::Slider & /* slider */) {

  // Track (Recessed Groove)
  const float trackWidth = 3.0f;
  const auto fx = static_cast<float>(x);
  const auto fy = static_cast<float>(y);
  const auto fw = static_cast<float>(width);
  const auto fh = static_cast<float>(height);
  const float trackX = fx + (fw * 0.5f) - (trackWidth * 0.5f);
  const float thumbHeight = 24.0f;
  const float thumbInset = thumbHeight * 0.5f;
  const float trackY = fy + thumbInset;
  const float trackHeight = fh - (thumbInset * 2.0f);

  g.setColour(juce::Colours::black.withAlpha(0.2f));
  g.fillRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 1.5f);

  // Inner shadow to emphasize depth
  g.setColour(juce::Colours::black.withAlpha(0.1f));
  g.drawRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 1.5f, 1.0f);

  // Thumb
  const float thumbWidth = fw - 4.0f;
  const float thumbX = fx + 2.0f;
  const float thumbY = juce::jlimit(static_cast<float>(y),
                                    static_cast<float>(y + height) - thumbHeight,
                                    sliderPos - thumbInset);

  // Drop Shadow
  g.setColour(juce::Colours::black.withAlpha(0.15f));
  g.fillRoundedRectangle(thumbX, thumbY + 2.0f, thumbWidth, thumbHeight, 3.0f);

  // Thumb Body
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 3.0f);

  // Thumb Border (Sharper contrast)
  g.setColour(UITheme::textSecondary.withAlpha(0.7f));
  g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 3.0f, 1.0f);

  // Indicator Line (Wider, more prominent)
  g.setColour(UITheme::textPrimary);
  const float lineY = thumbY + (thumbHeight * 0.5f);
  g.drawLine(thumbX + 1.0f, lineY, thumbX + thumbWidth - 1.0f, lineY, 2.0f);
}

void ChannelStripLookAndFeel::drawButtonBackground(juce::Graphics &g,
                                                   juce::Button &button,
                                                   const juce::Colour &backgroundColour,
                                                   bool isMouseOverButton,
                                                   bool isButtonDown) {
  auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
  const bool isEnabled = button.isEnabled();
  const bool isToggled = button.getToggleState();
  auto fill = isToggled ? button.findColour(juce::TextButton::buttonOnColourId)
                        : backgroundColour;
  const bool isMute = button.getComponentID() == "muteButton";
  const bool isSolo = button.getComponentID() == "soloButton";

  if (fill.isTransparent())
    fill = isToggled ? UITheme::cardBgPressed : UITheme::cardBg;

  if (isButtonDown)
    fill = fill.interpolatedWith(UITheme::cardBgPressed, 0.42f);
  else if (isMouseOverButton)
    fill = fill.interpolatedWith(UITheme::cardBgHover, 0.32f);

  if (isSolo && isToggled) {
    fill = UITheme::accentSoft;
    if (isMouseOverButton)
      fill = fill.brighter(0.08f);
  } else if (isMute && isToggled) {
    fill = UITheme::accentMuted;
    if (isMouseOverButton)
      fill = UITheme::accentMuted.brighter(0.08f);
  }

  if (!isEnabled)
    fill = fill.withMultipliedAlpha(0.48f);

  const float radius = juce::jlimit(3.0f, 6.0f, bounds.getHeight() * 0.16f);

  g.setColour(fill);
  g.fillRoundedRectangle(bounds, radius);

  const auto topSheen =
      isEnabled ? juce::Colours::white.withAlpha(isMouseOverButton ? 0.10f : 0.06f)
                : juce::Colours::white.withAlpha(0.03f);
  g.setColour(topSheen);
  g.drawHorizontalLine(static_cast<int>(bounds.getY() + 1.0f),
                       bounds.getX() + radius,
                       bounds.getRight() - radius);

  const auto border = (isToggled ? fill.contrasting(0.22f) : UITheme::cardBorder)
                          .withMultipliedAlpha(isEnabled ? 0.86f : 0.42f);
  g.setColour(border);
  g.drawRoundedRectangle(bounds, radius, 1.0f);
}

juce::Font ChannelStripLookAndFeel::getTextButtonFont(juce::TextButton &,
                                                      int buttonHeight) {
  const auto px =
      juce::jlimit(9.0f, 13.0f, static_cast<float>(buttonHeight) * 0.43f);
  return makeAppFont(px, AppFontWeight::SemiBold);
}
