#include "WaveformBar.h"

WaveformBar::WaveformBar() {
  editButton.setButtonText("EDITAR");
  editButton.setClickingTogglesState(true);
  editButton.setViewportIgnoreDragFlag(true);
  editButton.onClick = [this] {
    if (onEditToggle)
      onEditToggle(editButton.getToggleState());
  };
  addAndMakeVisible(editButton);
  refreshThemeColours();

  startTimerHz(30);
}

void WaveformBar::setWaveformBins(std::vector<float> bins) {
  waveformBins = std::move(bins);
  repaint();
}

void WaveformBar::setTransportProviders(PositionProvider positionCb,
                                        LengthProvider lengthCb) {
  positionProvider = std::move(positionCb);
  lengthProvider = std::move(lengthCb);
}

void WaveformBar::setSeekCallback(SeekCallback cb) { onSeekTo = std::move(cb); }

void WaveformBar::setOnEditToggle(EditToggleCallback cb) {
  onEditToggle = std::move(cb);
}

void WaveformBar::refreshThemeColours() {
  editButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  editButton.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
  editButton.setColour(juce::TextButton::textColourOffId,
                       UITheme::textPrimary);
  editButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::textPrimary);
  repaint();
}

void WaveformBar::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  if (bounds.getWidth() < 2.0f)
    return;

  drawStudioBackground(g, bounds);
  {
    juce::ColourGradient base(UITheme::panelDark.withAlpha(0.42f),
                              bounds.getCentreX(), bounds.getY(),
                              UITheme::panelDark.withAlpha(0.65f),
                              bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(base);
    g.fillRect(bounds);
  }

  const auto waveBounds = bounds.reduced(0.0f, 8.0f);

  if (waveformBins.empty()) {
    g.setColour(UITheme::cardBg.withAlpha(0.55f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText("Double-click a song to load stems waveform",
                     waveBounds.toNearestInt().reduced(20),
                     juce::Justification::centred, 2);
    g.setColour(UITheme::accent);
    g.fillRect(0.0f, 0.0f, 2.0f, bounds.getHeight());
    return;
  }

  g.setColour(UITheme::dividerSoft.withAlpha(0.22f));
  constexpr int gridCount = 8;
  const float gridStep = 1.0f / static_cast<float>(gridCount);
  for (int i = 1; i < gridCount; ++i) {
    const float x =
        waveBounds.getX() + waveBounds.getWidth() * (i * gridStep);
    g.drawLine(x, waveBounds.getY(), x, waveBounds.getBottom(), 0.6f);
  }

  const float midY = waveBounds.getCentreY();
  const float ampScale = waveBounds.getHeight() * 0.48f;

  juce::Path filledWave;
  const int width = juce::jmax(2, static_cast<int>(bounds.getWidth()));
  const int bins = static_cast<int>(waveformBins.size());
  const auto window = getVisibleWindow();
  const float windowStart = window.first;
  const float windowSpan = window.second;

  for (int x = 0; x < width; ++x) {
    const float t = static_cast<float>(x) / static_cast<float>(width - 1);
    const float index =
        (windowStart + (t * windowSpan)) * static_cast<float>(bins - 1);
    const int i0 = static_cast<int>(index);
    const int i1 = juce::jmin(i0 + 1, bins - 1);
    const float frac = index - static_cast<float>(i0);
    const float amp =
        juce::jmap(frac, waveformBins[static_cast<size_t>(i0)],
                   waveformBins[static_cast<size_t>(i1)]);
    const float yTop = midY - (amp * ampScale);

    if (x == 0)
      filledWave.startNewSubPath(0.0f, yTop);
    else
      filledWave.lineTo(static_cast<float>(x), yTop);
  }

  for (int x = width - 1; x >= 0; --x) {
    const float t = static_cast<float>(x) / static_cast<float>(width - 1);
    const float index =
        (windowStart + (t * windowSpan)) * static_cast<float>(bins - 1);
    const int i0 = static_cast<int>(index);
    const int i1 = juce::jmin(i0 + 1, bins - 1);
    const float frac = index - static_cast<float>(i0);
    const float amp =
        juce::jmap(frac, waveformBins[static_cast<size_t>(i0)],
                   waveformBins[static_cast<size_t>(i1)]);
    const float yBottom = midY + (amp * ampScale);
    filledWave.lineTo(static_cast<float>(x), yBottom);
  }

  filledWave.closeSubPath();

  // Midline
  g.setColour(UITheme::dividerSoft.withAlpha(0.6f));
  g.drawLine(waveBounds.getX(), midY, waveBounds.getRight(), midY, 1.0f);

  // Wave body
  juce::ColourGradient waveFill(UITheme::cardBgHover.withAlpha(0.90f),
                                waveBounds.getCentreX(), waveBounds.getY(),
                                UITheme::cardBg.withAlpha(0.74f),
                                waveBounds.getCentreX(),
                                waveBounds.getBottom(), false);
  g.setGradientFill(waveFill);
  g.fillPath(filledWave);

  // Soft depth shadow
  juce::Path shadow = filledWave;
  shadow.applyTransform(juce::AffineTransform::translation(0.0f, 1.2f));
  g.setColour(UITheme::panelDark.withAlpha(0.18f));
  g.fillPath(shadow);

  float progressRatio = 0.0f;
  if (positionProvider && lengthProvider) {
    const double songLength = juce::jmax(0.0, lengthProvider());
    if (songLength > 0.0) {
      const double songPosition = juce::jmax(0.0, positionProvider());
      progressRatio = static_cast<float>(
          juce::jlimit(0.0, 1.0, songPosition / songLength));
    }
  }

  float playheadRatio =
      (progressRatio - windowStart) / juce::jmax(0.0001f, windowSpan);
  playheadRatio = juce::jlimit(0.0f, 1.0f, playheadRatio);
  const float playheadX =
      waveBounds.getX() + (playheadRatio * waveBounds.getWidth());
  // Progress tint
  if (playheadX > waveBounds.getX()) {
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(waveBounds.withRight(playheadX).toNearestInt());
    juce::ColourGradient progressFill(
        UITheme::accent.withAlpha(0.36f), waveBounds.getCentreX(),
        waveBounds.getY(), UITheme::accentSoft.withAlpha(0.18f),
        waveBounds.getCentreX(), waveBounds.getBottom(), false);
    g.setGradientFill(progressFill);
    g.fillPath(filledWave);
  }

  // Crisp outline + highlight ridge
  g.setColour(UITheme::cardBorder.withAlpha(0.7f));
  g.strokePath(filledWave, juce::PathStrokeType(1.0f));
  g.setColour(UITheme::cardBgHover.withAlpha(0.55f));
  g.strokePath(filledWave, juce::PathStrokeType(0.6f));

  g.setColour(UITheme::accent);
  g.drawLine(playheadX, waveBounds.getY(), playheadX, waveBounds.getBottom(),
             1.4f);
}

void WaveformBar::resized() {
  auto area = getLocalBounds();
  const int buttonW = 64;
  const int buttonH = 22;
  editButton.setBounds(area.removeFromTop(buttonH)
                           .removeFromRight(buttonW)
                           .reduced(2));
}

void WaveformBar::mouseDown(const juce::MouseEvent &event) {
  handleSeek(event.position.x);
}

void WaveformBar::mouseDrag(const juce::MouseEvent &event) {
  handleSeek(event.position.x);
}

void WaveformBar::mouseWheelMove(const juce::MouseEvent &event,
                                 const juce::MouseWheelDetails &wheel) {
  const auto mods = event.mods;
  if (!mods.isCtrlDown() && !mods.isCommandDown())
    return;
  const float dir = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
  if (dir == 0.0f)
    return;
  applyZoom(dir > 0.0f ? 1.1f : 1.0f / 1.1f);
}

void WaveformBar::mouseMagnify(const juce::MouseEvent &,
                               float magnifyAmount) {
  if (magnifyAmount == 0.0f)
    return;
  applyZoom(1.0f + magnifyAmount);
}

void WaveformBar::timerCallback() { repaint(); }

void WaveformBar::handleSeek(float x) {
  if (!onSeekTo || !lengthProvider)
    return;
  const double songLength = juce::jmax(0.0, lengthProvider());
  if (songLength <= 0.0)
    return;

  const auto bounds = getLocalBounds().toFloat();
  const float clampedX = juce::jlimit(bounds.getX(), bounds.getRight(), x);
  const float ratio =
      bounds.getWidth() > 0.0f ? (clampedX - bounds.getX()) / bounds.getWidth()
                               : 0.0f;
  const auto window = getVisibleWindow();
  const double target =
      songLength * juce::jlimit(0.0f, 1.0f,
                                window.first + (ratio * window.second));
  onSeekTo(target);
}

std::pair<float, float> WaveformBar::getVisibleWindow() const {
  const float span = 1.0f / juce::jlimit(1.0f, 8.0f, zoom);
  float center = 0.5f;
  if (positionProvider && lengthProvider) {
    const double songLength = juce::jmax(0.0, lengthProvider());
    if (songLength > 0.0) {
      const double songPosition = juce::jmax(0.0, positionProvider());
      center = static_cast<float>(
          juce::jlimit(0.0, 1.0, songPosition / songLength));
    }
  }
  float start = center - (span * 0.5f);
  if (start < 0.0f)
    start = 0.0f;
  if (start + span > 1.0f)
    start = 1.0f - span;
  return {start, span};
}

void WaveformBar::applyZoom(float factor) {
  zoom = juce::jlimit(1.0f, 8.0f, zoom * factor);
  repaint();
}
