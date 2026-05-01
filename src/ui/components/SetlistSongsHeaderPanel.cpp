#include "SetlistSongsHeaderPanel.h"
#include "../../data/Localization.h"

namespace {

void drawPlus(juce::Graphics &g, const juce::Rectangle<float> &r,
              const juce::Colour colour) {
  g.setColour(colour);
  const float cx = r.getCentreX();
  const float cy = r.getCentreY();
  g.fillRoundedRectangle(cx - 1.0f, cy - 6.0f, 2.0f, 12.0f, 0.8f);
  g.fillRoundedRectangle(cx - 6.0f, cy - 1.0f, 12.0f, 2.0f, 0.8f);
}

void drawCaretDown(juce::Graphics &g, const juce::Rectangle<float> &r,
                   const juce::Colour colour) {
  g.setColour(colour);
  juce::Path p;
  p.startNewSubPath(r.getX(), r.getY() + 1.0f);
  p.lineTo(r.getRight(), r.getY() + 1.0f);
  p.lineTo(r.getCentreX(), r.getBottom() - 1.0f);
  p.closeSubPath();
  g.fillPath(p);
}

} // namespace

SetlistSongsHeaderPanel::SetlistSongsHeaderPanel()
    : addText(L10n::tr("Agregar al setlist", "Add to setlist")) {
  metricBlockBounds.fill({});
}

void SetlistSongsHeaderPanel::setAddText(const juce::String &text) {
  const auto next = text.trim();
  if (next == addText)
    return;
  addText = next;
  repaint(addButtonBounds);
}

void SetlistSongsHeaderPanel::setSummary(const Summary &nextSummary) {
  if (summary.duration == nextSummary.duration &&
      summary.songsCount == nextSummary.songsCount &&
      summary.averageBpm == nextSummary.averageBpm)
    return;
  summary = nextSummary;
  repaint(metricsPanelBounds);
}

void SetlistSongsHeaderPanel::setOnAddClicked(std::function<void()> cb) {
  onAddClicked = std::move(cb);
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

void SetlistSongsHeaderPanel::setAddEnabled(const bool enabled) {
  if (addEnabled == enabled)
    return;
  addEnabled = enabled;
  addPressed = false;
  repaint(addButtonBounds);
}

juce::Rectangle<int> SetlistSongsHeaderPanel::getAddButtonScreenBounds() const {
  return localAreaToGlobal(addButtonBounds);
}

void SetlistSongsHeaderPanel::paint(juce::Graphics &g) {
  auto addFill = UITheme::accent;
  if (!addEnabled)
    addFill = addFill.withMultipliedSaturation(0.20f).withAlpha(0.45f);
  else if (addPressed)
    addFill = addFill.darker(0.18f);
  else if (addHovered)
    addFill = addFill.brighter(0.07f);

  auto addRect = addButtonBounds.toFloat();
  if (addEnabled && addPressed)
    addRect = addRect.withSizeKeepingCentre(addRect.getWidth() * 0.98f,
                                            addRect.getHeight() * 0.98f);

  g.setColour(addFill);
  g.fillRoundedRectangle(addRect, 10.0f);

  const auto buttonTextColour =
      UITheme::onAccent.withAlpha(addEnabled ? 1.0f : 0.7f);
  const auto content = addRect.reduced(12.0f, 0.0f);
  const float plusSize = 16.0f;
  const float caretSize = 12.0f;
  const auto font = makeAppFont(14.0f, AppFontWeight::SemiBold);

  const float textWidth = font.getStringWidthFloat(addText);
  const float addGroupWidth = plusSize + 8.0f + textWidth + 8.0f + caretSize;
  const float addStartX = content.getCentreX() - addGroupWidth * 0.5f;
  const float addCenterY = content.getCentreY();

  drawPlus(g,
           juce::Rectangle<float>(addStartX, addCenterY - plusSize * 0.5f, plusSize,
                                  plusSize),
           buttonTextColour);

  g.setColour(buttonTextColour);
  g.setFont(font);
  g.drawText(addText,
             juce::Rectangle<float>(addStartX + plusSize + 8.0f,
                                    addCenterY - 12.0f, textWidth, 24.0f),
             juce::Justification::centred);

  drawCaretDown(g,
                juce::Rectangle<float>(addStartX + plusSize + 8.0f + textWidth + 8.0f,
                                       addCenterY - caretSize * 0.5f, caretSize,
                                       caretSize),
                buttonTextColour.withAlpha(0.7f));

  const auto panel = metricsPanelBounds.toFloat();
  g.setColour(juce::Colour::fromRGB(28, 28, 30));
  g.fillRoundedRectangle(panel, 12.0f);
  g.setColour(juce::Colours::white.withAlpha(0.05f));
  g.drawRoundedRectangle(panel, 12.0f, 1.0f);

  g.setColour(juce::Colours::white.withAlpha(0.08f));
  for (int i = 0; i < 2; ++i) {
    const auto &left = metricBlockBounds[static_cast<size_t>(i)];
    const float x = static_cast<float>(left.getRight());
    const float y0 = panel.getY() + 16.0f;
    const float y1 = panel.getBottom() - 16.0f;
    g.drawLine(x, y0, x, y1, 1.0f);
  }

  const bool hideLabels = getWidth() < 320;
  const bool valuesOnly = getWidth() < 280;
  const auto labelColour = juce::Colour::fromRGB(138, 138, 138);
  constexpr float metricsIconSize = 36.0f;
  constexpr int iconTextGap = 8;

  const std::array<juce::String, 3> labels = {
      L10n::tr("Duracion total", "Total duration"),
      L10n::tr("Canciones", "Songs"),
      L10n::tr("Prom. BPM", "Avg. BPM")};
  const std::array<juce::String, 3> values = {
      summary.duration,
      juce::String(juce::jmax(0, summary.songsCount)),
      juce::String(juce::jmax(0, summary.averageBpm))};

  for (int i = 0; i < 3; ++i) {
    auto block = metricBlockBounds[static_cast<size_t>(i)].reduced(8, 5);
    const bool clickable = isBlockClickable(i);
    const bool hovered = hoveredBlock == i && clickable;
    const auto iconGlyph = (i == 0) ? IconGlyph::Clock
                         : (i == 1) ? IconGlyph::MusicNote
                                    : IconGlyph::Waveform;

    if (hovered) {
      g.setColour(UITheme::cardBgHover.withAlpha(0.12f));
      g.fillRoundedRectangle(block.toFloat(), 8.0f);
    }

    if (valuesOnly) {
      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
      g.drawFittedText(values[static_cast<size_t>(i)], block,
                       juce::Justification::centred, 1);
      continue;
    }

    if (hideLabels) {
      const auto valueFont = makeAppFont(14.0f, AppFontWeight::SemiBold);
      const float valueWidth =
          valueFont.getStringWidthFloat(values[static_cast<size_t>(i)]);
      const float groupWidth = metricsIconSize + static_cast<float>(iconTextGap) + valueWidth;
      const float startX = static_cast<float>(block.getCentreX()) - (groupWidth * 0.5f);
      const float centerY = static_cast<float>(block.getCentreY());

      drawIconGlyph(
          g,
          juce::Rectangle<float>(startX, centerY - (metricsIconSize * 0.5f),
                                 metricsIconSize, metricsIconSize),
          iconGlyph, labelColour.withAlpha(0.95f), 1.45f);

      g.setColour(UITheme::textPrimary);
      g.setFont(valueFont);
      g.drawText(values[static_cast<size_t>(i)],
                 juce::Rectangle<float>(startX + metricsIconSize + iconTextGap,
                                        centerY - 11.0f, valueWidth, 22.0f),
                 juce::Justification::centredLeft);
      continue;
    }

    auto textArea = block;
    auto labelRow = textArea.removeFromTop(14);
    g.setColour(labelColour);
    g.setFont(makeAppFont(11.0f, AppFontWeight::Regular));
    g.drawFittedText(labels[static_cast<size_t>(i)], labelRow,
                     juce::Justification::centred, 1);

    const auto valueFont = makeAppFont(14.0f, AppFontWeight::SemiBold);
    const float valueWidth =
        valueFont.getStringWidthFloat(values[static_cast<size_t>(i)]);
    auto valueRow = textArea;
    const float groupWidth = metricsIconSize + static_cast<float>(iconTextGap) + valueWidth;
    const float startX =
        valueRow.toFloat().getCentreX() - (groupWidth * 0.5f);
    const float centerY = valueRow.toFloat().getCentreY();

    drawIconGlyph(
        g,
        juce::Rectangle<float>(startX, centerY - (metricsIconSize * 0.5f),
                               metricsIconSize, metricsIconSize),
        iconGlyph, labelColour.withAlpha(0.95f), 1.45f);

    g.setColour(UITheme::textPrimary);
    g.setFont(valueFont);
    g.drawText(values[static_cast<size_t>(i)],
               juce::Rectangle<float>(startX + metricsIconSize + iconTextGap,
                                      centerY - 11.0f, valueWidth, 22.0f),
               juce::Justification::centredLeft);
  }
}

void SetlistSongsHeaderPanel::resized() {
  auto area = getLocalBounds();
  constexpr int sideInset = 12;
  area = area.reduced(sideInset, 0);
  addButtonBounds = area.removeFromTop(44);
  area.removeFromTop(12);
  metricsPanelBounds = area.removeFromTop(56);

  const int blockWidth = metricsPanelBounds.getWidth() / 3;
  for (int i = 0; i < 3; ++i) {
    metricBlockBounds[static_cast<size_t>(i)] = juce::Rectangle<int>(
        metricsPanelBounds.getX() + i * blockWidth, metricsPanelBounds.getY(),
        i == 2 ? metricsPanelBounds.getRight() - (metricsPanelBounds.getX() + i * blockWidth)
               : blockWidth,
        metricsPanelBounds.getHeight());
  }
}

void SetlistSongsHeaderPanel::mouseMove(const juce::MouseEvent &event) {
  updateHoverState(event.position.toInt());
}

void SetlistSongsHeaderPanel::mouseExit(const juce::MouseEvent &) {
  const bool changed = addHovered || hoveredBlock >= 0;
  addHovered = false;
  hoveredBlock = -1;
  if (changed)
    repaint();
  setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SetlistSongsHeaderPanel::mouseDown(const juce::MouseEvent &event) {
  const auto p = event.position.toInt();
  addPressed = addEnabled && addButtonBounds.contains(p);
  pressedBlock = blockIndexAt(p);
  repaint();
}

void SetlistSongsHeaderPanel::mouseUp(const juce::MouseEvent &event) {
  const auto p = event.position.toInt();
  const bool fireAdd = addPressed && addEnabled && addButtonBounds.contains(p);
  const int releasedBlock = blockIndexAt(p);
  const bool fireBlock = (pressedBlock >= 0 && releasedBlock == pressedBlock &&
                          isBlockClickable(releasedBlock));

  addPressed = false;
  pressedBlock = -1;
  updateHoverState(p);
  repaint();

  if (fireAdd && onAddClicked)
    onAddClicked();
  if (fireBlock)
    triggerBlock(releasedBlock);
}

void SetlistSongsHeaderPanel::updateHoverState(const juce::Point<int> p) {
  const bool nextAddHovered = addEnabled && addButtonBounds.contains(p);
  const int nextHoveredBlock = blockIndexAt(p);

  if (addHovered == nextAddHovered && hoveredBlock == nextHoveredBlock)
    return;

  addHovered = nextAddHovered;
  hoveredBlock = nextHoveredBlock;

  if (addHovered || (hoveredBlock >= 0 && isBlockClickable(hoveredBlock)))
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  else
    setMouseCursor(juce::MouseCursor::NormalCursor);

  repaint();
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
