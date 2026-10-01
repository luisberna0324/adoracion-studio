#pragma once

#include <JuceHeader.h>
#include "../UiCommon.h"
#include <cmath>
#include <limits>

// Shared pitch/tempo interaction: preview while dragging, one change on release.
// The Slider value is the committed value exposed to keyboard/accessibility.
class PracticeWheelSelector : public juce::Slider, private juce::Timer {
public:
  static constexpr float itemSpacing = 44.0f;

  void cancelDrag() {
    dragging = moved = rejected = false;
    stopTimer();
    visualValue = getValue();
    repaint();
  }

  void enablementChanged() override {
    if (!isEnabled())
      cancelDrag();
    repaint();
  }

  void paint(juce::Graphics &g) override {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const float centre = bounds.getCentreX();
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(bounds, 12.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.65f));
    g.drawRoundedRectangle(bounds, 12.0f, 1.0f);

    juce::Graphics::ScopedSaveState clip(g);
    juce::Path contentClip;
    contentClip.addRoundedRectangle(bounds.reduced(1.0f), 11.0f);
    g.reduceClipRegion(contentClip);
    // The original value moves with the wheel in a cool neutral fill. The
    // current selection stays at the centre in the theme's accent fill.
    const float originalX = centre + static_cast<float>(originalValue() - visualValue) * itemSpacing;
    const float valueTop = bounds.getCentreY() - 17.0f;
    const auto originalFill = UITheme::cardBg.interpolatedWith(UITheme::textSecondary, 0.2f)
                                  .interpolatedWith(juce::Colour::fromRGB(98, 130, 156), 0.2f);
    g.setColour(originalFill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle({originalX - 22.0f, valueTop, 44.0f, 34.0f}, 8.0f);
    g.setColour(UITheme::accentMuted.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle({centre - 22.0f, valueTop, 44.0f, 34.0f}, 8.0f);
    const double origin = detentOrigin();
    const int centreIndex = juce::roundToInt(visualValue - origin);
    const int radius = static_cast<int>(std::ceil(bounds.getWidth() / (2.0f * itemSpacing))) + 1;
    for (int index = centreIndex - radius; index <= centreIndex + radius; ++index) {
      const double value = origin + static_cast<double>(index);
      if (value < wheelMinimum - 0.000001 || value > wheelMaximum + 0.000001)
        continue;
      const float distance = static_cast<float>(value - visualValue);
      const float x = centre + distance * itemSpacing;
      if (x < -itemSpacing || x > static_cast<float>(getWidth()) + itemSpacing)
        continue;
      const float emphasis = juce::jmax(0.0f, 1.0f - std::abs(distance));
      const auto textColour = std::abs(value - originalValue()) < 0.001
          ? UITheme::textPrimary
          : UITheme::textSecondary.interpolatedWith(UITheme::textPrimary, emphasis);
      g.setColour(textColour.withMultipliedAlpha(alpha));
      g.setFont(makeAppFont(16.0f + 2.0f * emphasis,
                           emphasis > 0.5f ? AppFontWeight::SemiBold : AppFontWeight::Medium));
      const auto label = itemText(value);
      g.drawFittedText(label, juce::Rectangle<float>(x - 22.0f, bounds.getY(), 44.0f, bounds.getHeight()).toNearestInt(),
                       juce::Justification::centred, 1);
    }
    // Fade the moving notes into the surface at both edges.
    const float fadeWidth = juce::jmin(30.0f, bounds.getWidth() * 0.18f);
    g.setGradientFill(juce::ColourGradient(UITheme::cardBg, bounds.getX(), 0.0f,
        UITheme::cardBg.withAlpha(0.0f), bounds.getX() + fadeWidth, 0.0f, false));
    g.fillRect(bounds.withWidth(fadeWidth));
    g.setGradientFill(juce::ColourGradient(UITheme::cardBg.withAlpha(0.0f), bounds.getRight() - fadeWidth, 0.0f,
        UITheme::cardBg, bounds.getRight(), 0.0f, false));
    g.fillRect(bounds.withLeft(bounds.getRight() - fadeWidth));
  }

  void mouseDown(const juce::MouseEvent &event) override {
    if (!isEnabled() || dragging)
      return;
    stopTimer();
    dragging = true;
    moved = rejected = false;
    pointerIndex = event.source.getIndex();
    dragOrigin = visualValue;
    dragStart = event.position;
    grabKeyboardFocus();
  }

  void mouseDrag(const juce::MouseEvent &event) override {
    if (!isEnabled() || !dragging || event.source.getIndex() != pointerIndex || rejected)
      return;
    const auto delta = event.position - dragStart;
    if (!moved) {
      if (std::abs(delta.y) > 8.0f && std::abs(delta.y) > std::abs(delta.x) * 1.2f) {
        rejected = true;
        return;
      }
      if (std::abs(delta.x) < 6.0f || std::abs(delta.x) < std::abs(delta.y))
        return;
      moved = true;
    }
    visualValue = bounded(dragOrigin - delta.x / itemSpacing);
    repaint();
  }

  void mouseUp(const juce::MouseEvent &event) override {
    if (!dragging || event.source.getIndex() != pointerIndex)
      return;
    // The last position may arrive only with release after coalesced input.
    mouseDrag(event);
    const bool commitDrag = moved;
    const bool ignore = rejected || !isEnabled() || event.getNumberOfClicks() > 1;
    dragging = false;
    if (ignore) {
      animateTo(getValue());
      return;
    }
    if (commitDrag)
      commit(snapToDetent(visualValue));
    else if (!event.mouseWasDraggedSinceMouseDown())
      commit(snapToDetent(visualValue +
          (event.position.x - static_cast<float>(getWidth()) * 0.5f) / itemSpacing));
    else
      animateTo(getValue());
  }

  void mouseDoubleClick(const juce::MouseEvent &) override {
    if (isEnabled()) {
      dragging = false;
      commit(originalValue());
    }
  }

  bool keyPressed(const juce::KeyPress &key) override {
    if (!isEnabled())
      return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey)
      commit(getValue() - 1.0);
    else if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)
      commit(getValue() + 1.0);
    else if (code == juce::KeyPress::homeKey)
      commit(originalValue());
    else
      return false;
    return true;
  }

  void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &wheel) override {
    if (!isEnabled() || dragging || wheel.isInertial)
      return;
    const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
    if (std::abs(delta) > 0.01f)
      commit(getValue() + (delta > 0.0f ? 1.0 : -1.0));
  }

protected:
  PracticeWheelSelector() {
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setWantsKeyboardFocus(true);
  }

  void configureRange(double minimum, double maximum, double interval) {
    cancelDrag();
    wheelMinimum = minimum;
    wheelMaximum = juce::jmax(minimum, maximum);
    // JUCE requires a nonempty Slider range, even for a locked engine range.
    setRange(minimum, wheelMaximum > minimum ? wheelMaximum
        : std::nextafter(minimum, std::numeric_limits<double>::infinity()), interval);
    cancelDrag();
  }

  void setSessionValue(double value) {
    if (!std::isfinite(value))
      return;
    const double next = bounded(value);
    if (juce::approximatelyEqual(getValue(), next))
      return;
    if (dragging)
      cancelDrag();
    setValue(next, juce::dontSendNotification);
    animateTo(next);
  }

  virtual double originalValue() const = 0;
  virtual double detentOrigin() const { return 0.0; }
  virtual juce::String itemText(double value) { return getTextFromValue(value); }

private:
  double bounded(double value) const {
    return juce::jlimit(wheelMinimum, wheelMaximum, value);
  }

  double snapToDetent(double value) const {
    // A tempo boundary may lie between whole-BPM detents. Reaching the edge
    // must still select the exact engine limit, rather than the nearest step.
    if (value <= wheelMinimum)
      return wheelMinimum;
    if (value >= wheelMaximum)
      return wheelMaximum;
    const double origin = detentOrigin();
    return bounded(origin + std::round(value - origin));
  }

  void valueChanged() override {
    // An external update invalidates an in-progress gesture instead of letting
    // its old origin override the newly received pitch.
    if (dragging)
      cancelDrag();
    animateTo(getValue());
  }

  void commit(double value) {
    dragging = false;
    setValue(bounded(value), juce::sendNotificationSync);
    animateTo(getValue());
  }

  void animateTo(double target) {
    animationOrigin = visualValue;
    animationTarget = target;
    animationStart = juce::Time::getMillisecondCounterHiRes();
    if (std::abs(animationOrigin - target) < 0.001) {
      visualValue = target;
      stopTimer();
    } else {
      startTimerHz(60);
    }
    repaint();
  }

  void timerCallback() override {
    const double progress = juce::jlimit(0.0, 1.0,
        (juce::Time::getMillisecondCounterHiRes() - animationStart) / 160.0);
    const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
    visualValue = animationOrigin + (animationTarget - animationOrigin) * eased;
    if (progress >= 1.0)
      stopTimer();
    repaint();
  }

  bool dragging = false, moved = false, rejected = false;
  int pointerIndex = 0;
  juce::Point<float> dragStart;
  double wheelMinimum = 0.0, wheelMaximum = 1.0;
  double dragOrigin = 0.0, visualValue = 0.0;
  double animationOrigin = 0.0, animationTarget = 0.0, animationStart = 0.0;
};
