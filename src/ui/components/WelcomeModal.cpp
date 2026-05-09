#include "WelcomeModal.h"
#include "BinaryData.h"
#include "../../data/Localization.h"

namespace {
constexpr const char *kSubscriptionInfoUrl = "https://playwordship.gumroad.com/";

class WelcomePillLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  enum class Style { Primary, Secondary };
  explicit WelcomePillLookAndFeel(Style s) : style(s) {}

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool isMouseOverButton,
                            bool isButtonDown) override {
    juce::ignoreUnused(backgroundColour);
    auto r = button.getLocalBounds().toFloat().reduced(0.5f, 1.0f);
    const float rad = r.getHeight() * 0.5f;
    if (style == Style::Primary) {
      auto fill = juce::Colour(0xfff5f5f7);
      if (isButtonDown)
        fill = fill.darker(0.14f);
      else if (isMouseOverButton)
        fill = juce::Colour(0xffffffff);
      g.setColour(fill);
      g.fillRoundedRectangle(r, rad);
      g.setColour(juce::Colour(0xff000000).withAlpha(0.06f));
      g.drawRoundedRectangle(r.reduced(0.5f), rad - 0.25f, 1.0f);
    } else {
      auto fill = juce::Colour(0x00000000);
      if (isButtonDown)
        fill = juce::Colour(0xff2a2a30).withAlpha(0.9f);
      else if (isMouseOverButton)
        fill = juce::Colour(0xff34343c).withAlpha(0.55f);
      g.setColour(fill);
      g.fillRoundedRectangle(r, rad);
      const auto border = juce::Colour(0xff5c5c66);
      g.setColour(border.withMultipliedAlpha(isMouseOverButton ? 1.1f : 0.85f));
      g.drawRoundedRectangle(r.reduced(0.5f), rad - 0.25f, 1.0f);
    }
  }

  juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override {
    const float px =
        juce::jlimit(15.0f, 17.0f, static_cast<float>(buttonHeight) * 0.34f);
    return juce::Font(
        juce::FontOptions(px).withStyle(style == Style::Primary ? "Bold"
                                                                 : "Regular"));
  }

private:
  Style style;
};
} // namespace

WelcomeModal::WelcomeModal() {
  setOpaque(false);

  primaryLaf = std::make_unique<WelcomePillLookAndFeel>(
      WelcomePillLookAndFeel::Style::Primary);
  secondaryLaf = std::make_unique<WelcomePillLookAndFeel>(
      WelcomePillLookAndFeel::Style::Secondary);

  logoImage = juce::ImageFileFormat::loadFrom(
      BinaryData::pwLogoSinFondo_png, BinaryData::pwLogoSinFondo_pngSize);
  if (logoImage.isValid()) {
    logo.setOpaque(false);
    addAndMakeVisible(logo);
  }

  auto clearLabelBg = [](juce::Label &l) {
    l.setOpaque(false);
    l.setColour(juce::Label::backgroundColourId,
                juce::Colours::transparentBlack);
    l.setColour(juce::Label::outlineColourId,
                juce::Colours::transparentBlack);
  };

  title.setText(
      L10n::tr("Te damos la bienvenida\na PlayWorship",
               "Welcome to\nPlayWorship"),
      juce::dontSendNotification);
  title.setJustificationType(juce::Justification::centred);
  title.setFont(juce::FontOptions(30.0f).withStyle("Bold"));
  title.setMinimumHorizontalScale(0.82f);
  title.setColour(juce::Label::textColourId,
                  juce::Colour::fromRGB(252, 252, 255));
  addAndMakeVisible(title);
  clearLabelBg(title);

  subtitle.setText(
      L10n::tr(u8"Inicia sesión para continuar con tu biblioteca y setlists.",
               "Sign in to continue with your library and setlists."),
      juce::dontSendNotification);
  subtitle.setJustificationType(juce::Justification::centred);
  subtitle.setFont(juce::FontOptions(15.0f));
  subtitle.setColour(juce::Label::textColourId,
                     juce::Colour::fromRGB(168, 170, 184));
  addAndMakeVisible(subtitle);
  clearLabelBg(subtitle);

  status.setText(
      L10n::tr("Pulsa Ingresar para abrir el navegador.",
               "Press Sign in to open the browser."),
      juce::dontSendNotification);
  status.setJustificationType(juce::Justification::centred);
  status.setFont(juce::FontOptions(13.0f));
  status.setColour(juce::Label::textColourId,
                   juce::Colour::fromRGB(146, 148, 160));
  addAndMakeVisible(status);
  clearLabelBg(status);

  loginButton.setButtonText(L10n::tr("Ingresar", "Sign in"));
  loginButton.setLookAndFeel(primaryLaf.get());
  loginButton.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::transparentWhite);
  loginButton.setColour(juce::TextButton::buttonOnColourId,
                        juce::Colours::transparentWhite);
  loginButton.setColour(juce::TextButton::textColourOffId,
                        juce::Colour::fromRGB(18, 18, 22));
  loginButton.setColour(juce::TextButton::textColourOnId,
                        juce::Colour::fromRGB(18, 18, 22));
  loginButton.onClick = [this] {
    if (onLoginRequested)
      onLoginRequested();
  };
  addAndMakeVisible(loginButton);

  moreInfoButton.setButtonText(
      L10n::tr(u8"Más información", "More information"));
  moreInfoButton.setLookAndFeel(secondaryLaf.get());
  moreInfoButton.setColour(juce::TextButton::buttonColourId,
                           juce::Colours::transparentWhite);
  moreInfoButton.setColour(juce::TextButton::buttonOnColourId,
                           juce::Colours::transparentWhite);
  moreInfoButton.setColour(juce::TextButton::textColourOffId,
                           juce::Colour::fromRGB(236, 236, 242));
  moreInfoButton.setColour(juce::TextButton::textColourOnId,
                           juce::Colour::fromRGB(252, 252, 255));
  moreInfoButton.onClick = [] {
    juce::URL(kSubscriptionInfoUrl).launchInDefaultBrowser();
  };
  addAndMakeVisible(moreInfoButton);
}

WelcomeModal::~WelcomeModal() {
  loginButton.setLookAndFeel(nullptr);
  moreInfoButton.setLookAndFeel(nullptr);
}

void WelcomeModal::setStatusText(const juce::String &text, bool isError) {
  status.setText(text, juce::dontSendNotification);
  status.setColour(juce::Label::textColourId,
                   isError ? juce::Colour::fromRGB(255, 164, 164)
                           : juce::Colour::fromRGB(146, 148, 160));
}

void WelcomeModal::resetStatusToDefault() {
  setStatusText(L10n::tr("Pulsa Ingresar para abrir el navegador.",
                         "Press Sign in to open the browser."),
                false);
  setChecking(false);
}

void WelcomeModal::setChecking(bool isChecking) {
  loginButton.setVisible(!isChecking);
  moreInfoButton.setVisible(!isChecking);
  subtitle.setVisible(!isChecking);
}

void WelcomeModal::paint(juce::Graphics &) {}

void WelcomeModal::resized() {
  const int sidePad = juce::jlimit(36, 56, getWidth() / 9);
  const int topPad = juce::jlimit(32, 48, getHeight() / 11);
  auto area = getLocalBounds().reduced(sidePad, topPad);
  const int maxCol = 420;
  const int colW = juce::jmin(maxCol, area.getWidth());
  auto column = area.withSizeKeepingCentre(colW, area.getHeight());

  if (logoImage.isValid()) {
    auto logoArea = column.removeFromTop(160);
    const int logoW = juce::jmin(400, logoArea.getWidth());
    const int logoH = logoArea.getHeight();
    logo.setImage(logoImage, juce::RectanglePlacement::centred);
    logo.setBounds(logoArea.withSizeKeepingCentre(logoW, logoH));
    column.removeFromTop(24);
  } else {
    column.removeFromTop(8);
  }

  title.setBounds(column.removeFromTop(72));
  column.removeFromTop(12);
  subtitle.setBounds(column.removeFromTop(52));
  column.removeFromTop(20);
  status.setBounds(column.removeFromTop(38));
  column.removeFromTop(26);

  const int btnH = 48;
  loginButton.setBounds(column.removeFromTop(btnH));
  column.removeFromTop(12);
  moreInfoButton.setBounds(column.removeFromTop(btnH));
}
