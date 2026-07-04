#include "WaveformBar.h"
#include "../../PluginProcessor.h"
#include "../../audio/DynamicGuideEngine.h"
#include "../../data/Localization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

namespace {
constexpr double kMinPartDurationSec = 0.25;
constexpr double kPartSnapSeconds = 0.1;
constexpr float kMinViewportSpan = 1.0f / 18.0f;
constexpr float kEditToolbarHeight = 24.0f;
constexpr float kEditToolbarGap = 4.0f;
constexpr float kSeekRulerHeight = 28.0f;
constexpr float kSeekRulerGap = 4.0f;
constexpr float kMiniMapHeight = 24.0f;
constexpr float kMiniMapGap = 10.0f;
constexpr float kPartHandleHitWidth = 12.0f;
constexpr float kViewportHandleHitWidth = 9.0f;
constexpr int kSnapModeBarId = 1;
constexpr int kSnapModeBeatId = 2;
constexpr int kSnapModeHalfBeatId = 3;
constexpr int kSnapModeFreeId = 4;
constexpr double kSongLoadingMinVisibleMs = 180.0;
constexpr double kFollowSuspendAfterManualViewportMs = 2500.0;
constexpr float kFollowEnabledViewportSpan = 0.25f;
constexpr float kFollowCenterRatio = 0.5f;
constexpr float kFollowSmoothFactor = 0.25f;
constexpr float kWheelPanSensitivity = 0.22f;
constexpr float kPlayheadHeadHeight = 9.5f;
constexpr float kPlayheadHeadHalfWidth = 5.8f;
constexpr float kPlayheadHeadInsetY = 1.5f;
constexpr float kPlayheadNeckHalfWidth = 1.5f;

float snapToPixelCenter(const float value) {
  return std::floor(value) + 0.5f;
}

juce::Colour playheadPrimaryColour() {
  return UITheme::accent;
}

juce::Colour playheadShadowColour() {
  return UITheme::accent.darker(0.85f);
}

void drawPlayheadNeedle(juce::Graphics &g, const juce::Rectangle<float> &bounds,
                        float x, const bool drawHead, const bool headAtBottom,
                        const float alpha = 1.0f) {
  if (bounds.getWidth() <= 1.0f || bounds.getHeight() <= 1.0f)
    return;

  x = snapToPixelCenter(x);
  const auto coreColour = playheadPrimaryColour().withAlpha(alpha);
  const auto shadowColour = playheadShadowColour().withAlpha(0.9f * alpha);

  const float headBottom = headAtBottom
                               ? (bounds.getBottom() - kPlayheadHeadInsetY)
                               : juce::jmin(bounds.getBottom() - 1.0f,
                                            bounds.getY() + kPlayheadHeadInsetY +
                                                kPlayheadHeadHeight);
  const float headTop = juce::jmax(bounds.getY() + 1.0f, headBottom - kPlayheadHeadHeight);
  const float lineStartY =
      (drawHead && !headAtBottom) ? (headBottom - 0.5f) : (bounds.getY() + 1.0f);
  const float lineEndY =
      (drawHead && headAtBottom) ? (headTop + 0.5f) : bounds.getBottom();

  g.setColour(shadowColour.withAlpha(0.26f * alpha));
  g.drawLine(x - 1.0f, lineStartY, x - 1.0f, lineEndY, 1.0f);
  g.drawLine(x + 1.0f, lineStartY, x + 1.0f, lineEndY, 1.0f);
  g.setColour(shadowColour.withAlpha(0.78f * alpha));
  g.drawLine(x, lineStartY, x, lineEndY, 2.0f);
  g.setColour(coreColour);
  g.drawLine(x, lineStartY, x, lineEndY, 1.05f);

  if (!drawHead || (headBottom - headTop) < 5.0f)
    return;

  const float halfWidth = juce::jmin(kPlayheadHeadHalfWidth, bounds.getWidth() * 0.24f);
  const float neckHalfWidth = juce::jmin(kPlayheadNeckHalfWidth, halfWidth - 1.0f);
  const float shoulderY = headTop + ((headBottom - headTop) * 0.60f);
  const float shoulderInset = juce::jmax(1.0f, halfWidth * 0.22f);

  juce::Path head;
  head.startNewSubPath(x - halfWidth, headTop + 0.7f);
  head.lineTo(x + halfWidth, headTop + 0.7f);
  head.lineTo(x + (halfWidth - shoulderInset), shoulderY);
  head.lineTo(x + neckHalfWidth, headBottom);
  head.lineTo(x - neckHalfWidth, headBottom);
  head.lineTo(x - (halfWidth - shoulderInset), shoulderY);
  head.closeSubPath();

  g.setColour(shadowColour.withAlpha(0.78f * alpha));
  g.fillPath(head, juce::AffineTransform::translation(0.0f, 0.6f));
  g.setColour(coreColour);
  g.fillPath(head);
}

juce::String formatSeconds(double seconds) {
  if (seconds < 0.0)
    seconds = 0.0;
  const int total = static_cast<int>(std::round(seconds));
  const int mins = total / 60;
  const int secs = total % 60;
  return juce::String(mins).paddedLeft('0', 2) + ":" +
         juce::String(secs).paddedLeft('0', 2);
}

bool nearlyEqual(double a, double b, double epsilon = 0.0001) {
  return std::abs(a - b) <= epsilon;
}

juce::Colour partColorOrFallback(const SongArrangementPartSnapshot &part,
                                 int partIndex) {
  juce::ignoreUnused(partIndex);
  auto colourText = standardArrangementPartColorForName(part.name);
  if (colourText.isEmpty())
    colourText = fallbackArrangementPartColorForName(part.name);
  if (isArrangementPartColorValid(colourText))
    return juce::Colour::fromString(colourText);
  return UITheme::accent;
}

struct UtilityCueSuggestion {
  const char *key = nullptr;
  const char *spanishLabel = nullptr;
  const char *englishLabel = nullptr;
};

const std::array<UtilityCueSuggestion, 17> kUtilityCueSuggestions = {{
    {"Build", "Sube intensidad", "Build"},
    {"Slowly Build", "Sube lento", "Slowly Build"},
    {"Breakdown", "Baja intensidad", "Breakdown"},
    {"Hold", "Sostener", "Hold"},
    {"Softly", "Suave", "Softly"},
    {"Break", "Pausa", "Break"},
    {"All In", "Toda la banda", "All In"},
    {"Drums", "Batería", "Drums"},
    {"Drums In", "Entra batería", "Drums In"},
    {"Bass", "Bajo", "Bass"},
    {"Guitar", "Guitarra", "Guitar"},
    {"Keys", "Teclado", "Keys"},
    {"Key Change Up", "Sube tono", "Key Change Up"},
    {"Key Change Down", "Baja tono", "Key Change Down"},
    {"Big Ending", "Final grande", "Big Ending"},
    {"Last Time", "Última vez", "Last Time"},
    {"A Capella", "A capella", "A Capella"},
}};

juce::String utilityCueDisplayLabelForName(const juce::String &name) {
  const auto key = DynamicGuideEngine::normalizeSectionKey(name);
  for (const auto &cue : kUtilityCueSuggestions) {
    if (key == DynamicGuideEngine::normalizeSectionKey(cue.key))
      return L10n::tr(cue.spanishLabel, cue.englishLabel);
  }
  return {};
}

int sectionVariantLimitForName(const juce::String &name) {
  const auto canonical = canonicalArrangementSectionName(name);
  if (canonical == "Chorus" || canonical == "Pre-Chorus" ||
      canonical == "Bridge")
    return 4;
  if (canonical == "Verse")
    return 6;
  return 0;
}

juce::String sectionVariantName(const juce::String &name, const int variant) {
  const auto canonical = canonicalArrangementSectionName(name);
  const auto baseName = canonical.isNotEmpty() ? canonical : name.trim();
  if (variant <= 1)
    return baseName;
  return baseName + " " + juce::String(variant);
}

juce::String sectionVariantDisplayLabel(const juce::String &name,
                                        const int variant) {
  auto label = arrangementPartDisplayLabelForName(sectionVariantName(name, 1));
  if (variant > 1)
    label << " " << variant;
  return label;
}

juce::String abbreviatedArrangementLabel(const juce::String &label) {
  const auto trimmed = label.trim();
  if (trimmed.length() <= 4)
    return trimmed;

  const auto canonical = canonicalArrangementSectionName(trimmed);
  if (canonical == "Intro")
    return "In";
  if (canonical == "Verse")
    return L10n::tr("Vr", "Vr");
  if (canonical == "Pre-Chorus")
    return L10n::tr("Pre", "Pre");
  if (canonical == "Chorus")
    return L10n::tr("Co", "Ch");
  if (canonical == "Post-Chorus")
    return L10n::tr("Post", "Post");
  if (canonical == "Bridge")
    return L10n::tr("Pu", "Br");
  if (canonical == "Instrumental")
    return "Inst";
  if (canonical == "Interlude")
    return "Int";
  if (canonical == "Outro")
    return "Out";
  if (canonical == "Ending")
    return L10n::tr("Fin", "End");

  return trimmed.substring(0, juce::jmin(4, trimmed.length()));
}

juce::String countWordLabel(const int value) {
  switch (value) {
  case 1:
    return L10n::tr("Uno", "One");
  case 2:
    return L10n::tr("Dos", "Two");
  case 3:
    return L10n::tr("Tres", "Three");
  case 4:
    return L10n::tr("Cuatro", "Four");
  case 5:
    return L10n::tr("Cinco", "Five");
  case 6:
    return L10n::tr("Seis", "Six");
  case 7:
    return L10n::tr("Siete", "Seven");
  default:
    break;
  }
  return juce::String(value);
}

juce::Colour tempoRegionColourForIndex(const int index) {
  static const std::array<juce::Colour, 8> colours = {
      juce::Colour::fromRGB(74, 144, 226),  juce::Colour::fromRGB(245, 166, 35),
      juce::Colour::fromRGB(126, 211, 33),  juce::Colour::fromRGB(189, 16, 224),
      juce::Colour::fromRGB(80, 227, 194),  juce::Colour::fromRGB(248, 83, 112),
      juce::Colour::fromRGB(184, 233, 134), juce::Colour::fromRGB(248, 231, 28)};
  return colours[static_cast<size_t>(
      juce::jmax(0, index) % static_cast<int>(colours.size()))];
}

juce::String tempoRegionDisplayLabel(const SongTempoRegionSnapshot &region,
                                     const bool compact) {
  const auto bpm = juce::String(region.bpm, 0);
  if (compact)
    return bpm + " " + region.timeSignature;
  return bpm + " BPM " + region.timeSignature;
}

class InsertSectionMenuLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  void setItemColour(const juce::String &text, juce::Colour colour) {
    itemColours[text.trim().toStdString()] = colour;
  }

  void drawPopupMenuBackground(juce::Graphics &g, int width,
                               int height) override {
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                               static_cast<float>(width),
                                               static_cast<float>(height));
    g.setColour(UITheme::cardBg.darker(0.18f).withAlpha(0.98f));
    g.fillRoundedRectangle(bounds.reduced(1.0f), 6.0f);
    g.setColour(UITheme::cardBorder.brighter(0.18f).withAlpha(0.95f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 6.0f, 1.0f);
  }

  void getIdealPopupMenuItemSize(const juce::String &text,
                                 bool isSeparator,
                                 int standardMenuItemHeight,
                                 int &idealWidth,
                                 int &idealHeight) override {
    if (isSeparator) {
      idealWidth = 220;
      idealHeight = 8;
      return;
    }

    juce::ignoreUnused(standardMenuItemHeight);
    juce::ignoreUnused(text);
    idealWidth = 260;
    idealHeight = 34;
  }

  void drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                         bool isSeparator, bool isActive, bool isHighlighted,
                         bool isTicked, bool hasSubMenu,
                         const juce::String &text,
                         const juce::String &shortcutKeyText,
                         const juce::Drawable *icon,
                         const juce::Colour *textColourToUse) override {
    juce::ignoreUnused(isTicked, hasSubMenu, shortcutKeyText, icon,
                       textColourToUse);

    if (isSeparator) {
      g.setColour(UITheme::cardBorder.withAlpha(0.55f));
      g.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX() + 10),
                           static_cast<float>(area.getRight() - 10));
      return;
    }

    auto row = area.toFloat().reduced(6.0f, 3.0f);
    const auto colour = colourForText(text);
    g.setColour(isHighlighted ? colour.withAlpha(0.24f)
                              : juce::Colours::black.withAlpha(0.10f));
    g.fillRoundedRectangle(row, 5.0f);

    const auto stripe = row.withWidth(4.0f).reduced(0.0f, 4.0f);
    g.setColour(colour.withAlpha(isActive ? 0.95f : 0.42f));
    g.fillRoundedRectangle(stripe, 2.0f);

    const auto chip = juce::Rectangle<float>(row.getX() + 12.0f,
                                            row.getCentreY() - 4.0f,
                                            8.0f, 8.0f);
    g.setColour(colour.withAlpha(isActive ? 0.95f : 0.45f));
    g.fillEllipse(chip);

    g.setFont(makeAppFont(15.5f, AppFontWeight::SemiBold));
    g.setColour((isActive ? juce::Colours::white : UITheme::textSecondary)
                    .withAlpha(isActive ? 0.96f : 0.62f));
    g.drawFittedText(text, row.toNearestInt().withTrimmedLeft(28).reduced(8, 0),
                     juce::Justification::centredLeft, 1);
  }

private:
  juce::Colour colourForText(const juce::String &text) const {
    const auto it = itemColours.find(text.trim().toStdString());
    if (it != itemColours.end())
      return it->second;
    return UITheme::accent;
  }

  std::unordered_map<std::string, juce::Colour> itemColours;
};

class CountChip final : public juce::Button {
public:
  CountChip(const int valueIn, juce::String labelIn)
      : juce::Button("countChip"), value(valueIn),
        displayLabel(std::move(labelIn)) {}

  void paintButton(juce::Graphics &g, bool isMouseOverButton,
                   bool isButtonDown) override {
    auto b = getLocalBounds().toFloat();
    auto fill = getToggleState() ? UITheme::accent : UITheme::cardBgHover;
    if (isMouseOverButton)
      fill = fill.brighter(0.06f);
    if (isButtonDown)
      fill = fill.darker(0.05f);

    const float radius = juce::jmax(4.0f, b.getHeight() * 0.42f);
    g.setColour(fill);
    g.fillRoundedRectangle(b, radius);
    g.setColour(UITheme::cardBorder.withAlpha(0.62f));
    g.drawRoundedRectangle(b, radius, 1.0f);
    g.setColour(getToggleState() ? UITheme::onAccent : UITheme::textPrimary);
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText("+ " + displayLabel, getLocalBounds(),
                     juce::Justification::centred, 1);
  }

  int getValue() const noexcept { return value; }

private:
  int value = 1;
  juce::String displayLabel;
};

class CountPickerPopover final : public juce::Component {
public:
  explicit CountPickerPopover(std::function<void(int)> onSelectCountCallback)
      : onSelectCount(std::move(onSelectCountCallback)) {
    title.setText(L10n::tr("Cuenta intermedia", "Intermediate count"),
                  juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setColour(juce::Label::textColourId, UITheme::textSecondary);
    title.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(title);

    chips.reserve(7);
    for (int value = 1; value <= 7; ++value) {
      const auto displayLabel = countWordLabel(value);
      auto chip = std::make_unique<CountChip>(value, displayLabel);
      chip->setClickingTogglesState(true);
      chip->setTooltip(L10n::tr("Agregar cuenta ", "Add count ") +
                       displayLabel);
      chip->onClick = [this, value] {
        if (onSelectCount != nullptr)
          onSelectCount(value);
      };
      addAndMakeVisible(*chip);
      chips.push_back(std::move(chip));
    }
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(b, 8.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(12, 10);
    title.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    constexpr int kCols = 4;
    const int gap = 6;
    const int chipH = 28;
    const int totalGap = gap * (kCols - 1);
    const int chipW = juce::jmax(52, (area.getWidth() - totalGap) / kCols);

    for (size_t i = 0; i < chips.size(); ++i) {
      const int idx = static_cast<int>(i);
      const int row = idx / kCols;
      const int col = idx % kCols;
      const int x = area.getX() + col * (chipW + gap);
      const int y = area.getY() + row * (chipH + gap);
      chips[i]->setBounds(x, y, chipW, chipH);
    }
  }

private:
  juce::Label title;
  std::vector<std::unique_ptr<CountChip>> chips;
  std::function<void(int)> onSelectCount;
};

class SectionVariantPopover final : public juce::Component {
public:
  SectionVariantPopover(juce::String sectionNameIn, const int variantLimit,
                        std::function<void(juce::String)> onSelectSectionCallback)
      : sectionName(std::move(sectionNameIn)),
        onSelectSection(std::move(onSelectSectionCallback)) {
    title.setText(arrangementPartDisplayLabelForName(sectionName),
                  juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setColour(juce::Label::textColourId, UITheme::textSecondary);
    title.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(title);

    const int limit = juce::jmax(1, variantLimit);
    chips.reserve(static_cast<size_t>(limit));
    for (int variant = 1; variant <= limit; ++variant) {
      const auto label = sectionVariantDisplayLabel(sectionName, variant);
      auto chip = std::make_unique<CountChip>(variant, label);
      chip->setTooltip(L10n::tr("Agregar ", "Add ") + label);
      chip->onClick = [this, variant] {
        if (onSelectSection != nullptr)
          onSelectSection(sectionVariantName(sectionName, variant));
      };
      addAndMakeVisible(*chip);
      chips.push_back(std::move(chip));
    }
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(b, 8.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(12, 10);
    title.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    constexpr int kCols = 2;
    const int gap = 6;
    const int chipH = 28;
    const int totalGap = gap * (kCols - 1);
    const int chipW = juce::jmax(92, (area.getWidth() - totalGap) / kCols);

    for (size_t i = 0; i < chips.size(); ++i) {
      const int idx = static_cast<int>(i);
      const int row = idx / kCols;
      const int col = idx % kCols;
      const int x = area.getX() + col * (chipW + gap);
      const int y = area.getY() + row * (chipH + gap);
      chips[i]->setBounds(x, y, chipW, chipH);
    }
  }

private:
  juce::String sectionName;
  juce::Label title;
  std::vector<std::unique_ptr<CountChip>> chips;
  std::function<void(juce::String)> onSelectSection;
};

class SectionPickerPopover final : public juce::Component {
public:
  explicit SectionPickerPopover(
      std::function<void(juce::String)> onSelectSectionCallback)
      : onSelectSection(std::move(onSelectSectionCallback)) {
    title.setText(L10n::tr("Agregar sección", "Add section"),
                  juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setColour(juce::Label::textColourId, UITheme::textSecondary);
    title.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(title);

    for (const auto *section : kSongSectionSuggestions) {
      const juce::String sectionName =
          section != nullptr ? juce::String(section) : juce::String("Part");
      const int limit = sectionVariantLimitForName(sectionName);
      const int variants = juce::jmax(1, limit);
      for (int variant = 1; variant <= variants; ++variant) {
        const auto label = sectionVariantDisplayLabel(sectionName, variant);
        auto chip = std::make_unique<CountChip>(variant, label);
        chip->setTooltip(L10n::tr("Agregar ", "Add ") + label);
        chip->onClick = [this, sectionName, variant] {
          if (onSelectSection != nullptr)
            onSelectSection(sectionVariantName(sectionName, variant));
        };
        addAndMakeVisible(*chip);
        chips.push_back(std::move(chip));
      }
    }
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(b, 8.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(12, 10);
    title.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    constexpr int kCols = 3;
    const int gap = 6;
    const int chipH = 28;
    const int totalGap = gap * (kCols - 1);
    const int chipW = juce::jmax(92, (area.getWidth() - totalGap) / kCols);

    for (size_t i = 0; i < chips.size(); ++i) {
      const int idx = static_cast<int>(i);
      const int row = idx / kCols;
      const int col = idx % kCols;
      const int x = area.getX() + col * (chipW + gap);
      const int y = area.getY() + row * (chipH + gap);
      chips[i]->setBounds(x, y, chipW, chipH);
    }
  }

  static int getPreferredHeight() {
    int chipCount = 0;
    for (const auto *section : kSongSectionSuggestions) {
      const juce::String sectionName =
          section != nullptr ? juce::String(section) : juce::String("Part");
      chipCount += juce::jmax(1, sectionVariantLimitForName(sectionName));
    }
    constexpr int kCols = 3;
    const int rows = (chipCount + kCols - 1) / kCols;
    return 44 + (rows * 28) + (juce::jmax(0, rows - 1) * 6);
  }

private:
  juce::Label title;
  std::vector<std::unique_ptr<CountChip>> chips;
  std::function<void(juce::String)> onSelectSection;
};

class UtilityCuePopover final : public juce::Component {
public:
  explicit UtilityCuePopover(std::function<void(juce::String)> onSelectCueCallback)
      : onSelectCue(std::move(onSelectCueCallback)) {
    title.setText(L10n::tr("Utilidades de guía", "Guide utilities"),
                  juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setColour(juce::Label::textColourId, UITheme::textSecondary);
    title.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(title);

    chips.reserve(kUtilityCueSuggestions.size());
    for (size_t i = 0; i < kUtilityCueSuggestions.size(); ++i) {
      const auto &cue = kUtilityCueSuggestions[i];
      const auto label = L10n::tr(cue.spanishLabel, cue.englishLabel);
      auto chip = std::make_unique<CountChip>(static_cast<int>(i), label);
      chip->setTooltip(L10n::tr("Agregar utilidad ", "Add utility ") + label);
      chip->onClick = [this, key = juce::String(cue.key)] {
        if (onSelectCue != nullptr)
          onSelectCue(key);
      };
      addAndMakeVisible(*chip);
      chips.push_back(std::move(chip));
    }
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(b, 8.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(12, 10);
    title.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    constexpr int kCols = 2;
    const int gap = 6;
    const int chipH = 28;
    const int totalGap = gap * (kCols - 1);
    const int chipW = juce::jmax(118, (area.getWidth() - totalGap) / kCols);
    for (size_t i = 0; i < chips.size(); ++i) {
      const int idx = static_cast<int>(i);
      const int row = idx / kCols;
      const int col = idx % kCols;
      const int x = area.getX() + col * (chipW + gap);
      const int y = area.getY() + row * (chipH + gap);
      chips[i]->setBounds(x, y, chipW, chipH);
    }
  }

private:
  juce::Label title;
  std::vector<std::unique_ptr<CountChip>> chips;
  std::function<void(juce::String)> onSelectCue;
};
} // namespace

WaveformIconButton::WaveformIconButton(const juce::String &name,
                                       const SvgIconName iconIn)
    : juce::Button(name), icon(iconIn) {}

void WaveformIconButton::setIcon(const SvgIconName iconIn) {
  if (icon == iconIn)
    return;
  icon = iconIn;
  repaint();
}

void WaveformIconButton::paintButton(juce::Graphics &g,
                                     const bool highlighted,
                                     const bool down) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  const bool toggled = getToggleState();
  auto fill = toggled ? UITheme::accent.withAlpha(0.92f)
                      : UITheme::cardBgHover.withAlpha(0.78f);
  if (down)
    fill = fill.darker(0.18f);
  else if (highlighted)
    fill = fill.brighter(0.10f);

  g.setColour(juce::Colours::black.withAlpha(0.18f));
  g.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), 5.0f);
  g.setColour(fill);
  g.fillRoundedRectangle(bounds, 5.0f);
  g.setColour((toggled ? UITheme::accent : UITheme::cardBorder)
                  .brighter(0.16f)
                  .withAlpha(highlighted || toggled ? 0.90f : 0.55f));
  g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

  const auto iconColour =
      toggled ? UITheme::onAccent : UITheme::textPrimary.withAlpha(0.92f);
  const auto iconBounds = bounds.reduced(4.5f);
  if (!drawSvgIcon(g, iconBounds, icon, iconColour)) {
    g.setColour(iconColour);
    g.setFont(makeAppFont(14.5f, AppFontWeight::SemiBold));
    g.drawFittedText(getButtonText(), getLocalBounds(),
                     juce::Justification::centred, 1);
  }
}

WaveformBar::WaveformBar() {
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(true);

  followButton.setIcon(SvgIconName::AudioWaveform);
  followButton.setButtonText({});
  followButton.setClickingTogglesState(true);
  followButton.setToggleState(followPlaybackEnabled, juce::dontSendNotification);
  followButton.setTooltip(
      L10n::tr("Auto centra la vista en la reproducción", "Auto-follow playback"));
  followButton.setViewportIgnoreDragFlag(true);
  followButton.onClick = [this] {
    followPlaybackEnabled = followButton.getToggleState();
    if (followPlaybackEnabled) {
      followPendingJumpWasActive = false;
      followLastSongProgressValid = false;
      followSuspendedUntilMs = 0.0;
      float targetRatio = 0.0f;
      bool hasTargetRatio = getPendingJumpProgressRatio(targetRatio);
      if (!hasTargetRatio && positionProvider != nullptr && lengthProvider != nullptr) {
        const double timelineLength = getVisualTimelineLengthSeconds();
        if (timelineLength > 0.0) {
          const double pos = juce::jmax(0.0, positionProvider());
          const double visualPos = visualSecondsFromSourceSeconds(pos);
          targetRatio = static_cast<float>(
              juce::jlimit(0.0, 1.0, visualPos / timelineLength));
          hasTargetRatio = true;
        }
      }
      if (!hasTargetRatio) {
        const auto window = getVisibleWindow();
        targetRatio = juce::jlimit(0.0f, 1.0f, window.first + (window.second * 0.5f));
      }

      const float span =
          juce::jlimit(kMinViewportSpan, 1.0f, kFollowEnabledViewportSpan);
      setVisibleWindow(targetRatio - (span * 0.5f), span);
    } else {
      followPendingJumpWasActive = false;
      followLastSongProgressValid = false;
      setVisibleWindow(0.0f, 1.0f);
    }
  };
  addAndMakeVisible(followButton);

  expandButton.setIcon(SvgIconName::Maximize);
  expandButton.setButtonText({});
  expandButton.setTooltip(L10n::tr("Ampliar waveform", "Expand waveform"));
  expandButton.setClickingTogglesState(true);
  expandButton.setToggleState(waveformExpanded, juce::dontSendNotification);
  expandButton.setViewportIgnoreDragFlag(true);
  expandButton.onClick = [this] {
    waveformExpanded = expandButton.getToggleState();
    expandButton.setIcon(waveformExpanded ? SvgIconName::Copy
                                          : SvgIconName::Maximize);
    expandButton.setTooltip(
        waveformExpanded ? L10n::tr("Reducir waveform", "Collapse waveform")
                         : L10n::tr("Ampliar waveform", "Expand waveform"));
    if (onExpandToggle)
      onExpandToggle(waveformExpanded);
    resized();
    repaint();
  };
  addAndMakeVisible(expandButton);

  editButton.setIcon(SvgIconName::ImageEdit);
  editButton.setButtonText({});
  editButton.setTooltip(
      L10n::tr(
          "Arrastra para ajustar; usa el selector para compas/beat/libre (Option/Alt = libre temporal)",
          "Drag snaps to grid; use the selector for bar/beat/free (hold Option/Alt for temporary free move)"));
  editButton.setClickingTogglesState(true);
  editButton.setViewportIgnoreDragFlag(true);
  editButton.onClick = [this] {
    const bool editing = editButton.getToggleState();
    timelineViewMode = TimelineViewMode::GuideMap;
    if (!editing) {
      snapMode = SnapMode::Bar;
      snapModeBox.setSelectedId(kSnapModeBarId, juce::sendNotificationSync);
    }
    editButton.setTooltip(
        editing ? L10n::tr("Salir del modo edición", "Exit edit mode")
                : L10n::tr("Editar secciones de la waveform",
                            "Edit waveform sections"));
    // Cuando el alto del panel no cambia entre modos, resized() no siempre se
    // dispara desde el padre. Forzamos layout local para mostrar/ocultar
    // correctamente los botones de partes.
    resized();
    if (editing && viewportSpan >= 0.999f)
      setVisibleWindow(0.0f, 1.0f);
    if (onEditToggle)
      onEditToggle(editing);
    repaint();
  };
  addAndMakeVisible(editButton);

  resetButton.setIcon(SvgIconName::SkipBack);
  resetButton.setButtonText({});
  resetButton.setTooltip(L10n::tr(
      "Restaurar secciones originales", "Reset original sections"));
  resetButton.setViewportIgnoreDragFlag(true);
  resetButton.onClick = [this] { resetArrangementToDefault(); };
  addAndMakeVisible(resetButton);

  auto configureViewButton = [this](juce::TextButton &button,
                                    const juce::String &text,
                                    TimelineViewMode mode) {
    button.setButtonText(text);
    button.setClickingTogglesState(true);
    button.setViewportIgnoreDragFlag(true);
    button.onClick = [this, mode] {
      timelineViewMode = mode;
      guideMapButton.setToggleState(timelineViewMode == TimelineViewMode::GuideMap,
                                    juce::dontSendNotification);
      tempoMapButton.setToggleState(timelineViewMode == TimelineViewMode::TempoMap,
                                    juce::dontSendNotification);
      updateTempoControlsFromSelection();
      resized();
      repaint();
    };
    addAndMakeVisible(button);
  };
  configureViewButton(guideMapButton, L10n::tr(u8"Mapa de guía", "Guide map"),
                      TimelineViewMode::GuideMap);
  configureViewButton(tempoMapButton, L10n::tr("Mapa de tempo", "Tempo map"),
                      TimelineViewMode::TempoMap);

  sectionMenuButton.setButtonText("+ " + L10n::tr("Sección", "Section"));
  sectionMenuButton.setTooltip(L10n::tr("Agregar sección", "Add section"));
  sectionMenuButton.setViewportIgnoreDragFlag(true);
  sectionMenuButton.onClick = [this] {
    auto safe = juce::Component::SafePointer<WaveformBar>(this);
    auto panel = std::make_unique<SectionPickerPopover>(
        [safe](juce::String sectionName) {
          if (safe != nullptr)
            safe->addArrangementPart(sectionName);
        });
    panel->setSize(336, SectionPickerPopover::getPreferredHeight());

    auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
    juce::Component *parentComp = editor != nullptr
                                      ? static_cast<juce::Component *>(editor)
                                      : static_cast<juce::Component *>(this);
    const auto anchor =
        parentComp->getLocalArea(&sectionMenuButton,
                                 sectionMenuButton.getLocalBounds());
    auto &callout = juce::CallOutBox::launchAsynchronously(
        std::move(panel), anchor, parentComp);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
  };
  addAndMakeVisible(sectionMenuButton);

  splitTempoRegionButton.setButtonText(L10n::tr("Agregar zona", "Add zone"));
  splitTempoRegionButton.setViewportIgnoreDragFlag(true);
  splitTempoRegionButton.onClick = [this] {
    if (!juce::isPositiveAndBelow(selectedTempoRegionIndex,
                                  static_cast<int>(tempoMap.regions.size())))
      return;
    const auto region = tempoMap.regions[static_cast<size_t>(selectedTempoRegionIndex)];
    const double splitAt = (region.startSec + region.endSec) * 0.5;
    if ((splitAt - region.startSec) < kMinPartDurationSec ||
        (region.endSec - splitAt) < kMinPartDurationSec)
      return;
    tempoMap.regions[static_cast<size_t>(selectedTempoRegionIndex)].endSec = splitAt;
    auto next = region;
    next.id = juce::Uuid().toString();
    next.startSec = splitAt;
    tempoMap.regions.insert(tempoMap.regions.begin() + selectedTempoRegionIndex + 1,
                            std::move(next));
    selectTempoRegion(selectedTempoRegionIndex + 1);
    notifyTempoMapChanged();
  };
  addAndMakeVisible(splitTempoRegionButton);

  tempoBpmLabel.setText("BPM", juce::dontSendNotification);
  tempoBpmLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(tempoBpmLabel);
  tempoBpmEditor.setInputRestrictions(6, "0123456789.");
  tempoBpmEditor.setJustification(juce::Justification::centred);
  tempoBpmEditor.setFont(makeAppFont(12.5f, AppFontWeight::SemiBold));
  tempoBpmEditor.onReturnKey = [this] { applyTempoControlsToSelection(); };
  tempoBpmEditor.onFocusLost = [this] { applyTempoControlsToSelection(); };
  addAndMakeVisible(tempoBpmEditor);

  tempoSignatureLabel.setText(L10n::tr(u8"Compás", "Meter"),
                              juce::dontSendNotification);
  tempoSignatureLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(tempoSignatureLabel);
  for (const auto *sig : {"2/4", "3/4", "4/4", "6/8"})
    tempoSignatureBox.addItem(sig, tempoSignatureBox.getNumItems() + 1);
  tempoSignatureBox.onChange = [this] { applyTempoControlsToSelection(); };
  addAndMakeVisible(tempoSignatureBox);

  snapModeLabel.setText(L10n::tr("Ajuste", "Grid"), juce::dontSendNotification);
  snapModeLabel.setJustificationType(juce::Justification::centredRight);
  snapModeLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(snapModeLabel);

  snapModeBox.addItem(L10n::tr(u8"Compás", "Bar"), kSnapModeBarId);
  snapModeBox.addItem(L10n::tr("Beat", "Beat"), kSnapModeBeatId);
  snapModeBox.addItem(L10n::tr("1/2 Beat", "1/2 Beat"), kSnapModeHalfBeatId);
  snapModeBox.addItem(L10n::tr("Libre", "Free"), kSnapModeFreeId);
  snapModeBox.setJustificationType(juce::Justification::centredLeft);
  snapModeBox.setTooltip(
      L10n::tr(u8"Modo de ajuste al mover secciónes", "Section move snap mode"));
  snapModeBox.setSelectedId(kSnapModeBarId, juce::dontSendNotification);
  snapModeBox.onChange = [this] {
    const int selectedId = snapModeBox.getSelectedId();
    switch (selectedId) {
    case kSnapModeBarId:
      snapMode = SnapMode::Bar;
      break;
    case kSnapModeHalfBeatId:
      snapMode = SnapMode::HalfBeat;
      break;
    case kSnapModeFreeId:
      snapMode = SnapMode::Free;
      break;
    case kSnapModeBeatId:
    default:
      snapMode = SnapMode::Beat;
      break;
    }
  };
  addAndMakeVisible(snapModeBox);

  rebuildSectionAddButtons();

  refreshThemeColours();
  startTimerHz(20);
}

void WaveformBar::setWaveformBins(std::vector<float> bins) {
  const bool wasEditing = editButton.getToggleState();
  waveformBins = std::move(bins);
  const bool hasLoadedSong = !waveformBins.empty();

  if (!hasLoadedSong && wasEditing) {
    snapMode = SnapMode::Bar;
    snapModeBox.setSelectedId(kSnapModeBarId, juce::sendNotificationSync);
    editButton.setToggleState(false, juce::dontSendNotification);
    editButton.setButtonText("E");
    if (onEditToggle)
      onEditToggle(false);
  }

  editButton.setVisible(hasLoadedSong);
  editButton.setEnabled(hasLoadedSong);
  resetButton.setVisible(hasLoadedSong);
  resetButton.setEnabled(hasLoadedSong);
  if (!hasLoadedSong)
    cancelDeferredPartJumpFromDoubleClick();
  resized();
  repaint();
}

void WaveformBar::setArrangement(
    const SongArrangementSnapshot &arrangementSnapshot,
    const bool isCustomArrangement) {
  arrangement = arrangementSnapshot;
  captureSourceArrangementParts(arrangementSnapshot);
  if (usesVirtualClips())
    rebuildPartsFromClips();
  customArrangement = isCustomArrangement;
  selectedPartIndex = -1;
  selectedPartIds.clear();
  normalizeArrangement(!usesVirtualClips(), false);
  lastCommittedArrangement = arrangement;
  arrangementUndoStack.clear();
  arrangementRedoStack.clear();
  arrangementClipboard = {};

  dragMode = DragMode::none;
  deferActiveResizeTimelineUpdate = false;
  activeDragPartId.clear();
  dragPartOrigins.clear();
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  hoveredActionPartIndex = -1;
  hoveredAddPartIndex = -1;
  hoveredMutePartIndex = -1;
  hoveredLoopPartIndex = -1;
  hoveredJumpPartIndex = -1;
  cancelDeferredPartJumpFromDoubleClick();
  repaint();
}

void WaveformBar::setTempoMap(const SongTempoMapSnapshot &tempoMapSnapshot) {
  tempoMap = tempoMapSnapshot;
  selectedTempoRegionIndex = tempoMap.regions.empty() ? -1 : 0;
  lastAppliedTempoRegionIndex = -1;
  normalizeTempoMap();
  updateTempoControlsFromSelection();
  repaint();
}

void WaveformBar::setSongRhythm(const double bpm,
                                const juce::String &timeSignature) {
  songBpm = juce::jmax(0.0, bpm);
  songTimeSignature = timeSignature.trim();
  if (songTimeSignature.isEmpty())
    songTimeSignature = "4/4";
  songBeatsPerBar = timeSignatureBeatsPerBar(songTimeSignature);
  normalizeTempoMap();

  repaint();
}

void WaveformBar::setTransportProviders(PositionProvider positionCb,
                                        LengthProvider lengthCb,
                                        PlayingProvider playingCb) {
  positionProvider = std::move(positionCb);
  lengthProvider = std::move(lengthCb);
  playingProvider = std::move(playingCb);
}

void WaveformBar::setSongWindowProviders(SongWindowProvider startCb,
                                         SongWindowProvider endCb) {
  songWindowStartProvider = std::move(startCb);
  songWindowEndProvider = std::move(endCb);
}

void WaveformBar::setSeekCallback(SeekCallback cb) { onSeekTo = std::move(cb); }

void WaveformBar::setOnEditToggle(EditToggleCallback cb) {
  onEditToggle = std::move(cb);
}

void WaveformBar::setOnExpandToggle(ExpandToggleCallback cb) {
  onExpandToggle = std::move(cb);
}

void WaveformBar::setOnArrangementChanged(ArrangementChangedCallback cb) {
  onArrangementChanged = std::move(cb);
}

void WaveformBar::setOnArrangementReset(ArrangementResetCallback cb) {
  onArrangementReset = std::move(cb);
}

void WaveformBar::setOnArrangementPartJumpRequest(
    ArrangementPartJumpRequestCallback cb) {
  onArrangementPartJumpRequest = std::move(cb);
}

void WaveformBar::setOnArrangementPartLoopToggleRequest(
    ArrangementPartLoopToggleRequestCallback cb) {
  onArrangementPartLoopToggleRequest = std::move(cb);
}

void WaveformBar::setOnTempoMapChanged(TempoMapChangedCallback cb) {
  onTempoMapChanged = std::move(cb);
}

void WaveformBar::setOnActiveTempoRegionChanged(ActiveTempoRegionCallback cb) {
  onActiveTempoRegionChanged = std::move(cb);
}

void WaveformBar::setPendingArrangementPartProvider(
    PendingArrangementPartProvider cb) {
  pendingArrangementPartProvider = std::move(cb);
}

void WaveformBar::setActiveArrangementLoopPartProvider(
    ActiveArrangementLoopPartProvider cb) {
  activeArrangementLoopPartProvider = std::move(cb);
}

void WaveformBar::setCurrentArrangementPartProvider(
    CurrentArrangementPartProvider cb) {
  currentArrangementPartProvider = std::move(cb);
}

void WaveformBar::setCurrentArrangementPartIdProvider(
    CurrentArrangementPartIdProvider cb) {
  currentArrangementPartIdProvider = std::move(cb);
}

void WaveformBar::setMidiLearnActive(const bool active) {
  midiLearnActive = active;
  if (!midiLearnActive)
    midiLearnPendingTargetId = -1;
  repaint();
}

void WaveformBar::setMidiLearnPendingTarget(const int targetId) {
  midiLearnPendingTargetId = targetId;
  repaint();
}

void WaveformBar::setMidiLearnBindingLabelProvider(
    std::function<juce::String(int)> cb) {
  midiLearnBindingLabelProvider = std::move(cb);
  repaint();
}

void WaveformBar::setOnMidiLearnTargetSelected(std::function<void(int)> cb) {
  onMidiLearnTargetSelected = std::move(cb);
}

void WaveformBar::triggerEditToggleFromMidi() {
  if (!editButton.isVisible() || !editButton.isEnabled() || midiLearnActive)
    return;
  editButton.triggerClick();
}

void WaveformBar::refreshThemeColours() {
  followButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  followButton.setColour(juce::TextButton::buttonOnColourId,
                         UITheme::accent.withAlpha(0.95f));
  followButton.setColour(juce::TextButton::textColourOffId,
                         UITheme::textPrimary);
  followButton.setColour(juce::TextButton::textColourOnId,
                         UITheme::onAccent);
  expandButton.setColour(juce::TextButton::buttonColourId,
                         UITheme::cardBgHover);
  expandButton.setColour(juce::TextButton::buttonOnColourId,
                         UITheme::cardBgPressed);
  expandButton.setColour(juce::TextButton::textColourOffId,
                         UITheme::textPrimary);
  expandButton.setColour(juce::TextButton::textColourOnId,
                         UITheme::textPrimary);
  editButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  editButton.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
  editButton.setColour(juce::TextButton::textColourOffId,
                       UITheme::textPrimary);
  editButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::textPrimary);
  for (auto *button : {&guideMapButton, &tempoMapButton}) {
    button->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
    button->setColour(juce::TextButton::buttonOnColourId,
                      UITheme::accent.withAlpha(0.95f));
    button->setColour(juce::TextButton::textColourOffId,
                      UITheme::textPrimary);
    button->setColour(juce::TextButton::textColourOnId,
                      UITheme::onAccent);
  }
  sectionMenuButton.setColour(juce::TextButton::buttonColourId,
                              UITheme::cardBgHover);
  sectionMenuButton.setColour(juce::TextButton::buttonOnColourId,
                              UITheme::cardBgPressed);
  sectionMenuButton.setColour(juce::TextButton::textColourOffId,
                              UITheme::textPrimary);
  sectionMenuButton.setColour(juce::TextButton::textColourOnId,
                              UITheme::textPrimary);
  snapModeLabel.setFont(makeAppFont(10.5f, AppFontWeight::SemiBold));
  snapModeLabel.setColour(juce::Label::textColourId,
                          UITheme::textSecondary.withAlpha(0.95f));
  snapModeBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  snapModeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  snapModeBox.setColour(juce::ComboBox::outlineColourId,
                        UITheme::cardBorder.withAlpha(0.75f));
  snapModeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  for (auto *label : {&tempoBpmLabel, &tempoSignatureLabel}) {
    label->setFont(makeAppFont(12.0f, AppFontWeight::SemiBold));
    label->setColour(juce::Label::textColourId, UITheme::textPrimary);
  }
  tempoBpmEditor.setColour(juce::TextEditor::backgroundColourId,
                           UITheme::cardBgHover);
  tempoBpmEditor.setColour(juce::TextEditor::textColourId,
                           UITheme::textPrimary);
  tempoBpmEditor.setColour(juce::TextEditor::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.75f));
  tempoSignatureBox.setColour(juce::ComboBox::backgroundColourId,
                              UITheme::cardBgHover);
  tempoSignatureBox.setColour(juce::ComboBox::textColourId,
                              UITheme::textPrimary);
  tempoSignatureBox.setColour(juce::ComboBox::outlineColourId,
                              UITheme::cardBorder.withAlpha(0.75f));
  tempoSignatureBox.setColour(juce::ComboBox::arrowColourId,
                              UITheme::textPrimary);
  for (auto *button : {&splitTempoRegionButton}) {
    button->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
    button->setColour(juce::TextButton::buttonOnColourId,
                      UITheme::cardBgPressed);
    button->setColour(juce::TextButton::textColourOffId,
                      UITheme::textPrimary);
    button->setColour(juce::TextButton::textColourOnId,
                      UITheme::textPrimary);
  }

  for (auto &btn : sectionAddButtons) {
    btn->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
    btn->setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
    btn->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    btn->setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
  }
  repaint();
}

void WaveformBar::setSongLoadingState(const bool active,
                                      const double progress01,
                                      const juce::String &phaseText) {
  const double nowMs = juce::Time::getMillisecondCounterHiRes();
  if (active && !songLoadingActive) {
    songLoadingShownAtMs = nowMs;
    songLoadingHideAfterMs = 0.0;
  }
  songLoadingActive = active;
  songLoadingProgress01 = juce::jlimit(0.0, 1.0, progress01);
  songLoadingPhaseText = phaseText;
  repaint();
}

void WaveformBar::clearSongLoadingState() {
  if (!songLoadingActive && songLoadingHideAfterMs <= 0.0)
    return;

  const double nowMs = juce::Time::getMillisecondCounterHiRes();
  const double elapsedMs = nowMs - songLoadingShownAtMs;
  if (songLoadingActive && elapsedMs < kSongLoadingMinVisibleMs) {
    songLoadingHideAfterMs = songLoadingShownAtMs + kSongLoadingMinVisibleMs;
    return;
  }

  songLoadingActive = false;
  songLoadingHideAfterMs = 0.0;
  songLoadingProgress01 = 0.0;
  songLoadingPhaseText.clear();
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

  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);

  if (mainWaveBodyBounds.getWidth() < 2.0f ||
      mainWaveBodyBounds.getHeight() < 2.0f)
    return;

  if (waveformBins.empty()) {
    g.setColour(UITheme::cardBg.withAlpha(0.55f));
    g.setFont(makeAppFont(15.5f, AppFontWeight::SemiBold));
    juce::String emptyText =
        L10n::tr(u8"Doble click en una canción para cargar waveform",
                 "Double-click a song to load stems waveform");
    if (songLoadingActive || songLoadingHideAfterMs > 0.0) {
      const int percent = static_cast<int>(
          std::round(juce::jlimit(0.0, 1.0, songLoadingProgress01) * 100.0));
      emptyText = L10n::tr(u8"Cargando canción...", "Loading song...") + " " +
                  juce::String(percent) + "%";
    }
    g.drawFittedText(emptyText,
                     mainWaveBodyBounds.toNearestInt().reduced(18),
                     juce::Justification::centred, 2);
    if (miniMapBounds.getHeight() > 0.0f) {
      g.setColour(UITheme::cardBg.withAlpha(0.45f));
      g.fillRoundedRectangle(miniMapBounds, 4.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.7f));
      g.drawRoundedRectangle(miniMapBounds, 4.0f, 1.0f);
    }
  g.setColour(UITheme::accent);
  g.fillRect(0.0f, 0.0f, 2.0f, bounds.getHeight());
  return;
}

  const auto window = getVisibleWindow();
  drawWaveform(g, mainWaveBodyBounds, window.first, window.second,
               isEditingArrangement(), true);
  if (timelineViewMode == TimelineViewMode::TempoMap)
    drawTempoOverlay(g, mainWaveBodyBounds);
  else
    drawArrangementOverlay(g, mainWaveBodyBounds);

  if (seekRulerBounds.getWidth() > 2.0f && seekRulerBounds.getHeight() > 2.0f)
    drawSeekRuler(g, seekRulerBounds, window.first, window.second);

  if (miniMapBounds.getWidth() > 2.0f && miniMapBounds.getHeight() > 2.0f)
    drawMiniMap(g, miniMapBounds);
}

void WaveformBar::paintOverChildren(juce::Graphics &g) {
  if (dragMode != DragMode::selectParts)
    return;

  const auto selectionBounds = getSelectionDragBounds();
  if (selectionBounds.getWidth() < 2.0f || selectionBounds.getHeight() < 2.0f)
    return;

  g.setColour(UITheme::accent.withAlpha(0.16f));
  g.fillRoundedRectangle(selectionBounds, 4.0f);
  g.setColour(UITheme::accent.withAlpha(0.82f));
  g.drawRoundedRectangle(selectionBounds, 4.0f, 1.4f);
}

void WaveformBar::resized() {
  const auto localBounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(localBounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  auto toolbarBounds = getEditToolbarBounds(localBounds).toNearestInt();
  const int editButtonW = 42;
  const int resetButtonW = 42;
  const int expandButtonW = 42;
  const int followButtonW = 42;
  const int followGap = 6;
  const int snapModeGroupW = 186;
  const int snapModeLabelW = 56;
  const bool hasLoadedSong = !waveformBins.empty();

  if (!hasLoadedSong) {
    followButton.setVisible(false);
    followButton.setEnabled(false);
    expandButton.setVisible(false);
    expandButton.setEnabled(false);
    editButton.setVisible(false);
    editButton.setEnabled(false);
    resetButton.setVisible(false);
    resetButton.setEnabled(false);
    guideMapButton.setVisible(false);
    guideMapButton.setEnabled(false);
    tempoMapButton.setVisible(false);
    tempoMapButton.setEnabled(false);
    snapModeLabel.setVisible(false);
    snapModeLabel.setEnabled(false);
    snapModeBox.setVisible(false);
    snapModeBox.setEnabled(false);
    sectionMenuButton.setVisible(false);
    sectionMenuButton.setEnabled(false);
    splitTempoRegionButton.setVisible(false);
    splitTempoRegionButton.setEnabled(false);
    tempoBpmLabel.setVisible(false);
    tempoBpmEditor.setVisible(false);
    tempoSignatureLabel.setVisible(false);
    tempoSignatureBox.setVisible(false);
    for (auto &btn : sectionAddButtons) {
      btn->setVisible(false);
      btn->setEnabled(false);
    }
    return;
  }

  followButton.setVisible(true);
  followButton.setEnabled(true);
  followButton.setToggleState(followPlaybackEnabled, juce::dontSendNotification);
  followButton.setButtonText("F");
  expandButton.setVisible(true);
  expandButton.setEnabled(true);
  expandButton.setToggleState(waveformExpanded, juce::dontSendNotification);
  expandButton.setIcon(waveformExpanded ? SvgIconName::Copy
                                        : SvgIconName::Maximize);
  expandButton.setButtonText(waveformExpanded ? "-" : "+");
  editButton.setVisible(true);
  editButton.setEnabled(true);
  resetButton.setVisible(true);
  resetButton.setEnabled(true);
  const bool editing = isEditingArrangement();

  auto placeBottomWaveControls = [&](const bool includeFollow,
                                     const bool includeViewButtons) {
    auto floating = mainWaveBodyBounds.toNearestInt();
    if (floating.getWidth() > 4 && floating.getHeight() > 4) {
      const int controlH = 26;
      auto bottomControls = floating.removeFromBottom(controlH);
      bottomControls = bottomControls.reduced(2, 1);
      editButton.setBounds(bottomControls.removeFromRight(editButtonW).reduced(1));
      bottomControls.removeFromRight(4);
      resetButton.setBounds(
          bottomControls.removeFromRight(resetButtonW).reduced(1));
      bottomControls.removeFromRight(4);
      expandButton.setBounds(
          bottomControls.removeFromRight(expandButtonW).reduced(1));
      if (includeViewButtons) {
        bottomControls.removeFromRight(followGap);
        tempoMapButton.setBounds(bottomControls.removeFromRight(112).reduced(1));
        bottomControls.removeFromRight(4);
        guideMapButton.setBounds(bottomControls.removeFromRight(112).reduced(1));
      }
      if (includeFollow) {
        bottomControls.removeFromRight(followGap);
        followButton.setBounds(
            bottomControls.removeFromRight(followButtonW).reduced(1));
      }
      return true;
    }
    return false;
  };

  if (editing) {
    placeBottomWaveControls(false, true);
    followButton.setVisible(false);
    followButton.setEnabled(false);
    expandButton.setVisible(true);
    expandButton.setEnabled(true);
    resetButton.setVisible(true);
    resetButton.setEnabled(true);
    guideMapButton.setVisible(true);
    guideMapButton.setEnabled(true);
    tempoMapButton.setVisible(true);
    tempoMapButton.setEnabled(true);
    guideMapButton.setToggleState(timelineViewMode == TimelineViewMode::GuideMap,
                                  juce::dontSendNotification);
    tempoMapButton.setToggleState(timelineViewMode == TimelineViewMode::TempoMap,
                                  juce::dontSendNotification);

    auto leftGroup =
        toolbarBounds.removeFromLeft(
                         juce::jmin(snapModeGroupW,
                                    juce::jmax(122, toolbarBounds.getWidth() / 2)))
            .reduced(1);
    auto labelArea = leftGroup.removeFromLeft(
        juce::jmin(snapModeLabelW, juce::jmax(44, leftGroup.getWidth() / 2)));
    snapModeLabel.setBounds(labelArea);
    snapModeLabel.setVisible(true);
    snapModeLabel.setEnabled(true);
    snapModeBox.setBounds(leftGroup);
    snapModeBox.setVisible(true);
    snapModeBox.setEnabled(true);
    toolbarBounds.removeFromLeft(6);

    auto buttonsArea = toolbarBounds.reduced(0, 1);
    const int buttonGap = 4;

    const bool tempoMode = timelineViewMode == TimelineViewMode::TempoMap;
    sectionMenuButton.setVisible(false);
    sectionMenuButton.setEnabled(false);
    for (auto &btn : sectionAddButtons) {
      btn->setVisible(false);
      btn->setEnabled(false);
    }
    splitTempoRegionButton.setVisible(false);
    splitTempoRegionButton.setEnabled(false);
    tempoBpmLabel.setVisible(false);
    tempoBpmEditor.setVisible(false);
    tempoSignatureLabel.setVisible(false);
    tempoSignatureBox.setVisible(false);
    if (tempoMode) {
      int x = buttonsArea.getX();
      const int h = buttonsArea.getHeight();
      splitTempoRegionButton.setBounds(x, buttonsArea.getY(), 104, h);
      splitTempoRegionButton.setVisible(true);
      splitTempoRegionButton.setEnabled(selectedTempoRegionIndex >= 0);
      x += 108;
      tempoBpmLabel.setBounds(x, buttonsArea.getY(), 42, h);
      tempoBpmEditor.setBounds(x + 44, buttonsArea.getY(), 78, h);
      x += 128;
      tempoSignatureLabel.setBounds(x, buttonsArea.getY(), 62, h);
      tempoSignatureBox.setBounds(x + 64, buttonsArea.getY(), 78, h);
      tempoBpmLabel.setVisible(true);
      tempoBpmEditor.setVisible(true);
      tempoSignatureLabel.setVisible(true);
      tempoSignatureBox.setVisible(true);
      return;
    }

    std::vector<int> widths;
    widths.reserve(sectionAddButtons.size());
    int totalWidth = 0;
    for (const auto &btn : sectionAddButtons) {
      const auto text = btn->getButtonText();
      const int roughTextPx = text.length() * 7;
      const int width = juce::jlimit(56, 96, 20 + roughTextPx);
      widths.push_back(width);
      totalWidth += width;
    }

    if (!widths.empty())
      totalWidth += buttonGap * (static_cast<int>(widths.size()) - 1);

    const bool compactSections = buttonsArea.getWidth() <= totalWidth;
    sectionMenuButton.setVisible(compactSections);
    sectionMenuButton.setEnabled(compactSections);

    if (compactSections) {
      const int actionCount = static_cast<int>(sectionAddButtons.size());
      const int fixedStart = juce::jmax(0, actionCount - 2);
      const int sectionMenuW = 96;
      int x = buttonsArea.getX();
      sectionMenuButton.setBounds(x, buttonsArea.getY(), sectionMenuW,
                                  buttonsArea.getHeight());
      x += sectionMenuW + buttonGap;

      for (int i = 0; i < actionCount; ++i) {
        auto &button = sectionAddButtons[static_cast<size_t>(i)];
        if (i < fixedStart) {
          button->setVisible(false);
          button->setEnabled(false);
          continue;
        }
        const int width =
            juce::jlimit(70, 102, 20 + (button->getButtonText().length() * 7));
        const bool fits = (x + width) <= buttonsArea.getRight();
        button->setBounds(x, buttonsArea.getY(), width, buttonsArea.getHeight());
        button->setVisible(fits);
        button->setEnabled(fits);
        x += width + buttonGap;
      }
    } else {
      int x = buttonsArea.getX();
      if (buttonsArea.getWidth() > totalWidth)
        x += (buttonsArea.getWidth() - totalWidth) / 2;

      for (size_t i = 0; i < sectionAddButtons.size(); ++i) {
        const int width = widths[i];
        sectionAddButtons[i]->setBounds(x, buttonsArea.getY(), width,
                                        buttonsArea.getHeight());
        sectionAddButtons[i]->setVisible(true);
        sectionAddButtons[i]->setEnabled(true);
        x += width + buttonGap;
      }
    }
    return;
  }

  guideMapButton.setVisible(false);
  guideMapButton.setEnabled(false);
  tempoMapButton.setVisible(false);
  tempoMapButton.setEnabled(false);
  splitTempoRegionButton.setVisible(false);
  splitTempoRegionButton.setEnabled(false);
  tempoBpmLabel.setVisible(false);
  tempoBpmEditor.setVisible(false);
  tempoSignatureLabel.setVisible(false);
  tempoSignatureBox.setVisible(false);
  sectionMenuButton.setVisible(false);
  sectionMenuButton.setEnabled(false);
  for (auto &btn : sectionAddButtons) {
    btn->setVisible(false);
    btn->setEnabled(false);
  }
  snapModeLabel.setVisible(false);
  snapModeLabel.setEnabled(false);
  snapModeBox.setVisible(false);
  snapModeBox.setEnabled(false);

  if (!placeBottomWaveControls(true, false)) {
    editButton.setBounds(toolbarBounds.removeFromRight(editButtonW).reduced(1));
    toolbarBounds.removeFromRight(4);
    resetButton.setBounds(toolbarBounds.removeFromRight(resetButtonW).reduced(1));
    toolbarBounds.removeFromRight(4);
    expandButton.setBounds(toolbarBounds.removeFromRight(expandButtonW).reduced(1));
    toolbarBounds.removeFromRight(followGap);
    followButton.setBounds(toolbarBounds.removeFromRight(followButtonW).reduced(1));
  }
}

void WaveformBar::mouseDown(const juce::MouseEvent &event) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  dragMode = DragMode::none;
  deferActiveResizeTimelineUpdate = false;
  activeDragPartId.clear();
  dragPartOrigins.clear();
  selectionDragAdditive = false;
  selectionDragBaseIds.clear();
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  arrangementDragPendingCommit = false;
  tempoDragPendingCommit = false;
  activeDragTempoRegionIndex = -1;

  const auto bounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);

  if (miniMapBounds.getWidth() > 2.0f && miniMapBounds.contains(event.position)) {
    if (event.mods.isPopupMenu())
      return;

    followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                             kFollowSuspendAfterManualViewportMs;

    const auto viewportBounds = getViewportBoundsInMiniMap(miniMapBounds);
    const float normalized = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - miniMapBounds.getX()) /
            juce::jmax(1.0f, miniMapBounds.getWidth()));

    dragViewportStart = viewportStart;
    dragViewportSpan = viewportSpan;
    dragAnchorNormalized = normalized;

    const bool nearLeft =
        std::abs(event.position.x - viewportBounds.getX()) <=
        kViewportHandleHitWidth;
    const bool nearRight =
        std::abs(event.position.x - viewportBounds.getRight()) <=
        kViewportHandleHitWidth;

    if (nearLeft)
      dragMode = DragMode::resizeViewportLeft;
    else if (nearRight)
      dragMode = DragMode::resizeViewportRight;
    else {
      if (!viewportBounds.contains(event.position))
        setVisibleWindow(normalized - (viewportSpan * 0.5f), viewportSpan);
      dragMode = DragMode::panViewport;
      dragViewportStart = viewportStart;
      dragViewportSpan = viewportSpan;
      dragAnchorNormalized = normalized;
    }

    if (dragMode == DragMode::resizeViewportLeft ||
        dragMode == DragMode::resizeViewportRight)
      setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);

    repaint();
    return;
  }

  if (seekRulerBounds.contains(event.position)) {
    if (event.mods.isPopupMenu())
      return;
    dragMode = DragMode::seek;
    handleSeek(event.position.x, seekRulerBounds);
    return;
  }

  if (!mainWaveBounds.contains(event.position))
    return;

  if (isEditingArrangement() && timelineViewMode == TimelineViewMode::TempoMap &&
      mainWaveBodyBounds.contains(event.position)) {
    const auto hit = hitTestTempoRegion(event.position);
    if (juce::isPositiveAndBelow(hit.regionIndex,
                                 static_cast<int>(tempoMap.regions.size()))) {
      selectTempoRegion(hit.regionIndex);
      if (hit.deleteButton) {
        deleteTempoRegion(hit.regionIndex);
        repaint();
        return;
      }
      const auto &region =
          tempoMap.regions[static_cast<size_t>(hit.regionIndex)];
      activeDragTempoRegionIndex = hit.regionIndex;
      dragOriginTempoStartSec = region.startSec;
      dragOriginTempoEndSec = region.endSec;
      dragAnchorSourceSec =
          windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);

      if (hit.leftEdge)
        dragMode = DragMode::resizeTempoRegionLeft;
      else if (hit.rightEdge)
        dragMode = DragMode::resizeTempoRegionRight;
      else
        dragMode = DragMode::moveTempoRegion;

      if (dragMode == DragMode::resizeTempoRegionLeft ||
          dragMode == DragMode::resizeTempoRegionRight)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
      else
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);

      repaint();
      return;
    }
  }

  if (mainWaveBodyBounds.contains(event.position)) {
    rebuildPartOverlayBounds(mainWaveBodyBounds);
    const auto actionHit = hitTestArrangementPart(event.position);
    if (juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.loopButton) {
      setSingleSelectedPart(actionHit.partIndex);
      cancelDeferredPartJumpFromDoubleClick();
      if (onArrangementPartLoopToggleRequest)
        onArrangementPartLoopToggleRequest(actionHit.partIndex);
      repaint();
      return;
    }

    if (juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.muteButton) {
      setSingleSelectedPart(actionHit.partIndex);
      cancelDeferredPartJumpFromDoubleClick();
      toggleArrangementPartMute(actionHit.partIndex);
      repaint();
      return;
    }

    if (juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.jumpButton) {
      setSingleSelectedPart(actionHit.partIndex);
      cancelDeferredPartJumpFromDoubleClick();
      if (onArrangementPartJumpRequest != nullptr)
        onArrangementPartJumpRequest(actionHit.partIndex);
      repaint();
      return;
    }
  }

  if (isEditingArrangement() && mainWaveBodyBounds.contains(event.position)) {
    rebuildPartOverlayBounds(mainWaveBodyBounds);
    const auto hit = hitTestArrangementPart(event.position);
    if (juce::isPositiveAndBelow(hit.partIndex,
                                 static_cast<int>(arrangement.parts.size()))) {
      if (hit.deleteButton) {
        setSingleSelectedPart(hit.partIndex);
        deleteArrangementPart(hit.partIndex);
        repaint();
        return;
      }
      if (hit.muteButton) {
        setSingleSelectedPart(hit.partIndex);
        toggleArrangementPartMute(hit.partIndex);
        repaint();
        return;
      }
      if (hit.addButton) {
        setSingleSelectedPart(hit.partIndex);
        for (const auto &overlay : partOverlayBounds) {
          if (overlay.partIndex == hit.partIndex) {
            showInsertSectionMenu(hit.partIndex, overlay.addButton);
            break;
          }
        }
        repaint();
        return;
      }
      const auto &part = arrangement.parts[static_cast<size_t>(hit.partIndex)];

      const bool multiSelectGesture =
          event.mods.isShiftDown() || event.mods.isCommandDown() ||
          event.mods.isCtrlDown();
      if (multiSelectGesture && !hit.leftEdge && !hit.rightEdge &&
          !event.mods.isPopupMenu()) {
        togglePartSelection(hit.partIndex);
        repaint();
        return;
      }

      if (!hit.leftEdge && !hit.rightEdge) {
        const bool isEdgePart =
            hit.partIndex == 0 ||
            hit.partIndex == static_cast<int>(arrangement.parts.size()) - 1;
        const bool canMovePart =
            isEdgePart &&
            (!juce::isPositiveAndBelow(hit.partIndex,
                                       static_cast<int>(arrangement.clips.size())) ||
             arrangement.clips[static_cast<size_t>(hit.partIndex)]
                 .primaryInstance);
        ensurePartSelectedForDrag(hit.partIndex);
        if (!canMovePart) {
          setMouseCursor(juce::MouseCursor::NormalCursor);
          repaint();
          return;
        }

        if (arrangement.clips.size() != arrangement.parts.size())
          rebuildClipsFromParts();

        activeDragPartId = part.id;
        dragOriginPartStartSec = part.startSec;
        dragOriginPartEndSec = part.endSec;
        dragAnchorSourceSec =
            windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);
        dragMode = DragMode::movePart;
        dragPartOrigins.clear();
        captureDragPartOrigins();
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        repaint();
        return;
      }

      setSingleSelectedPart(hit.partIndex);
      if (arrangement.clips.size() != arrangement.parts.size())
        rebuildClipsFromParts();

      activeDragPartId = part.id;
      dragOriginPartStartSec = part.startSec;
      dragOriginPartEndSec = part.endSec;
      dragAnchorSourceSec =
          windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);

      if (hit.leftEdge)
        dragMode = DragMode::resizePartLeft;
      else if (hit.rightEdge)
        dragMode = DragMode::resizePartRight;
      else
        dragMode = DragMode::movePart;

      deferActiveResizeTimelineUpdate =
          dragMode == DragMode::resizePartLeft ||
          dragMode == DragMode::resizePartRight;

      if (dragMode == DragMode::movePart)
        captureDragPartOrigins();
      else
        dragPartOrigins.clear();

      if (dragMode == DragMode::resizePartLeft ||
          dragMode == DragMode::resizePartRight)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
      else
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);

      repaint();
      return;
    }

    if (!event.mods.isPopupMenu()) {
      const bool additive =
          event.mods.isShiftDown() || event.mods.isCommandDown() ||
          event.mods.isCtrlDown();
      if (!additive) {
        selectedPartIds.clear();
        selectedPartIndex = -1;
      }
      dragMode = DragMode::selectParts;
      selectionDragStart = event.position;
      selectionDragCurrent = event.position;
      selectionDragAdditive = additive;
      selectionDragBaseIds = additive ? selectedPartIds : std::vector<juce::String>{};
      setMouseCursor(juce::MouseCursor::CrosshairCursor);
      repaint();
      return;
    }
  }

  if (event.mods.isPopupMenu())
    return;
}

void WaveformBar::mouseDrag(const juce::MouseEvent &event) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (midiLearnActive)
    return;

  const auto bounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);
  const auto seekBounds =
      mainWaveBodyBounds.getWidth() > 2.0f ? mainWaveBodyBounds : mainWaveBounds;

  if (dragMode == DragMode::seek) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    handleSeek(event.position.x, seekBounds);
    return;
  }

  if (dragMode == DragMode::panViewport ||
      dragMode == DragMode::resizeViewportLeft ||
      dragMode == DragMode::resizeViewportRight) {
    followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                             kFollowSuspendAfterManualViewportMs;
    if (dragMode == DragMode::panViewport)
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
      setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);

    if (miniMapBounds.getWidth() <= 2.0f)
      return;

    const float normalized = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - miniMapBounds.getX()) /
            juce::jmax(1.0f, miniMapBounds.getWidth()));

    if (dragMode == DragMode::panViewport) {
      const float delta = normalized - dragAnchorNormalized;
      setVisibleWindow(dragViewportStart + delta, dragViewportSpan);
      return;
    }

    if (dragMode == DragMode::resizeViewportLeft) {
      const float right = dragViewportStart + dragViewportSpan;
      const float nextStart =
          juce::jlimit(0.0f, right - kMinViewportSpan, normalized);
      setVisibleWindow(nextStart, right - nextStart);
      return;
    }

    const float minEnd = dragViewportStart + kMinViewportSpan;
    const float nextEnd = juce::jlimit(minEnd, 1.0f, normalized);
    setVisibleWindow(dragViewportStart, nextEnd - dragViewportStart);
    return;
  }

  if (dragMode == DragMode::selectParts) {
    if (!isEditingArrangement())
      return;
    selectionDragCurrent = event.position;
    updateSelectionFromDragBounds();
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    repaint();
    return;
  }

  if (dragMode == DragMode::moveTempoRegion ||
      dragMode == DragMode::resizeTempoRegionLeft ||
      dragMode == DragMode::resizeTempoRegionRight) {
    if (!isEditingArrangement() || timelineViewMode != TimelineViewMode::TempoMap)
      return;

    if (dragMode == DragMode::moveTempoRegion)
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
      setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);

    if (!juce::isPositiveAndBelow(activeDragTempoRegionIndex,
                                  static_cast<int>(tempoMap.regions.size())))
      return;

    const double songLength = getSongLengthSeconds();
    if (songLength <= 0.0)
      return;

    const double pointerSec =
        windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);
    const bool bypassSnap = isSnapBypassed(event.mods);
    const double deltaSec =
        snapSeconds(pointerSec - dragAnchorSourceSec, bypassSnap);

    auto &region =
        tempoMap.regions[static_cast<size_t>(activeDragTempoRegionIndex)];
    const double previousStart = region.startSec;
    const double previousEnd = region.endSec;

    if (dragMode == DragMode::moveTempoRegion) {
      double duration = juce::jmax(kMinPartDurationSec,
                                   dragOriginTempoEndSec -
                                       dragOriginTempoStartSec);
      duration = juce::jmin(duration, songLength);
      const double maxStart = juce::jmax(0.0, songLength - duration);
      const double nextStart =
          juce::jlimit(0.0, maxStart,
                       snapSeconds(dragOriginTempoStartSec + deltaSec,
                                   bypassSnap));
      region.startSec = nextStart;
      region.endSec = juce::jlimit(nextStart, songLength, nextStart + duration);
    } else if (dragMode == DragMode::resizeTempoRegionLeft) {
      const double maxStart =
          juce::jmax(0.0, dragOriginTempoEndSec - kMinPartDurationSec);
      region.startSec =
          juce::jlimit(0.0, maxStart,
                       snapSeconds(dragOriginTempoStartSec + deltaSec,
                                   bypassSnap));
      region.endSec = dragOriginTempoEndSec;
    } else if (dragMode == DragMode::resizeTempoRegionRight) {
      const double minEnd = dragOriginTempoStartSec + kMinPartDurationSec;
      region.startSec = dragOriginTempoStartSec;
      region.endSec =
          juce::jlimit(minEnd, songLength,
                       snapSeconds(dragOriginTempoEndSec + deltaSec,
                                   bypassSnap));
    }

    if (region.endSec < region.startSec)
      std::swap(region.endSec, region.startSec);

    if ((region.endSec - region.startSec) < kMinPartDurationSec) {
      region.endSec =
          juce::jmin(songLength, region.startSec + kMinPartDurationSec);
      if ((region.endSec - region.startSec) < kMinPartDurationSec)
        region.startSec = juce::jmax(0.0, region.endSec - kMinPartDurationSec);
    }

    if (nearlyEqual(previousStart, region.startSec) &&
        nearlyEqual(previousEnd, region.endSec))
      return;

    selectedTempoRegionIndex = activeDragTempoRegionIndex;
    tempoDragPendingCommit = true;
    updateTempoControlsFromSelection();
    repaint();
    return;
  }

  if (dragMode != DragMode::movePart && dragMode != DragMode::resizePartLeft &&
      dragMode != DragMode::resizePartRight)
    return;

  if (dragMode == DragMode::movePart)
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
  else
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);

  if (!isEditingArrangement() || mainWaveBodyBounds.getWidth() <= 2.0f)
    return;

  const int partIndex = findPartIndexById(activeDragPartId);
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return;

  const double pointerSec =
      windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);
  const bool bypassSnap = isSnapBypassed(event.mods);
  const double rawDeltaSec = pointerSec - dragAnchorSourceSec;
  const double deltaSec = snapSeconds(rawDeltaSec, bypassSnap);
  const double anchorAttachThresholdSec = getAnchorAttachThresholdSeconds();
  const double anchorReleaseThresholdSec = getAnchorReleaseThresholdSeconds();

  if (dragMode == DragMode::movePart && dragPartOrigins.size() > 1) {
    double minStart = std::numeric_limits<double>::max();
    double maxEnd = 0.0;
    for (const auto &origin : dragPartOrigins) {
      minStart = juce::jmin(minStart, origin.startSec);
      maxEnd = juce::jmax(maxEnd, origin.endSec);
    }

    if (dragPartOrigins.empty())
      return;

    double appliedDelta = deltaSec;
    appliedDelta = juce::jmax(appliedDelta, -minStart);
    appliedDelta = juce::jmin(appliedDelta, songLength - maxEnd);

    bool changed = false;
    for (const auto &origin : dragPartOrigins) {
      const int idx = findPartIndexById(origin.id);
      if (!juce::isPositiveAndBelow(idx,
                                    static_cast<int>(arrangement.parts.size())))
        continue;

      auto &part = arrangement.parts[static_cast<size_t>(idx)];
      const double previousStart = part.startSec;
      const double previousEnd = part.endSec;
      part.startSec = juce::jlimit(0.0, songLength, origin.startSec + appliedDelta);
      part.endSec = juce::jlimit(0.0, songLength, origin.endSec + appliedDelta);
      if ((part.endSec - part.startSec) < kMinPartDurationSec)
        part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);

      changed = changed || !nearlyEqual(previousStart, part.startSec) ||
                !nearlyEqual(previousEnd, part.endSec);
    }

    if (!changed)
      return;

    selectedPartIndex = partIndex;
    customArrangement = true;
    arrangementDragPendingCommit = true;
    repaint();
    return;
  }

  struct AnchorBoundaryResult {
    bool found = false;
    double boundarySec = 0.0;
    double distanceSec = std::numeric_limits<double>::max();
  };

  const auto findNearestBoundary =
      [this, partIndex](const double candidateSec,
                        const double maxDistanceSec) -> AnchorBoundaryResult {
    AnchorBoundaryResult best;
    for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
      if (i == partIndex)
        continue;
      const auto &otherPart = arrangement.parts[static_cast<size_t>(i)];
      const double boundaries[2] = {otherPart.startSec, otherPart.endSec};
      for (const auto boundarySec : boundaries) {
        const double distance = std::abs(boundarySec - candidateSec);
        if (distance <= maxDistanceSec && distance < best.distanceSec) {
          best.found = true;
          best.distanceSec = distance;
          best.boundarySec = boundarySec;
        }
      }
    }
    return best;
  };

  const auto applyStickyAnchorToEdge =
      [this, &findNearestBoundary, bypassSnap, anchorAttachThresholdSec,
       anchorReleaseThresholdSec](const double candidateEdgeSec,
                                  const bool edgeIsStart) -> double {
    if (bypassSnap) {
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
      return candidateEdgeSec;
    }

    if (dragAnchorActive && dragAnchorEdgeIsStart == edgeIsStart) {
      if (std::abs(candidateEdgeSec - dragAnchoredBoundarySec) <=
          anchorReleaseThresholdSec) {
        return dragAnchoredBoundarySec;
      }
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
    }

    const auto nearest =
        findNearestBoundary(candidateEdgeSec, anchorAttachThresholdSec);
    if (nearest.found) {
      dragAnchorActive = true;
      dragAnchorEdgeIsStart = edgeIsStart;
      dragAnchoredBoundarySec = nearest.boundarySec;
      return nearest.boundarySec;
    }

    return candidateEdgeSec;
  };

  auto &part = arrangement.parts[static_cast<size_t>(partIndex)];
  bool primaryClipInstance = true;
  double clipSourceStartSec = 0.0;
  double clipSourceEndSec = songLength;
  if (juce::isPositiveAndBelow(partIndex,
                               static_cast<int>(arrangement.clips.size()))) {
    const auto &clip = arrangement.clips[static_cast<size_t>(partIndex)];
    primaryClipInstance = clip.primaryInstance;
    if (!primaryClipInstance && clip.sourceEndSec > clip.sourceStartSec) {
      clipSourceStartSec = clip.sourceStartSec;
      clipSourceEndSec = clip.sourceEndSec;
    }
  }
  const double previousStart = part.startSec;
  const double previousEnd = part.endSec;

  if (dragMode == DragMode::movePart) {
    double duration = juce::jmax(kMinPartDurationSec,
                                 dragOriginPartEndSec - dragOriginPartStartSec);
    duration = juce::jmin(duration, songLength);

    const double maxStart = juce::jmax(0.0, songLength - duration);
    double nextStart = snapSeconds(dragOriginPartStartSec + deltaSec, bypassSnap);
    nextStart = juce::jlimit(0.0, maxStart, nextStart);

    double nextEnd = juce::jlimit(nextStart, songLength, nextStart + duration);
    const double rawNextStart =
        juce::jlimit(0.0, maxStart, dragOriginPartStartSec + rawDeltaSec);
    const double rawNextEnd =
        juce::jlimit(rawNextStart, songLength, rawNextStart + duration);

    if (!bypassSnap && dragAnchorActive) {
      const double activeEdge = dragAnchorEdgeIsStart ? rawNextStart : rawNextEnd;
      if (std::abs(activeEdge - dragAnchoredBoundarySec) <=
          anchorReleaseThresholdSec) {
        const double snappedActiveEdge = dragAnchorEdgeIsStart ? nextStart : nextEnd;
        const double offset = dragAnchoredBoundarySec - snappedActiveEdge;
        nextStart += offset;
        nextEnd += offset;
      } else {
        dragAnchorActive = false;
        dragAnchoredBoundarySec = 0.0;
      }
    }

    if (!bypassSnap && !dragAnchorActive) {
      const auto startAnchor =
          findNearestBoundary(rawNextStart, anchorAttachThresholdSec);
      const auto endAnchor =
          findNearestBoundary(rawNextEnd, anchorAttachThresholdSec);
      if (startAnchor.found || endAnchor.found) {
        const bool anchorStart =
            !endAnchor.found ||
            (startAnchor.found &&
             startAnchor.distanceSec <= endAnchor.distanceSec);
        dragAnchorActive = true;
        dragAnchorEdgeIsStart = anchorStart;
        dragAnchoredBoundarySec =
            anchorStart ? startAnchor.boundarySec : endAnchor.boundarySec;

        const double snappedActiveEdge = anchorStart ? nextStart : nextEnd;
        const double offset = dragAnchoredBoundarySec - snappedActiveEdge;
        nextStart += offset;
        nextEnd += offset;
      }
    }

    if (nextStart < 0.0) {
      nextStart = 0.0;
      nextEnd = juce::jmin(songLength, duration);
    } else if (nextEnd > songLength) {
      nextEnd = songLength;
      nextStart = juce::jmax(0.0, songLength - duration);
    }

    part.startSec = nextStart;
    part.endSec = nextEnd;
  } else if (dragMode == DragMode::resizePartLeft) {
    double nextStart = snapSeconds(dragOriginPartStartSec + deltaSec, bypassSnap);
    const double anchoredStart =
        applyStickyAnchorToEdge(dragOriginPartStartSec + rawDeltaSec, true);
    if (dragAnchorActive && dragAnchorEdgeIsStart)
      nextStart = anchoredStart;
    const double maxStart =
        juce::jmax(clipSourceStartSec, dragOriginPartEndSec - kMinPartDurationSec);
    part.startSec = juce::jlimit(clipSourceStartSec, maxStart, nextStart);
    part.endSec = dragOriginPartEndSec;

    if (dragAnchorActive && dragAnchorEdgeIsStart &&
        !nearlyEqual(part.startSec, dragAnchoredBoundarySec, 0.001)) {
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
    }
  } else if (dragMode == DragMode::resizePartRight) {
    double nextEnd = snapSeconds(dragOriginPartEndSec + deltaSec, bypassSnap);
    const double anchoredEnd =
        applyStickyAnchorToEdge(dragOriginPartEndSec + rawDeltaSec, false);
    if (dragAnchorActive && !dragAnchorEdgeIsStart)
      nextEnd = anchoredEnd;
    const double minEnd = dragOriginPartStartSec + kMinPartDurationSec;
    part.startSec = dragOriginPartStartSec;
    part.endSec = juce::jlimit(minEnd, clipSourceEndSec, nextEnd);

    if (dragAnchorActive && !dragAnchorEdgeIsStart &&
        !nearlyEqual(part.endSec, dragAnchoredBoundarySec, 0.001)) {
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
    }
  }

  if (part.endSec < part.startSec)
    std::swap(part.endSec, part.startSec);

  if ((part.endSec - part.startSec) < kMinPartDurationSec) {
    part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);
    if ((part.endSec - part.startSec) < kMinPartDurationSec)
      part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
  }

  if (nearlyEqual(previousStart, part.startSec) &&
      nearlyEqual(previousEnd, part.endSec))
    return;

  if (juce::isPositiveAndBelow(partIndex,
                               static_cast<int>(arrangement.clips.size()))) {
    auto &clip = arrangement.clips[static_cast<size_t>(partIndex)];
    if (clip.primaryInstance) {
      clip.sourceStartSec = juce::jlimit(0.0, songLength, part.startSec);
      clip.sourceEndSec = juce::jlimit(0.0, songLength, part.endSec);
      if (clip.sourceEndSec < clip.sourceStartSec)
        std::swap(clip.sourceStartSec, clip.sourceEndSec);
      clip.playbackStartSec = clip.sourceStartSec;
      clip.playbackEndSec = clip.sourceEndSec;
    } else {
      clip.playbackStartSec =
          juce::jlimit(clip.sourceStartSec, clip.sourceEndSec, part.startSec);
      clip.playbackEndSec =
          juce::jlimit(clip.sourceStartSec, clip.sourceEndSec, part.endSec);
    }
    if (clip.playbackEndSec < clip.playbackStartSec)
      std::swap(clip.playbackStartSec, clip.playbackEndSec);
    part.startSec =
        clip.primaryInstance ? clip.sourceStartSec : clip.playbackStartSec;
    part.endSec = clip.primaryInstance ? clip.sourceEndSec : clip.playbackEndSec;
  }

  selectedPartIndex = partIndex;
  pruneSelectedPartIds();
  customArrangement = true;
  arrangementDragPendingCommit = true;
  repaint();
}

void WaveformBar::mouseUp(const juce::MouseEvent &) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (midiLearnActive)
    return;

  const bool wasArrangementDrag =
      dragMode == DragMode::movePart || dragMode == DragMode::resizePartLeft ||
      dragMode == DragMode::resizePartRight;
  const bool wasTempoDrag =
      dragMode == DragMode::moveTempoRegion ||
      dragMode == DragMode::resizeTempoRegionLeft ||
      dragMode == DragMode::resizeTempoRegionRight;
  const bool wasSelectionDrag = dragMode == DragMode::selectParts;
  dragMode = DragMode::none;
  deferActiveResizeTimelineUpdate = false;
  activeDragPartId.clear();
  dragPartOrigins.clear();
  activeDragTempoRegionIndex = -1;
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  if (wasArrangementDrag && arrangementDragPendingCommit) {
    arrangementDragPendingCommit = false;
    notifyArrangementChanged(true);
    repaint();
  } else {
    arrangementDragPendingCommit = false;
  }
  if (wasSelectionDrag)
    repaint();
  if (wasTempoDrag && tempoDragPendingCommit) {
    tempoDragPendingCommit = false;
    notifyTempoMapChanged();
    repaint();
  } else {
    tempoDragPendingCommit = false;
  }
  setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformBar::mouseMove(const juce::MouseEvent &event) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    return;
  }

  if (midiLearnActive) {
    if (hoveredActionPartIndex >= 0 || hoveredAddPartIndex >= 0 ||
        hoveredMutePartIndex >= 0 ||
        hoveredLoopPartIndex >= 0 ||
        hoveredJumpPartIndex >= 0) {
      hoveredActionPartIndex = -1;
      hoveredAddPartIndex = -1;
      hoveredMutePartIndex = -1;
      hoveredLoopPartIndex = -1;
      hoveredJumpPartIndex = -1;
      repaint();
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    return;
  }

  juce::MouseCursor nextCursor = juce::MouseCursor::NormalCursor;
  int nextHoveredActionPartIndex = -1;
  int nextHoveredAddPartIndex = -1;
  int nextHoveredMutePartIndex = -1;
  int nextHoveredLoopPartIndex = -1;
  int nextHoveredJumpPartIndex = -1;
  const auto bounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);

  if (miniMapBounds.contains(event.position) && miniMapBounds.getWidth() > 2.0f) {
    const auto viewportBounds = getViewportBoundsInMiniMap(miniMapBounds);
    const bool nearLeft =
        std::abs(event.position.x - viewportBounds.getX()) <=
        kViewportHandleHitWidth;
    const bool nearRight =
        std::abs(event.position.x - viewportBounds.getRight()) <=
        kViewportHandleHitWidth;

    if (nearLeft || nearRight)
      nextCursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (viewportBounds.contains(event.position))
      nextCursor = juce::MouseCursor::DraggingHandCursor;
  } else if (seekRulerBounds.contains(event.position)) {
    nextCursor = juce::MouseCursor::PointingHandCursor;
  } else if (mainWaveBodyBounds.contains(event.position)) {
    if (isEditingArrangement() && timelineViewMode == TimelineViewMode::TempoMap) {
      const auto hit = hitTestTempoRegion(event.position);
      if (juce::isPositiveAndBelow(hit.regionIndex,
                                   static_cast<int>(tempoMap.regions.size()))) {
        if (hit.deleteButton)
          nextCursor = juce::MouseCursor::PointingHandCursor;
        else if (hit.leftEdge || hit.rightEdge)
          nextCursor = juce::MouseCursor::LeftRightResizeCursor;
        else
          nextCursor = juce::MouseCursor::DraggingHandCursor;
      }
    } else {
    rebuildPartOverlayBounds(mainWaveBodyBounds);
    const auto hit = hitTestArrangementPart(event.position);
    if (juce::isPositiveAndBelow(hit.partIndex,
                                 static_cast<int>(arrangement.parts.size()))) {
      nextHoveredActionPartIndex = hit.partIndex;
      if (hit.loopButton) {
        nextHoveredLoopPartIndex = hit.partIndex;
        nextCursor = juce::MouseCursor::PointingHandCursor;
      } else if (hit.muteButton) {
        nextHoveredMutePartIndex = hit.partIndex;
        nextCursor = juce::MouseCursor::PointingHandCursor;
      } else if (hit.addButton) {
        nextHoveredAddPartIndex = hit.partIndex;
        nextCursor = juce::MouseCursor::PointingHandCursor;
      } else if (hit.jumpButton) {
        nextHoveredJumpPartIndex = hit.partIndex;
        nextCursor = juce::MouseCursor::PointingHandCursor;
      } else if (isEditingArrangement() && hit.deleteButton)
        nextCursor = juce::MouseCursor::PointingHandCursor;
      else if (isEditingArrangement() && (hit.leftEdge || hit.rightEdge))
        nextCursor = juce::MouseCursor::LeftRightResizeCursor;
      else if (isEditingArrangement()) {
        const bool isEdgePart =
            hit.partIndex == 0 ||
            hit.partIndex == static_cast<int>(arrangement.parts.size()) - 1;
        const bool canMovePart =
            isEdgePart &&
            (!juce::isPositiveAndBelow(hit.partIndex,
                                       static_cast<int>(arrangement.clips.size())) ||
             arrangement.clips[static_cast<size_t>(hit.partIndex)]
                 .primaryInstance);
        nextCursor = canMovePart ? juce::MouseCursor::DraggingHandCursor
                                 : juce::MouseCursor::NormalCursor;
      }
    }
    }
  }

  if (hoveredActionPartIndex != nextHoveredActionPartIndex ||
      hoveredAddPartIndex != nextHoveredAddPartIndex ||
      hoveredMutePartIndex != nextHoveredMutePartIndex ||
      hoveredLoopPartIndex != nextHoveredLoopPartIndex ||
      hoveredJumpPartIndex != nextHoveredJumpPartIndex) {
    hoveredActionPartIndex = nextHoveredActionPartIndex;
    hoveredAddPartIndex = nextHoveredAddPartIndex;
    hoveredMutePartIndex = nextHoveredMutePartIndex;
    hoveredLoopPartIndex = nextHoveredLoopPartIndex;
    hoveredJumpPartIndex = nextHoveredJumpPartIndex;
    repaint();
  }
  setMouseCursor(nextCursor);
}

void WaveformBar::mouseExit(const juce::MouseEvent &) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (hoveredActionPartIndex >= 0 || hoveredAddPartIndex >= 0 ||
      hoveredMutePartIndex >= 0 ||
      hoveredLoopPartIndex >= 0 ||
      hoveredJumpPartIndex >= 0) {
    hoveredActionPartIndex = -1;
    hoveredAddPartIndex = -1;
    hoveredMutePartIndex = -1;
    hoveredLoopPartIndex = -1;
    hoveredJumpPartIndex = -1;
    repaint();
  }
  setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformBar::mouseDoubleClick(const juce::MouseEvent &event) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (midiLearnActive)
    return;

  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  if (!mainWaveBodyBounds.contains(event.position))
    return;

  rebuildPartOverlayBounds(mainWaveBodyBounds);
  const auto hit = hitTestArrangementPart(event.position);
  if (juce::isPositiveAndBelow(hit.partIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    setSingleSelectedPart(hit.partIndex);
    if (isEditingArrangement()) {
      cancelDeferredPartJumpFromDoubleClick();
    } else {
      const int clicks = event.getNumberOfClicks();
      if (clicks >= 2) {
        scheduleDeferredPartJumpFromDoubleClick(hit.partIndex);
      }
    }
    repaint();
  }
}

bool WaveformBar::keyPressed(const juce::KeyPress &key) {
  const auto mods = key.getModifiers();
  const bool undoShortcut =
      (mods.isCommandDown() || mods.isCtrlDown()) &&
      !mods.isShiftDown() &&
      (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z');
  if (undoShortcut)
    return undoLastArrangementChange();

  const bool redoShortcut =
      (mods.isCommandDown() || mods.isCtrlDown()) &&
      (((key.getKeyCode() == 'z' || key.getKeyCode() == 'Z') &&
        mods.isShiftDown()) ||
       key.getKeyCode() == 'y' || key.getKeyCode() == 'Y');
  if (redoShortcut)
    return redoLastArrangementChange();

  if (!isEditingArrangement())
    return false;

  const bool copyShortcut =
      (mods.isCommandDown() || mods.isCtrlDown()) &&
      (key.getKeyCode() == 'c' || key.getKeyCode() == 'C');
  if (copyShortcut && timelineViewMode == TimelineViewMode::GuideMap)
    return copySelectedArrangementParts();

  const bool pasteShortcut =
      (mods.isCommandDown() || mods.isCtrlDown()) &&
      (key.getKeyCode() == 'v' || key.getKeyCode() == 'V');
  if (pasteShortcut && timelineViewMode == TimelineViewMode::GuideMap)
    return pasteArrangementClipboard();

  if (key == juce::KeyPress::deleteKey ||
      key == juce::KeyPress::backspaceKey) {
    if (timelineViewMode == TimelineViewMode::TempoMap)
      deleteTempoRegion(selectedTempoRegionIndex);
    else
      deleteSelectedArrangementParts();
    return true;
  }

  return false;
}

void WaveformBar::mouseWheelMove(const juce::MouseEvent &event,
                                 const juce::MouseWheelDetails &wheel) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (midiLearnActive)
    return;

  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const bool onWaveBody = mainWaveBodyBounds.contains(event.position);
  const bool onSeekRuler = seekRulerBounds.contains(event.position);
  if (!onWaveBody && !onSeekRuler)
    return;

  const float absDx = std::abs(wheel.deltaX);
  const float absDy = std::abs(wheel.deltaY);
  const bool horizontalPanGesture = absDx > 0.0f && absDx > (absDy * 1.05f);
  if (horizontalPanGesture) {
    const auto window = getVisibleWindow();
    const float intensity = juce::jlimit(1.0f, 3.0f, 1.0f + (absDx * 2.0f));
    const float deltaStart =
        -wheel.deltaX * intensity * juce::jmax(kMinViewportSpan, window.second) *
        kWheelPanSensitivity;
    followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                             kFollowSuspendAfterManualViewportMs;
    setVisibleWindow(window.first + deltaStart, window.second);
    return;
  }

  const float dir = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
  if (dir == 0.0f)
    return;

  const float anchor =
      mainWaveBodyBounds.getWidth() > 0.0f
          ? juce::jlimit(0.0f, 1.0f,
                         (event.position.x - mainWaveBodyBounds.getX()) /
                             mainWaveBodyBounds.getWidth())
          : 0.5f;

  const float intensity = juce::jlimit(1.0f, 3.5f, 1.0f + std::abs(dir) * 2.4f);
  const float step = 1.06f * intensity;
  followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                           kFollowSuspendAfterManualViewportMs;
  applyZoom(dir > 0.0f ? step : 1.0f / step, anchor);
}

void WaveformBar::mouseMagnify(const juce::MouseEvent &event,
                               float magnifyAmount) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (midiLearnActive)
    return;

  if (magnifyAmount == 0.0f)
    return;

  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const bool onWaveBody = mainWaveBodyBounds.contains(event.position);
  const bool onSeekRuler = seekRulerBounds.contains(event.position);
  if (!onWaveBody && !onSeekRuler)
    return;

  const float anchor =
      mainWaveBodyBounds.getWidth() > 0.0f
          ? juce::jlimit(0.0f, 1.0f,
                         (event.position.x - mainWaveBodyBounds.getX()) /
                             mainWaveBodyBounds.getWidth())
          : 0.5f;
  followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                           kFollowSuspendAfterManualViewportMs;
  applyZoom(1.0f + magnifyAmount, anchor);
}

void WaveformBar::timerCallback() {
  const double nowMs = juce::Time::getMillisecondCounterHiRes();

  if (songLoadingHideAfterMs > 0.0 &&
      nowMs >= songLoadingHideAfterMs) {
    songLoadingActive = false;
    songLoadingHideAfterMs = 0.0;
    songLoadingProgress01 = 0.0;
    songLoadingPhaseText.clear();
  }

  const bool isPlayingNow = playingProvider ? playingProvider() : false;
  float pendingJumpRatio = 0.0f;
  const bool hasPendingJumpRatio = getPendingJumpProgressRatio(pendingJumpRatio);
  const bool followJumpJustExecuted =
      followPendingJumpWasActive && !hasPendingJumpRatio &&
      followPlaybackEnabled && !isEditingArrangement() && isPlayingNow &&
      dragMode != DragMode::panViewport &&
      dragMode != DragMode::resizeViewportLeft &&
      dragMode != DragMode::resizeViewportRight;

  bool hasSongProgress = false;
  float songProgress = 0.0f;
  if (positionProvider != nullptr && lengthProvider != nullptr) {
    const double timelineLength = getVisualTimelineLengthSeconds();
    if (timelineLength > 0.0) {
      const double songPosSec = juce::jmax(0.0, positionProvider());
      const double visualPosSec = visualSecondsFromSourceSeconds(songPosSec);
      songProgress = static_cast<float>(
          juce::jlimit(0.0, 1.0, visualPosSec / timelineLength));
      hasSongProgress = true;
    }
  }

  if ((isFollowTrackingActive() || followJumpJustExecuted) && hasSongProgress) {
    auto window = getVisibleWindow();
    const float start = window.first;
    const float span = window.second;
    const float targetStart = songProgress - (kFollowCenterRatio * span);

    bool followDiscontinuity = false;
    if (followLastSongProgressValid) {
      const float delta = std::abs(songProgress - followLastSongProgressRatio);
      const float threshold = juce::jmax(0.05f, span * 0.45f);
      const bool wrappedBackwards =
          songProgress < (followLastSongProgressRatio - 0.0015f);
      followDiscontinuity = wrappedBackwards || (delta >= threshold);
    }

    if (!nearlyEqual(targetStart, start, 0.0001)) {
      if (followJumpJustExecuted || followDiscontinuity)
        setVisibleWindow(targetStart, span);
      else {
        const float smoothedStart =
            start + ((targetStart - start) * kFollowSmoothFactor);
        setVisibleWindow(smoothedStart, span);
      }
    }
  }

  followPendingJumpWasActive = followPlaybackEnabled && !isEditingArrangement() &&
                               isPlayingNow && hasPendingJumpRatio;
  if (followPlaybackEnabled && !isEditingArrangement() && isPlayingNow &&
      hasSongProgress) {
    followLastSongProgressRatio = songProgress;
    followLastSongProgressValid = true;
  } else {
    followLastSongProgressValid = false;
  }

  updateActiveTempoRegionFromTransport();
  flushDeferredPartJumpFromDoubleClickIfDue();
  repaint();
}

void WaveformBar::scheduleDeferredPartJumpFromDoubleClick(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    cancelDeferredPartJumpFromDoubleClick();
    return;
  }

  pendingJumpDoubleClickPartIndex = partIndex;
  const double timeoutMs = juce::jlimit(
      140.0, 260.0, static_cast<double>(juce::MouseEvent::getDoubleClickTimeout()));
  pendingJumpDoubleClickDispatchAtMs =
      juce::Time::getMillisecondCounterHiRes() + timeoutMs;
}

void WaveformBar::cancelDeferredPartJumpFromDoubleClick() {
  pendingJumpDoubleClickPartIndex = -1;
  pendingJumpDoubleClickDispatchAtMs = 0.0;
}

void WaveformBar::flushDeferredPartJumpFromDoubleClickIfDue(const bool force) {
  if (!juce::isPositiveAndBelow(pendingJumpDoubleClickPartIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    cancelDeferredPartJumpFromDoubleClick();
    return;
  }

  if (!force) {
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs < pendingJumpDoubleClickDispatchAtMs)
      return;
  }

  const int partIndex = pendingJumpDoubleClickPartIndex;
  cancelDeferredPartJumpFromDoubleClick();
  if (onArrangementPartJumpRequest != nullptr)
    onArrangementPartJumpRequest(partIndex);
}

void WaveformBar::handleSeek(float x) {
  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  if (seekRulerBounds.getWidth() > 2.0f)
    handleSeek(x, seekRulerBounds);
  else if (mainWaveBodyBounds.getWidth() > 2.0f)
    handleSeek(x, mainWaveBodyBounds);
  else
    handleSeek(x, mainWaveBounds);
}

void WaveformBar::handleSeek(float x, const juce::Rectangle<float> &seekBounds) {
  if (!onSeekTo || !lengthProvider)
    return;

  if (getVisualTimelineLengthSeconds() <= 0.0 || seekBounds.getWidth() <= 0.0f)
    return;

  onSeekTo(windowXToSourceSeconds(seekBounds, x));
}

std::pair<float, float> WaveformBar::getVisibleWindow() const {
  const float span = juce::jlimit(kMinViewportSpan, 1.0f, viewportSpan);
  float start = juce::jlimit(0.0f, 1.0f, viewportStart);
  if ((start + span) > 1.0f)
    start = 1.0f - span;
  return {start, span};
}

void WaveformBar::setVisibleWindow(float start, float span) {
  span = juce::jlimit(kMinViewportSpan, 1.0f, span);
  start = juce::jlimit(0.0f, 1.0f, start);
  if ((start + span) > 1.0f)
    start = 1.0f - span;

  const bool changed = !nearlyEqual(start, viewportStart) ||
                       !nearlyEqual(span, viewportSpan);
  viewportStart = start;
  viewportSpan = span;
  if (changed)
    repaint();
}

void WaveformBar::applyZoom(float factor, float anchorRatio) {
  if (!std::isfinite(factor) || factor <= 0.0f)
    return;

  anchorRatio = juce::jlimit(0.0f, 1.0f, anchorRatio);
  const float oldSpan = viewportSpan;
  const float newSpan =
      juce::jlimit(kMinViewportSpan, 1.0f, oldSpan / factor);
  const float anchorSource = viewportStart + (anchorRatio * oldSpan);
  const float newStart = anchorSource - (anchorRatio * newSpan);
  setVisibleWindow(newStart, newSpan);
}

bool WaveformBar::isFollowTrackingActive() const {
  if (!followPlaybackEnabled || isEditingArrangement())
    return false;
  if (dragMode == DragMode::panViewport ||
      dragMode == DragMode::resizeViewportLeft ||
      dragMode == DragMode::resizeViewportRight)
    return false;
  if (playingProvider == nullptr || !playingProvider())
    return false;
  const double nowMs = juce::Time::getMillisecondCounterHiRes();
  return nowMs >= followSuspendedUntilMs;
}

bool WaveformBar::getPendingJumpProgressRatio(float &ratioOut) const {
  if (pendingArrangementPartProvider == nullptr)
    return false;

  const int pendingPartIndex = pendingArrangementPartProvider();
  if (!juce::isPositiveAndBelow(pendingPartIndex,
                                static_cast<int>(arrangement.parts.size())))
    return false;

  const double timelineLength = getVisualTimelineLengthSeconds();
  if (timelineLength <= 0.0)
    return false;

  const double targetVisualSec =
      juce::jlimit(0.0, timelineLength,
                   visualSecondsForPartStart(pendingPartIndex));
  ratioOut = static_cast<float>(
      juce::jlimit(0.0, 1.0, targetVisualSec / timelineLength));
  return true;
}

double WaveformBar::getSongLengthSeconds() const {
  return lengthProvider ? juce::jmax(0.0, lengthProvider()) : 0.0;
}

std::vector<WaveformBar::VisualTimelineSegment>
WaveformBar::buildVisualTimelineSegments() const {
  std::vector<VisualTimelineSegment> segments;
  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return segments;

  if (!usesVirtualClips() || arrangement.parts.empty()) {
    segments.push_back({-1, 0.0, songLength, 0.0, songLength, true});
    return segments;
  }

  segments.reserve(arrangement.parts.size() + 4);
  double primarySourceCursor = 0.0;
  bool sawPrimarySegment = false;
  const auto appendSourceGap = [&segments](const double startSec,
                                           const double endSec) {
    if (endSec > startSec + 0.0001) {
      segments.push_back({-1, startSec, endSec, startSec, endSec, true});
    }
  };
  for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
    const auto &part = arrangement.parts[static_cast<size_t>(i)];
    double sourceStart = part.startSec;
    double sourceEnd = part.endSec;
    double playbackStart = part.startSec;
    double playbackEnd = part.endSec;
    double previewEnvelopeStart = sourceStart;
    double previewEnvelopeEnd = sourceEnd;
    bool primary = true;
    bool activeDuplicateResizePreview = false;
    if (juce::isPositiveAndBelow(i, static_cast<int>(arrangement.clips.size()))) {
      const auto &clip = arrangement.clips[static_cast<size_t>(i)];
      primary = clip.primaryInstance;
      activeDuplicateResizePreview =
          deferActiveResizeTimelineUpdate && !primary &&
          activeDragPartId.isNotEmpty() && clip.id == activeDragPartId;
      sourceStart = primary ? clip.sourceStartSec
                            : clip.playbackStartSec;
      sourceEnd = primary ? clip.sourceEndSec : clip.playbackEndSec;
      playbackStart = clip.playbackStartSec;
      playbackEnd = clip.playbackEndSec;
      if (activeDuplicateResizePreview) {
        previewEnvelopeStart = dragOriginPartStartSec;
        previewEnvelopeEnd = dragOriginPartEndSec;
      }
    }

    sourceStart = juce::jlimit(0.0, songLength, sourceStart);
    sourceEnd = juce::jlimit(sourceStart, songLength, sourceEnd);
    playbackStart = juce::jlimit(sourceStart, sourceEnd, playbackStart);
    playbackEnd = juce::jlimit(sourceStart, sourceEnd, playbackEnd);
    if (playbackEnd < playbackStart)
      std::swap(playbackStart, playbackEnd);
    if (primary) {
      sawPrimarySegment = true;
      appendSourceGap(primarySourceCursor, sourceStart);
      primarySourceCursor = juce::jmax(primarySourceCursor, sourceEnd);
    }
    if (activeDuplicateResizePreview) {
      previewEnvelopeStart =
          juce::jlimit(0.0, songLength, previewEnvelopeStart);
      previewEnvelopeEnd =
          juce::jlimit(previewEnvelopeStart, songLength, previewEnvelopeEnd);
      if (sourceStart > previewEnvelopeStart + 0.0001) {
        segments.push_back({-1, previewEnvelopeStart, sourceStart,
                            previewEnvelopeStart, sourceStart, true});
      }
    }
    if (sourceEnd > sourceStart + 0.0001)
      segments.push_back({i, sourceStart, sourceEnd, playbackStart,
                          playbackEnd, primary});
    if (activeDuplicateResizePreview &&
        previewEnvelopeEnd > sourceEnd + 0.0001) {
      segments.push_back({-1, sourceEnd, previewEnvelopeEnd, sourceEnd,
                          previewEnvelopeEnd, true});
    }
  }

  if (sawPrimarySegment)
    appendSourceGap(primarySourceCursor, songLength);

  if (segments.empty())
    segments.push_back({-1, 0.0, songLength, 0.0, songLength, true});
  return segments;
}

double WaveformBar::getVisualTimelineLengthSeconds() const {
  double total = 0.0;
  for (const auto &segment : buildVisualTimelineSegments())
    total += juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
  return total > 0.0 ? total : getSongLengthSeconds();
}

double WaveformBar::visualSecondsForPartStart(const int partIndex) const {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    return 0.0;
  }

  if (!usesVirtualClips())
    return arrangement.parts[static_cast<size_t>(partIndex)].startSec;

  double total = 0.0;
  for (const auto &segment : buildVisualTimelineSegments()) {
    if (segment.partIndex == partIndex)
      return total;
    total += juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
  }

  return total;
}

double WaveformBar::visualSecondsForPartEnd(const int partIndex) const {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    return 0.0;
  }
  if (!usesVirtualClips())
    return arrangement.parts[static_cast<size_t>(partIndex)].endSec;

  for (const auto &segment : buildVisualTimelineSegments()) {
    if (segment.partIndex == partIndex) {
      return visualSecondsForPartStart(partIndex) +
             juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
    }
  }
  return visualSecondsForPartStart(partIndex);
}

double WaveformBar::visualSecondsForPartPlaybackStart(const int partIndex) const {
  double total = 0.0;
  for (const auto &segment : buildVisualTimelineSegments()) {
    const double duration =
        juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
    if (segment.partIndex == partIndex) {
      return total + juce::jlimit(0.0, duration,
                                  segment.playbackStartSec -
                                      segment.sourceStartSec);
    }
    total += duration;
  }
  return visualSecondsForPartStart(partIndex);
}

double WaveformBar::visualSecondsForPartPlaybackEnd(const int partIndex) const {
  double total = 0.0;
  for (const auto &segment : buildVisualTimelineSegments()) {
    const double duration =
        juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
    if (segment.partIndex == partIndex) {
      return total + juce::jlimit(0.0, duration,
                                  segment.playbackEndSec -
                                      segment.sourceStartSec);
    }
    total += duration;
  }
  return visualSecondsForPartEnd(partIndex);
}

double WaveformBar::sourceSecondsFromVisualSeconds(
    const double visualSeconds) const {
  if (!usesVirtualClips())
    return visualSeconds;

  double remaining = juce::jmax(0.0, visualSeconds);
  for (const auto &segment : buildVisualTimelineSegments()) {
    const double duration =
        juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
    if (duration <= 0.0)
      continue;
    if (remaining <= duration)
      return segment.sourceStartSec + remaining;
    remaining -= duration;
  }

  const double songLength = getSongLengthSeconds();
  if (songLength > 0.0)
    return songLength;
  return 0.0;
}

double WaveformBar::visualSecondsFromSourceSeconds(
    const double sourceSeconds) const {
  if (!usesVirtualClips())
    return sourceSeconds;

  const auto segments = buildVisualTimelineSegments();
  const juce::String currentPartId =
      currentArrangementPartIdProvider ? currentArrangementPartIdProvider().trim()
                                       : juce::String{};
  const int currentPartIndex =
      currentArrangementPartProvider ? currentArrangementPartProvider() : -1;

  if (currentPartId.isNotEmpty()) {
    double currentTotal = 0.0;
    for (const auto &segment : segments) {
      const double duration =
          juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
      if (duration <= 0.0)
        continue;
      if (juce::isPositiveAndBelow(
              segment.partIndex, static_cast<int>(arrangement.parts.size()))) {
        const auto &part =
            arrangement.parts[static_cast<size_t>(segment.partIndex)];
        if (part.id == currentPartId &&
            sourceSeconds >= segment.sourceStartSec &&
            sourceSeconds <= segment.sourceEndSec) {
          return currentTotal + (sourceSeconds - segment.sourceStartSec);
        }
      }
      currentTotal += duration;
    }
  }

  if (currentPartIndex >= 0) {
    double currentTotal = 0.0;
    for (const auto &segment : segments) {
      const double duration =
          juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
      if (duration <= 0.0)
        continue;
      if (segment.partIndex == currentPartIndex &&
          sourceSeconds >= segment.sourceStartSec &&
          sourceSeconds <= segment.sourceEndSec) {
        return currentTotal + (sourceSeconds - segment.sourceStartSec);
      }
      currentTotal += duration;
    }
  }

  double total = 0.0;
  for (const auto &segment : segments) {
    const double duration =
        juce::jmax(0.0, segment.sourceEndSec - segment.sourceStartSec);
    if (duration <= 0.0)
      continue;
    if (sourceSeconds >= segment.sourceStartSec &&
        sourceSeconds <= segment.sourceEndSec) {
      return total + (sourceSeconds - segment.sourceStartSec);
    }
    total += duration;
  }

  return juce::jlimit(0.0, getVisualTimelineLengthSeconds(), total);
}

std::pair<double, double> WaveformBar::getPlayableRangeSeconds() const {
  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return {0.0, 0.0};

  double startSec = songWindowStartProvider ? songWindowStartProvider() : 0.0;
  double endSec = songWindowEndProvider ? songWindowEndProvider() : songLength;
  endSec = juce::jlimit(0.0, songLength, endSec);
  startSec = juce::jlimit(0.0, endSec, startSec);
  return {startSec, endSec};
}

double WaveformBar::getBarDurationSeconds() const {
  if (songBpm <= 0.0 || songBeatsPerBar <= 0)
    return 0.0;
  return (60.0 / songBpm) * static_cast<double>(songBeatsPerBar);
}

double WaveformBar::getSnapStepSeconds() const {
  const double beatDurationSec = songBpm > 0.0 ? 60.0 / songBpm : 0.0;
  const double barDurationSec = getBarDurationSeconds();

  switch (snapMode) {
  case SnapMode::Bar:
    return barDurationSec > 0.0
               ? barDurationSec
               : (beatDurationSec > 0.0 ? beatDurationSec : kPartSnapSeconds);
  case SnapMode::HalfBeat:
    return beatDurationSec > 0.0 ? (beatDurationSec * 0.5)
                                 : (kPartSnapSeconds * 0.5);
  case SnapMode::Free:
    return 0.0;
  case SnapMode::Beat:
  default:
    return beatDurationSec > 0.0 ? beatDurationSec : kPartSnapSeconds;
  }
}

double WaveformBar::getAnchorAttachThresholdSeconds() const {
  const double beatDurationSec = songBpm > 0.0 ? 60.0 / songBpm : 0.0;
  const double fallback = beatDurationSec > 0.0 ? beatDurationSec : 0.5;
  const double snapStepSec = getSnapStepSeconds();
  const double base = snapStepSec > 0.0 ? snapStepSec : fallback;
  return juce::jlimit(0.06, 0.80, base * 0.35);
}

double WaveformBar::getAnchorReleaseThresholdSeconds() const {
  const double beatDurationSec = songBpm > 0.0 ? 60.0 / songBpm : 0.0;
  const double fallback = beatDurationSec > 0.0 ? beatDurationSec : 0.5;
  const double snapStepSec = getSnapStepSeconds();
  const double base = snapStepSec > 0.0 ? snapStepSec : fallback;
  return juce::jlimit(0.10, 1.25, base * 0.65);
}

bool WaveformBar::isSnapBypassed(const juce::ModifierKeys &mods) const {
  return snapMode == SnapMode::Free || mods.isAltDown();
}

double WaveformBar::snapSeconds(const double seconds,
                                const bool bypassSnap) const {
  if (!std::isfinite(seconds))
    return 0.0;
  if (bypassSnap)
    return seconds;

  const double snapStepSec = getSnapStepSeconds();
  if (snapStepSec <= 0.0 || !std::isfinite(snapStepSec))
    return seconds;
  return std::round(seconds / snapStepSec) * snapStepSec;
}

double WaveformBar::snapSecondsForward(const double seconds) const {
  if (!std::isfinite(seconds))
    return 0.0;

  const double snapStepSec = getSnapStepSeconds();
  if (snapStepSec <= 0.0 || !std::isfinite(snapStepSec))
    return seconds;

  return std::ceil((seconds - 1.0e-9) / snapStepSec) * snapStepSec;
}

void WaveformBar::normalizeArrangement(const bool sortByStart,
                                       const bool snapToGrid) {
  const double songLength = getSongLengthSeconds();
  juce::String selectedId;
  if (juce::isPositiveAndBelow(selectedPartIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    selectedId = arrangement.parts[static_cast<size_t>(selectedPartIndex)].id;
  }

  for (auto &part : arrangement.parts) {
    if (part.id.isEmpty())
      part.id = juce::Uuid().toString();

    const auto trimmedName = part.name.trim();
    part.name = trimmedName.isEmpty() ? L10n::tr("Parte", "Part") : trimmedName;
    auto targetColour = standardArrangementPartColorForName(part.name);
    if (targetColour.isEmpty())
      targetColour = fallbackArrangementPartColorForName(part.name);
    part.color = targetColour;

    part.startSec = juce::jlimit(0.0, songLength, part.startSec);
    part.endSec = juce::jlimit(0.0, songLength, part.endSec);
    if (part.endSec < part.startSec)
      std::swap(part.endSec, part.startSec);

    if ((part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);
      if ((part.endSec - part.startSec) < kMinPartDurationSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }

    if (snapToGrid) {
      part.startSec = snapSeconds(part.startSec);
      part.endSec = snapSeconds(part.endSec);
    }

    part.startSec = juce::jlimit(0.0, songLength, part.startSec);
    part.endSec = juce::jlimit(0.0, songLength, part.endSec);
    if ((part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);
      if ((part.endSec - part.startSec) < kMinPartDurationSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }
  }

  if (sortByStart && arrangement.parts.size() > 1) {
    std::stable_sort(
        arrangement.parts.begin(), arrangement.parts.end(),
        [](const SongArrangementPartSnapshot &a,
           const SongArrangementPartSnapshot &b) {
          if (nearlyEqual(a.startSec, b.startSec))
            return false;
          return a.startSec < b.startSec;
        });
  }

  if (selectedId.isNotEmpty())
    selectedPartIndex = findPartIndexById(selectedId);

  pruneSelectedPartIds();

  if (!juce::isPositiveAndBelow(selectedPartIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    selectedPartIndex = arrangement.parts.empty() ? -1 : 0;
  }

  if (selectedPartIds.empty() && juce::isPositiveAndBelow(
                                     selectedPartIndex,
                                     static_cast<int>(arrangement.parts.size()))) {
    selectedPartIds.push_back(
        arrangement.parts[static_cast<size_t>(selectedPartIndex)].id);
  }
}

bool WaveformBar::usesVirtualClips() const { return !arrangement.clips.empty(); }

void WaveformBar::captureSourceArrangementParts(
    const SongArrangementSnapshot &snapshot) {
  sourceArrangementParts.clear();
  std::unordered_set<std::string> seenSourceKeys;

  const auto addPart = [this, &seenSourceKeys](
                           const juce::String &sourceId,
                           const juce::String &name, const juce::String &color,
                           const double startSec, const double endSec) {
    const auto key =
        (sourceId.trim().isNotEmpty()
             ? sourceId.trim()
             : name.trim() + "|" + juce::String(startSec, 4) + "|" +
                   juce::String(endSec, 4))
            .toStdString();
    if (!seenSourceKeys.insert(key).second)
      return;

    SongArrangementPartSnapshot part;
    part.id = sourceId.trim().isEmpty() ? juce::Uuid().toString()
                                        : sourceId.trim();
    part.name = name.trim().isEmpty() ? L10n::tr("Parte", "Part") : name.trim();
    part.color = color.trim();
    if (part.color.isEmpty())
      part.color = fallbackArrangementPartColorForName(part.name);
    part.startSec = startSec;
    part.endSec = endSec;
    sourceArrangementParts.push_back(std::move(part));
  };

  for (const auto &part : snapshot.parts) {
    addPart(part.id, part.name, part.color, part.startSec, part.endSec);
  }

  if (sourceArrangementParts.empty()) {
    for (const auto &clip : snapshot.clips) {
      addPart(clip.sourceSectionId, clip.label, clip.color,
              clip.sourceStartSec, clip.sourceEndSec);
    }
  }
}

void WaveformBar::rebuildPartsFromClips() {
  if (arrangement.clips.empty())
    return;

  arrangement.parts.clear();
  arrangement.parts.reserve(arrangement.clips.size());
  for (const auto &clip : arrangement.clips) {
    SongArrangementPartSnapshot part;
    part.id = clip.id;
    part.name = clip.label;
    part.color = clip.color;
    part.startSec =
        clip.primaryInstance ? clip.sourceStartSec : clip.playbackStartSec;
    part.endSec = clip.primaryInstance ? clip.sourceEndSec : clip.playbackEndSec;
    arrangement.parts.push_back(std::move(part));
  }
}

void WaveformBar::rebuildClipsFromParts() {
  std::unordered_map<std::string, SongArrangementClipSnapshot> existingById;
  existingById.reserve(arrangement.clips.size());
  for (const auto &clip : arrangement.clips) {
    if (!clip.id.isEmpty())
      existingById.emplace(clip.id.toStdString(), clip);
  }

  arrangement.clips.clear();
  arrangement.clips.reserve(arrangement.parts.size());
  for (auto &part : arrangement.parts) {
    SongArrangementClipSnapshot clip;
    auto it = existingById.find(part.id.toStdString());
    if (it != existingById.end())
      clip = it->second;

    clip.id = part.id;
    if (clip.id.isEmpty())
      clip.id = juce::Uuid().toString();
    if (clip.sourceSectionId.trim().isEmpty())
      clip.sourceSectionId = clip.id;
    clip.label = part.name;
    clip.color = part.color;
    if (clip.sourceEndSec <= clip.sourceStartSec) {
      clip.sourceStartSec = part.startSec;
      clip.sourceEndSec = part.endSec;
      clip.primaryInstance = true;
    }
    if (clip.primaryInstance) {
      clip.sourceStartSec = part.startSec;
      clip.sourceEndSec = part.endSec;
      if (clip.sourceEndSec < clip.sourceStartSec)
        std::swap(clip.sourceStartSec, clip.sourceEndSec);
      clip.playbackStartSec = clip.sourceStartSec;
      clip.playbackEndSec = clip.sourceEndSec;
    } else {
      clip.playbackStartSec =
          juce::jlimit(clip.sourceStartSec, clip.sourceEndSec, part.startSec);
      clip.playbackEndSec =
          juce::jlimit(clip.sourceStartSec, clip.sourceEndSec, part.endSec);
    }
    if (clip.playbackEndSec < clip.playbackStartSec)
      std::swap(clip.playbackStartSec, clip.playbackEndSec);
    part.startSec =
        clip.primaryInstance ? clip.sourceStartSec : clip.playbackStartSec;
    part.endSec = clip.primaryInstance ? clip.sourceEndSec : clip.playbackEndSec;
    arrangement.clips.push_back(std::move(clip));
  }
}

void WaveformBar::showInsertSectionMenu(
    const int afterPartIndex, const juce::Rectangle<float> &anchorBounds) {
  if (!juce::isPositiveAndBelow(afterPartIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    return;
  }

  struct SectionTemplate {
    juce::String name;
    juce::String color;
    juce::String sourceSectionId;
    double startSec = 0.0;
    double endSec = 0.0;
    bool deleted = false;
  };

  std::vector<SectionTemplate> templates;
  std::unordered_set<std::string> seenKeys;
  const auto &sourceParts =
      sourceArrangementParts.empty() ? arrangement.parts : sourceArrangementParts;
  templates.reserve(sourceParts.size());
  seenKeys.reserve(sourceParts.size());

  const auto isSourceActive = [this](const SongArrangementPartSnapshot &source) {
    const auto sourceId = source.id.trim();
    if (!arrangement.clips.empty()) {
      return std::any_of(arrangement.clips.begin(), arrangement.clips.end(),
                         [&source, &sourceId](const auto &clip) {
        if (sourceId.isNotEmpty() &&
            clip.sourceSectionId.trim() == sourceId) {
          return true;
        }
        return nearlyEqual(clip.sourceStartSec, source.startSec) &&
               nearlyEqual(clip.sourceEndSec, source.endSec) &&
               clip.label.trim().equalsIgnoreCase(source.name.trim());
      });
    }

    return std::any_of(arrangement.parts.begin(), arrangement.parts.end(),
                       [&source, &sourceId](const auto &part) {
      if (sourceId.isNotEmpty() && part.id == sourceId)
        return true;
      return nearlyEqual(part.startSec, source.startSec) &&
             nearlyEqual(part.endSec, source.endSec) &&
             part.name.trim().equalsIgnoreCase(source.name.trim());
    });
  };

  auto addTemplate = [&](const SectionTemplate &section) {
    const auto key =
        (section.sourceSectionId.trim() + "|" + section.name.trim() + "|" +
         juce::String(section.startSec, 4) + "|" +
         juce::String(section.endSec, 4))
            .toStdString();
    if (seenKeys.insert(key).second)
      templates.push_back(section);
  };

  for (const auto &part : sourceParts) {
    addTemplate({part.name, part.color, part.id, part.startSec, part.endSec,
                 !isSourceActive(part)});
  }

  if (templates.empty())
    return;

  auto menuLookAndFeel = std::make_unique<InsertSectionMenuLookAndFeel>();
  juce::PopupMenu menu;
  for (size_t i = 0; i < templates.size(); ++i) {
    const auto &section = templates[i];
    auto label = arrangementPartDisplayLabelForName(section.name);
    if (section.deleted)
      label = L10n::tr(u8"Restaurar ", "Restore ") + label +
              L10n::tr(u8"  (borrada)", "  (deleted)");
    auto colourText = section.color.trim();
    if (colourText.isEmpty())
      colourText = fallbackArrangementPartColorForName(section.name);
    const auto colour = isArrangementPartColorValid(colourText)
                            ? juce::Colour::fromString(colourText)
                            : UITheme::accent;
    menuLookAndFeel->setItemColour(label, section.deleted
                                              ? colour.withAlpha(0.55f)
                                              : colour);
    menu.addItem(static_cast<int>(i) + 1, label);
  }

  insertSectionMenuLookAndFeel = std::move(menuLookAndFeel);
  menu.setLookAndFeel(insertSectionMenuLookAndFeel.get());

  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  menu.showMenuAsync(
      juce::PopupMenu::Options()
          .withTargetComponent(this)
          .withMinimumWidth(240)
          .withTargetScreenArea(getScreenBounds().getIntersection(
              localAreaToGlobal(anchorBounds.toNearestInt()))),
      [safe, templates](const int result) {
        if (safe == nullptr)
          return;

        safe->insertSectionMenuLookAndFeel.reset();

        if (result <= 0 ||
            !juce::isPositiveAndBelow(result - 1,
                                      static_cast<int>(templates.size()))) {
          return;
        }

        const auto &section = templates[static_cast<size_t>(result - 1)];
        SongArrangementPartSnapshot part;
        part.id = juce::Uuid().toString();
        part.name = section.name.trim().isEmpty() ? L10n::tr("Parte", "Part")
                                                  : section.name;
        part.color = section.color;
        if (part.color.isEmpty())
          part.color = fallbackArrangementPartColorForName(part.name);
        part.startSec = section.startSec;
        part.endSec = section.endSec;

        SongArrangementClipSnapshot clip;
        clip.id = part.id;
        clip.sourceSectionId =
            section.sourceSectionId.trim().isEmpty() ? part.id
                                                     : section.sourceSectionId;
        clip.label = part.name;
        clip.color = part.color;
        clip.sourceStartSec = part.startSec;
        clip.sourceEndSec = part.endSec;
        clip.playbackStartSec = part.startSec;
        clip.playbackEndSec = part.endSec;
        clip.primaryInstance = section.deleted;

        if (safe->arrangement.clips.size() != safe->arrangement.parts.size())
          safe->rebuildClipsFromParts();

        const int preferredInsertIndex =
            section.deleted
                ? safe->findRestoreIndexForSourcePart(section.sourceSectionId)
                : safe->findInsertIndexForCurrentSeek();
        const int insertIndex = juce::jlimit(
            0, static_cast<int>(safe->arrangement.parts.size()),
            preferredInsertIndex);
        safe->arrangement.parts.insert(safe->arrangement.parts.begin() + insertIndex,
                                       part);
        safe->arrangement.clips.insert(safe->arrangement.clips.begin() + insertIndex,
                                       clip);
        safe->setSingleSelectedPart(insertIndex);
        safe->customArrangement = true;
        safe->notifyArrangementChanged(true);
        safe->repaint();
      });
}

int WaveformBar::findRestoreIndexForSourcePart(
    const juce::String &sourcePartId) const {
  const auto sourceId = sourcePartId.trim();
  if (sourceId.isEmpty())
    return juce::jlimit(0, static_cast<int>(arrangement.parts.size()), 0);

  auto sourceIndexForId = [this](const juce::String &id) {
    for (int i = 0; i < static_cast<int>(sourceArrangementParts.size()); ++i) {
      if (sourceArrangementParts[static_cast<size_t>(i)].id == id)
        return i;
    }
    return -1;
  };

  const int restoreSourceIndex = sourceIndexForId(sourceId);
  if (restoreSourceIndex < 0)
    return static_cast<int>(arrangement.parts.size());

  int insertIndex = 0;
  for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
    juce::String currentSourceId;
    if (juce::isPositiveAndBelow(i, static_cast<int>(arrangement.clips.size()))) {
      currentSourceId = arrangement.clips[static_cast<size_t>(i)]
                            .sourceSectionId.trim();
    }
    if (currentSourceId.isEmpty())
      currentSourceId = arrangement.parts[static_cast<size_t>(i)].id.trim();

    const int currentSourceIndex = sourceIndexForId(currentSourceId);
    if (currentSourceIndex >= 0 && currentSourceIndex < restoreSourceIndex)
      insertIndex = i + 1;
  }

  return juce::jlimit(0, static_cast<int>(arrangement.parts.size()), insertIndex);
}

double WaveformBar::getCurrentSeekSourceSeconds() const {
  const double songLength = getSongLengthSeconds();
  const double current =
      positionProvider != nullptr ? juce::jmax(0.0, positionProvider()) : 0.0;
  const double snapped = snapSeconds(current);
  return juce::jlimit(0.0, songLength, snapped);
}

int WaveformBar::findInsertIndexForCurrentSeek() const {
  if (arrangement.parts.empty())
    return 0;

  const double sourceSeconds = getCurrentSeekSourceSeconds();
  const double visualSeconds = usesVirtualClips()
                                   ? visualSecondsFromSourceSeconds(sourceSeconds)
                                   : sourceSeconds;

  int insertIndex = 0;
  for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
    const double partEnd = visualSecondsForPartEnd(i);
    if (visualSeconds >= partEnd - 0.0001)
      insertIndex = i + 1;
  }

  return juce::jlimit(0, static_cast<int>(arrangement.parts.size()), insertIndex);
}

bool WaveformBar::arrangementsEqual(const SongArrangementSnapshot &a,
                                    const SongArrangementSnapshot &b) const {
  if (a.parts.size() != b.parts.size() || a.clips.size() != b.clips.size())
    return false;

  for (size_t i = 0; i < a.parts.size(); ++i) {
    const auto &lhs = a.parts[i];
    const auto &rhs = b.parts[i];
    if (lhs.id != rhs.id || lhs.name != rhs.name || lhs.color != rhs.color ||
        !nearlyEqual(lhs.startSec, rhs.startSec) ||
        !nearlyEqual(lhs.endSec, rhs.endSec)) {
      return false;
    }
  }

  for (size_t i = 0; i < a.clips.size(); ++i) {
    const auto &lhs = a.clips[i];
    const auto &rhs = b.clips[i];
    if (lhs.id != rhs.id || lhs.sourceSectionId != rhs.sourceSectionId ||
        lhs.label != rhs.label || lhs.color != rhs.color ||
        !nearlyEqual(lhs.sourceStartSec, rhs.sourceStartSec) ||
        !nearlyEqual(lhs.sourceEndSec, rhs.sourceEndSec) ||
        !nearlyEqual(lhs.playbackStartSec, rhs.playbackStartSec) ||
        !nearlyEqual(lhs.playbackEndSec, rhs.playbackEndSec) ||
        lhs.muted != rhs.muted ||
        lhs.primaryInstance != rhs.primaryInstance) {
      return false;
    }
  }

  return true;
}

SongArrangementSnapshot
WaveformBar::buildArrangementSnapshotForPersistence() const {
  SongArrangementSnapshot snapshot = arrangement;
  if (snapshot.clips.empty())
    return snapshot;

  snapshot.parts = sourceArrangementParts;
  std::unordered_map<std::string, size_t> sourceIndexById;
  sourceIndexById.reserve(snapshot.parts.size());
  for (size_t i = 0; i < snapshot.parts.size(); ++i) {
    const auto key = snapshot.parts[i].id.trim().toStdString();
    if (!key.empty())
      sourceIndexById.emplace(key, i);
  }

  for (const auto &clip : snapshot.clips) {
    if (!clip.primaryInstance)
      continue;

    SongArrangementPartSnapshot updated;
    updated.id = clip.sourceSectionId.trim().isNotEmpty() ? clip.sourceSectionId
                                                          : clip.id;
    if (updated.id.isEmpty())
      updated.id = juce::Uuid().toString();
    updated.name = clip.label.trim().isNotEmpty() ? clip.label
                                                  : L10n::tr("Parte", "Part");
    updated.color = clip.color.trim();
    if (updated.color.isEmpty())
      updated.color = fallbackArrangementPartColorForName(updated.name);
    updated.startSec = clip.sourceStartSec;
    updated.endSec = clip.sourceEndSec;

    const auto key = updated.id.toStdString();
    auto found = sourceIndexById.find(key);
    if (found != sourceIndexById.end()) {
      snapshot.parts[found->second] = updated;
    } else {
      sourceIndexById.emplace(key, snapshot.parts.size());
      snapshot.parts.push_back(std::move(updated));
    }
  }

  return snapshot;
}

void WaveformBar::rememberArrangementForUndo() {
  if (arrangementsEqual(lastCommittedArrangement, arrangement))
    return;

  arrangementUndoStack.push_back(lastCommittedArrangement);
  arrangementRedoStack.clear();
  constexpr size_t kMaxUndoSnapshots = 64;
  if (arrangementUndoStack.size() > kMaxUndoSnapshots) {
    arrangementUndoStack.erase(arrangementUndoStack.begin(),
                               arrangementUndoStack.begin() +
                                   static_cast<std::ptrdiff_t>(
                                       arrangementUndoStack.size() -
                                       kMaxUndoSnapshots));
  }
}

bool WaveformBar::undoLastArrangementChange() {
  if (arrangementUndoStack.empty())
    return false;

  arrangementRedoStack.push_back(lastCommittedArrangement);
  arrangement = arrangementUndoStack.back();
  arrangementUndoStack.pop_back();
  if (usesVirtualClips())
    rebuildPartsFromClips();
  normalizeArrangement(!usesVirtualClips(), false);
  rebuildClipsFromParts();
  lastCommittedArrangement = arrangement;

  selectedPartIndex =
      arrangement.parts.empty()
          ? -1
          : juce::jlimit(0, static_cast<int>(arrangement.parts.size()) - 1,
                         selectedPartIndex);
  selectedPartIds.clear();
  hoveredActionPartIndex = -1;
  hoveredAddPartIndex = -1;
  hoveredMutePartIndex = -1;
  hoveredLoopPartIndex = -1;
  hoveredJumpPartIndex = -1;
  customArrangement = !arrangement.parts.empty() || !arrangement.clips.empty();

  if (onArrangementChanged)
    onArrangementChanged(arrangement, customArrangement);

  repaint();
  return true;
}

bool WaveformBar::redoLastArrangementChange() {
  if (arrangementRedoStack.empty())
    return false;

  arrangementUndoStack.push_back(lastCommittedArrangement);
  arrangement = arrangementRedoStack.back();
  arrangementRedoStack.pop_back();
  if (usesVirtualClips())
    rebuildPartsFromClips();
  normalizeArrangement(!usesVirtualClips(), false);
  rebuildClipsFromParts();
  lastCommittedArrangement = arrangement;

  selectedPartIndex =
      arrangement.parts.empty()
          ? -1
          : juce::jlimit(0, static_cast<int>(arrangement.parts.size()) - 1,
                         selectedPartIndex);
  selectedPartIds.clear();
  hoveredActionPartIndex = -1;
  hoveredAddPartIndex = -1;
  hoveredMutePartIndex = -1;
  hoveredLoopPartIndex = -1;
  hoveredJumpPartIndex = -1;
  customArrangement = !arrangement.parts.empty() || !arrangement.clips.empty();

  if (onArrangementChanged)
    onArrangementChanged(arrangement, customArrangement);

  repaint();
  return true;
}

bool WaveformBar::copySelectedArrangementParts() {
  arrangementClipboard = {};
  if (arrangement.parts.empty())
    return false;

  pruneSelectedPartIds();
  std::vector<juce::String> idsToCopy = selectedPartIds;
  if (idsToCopy.empty() &&
      juce::isPositiveAndBelow(selectedPartIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    idsToCopy.push_back(
        arrangement.parts[static_cast<size_t>(selectedPartIndex)].id);
  }

  if (idsToCopy.empty())
    return false;

  for (const auto &part : arrangement.parts) {
    if (std::find(idsToCopy.begin(), idsToCopy.end(), part.id) ==
        idsToCopy.end()) {
      continue;
    }

    arrangementClipboard.parts.push_back(part);

    auto clipIt = std::find_if(
        arrangement.clips.begin(), arrangement.clips.end(),
        [&part](const SongArrangementClipSnapshot &clip) {
          return clip.id == part.id;
        });
    if (clipIt != arrangement.clips.end()) {
      arrangementClipboard.clips.push_back(*clipIt);
    } else if (!arrangement.clips.empty()) {
      SongArrangementClipSnapshot clip;
      clip.id = part.id;
      clip.sourceSectionId = part.id;
      clip.label = part.name;
      clip.color = part.color;
      clip.sourceStartSec = part.startSec;
      clip.sourceEndSec = part.endSec;
      clip.playbackStartSec = part.startSec;
      clip.playbackEndSec = part.endSec;
      clip.primaryInstance = false;
      arrangementClipboard.clips.push_back(std::move(clip));
    }
  }

  return !arrangementClipboard.parts.empty();
}

bool WaveformBar::pasteArrangementClipboard() {
  if (arrangementClipboard.parts.empty())
    return false;

  if (arrangement.clips.empty() && !arrangementClipboard.clips.empty())
    rebuildClipsFromParts();

  const bool pasteClips = !arrangement.clips.empty() ||
                          !arrangementClipboard.clips.empty();
  if (pasteClips && arrangement.clips.size() != arrangement.parts.size())
    rebuildClipsFromParts();

  const int insertIndex = findInsertIndexForCurrentSeek();

  std::vector<juce::String> pastedIds;
  pastedIds.reserve(arrangementClipboard.parts.size());

  for (size_t i = 0; i < arrangementClipboard.parts.size(); ++i) {
    auto part = arrangementClipboard.parts[i];
    const auto originalId = part.id;
    part.id = juce::Uuid().toString();
    if (part.name.trim().isEmpty())
      part.name = L10n::tr("Parte", "Part");
    if (part.color.trim().isEmpty())
      part.color = fallbackArrangementPartColorForName(part.name);

    const int targetIndex = insertIndex + static_cast<int>(i);
    arrangement.parts.insert(arrangement.parts.begin() + targetIndex, part);
    pastedIds.push_back(part.id);

    if (pasteClips) {
      auto clipIt = std::find_if(
          arrangementClipboard.clips.begin(), arrangementClipboard.clips.end(),
          [&originalId](const SongArrangementClipSnapshot &clip) {
            return clip.id == originalId;
          });

      SongArrangementClipSnapshot clip;
      if (clipIt != arrangementClipboard.clips.end())
        clip = *clipIt;
      clip.id = part.id;
      if (clip.sourceSectionId.trim().isEmpty())
        clip.sourceSectionId = originalId.isNotEmpty() ? originalId : part.id;
      clip.label = part.name;
      clip.color = part.color;
      if (clip.sourceEndSec <= clip.sourceStartSec) {
        clip.sourceStartSec = part.startSec;
        clip.sourceEndSec = part.endSec;
      }
      clip.playbackStartSec = part.startSec;
      clip.playbackEndSec = part.endSec;
      clip.primaryInstance = false;
      arrangement.clips.insert(arrangement.clips.begin() + targetIndex, clip);
    }
  }

  selectedPartIds = pastedIds;
  selectedPartIndex = insertIndex;
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
  return true;
}

int WaveformBar::findPartIndexById(const juce::String &id) const {
  if (id.isEmpty())
    return -1;
  for (size_t i = 0; i < arrangement.parts.size(); ++i) {
    if (arrangement.parts[i].id == id)
      return static_cast<int>(i);
  }
  return -1;
}

bool WaveformBar::isPartSelected(const int partIndex) const {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return false;

  const auto &id = arrangement.parts[static_cast<size_t>(partIndex)].id;
  if (id.isEmpty())
    return partIndex == selectedPartIndex;

  return std::find(selectedPartIds.begin(), selectedPartIds.end(), id) !=
         selectedPartIds.end();
}

void WaveformBar::pruneSelectedPartIds() {
  selectedPartIds.erase(
      std::remove_if(selectedPartIds.begin(), selectedPartIds.end(),
                     [this](const juce::String &id) {
                       return findPartIndexById(id) < 0;
                     }),
      selectedPartIds.end());
}

void WaveformBar::setSingleSelectedPart(const int partIndex) {
  selectedPartIndex = partIndex;
  selectedPartIds.clear();
  if (juce::isPositiveAndBelow(partIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    auto id = arrangement.parts[static_cast<size_t>(partIndex)].id;
    if (id.isEmpty()) {
      id = juce::Uuid().toString();
      arrangement.parts[static_cast<size_t>(partIndex)].id = id;
    }
    selectedPartIds.push_back(id);
  }
}

void WaveformBar::togglePartSelection(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  auto id = arrangement.parts[static_cast<size_t>(partIndex)].id;
  if (id.isEmpty()) {
    id = juce::Uuid().toString();
    arrangement.parts[static_cast<size_t>(partIndex)].id = id;
  }

  const auto it = std::find(selectedPartIds.begin(), selectedPartIds.end(), id);
  if (it != selectedPartIds.end()) {
    selectedPartIds.erase(it);
    if (selectedPartIndex == partIndex)
      selectedPartIndex = selectedPartIds.empty()
                              ? -1
                              : findPartIndexById(selectedPartIds.back());
    return;
  }

  selectedPartIds.push_back(id);
  selectedPartIndex = partIndex;
}

void WaveformBar::ensurePartSelectedForDrag(const int partIndex) {
  if (isPartSelected(partIndex)) {
    selectedPartIndex = partIndex;
    return;
  }
  setSingleSelectedPart(partIndex);
}

void WaveformBar::captureDragPartOrigins() {
  dragPartOrigins.clear();
  pruneSelectedPartIds();

  if (selectedPartIds.empty() && juce::isPositiveAndBelow(
                                     selectedPartIndex,
                                     static_cast<int>(arrangement.parts.size()))) {
    selectedPartIds.push_back(
        arrangement.parts[static_cast<size_t>(selectedPartIndex)].id);
  }

  for (const auto &id : selectedPartIds) {
    const int idx = findPartIndexById(id);
    if (!juce::isPositiveAndBelow(idx,
                                  static_cast<int>(arrangement.parts.size())))
      continue;
    const auto &part = arrangement.parts[static_cast<size_t>(idx)];
    dragPartOrigins.push_back({part.id, part.startSec, part.endSec});
  }
}

juce::Rectangle<float> WaveformBar::getSelectionDragBounds() const {
  const auto x = juce::jmin(selectionDragStart.x, selectionDragCurrent.x);
  const auto y = juce::jmin(selectionDragStart.y, selectionDragCurrent.y);
  const auto right = juce::jmax(selectionDragStart.x, selectionDragCurrent.x);
  const auto bottom = juce::jmax(selectionDragStart.y, selectionDragCurrent.y);
  return {x, y, right - x, bottom - y};
}

void WaveformBar::updateSelectionFromDragBounds() {
  const auto bounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  rebuildPartOverlayBounds(mainWaveBodyBounds);

  auto selectionBounds = getSelectionDragBounds();
  if (selectionBounds.getWidth() < 2.0f && selectionBounds.getHeight() < 2.0f)
    return;

  selectionBounds = selectionBounds.getIntersection(mainWaveBodyBounds);
  std::vector<juce::String> nextSelection =
      selectionDragAdditive ? selectionDragBaseIds : std::vector<juce::String>{};

  const auto addId = [&nextSelection](const juce::String &id) {
    if (id.isEmpty())
      return;
    if (std::find(nextSelection.begin(), nextSelection.end(), id) ==
        nextSelection.end())
      nextSelection.push_back(id);
  };

  for (const auto &overlay : partOverlayBounds) {
    if (!juce::isPositiveAndBelow(overlay.partIndex,
                                  static_cast<int>(arrangement.parts.size())))
      continue;
    if (!overlay.body.intersects(selectionBounds))
      continue;

    auto &part = arrangement.parts[static_cast<size_t>(overlay.partIndex)];
    if (part.id.isEmpty())
      part.id = juce::Uuid().toString();
    addId(part.id);
  }

  selectedPartIds = std::move(nextSelection);
  pruneSelectedPartIds();
  selectedPartIndex =
      selectedPartIds.empty() ? -1 : findPartIndexById(selectedPartIds.back());
}

bool WaveformBar::isEditingArrangement() const {
  return editButton.getToggleState();
}

juce::Rectangle<float>
WaveformBar::getEditToolbarBounds(const juce::Rectangle<float> &bounds) const {
  const auto area = bounds.reduced(8.0f, 6.0f);
  const float height = juce::jmin(kEditToolbarHeight, area.getHeight());
  return {area.getX(), area.getY(), area.getWidth(), height};
}

juce::Rectangle<float>
WaveformBar::getMainWaveBounds(const juce::Rectangle<float> &bounds) const {
  auto area = bounds.reduced(8.0f, 6.0f);
  if (isEditingArrangement())
    area = area.withTrimmedTop(kEditToolbarHeight + kEditToolbarGap);
  area = area.withTrimmedBottom(kMiniMapHeight + kMiniMapGap);
  return area;
}

juce::Rectangle<float>
WaveformBar::getSeekRulerBounds(
    const juce::Rectangle<float> &mainWaveBounds) const {
  if (mainWaveBounds.getWidth() <= 2.0f || mainWaveBounds.getHeight() <= 8.0f)
    return {};

  const float rulerHeight =
      juce::jmin(kSeekRulerHeight, mainWaveBounds.getHeight() - 6.0f);
  return {mainWaveBounds.getX(), mainWaveBounds.getY(), mainWaveBounds.getWidth(),
          rulerHeight};
}

juce::Rectangle<float> WaveformBar::getMainWaveBodyBounds(
    const juce::Rectangle<float> &mainWaveBounds) const {
  const auto ruler = getSeekRulerBounds(mainWaveBounds);
  if (ruler.getHeight() <= 0.0f)
    return mainWaveBounds;

  auto body = mainWaveBounds.withTrimmedTop(ruler.getHeight() + kSeekRulerGap);
  if (body.getHeight() < 2.0f)
    body.setHeight(0.0f);
  return body;
}

juce::Rectangle<float>
WaveformBar::getMiniMapBounds(const juce::Rectangle<float> &bounds) const {
  auto area = bounds.reduced(8.0f, 6.0f);
  if (area.getHeight() <= kMiniMapHeight)
    return {};

  return {area.getX(), area.getBottom() - kMiniMapHeight, area.getWidth(),
          kMiniMapHeight};
}

juce::Rectangle<float> WaveformBar::getArrangementLaneBounds(
    const juce::Rectangle<float> &mainWaveBounds) const {
  return mainWaveBounds.reduced(2.0f, 2.0f);
}

juce::Rectangle<float> WaveformBar::getViewportBoundsInMiniMap(
    const juce::Rectangle<float> &miniMapBounds) const {
  const auto window = getVisibleWindow();
  const float x = miniMapBounds.getX() + (window.first * miniMapBounds.getWidth());
  const float w = window.second * miniMapBounds.getWidth();
  return {x, miniMapBounds.getY(), w, miniMapBounds.getHeight()};
}

double WaveformBar::windowXToSourceSeconds(
    const juce::Rectangle<float> &windowBounds, const float x) const {
  const double timelineLength = getVisualTimelineLengthSeconds();
  if (timelineLength <= 0.0 || windowBounds.getWidth() <= 0.0f)
    return 0.0;

  const float clampedX =
      juce::jlimit(windowBounds.getX(), windowBounds.getRight(), x);
  const float ratio = (clampedX - windowBounds.getX()) / windowBounds.getWidth();
  const auto window = getVisibleWindow();
  const float sourceRatio =
      juce::jlimit(0.0f, 1.0f, window.first + (ratio * window.second));
  return sourceSecondsFromVisualSeconds(static_cast<double>(sourceRatio) *
                                        timelineLength);
}

float WaveformBar::sourceSecondsToWindowX(
    const juce::Rectangle<float> &windowBounds, const double sourceSeconds) const {
  const double timelineLength =
      usesVirtualClips() ? getVisualTimelineLengthSeconds() : getSongLengthSeconds();
  if (timelineLength <= 0.0 || windowBounds.getWidth() <= 0.0f)
    return windowBounds.getX();

  const auto window = getVisibleWindow();
  const double timelineSeconds =
      usesVirtualClips() ? visualSecondsFromSourceSeconds(sourceSeconds)
                         : sourceSeconds;
  const float normalizedSource = static_cast<float>(
      juce::jlimit(0.0, 1.0, timelineSeconds / timelineLength));
  const float normalizedWindow =
      (normalizedSource - window.first) / juce::jmax(0.0001f, window.second);
  return windowBounds.getX() +
         (juce::jlimit(0.0f, 1.0f, normalizedWindow) * windowBounds.getWidth());
}

WaveformBar::PartHitResult
WaveformBar::hitTestArrangementPart(const juce::Point<float> pos) const {
  for (int i = static_cast<int>(partOverlayBounds.size()) - 1; i >= 0; --i) {
    const auto &bounds = partOverlayBounds[static_cast<size_t>(i)];
    if (!bounds.body.contains(pos))
      continue;

    PartHitResult hit;
    hit.partIndex = bounds.partIndex;
    hit.deleteButton = bounds.showsDeleteButton && bounds.deleteButton.contains(pos);
    if (hit.deleteButton)
      return hit;
    hit.muteButton = bounds.showsMuteButton && bounds.muteButton.contains(pos);
    if (hit.muteButton)
      return hit;
    hit.addButton = bounds.showsAddButton && bounds.addButton.contains(pos);
    if (hit.addButton)
      return hit;
    hit.leftEdge = bounds.leftHandle.contains(pos);
    hit.rightEdge = bounds.rightHandle.contains(pos);
    if (hit.leftEdge || hit.rightEdge)
      return hit;
    hit.loopButton = bounds.showsLoopButton && bounds.loopButton.contains(pos);
    if (hit.loopButton)
      return hit;
    hit.jumpButton = bounds.showsJumpButton && bounds.jumpButton.contains(pos);
    if (hit.jumpButton)
      return hit;
    return hit;
  }
  return {};
}

WaveformBar::TempoRegionHitResult
WaveformBar::hitTestTempoRegion(const juce::Point<float> pos) const {
  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto lane = getArrangementLaneBounds(getMainWaveBodyBounds(mainWaveBounds));
  if (!lane.contains(pos))
    return {};
  for (int i = static_cast<int>(tempoMap.regions.size()) - 1; i >= 0; --i) {
    const auto &region = tempoMap.regions[static_cast<size_t>(i)];
    const float startX = sourceSecondsToWindowX(lane, region.startSec);
    const float endX = sourceSecondsToWindowX(lane, region.endSec);
    if (endX <= lane.getX() || startX >= lane.getRight())
      continue;
    auto body = lane.withX(juce::jmax(lane.getX(), startX))
                    .withRight(juce::jmin(lane.getRight(), endX))
                    .reduced(1.0f, 2.0f);
    if (!body.contains(pos))
      continue;
    TempoRegionHitResult hit;
    hit.regionIndex = i;
    constexpr float deleteSize = 13.0f;
    const bool showsDeleteButton =
        tempoMap.regions.size() > 1 &&
        body.getWidth() >= (deleteSize + 16.0f) &&
        body.getHeight() >= (deleteSize + 8.0f);
    if (showsDeleteButton) {
      const auto deleteButton = juce::Rectangle<float>(
          body.getRight() - deleteSize - 3.0f, body.getY() + 3.0f,
          deleteSize, deleteSize);
      hit.deleteButton = deleteButton.contains(pos);
      if (hit.deleteButton)
        return hit;
    }
    hit.leftEdge = std::abs(pos.x - body.getX()) <= kPartHandleHitWidth;
    hit.rightEdge = std::abs(pos.x - body.getRight()) <= kPartHandleHitWidth;
    return hit;
  }
  return {};
}

void WaveformBar::rebuildPartOverlayBounds(
    const juce::Rectangle<float> &mainWaveBounds) {
  partOverlayBounds.clear();

  if (arrangement.parts.empty())
    return;

  const auto lane = getArrangementLaneBounds(mainWaveBounds);
  if (lane.getWidth() <= 2.0f || lane.getHeight() <= 2.0f)
    return;

  const double timelineLength = getVisualTimelineLengthSeconds();
  if (timelineLength <= 0.0)
    return;

  const auto window = getVisibleWindow();
  for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
    const double partStartVisual = visualSecondsForPartStart(i);
    const double partEndVisual = visualSecondsForPartEnd(i);
    const float startRatio = static_cast<float>(
        juce::jlimit(0.0, 1.0, partStartVisual / timelineLength));
    const float endRatio = static_cast<float>(
        juce::jlimit(0.0, 1.0, partEndVisual / timelineLength));

    const float x1 = lane.getX() +
                     ((startRatio - window.first) /
                      juce::jmax(0.0001f, window.second)) *
                         lane.getWidth();
    const float x2 = lane.getX() +
                     ((endRatio - window.first) /
                      juce::jmax(0.0001f, window.second)) *
                         lane.getWidth();

    if (x2 < lane.getX() || x1 > lane.getRight())
      continue;

    const float left = juce::jlimit(lane.getX(), lane.getRight(), x1);
    const float right = juce::jlimit(lane.getX(), lane.getRight(), x2);
    float width = right - left;
    if (width < 2.0f)
      width = 2.0f;

    juce::Rectangle<float> body(left, lane.getY(), width, lane.getHeight());
    if (body.getRight() > lane.getRight())
      body.setRight(lane.getRight());

    const double playbackStartVisual = visualSecondsForPartPlaybackStart(i);
    const double playbackEndVisual = visualSecondsForPartPlaybackEnd(i);
    const float playbackStartRatio = static_cast<float>(
        juce::jlimit(0.0, 1.0, playbackStartVisual / timelineLength));
    const float playbackEndRatio = static_cast<float>(
        juce::jlimit(0.0, 1.0, playbackEndVisual / timelineLength));
    const float playbackX1 = lane.getX() +
                             ((playbackStartRatio - window.first) /
                              juce::jmax(0.0001f, window.second)) *
                                 lane.getWidth();
    const float playbackX2 = lane.getX() +
                             ((playbackEndRatio - window.first) /
                              juce::jmax(0.0001f, window.second)) *
                                 lane.getWidth();

    const float handleWidth = juce::jmin(kPartHandleHitWidth, body.getWidth() * 0.4f);
    PartOverlayBounds overlay;
    overlay.partIndex = i;
    overlay.body = body;
    overlay.leftHandle =
        juce::Rectangle<float>(
            juce::jlimit(body.getX(), body.getRight() - handleWidth,
                         playbackX1 - (handleWidth * 0.5f)),
            body.getY(), handleWidth, body.getHeight());
    overlay.rightHandle =
        juce::Rectangle<float>(
            juce::jlimit(body.getX(), body.getRight() - handleWidth,
                         playbackX2 - (handleWidth * 0.5f)),
            body.getY(), handleWidth, body.getHeight());
    constexpr float deleteSize = 18.0f;
    constexpr float overlayButtonInsetX = 6.0f;
    overlay.showsDeleteButton =
        body.getWidth() >= (deleteSize + 18.0f) &&
        body.getHeight() >= (deleteSize + 10.0f);
    if (overlay.showsDeleteButton) {
      overlay.deleteButton = juce::Rectangle<float>(
          body.getX() + overlayButtonInsetX,
          body.getCentreY() - (deleteSize * 0.5f), deleteSize,
          deleteSize);
    } else {
      overlay.deleteButton = {};
    }

    constexpr float muteSize = 18.0f;
    const bool editing = isEditingArrangement();
    overlay.showsMuteButton =
        editing && body.getWidth() >= (muteSize + 64.0f) &&
        body.getHeight() >= (muteSize + 10.0f);
    if (overlay.showsMuteButton) {
      overlay.muteButton = juce::Rectangle<float>(
          body.getCentreX() - (muteSize * 0.5f),
          body.getCentreY() - (muteSize * 0.5f), muteSize, muteSize);
    } else {
      overlay.muteButton = {};
    }

    constexpr float addSize = 18.0f;
    overlay.showsAddButton =
        isEditingArrangement() && body.getWidth() >= (addSize + 20.0f) &&
        body.getHeight() >= (addSize + 10.0f);
    if (overlay.showsAddButton) {
      overlay.addButton = juce::Rectangle<float>(
          body.getRight() - addSize - overlayButtonInsetX,
          body.getCentreY() - (addSize * 0.5f),
          addSize, addSize);
      overlay.addIcon = overlay.addButton.reduced(4.2f, 4.2f);
    } else {
      overlay.addButton = {};
      overlay.addIcon = {};
    }

    constexpr float actionHitSize = 28.0f;
    constexpr float actionIconSize = 18.0f;
    constexpr float actionGap = 8.0f;
    const float actionMinTop = body.getY() + 29.0f;
    const float actionBottomPadding = 6.0f;
    const float fullActionStackHeight = (actionHitSize * 3.0f) + (actionGap * 2.0f);
    const float compactActionStackHeight = (actionHitSize * 2.0f) + actionGap;
    const float availableActionHeight =
        body.getBottom() - actionMinTop - actionBottomPadding;
    const bool showContextActions = !editing;
    const bool showFullContextActions =
        showContextActions && body.getWidth() >= 44.0f &&
        availableActionHeight >= fullActionStackHeight;
    const bool showCompactContextActions =
        showContextActions && !showFullContextActions && body.getWidth() >= 44.0f &&
        availableActionHeight >= compactActionStackHeight;
    overlay.showsJumpButton =
        showFullContextActions || showCompactContextActions;
    overlay.showsLoopButton =
        showContextActions && body.getWidth() >= 44.0f &&
        availableActionHeight >= actionHitSize;
    if (showFullContextActions)
      overlay.showsMuteButton = true;
    if (overlay.showsLoopButton) {
      const float desiredTop =
          showFullContextActions
              ? body.getCentreY() - (fullActionStackHeight * 0.5f)
              : overlay.showsJumpButton
                    ? body.getCentreY() - (compactActionStackHeight * 0.5f)
                    : body.getCentreY() - (actionHitSize * 0.5f);
      const float actionStackHeight =
          showFullContextActions
              ? fullActionStackHeight
              : overlay.showsJumpButton ? compactActionStackHeight
                                        : actionHitSize;
      const float maxTop =
          body.getBottom() - actionStackHeight - actionBottomPadding;
      const float loopTop = juce::jlimit(actionMinTop, maxTop, desiredTop);
      overlay.loopButton = juce::Rectangle<float>(
          body.getCentreX() - (actionHitSize * 0.5f), loopTop, actionHitSize,
          actionHitSize);
      overlay.loopIcon =
          overlay.loopButton.withSizeKeepingCentre(actionIconSize, actionIconSize);
      if (showFullContextActions) {
        overlay.muteButton =
            overlay.loopButton.translated(0.0f, actionHitSize + actionGap);
        overlay.jumpButton =
            overlay.muteButton.translated(0.0f, actionHitSize + actionGap);
        overlay.jumpIcon =
            overlay.jumpButton.withSizeKeepingCentre(actionIconSize, actionIconSize);
      } else if (overlay.showsJumpButton) {
        overlay.jumpButton =
            overlay.loopButton.translated(0.0f, actionHitSize + actionGap);
        overlay.jumpIcon =
            overlay.jumpButton.withSizeKeepingCentre(actionIconSize, actionIconSize);
      } else {
        overlay.jumpButton = {};
        overlay.jumpIcon = {};
      }
    } else {
      overlay.loopButton = {};
      overlay.loopIcon = {};
      if (!editing) {
        overlay.showsMuteButton = false;
        overlay.muteButton = {};
      }
      overlay.jumpButton = {};
      overlay.jumpIcon = {};
      overlay.showsJumpButton = false;
    }
    partOverlayBounds.push_back(overlay);
  }
}

void WaveformBar::drawWaveform(juce::Graphics &g,
                               const juce::Rectangle<float> &bounds,
                               const float windowStart, const float windowSpan,
                               const bool showGrid,
                               const bool showPlayhead) const {
  if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f)
    return;
  if (waveformBins.empty())
    return;

  const int bins = static_cast<int>(waveformBins.size());
  if (bins < 2)
    return;

  const auto waveformBg = juce::Colour::fromRGB(0x1E, 0x1E, 0x1E);
  g.setColour(waveformBg);
  g.fillRoundedRectangle(bounds, 4.0f);

  const double sourceSongLength = getSongLengthSeconds();
  const double timelineLength = getVisualTimelineLengthSeconds();
  if (sourceSongLength <= 0.0 || timelineLength <= 0.0)
    return;

  if (showGrid) {
    if (!usesVirtualClips() && songBpm > 0.0 && sourceSongLength > 0.0 &&
        songBeatsPerBar > 0) {
      const double barDurationSec = getBarDurationSeconds();
      const double beatDurationSec = 60.0 / songBpm;
      const double divisionSec = getSnapStepSeconds();
      if (barDurationSec > 0.0 && beatDurationSec > 0.0 && divisionSec > 0.0) {
        const double visibleStartSec = sourceSongLength * windowStart;
        const double visibleEndSec = sourceSongLength * (windowStart + windowSpan);
        if (visibleEndSec > visibleStartSec) {
          const double visibleDurationSec = visibleEndSec - visibleStartSec;
          const double pixelsPerDivision =
              (bounds.getWidth() * divisionSec) /
              juce::jmax(0.0001, visibleDurationSec);
          const int64_t divisionSkip = juce::jmax<int64_t>(
              1, static_cast<int64_t>(
                     std::ceil(6.0 / juce::jmax(0.35, pixelsPerDivision))));
          const int64_t divisionsPerBar = juce::jmax<int64_t>(
              1, static_cast<int64_t>(std::llround(barDurationSec / divisionSec)));
          const int64_t divisionsPerBeat = juce::jmax<int64_t>(
              1, static_cast<int64_t>(std::llround(beatDurationSec / divisionSec)));
          int64_t firstDivision = static_cast<int64_t>(
              std::ceil((visibleStartSec - 1.0e-9) / divisionSec));
          if (firstDivision < 0)
            firstDivision = 0;
          const int64_t lastDivision = static_cast<int64_t>(
              std::floor((visibleEndSec + 1.0e-9) / divisionSec));

          for (int64_t division = firstDivision; division <= lastDivision;
               ++division) {
            const bool isBar = (division % divisionsPerBar) == 0;
            if (!isBar && (division % divisionSkip) != 0)
              continue;

            const bool isBeat = (division % divisionsPerBeat) == 0;
            const int64_t barIndex = division / divisionsPerBar;
            const bool isEveryFourBars = isBar && ((barIndex % 4) == 0);
            const float x =
                sourceSecondsToWindowX(bounds, static_cast<double>(division) * divisionSec);
            if (x < bounds.getX() || x > bounds.getRight())
              continue;

            float alpha = 0.10f;
            float thickness = 0.65f;
            if (isBar) {
              alpha = isEveryFourBars ? 0.34f : 0.18f;
              thickness = isEveryFourBars ? 1.25f : 0.85f;
            } else if (isBeat) {
              alpha = 0.16f;
              thickness = 0.85f;
            }

            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.drawLine(x, bounds.getY(), x, bounds.getBottom(), thickness);

            if (isBar) {
              const float anchorRadius = isEveryFourBars ? 2.4f : 1.7f;
              const float anchorY = bounds.getY() + 5.0f;
              g.setColour(juce::Colours::white.withAlpha(isEveryFourBars ? 0.75f
                                                                          : 0.45f));
              g.fillEllipse(x - anchorRadius, anchorY - anchorRadius,
                            anchorRadius * 2.0f, anchorRadius * 2.0f);
            }
          }
        }
      }
    }
  }

  const float midY = bounds.getCentreY();
  const float ampScale = bounds.getHeight() * 0.495f;
  const int widthPixels = juce::jmax(2, static_cast<int>(bounds.getWidth()));

  float progressRatio = 0.0f;
  float pendingTravelRatio = 0.0f;
  bool showPendingTravelSeek = false;
  if (songLoadingActive || songLoadingHideAfterMs > 0.0) {
    progressRatio =
        static_cast<float>(juce::jlimit(0.0, 1.0, songLoadingProgress01));
  } else if (positionProvider && lengthProvider) {
    const double songLength = juce::jmax(0.0, lengthProvider());
    if (songLength > 0.0) {
      const double songPosition = juce::jmax(0.0, positionProvider());
      const double visualPosition = visualSecondsFromSourceSeconds(songPosition);
      progressRatio = static_cast<float>(juce::jlimit(
          0.0, 1.0, visualPosition / juce::jmax(0.0001, timelineLength)));
      pendingTravelRatio = progressRatio;
    }
  }

  if (followPlaybackEnabled && !isEditingArrangement() && playingProvider != nullptr &&
      playingProvider()) {
    float pendingRatio = 0.0f;
    if (getPendingJumpProgressRatio(pendingRatio)) {
      pendingTravelRatio = pendingRatio;
      showPendingTravelSeek = !songLoadingActive &&
                              songLoadingHideAfterMs <= 0.0 &&
                              positionProvider != nullptr &&
                              lengthProvider != nullptr;
    }
  }

  const float playheadRatio = juce::jlimit(
      0.0f, 1.0f,
      (progressRatio - windowStart) / juce::jmax(0.0001f, windowSpan));
  const float playheadX = snapToPixelCenter(
      bounds.getX() + (playheadRatio * bounds.getWidth()));

  float playableStartRatio = 0.0f;
  float playableEndRatio = 1.0f;
  bool showPlayableWindow = false;
  if (lengthProvider && songWindowStartProvider && songWindowEndProvider) {
    const double songLength = juce::jmax(0.0, lengthProvider());
    if (songLength > 0.0) {
      double startSec = songWindowStartProvider();
      double endSec = songWindowEndProvider();
      endSec = juce::jlimit(0.0, songLength, endSec);
      startSec = juce::jlimit(0.0, endSec, startSec);
      playableStartRatio =
          static_cast<float>(juce::jlimit(0.0, 1.0, startSec / songLength));
      playableEndRatio =
          static_cast<float>(juce::jlimit(0.0, 1.0, endSec / songLength));
      showPlayableWindow =
          playableStartRatio > 0.0005f || playableEndRatio < 0.9995f;
    }
  }

  const float playableStartNorm = juce::jlimit(
      0.0f, 1.0f,
      (playableStartRatio - windowStart) / juce::jmax(0.0001f, windowSpan));
  const float playableEndNorm = juce::jlimit(
      0.0f, 1.0f,
      (playableEndRatio - windowStart) / juce::jmax(0.0001f, windowSpan));
  const float playableStartX =
      snapToPixelCenter(bounds.getX() + playableStartNorm * bounds.getWidth());
  const float playableEndX = snapToPixelCenter(
      bounds.getX() + playableEndNorm * bounds.getWidth());

  g.setColour(juce::Colours::white.withAlpha(0.22f));
  const float centerDashes[] = {2.0f, 4.0f};
  g.drawDashedLine({bounds.getX(), midY, bounds.getRight(), midY},
                   centerDashes, 2, 1.0f);

  const int step = 2;
  for (int x = 0; x < widthPixels; x += step) {
    const float t = static_cast<float>(x) / static_cast<float>(widthPixels - 1);
    const double visualSeconds =
        static_cast<double>(windowStart + (t * windowSpan)) * timelineLength;
    const double sourceSeconds = sourceSecondsFromVisualSeconds(visualSeconds);
    const float index = static_cast<float>(
        juce::jlimit(0.0, 1.0, sourceSeconds / sourceSongLength)) *
                        static_cast<float>(bins - 1);
    const int i0 = static_cast<int>(index);
    const int i1 = juce::jmin(i0 + 1, bins - 1);
    const float frac = index - static_cast<float>(i0);
    const float amp = juce::jmap(frac, waveformBins[static_cast<size_t>(i0)],
                                 waveformBins[static_cast<size_t>(i1)]);
    const float lineHalf = juce::jmax(1.2f, amp * ampScale);
    const float xPos = bounds.getX() + static_cast<float>(x);

    juce::Colour lineColour = juce::Colours::white.withAlpha(0.70f);
    if (showPlayhead && xPos <= playheadX)
      lineColour = juce::Colours::white.withAlpha(0.92f);
    g.setColour(lineColour);
    g.drawLine(xPos, midY - lineHalf, xPos, midY + lineHalf, 1.0f);
  }

  if (showPlayableWindow) {
    g.setColour(juce::Colours::black.withAlpha(0.34f));
    if (playableStartX > bounds.getX())
      g.fillRect(bounds.withRight(playableStartX));
    if (playableEndX < bounds.getRight())
      g.fillRect(bounds.withLeft(playableEndX));

    g.setColour(UITheme::accent.withAlpha(0.82f));
    g.drawLine(playableStartX, bounds.getY(), playableStartX, bounds.getBottom(), 1.25f);
    g.drawLine(playableEndX, bounds.getY(), playableEndX, bounds.getBottom(), 1.25f);

    juce::ignoreUnused(showGrid);
  }

  g.setColour(waveformBg.brighter(0.16f).withAlpha(0.9f));
  g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

  if (showPlayhead) {
    drawPlayheadNeedle(g, bounds, playheadX, false, false, 1.0f);

    if (showPendingTravelSeek) {
      const float pendingTravelNorm = juce::jlimit(
          0.0f, 1.0f, (pendingTravelRatio - windowStart) /
                           juce::jmax(0.0001f, windowSpan));
      const float pendingTravelX = snapToPixelCenter(
          bounds.getX() + (pendingTravelNorm * bounds.getWidth()));
      g.setColour(juce::Colours::white.withAlpha(0.62f));
      g.drawLine(pendingTravelX, bounds.getY(), pendingTravelX,
                 bounds.getBottom(), 1.0f);
    }
  }
}

void WaveformBar::drawSeekRuler(juce::Graphics &g,
                                const juce::Rectangle<float> &bounds,
                                const float windowStart,
                                const float windowSpan) const {
  if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f)
    return;

  g.setColour(UITheme::cardBg.withAlpha(0.58f));
  g.fillRoundedRectangle(bounds, 3.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.78f));
  g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

  const double timelineLength = getVisualTimelineLengthSeconds();
  if (timelineLength > 0.0) {
    const float topY = bounds.getY() + 2.0f;
    const float minorBottom = bounds.getBottom() - 6.0f;
    const float majorBottom = bounds.getBottom() - 2.5f;

    if (songBpm > 0.0 && songBeatsPerBar > 0) {
      const double barDurationSec = getBarDurationSeconds();
      const double beatDurationSec = 60.0 / songBpm;
      const double divisionSec = getSnapStepSeconds();
      if (barDurationSec > 0.0 && beatDurationSec > 0.0 && divisionSec > 0.0) {
        const double visibleStartSec = timelineLength * windowStart;
        const double visibleEndSec = timelineLength * (windowStart + windowSpan);
        const double visibleDurationSec = visibleEndSec - visibleStartSec;
        const double pixelsPerDivision =
            (bounds.getWidth() * divisionSec) / juce::jmax(0.0001, visibleDurationSec);
        const int64_t divisionSkip = juce::jmax<int64_t>(
            1, static_cast<int64_t>(
                   std::ceil(8.0 / juce::jmax(0.35, pixelsPerDivision))));
        const int64_t divisionsPerBar = juce::jmax<int64_t>(
            1, static_cast<int64_t>(std::llround(barDurationSec / divisionSec)));
        const int64_t divisionsPerBeat = juce::jmax<int64_t>(
            1, static_cast<int64_t>(std::llround(beatDurationSec / divisionSec)));
        int64_t firstDivision = static_cast<int64_t>(
            std::ceil((visibleStartSec - 1.0e-9) / divisionSec));
        if (firstDivision < 0)
          firstDivision = 0;
        const int64_t lastDivision = static_cast<int64_t>(
            std::floor((visibleEndSec + 1.0e-9) / divisionSec));

        for (int64_t division = firstDivision; division <= lastDivision;
             ++division) {
          const bool isBar = (division % divisionsPerBar) == 0;
          if (!isBar && (division % divisionSkip) != 0)
            continue;

          const bool isBeat = (division % divisionsPerBeat) == 0;
          const int64_t barIndex = division / divisionsPerBar;
          const bool isEveryFourBars = isBar && ((barIndex % 4) == 0);
          const double visualSec = static_cast<double>(division) * divisionSec;
          const float normalizedTimeline =
              static_cast<float>(juce::jlimit(0.0, 1.0,
                                              visualSec / timelineLength));
          const float normalizedWindow =
              (normalizedTimeline - windowStart) /
              juce::jmax(0.0001f, windowSpan);
          const float x = bounds.getX() + (normalizedWindow * bounds.getWidth());
          if (x < bounds.getX() || x > bounds.getRight())
            continue;

          float alpha = 0.34f;
          float thickness = 0.7f;
          float bottom = minorBottom - 2.5f;
          if (isBar) {
            alpha = isEveryFourBars ? 0.82f : 0.56f;
            thickness = isEveryFourBars ? 1.2f : 0.95f;
            bottom = isEveryFourBars ? majorBottom : minorBottom;
          } else if (isBeat) {
            alpha = 0.44f;
            thickness = 0.8f;
            bottom = minorBottom - 1.0f;
          }

          g.setColour(juce::Colours::white.withAlpha(alpha));
          g.drawLine(x, topY, x, bottom, thickness);

          if (isEveryFourBars) {
            g.setColour(juce::Colours::white.withAlpha(0.78f));
            g.setFont(juce::FontOptions(9.5f));
            g.drawText(juce::String(barIndex + 1),
                       juce::Rectangle<float>(x + 3.0f, bounds.getY() + 1.0f,
                                              34.0f, 10.0f),
                       juce::Justification::left, false);
          }
        }
      }
    } else {
      const int tickCount = 12;
      for (int i = 0; i <= tickCount; ++i) {
        const float ratio = static_cast<float>(i) / static_cast<float>(tickCount);
        const float x =
            snapToPixelCenter(bounds.getX() + ratio * bounds.getWidth());
        const bool major = (i % 2) == 0;
        g.setColour(juce::Colours::white.withAlpha(major ? 0.56f : 0.35f));
        g.drawLine(x, topY, x, major ? majorBottom : minorBottom,
                   major ? 1.05f : 0.8f);
      }
    }
  }

  if (positionProvider && lengthProvider) {
    const double length = juce::jmax(0.0, lengthProvider());
    const double currentTimelineLength = getVisualTimelineLengthSeconds();
    if (length > 0.0 && currentTimelineLength > 0.0) {
      double position = juce::jmax(0.0, positionProvider());
      bool showPendingTravelSeek = false;
      float pendingTravelRatio = 0.0f;
      float progress = static_cast<float>(juce::jlimit(
          0.0, 1.0,
          visualSecondsFromSourceSeconds(position) / currentTimelineLength));
      if (followPlaybackEnabled && !isEditingArrangement() &&
          playingProvider != nullptr && playingProvider()) {
        float pendingRatio = 0.0f;
        if (getPendingJumpProgressRatio(pendingRatio)) {
          pendingTravelRatio = pendingRatio;
          showPendingTravelSeek = true;
        }
      }
      const float playheadNorm =
          juce::jlimit(0.0f, 1.0f, (progress - windowStart) /
                                         juce::jmax(0.0001f, windowSpan));
      const float x = snapToPixelCenter(
          bounds.getX() + playheadNorm * bounds.getWidth());
      drawPlayheadNeedle(g, bounds.reduced(0.0f, 0.4f), x, true, true, 0.96f);

      if (showPendingTravelSeek) {
        const float pendingTravelNorm = juce::jlimit(
            0.0f, 1.0f, (pendingTravelRatio - windowStart) /
                             juce::jmax(0.0001f, windowSpan));
        const float pendingTravelX = snapToPixelCenter(
            bounds.getX() + (pendingTravelNorm * bounds.getWidth()));
        g.setColour(juce::Colours::white.withAlpha(0.62f));
        g.drawLine(pendingTravelX, bounds.getY() + 1.0f, pendingTravelX,
                   bounds.getBottom() - 1.0f, 1.0f);
      }
    }
  }
}

void WaveformBar::drawArrangementOverlay(
    juce::Graphics &g, const juce::Rectangle<float> &mainWaveBounds) {
  rebuildPartOverlayBounds(mainWaveBounds);
  const bool editing = isEditingArrangement();
  const int pendingJumpPartIndex =
      pendingArrangementPartProvider ? pendingArrangementPartProvider() : -1;
  const int activeLoopPartIndex =
      activeArrangementLoopPartProvider ? activeArrangementLoopPartProvider() : -1;
  const bool showPendingJumpSectionHighlight = !followPlaybackEnabled;

  float pendingPulse = 0.0f;
  if (showPendingJumpSectionHighlight && pendingJumpPartIndex >= 0) {
    if (songBpm > 0.0 && positionProvider != nullptr) {
      const double currentSec = juce::jmax(0.0, positionProvider());
      const double beats = (currentSec * songBpm) / 60.0;
      const double phase = beats - std::floor(beats);
      pendingPulse = static_cast<float>(
          0.5 + 0.5 *
                    std::sin((phase * juce::MathConstants<double>::twoPi) -
                             juce::MathConstants<double>::halfPi));
    } else {
      pendingPulse = 0.35f;
    }
  }

  const auto lane = getArrangementLaneBounds(mainWaveBounds);
  if (lane.getWidth() <= 2.0f || lane.getHeight() <= 2.0f)
    return;

  if (editing && dragAnchorActive) {
    const float anchorX = sourceSecondsToWindowX(lane, dragAnchoredBoundarySec);
    if (anchorX >= lane.getX() && anchorX <= lane.getRight()) {
      g.setColour(UITheme::accent.withAlpha(0.85f));
      g.drawLine(anchorX, lane.getY() + 2.0f, anchorX, lane.getBottom() - 2.0f,
                 2.0f);
    }
  }

  for (const auto &overlay : partOverlayBounds) {
    const bool selected = editing && isPartSelected(overlay.partIndex);
    const bool loopActive = overlay.partIndex == activeLoopPartIndex;
    const bool pendingJump = showPendingJumpSectionHighlight && !loopActive &&
                             overlay.partIndex == pendingJumpPartIndex;
    if (!juce::isPositiveAndBelow(overlay.partIndex,
                                  static_cast<int>(arrangement.parts.size())))
      continue;

    const auto &part = arrangement.parts[static_cast<size_t>(overlay.partIndex)];
    const auto sectionColour = partColorOrFallback(part, overlay.partIndex);
    const bool muted =
        juce::isPositiveAndBelow(overlay.partIndex,
                                 static_cast<int>(arrangement.clips.size())) &&
        arrangement.clips[static_cast<size_t>(overlay.partIndex)].muted;
    auto fill = sectionColour.withAlpha(editing ? (selected ? 0.28f : 0.18f) : 0.16f);
    if (muted)
      fill = juce::Colours::black.withAlpha(selected ? 0.34f : 0.25f);

    g.setColour(fill);
    g.fillRoundedRectangle(overlay.body, 2.5f);

    const bool primaryInstance =
        !juce::isPositiveAndBelow(overlay.partIndex,
                                  static_cast<int>(arrangement.clips.size())) ||
        arrangement.clips[static_cast<size_t>(overlay.partIndex)].primaryInstance;
    if (primaryInstance &&
        (overlay.leftHandle.getCentreX() > overlay.body.getX() + 1.0f ||
         overlay.rightHandle.getCentreX() < overlay.body.getRight() - 1.0f)) {
      auto playbackBody = overlay.body.withX(
          juce::jlimit(overlay.body.getX(), overlay.body.getRight(),
                       overlay.leftHandle.getCentreX()));
      playbackBody.setRight(juce::jlimit(overlay.body.getX(), overlay.body.getRight(),
                                         overlay.rightHandle.getCentreX()));
      if (playbackBody.getWidth() > 1.5f) {
        g.setColour(sectionColour.withAlpha(muted ? 0.18f : 0.18f));
        g.fillRoundedRectangle(playbackBody.reduced(0.0f, 2.0f), 2.0f);
      }
    }

    if (pendingJump) {
      const float darkAlpha = juce::jlimit(
          0.16f, 0.40f, 0.22f + (pendingPulse * (editing ? 0.18f : 0.22f)));
      g.setColour(juce::Colours::black.withAlpha(darkAlpha));
      g.fillRoundedRectangle(overlay.body, 2.5f);
      g.setColour(juce::Colours::white.withAlpha(0.78f));
      g.drawRoundedRectangle(overlay.body, 2.5f, 1.05f);
    }

    const bool highlighted = selected || pendingJump;
    const float separatorX = overlay.body.getX();
    const float separatorThickness = highlighted ? 2.45f : 1.55f;
    const float separatorAlpha = highlighted ? 0.98f : 0.78f;
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.drawLine(separatorX + 0.8f, lane.getY() + 2.0f, separatorX + 0.8f,
               lane.getBottom() - 2.0f, separatorThickness + 0.9f);
    g.setColour((muted ? juce::Colours::white : sectionColour)
                    .withAlpha(muted ? (highlighted ? 0.54f : 0.34f)
                                     : separatorAlpha));
    g.drawLine(separatorX, lane.getY() + 2.0f, separatorX,
               lane.getBottom() - 2.0f, separatorThickness);
    auto partName = arrangementPartDisplayLabelForName(part.name);
    if (const auto utilityLabel = utilityCueDisplayLabelForName(part.name);
        utilityLabel.isNotEmpty()) {
      partName = utilityLabel;
    }

    auto textArea = overlay.body.toNearestInt().reduced(6, 4);
    if (overlay.body.getWidth() >= 18.0f && textArea.getHeight() >= 13) {
      const bool compactSection = overlay.body.getWidth() < 112.0f;
      const bool useAbbreviation = overlay.body.getWidth() < 68.0f;
      const auto visibleName =
          useAbbreviation ? abbreviatedArrangementLabel(partName) : partName;
      const auto labelFont = makeAppFont(compactSection ? 8.8f : 10.6f,
                                         AppFontWeight::SemiBold);
      auto labelBand =
          juce::Rectangle<float>(separatorX, overlay.body.getY() + 3.0f,
                                 overlay.body.getRight() - separatorX,
                                 compactSection ? 13.0f : 17.0f);
      labelBand.setRight(overlay.body.getRight());
      labelBand = labelBand.reduced(0.0f, 0.0f);

      const auto labelFill = sectionColour.withAlpha(
          highlighted ? 0.92f : (compactSection ? 0.54f : 0.80f));
      g.setColour(juce::Colours::black.withAlpha(compactSection ? 0.16f : 0.28f));
      g.fillRect(labelBand.translated(0.8f, 1.0f));
      g.setColour(labelFill);
      g.fillRect(labelBand);
      g.setColour(sectionColour.brighter(0.18f).withAlpha(
          highlighted ? 0.92f : (compactSection ? 0.32f : 0.62f)));
      if (compactSection)
        g.drawLine(labelBand.getX(), labelBand.getBottom(), labelBand.getRight(),
                   labelBand.getBottom(), highlighted ? 1.1f : 0.75f);
      else
        g.drawRect(labelBand, highlighted ? 1.2f : 0.8f);
      g.setColour(juce::Colours::black.withAlpha(0.48f));
      g.drawText(visibleName,
                 labelBand.translated(0.7f, 0.8f)
                     .toNearestInt()
                     .reduced(compactSection ? 4 : 8, 0),
                 juce::Justification::centredLeft, false);
      g.setColour(juce::Colours::white.withAlpha(0.96f));
      g.setFont(labelFont);
      g.drawFittedText(visibleName,
                       labelBand.toNearestInt().reduced(compactSection ? 4 : 8, 0),
                       juce::Justification::centredLeft, 1);
    }

    const bool showsSectionActions = hoveredActionPartIndex == overlay.partIndex;
    if ((showsSectionActions || loopActive) && overlay.showsLoopButton) {
      const bool loopHovered = hoveredLoopPartIndex == overlay.partIndex;
      const auto button = overlay.loopButton;
      const auto icon = overlay.loopIcon.withSizeKeepingCentre(
          overlay.loopIcon.getWidth() + (loopHovered ? 1.5f : 0.0f),
          overlay.loopIcon.getHeight() + (loopHovered ? 1.5f : 0.0f));
      const auto bubbleColour =
          loopActive ? sectionColour.withAlpha(loopHovered ? 0.94f : 0.72f)
                     : juce::Colours::black.withAlpha(loopHovered ? 0.30f : 0.18f);
      const float iconAlpha = loopHovered || loopActive ? 0.96f : 0.58f;

      g.setColour(juce::Colours::black.withAlpha(loopHovered ? 0.18f : 0.10f));
      g.fillEllipse(button.reduced(1.0f).translated(0.0f, 1.0f));
      g.setColour(bubbleColour);
      g.fillEllipse(button.reduced(loopHovered ? 4.0f : 5.0f));
      drawSvgIcon(g, icon, SvgIconName::Repeat,
                  juce::Colours::white.withAlpha(iconAlpha));
    }

    if (showsSectionActions && overlay.showsJumpButton) {
      const bool jumpHovered = hoveredJumpPartIndex == overlay.partIndex;
      const auto button = overlay.jumpButton;
      const auto icon = overlay.jumpIcon.withSizeKeepingCentre(
          overlay.jumpIcon.getWidth() + (jumpHovered ? 1.5f : 0.0f),
          overlay.jumpIcon.getHeight() + (jumpHovered ? 1.5f : 0.0f));
      const auto bubbleColour =
          jumpHovered ? sectionColour.withAlpha(0.58f)
                      : juce::Colours::black.withAlpha(0.18f);

      g.setColour(juce::Colours::black.withAlpha(jumpHovered ? 0.18f : 0.10f));
      g.fillEllipse(button.reduced(1.0f).translated(0.0f, 1.0f));
      g.setColour(bubbleColour);
      g.fillEllipse(button.reduced(jumpHovered ? 4.0f : 5.0f));
      drawSvgIcon(g, icon, SvgIconName::CornerDownRight,
                  juce::Colours::white.withAlpha(jumpHovered ? 0.96f : 0.58f));
    }

    if (!editing && showsSectionActions && overlay.showsMuteButton) {
      const bool muteHovered = hoveredMutePartIndex == overlay.partIndex;
      const auto muteFill =
          muted ? juce::Colour::fromRGB(214, 175, 82)
                      .withAlpha(muteHovered ? 0.86f : 0.68f)
                : juce::Colours::black.withAlpha(muteHovered ? 0.34f : 0.20f);
      g.setColour(juce::Colours::black.withAlpha(muteHovered ? 0.18f : 0.10f));
      g.fillEllipse(overlay.muteButton.reduced(1.0f).translated(0.0f, 1.0f));
      g.setColour(muteFill);
      g.fillEllipse(overlay.muteButton.reduced(muteHovered ? 4.0f : 5.0f));
      g.setColour(juce::Colours::white.withAlpha(muteHovered || muted ? 0.96f : 0.64f));
      g.setFont(makeAppFont(10.0f, AppFontWeight::Bold));
      g.drawText("M", overlay.muteButton.toNearestInt(),
                 juce::Justification::centred, false);
    }

    const bool showTimingText =
        editing && overlay.body.getWidth() >= 96.0f &&
        (selected || hoveredActionPartIndex == overlay.partIndex ||
         overlay.body.getWidth() >= 160.0f);
    if (showTimingText) {
      textArea.removeFromTop(17);
      g.setFont(juce::FontOptions(10.2f));
      g.drawFittedText(L10n::tr("Inicio ", "Start ") + formatSeconds(part.startSec),
                       textArea.removeFromTop(12), juce::Justification::topLeft, 1);
      g.drawFittedText(L10n::tr("Fin ", "End ") + formatSeconds(part.endSec),
                       textArea.removeFromTop(12), juce::Justification::topLeft, 1);
    }

    if (editing && overlay.showsDeleteButton) {
      const bool removeHovered = hoveredActionPartIndex == overlay.partIndex;
      g.setColour(juce::Colours::black.withAlpha(removeHovered ? 0.26f : 0.18f));
      g.fillEllipse(overlay.deleteButton.translated(0.0f, 1.0f));
      const auto removeFill =
          juce::Colour::fromRGB(176, 92, 92).withAlpha(removeHovered ? 0.62f : 0.46f);
      const auto removeBorder =
          juce::Colour::fromRGB(244, 210, 210).withAlpha(removeHovered ? 0.34f : 0.20f);
      g.setColour(removeFill);
      g.fillEllipse(overlay.deleteButton);
      g.setColour(removeBorder);
      g.drawEllipse(overlay.deleteButton, 1.0f);
      g.setColour(juce::Colours::white.withAlpha(0.96f));
      const auto db = overlay.deleteButton.reduced(5.0f, 5.0f);
      g.drawLine(db.getX(), db.getCentreY(), db.getRight(), db.getCentreY(), 1.9f);
    }

    if (editing && overlay.showsMuteButton) {
      const bool muteHovered = hoveredMutePartIndex == overlay.partIndex;
      const auto muteFill =
          muted ? juce::Colour::fromRGB(214, 175, 82).withAlpha(muteHovered ? 0.78f : 0.62f)
                : juce::Colours::black.withAlpha(muteHovered ? 0.58f : 0.42f);
      g.setColour(juce::Colours::black.withAlpha(muteHovered ? 0.28f : 0.18f));
      g.fillEllipse(overlay.muteButton.translated(0.0f, 1.0f));
      g.setColour(muteFill);
      g.fillEllipse(overlay.muteButton);
      g.setColour(juce::Colours::white.withAlpha(muteHovered || muted ? 0.96f : 0.78f));
      g.setFont(makeAppFont(10.5f, AppFontWeight::Bold));
      g.drawText("M", overlay.muteButton.toNearestInt(),
                 juce::Justification::centred, false);
    }

    if (editing && overlay.showsAddButton) {
      const bool addHovered = hoveredAddPartIndex == overlay.partIndex;
      g.setColour(juce::Colours::black.withAlpha(addHovered ? 0.28f : 0.18f));
      g.fillEllipse(overlay.addButton.translated(0.0f, 1.0f));
      g.setColour(juce::Colours::black.withAlpha(addHovered ? 0.66f : 0.48f));
      g.fillEllipse(overlay.addButton);
      g.setColour(juce::Colours::white.withAlpha(addHovered ? 0.22f : 0.10f));
      g.drawEllipse(overlay.addButton, 1.0f);
      g.setColour(juce::Colours::white.withAlpha(addHovered ? 0.98f : 0.94f));
      g.drawLine(overlay.addIcon.getCentreX(), overlay.addIcon.getY(),
                 overlay.addIcon.getCentreX(), overlay.addIcon.getBottom(), 1.8f);
      g.drawLine(overlay.addIcon.getX(), overlay.addIcon.getCentreY(),
                 overlay.addIcon.getRight(), overlay.addIcon.getCentreY(), 1.8f);
    }
  }
}

void WaveformBar::drawTempoOverlay(
    juce::Graphics &g, const juce::Rectangle<float> &mainWaveBounds) {
  const auto lane = getArrangementLaneBounds(mainWaveBounds);
  if (lane.getWidth() <= 2.0f || lane.getHeight() <= 2.0f)
    return;

  const bool editing = isEditingArrangement();
  for (int i = 0; i < static_cast<int>(tempoMap.regions.size()); ++i) {
    const auto &region = tempoMap.regions[static_cast<size_t>(i)];
    const auto regionColour = tempoRegionColourForIndex(i);
    const float startX = sourceSecondsToWindowX(lane, region.startSec);
    const float endX = sourceSecondsToWindowX(lane, region.endSec);
    if (endX <= lane.getX() || startX >= lane.getRight())
      continue;

    auto body = lane.withX(juce::jmax(lane.getX(), startX))
                    .withRight(juce::jmin(lane.getRight(), endX))
                    .reduced(1.0f, 2.0f);
    if (body.getWidth() < 2.0f)
      continue;

    const bool selected = editing && i == selectedTempoRegionIndex;
    auto fill = regionColour.withAlpha(editing ? (selected ? 0.30f : 0.19f) : 0.16f);
    g.setColour(fill);
    g.fillRoundedRectangle(body, 2.5f);
    g.setColour(regionColour.withAlpha(selected ? 0.95f : 0.72f));
    g.drawRoundedRectangle(body, 2.5f, selected ? 1.6f : 1.0f);

    const float separatorX = body.getX();
    const float separatorThickness = selected ? 2.45f : 1.55f;
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.drawLine(separatorX + 0.8f, lane.getY() + 2.0f, separatorX + 0.8f,
               lane.getBottom() - 2.0f, separatorThickness + 0.9f);
    g.setColour(regionColour.withAlpha(selected ? 0.98f : 0.78f));
    g.drawLine(separatorX, lane.getY() + 2.0f, separatorX,
               lane.getBottom() - 2.0f, separatorThickness);

    if (editing) {
      g.setColour((selected ? regionColour : UITheme::textSecondary)
                      .withAlpha(selected ? 0.90f : 0.45f));
      const float handleInset = 5.0f;
      g.drawVerticalLine(static_cast<int>(std::round(body.getX())),
                         body.getY() + handleInset,
                         body.getBottom() - handleInset);
      g.drawVerticalLine(static_cast<int>(std::round(body.getRight())),
                         body.getY() + handleInset,
                         body.getBottom() - handleInset);

      constexpr float deleteSize = 13.0f;
      const bool showsDeleteButton =
          tempoMap.regions.size() > 1 &&
          body.getWidth() >= (deleteSize + 16.0f) &&
          body.getHeight() >= (deleteSize + 8.0f);
      if (showsDeleteButton) {
        const auto deleteButton = juce::Rectangle<float>(
            body.getRight() - deleteSize - 3.0f, body.getY() + 3.0f,
            deleteSize, deleteSize);
        g.setColour(juce::Colours::white.withAlpha(selected ? 0.20f : 0.15f));
        g.fillRoundedRectangle(deleteButton, 2.0f);
        g.setColour(juce::Colours::white.withAlpha(selected ? 0.96f : 0.86f));
        const auto db = deleteButton.reduced(3.2f, 3.2f);
        g.drawLine(db.getX(), db.getY(), db.getRight(), db.getBottom(), 1.2f);
        g.drawLine(db.getRight(), db.getY(), db.getX(), db.getBottom(), 1.2f);
      }
    }

    if (body.getWidth() >= 18.0f) {
      const bool compact = body.getWidth() < 92.0f;
      auto visibleLabel = tempoRegionDisplayLabel(region, compact);
      if (body.getWidth() < 52.0f)
        visibleLabel = juce::String(region.bpm, 0);

      const auto labelFont = makeAppFont(compact ? 9.4f : 10.8f,
                                         AppFontWeight::SemiBold);
      const float textWidth = labelFont.getStringWidthFloat(visibleLabel);
      const float maxFlagWidth = juce::jmax(16.0f, body.getWidth() - 2.0f);
      const float flagWidth = juce::jlimit(16.0f, maxFlagWidth, textWidth + 18.0f);
      auto flag = juce::Rectangle<float>(separatorX, body.getY() + 1.0f,
                                         flagWidth, 16.0f);
      if (flag.getRight() > body.getRight() - 1.0f)
        flag.setRight(body.getRight() - 1.0f);

      const auto labelFill = regionColour.withAlpha(selected ? 0.96f : 0.88f);
      g.setColour(juce::Colours::black.withAlpha(0.28f));
      g.fillRect(flag.translated(0.8f, 1.0f));
      g.setColour(labelFill);
      g.fillRect(flag);
      g.setColour(regionColour.brighter(0.18f)
                      .withAlpha(selected ? 0.95f : 0.72f));
      g.drawRect(flag, selected ? 1.2f : 0.8f);
      g.setColour(juce::Colours::black.withAlpha(0.48f));
      g.drawText(visibleLabel,
                 flag.translated(0.7f, 0.8f).toNearestInt().reduced(4, 0),
                 juce::Justification::centred, false);
      g.setColour(juce::Colours::white.withAlpha(0.96f));
      g.setFont(labelFont);
      g.drawFittedText(visibleLabel, flag.toNearestInt().reduced(4, 0),
                       juce::Justification::centred, 1);
    }
  }
}

void WaveformBar::drawMiniMap(juce::Graphics &g,
                              const juce::Rectangle<float> &miniMapBounds) const {
  if (miniMapBounds.getWidth() <= 2.0f || miniMapBounds.getHeight() <= 2.0f)
    return;

  g.setColour(UITheme::panelDark.withAlpha(0.45f));
  g.fillRoundedRectangle(miniMapBounds, 4.0f);

  drawWaveform(g, miniMapBounds, 0.0f, 1.0f, false, false);

  const double timelineLength = getVisualTimelineLengthSeconds();
  if (timelineLength > 0.0) {
    for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
      const auto &part = arrangement.parts[static_cast<size_t>(i)];
      const float x = miniMapBounds.getX() +
                      static_cast<float>(
                          juce::jlimit(0.0, 1.0,
                                       visualSecondsForPartStart(i) /
                                           timelineLength)) *
                          miniMapBounds.getWidth();
      g.setColour(partColorOrFallback(part, i).withAlpha(0.7f));
      g.drawLine(x, miniMapBounds.getY(), x, miniMapBounds.getBottom(), 0.7f);
    }
  }

  const auto viewport = getViewportBoundsInMiniMap(miniMapBounds);

  g.setColour(UITheme::panelDark.withAlpha(0.6f));
  g.fillRect(miniMapBounds.withRight(viewport.getX()));
  g.fillRect(miniMapBounds.withLeft(viewport.getRight()));

  g.setColour(UITheme::accent.withAlpha(0.2f));
  g.fillRect(viewport);
  g.setColour(UITheme::accent.withAlpha(0.9f));
  g.drawRect(viewport, 1.5f);

  const float handleWidth = juce::jmin(8.0f, juce::jmax(4.0f, viewport.getWidth() * 0.2f));
  g.setColour(UITheme::accentSoft.withAlpha(0.95f));
  g.fillRect(juce::Rectangle<float>(viewport.getX(), viewport.getY(), handleWidth,
                                    viewport.getHeight()));
  g.fillRect(juce::Rectangle<float>(viewport.getRight() - handleWidth,
                                    viewport.getY(), handleWidth,
                                    viewport.getHeight()));

  if (positionProvider && lengthProvider) {
    const double length = juce::jmax(0.0, lengthProvider());
    if (length > 0.0) {
      double current = juce::jmax(0.0, positionProvider());
      bool showPendingTravelSeek = false;
      float pendingTravelRatio = 0.0f;
      if (followPlaybackEnabled && !isEditingArrangement() &&
          playingProvider != nullptr && playingProvider()) {
        float pendingRatio = 0.0f;
        if (getPendingJumpProgressRatio(pendingRatio)) {
          pendingTravelRatio = pendingRatio;
          showPendingTravelSeek = true;
        }
      }
      const double visualCurrent = visualSecondsFromSourceSeconds(current);
      const float progress = static_cast<float>(juce::jlimit(
          0.0, 1.0, visualCurrent / juce::jmax(0.0001, timelineLength)));
      const float x = snapToPixelCenter(
          miniMapBounds.getX() + progress * miniMapBounds.getWidth());
      drawPlayheadNeedle(g, miniMapBounds, x, false, false, 0.95f);

      if (showPendingTravelSeek) {
        const float pendingX = snapToPixelCenter(
            miniMapBounds.getX() + pendingTravelRatio * miniMapBounds.getWidth());
        g.setColour(juce::Colours::white.withAlpha(0.62f));
        g.drawLine(pendingX, miniMapBounds.getY(), pendingX,
                   miniMapBounds.getBottom(), 0.95f);
      }
    }
  }

  g.setColour(UITheme::cardBorder.withAlpha(0.85f));
  g.drawRoundedRectangle(miniMapBounds, 4.0f, 1.0f);
}

void WaveformBar::rebuildSectionAddButtons() {
  sectionAddButtons.clear();
  sectionAddButtons.reserve(kSongSectionSuggestions.size() + 2);

  for (const auto *section : kSongSectionSuggestions) {
    auto btn = std::make_unique<juce::TextButton>();
    const juce::String sectionName = section != nullptr
                                         ? juce::String(section)
                                         : L10n::tr("Parte", "Part");
    const auto displayName = arrangementPartDisplayLabelForName(sectionName);
    btn->setButtonText("+ " + displayName);
    btn->setTooltip(L10n::tr("Agregar ", "Add ") + displayName);
    btn->setViewportIgnoreDragFlag(true);
    auto *buttonPtr = btn.get();
    btn->onClick = [this, sectionName, buttonPtr] {
      const int variantLimit = sectionVariantLimitForName(sectionName);
      if (variantLimit <= 0) {
        addArrangementPart(sectionName);
        return;
      }

      auto safe = juce::Component::SafePointer<WaveformBar>(this);
      auto panel = std::make_unique<SectionVariantPopover>(
          sectionName, variantLimit,
          [safe](juce::String selectedSectionName) {
            if (safe != nullptr)
              safe->addArrangementPart(selectedSectionName);
          });

      const int rows = (variantLimit + 1) / 2;
      panel->setSize(250, 44 + (rows * 28) + (juce::jmax(0, rows - 1) * 6));

      auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
      juce::Component *parentComp = editor != nullptr
                                        ? static_cast<juce::Component *>(editor)
                                        : static_cast<juce::Component *>(this);
      const auto anchor =
          parentComp->getLocalArea(buttonPtr, buttonPtr->getLocalBounds());
      auto &callout = juce::CallOutBox::launchAsynchronously(
          std::move(panel), anchor, parentComp);
      callout.setDismissalMouseClicksAreAlwaysConsumed(true);
    };
    addAndMakeVisible(*btn);
    btn->setVisible(isEditingArrangement());
    sectionAddButtons.push_back(std::move(btn));
  }

  auto countBtn = std::make_unique<juce::TextButton>();
  const auto countLabel = L10n::tr("Cuenta", "Count");
  auto *countBtnPtr = countBtn.get();
  countBtn->setButtonText("+ " + countLabel);
  countBtn->setTooltip(L10n::tr("Agregar cuenta intermedia", "Add intermediate count"));
  countBtn->setViewportIgnoreDragFlag(true);
  countBtn->onClick = [this, countBtnPtr] {
    auto safe = juce::Component::SafePointer<WaveformBar>(this);
    auto panel = std::make_unique<CountPickerPopover>(
        [safe](const int selectedCount) {
          if (safe == nullptr || selectedCount < 1 || selectedCount > 7)
            return;
          safe->addCountSequenceParts(selectedCount, selectedCount);
        });
    panel->setSize(302, 112);

    auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
    juce::Component *parentComp = editor != nullptr
                                      ? static_cast<juce::Component *>(editor)
                                      : static_cast<juce::Component *>(this);
    const auto anchor =
        parentComp->getLocalArea(countBtnPtr, countBtnPtr->getLocalBounds());
    auto &callout = juce::CallOutBox::launchAsynchronously(
        std::move(panel), anchor, parentComp);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
  };
  addAndMakeVisible(*countBtn);
  countBtn->setVisible(isEditingArrangement());
  sectionAddButtons.push_back(std::move(countBtn));

  auto utilityBtn = std::make_unique<juce::TextButton>();
  auto *utilityBtnPtr = utilityBtn.get();
  utilityBtn->setButtonText("+ " + L10n::tr("Utilidad", "Utility"));
  utilityBtn->setTooltip(L10n::tr("Agregar utilidad de guía",
                                  "Add guide utility"));
  utilityBtn->setViewportIgnoreDragFlag(true);
  utilityBtn->onClick = [this, utilityBtnPtr] {
    auto safe = juce::Component::SafePointer<WaveformBar>(this);
    auto panel = std::make_unique<UtilityCuePopover>(
        [safe](juce::String cueName) {
          if (safe != nullptr)
            safe->addUtilityCuePart(cueName);
        });
    constexpr int kCols = 2;
    const int rows =
        (static_cast<int>(kUtilityCueSuggestions.size()) + kCols - 1) / kCols;
    panel->setSize(326, 44 + (rows * 28) + (juce::jmax(0, rows - 1) * 6));

    auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
    juce::Component *parentComp = editor != nullptr
                                      ? static_cast<juce::Component *>(editor)
                                      : static_cast<juce::Component *>(this);
    const auto anchor =
        parentComp->getLocalArea(utilityBtnPtr, utilityBtnPtr->getLocalBounds());
    auto &callout = juce::CallOutBox::launchAsynchronously(
        std::move(panel), anchor, parentComp);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
  };
  addAndMakeVisible(*utilityBtn);
  utilityBtn->setVisible(isEditingArrangement());
  sectionAddButtons.push_back(std::move(utilityBtn));
}

void WaveformBar::addArrangementPart(const juce::String &preferredName) {
  const auto [playableStartSec, playableEndSec] = getPlayableRangeSeconds();
  const double playableLength = playableEndSec - playableStartSec;
  if (playableLength < kMinPartDurationSec)
    return;

  juce::String nextName = preferredName.trim();
  if (nextName.isEmpty()) {
    const int suggestionIndex =
        static_cast<int>(arrangement.parts.size() % kSongSectionSuggestions.size());
    nextName = kSongSectionSuggestions[static_cast<size_t>(suggestionIndex)];
  }
  if (nextName.isEmpty())
    nextName = L10n::tr("Parte", "Part");

  double startSec = getCurrentSeekSourceSeconds();

  startSec =
      juce::jlimit(playableStartSec, playableEndSec, snapSecondsForward(startSec));
  if ((playableEndSec - startSec) < kMinPartDurationSec)
    startSec = juce::jmax(playableStartSec, playableEndSec - kMinPartDurationSec);

  double endSec =
      juce::jmin(playableEndSec, startSec + juce::jmax(8.0, kMinPartDurationSec));
  endSec = snapSeconds(endSec);
  endSec = juce::jlimit(playableStartSec, playableEndSec, endSec);
  if ((endSec - startSec) < kMinPartDurationSec) {
    endSec = juce::jmin(playableEndSec, startSec + kMinPartDurationSec);
    if ((endSec - startSec) < kMinPartDurationSec)
      startSec = juce::jmax(playableStartSec, endSec - kMinPartDurationSec);
  }

  SongArrangementPartSnapshot part;
  part.id = juce::Uuid().toString();
  part.name = nextName;
  part.color = standardArrangementPartColorForName(nextName);
  if (part.color.isEmpty())
    part.color = fallbackArrangementPartColorForName(nextName);
  part.startSec = startSec;
  part.endSec = endSec;

  if (arrangement.clips.size() != arrangement.parts.size())
    rebuildClipsFromParts();

  const int insertIndex = findInsertIndexForCurrentSeek();
  arrangement.parts.insert(arrangement.parts.begin() + insertIndex, part);
  arrangement.clips.insert(arrangement.clips.begin() + insertIndex,
                           SongArrangementClipSnapshot{});
  arrangement.clips[static_cast<size_t>(insertIndex)].id = part.id;
  arrangement.clips[static_cast<size_t>(insertIndex)].sourceSectionId = part.id;
  arrangement.clips[static_cast<size_t>(insertIndex)].label = part.name;
  arrangement.clips[static_cast<size_t>(insertIndex)].color = part.color;
  arrangement.clips[static_cast<size_t>(insertIndex)].sourceStartSec =
      part.startSec;
  arrangement.clips[static_cast<size_t>(insertIndex)].sourceEndSec = part.endSec;
  arrangement.clips[static_cast<size_t>(insertIndex)].playbackStartSec =
      part.startSec;
  arrangement.clips[static_cast<size_t>(insertIndex)].playbackEndSec =
      part.endSec;
  arrangement.clips[static_cast<size_t>(insertIndex)].primaryInstance = true;
  setSingleSelectedPart(insertIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::addCountSequenceParts(const int firstCount,
                                        const int lastCount) {
  if (firstCount <= 0 || lastCount < firstCount)
    return;

  const auto [playableStartSec, playableEndSec] = getPlayableRangeSeconds();
  const double playableLength = playableEndSec - playableStartSec;
  if (playableLength < kMinPartDurationSec)
    return;

  const double beatDurationSec = songBpm > 0.0 ? 60.0 / songBpm : 0.0;
  const double snapStepSec = getSnapStepSeconds();
  const double oneCountDurationSec =
      juce::jmax(kMinPartDurationSec,
                 snapStepSec > 0.0
                     ? snapStepSec
                     : (beatDurationSec > 0.0 ? beatDurationSec : 0.5));

  double cursorSec = getCurrentSeekSourceSeconds();

  cursorSec =
      juce::jlimit(playableStartSec, playableEndSec, snapSecondsForward(cursorSec));

  if (arrangement.clips.size() != arrangement.parts.size())
    rebuildClipsFromParts();

  const int insertIndex = findInsertIndexForCurrentSeek();
  int lastInsertedIndex = -1;
  for (int count = firstCount; count <= lastCount; ++count) {
    const double startSec = cursorSec;
    double endSec = juce::jmin(playableEndSec, startSec + oneCountDurationSec);
    if ((endSec - startSec) < kMinPartDurationSec)
      break;

    SongArrangementPartSnapshot part;
    part.id = juce::Uuid().toString();
    part.name = juce::String(count);
    part.color = standardArrangementPartColorForName(part.name);
    if (part.color.isEmpty())
      part.color = fallbackArrangementPartColorForName(part.name);
    part.startSec = startSec;
    part.endSec = endSec;

    SongArrangementClipSnapshot clip;
    clip.id = part.id;
    clip.sourceSectionId = part.id;
    clip.label = part.name;
    clip.color = part.color;
    clip.sourceStartSec = part.startSec;
    clip.sourceEndSec = part.endSec;
    clip.playbackStartSec = part.startSec;
    clip.playbackEndSec = part.endSec;
    clip.primaryInstance = true;

    const int targetIndex = insertIndex + (count - firstCount);
    arrangement.parts.insert(arrangement.parts.begin() + targetIndex, part);
    arrangement.clips.insert(arrangement.clips.begin() + targetIndex, clip);
    lastInsertedIndex = targetIndex;
    cursorSec = endSec;
  }

  if (lastInsertedIndex < 0)
    return;

  setSingleSelectedPart(lastInsertedIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::addUtilityCuePart(const juce::String &cueName) {
  const auto normalizedCue = DynamicGuideEngine::normalizeSectionKey(cueName);
  if (normalizedCue.isEmpty())
    return;

  const auto [playableStartSec, playableEndSec] = getPlayableRangeSeconds();
  const double playableLength = playableEndSec - playableStartSec;
  if (playableLength < kMinPartDurationSec)
    return;

  const double beatDurationSec = songBpm > 0.0 ? 60.0 / songBpm : 0.0;
  const double snapStepSec = getSnapStepSeconds();
  const double cueDurationSec =
      juce::jmax(kMinPartDurationSec,
                 snapStepSec > 0.0
                     ? snapStepSec
                     : (beatDurationSec > 0.0 ? beatDurationSec : 0.5));

  double startSec = getCurrentSeekSourceSeconds();
  startSec =
      juce::jlimit(playableStartSec, playableEndSec, snapSecondsForward(startSec));
  if ((playableEndSec - startSec) < kMinPartDurationSec)
    startSec = juce::jmax(playableStartSec, playableEndSec - kMinPartDurationSec);

  double endSec = juce::jmin(playableEndSec, startSec + cueDurationSec);
  if ((endSec - startSec) < kMinPartDurationSec)
    return;

  SongArrangementPartSnapshot part;
  part.id = juce::Uuid().toString();
  part.name = cueName.trim();
  part.color = fallbackArrangementPartColorForName(part.name);
  part.startSec = startSec;
  part.endSec = endSec;

  if (arrangement.clips.size() != arrangement.parts.size())
    rebuildClipsFromParts();

  SongArrangementClipSnapshot clip;
  clip.id = part.id;
  clip.sourceSectionId = part.id;
  clip.label = part.name;
  clip.color = part.color;
  clip.sourceStartSec = part.startSec;
  clip.sourceEndSec = part.endSec;
  clip.playbackStartSec = part.startSec;
  clip.playbackEndSec = part.endSec;
  clip.primaryInstance = true;

  const int insertIndex = findInsertIndexForCurrentSeek();
  arrangement.parts.insert(arrangement.parts.begin() + insertIndex, part);
  arrangement.clips.insert(arrangement.clips.begin() + insertIndex, clip);
  setSingleSelectedPart(insertIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::duplicateArrangementPart(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;
  const auto [playableStartSec, playableEndSec] = getPlayableRangeSeconds();
  const double playableLength = playableEndSec - playableStartSec;
  if (playableLength < kMinPartDurationSec)
    return;

  auto copy = arrangement.parts[static_cast<size_t>(partIndex)];
  const double desiredDuration = juce::jlimit(
      kMinPartDurationSec, playableLength,
      juce::jmax(kMinPartDurationSec, copy.endSec - copy.startSec));

  double startSec =
      juce::jlimit(playableStartSec, playableEndSec, copy.startSec);
  if ((playableEndSec - startSec) < desiredDuration)
    startSec = juce::jmax(playableStartSec, playableEndSec - desiredDuration);
  double endSec = juce::jmin(playableEndSec, startSec + desiredDuration);

  if ((endSec - startSec) < kMinPartDurationSec)
    return;

  copy.startSec = startSec;
  copy.endSec = endSec;
  copy.id = juce::Uuid().toString();
  const auto originalName = copy.name.trim();
  if (originalName.isEmpty())
    copy.name = L10n::tr("Parte", "Part");
  copy.color = standardArrangementPartColorForName(copy.name);
  if (copy.color.isEmpty())
    copy.color = fallbackArrangementPartColorForName(copy.name);
  const int insertedIndex = partIndex + 1;
  const auto originalPartId =
      arrangement.parts[static_cast<size_t>(partIndex)].id;
  if (arrangement.clips.size() != arrangement.parts.size())
    rebuildClipsFromParts();
  arrangement.parts.insert(arrangement.parts.begin() + insertedIndex, copy);

  SongArrangementClipSnapshot clip;
  auto sourceClipIt = std::find_if(
      arrangement.clips.begin(), arrangement.clips.end(),
      [&originalPartId](const SongArrangementClipSnapshot &candidate) {
        return candidate.id == originalPartId;
      });
  if (sourceClipIt != arrangement.clips.end())
    clip = *sourceClipIt;

  clip.id = arrangement.parts[static_cast<size_t>(insertedIndex)].id;
  if (clip.sourceSectionId.trim().isEmpty())
    clip.sourceSectionId = originalPartId;
  clip.label = arrangement.parts[static_cast<size_t>(insertedIndex)].name;
  clip.color = arrangement.parts[static_cast<size_t>(insertedIndex)].color;
  if (clip.sourceEndSec <= clip.sourceStartSec) {
    clip.sourceStartSec =
        arrangement.parts[static_cast<size_t>(insertedIndex)].startSec;
    clip.sourceEndSec =
        arrangement.parts[static_cast<size_t>(insertedIndex)].endSec;
  }
  clip.playbackStartSec =
      arrangement.parts[static_cast<size_t>(insertedIndex)].startSec;
  clip.playbackEndSec =
      arrangement.parts[static_cast<size_t>(insertedIndex)].endSec;
  clip.primaryInstance = false;
  arrangement.clips.insert(arrangement.clips.begin() + insertedIndex, clip);

  setSingleSelectedPart(insertedIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::renameArrangementPart(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  auto *dialog = new juce::AlertWindow(L10n::tr("Renombrar parte", "Rename part"),
                                       L10n::tr("Nuevo nombre", "New name"),
                                       juce::AlertWindow::NoIcon);
  dialog->addTextEditor("name",
                        arrangement.parts[static_cast<size_t>(partIndex)].name,
                        L10n::tr("Nombre", "Name"));
  dialog->addButton(L10n::tr("Cancelar", "Cancel"), 0);
  dialog->addButton(L10n::tr("Guardar", "Save"), 1);

  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  dialog->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safe, dialog, partIndex](int result) {
        if (result != 1 || safe == nullptr)
          return;
        if (!juce::isPositiveAndBelow(
                partIndex, static_cast<int>(safe->arrangement.parts.size())))
          return;
        const auto nextName = dialog->getTextEditorContents("name").trim();
        safe->arrangement.parts[static_cast<size_t>(partIndex)].name =
            nextName.isEmpty() ? L10n::tr("Parte", "Part") : nextName;
        safe->setSingleSelectedPart(partIndex);
        safe->customArrangement = true;
        safe->notifyArrangementChanged(true);
        safe->repaint();
      }),
      true);
}

void WaveformBar::editArrangementPartRange(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  const auto &part = arrangement.parts[static_cast<size_t>(partIndex)];
  auto *dialog = new juce::AlertWindow(L10n::tr("Editar rango", "Edit range"),
                                       L10n::tr("Ajusta inicio y fin",
                                                "Adjust start and end"),
                                       juce::AlertWindow::NoIcon);
  dialog->addTextEditor("start", juce::String(part.startSec, 2),
                        L10n::tr("Inicio (seg)", "Start (sec)"));
  dialog->addTextEditor("end", juce::String(part.endSec, 2),
                        L10n::tr("Fin (seg)", "End (sec)"));
  dialog->addButton(L10n::tr("Cancelar", "Cancel"), 0);
  dialog->addButton(L10n::tr("Guardar", "Save"), 1);

  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  dialog->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safe, dialog, partIndex](int result) {
        if (result != 1 || safe == nullptr)
          return;
        if (!juce::isPositiveAndBelow(
                partIndex, static_cast<int>(safe->arrangement.parts.size())))
          return;

        const double songLength = safe->getSongLengthSeconds();
        double start =
            safe->snapSeconds(dialog->getTextEditorContents("start").getDoubleValue());
        double end =
            safe->snapSeconds(dialog->getTextEditorContents("end").getDoubleValue());

        start = juce::jlimit(0.0, songLength, start);
        end = juce::jlimit(0.0, songLength, end);
        if (end < start)
          std::swap(start, end);

        if ((end - start) < kMinPartDurationSec) {
          end = juce::jmin(songLength, start + kMinPartDurationSec);
          if ((end - start) < kMinPartDurationSec)
            start = juce::jmax(0.0, end - kMinPartDurationSec);
        }

        auto &editedPart = safe->arrangement.parts[static_cast<size_t>(partIndex)];
        editedPart.startSec = start;
        editedPart.endSec = end;
        safe->setSingleSelectedPart(partIndex);
        safe->customArrangement = true;
        safe->notifyArrangementChanged(true);
        safe->repaint();
      }),
      true);
}

void WaveformBar::toggleArrangementPartMute(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  if (arrangement.clips.size() != arrangement.parts.size())
    rebuildClipsFromParts();

  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.clips.size())))
    return;

  auto &clip = arrangement.clips[static_cast<size_t>(partIndex)];
  clip.muted = !clip.muted;
  setSingleSelectedPart(partIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::deleteArrangementPart(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  if (arrangement.parts.size() <= 1) {
    resetArrangementToDefault();
    return;
  }

  arrangement.parts.erase(arrangement.parts.begin() + partIndex);
  setSingleSelectedPart(
      juce::jlimit(0, static_cast<int>(arrangement.parts.size()) - 1, partIndex));
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::deleteSelectedArrangementParts() {
  if (arrangement.parts.empty())
    return;

  pruneSelectedPartIds();
  std::vector<juce::String> idsToDelete = selectedPartIds;
  if (idsToDelete.empty() && juce::isPositiveAndBelow(
                                 selectedPartIndex,
                                 static_cast<int>(arrangement.parts.size()))) {
    idsToDelete.push_back(
        arrangement.parts[static_cast<size_t>(selectedPartIndex)].id);
  }

  if (idsToDelete.empty())
    return;

  if (idsToDelete.size() >= arrangement.parts.size()) {
    resetArrangementToDefault();
    return;
  }

  const auto shouldDelete = [&idsToDelete](const SongArrangementPartSnapshot &part) {
    return std::find(idsToDelete.begin(), idsToDelete.end(), part.id) !=
           idsToDelete.end();
  };

  int fallbackIndex = selectedPartIndex;
  arrangement.parts.erase(
      std::remove_if(arrangement.parts.begin(), arrangement.parts.end(), shouldDelete),
      arrangement.parts.end());
  fallbackIndex = juce::jlimit(0, static_cast<int>(arrangement.parts.size()) - 1,
                               fallbackIndex);
  setSingleSelectedPart(fallbackIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::notifyArrangementChanged(const bool isCustom) {
  rememberArrangementForUndo();
  normalizeArrangement(!usesVirtualClips(), false);
  rebuildClipsFromParts();
  lastCommittedArrangement = arrangement;
  const auto arrangementToPersist = buildArrangementSnapshotForPersistence();
  sourceArrangementParts = arrangementToPersist.parts;
  if (onArrangementChanged)
    onArrangementChanged(arrangementToPersist, isCustom);
}

void WaveformBar::resetArrangementToDefault() {
  rememberArrangementForUndo();

  SongArrangementSnapshot resetArrangement;
  resetArrangement.parts = sourceArrangementParts;
  resetArrangement.clips.clear();
  arrangement = resetArrangement;

  customArrangement = !arrangement.parts.empty();
  selectedPartIndex = -1;
  selectedPartIds.clear();
  hoveredActionPartIndex = -1;
  hoveredAddPartIndex = -1;
  hoveredMutePartIndex = -1;
  hoveredLoopPartIndex = -1;
  hoveredJumpPartIndex = -1;
  setVisibleWindow(0.0f, 1.0f);

  normalizeArrangement(!usesVirtualClips(), false);
  lastCommittedArrangement = arrangement;
  arrangementRedoStack.clear();
  if (onArrangementChanged)
    onArrangementChanged(arrangement, customArrangement);
  repaint();
}

void WaveformBar::normalizeTempoMap() {
  const double songLength = getSongLengthSeconds();
  if (tempoMap.regions.empty() && songLength > 0.0) {
    SongTempoRegionSnapshot region;
    region.id = juce::Uuid().toString();
    region.startSec = 0.0;
    region.endSec = songLength;
    region.bpm = songBpm > 0.0 ? songBpm : 120.0;
    region.timeSignature = songTimeSignature;
    region.clickDivision = ClickDivision::Quarter;
    region.accentEnabled = true;
    tempoMap.regions.push_back(std::move(region));
  }

  for (auto &region : tempoMap.regions) {
    if (region.id.isEmpty())
      region.id = juce::Uuid().toString();
    region.startSec = juce::jmax(0.0, region.startSec);
    if (songLength > 0.0)
      region.endSec = juce::jlimit(region.startSec + kMinPartDurationSec,
                                   songLength, region.endSec);
    if (region.bpm <= 0.0)
      region.bpm = songBpm > 0.0 ? songBpm : 120.0;
    if (region.timeSignature.trim().isEmpty())
      region.timeSignature = songTimeSignature;
  }

  std::sort(tempoMap.regions.begin(), tempoMap.regions.end(),
            [](const auto &a, const auto &b) {
              return a.startSec < b.startSec;
            });
  if (!juce::isPositiveAndBelow(selectedTempoRegionIndex,
                                static_cast<int>(tempoMap.regions.size())))
    selectedTempoRegionIndex = tempoMap.regions.empty() ? -1 : 0;
}

void WaveformBar::selectTempoRegion(const int regionIndex) {
  if (!juce::isPositiveAndBelow(regionIndex,
                                static_cast<int>(tempoMap.regions.size())))
    return;
  selectedTempoRegionIndex = regionIndex;
  updateTempoControlsFromSelection();
}

void WaveformBar::updateTempoControlsFromSelection() {
  const bool hasSelection =
      juce::isPositiveAndBelow(selectedTempoRegionIndex,
                               static_cast<int>(tempoMap.regions.size()));
  tempoBpmEditor.setEnabled(hasSelection);
  tempoSignatureBox.setEnabled(hasSelection);
  if (!hasSelection)
    return;

  const auto &region = tempoMap.regions[static_cast<size_t>(selectedTempoRegionIndex)];
  tempoBpmEditor.setText(juce::String(region.bpm, 1),
                         juce::dontSendNotification);
  const auto signature = region.timeSignature.trim();
  for (int i = 0; i < tempoSignatureBox.getNumItems(); ++i) {
    if (tempoSignatureBox.getItemText(i) == signature) {
      tempoSignatureBox.setSelectedItemIndex(i, juce::dontSendNotification);
      break;
    }
  }
}

void WaveformBar::applyTempoControlsToSelection() {
  if (!juce::isPositiveAndBelow(selectedTempoRegionIndex,
                                static_cast<int>(tempoMap.regions.size())))
    return;
  auto &region = tempoMap.regions[static_cast<size_t>(selectedTempoRegionIndex)];
  const double bpm = tempoBpmEditor.getText().getDoubleValue();
  if (bpm > 0.0)
    region.bpm = juce::jlimit(20.0, 300.0, bpm);
  if (tempoSignatureBox.getSelectedItemIndex() >= 0)
    region.timeSignature = tempoSignatureBox.getText().trim();
  notifyTempoMapChanged();
}

void WaveformBar::deleteTempoRegion(const int regionIndex) {
  if (!juce::isPositiveAndBelow(regionIndex,
                                static_cast<int>(tempoMap.regions.size())))
    return;

  if (tempoMap.regions.size() <= 1)
    return;

  tempoMap.regions.erase(tempoMap.regions.begin() + regionIndex);
  selectedTempoRegionIndex =
      juce::jlimit(0, static_cast<int>(tempoMap.regions.size()) - 1,
                   regionIndex);
  activeDragTempoRegionIndex = -1;
  tempoDragPendingCommit = false;
  notifyTempoMapChanged();
}

void WaveformBar::notifyTempoMapChanged() {
  normalizeTempoMap();
  updateTempoControlsFromSelection();
  lastAppliedTempoRegionIndex = -1;
  updateActiveTempoRegionFromTransport();
  if (onTempoMapChanged)
    onTempoMapChanged(tempoMap);
  repaint();
}

void WaveformBar::updateActiveTempoRegionFromTransport() {
  if (tempoMap.regions.empty() || onActiveTempoRegionChanged == nullptr)
    return;
  const double pos = positionProvider != nullptr ? positionProvider() : 0.0;
  int active = 0;
  for (int i = 0; i < static_cast<int>(tempoMap.regions.size()); ++i) {
    const auto &region = tempoMap.regions[static_cast<size_t>(i)];
    if (pos >= region.startSec && pos < region.endSec) {
      active = i;
      break;
    }
  }
  if (active == lastAppliedTempoRegionIndex)
    return;
  lastAppliedTempoRegionIndex = active;
  onActiveTempoRegionChanged(tempoMap.regions[static_cast<size_t>(active)]);
}
