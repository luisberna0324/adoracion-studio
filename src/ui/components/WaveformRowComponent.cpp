#include "WaveformRowComponent.h"
#include "ChannelStripLookAndFeel.h"

// --- Subcomponents ---

class WaveformRowComponent::Artwork : public juce::Component {
public:
  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    // Background radial gradient
    juce::Colour colourCenter = juce::Colour::fromRGB(45, 60, 80);
    juce::Colour colourEdge = juce::Colour::fromRGB(20, 25, 35);
    juce::ColourGradient grad(colourCenter, bounds.getCentre(), colourEdge,
                              bounds.getBottomRight(), true);

    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Decorative curved lines
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    juce::Path p;
    p.startNewSubPath(0, bounds.getHeight() * 0.7f);
    p.quadraticTo(bounds.getWidth() * 0.5f, bounds.getHeight() * 0.3f,
                  bounds.getWidth(), bounds.getHeight() * 0.8f);
    g.strokePath(p, juce::PathStrokeType(1.5f));

    // Decorative circle
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawEllipse(bounds.getWidth() * 0.6f, bounds.getHeight() * 0.2f, 15.0f,
                  15.0f, 1.5f);
  }
};

class WaveformRowComponent::PlayButton : public juce::Component {
public:
  bool isPlaying = false;
  std::function<void()> onClick;

  PlayButton() { setOpaque(false); }

  void mouseDown(const juce::MouseEvent &) override {
    isPlaying = !isPlaying;
    if (onClick)
      onClick();
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    if (isPlaying) {
      // Active background with halo
      g.setColour(juce::Colour::fromRGBA(193, 122, 90, 60)); // Halo
      g.fillEllipse(bounds);
      g.setColour(juce::Colour::fromRGB(
          193, 122, 90)); // Terracota #C17A5A ≈ rgb(193,122,90)
      g.fillEllipse(bounds.reduced(2.0f));

      // Pause Icon
      g.setColour(juce::Colours::white);
      g.fillRoundedRectangle(bounds.getCentreX() - 4.0f,
                             bounds.getCentreY() - 5.0f, 3.0f, 10.0f, 1.0f);
      g.fillRoundedRectangle(bounds.getCentreX() + 1.0f,
                             bounds.getCentreY() - 5.0f, 3.0f, 10.0f, 1.0f);
    } else {
      // Inactive background
      g.setColour(juce::Colour::fromRGB(200, 200, 200)); // #c8c8c8
      g.fillEllipse(bounds.reduced(2.0f));

      // Play Triangle
      juce::Path p;
      p.addTriangle(bounds.getCentreX() - 2.0f, bounds.getCentreY() - 5.0f,
                    bounds.getCentreX() - 2.0f, bounds.getCentreY() + 5.0f,
                    bounds.getCentreX() + 5.0f, bounds.getCentreY());
      g.setColour(juce::Colour::fromRGB(42, 42, 42));
      g.fillPath(p);
    }
  }
};

class WaveformRowComponent::WaveformDisplay : public juce::Component,
                                              private juce::Timer {
public:
  float playheadPos = 0.0f; // 0.0 to 1.0
  float hoverPos = -1.0f;   // 0.0 to 1.0, -1 means no hover
  std::vector<float> amplitudes;
  bool isPlaying = false;

  WaveformDisplay() {
    // Generate dummy amplitudes for 120 bars
    auto &rng = juce::Random::getSystemRandom();
    for (int i = 0; i < 120; ++i) {
      float phase = (float)i / 120.0f;
      float envelope = std::sin(phase * juce::MathConstants<float>::pi);
      float noise = rng.nextFloat() * 0.4f;
      // minimum height 5px (will be scaled during paint)
      amplitudes.push_back(0.1f + envelope * 0.5f + noise);
    }
    startTimerHz(60);
  }

  void timerCallback() override {
    if (isPlaying) {
      playheadPos += 0.001f;
      if (playheadPos > 1.0f)
        playheadPos = 0.0f;
      repaint();
    }
  }

  void mouseMove(const juce::MouseEvent &e) override {
    hoverPos = (float)e.x / (float)getWidth();
    repaint();
  }

  void mouseExit(const juce::MouseEvent &) override {
    hoverPos = -1.0f;
    repaint();
  }

  void mouseDown(const juce::MouseEvent &e) override {
    playheadPos = juce::jlimit(0.0f, 1.0f, (float)e.x / (float)getWidth());
    repaint();
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    playheadPos = juce::jlimit(0.0f, 1.0f, (float)e.x / (float)getWidth());
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    int numBars = 120;
    float barWidth = 5.0f;
    float barGap = 2.0f;
    float totalWidth = numBars * (barWidth + barGap) - barGap;

    float startX = (bounds.getWidth() - totalWidth) * 0.5f;

    // Colors
    juce::Colour colorPlayedPeak =
        juce::Colour::fromRGB(193, 122, 90); // #C17A5A
    juce::Colour colorPlayedBase =
        juce::Colour::fromRGB(196, 154, 108); // #C49A6C
    juce::Colour colorPending = juce::Colour::fromRGB(42, 42, 42)
                                    .withAlpha(0.6f); // #2a2a2a reduced opacity

    for (int i = 0; i < numBars; ++i) {
      float x = startX + i * (barWidth + barGap);
      float normX = (float)i / (float)numBars;

      float amp = amplitudes[static_cast<size_t>(i)];
      float height = juce::jmax(5.0f, bounds.getHeight() * amp);
      float y = bounds.getCentreY() - height * 0.5f;

      juce::Rectangle<float> rect(x, y, barWidth, height);

      bool isPlayed = normX <= playheadPos;
      juce::Colour c = isPlayed
                           ? (amp > 0.6f ? colorPlayedPeak : colorPlayedBase)
                           : colorPending;

      if (hoverPos >= 0.0f) {
        if (normX < hoverPos)
          c = c.brighter(0.2f);
        else
          c = c.darker(0.2f);
      }

      g.setColour(c);
      g.fillRoundedRectangle(rect, 1.5f);
    }

    // Playhead
    float pX = startX + playheadPos * totalWidth;
    g.setColour(colorPlayedPeak);
    g.drawLine(pX, 0, pX, bounds.getHeight(), 1.5f);

    // Triangle top
    juce::Path p;
    p.addTriangle(pX - 4.0f, 0.0f, pX + 4.0f, 0.0f, pX, 7.0f);
    g.fillPath(p);

    // Hover line
    if (hoverPos >= 0.0f) {
      float hX = startX + hoverPos * totalWidth;
      float dashLengths[] = {2.0f, 2.0f};
      g.setColour(juce::Colours::grey.withAlpha(0.7f));
      g.drawDashedLine(juce::Line<float>(hX, 0, hX, bounds.getHeight()),
                       dashLengths, 2, 1.0f);
    }

    // Edge Gradients
    juce::Colour bg = juce::Colour::fromRGB(212, 212, 212); // #D4D4D4
    juce::ColourGradient leftGrad(bg, 0, 0, bg.withAlpha(0.0f), 16.0f, 0,
                                  false);
    g.setGradientFill(leftGrad);
    g.fillRect(0.0f, 0.0f, 16.0f, bounds.getHeight());

    juce::ColourGradient rightGrad(bg, bounds.getWidth(), 0, bg.withAlpha(0.0f),
                                   bounds.getWidth() - 16.0f, 0, false);
    g.setGradientFill(rightGrad);
    g.fillRect(bounds.getWidth() - 16.0f, 0.0f, 16.0f, bounds.getHeight());
  }
};

// --- WaveformRowComponent ---

WaveformRowComponent::WaveformRowComponent(ChannelStripLookAndFeel &lookAndFeel,
                                           juce::String trackName,
                                           juce::String trackTime)
    : lnf(lookAndFeel) {
  artwork = std::make_unique<Artwork>();
  addAndMakeVisible(*artwork);

  nameLabel.setText(std::move(trackName), juce::dontSendNotification);
  nameLabel.setFont(
      juce::FontOptions(11.0f, juce::Font::bold).withKerningFactor(0.03f));
  nameLabel.setColour(juce::Label::textColourId,
                      juce::Colour::fromRGB(42, 42, 42)); // #2a2a2a
  addAndMakeVisible(nameLabel);

  timeLabel.setText(std::move(trackTime), juce::dontSendNotification);
  timeLabel.setFont(juce::FontOptions(8.0f));
  timeLabel.setColour(juce::Label::textColourId,
                      juce::Colour::fromRGBA(119, 119, 119, 136)); // #77777788
  addAndMakeVisible(timeLabel);

  playButton = std::make_unique<PlayButton>();
  playButton->onClick = [this]() {
    waveform->isPlaying = playButton->isPlaying;
  };
  addAndMakeVisible(*playButton);

  waveform = std::make_unique<WaveformDisplay>();
  addAndMakeVisible(*waveform);

  volSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  volSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volSlider.setRange(0.0, 1.0, 0.0);
  volSlider.setValue(0.7);
  volSlider.setLookAndFeel(&lnf);
  addAndMakeVisible(volSlider);

  volLabel.setText("VOL", juce::dontSendNotification);
  volLabel.setFont(juce::FontOptions(6.0f));
  volLabel.setJustificationType(juce::Justification::centred);
  volLabel.setColour(juce::Label::textColourId,
                     juce::Colour::fromRGB(42, 42, 42));
  addAndMakeVisible(volLabel);
}

WaveformRowComponent::~WaveformRowComponent() {
  volSlider.setLookAndFeel(nullptr);
}

void WaveformRowComponent::setTrackInfo(juce::String trackName,
                                        juce::String trackTime) {
  nameLabel.setText(std::move(trackName), juce::dontSendNotification);
  timeLabel.setText(std::move(trackTime), juce::dontSendNotification);
}

int WaveformRowComponent::getTargetHeight() const {
  return kRowHeight;
}

void WaveformRowComponent::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds().toFloat();

  // Background and border for the row
  g.setColour(juce::Colour::fromRGB(212, 212, 212)); // #D4D4D4
  g.fillRoundedRectangle(bounds, 6.0f);

  g.setColour(juce::Colour::fromRGB(200, 200, 200)); // subtle border
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  g.setColour(juce::Colours::black.withAlpha(0.08f));
  g.fillRect(12.0f, bounds.getBottom() - 1.0f, bounds.getWidth() - 24.0f, 1.0f);
}

void WaveformRowComponent::resized() {
  int paddingX = 14;
  int paddingY = 10;

  artwork->setBounds(paddingX, paddingY, 52, 52);

  int infoX = artwork->getRight() + 10;
  nameLabel.setBounds(infoX, paddingY + 8, 108, 14);
  timeLabel.setBounds(infoX, nameLabel.getBottom() + 4, 108, 12);

  int playX = infoX + 108 + 15;
  playButton->setBounds(playX, 21, 30, 30);

  int rightEdge = getWidth() - paddingX;
  int knobX = rightEdge - 28;
  volSlider.setBounds(knobX, 18, 28, 28);
  volLabel.setBounds(knobX - 5, volSlider.getBottom() + 1, 38, 10);

  int waveX = playButton->getRight() + 15;
  int waveWidth = juce::jmax(0, knobX - 15 - waveX);
  waveform->setBounds(waveX, (kRowHeight - 58) / 2, waveWidth, 58);
}
