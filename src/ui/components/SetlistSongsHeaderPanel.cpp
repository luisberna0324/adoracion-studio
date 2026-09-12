#include "SetlistSongsHeaderPanel.h"
#include "../../data/Localization.h"

namespace {
float measureTextWidth(const juce::Font &font, const juce::String &text) {
  juce::GlyphArrangement glyphs;
  glyphs.addLineOfText(font, text, 0.0f, 0.0f);
  return glyphs.getBoundingBox(0, -1, true).getWidth();
}
} // namespace

SetlistSongsHeaderPanel::SetlistSongsHeaderPanel() {
  metricBlockBounds.fill({});
  for (auto &column : metricColumns) {
    column.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(column);
  }
}

void SetlistSongsHeaderPanel::refreshLanguageTexts() { repaint(); }

void SetlistSongsHeaderPanel::setSummary(const Summary &nextSummary) {
  if (summary.duration == nextSummary.duration &&
      summary.songsCount == nextSummary.songsCount &&
      summary.averageBpm == nextSummary.averageBpm)
    return;
  summary = nextSummary;
  repaint(metricsPanelBounds);
}

void SetlistSongsHeaderPanel::setOnAddSongsClicked(std::function<void()> cb) {
  onAddSongsClicked = std::move(cb);
}

void SetlistSongsHeaderPanel::setOnPadsClicked(std::function<void()> cb) {
  onPadsClicked = std::move(cb);
  resized();
  repaint();
}

void SetlistSongsHeaderPanel::setOnDurationClicked(std::function<void()> cb) {
  onDurationClicked = std::move(cb);
}

void SetlistSongsHeaderPanel::setOnSongsClicked(std::function<void()> cb) {
  onSongsClicked = std::move(cb);
}

void SetlistSongsHeaderPanel::setOnBpmClicked(std::function<void()> cb) {
  onBpmClicked = std::move(cb);
}

void SetlistSongsHeaderPanel::setActionsEnabled(const bool enabled) {
  if (actionsEnabled == enabled)
    return;
  actionsEnabled = enabled;
  pressedAddAction = false;
  repaint();
}

void SetlistSongsHeaderPanel::setLibraryActionVisible(const bool visible) {
  if (libraryActionVisible == visible)
    return;
  libraryActionVisible = visible;
  hoveredAddAction = false;
  pressedAddAction = false;
  resized();
  repaint();
}

void SetlistSongsHeaderPanel::paint(juce::Graphics &g) {
  const auto paintAction = [&g](const juce::Rectangle<int> &bounds,
                                const juce::String &label,
                                const SvgIconName icon,
                                const bool enabled, const bool hovered,
                                const bool pressed) {
    if (bounds.isEmpty())
      return;
    auto actionRect = bounds.toFloat().reduced(0.5f);
    auto fill = UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.34f);
    if (!enabled)
      fill = fill.withMultipliedAlpha(0.45f);
    else if (pressed)
      fill = fill.interpolatedWith(UITheme::accent, 0.18f);
    else if (hovered)
      fill = fill.interpolatedWith(UITheme::accent, 0.10f);

    if (enabled && pressed)
      actionRect = actionRect.withSizeKeepingCentre(actionRect.getWidth() * 0.985f,
                                                    actionRect.getHeight() * 0.985f);

    juce::ColourGradient actionFill(fill.brighter(0.03f), actionRect.getX(),
                                    actionRect.getY(), fill.darker(0.08f),
                                    actionRect.getX(), actionRect.getBottom(),
                                    false);
    g.setGradientFill(actionFill);
    g.fillRoundedRectangle(actionRect, 8.0f);

    g.setColour(UITheme::cardBorder.withAlpha(hovered ? 0.86f : 0.62f));
    g.drawRoundedRectangle(actionRect, 8.0f, hovered ? 1.2f : 1.0f);

    const auto textColour = UITheme::textPrimary.withAlpha(enabled ? 1.0f : 0.66f);
    const auto iconColour = UITheme::accent.withAlpha(enabled ? 0.98f : 0.54f);
    const auto font = makeAppFont(12.4f, AppFontWeight::SemiBold);
    const float textWidth = measureTextWidth(font, label);
    constexpr float iconSize = 15.0f;
    constexpr float iconGap = 7.0f;
    const float actionGroupWidth = iconSize + iconGap + textWidth;
    const float actionStartX = actionRect.getCentreX() - (actionGroupWidth * 0.5f);
    const float actionCenterY = actionRect.getCentreY();
    drawSvgIcon(g,
                juce::Rectangle<float>(actionStartX,
                                       actionCenterY - (iconSize * 0.5f),
                                       iconSize, iconSize),
                icon, iconColour);

    g.setColour(textColour);
    g.setFont(font);
    g.drawText(label,
               juce::Rectangle<float>(actionStartX + iconSize + iconGap,
                                      actionCenterY - 11.0f, textWidth, 22.0f),
               juce::Justification::centredLeft);
  };
  if (libraryActionVisible)
    paintAction(addSongsButtonBounds, L10n::tr("Biblioteca", "Library"),
                SvgIconName::Music, isAddActionEnabled(), hoveredAddAction,
                pressedAddAction);
  paintAction(padsButtonBounds, "Pads", SvgIconName::AudioWaveform,
              isPadsActionEnabled(), hoveredPadsAction, pressedPadsAction);

  const auto panel = metricsPanelBounds.toFloat();
  g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.18f));
  g.fillRoundedRectangle(panel, 8.0f);
  g.setColour(juce::Colours::white.withAlpha(0.035f));
  g.drawRoundedRectangle(panel, 8.0f, 1.0f);

  g.setColour(juce::Colours::white.withAlpha(0.055f));
  for (int i = 0; i < 2; ++i) {
    const auto &left = metricBlockBounds[static_cast<size_t>(i)];
    const float x = static_cast<float>(left.getRight());
    const float y0 = panel.getY() + 11.0f;
    const float y1 = panel.getBottom() - 11.0f;
    g.drawLine(x, y0, x, y1, 1.0f);
  }

  const bool hideLabels = getWidth() < 320;
  const bool valuesOnly = getWidth() < 280;
  const auto labelColour = UITheme::textSecondary.withAlpha(0.76f);
  constexpr float metricsIconSize = 16.0f;
  constexpr int iconTextGap = 7;

  const std::array<juce::String, 3> labels = {
      L10n::tr(u8"Duración total", "Total duration"),
      L10n::tr(u8"Canciónes", "Songs"),
      L10n::tr("Prom. BPM", "Avg. BPM")};
  const std::array<juce::String, 3> values = {
      summary.duration,
      juce::String(juce::jmax(0, summary.songsCount)),
      juce::String(juce::jmax(0, summary.averageBpm))};

  for (int i = 0; i < 3; ++i) {
    auto block = metricBlockBounds[static_cast<size_t>(i)].reduced(7, 4);
    const bool clickable = isBlockClickable(i);
    const bool hovered = hoveredBlock == i && clickable;
    const auto iconName = (i == 0) ? SvgIconName::Clock
                        : (i == 1) ? SvgIconName::Music
                                   : SvgIconName::AudioWaveform;

    if (hovered) {
      g.setColour(UITheme::cardBgHover.withAlpha(0.12f));
      g.fillRoundedRectangle(block.toFloat(), 6.0f);
    }

    if (valuesOnly) {
      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(13.0f, AppFontWeight::SemiBold));
      g.drawFittedText(values[static_cast<size_t>(i)], block,
                       juce::Justification::centred, 1);
      continue;
    }

    if (hideLabels) {
      const auto valueFont = makeAppFont(13.0f, AppFontWeight::SemiBold);
      const float valueWidth =
          measureTextWidth(valueFont, values[static_cast<size_t>(i)]);
      const float groupWidth = metricsIconSize + static_cast<float>(iconTextGap) + valueWidth;
      const float startX = static_cast<float>(block.getCentreX()) - (groupWidth * 0.5f);
      const float centerY = static_cast<float>(block.getCentreY());

      drawSvgIcon(g,
                  juce::Rectangle<float>(startX,
                                         centerY - (metricsIconSize * 0.5f),
                                         metricsIconSize, metricsIconSize),
                  iconName, labelColour.withAlpha(0.95f));

      g.setColour(UITheme::textPrimary);
      g.setFont(valueFont);
      g.drawText(values[static_cast<size_t>(i)],
                 juce::Rectangle<float>(startX + metricsIconSize + iconTextGap,
                                        centerY - 10.0f, valueWidth, 20.0f),
                 juce::Justification::centredLeft);
      continue;
    }

    auto textArea = block;
    auto labelRow = textArea.removeFromTop(11);
    g.setColour(labelColour);
    g.setFont(makeAppFont(9.7f, AppFontWeight::Regular));
    g.drawFittedText(labels[static_cast<size_t>(i)], labelRow,
                     juce::Justification::centred, 1);

    const auto valueFont = makeAppFont(13.0f, AppFontWeight::SemiBold);
    const float valueWidth =
        measureTextWidth(valueFont, values[static_cast<size_t>(i)]);
    auto valueRow = textArea;
    const float groupWidth = metricsIconSize + static_cast<float>(iconTextGap) + valueWidth;
    const float startX =
        valueRow.toFloat().getCentreX() - (groupWidth * 0.5f);
    const float centerY = valueRow.toFloat().getCentreY();

    drawSvgIcon(g,
                juce::Rectangle<float>(startX,
                                       centerY - (metricsIconSize * 0.5f),
                                       metricsIconSize, metricsIconSize),
                iconName, labelColour.withAlpha(0.95f));

    g.setColour(UITheme::textPrimary);
    g.setFont(valueFont);
    g.drawText(values[static_cast<size_t>(i)],
               juce::Rectangle<float>(startX + metricsIconSize + iconTextGap,
                                      centerY - 10.0f, valueWidth, 20.0f),
               juce::Justification::centredLeft);
  }
}

void SetlistSongsHeaderPanel::resized() {
  auto area = getLocalBounds();
  constexpr int sideInset = 12;
  area = area.reduced(sideInset, 0);
  if (libraryActionVisible) {
    auto actionRow = area.removeFromTop(38);
    const int gap = 8;
    const int actionWidth = juce::jlimit(92, 170, (actionRow.getWidth() - gap) / 2);
    const int actionsWidth = (actionWidth * 2) + gap;
    auto actions = actionRow.withSizeKeepingCentre(actionsWidth, 38);
    addSongsButtonBounds = actions.removeFromLeft(actionWidth);
    actions.removeFromLeft(gap);
    padsButtonBounds = actions.removeFromLeft(actionWidth);
    area.removeFromTop(8);
  } else {
    addSongsButtonBounds = {};
    padsButtonBounds = {};
  }
  metricsPanelBounds = area.removeFromTop(42);

  juce::Grid statsGrid;
  statsGrid.templateColumns = {juce::Grid::TrackInfo(juce::Grid::Fr{1}),
                                juce::Grid::TrackInfo(juce::Grid::Fr{1}),
                                juce::Grid::TrackInfo(juce::Grid::Fr{1})};
  statsGrid.templateRows = {juce::Grid::TrackInfo(juce::Grid::Fr{1})};
  statsGrid.items = {juce::GridItem(metricColumns[0]),
                     juce::GridItem(metricColumns[1]),
                     juce::GridItem(metricColumns[2])};
  statsGrid.performLayout(metricsPanelBounds);
  for (int i = 0; i < 3; ++i)
    metricBlockBounds[static_cast<size_t>(i)] =
        metricColumns[static_cast<size_t>(i)].getBounds();
}

void SetlistSongsHeaderPanel::mouseMove(const juce::MouseEvent &event) {
  updateHoverState(event.position.toInt());
}

void SetlistSongsHeaderPanel::mouseExit(const juce::MouseEvent &) {
  const bool changed = hoveredAddAction || hoveredPadsAction || hoveredBlock >= 0;
  hoveredAddAction = false;
  hoveredPadsAction = false;
  hoveredBlock = -1;
  if (changed)
    repaint();
  setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SetlistSongsHeaderPanel::mouseDown(const juce::MouseEvent &event) {
  const auto p = event.position.toInt();
  pressedAddAction = isAddActionEnabled() && addSongsButtonBounds.contains(p);
  pressedPadsAction = isPadsActionEnabled() && padsButtonBounds.contains(p);
  pressedBlock = blockIndexAt(p);
  repaint();
}

void SetlistSongsHeaderPanel::mouseUp(const juce::MouseEvent &event) {
  const auto p = event.position.toInt();
  const bool fireAction = pressedAddAction && isAddActionEnabled() &&
                          addSongsButtonBounds.contains(p);
  const bool firePads = pressedPadsAction && isPadsActionEnabled() &&
                        padsButtonBounds.contains(p);
  const int releasedBlock = blockIndexAt(p);
  const bool fireBlock = (pressedBlock >= 0 && releasedBlock == pressedBlock &&
                          isBlockClickable(releasedBlock));

  pressedAddAction = false;
  pressedPadsAction = false;
  pressedBlock = -1;
  updateHoverState(p);
  repaint();

  if (fireAction)
    onAddSongsClicked();
  if (firePads)
    onPadsClicked();
  if (fireBlock)
    triggerBlock(releasedBlock);
}

void SetlistSongsHeaderPanel::updateHoverState(const juce::Point<int> p) {
  const bool nextHoveredAdd = isAddActionEnabled() && addSongsButtonBounds.contains(p);
  const bool nextHoveredPads = isPadsActionEnabled() && padsButtonBounds.contains(p);
  const int nextHoveredBlock = blockIndexAt(p);

  if (hoveredAddAction == nextHoveredAdd && hoveredPadsAction == nextHoveredPads &&
      hoveredBlock == nextHoveredBlock)
    return;

  hoveredAddAction = nextHoveredAdd;
  hoveredPadsAction = nextHoveredPads;
  hoveredBlock = nextHoveredBlock;

  if (hoveredAddAction || hoveredPadsAction ||
      (hoveredBlock >= 0 && isBlockClickable(hoveredBlock)))
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  else
    setMouseCursor(juce::MouseCursor::NormalCursor);

  repaint();
}

bool SetlistSongsHeaderPanel::isAddActionEnabled() const {
  return libraryActionVisible && actionsEnabled && static_cast<bool>(onAddSongsClicked);
}

bool SetlistSongsHeaderPanel::isPadsActionEnabled() const {
  return libraryActionVisible && actionsEnabled && static_cast<bool>(onPadsClicked);
}

int SetlistSongsHeaderPanel::blockIndexAt(const juce::Point<int> p) const {
  for (int i = 0; i < 3; ++i) {
    if (metricBlockBounds[static_cast<size_t>(i)].contains(p))
      return i;
  }
  return -1;
}

bool SetlistSongsHeaderPanel::isBlockClickable(const int index) const {
  if (index == 0)
    return static_cast<bool>(onDurationClicked);
  if (index == 1)
    return static_cast<bool>(onSongsClicked);
  if (index == 2)
    return static_cast<bool>(onBpmClicked);
  return false;
}

void SetlistSongsHeaderPanel::triggerBlock(const int index) {
  if (index == 0 && onDurationClicked)
    onDurationClicked();
  else if (index == 1 && onSongsClicked)
    onSongsClicked();
  else if (index == 2 && onBpmClicked)
    onBpmClicked();
}
