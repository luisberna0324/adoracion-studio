#include "BottomStatusBar.h"

#include "../../data/Localization.h"

#include <cmath>

namespace {
float titleFontForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 10.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 11.0f;
  return 10.5f;
}

float valueFontForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 10.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 11.0f;
  return 10.5f;
}

float stripHeightForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 22.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 26.0f;
  return 24.0f;
}

float meterGapForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 10.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 14.0f;
  return 12.0f;
}

float labelWidthForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 26.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 34.0f;
  return 30.0f;
}

float valueWidthForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 84.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 102.0f;
  return 92.0f;
}

float meterMinWidthForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 154.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 188.0f;
  return 170.0f;
}

float stripMaxWidthForProfile(const ResponsiveLayoutProfile profile) {
  if (profile == ResponsiveLayoutProfile::Compact)
    return 392.0f;
  if (profile == ResponsiveLayoutProfile::Large)
    return 500.0f;
  return 450.0f;
}
} // namespace

BottomStatusBar::BottomStatusBar() {
  updateTitlesFromLanguage();
  cpuMeter.valueText = "0.0%";
  memoryMeter.valueText = "0.0%";
}

void BottomStatusBar::updateTitlesFromLanguage() {
  juce::ignoreUnused(L10n::isEnglish());
  cpuMeter.title = "CPU";
  memoryMeter.title = "MEM";
}

void BottomStatusBar::setResourceUsage(double cpuUsage01, double memoryUsage01,
                                       juce::String cpuText,
                                       juce::String memoryText) {
  updateTitlesFromLanguage();

  const double nextCpu = juce::jlimit(0.0, 1.0, cpuUsage01);
  const double nextMemory = juce::jlimit(0.0, 1.0, memoryUsage01);

  if (cpuText.trim().isEmpty())
    cpuText = juce::String(nextCpu * 100.0, 1) + "%";
  if (memoryText.trim().isEmpty())
    memoryText = juce::String(nextMemory * 100.0, 1) + "%";

  const bool changed = std::abs(cpuMeter.value01 - nextCpu) > 0.002 ||
                       std::abs(memoryMeter.value01 - nextMemory) > 0.002 ||
                       cpuMeter.valueText != cpuText ||
                       memoryMeter.valueText != memoryText;
  if (!changed)
    return;

  cpuMeter.value01 = nextCpu;
  cpuMeter.valueText = std::move(cpuText);
  memoryMeter.value01 = nextMemory;
  memoryMeter.valueText = std::move(memoryText);
  repaint();
}

void BottomStatusBar::setResponsiveProfile(const ResponsiveLayoutProfile profile) {
  if (responsiveProfile == profile)
    return;
  responsiveProfile = profile;
  resized();
  repaint();
}

void BottomStatusBar::refreshThemeColours() { repaint(); }

void BottomStatusBar::resized() {
  auto area = getLocalBounds().toFloat().reduced(6.0f, 4.0f);
  const float stripH = stripHeightForProfile(responsiveProfile);
  const float stripMaxW = stripMaxWidthForProfile(responsiveProfile);
  const float stripW = juce::jmin(stripMaxW, juce::jmax(220.0f, area.getWidth() - 8.0f));
  compactStripBounds = juce::Rectangle<float>(stripW, stripH).withCentre(area.getCentre());

  auto stripInner = compactStripBounds.reduced(10.0f, 4.0f);
  const float meterGap = meterGapForProfile(responsiveProfile);
  const float meterMinW = meterMinWidthForProfile(responsiveProfile);
  float meterW = (stripInner.getWidth() - meterGap) * 0.5f;
  if (meterW < meterMinW) {
    meterW = meterMinW;
    const float requiredW = (meterW * 2.0f) + meterGap;
    if (requiredW > stripInner.getWidth())
      meterW = juce::jmax(100.0f, (stripInner.getWidth() - meterGap) * 0.5f);
  }

  cpuMeter.bounds = stripInner.removeFromLeft(meterW);
  stripInner.removeFromLeft(meterGap);
  memoryMeter.bounds = stripInner.removeFromLeft(meterW);

  auto setupMeterLayout = [this](MeterState &meter) {
    auto row = meter.bounds;
    const float labelW = juce::jmin(labelWidthForProfile(responsiveProfile),
                                    juce::jmax(16.0f, row.getWidth() * 0.22f));
    const float valueW = juce::jmin(valueWidthForProfile(responsiveProfile),
                                    juce::jmax(48.0f, row.getWidth() * 0.34f));
    meter.labelBounds = row.removeFromLeft(labelW);
    row.removeFromLeft(6.0f);
    meter.valueBounds = row.removeFromRight(valueW);
    row.removeFromRight(6.0f);
    meter.barBounds = row.withTrimmedTop(4.0f).withTrimmedBottom(4.0f);
  };

  setupMeterLayout(cpuMeter);
  setupMeterLayout(memoryMeter);
}

void BottomStatusBar::drawMeter(juce::Graphics &g, const MeterState &meter,
                                const juce::Colour accent) const {
  if (meter.bounds.isEmpty() || meter.barBounds.isEmpty())
    return;

  g.setColour(UITheme::textSecondary.withAlpha(0.88f));
  g.setFont(makeAppFont(titleFontForProfile(responsiveProfile),
                        AppFontWeight::Medium));
  g.drawFittedText(meter.title, meter.labelBounds.toNearestInt(),
                   juce::Justification::centredLeft, 1);

  g.setColour(UITheme::textPrimary);
  g.setFont(makeAppFont(valueFontForProfile(responsiveProfile),
                        AppFontWeight::SemiBold));
  g.drawFittedText(meter.valueText, meter.valueBounds.toNearestInt(),
                   juce::Justification::centredRight, 1);

  g.setColour(UITheme::cardBorder.withAlpha(0.48f));
  g.fillRoundedRectangle(meter.barBounds, 2.5f);

  const float fillWidth =
      static_cast<float>(meter.barBounds.getWidth() *
                         juce::jlimit(0.0, 1.0, meter.value01));
  if (fillWidth <= 0.5f)
    return;

  auto fillBounds = meter.barBounds;
  fillBounds.setWidth(fillWidth);
  g.setColour(accent.withAlpha(0.92f));
  g.fillRoundedRectangle(fillBounds, 2.5f);
}

void BottomStatusBar::paint(juce::Graphics &g) {
  if (compactStripBounds.isEmpty())
    return;

  g.setColour(juce::Colours::black.withAlpha(0.22f));
  g.fillRoundedRectangle(compactStripBounds.translated(0.0f, 1.0f), 6.0f);

  g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.06f)
                  .withAlpha(0.94f));
  g.fillRoundedRectangle(compactStripBounds, 6.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.46f));
  g.drawRoundedRectangle(compactStripBounds, 6.0f, 1.0f);

  drawMeter(g, cpuMeter, UITheme::accent);
  drawMeter(g, memoryMeter, UITheme::accentSoft);

  if (!cpuMeter.bounds.isEmpty() && !memoryMeter.bounds.isEmpty()) {
    const float dividerX = cpuMeter.bounds.getRight() +
                           ((memoryMeter.bounds.getX() - cpuMeter.bounds.getRight()) * 0.5f);
    g.setColour(UITheme::dividerSoft.withAlpha(0.38f));
    g.drawLine(dividerX, compactStripBounds.getY() + 4.0f,
               dividerX, compactStripBounds.getBottom() - 4.0f,
               1.0f);
  }
}
