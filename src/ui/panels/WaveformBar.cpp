#include "WaveformBar.h"
#include "TimelineRulerStyle.h"
#include "../../PluginProcessor.h"
#include "../../audio/DynamicGuideEngine.h"
#include "../../data/ArrangementSourceMapping.h"
#include "../../data/Localization.h"
#include "../WaveformCacheWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace {
constexpr double kMinPartDurationSec = 0.25;
constexpr double kPartSnapSeconds = 0.1;
constexpr float kMinViewportSpan = 1.0f / 18.0f;
constexpr float kEditToolbarHeight = 24.0f;
constexpr float kEditToolbarBottomGap = 6.0f;
constexpr float kSeekRulerHeight = 24.0f;
constexpr float kSeekRulerGap = 4.0f;
constexpr float kMiniMapHeight = 24.0f;
constexpr float kMiniMapGap = 10.0f;
constexpr float kBottomSafetyInset = 8.0f;
constexpr float kPartHandleHitWidth = 7.0f;
constexpr float kViewportHandleHitWidth = 9.0f;
constexpr int kSnapModeBarId = 1;
constexpr int kSnapModeBeatId = 2;
constexpr int kSnapModeHalfBeatId = 3;
constexpr int kSnapModeFreeId = 4;
constexpr double kSongLoadingMinVisibleMs = 180.0;
constexpr double kFollowSuspendAfterManualViewportMs = 2500.0;
constexpr float kFollowEnabledViewportSpan = 0.25f;
constexpr float kFollowCenterRatio = 0.5f;
constexpr double kFollowCameraSmoothingTimeSec = 0.065;
#if JUCE_ANDROID
constexpr float kFollowWaveformCacheOverscan = 2.0f;
#else
constexpr float kFollowWaveformCacheOverscan = 3.0f;
#endif
constexpr double kFollowViewportPositionEpsilon = 1.0e-7;
constexpr float kWheelPanSensitivity = 0.22f;
constexpr float kTouchPanSlopPx = 6.0f;
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
  if (canonical == "Refrain")
    return "Ref";
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
      std::function<void(juce::String)> onSelectSectionCallback,
      std::function<void(int)> onSelectCountCallback,
      std::function<void(juce::String)> onSelectUtilityCallback)
      : onSelectSection(std::move(onSelectSectionCallback)),
        onSelectCount(std::move(onSelectCountCallback)),
        onSelectUtility(std::move(onSelectUtilityCallback)) {
    title.setText(L10n::tr("Agregar sección", "Add section"),
                  juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setColour(juce::Label::textColourId, UITheme::textSecondary);
    title.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(title);

    addGroupLabel(L10n::tr("Secciones", "Sections"));
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
        sectionChips.push_back(std::move(chip));
      }
    }

    addGroupLabel(L10n::tr("Conteos", "Counts"));
    for (int value = 1; value <= 7; ++value) {
      auto chip = std::make_unique<CountChip>(value, countWordLabel(value));
      chip->setTooltip(L10n::tr("Agregar cuenta ", "Add count ") +
                       countWordLabel(value));
      chip->onClick = [this, value] {
        if (onSelectCount != nullptr)
          onSelectCount(value);
      };
      addAndMakeVisible(*chip);
      countChips.push_back(std::move(chip));
    }

    addGroupLabel(L10n::tr("Utilidades", "Utilities"));
    for (const auto &cue : kUtilityCueSuggestions) {
      const auto label = L10n::tr(cue.spanishLabel, cue.englishLabel);
      auto chip = std::make_unique<CountChip>(
          static_cast<int>(utilityChips.size()), label);
      chip->setTooltip(L10n::tr("Agregar utilidad ", "Add utility ") + label);
      chip->onClick = [this, key = juce::String(cue.key)] {
        if (onSelectUtility != nullptr)
          onSelectUtility(key);
      };
      addAndMakeVisible(*chip);
      utilityChips.push_back(std::move(chip));
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

    layoutGroup(area, groupLabels[0].get(), sectionChips, 3, 92);
    layoutGroup(area, groupLabels[1].get(), countChips, 4, 52);
    layoutGroup(area, groupLabels[2].get(), utilityChips, 2, 118);
  }

  int getPreferredHeight() const {
    int chipCount = 0;
    for (const auto *section : kSongSectionSuggestions) {
      const juce::String sectionName =
          section != nullptr ? juce::String(section) : juce::String("Part");
      chipCount += juce::jmax(1, sectionVariantLimitForName(sectionName));
    }
    const int sectionRows = (chipCount + 2) / 3;
    const int countRows = (7 + 3) / 4;
    const int utilityRows =
        (static_cast<int>(kUtilityCueSuggestions.size()) + 1) / 2;
    return 54 + (sectionRows * 28) + (juce::jmax(0, sectionRows - 1) * 6) +
           (countRows * 28) + (juce::jmax(0, countRows - 1) * 6) +
           (utilityRows * 28) + (juce::jmax(0, utilityRows - 1) * 6) +
           (3 * 22);
  }

private:
  void addGroupLabel(const juce::String &text) {
    auto label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);
    label->setColour(juce::Label::textColourId,
                     UITheme::textSecondary.withAlpha(0.88f));
    label->setFont(juce::FontOptions(10.0f));
    label->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*label);
    groupLabels.push_back(std::move(label));
  }

  template <typename ChipType>
  void layoutGroup(juce::Rectangle<int> &area, juce::Label *groupLabel,
                   const std::vector<std::unique_ptr<ChipType>> &group,
                   const int columns, const int minimumWidth) {
    if (groupLabel == nullptr || group.empty())
      return;
    groupLabel->setBounds(area.removeFromTop(16));
    area.removeFromTop(4);
    constexpr int gap = 6;
    constexpr int chipH = 28;
    const int totalGap = gap * (columns - 1);
    const int chipW = juce::jmax(minimumWidth,
                                 (area.getWidth() - totalGap) / columns);
    const int rows = (static_cast<int>(group.size()) + columns - 1) / columns;
    for (size_t i = 0; i < group.size(); ++i) {
      const int index = static_cast<int>(i);
      const int row = index / columns;
      const int column = index % columns;
      group[i]->setBounds(area.getX() + column * (chipW + gap),
                          area.getY() + row * (chipH + gap), chipW, chipH);
    }
    area.removeFromTop(rows * chipH + juce::jmax(0, rows - 1) * gap);
    area.removeFromTop(8);
  }

  juce::Label title;
  std::vector<std::unique_ptr<juce::Label>> groupLabels;
  std::vector<std::unique_ptr<CountChip>> sectionChips;
  std::vector<std::unique_ptr<CountChip>> countChips;
  std::vector<std::unique_ptr<CountChip>> utilityChips;
  std::function<void(juce::String)> onSelectSection;
  std::function<void(int)> onSelectCount;
  std::function<void(juce::String)> onSelectUtility;
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

class ExportFormatButton final : public juce::Button {
public:
  ExportFormatButton(juce::String titleToUse, juce::String detailToUse,
                     juce::String formatToUse,
                     std::function<void(juce::String)> onSelectCallback)
      : juce::Button(titleToUse), title(std::move(titleToUse)),
        detail(std::move(detailToUse)), format(std::move(formatToUse)),
        onSelect(std::move(onSelectCallback)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [this] {
      const auto callback = onSelect;
      const auto selectedFormat = format;
      dismissActiveActionPopover();
      if (callback)
        callback(selectedFormat);
    };
  }

  void paintButton(juce::Graphics &g, const bool isMouseOverButton,
                   const bool isButtonDown) override {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = UITheme::cardBgHover;
    if (isButtonDown)
      fill = fill.darker(0.06f);
    else if (isMouseOverButton)
      fill = fill.brighter(0.05f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour((isMouseOverButton ? UITheme::accentSoft : UITheme::cardBorder)
                    .withAlpha(isMouseOverButton ? 0.82f : 0.64f));
    g.drawRoundedRectangle(bounds, 9.0f, 1.0f);

    auto content = getLocalBounds().reduced(12, 6);
    auto iconArea = content.removeFromLeft(30).toFloat().reduced(4.0f);
    drawSvgIcon(g, iconArea, SvgIconName::FileAudio,
                isMouseOverButton ? UITheme::accentSoft
                                  : UITheme::textPrimary.withAlpha(0.92f));
    content.removeFromLeft(8);
    auto titleArea = content.removeFromTop(19);
    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
    g.drawFittedText(title, titleArea, juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textSecondary.withAlpha(0.92f));
    g.setFont(makeAppFont(10.5f, AppFontWeight::Regular));
    g.drawFittedText(detail, content, juce::Justification::centredLeft, 1);
  }

private:
  juce::String title;
  juce::String detail;
  juce::String format;
  std::function<void(juce::String)> onSelect;
};

class ExportFormatPopover final : public juce::Component {
public:
  explicit ExportFormatPopover(std::function<void(juce::String)> onSelect)
      : wavButton("WAV (24 bits)",
                  L10n::tr("Máxima calidad", "Maximum quality"), "wav",
                  onSelect),
        mp3Button("MP3 (320 kbps)",
                  L10n::tr("Compatible y compacto", "Compatible and compact"),
                  "mp3", onSelect),
        m4aButton("M4A / AAC (256 kbps)",
                  L10n::tr("Optimizado para Apple", "Optimized for Apple"),
                  "m4a", std::move(onSelect)) {
    title.setText(L10n::tr("Formato de exportación", "Export format"),
                  juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, UITheme::textPrimary);
    title.setFont(makeAppFont(15.0f, AppFontWeight::SemiBold));
    title.setJustificationType(juce::Justification::centredLeft);
    title.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(title);
    addAndMakeVisible(wavButton);
#if !JUCE_IOS && !JUCE_ANDROID
    addAndMakeVisible(mp3Button);
    addAndMakeVisible(m4aButton);
#endif
  }

  void paint(juce::Graphics &g) override {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.76f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(12, 10);
    title.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    constexpr int buttonHeight = 46;
#if JUCE_IOS || JUCE_ANDROID
    wavButton.setBounds(area.removeFromTop(buttonHeight));
#else
    constexpr int gap = 7;
    wavButton.setBounds(area.removeFromTop(buttonHeight));
    area.removeFromTop(gap);
    mp3Button.setBounds(area.removeFromTop(buttonHeight));
    area.removeFromTop(gap);
    m4aButton.setBounds(area.removeFromTop(buttonHeight));
#endif
  }

private:
  juce::Label title;
  ExportFormatButton wavButton;
  ExportFormatButton mp3Button;
  ExportFormatButton m4aButton;
};

class WaveformActionButton final : public juce::Button {
public:
  WaveformActionButton(juce::String labelToUse, const SvgIconName iconToUse,
                       const bool enabled, const bool dismissOnTrigger,
                       std::function<void()> callback)
      : juce::Button(labelToUse), label(std::move(labelToUse)), icon(iconToUse),
        dismissAfterClick(dismissOnTrigger), onSelect(std::move(callback)) {
    setEnabled(enabled);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [this] {
      const auto callbackCopy = onSelect;
      if (dismissAfterClick)
        dismissActiveActionPopover();
      juce::MessageManager::callAsync([callbackCopy] {
        if (callbackCopy)
          callbackCopy();
      });
    };
  }

  void paintButton(juce::Graphics &g, const bool isMouseOverButton,
                   const bool isButtonDown) override {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = UITheme::cardBgHover;
    if (isButtonDown)
      fill = fill.darker(0.06f);
    else if (isMouseOverButton)
      fill = fill.brighter(0.05f);
    if (!isEnabled())
      fill = fill.withAlpha(0.55f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(UITheme::cardBorder.withAlpha(isEnabled() ? 0.64f : 0.36f));
    g.drawRoundedRectangle(bounds, 9.0f, 1.0f);

    auto content = getLocalBounds().reduced(12, 5);
    const auto colour = isEnabled() ? UITheme::textPrimary
                                    : UITheme::textSecondary.withAlpha(0.7f);
    const auto iconBounds =
        content.removeFromLeft(24).toFloat().withSizeKeepingCentre(17.0f, 17.0f);
    drawSvgIcon(g, iconBounds, icon, colour);
    content.removeFromLeft(8);
    g.setColour(colour);
    g.setFont(makeAppFont(13.5f, AppFontWeight::SemiBold));
    g.drawFittedText(label, content, juce::Justification::centredLeft, 1);
  }

private:
  juce::String label;
  SvgIconName icon;
  bool dismissAfterClick = true;
  std::function<void()> onSelect;
};

class WaveformActionsPopover final : public juce::Component {
public:
  void addAction(juce::String label, const SvgIconName icon, const bool enabled,
                 const bool dismissOnTrigger, std::function<void()> callback) {
    auto button = std::make_unique<WaveformActionButton>(
        std::move(label), icon, enabled, dismissOnTrigger, std::move(callback));
    addAndMakeVisible(*button);
    actions.push_back(std::move(button));
  }

  int preferredHeight() const {
    constexpr int inset = 8;
    constexpr int rowHeight = 42;
    constexpr int gap = 6;
    return inset * 2 + static_cast<int>(actions.size()) * rowHeight +
           juce::jmax(0, static_cast<int>(actions.size()) - 1) * gap;
  }

  void paint(juce::Graphics &g) override {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.76f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(8);
    constexpr int rowHeight = 42;
    constexpr int gap = 6;
    for (auto &action : actions) {
      action->setBounds(area.removeFromTop(rowHeight));
      area.removeFromTop(gap);
    }
  }

private:
  std::vector<std::unique_ptr<WaveformActionButton>> actions;
};
} // namespace

SvgToolbarButton::SvgToolbarButton(const juce::String &name,
                                   const SvgIconName icon)
    : juce::TextButton(name), iconName(icon) {
  setButtonText({});
}

void SvgToolbarButton::setIcon(const SvgIconName nextIcon) {
  iconName = nextIcon;
  repaint();
}

void SvgToolbarButton::paintButton(juce::Graphics &g,
                                   const bool isMouseOverButton,
                                   const bool isButtonDown) {
  auto b = getLocalBounds().toFloat().reduced(0.5f);
  const bool active = isButtonDown || getToggleState();
  auto fill = UITheme::cardBgHover;
  if (active)
    fill = UITheme::accent.withAlpha(0.88f);
  else if (isMouseOverButton)
    fill = fill.brighter(0.10f);

  g.setColour(juce::Colours::black.withAlpha(active ? 0.24f : 0.16f));
  g.fillRoundedRectangle(b.translated(0.0f, 1.0f), 7.0f);
  g.setColour(fill);
  g.fillRoundedRectangle(b, 7.0f);
  g.setColour((active ? UITheme::accentSoft : UITheme::cardBorder)
                  .withAlpha(isMouseOverButton ? 0.92f : 0.76f));
  g.drawRoundedRectangle(b, 7.0f, 1.0f);

  const auto iconColour = active ? UITheme::onAccent : UITheme::textPrimary;
  drawSvgIcon(g, b.withSizeKeepingCentre(18.0f, 18.0f), iconName, iconColour);
}

WaveformBar::WaveformBar() {
#if JUCE_ANDROID
  // The Android peer is software-painted before upload. This component covers
  // every pixel in its bounds, so marking it opaque prevents JUCE from also
  // repainting the editor background behind every transport frame.
  setOpaque(true);
#endif
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(true);

  followButton.setClickingTogglesState(true);
  followButton.setToggleState(followPlaybackEnabled, juce::dontSendNotification);
  followButton.setTooltip(
      L10n::tr("Auto centra la vista en la reproducción", "Auto-follow playback"));
  followButton.setViewportIgnoreDragFlag(true);
  followButton.onClick = [this] {
    followPlaybackEnabled = followButton.getToggleState();
    followLastFrameTimestampSec = 0.0;
    if (followPlaybackEnabled) {
      followPendingJumpWasActive = false;
      followLastSongProgressValid = false;
      followSuspendedUntilMs = 0.0;
      float targetRatio = 0.0f;
      bool hasTargetRatio = getPendingJumpProgressRatio(targetRatio);
      if (!hasTargetRatio && positionProvider != nullptr && lengthProvider != nullptr) {
        const double length = juce::jmax(0.0, lengthProvider());
        if (length > 0.0) {
          const double pos = juce::jmax(0.0, positionProvider());
          targetRatio = static_cast<float>(juce::jlimit(0.0, 1.0, pos / length));
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

  moreButton.setTooltip(L10n::tr("Más acciones", "More actions"));
  moreButton.setViewportIgnoreDragFlag(true);
  moreButton.onClick = [this] {
    const auto state = songActionsStateProvider != nullptr
                           ? songActionsStateProvider()
                           : WaveformSongActionsState{};
    const auto safeBar = juce::Component::SafePointer<WaveformBar>(this);
    const auto screenAnchor =
        moreButton.localAreaToGlobal(moreButton.getLocalBounds());
    auto panel = std::make_unique<WaveformActionsPopover>();
    panel->addAction(
        L10n::tr("Exportar canción", "Export song"), SvgIconName::FileAudio,
        !exportBusy && onExportMixRequested != nullptr, true, [safeBar] {
          if (safeBar != nullptr)
            safeBar->showExportFormatPopover();
        });
    if (state.cloudLinked && onCloudSettingsVersionsRequested != nullptr) {
      panel->addAction(
          L10n::tr("Ver versiones", "View versions"), SvgIconName::Clock, true,
          false, [safeBar, screenAnchor] {
            if (safeBar != nullptr &&
                safeBar->onCloudSettingsVersionsRequested != nullptr)
              safeBar->onCloudSettingsVersionsRequested(screenAnchor);
          });
    }
    if (state.cloudLinked && state.settingsPending &&
        onCloudSettingsSyncRequested != nullptr) {
      panel->addAction(
          L10n::tr("Guardar versión", "Save version"), SvgIconName::Check,
          !state.settingsActionBusy, true, [safeBar] {
            if (safeBar != nullptr &&
                safeBar->onCloudSettingsSyncRequested != nullptr)
              safeBar->onCloudSettingsSyncRequested();
          });
    }
    panel->setSize(250, panel->preferredHeight());

    auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
    juce::Component *parentComp = editor != nullptr
                                      ? static_cast<juce::Component *>(editor)
                                      : static_cast<juce::Component *>(this);
    const auto anchor =
        parentComp->getLocalArea(&moreButton, moreButton.getLocalBounds());
    ActionPopoverOptions options;
    options.compactMaximumWidth = 320;
    presentActionPopover(std::move(panel), anchor, parentComp, options);
  };
  addAndMakeVisible(moreButton);

  editButton.setTooltip(
      L10n::tr(
          "Arrastra para ajustar; usa el selector para compas/beat/libre (Option/Alt = libre temporal)",
          "Drag snaps to grid; use the selector for bar/beat/free (hold Option/Alt for temporary free move)"));
  editButton.setClickingTogglesState(true);
  editButton.setViewportIgnoreDragFlag(true);
  editButton.onClick = [this] {
    const bool editing = editButton.getToggleState();
    invalidateWaveformRenderCaches();
    timelineViewMode = TimelineViewMode::GuideMap;
    if (!editing) {
      snapMode = SnapMode::Bar;
      snapModeBox.setSelectedId(kSnapModeBarId, juce::sendNotificationSync);
    }
    // Cuando el alto del panel no cambia entre modos, resized() no siempre se
    // dispara desde el padre. Forzamos layout local para mostrar/ocultar
    // correctamente los botones de partes.
    resized();
    if (editing && viewportSpan >= 0.999f)
      setVisibleWindow(0.0f, 1.0f);
    if (!editing)
      clearSectionInsertionAnchor();
    if (onEditToggle)
      onEditToggle(editing);
    repaint();
  };
  addAndMakeVisible(editButton);

  waveHeightButton.setClickingTogglesState(true);
  waveHeightButton.setTooltip(
      L10n::tr("Expandir altura de la onda", "Expand waveform height"));
  waveHeightButton.setViewportIgnoreDragFlag(true);
  waveHeightButton.onClick = [this] {
    waveformVerticalExpanded = waveHeightButton.getToggleState();
    if (onWaveformHeightToggle)
      onWaveformHeightToggle(waveformVerticalExpanded);
    resized();
    repaint();
  };
  addAndMakeVisible(waveHeightButton);

  auto configureViewButton = [this](juce::TextButton &button,
                                    const juce::String &text,
                                    TimelineViewMode mode) {
    button.setButtonText({});
    button.setTooltip(text);
    button.setClickingTogglesState(true);
    button.setViewportIgnoreDragFlag(true);
    button.onClick = [this, mode] {
      timelineViewMode = mode;
      if (timelineViewMode != TimelineViewMode::GuideMap)
        clearSectionInsertionAnchor();
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
  sectionMenuButton.setTooltip(L10n::tr(
      "Agregar en el punto marcado de la regla o, sin marca, al final",
      "Add at the ruler marker or, without a marker, at the end"));
  sectionMenuButton.setViewportIgnoreDragFlag(true);
  sectionMenuButton.onClick = [this] {
    openSectionPicker(sectionMenuButton.getBounds().toFloat(),
                      contextualSectionInsertPartIndex);
  };
  addAndMakeVisible(sectionMenuButton);
  sectionMenuButton.setButtonText("+");
  sectionMenuButton.setTooltip(L10n::tr("Agregar sección", "Add section"));
  sectionMenuButton.setColour(juce::TextButton::buttonColourId,
                              UITheme::panelDark.withAlpha(0.55f));
  sectionMenuButton.setColour(juce::TextButton::buttonOnColourId,
                              UITheme::accent.withAlpha(0.8f));

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
#if ! JUCE_ANDROID
  playbackVBlankAttachment = juce::VBlankAttachment(
      this, [this](const double frameTimestampSec) {
        updatePlaybackAnimation(frameTimestampSec);
      });
#endif
#if JUCE_ANDROID
  // A stable 30 Hz leaves enough render time for the waveform and 15 meters
  // on tablet GPUs. Requesting 60 Hz here can cause missed frames and visible
  // multi-frame jumps under load.
  startTimerHz(30);
#else
  startTimerHz(60);
#endif
}

void WaveformBar::resetPlaybackViewForSong() {
  followPendingJumpWasActive = false;
  followLastSongProgressValid = false;
  followLastFrameTimestampSec = 0.0;
  followSuspendedUntilMs = 0.0;
  lastTimerSongProgressValid = false;
  phonePanInertiaActive = false;
  phonePanVelocity = 0.0f;
#if JUCE_ANDROID
  androidVisualProgressValid = false;
  androidVisualProgressTimestampSec = 0.0;
  androidLastFixedPlayheadX = -1;
#endif
  const float span = followPlaybackEnabled ? kFollowEnabledViewportSpan : 1.0f;
  setVisibleWindow(0.0f, span);
  repaint();
}

void WaveformBar::setWaveformBins(std::vector<float> bins) {
  const bool wasEditing = editButton.getToggleState();
  waveformBins = std::move(bins);
  invalidateWaveformRenderCaches();
  const bool hasLoadedSong = !waveformBins.empty();

  if (!hasLoadedSong && wasEditing) {
    snapMode = SnapMode::Bar;
    snapModeBox.setSelectedId(kSnapModeBarId, juce::sendNotificationSync);
    editButton.setToggleState(false, juce::dontSendNotification);
    if (onEditToggle)
      onEditToggle(false);
  }

  editButton.setVisible(hasLoadedSong && !minimalPhoneMode);
  editButton.setEnabled(hasLoadedSong && !minimalPhoneMode);
  if (!hasLoadedSong) {
    cancelDeferredPartJumpFromDoubleClick();
    clearSectionInsertionAnchor();
  }
  resized();
  repaint();
}

void WaveformBar::setArrangement(
    const SongArrangementSnapshot &arrangementSnapshot,
    const bool isCustomArrangement) {
  arrangement = arrangementSnapshot;
  invalidateWaveformRenderCaches();
  customArrangement = isCustomArrangement;
  selectedPartIndex = -1;
  selectedPartIds.clear();
  arrangementUndoStack.clear();
  arrangementRedoStack.clear();
  arrangementClipboard.clear();
  hasArrangementDragUndoSnapshot = false;
  normalizeArrangement(true, false);

  dragMode = DragMode::none;
  activeDragPartId.clear();
  dragPartOrigins.clear();
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  clearSectionInsertionAnchor();
  hoveredActionPartIndex = -1;
  hoveredLoopPartIndex = -1;
  hoveredJumpPartIndex = -1;
  hoveredMutePartIndex = -1;
  hoveredSkipPartIndex = -1;
  hoveredDuplicatePartIndex = -1;
  hoveredAddPartIndex = -1;
  hoveredDeletePartIndex = -1;
  cancelDeferredPartJumpFromDoubleClick();
  resized();
  repaint();
  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  juce::MessageManager::callAsync([safe] {
    if (safe != nullptr) {
      safe->resized();
      safe->repaint();
    }
  });
}

void WaveformBar::setTempoMap(const SongTempoMapSnapshot &tempoMapSnapshot) {
  tempoMap = tempoMapSnapshot;
  invalidateWaveformRenderCaches();
  selectedTempoRegionIndex = tempoMap.regions.empty() ? -1 : 0;
  lastAppliedTempoRegionIndex = -1;
  normalizeTempoMap();
  updateTempoControlsFromSelection();
  repaint();
}

juce::Range<double> WaveformBar::getArrangementPartTimeRange(const int index) const {
  if (!juce::isPositiveAndBelow(index, static_cast<int>(arrangement.parts.size())))
    return {};
  const auto &part = arrangement.parts[static_cast<size_t>(index)];
  return {part.startSec, part.endSec};
}

std::vector<PracticeTimelineMark> WaveformBar::getPracticeTimelineMarks(
    const juce::Range<double> visibleRange, const float width) const {
  const double length = getSongLengthSeconds();
  const auto playable = visibleRange.getIntersectionWith({0.0, length});
  if (playable.isEmpty())
    return {};
  // Keep the original width/span ratio when the centered viewport extends
  // beyond the song. The caller draws returned source times in that viewport.
  const float playableWidth = width * static_cast<float>(
      playable.getLength() / juce::jmax(0.000001, visibleRange.getLength()));
  return PracticeTimelineRuler::build(playable, playableWidth, songBpm,
                                     songTimeSignature, tempoMap);
}

double WaveformBar::getPracticeSnappedPosition(
    const double position, const juce::Range<double> allowed) const {
  const double length = getSongLengthSeconds();
  if (!std::isfinite(length) || length <= 0.0 ||
      !std::isfinite(allowed.getStart()) || !std::isfinite(allowed.getEnd()) ||
      allowed.getEnd() < 0.0 || allowed.getStart() > length)
    return std::numeric_limits<double>::quiet_NaN();
  const juce::Range<double> playable{juce::jmax(0.0, allowed.getStart()),
                                     juce::jmin(length, allowed.getEnd())};
  return PracticeTimelineRuler::snapToPulse(position, songBpm,
                                           songTimeSignature, tempoMap, playable);
}

void WaveformBar::setSongRhythm(const double bpm,
                                const juce::String &timeSignature) {
  songBpm = juce::jmax(0.0, bpm);
  songTimeSignature = timeSignature.trim();
  if (songTimeSignature.isEmpty())
    songTimeSignature = "4/4";
  songBeatsPerBar = timeSignatureBeatsPerBar(songTimeSignature);
  invalidateWaveformRenderCaches();
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

void WaveformBar::setSongSourceLengthProvider(LengthProvider lengthCb) {
  sourceLengthProvider = std::move(lengthCb);
  invalidateWaveformRenderCaches();
}

void WaveformBar::setSongEndCustomizedProvider(
    std::function<bool()> provider) {
  songEndCustomizedProvider = std::move(provider);
  invalidateWaveformRenderCaches();
  repaint();
}

void WaveformBar::setSongWindowProviders(SongWindowProvider startCb,
                                         SongWindowProvider endCb) {
  songWindowStartProvider = std::move(startCb);
  songWindowEndProvider = std::move(endCb);
  invalidateWaveformRenderCaches();
}

void WaveformBar::setSeekCallback(SeekCallback cb) { onSeekTo = std::move(cb); }

void WaveformBar::setOnEditToggle(EditToggleCallback cb) {
  onEditToggle = std::move(cb);
}

void WaveformBar::setOnWaveformHeightToggle(WaveformHeightToggleCallback cb) {
  onWaveformHeightToggle = std::move(cb);
}

void WaveformBar::setMinimalPhoneMode(const bool enabled) {
  if (minimalPhoneMode != enabled) {
    phoneTouchPositions.clear();
    phonePinchActive = false;
    viewportDragMoved = false;
    if (dragMode == DragMode::panViewport)
      dragMode = DragMode::none;
  }
  minimalPhoneMode = enabled;
  if (enabled) {
    waveformVerticalExpanded = false;
    if (isEditingArrangement()) {
      editButton.setToggleState(false, juce::dontSendNotification);
      timelineViewMode = TimelineViewMode::GuideMap;
      clearSectionInsertionAnchor();
      if (onEditToggle)
        onEditToggle(false);
    }
  }
  waveHeightButton.setVisible(!enabled);
  waveHeightButton.setEnabled(!enabled);
  editButton.setVisible(!enabled && !waveformBins.empty());
  editButton.setEnabled(!enabled && !waveformBins.empty());
  resized();
}

void WaveformBar::setPhoneSafeAreaInsets(const juce::BorderSize<int> insets) {
  if (phoneSafeAreaInsets == insets)
    return;
  phoneSafeAreaInsets = insets;
  resized();
  repaint();
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

void WaveformBar::setOnArrangementPendingJumpClearRequest(
    ArrangementPendingJumpClearCallback cb) {
  onArrangementPendingJumpClearRequest = std::move(cb);
}

void WaveformBar::setOnTempoMapChanged(TempoMapChangedCallback cb) {
  onTempoMapChanged = std::move(cb);
}

void WaveformBar::setOnActiveTempoRegionChanged(ActiveTempoRegionCallback cb) {
  onActiveTempoRegionChanged = std::move(cb);
}

void WaveformBar::setOnExportMixRequested(
    std::function<void(juce::String)> cb) {
  onExportMixRequested = std::move(cb);
}

void WaveformBar::setSongActionsStateProvider(
    std::function<WaveformSongActionsState()> provider) {
  songActionsStateProvider = std::move(provider);
}

void WaveformBar::setOnCloudSettingsSyncRequested(std::function<void()> cb) {
  onCloudSettingsSyncRequested = std::move(cb);
}

void WaveformBar::setOnCloudSettingsVersionsRequested(
    std::function<void(juce::Rectangle<int>)> cb) {
  onCloudSettingsVersionsRequested = std::move(cb);
}

void WaveformBar::setExportBusy(const bool busy) {
  exportBusy = busy;
  moreButton.repaint();
}

void WaveformBar::showExportFormatPopover() {
  if (exportBusy || !onExportMixRequested)
    return;
  const juce::Component::SafePointer<WaveformBar> safeBar(this);
  auto panel = std::make_unique<ExportFormatPopover>(
      [safeBar](const juce::String &format) {
        if (safeBar != nullptr && safeBar->onExportMixRequested)
          safeBar->onExportMixRequested(format);
      });
#if JUCE_IOS || JUCE_ANDROID
  panel->setSize(330, 88);
#else
  panel->setSize(330, 202);
#endif

  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor =
      parentComp->getLocalArea(&moreButton, moreButton.getLocalBounds());
  ActionPopoverOptions options;
  options.compactMaximumWidth = 380;
  presentActionPopover(std::move(panel), anchor, parentComp, options);
}

void WaveformBar::setPendingArrangementPartProvider(
    PendingArrangementPartProvider cb) {
  pendingArrangementPartProvider = std::move(cb);
}

void WaveformBar::setActiveArrangementLoopPartProvider(
    ActiveArrangementLoopPartProvider cb) {
  activeArrangementLoopPartProvider = std::move(cb);
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

void WaveformBar::openSectionPicker(const juce::Rectangle<float> &anchor,
                                    const int insertionPartIndex) {
  const auto playableRange = getPlayableRangeSeconds();
  if (playableRange.second - playableRange.first < kMinPartDurationSec)
    return;

  if (juce::isPositiveAndBelow(
          insertionPartIndex, static_cast<int>(arrangement.parts.size()))) {
    sectionInsertionAnchorSec =
        arrangement.parts[static_cast<size_t>(insertionPartIndex)].endSec;
    sectionInsertionAnchorValid = true;
  } else if (arrangement.parts.empty() && !sectionInsertionAnchorValid) {
    sectionInsertionAnchorSec =
        playableRange.first + ((playableRange.second - playableRange.first) * 0.5);
    sectionInsertionAnchorValid = true;
  }

  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto calloutAnchor = parentComp->getLocalArea(
      this, anchor.toNearestInt().getIntersection(getLocalBounds()));

  juce::PopupMenu menu;
  std::vector<std::pair<int, juce::String>> sectionItemMappings;
  int nextItemId = 1;
  for (const auto *section : kSongSectionSuggestions) {
    const juce::String sectionName =
        section != nullptr ? juce::String(section) : juce::String("Part");
    const int variants = juce::jmax(1, sectionVariantLimitForName(sectionName));
    juce::PopupMenu sectionVariantsMenu;
    for (int variant = 1; variant <= variants; ++variant) {
      const int itemId = nextItemId++;
      sectionItemMappings.emplace_back(
          itemId, sectionVariantName(sectionName, variant));
      if (variants > 1) {
        sectionVariantsMenu.addItem(
            itemId, sectionVariantDisplayLabel(sectionName, variant));
      } else {
        menu.addItem(itemId, sectionVariantDisplayLabel(sectionName, variant));
      }
    }
    if (variants > 1)
      menu.addSubMenu(arrangementPartDisplayLabelForName(sectionName),
                      sectionVariantsMenu);
  }

  juce::PopupMenu countsMenu;
  for (int count = 1; count <= 7; ++count)
    countsMenu.addItem(1000 + count, countWordLabel(count));
  menu.addSubMenu(L10n::tr("Conteos", "Counts"), countsMenu);

  juce::PopupMenu utilityMenu;
  std::vector<std::pair<int, juce::String>> utilityItemMappings;
  for (size_t i = 0; i < kUtilityCueSuggestions.size(); ++i) {
    const auto &cue = kUtilityCueSuggestions[i];
    const int itemId = 2000 + static_cast<int>(i);
    utilityItemMappings.emplace_back(itemId, juce::String(cue.key));
    utilityMenu.addItem(itemId,
                        L10n::tr(cue.spanishLabel, cue.englishLabel));
  }
  menu.addSubMenu(L10n::tr("Utilidades", "Utilities"), utilityMenu);

  const auto safe = juce::Component::SafePointer<WaveformBar>(this);
  auto options = makeTouchPopupMenuOptions(nullptr)
                     .withTargetScreenArea(parentComp->localAreaToGlobal(
                         calloutAnchor))
                     .withParentComponent(parentComp);
  menu.showMenuAsync(
      options, [safe, sectionItems = std::move(sectionItemMappings),
                utilityItems = std::move(utilityItemMappings)](const int result) {
        if (safe == nullptr)
          return;
        for (const auto &[itemId, sectionName] : sectionItems) {
          if (result == itemId) {
            safe->addArrangementPart(sectionName);
            return;
          }
        }
        if (result >= 1001 && result <= 1007) {
          safe->addCountSequenceParts(result - 1000, result - 1000);
          return;
        }
        for (const auto &[itemId, utilityName] : utilityItems) {
          if (result == itemId) {
            safe->addUtilityCuePart(utilityName);
            return;
          }
        }
      });
}

void WaveformBar::refreshThemeColours() {
  invalidateWaveformRenderCaches();
  followButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  followButton.setColour(juce::TextButton::buttonOnColourId,
                         UITheme::accent.withAlpha(0.95f));
  followButton.setColour(juce::TextButton::textColourOffId,
                         UITheme::textPrimary);
  followButton.setColour(juce::TextButton::textColourOnId,
                         UITheme::onAccent);
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
  applyComboBoxTheme(snapModeBox, UITheme::cardBgHover,
                     UITheme::cardBorder.withAlpha(0.75f));
  for (auto *label : {&tempoBpmLabel, &tempoSignatureLabel}) {
    label->setFont(makeAppFont(12.0f, AppFontWeight::SemiBold));
    label->setColour(juce::Label::textColourId, UITheme::textPrimary);
  }
  applyTextEditorTheme(tempoBpmEditor, UITheme::cardBgHover,
                       UITheme::cardBorder.withAlpha(0.75f));
  applyComboBoxTheme(tempoSignatureBox, UITheme::cardBgHover,
                     UITheme::cardBorder.withAlpha(0.75f));
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

void WaveformBar::setRemoteIndicator(const RemoteIndicator state) {
  if (remoteIndicator == state)
    return;
  remoteIndicator = state;
  repaint();
}

void WaveformBar::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  if (bounds.getWidth() < 2.0f)
    return;

#if JUCE_ANDROID
  // Avoid four full-surface gradient/noise passes for every playback frame.
  // The waveform and overlays retain their normal colours above this base.
  g.fillAll(UITheme::panelDark);
#else
  drawStudioBackground(g, bounds);
  {
    juce::ColourGradient base(UITheme::panelDark.withAlpha(0.42f),
                              bounds.getCentreX(), bounds.getY(),
                              UITheme::panelDark.withAlpha(0.65f),
                              bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(base);
    g.fillRect(bounds);
  }
#endif

  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);

  const auto safeOverlayBounds = minimalPhoneMode
      ? phoneSafeAreaInsets.subtractedFrom(getLocalBounds()).toFloat()
            .getIntersection(mainWaveBodyBounds)
      : mainWaveBodyBounds;

  // Status is painted directly on the waveform, so it does not alter layout
  // or take taps away from seeking and section controls.
  const auto paintRemoteIndicator = [&] {
    if (remoteIndicator == RemoteIndicator::Hidden ||
        safeOverlayBounds.getWidth() < 80.0f ||
        safeOverlayBounds.getHeight() < 24.0f)
      return;
    const bool connected = remoteIndicator == RemoteIndicator::Connected;
    const auto label = connected ? L10n::tr("REMOTO", "REMOTE")
        : remoteIndicator == RemoteIndicator::Searching
            ? L10n::tr("BUSCANDO", "SEARCHING")
            : remoteIndicator == RemoteIndicator::Connecting
                ? L10n::tr("CONECTANDO", "CONNECTING")
            : L10n::tr("RECONECTANDO", "RECONNECTING");
    const auto font = makeAppFont(minimalPhoneMode ? 10.0f : 10.5f,
                                  AppFontWeight::Bold);
    const float width = juce::jmin(safeOverlayBounds.getWidth() - 12.0f,
                                   juce::jmax(68.0f,
                                              font.getStringWidthFloat(label) + 29.0f));
    const juce::Rectangle<float> pill(safeOverlayBounds.getRight() - width - 6.0f,
                                       safeOverlayBounds.getY() + 5.0f,
                                       width, 20.0f);
    const auto statusColour = connected ? juce::Colour(0xff62d995)
                                        : juce::Colour(0xffffbe63);
    g.setColour(UITheme::panelDark.withAlpha(0.92f));
    g.fillRoundedRectangle(pill, 10.0f);
    g.setColour(statusColour.withAlpha(0.78f));
    g.drawRoundedRectangle(pill, 10.0f, 1.0f);
    g.setColour(statusColour);
    g.fillEllipse(pill.getX() + 8.0f, pill.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(0.96f));
    g.setFont(font);
    g.drawFittedText(label,
                     pill.withTrimmedLeft(19.0f).withTrimmedRight(6.0f).toNearestInt(),
                     juce::Justification::centredLeft, 1);
  };

  if (mainWaveBodyBounds.getWidth() < 2.0f ||
      mainWaveBodyBounds.getHeight() < 2.0f)
    return;

  if (waveformBins.empty()) {
    const bool isShowingLoadingState =
        songLoadingActive || songLoadingHideAfterMs > 0.0;
    g.setColour(isShowingLoadingState ? juce::Colours::white
                                      : UITheme::cardBg.withAlpha(0.55f));
    g.setFont(makeAppFont(15.5f, AppFontWeight::SemiBold));
    juce::String emptyText =
        L10n::tr(u8"Doble click en una canción para cargar waveform",
                 "Double-click a song to load stems waveform");
    if (isShowingLoadingState) {
      const int percent = static_cast<int>(
          std::round(juce::jlimit(0.0, 1.0, songLoadingProgress01) * 100.0));
      emptyText = L10n::tr(u8"Cargando canción...", "Loading song...") + " " +
                  juce::String(percent) + "%";
    }
    g.drawFittedText(emptyText,
                     safeOverlayBounds.toNearestInt().reduced(18),
                     juce::Justification::centred, 2);
    if (miniMapBounds.getHeight() > 0.0f) {
      g.setColour(UITheme::cardBg.withAlpha(0.45f));
      g.fillRoundedRectangle(miniMapBounds, 4.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.7f));
      g.drawRoundedRectangle(miniMapBounds, 4.0f, 1.0f);
    }
  g.setColour(UITheme::accent);
  g.fillRect(0.0f, 0.0f, 2.0f, bounds.getHeight());
  paintRemoteIndicator();
  return;
}

  const auto window = getVisibleWindow();
  drawWaveform(g, mainWaveBodyBounds, window.first, window.second,
               isEditingArrangement(), true);
  if (timelineViewMode == TimelineViewMode::TempoMap)
    drawTempoOverlay(g, mainWaveBodyBounds);
  else
    drawArrangementOverlay(g, mainWaveBodyBounds);

  // The arrangement overlay is painted after the waveform. Draw the playback
  // limit here so it remains visible over section cards and cannot be mistaken
  // for a gap in the audio source.
  drawPlaybackWindowGuides(g, mainWaveBodyBounds, window.first,
                           window.second);

  if (seekRulerBounds.getWidth() > 2.0f && seekRulerBounds.getHeight() > 2.0f)
    drawSeekRuler(g, seekRulerBounds, window.first, window.second);

  drawSectionInsertionAnchor(g, mainWaveBodyBounds, seekRulerBounds,
                             window.first, window.second);

  if (miniMapBounds.getWidth() > 2.0f && miniMapBounds.getHeight() > 2.0f)
    drawMiniMap(g, miniMapBounds);

  if (songLoadingActive || songLoadingHideAfterMs > 0.0) {
    const int percent = static_cast<int>(
        std::round(juce::jlimit(0.0, 1.0, songLoadingProgress01) * 100.0));
    const auto text = songLoadingPhaseText.isNotEmpty()
                          ? songLoadingPhaseText
                          : L10n::tr(u8"Procesando… ", "Processing… ") +
                                juce::String(percent) + "%";
    g.setFont(makeAppFont(11.5f, AppFontWeight::SemiBold));
    const float pillW = juce::jmin(mainWaveBodyBounds.getWidth() - 16.0f,
                                   juce::jmax(180.0f, g.getCurrentFont().getStringWidthFloat(text) + 30.0f));
    const auto pill = juce::Rectangle<float>(pillW, 26.0f)
                          .withCentre(mainWaveBodyBounds.getCentre());
    g.setColour(UITheme::panelDark.withAlpha(0.92f));
    g.fillRoundedRectangle(pill, 13.0f);
    g.setColour(UITheme::accent.withAlpha(0.9f));
    g.drawRoundedRectangle(pill, 13.0f, 1.0f);
    g.setColour(juce::Colours::white);
    g.drawFittedText(text, pill.toNearestInt().reduced(10, 2),
                     juce::Justification::centred, 1);
  }
  paintRemoteIndicator();
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
  invalidateWaveformRenderCaches();
  const auto localBounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(localBounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  auto toolbarBounds = getEditToolbarBounds(localBounds).toNearestInt();
  const int iconButtonW = 30;
  const int followGap = 6;
  const int snapModeGroupW = 186;
  const int snapModeLabelW = 56;
  const bool hasLoadedSong = !waveformBins.empty();

  if (!hasLoadedSong) {
    followButton.setVisible(false);
    followButton.setEnabled(false);
    moreButton.setVisible(false);
    moreButton.setEnabled(false);
    editButton.setVisible(false);
    editButton.setEnabled(false);
    waveHeightButton.setVisible(false);
    waveHeightButton.setEnabled(false);
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
  moreButton.setVisible(true);
  moreButton.setEnabled(true);
  editButton.setVisible(!minimalPhoneMode);
  editButton.setEnabled(!minimalPhoneMode);
  waveHeightButton.setVisible(!minimalPhoneMode);
  waveHeightButton.setEnabled(!minimalPhoneMode);
  waveHeightButton.setToggleState(waveformVerticalExpanded,
                                  juce::dontSendNotification);
  const bool editing = isEditingArrangement();

  auto placeBottomWaveControls = [&](const bool includeFollow,
                                     const bool includeViewButtons) {
    auto floating = mainWaveBodyBounds.toNearestInt();
    if (minimalPhoneMode)
      floating = floating.getIntersection(
          phoneSafeAreaInsets.subtractedFrom(getLocalBounds()));
    if (floating.getWidth() > 4 && floating.getHeight() > 4) {
      const int controlH = minimalPhoneMode ? 30 : 26;
      const int buttonGap = minimalPhoneMode ? 6 : 4;
      auto bottomControls = floating.removeFromBottom(controlH);
      bottomControls = bottomControls.reduced(2, 1);
      if (minimalPhoneMode)
        bottomControls.translate(0, -5);
      if (!minimalPhoneMode) {
        editButton.setBounds(bottomControls.removeFromRight(iconButtonW).reduced(1));
        bottomControls.removeFromRight(buttonGap);
      } else {
        editButton.setBounds({});
      }
      if (!minimalPhoneMode) {
        waveHeightButton.setBounds(
            bottomControls.removeFromRight(iconButtonW).reduced(1));
        bottomControls.removeFromRight(buttonGap);
      } else {
        waveHeightButton.setBounds({});
      }
      if (includeViewButtons) {
        tempoMapButton.setBounds(bottomControls.removeFromRight(iconButtonW).reduced(1));
        bottomControls.removeFromRight(buttonGap);
        guideMapButton.setBounds(bottomControls.removeFromRight(iconButtonW).reduced(1));
      }
      if (includeFollow) {
        followButton.setBounds(
            bottomControls.removeFromRight(iconButtonW).reduced(1));
        bottomControls.removeFromRight(buttonGap);
        moreButton.setBounds(
            bottomControls.removeFromRight(iconButtonW).reduced(1));
      }
      return true;
    }
    return false;
  };

  if (editing) {
    placeBottomWaveControls(false, true);
    followButton.setVisible(false);
    followButton.setEnabled(false);
    moreButton.setVisible(false);
    moreButton.setEnabled(false);
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

    contextualSectionInsertPartIndex = -1;
    juce::Rectangle<float> addBounds;
    if (sectionInsertionAnchorValid) {
      constexpr float addSize = 28.0f;
      const float guideX = sourceSecondsToWindowX(
          mainWaveBodyBounds, sectionInsertionAnchorSec);
      addBounds = juce::Rectangle<float>(addSize, addSize).withCentre(
          juce::Point<float>(guideX + (addSize * 0.5f),
                             mainWaveBodyBounds.getCentreY()));
    } else if (arrangement.parts.empty()) {
      addBounds = juce::Rectangle<float>(30.0f, 30.0f)
                      .withCentre(mainWaveBodyBounds.getCentre());
    }
    if (!addBounds.isEmpty()) {
      sectionMenuButton.setBounds(addBounds.toNearestInt());
      sectionMenuButton.setVisible(true);
      sectionMenuButton.setEnabled(true);
      sectionMenuButton.toFront(false);
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
    editButton.setBounds(toolbarBounds.removeFromRight(iconButtonW).reduced(1));
    toolbarBounds.removeFromRight(followGap);
    waveHeightButton.setBounds(toolbarBounds.removeFromRight(iconButtonW).reduced(1));
    toolbarBounds.removeFromRight(4);
    followButton.setBounds(toolbarBounds.removeFromRight(iconButtonW).reduced(1));
    toolbarBounds.removeFromRight(4);
    moreButton.setBounds(toolbarBounds.removeFromRight(iconButtonW).reduced(1));
  }
}

void WaveformBar::mouseDown(const juce::MouseEvent &event) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (minimalPhoneMode && !isEditingArrangement() && event.source.isTouch()) {
    const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
    const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
    if (mainWaveBodyBounds.contains(event.position)) {
      phoneTouchPositions[event.source.getIndex()] = event.position;
      if (phoneTouchPositions.size() >= 2) {
        auto first = phoneTouchPositions.begin();
        const auto firstPosition = first->second;
        const auto secondPosition = std::next(first)->second;
        const auto midpoint = (firstPosition + secondPosition) * 0.5f;
        phonePinchStartDistance = juce::jmax(
            1.0f, firstPosition.getDistanceFrom(secondPosition));
        phonePinchStartViewportSpan = viewportSpan;
        phonePinchAnchorRatio = juce::jlimit(
            0.0f, 1.0f,
            (midpoint.x - mainWaveBodyBounds.getX()) /
                juce::jmax(1.0f, mainWaveBodyBounds.getWidth()));
        phonePinchAnchorSource =
            viewportStart + (phonePinchAnchorRatio * phonePinchStartViewportSpan);
        phonePinchActive = true;
        viewportDragMoved = true;
        dragMode = DragMode::none;
        followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                                 kFollowSuspendAfterManualViewportMs;
        return;
      }
    }
  }

  dragMode = DragMode::none;
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

  // Phones do not expose the desktop minimap. Pan the visible waveform
  // directly with one finger instead, while preserving the arrange editor's
  // section manipulation gestures.
  if (minimalPhoneMode && !isEditingArrangement() &&
      mainWaveBodyBounds.contains(event.position) && !event.mods.isPopupMenu()) {
    followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                             kFollowSuspendAfterManualViewportMs;
    dragMode = DragMode::panViewport;
    dragViewportStart = viewportStart;
    dragViewportSpan = viewportSpan;
    dragAnchorNormalized = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - mainWaveBodyBounds.getX()) /
            juce::jmax(1.0f, mainWaveBodyBounds.getWidth()));
    phonePanInertiaActive = false;
    phonePanVelocity = 0.0f;
    phonePanLastNormalized = dragAnchorNormalized;
    phonePanLastTimestampMs = juce::Time::getMillisecondCounterHiRes();
    viewportDragStartPosition = event.position;
    viewportDragMoved = false;
    phoneTouchPositions[event.source.getIndex()] = event.position;
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    return;
  }

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
    const auto clearMuteForJumpSelection = [this](const int partIndex) {
      if (!juce::isPositiveAndBelow(partIndex,
                                    static_cast<int>(arrangement.parts.size())))
        return;

      auto &part = arrangement.parts[static_cast<size_t>(partIndex)];
      if (!part.muteInstruments)
        return;

      pushArrangementUndoSnapshot();
      part.muteInstruments = false;
      customArrangement = true;
      notifyArrangementChanged(true);
    };

    if (juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.loopButton) {
      setSingleSelectedPart(actionHit.partIndex);
      playbackControlFocusPartIndex = actionHit.partIndex;
      playbackControlFocus = PlaybackControlFocus::Loop;
      playbackControlFocusChangedAtMs = juce::Time::getMillisecondCounterHiRes();
      cancelDeferredPartJumpFromDoubleClick();
      if (onArrangementPartLoopToggleRequest)
        onArrangementPartLoopToggleRequest(actionHit.partIndex);
      repaint();
      return;
    }

    if (juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.jumpButton) {
      setSingleSelectedPart(actionHit.partIndex);
      playbackControlFocusPartIndex = actionHit.partIndex;
      playbackControlFocus = PlaybackControlFocus::Jump;
      playbackControlFocusChangedAtMs = juce::Time::getMillisecondCounterHiRes();
      cancelDeferredPartJumpFromDoubleClick();
      clearMuteForJumpSelection(actionHit.partIndex);
      if (onArrangementPartJumpRequest != nullptr)
        onArrangementPartJumpRequest(actionHit.partIndex);
      repaint();
      return;
    }

    if (juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.muteButton) {
      playbackControlFocusPartIndex = actionHit.partIndex;
      playbackControlFocus = PlaybackControlFocus::Mute;
      playbackControlFocusChangedAtMs = juce::Time::getMillisecondCounterHiRes();
      if (onArrangementPendingJumpClearRequest != nullptr)
        onArrangementPendingJumpClearRequest();
      toggleArrangementPartMute(actionHit.partIndex);
      return;
    }

    if (isEditingArrangement() &&
        juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.skipButton) {
      toggleArrangementPartSkip(actionHit.partIndex);
      return;
    }

    if (isEditingArrangement() &&
        juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.duplicateButton) {
      createDuplicatedSection(actionHit.partIndex);
      return;
    }

    if (isEditingArrangement() &&
        juce::isPositiveAndBelow(actionHit.partIndex,
                                 static_cast<int>(arrangement.parts.size())) &&
        actionHit.addButton) {
      for (const auto &overlay : partOverlayBounds) {
        if (overlay.partIndex == actionHit.partIndex) {
          openSectionPicker(overlay.addButton, actionHit.partIndex);
          return;
        }
      }
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

      if (!hit.leftEdge && !hit.rightEdge)
        ensurePartSelectedForDrag(hit.partIndex);
      else
        setSingleSelectedPart(hit.partIndex);

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

      if (dragMode == DragMode::movePart)
        captureDragPartOrigins();
      else
        dragPartOrigins.clear();
      arrangementDragUndoSnapshot = makeArrangementHistoryEntry();
      hasArrangementDragUndoSnapshot = true;

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

  if (minimalPhoneMode && phonePinchActive && event.source.isTouch()) {
    phoneTouchPositions[event.source.getIndex()] = event.position;
    if (phoneTouchPositions.size() < 2)
      return;

    auto first = phoneTouchPositions.begin();
    const auto firstPosition = first->second;
    const auto secondPosition = std::next(first)->second;
    const float currentDistance = juce::jmax(
        1.0f, firstPosition.getDistanceFrom(secondPosition));
    const float factor = currentDistance / phonePinchStartDistance;
    const float nextSpan = juce::jlimit(
        kMinViewportSpan, 1.0f, phonePinchStartViewportSpan / factor);
    setVisibleWindow(phonePinchAnchorSource -
                         (phonePinchAnchorRatio * nextSpan),
                     nextSpan);
    followSuspendedUntilMs = juce::Time::getMillisecondCounterHiRes() +
                             kFollowSuspendAfterManualViewportMs;
    return;
  }

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

    const bool directWaveformPan = minimalPhoneMode &&
                                   miniMapBounds.getWidth() <= 2.0f;
    const auto panBounds = directWaveformPan ? mainWaveBodyBounds : miniMapBounds;
    if (panBounds.getWidth() <= 2.0f)
      return;

    const float normalized = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - panBounds.getX()) /
            juce::jmax(1.0f, panBounds.getWidth()));

    if (dragMode == DragMode::panViewport) {
      if (directWaveformPan &&
          event.position.getDistanceFrom(viewportDragStartPosition) >=
              kTouchPanSlopPx)
        viewportDragMoved = true;
      const float delta = normalized - dragAnchorNormalized;
      // Direct manipulation moves the waveform one-to-one with the finger.
      // Convert the screen ratio into the currently visible source span; using
      // the full timeline here would make a zoomed waveform race ahead.
      const float viewportDelta = directWaveformPan
                                      ? -(delta * dragViewportSpan)
                                      : delta;
      setVisibleWindow(dragViewportStart + viewportDelta, dragViewportSpan);
      if (directWaveformPan) {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const float dt = static_cast<float>(juce::jlimit(
            0.001, 0.1, (nowMs - phonePanLastTimestampMs) * 0.001));
        const float sampleVelocity = -(normalized - phonePanLastNormalized) /
                                     dt;
        phonePanVelocity = (phonePanVelocity * 0.65f) +
                           (sampleVelocity * 0.35f);
        phonePanLastNormalized = normalized;
        phonePanLastTimestampMs = nowMs;
      }
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

  int partIndex = findPartIndexById(activeDragPartId);
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  // A copied part is an insertion in the arrangement timeline. Recreate its
  // preview from the drag origin on every mouse event so its insertion gap
  // moves or resizes with it instead of leaving a second gap behind. Multi-part moves
  // retain their existing independent-position behaviour.
  const double previousStart =
      arrangement.parts[static_cast<size_t>(partIndex)].startSec;
  const double previousEnd =
      arrangement.parts[static_cast<size_t>(partIndex)].endSec;
  const bool movingSingleDuplicatedPart =
      dragMode == DragMode::movePart && dragPartOrigins.size() == 1 &&
      hasArrangementDragUndoSnapshot;
  const bool resizingRightPart =
      dragMode == DragMode::resizePartRight && hasArrangementDragUndoSnapshot;
  auto dragOriginIt = arrangementDragUndoSnapshot.arrangement.parts.end();
  if (movingSingleDuplicatedPart || resizingRightPart) {
    dragOriginIt = std::find_if(
        arrangementDragUndoSnapshot.arrangement.parts.begin(),
        arrangementDragUndoSnapshot.arrangement.parts.end(),
        [this](const SongArrangementPartSnapshot &part) {
          return part.id == activeDragPartId;
        });
  }
  const bool editingDuplicatedInsertion =
      dragOriginIt != arrangementDragUndoSnapshot.arrangement.parts.end() &&
      dragOriginIt->sourcePartId.isNotEmpty();
  const bool movingDuplicatedInsertion =
      movingSingleDuplicatedPart && editingDuplicatedInsertion;
  const bool resizingDuplicatedInsertion =
      resizingRightPart && editingDuplicatedInsertion;
  if (editingDuplicatedInsertion) {
    arrangement = arrangementDragUndoSnapshot.arrangement;
    partIndex = findPartIndexById(activeDragPartId);
    if (!juce::isPositiveAndBelow(
            partIndex, static_cast<int>(arrangement.parts.size())))
      return;
  }

  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return;

  double pointerSec =
      windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);
  if (resizingDuplicatedInsertion &&
      event.position.x > mainWaveBodyBounds.getRight()) {
    // A tail copy can grow back to its full source duration even when its
    // current right edge is already the end of the visible arrangement.
    pointerSec += (event.position.x - mainWaveBodyBounds.getRight()) /
                  mainWaveBodyBounds.getWidth() * getVisibleWindow().second *
                  songLength;
  }
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

  if (dragMode == DragMode::movePart) {
    double duration = juce::jmax(kMinPartDurationSec,
                                 dragOriginPartEndSec - dragOriginPartStartSec);
    duration = juce::jmin(duration, songLength);

    const double maxStart = juce::jmax(0.0, songLength - duration);
    double nextStart = snapSeconds(dragOriginPartStartSec + deltaSec, bypassSnap);
    nextStart = movingDuplicatedInsertion
                    ? juce::jmax(0.0, nextStart)
                    : juce::jlimit(0.0, maxStart, nextStart);

    double nextEnd = movingDuplicatedInsertion
                         ? nextStart + duration
                         : juce::jlimit(nextStart, songLength,
                                        nextStart + duration);
    const double rawNextStart =
        movingDuplicatedInsertion
            ? juce::jmax(0.0, dragOriginPartStartSec + rawDeltaSec)
            : juce::jlimit(0.0, maxStart,
                           dragOriginPartStartSec + rawDeltaSec);
    const double rawNextEnd =
        movingDuplicatedInsertion
            ? rawNextStart + duration
            : juce::jlimit(rawNextStart, songLength, rawNextStart + duration);

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
    } else if (!movingDuplicatedInsertion && nextEnd > songLength) {
      nextEnd = songLength;
      nextStart = juce::jmax(0.0, songLength - duration);
    }

    if (movingDuplicatedInsertion) {
      const double insertionDuration =
          dragOriginIt->endSec - dragOriginIt->startSec;
      if (insertionDuration >= kMinPartDurationSec) {
        // Unlike a normal part, an inserted copy may extend the arrangement.
        // The next drag event starts from the original snapshot again, so
        // moving back left removes this temporary tail extension.
        const double destinationStart = juce::jmax(0.0, nextStart);

        for (auto &candidate : arrangement.parts) {
          if (candidate.id == activeDragPartId)
            continue;

          // Remove the old insertion first, then make the same-sized gap at
          // the destination. Together these shifts move only the material the
          // copied clip crosses; extending past the final part grows the
          // arrangement only by the new trailing gap.
          if (candidate.startSec >= dragOriginIt->endSec - 0.0001) {
            candidate.startSec -= insertionDuration;
            candidate.endSec -= insertionDuration;
          }
          if (candidate.startSec >= destinationStart - 0.0001) {
            candidate.startSec += insertionDuration;
            candidate.endSec += insertionDuration;
          }
        }

        part.startSec = destinationStart;
        part.endSec = destinationStart + insertionDuration;
      } else {
        part.startSec = nextStart;
        part.endSec = nextEnd;
      }
    } else {
      part.startSec = nextStart;
      part.endSec = nextEnd;
    }
  } else if (dragMode == DragMode::resizePartLeft) {
    double nextStart = snapSeconds(dragOriginPartStartSec + deltaSec, bypassSnap);
    const double anchoredStart =
        applyStickyAnchorToEdge(dragOriginPartStartSec + rawDeltaSec, true);
    if (dragAnchorActive && dragAnchorEdgeIsStart)
      nextStart = anchoredStart;
    const double maxStart =
        juce::jmax(0.0, dragOriginPartEndSec - kMinPartDurationSec);
    part.startSec = juce::jlimit(0.0, maxStart, nextStart);
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
    part.endSec = resizingDuplicatedInsertion
                      ? juce::jmax(minEnd, nextEnd)
                      : juce::jlimit(minEnd, songLength, nextEnd);

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

  if (part.sourcePartId.isNotEmpty() && dragMode != DragMode::movePart) {
    clampDuplicatedPartToSourceDuration(
        part, dragMode == DragMode::resizePartLeft,
        hasArrangementDragUndoSnapshot
            ? arrangementDragUndoSnapshot.arrangement
            : arrangement);
  }

  if (resizingDuplicatedInsertion) {
    // Rebuild both shrinking and growing previews from the same snapshot.
    // Following sections stay adjacent and the ripple is not accumulated
    // across mouse events or applied a second time when the drag commits.
    const double delta = part.endSec - dragOriginPartEndSec;
    for (auto &candidate : arrangement.parts) {
      if (candidate.id == activeDragPartId ||
          candidate.startSec < dragOriginPartEndSec - 0.0001)
        continue;
      candidate.startSec += delta;
      candidate.endSec += delta;
    }
  }

  if (nearlyEqual(previousStart, part.startSec) &&
      nearlyEqual(previousEnd, part.endSec))
    return;

  selectedPartIndex = partIndex;
  pruneSelectedPartIds();
  customArrangement = true;
  arrangementDragPendingCommit = true;
  repaint();
}

void WaveformBar::mouseUp(const juce::MouseEvent &event) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (midiLearnActive)
    return;

  if (minimalPhoneMode && event.source.isTouch()) {
    const bool wasPhonePinch = phonePinchActive;
    phoneTouchPositions.erase(event.source.getIndex());
    if (wasPhonePinch) {
      if (phoneTouchPositions.size() < 2)
        phonePinchActive = false;
      dragMode = DragMode::none;
      viewportDragMoved = false;
      setMouseCursor(juce::MouseCursor::NormalCursor);
      return;
    }
  }

  const bool wasArrangementDrag =
      dragMode == DragMode::movePart || dragMode == DragMode::resizePartLeft ||
      dragMode == DragMode::resizePartRight;
  const bool wasTempoDrag =
      dragMode == DragMode::moveTempoRegion ||
      dragMode == DragMode::resizeTempoRegionLeft ||
      dragMode == DragMode::resizeTempoRegionRight;
  const bool wasSelectionDrag = dragMode == DragMode::selectParts;
  const bool wasPhoneWaveformTap =
      minimalPhoneMode && dragMode == DragMode::panViewport &&
      !viewportDragMoved;
  const bool wasPhonePanDrag =
      minimalPhoneMode && dragMode == DragMode::panViewport &&
      viewportDragMoved && event.source.isTouch();
  dragMode = DragMode::none;
  activeDragPartId.clear();
  dragPartOrigins.clear();
  activeDragTempoRegionIndex = -1;
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  viewportDragMoved = false;
  if (wasPhonePanDrag && std::abs(phonePanVelocity) > 0.01f)
    phonePanInertiaActive = true;
  if (wasPhoneWaveformTap) {
    const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
    const auto seekBounds = getMainWaveBodyBounds(mainWaveBounds);
    handleSeek(event.position.x, seekBounds);
  }
  if (wasArrangementDrag && arrangementDragPendingCommit) {
    if (hasArrangementDragUndoSnapshot)
      pushArrangementUndoSnapshot(arrangementDragUndoSnapshot);
    hasArrangementDragUndoSnapshot = false;
    arrangementDragPendingCommit = false;
    notifyArrangementChanged(true);
    repaint();
  } else {
    hasArrangementDragUndoSnapshot = false;
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
    if (hoveredActionPartIndex >= 0 || hoveredLoopPartIndex >= 0 ||
        hoveredJumpPartIndex >= 0 || hoveredMutePartIndex >= 0 ||
        hoveredSkipPartIndex >= 0 || hoveredDuplicatePartIndex >= 0 ||
        hoveredAddPartIndex >= 0 ||
        hoveredDeletePartIndex >= 0) {
      hoveredActionPartIndex = -1;
      hoveredLoopPartIndex = -1;
      hoveredJumpPartIndex = -1;
      hoveredMutePartIndex = -1;
      hoveredSkipPartIndex = -1;
      hoveredDuplicatePartIndex = -1;
      hoveredAddPartIndex = -1;
      hoveredDeletePartIndex = -1;
      repaint();
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    return;
  }

  juce::MouseCursor nextCursor = juce::MouseCursor::NormalCursor;
  int nextHoveredActionPartIndex = -1;
  int nextHoveredLoopPartIndex = -1;
  int nextHoveredJumpPartIndex = -1;
  int nextHoveredMutePartIndex = -1;
  int nextHoveredSkipPartIndex = -1;
  int nextHoveredDuplicatePartIndex = -1;
  int nextHoveredAddPartIndex = -1;
  int nextHoveredDeletePartIndex = -1;
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
        } else if (hit.jumpButton) {
          nextHoveredJumpPartIndex = hit.partIndex;
          nextCursor = juce::MouseCursor::PointingHandCursor;
        } else if (hit.muteButton) {
          nextHoveredMutePartIndex = hit.partIndex;
          nextCursor = juce::MouseCursor::PointingHandCursor;
        } else if (hit.skipButton) {
          nextHoveredSkipPartIndex = hit.partIndex;
          nextCursor = juce::MouseCursor::PointingHandCursor;
        } else if (isEditingArrangement() && hit.duplicateButton) {
          nextHoveredDuplicatePartIndex = hit.partIndex;
          nextCursor = juce::MouseCursor::PointingHandCursor;
        } else if (isEditingArrangement() && hit.addButton) {
          nextHoveredAddPartIndex = hit.partIndex;
          nextCursor = juce::MouseCursor::PointingHandCursor;
        } else if (isEditingArrangement() && hit.deleteButton) {
          nextHoveredDeletePartIndex = hit.partIndex;
          nextCursor = juce::MouseCursor::PointingHandCursor;
        } else if (isEditingArrangement() && (hit.leftEdge || hit.rightEdge))
          nextCursor = juce::MouseCursor::LeftRightResizeCursor;
        else if (isEditingArrangement())
          nextCursor = juce::MouseCursor::DraggingHandCursor;
      }
    }
  }

  if (hoveredActionPartIndex != nextHoveredActionPartIndex ||
      hoveredLoopPartIndex != nextHoveredLoopPartIndex ||
      hoveredJumpPartIndex != nextHoveredJumpPartIndex ||
      hoveredMutePartIndex != nextHoveredMutePartIndex ||
      hoveredSkipPartIndex != nextHoveredSkipPartIndex ||
      hoveredDuplicatePartIndex != nextHoveredDuplicatePartIndex ||
      hoveredAddPartIndex != nextHoveredAddPartIndex ||
      hoveredDeletePartIndex != nextHoveredDeletePartIndex) {
    hoveredActionPartIndex = nextHoveredActionPartIndex;
    hoveredLoopPartIndex = nextHoveredLoopPartIndex;
    hoveredJumpPartIndex = nextHoveredJumpPartIndex;
    hoveredMutePartIndex = nextHoveredMutePartIndex;
    hoveredSkipPartIndex = nextHoveredSkipPartIndex;
    hoveredDuplicatePartIndex = nextHoveredDuplicatePartIndex;
    hoveredAddPartIndex = nextHoveredAddPartIndex;
    hoveredDeletePartIndex = nextHoveredDeletePartIndex;
    resized();
    repaint();
  }
  setMouseCursor(nextCursor);
}

void WaveformBar::mouseExit(const juce::MouseEvent &) {
  if (songLoadingActive || songLoadingHideAfterMs > 0.0)
    return;

  if (hoveredActionPartIndex >= 0 || hoveredLoopPartIndex >= 0 ||
      hoveredJumpPartIndex >= 0 || hoveredMutePartIndex >= 0 ||
      hoveredSkipPartIndex >= 0 || hoveredDuplicatePartIndex >= 0 ||
      hoveredAddPartIndex >= 0 ||
      hoveredDeletePartIndex >= 0) {
    hoveredActionPartIndex = -1;
    hoveredLoopPartIndex = -1;
    hoveredJumpPartIndex = -1;
    hoveredMutePartIndex = -1;
    hoveredSkipPartIndex = -1;
    hoveredDuplicatePartIndex = -1;
    hoveredAddPartIndex = -1;
    hoveredDeletePartIndex = -1;
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
  if (!isEditingArrangement())
    return false;

  if (key == juce::KeyPress::escapeKey && sectionInsertionAnchorValid) {
    clearSectionInsertionAnchor();
    return true;
  }

  const auto modifiers = key.getModifiers();
  const bool commandDown = modifiers.isCommandDown() || modifiers.isCtrlDown();
  const int keyCode = key.getKeyCode();
  if (commandDown && timelineViewMode == TimelineViewMode::GuideMap) {
    if (keyCode == 'c' || keyCode == 'C')
      return copySelectedArrangementParts();

    if (keyCode == 'v' || keyCode == 'V') {
      if (arrangementClipboard.empty() && !copySelectedArrangementParts())
        return false;
      return pasteCopiedArrangementParts();
    }

    if (keyCode == 'z' || keyCode == 'Z')
      return undoArrangementEdit();

    if (keyCode == 'y' || keyCode == 'Y')
      return redoArrangementEdit();
  }

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
  if (phonePanInertiaActive && minimalPhoneMode && dragMode == DragMode::none) {
    const float dt = 1.0f / 30.0f;
    const float displacement = phonePanVelocity * dt;
    const float nextStart = viewportStart + (displacement * viewportSpan);
    const float clampedStart = juce::jlimit(0.0f, 1.0f - viewportSpan, nextStart);
    setVisibleWindow(clampedStart, viewportSpan);
    phonePanVelocity *= 0.86f;
    if (std::abs(phonePanVelocity) < 0.01f ||
        !juce::approximatelyEqual(clampedStart, nextStart)) {
      phonePanVelocity = 0.0f;
      phonePanInertiaActive = false;
    }
    followSuspendedUntilMs = nowMs + kFollowSuspendAfterManualViewportMs;
    repaint();
  }
#if JUCE_ANDROID
  // VBlank delivery varies across Android devices and emulators. Drive the
  // visual transport from the message timer so follow mode remains regular.
  updatePlaybackAnimation(nowMs * 0.001);
#endif
  bool loadingStateCleared = false;

  if (songLoadingHideAfterMs > 0.0 &&
      nowMs >= songLoadingHideAfterMs) {
    songLoadingActive = false;
    songLoadingHideAfterMs = 0.0;
    songLoadingProgress01 = 0.0;
    songLoadingPhaseText.clear();
    loadingStateCleared = true;
  }

  const bool isPlayingNow = playingProvider ? playingProvider() : false;
  float pendingJumpRatio = 0.0f;
  const bool hasPendingJumpRatio = getPendingJumpProgressRatio(pendingJumpRatio);

  bool hasSongProgress = false;
  float songProgress = 0.0f;
  if (positionProvider != nullptr && lengthProvider != nullptr) {
    const double songLength = juce::jmax(0.0, lengthProvider());
    if (songLength > 0.0) {
      const double songPosSec = juce::jmax(0.0, positionProvider());
      songProgress =
          static_cast<float>(juce::jlimit(0.0, 1.0, songPosSec / songLength));
      hasSongProgress = true;
    }
  }

  const bool transportVisualChanged =
      hasSongProgress &&
      (!lastTimerSongProgressValid ||
       !nearlyEqual(songProgress, lastTimerSongProgressRatio, 0.000001));
  lastTimerSongProgressValid = hasSongProgress;
  if (hasSongProgress)
    lastTimerSongProgressRatio = songProgress;

  bool hasEndMarker = false;
  double endMarkerSeconds = 0.0;
  hasEndMarker = getPlaybackEndGuideSeconds(endMarkerSeconds);
  const bool endMarkerChanged =
      hasEndMarker != lastTimerEndValid ||
      (hasEndMarker &&
       !nearlyEqual(endMarkerSeconds, lastTimerEndSeconds, 0.0001));
  lastTimerEndValid = hasEndMarker;
  if (hasEndMarker)
    lastTimerEndSeconds = endMarkerSeconds;

  bool hasStartMarker = false;
  double startMarkerSeconds = 0.0;
  hasStartMarker = getPlaybackStartGuideSeconds(startMarkerSeconds);
  const bool startMarkerChanged =
      hasStartMarker != lastTimerStartValid ||
      (hasStartMarker &&
       !nearlyEqual(startMarkerSeconds, lastTimerStartSeconds, 0.0001));
  lastTimerStartValid = hasStartMarker;
  if (hasStartMarker)
    lastTimerStartSeconds = startMarkerSeconds;

  updateActiveTempoRegionFromTransport();
  flushDeferredPartJumpFromDoubleClickIfDue();
  const bool needsAnimatedRepaint =
      songLoadingActive || songLoadingHideAfterMs > 0.0 ||
      (!isPlayingNow && hasPendingJumpRatio) ||
      pendingJumpDoubleClickDispatchAtMs > 0.0 ||
      playbackControlFocus != PlaybackControlFocus::None ||
      (!isPlayingNow && transportVisualChanged);
  if (loadingStateCleared || startMarkerChanged || endMarkerChanged ||
      needsAnimatedRepaint)
    repaint();
}

void WaveformBar::updatePlaybackAnimation(const double frameTimestampSec) {
  const bool isPlayingNow = playingProvider ? playingProvider() : false;
#if JUCE_ANDROID
  if (isPlayingNow != androidWasPlaying) {
    // Playback uses a cached image; pause/stop use the detailed waveform.
    // Retained GPU rendering requires replacing the entire old representation
    // at the transition before resuming narrow playhead invalidations.
    androidWasPlaying = isPlayingNow;
    androidLastFixedPlayheadX = -1;
    repaint();
  }
#endif
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
    const double songLength = juce::jmax(0.0, lengthProvider());
    if (songLength > 0.0) {
      const double songPosSec = juce::jmax(0.0, positionProvider());
      songProgress =
          static_cast<float>(juce::jlimit(0.0, 1.0, songPosSec / songLength));
      hasSongProgress = true;
    }
  }

#if JUCE_ANDROID
  if (hasSongProgress) {
    if (!isPlayingNow || !androidVisualProgressValid ||
        androidVisualProgressTimestampSec <= 0.0) {
      androidVisualProgressRatio = songProgress;
    } else {
      const double elapsedSec = juce::jlimit(
          0.0, 0.1, frameTimestampSec - androidVisualProgressTimestampSec);
      const float delta = songProgress - androidVisualProgressRatio;
      if (std::abs(delta) >= 0.02f) {
        // Preserve intentional seeks, arrangement jumps and loop wraps.
        androidVisualProgressRatio = songProgress;
      } else {
        constexpr double smoothingTimeSec = 0.085;
        const float alpha = static_cast<float>(
            1.0 - std::exp(-elapsedSec / smoothingTimeSec));
        androidVisualProgressRatio += delta * alpha;
      }
    }

    androidVisualProgressValid = true;
    androidVisualProgressTimestampSec = frameTimestampSec;
    songProgress = androidVisualProgressRatio;
  } else {
    androidVisualProgressValid = false;
    androidVisualProgressTimestampSec = frameTimestampSec;
  }
#endif

  const bool followTrackingActive = isFollowTrackingActive();
  if ((followTrackingActive || followJumpJustExecuted) && hasSongProgress) {
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

    if (!nearlyEqual(targetStart, start, kFollowViewportPositionEpsilon)) {
      if (followJumpJustExecuted || followDiscontinuity) {
        setVisibleWindow(targetStart, span);
        followLastFrameTimestampSec = frameTimestampSec;
      } else {
        const double elapsedSec =
            followLastFrameTimestampSec > 0.0
                ? juce::jlimit(0.0, 0.25,
                               frameTimestampSec -
                                   followLastFrameTimestampSec)
                : (1.0 / 60.0);
        const float smoothingAlpha = static_cast<float>(
            1.0 - std::exp(-elapsedSec / kFollowCameraSmoothingTimeSec));
        const float smoothedStart =
            start + ((targetStart - start) * smoothingAlpha);
        setVisibleWindow(smoothedStart, span);
        followLastFrameTimestampSec = frameTimestampSec;
      }
    } else {
      followLastFrameTimestampSec = frameTimestampSec;
    }
  } else if (!followTrackingActive) {
    followLastFrameTimestampSec = 0.0;
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

  if (isPlayingNow || hasPendingJumpRatio || followJumpJustExecuted) {
#if JUCE_ANDROID
    if (isPlayingNow && hasSongProgress &&
        !hasPendingJumpRatio && !followJumpJustExecuted) {
      const auto waveBounds =
          getMainWaveBodyBounds(getMainWaveBounds(getLocalBounds().toFloat()));
      const auto window = getVisibleWindow();
      const float playheadNorm =
          juce::jlimit(0.0f, 1.0f,
                       (songProgress - window.first) /
                           juce::jmax(0.0001f, window.second));
      const int currentX = juce::roundToInt(
          waveBounds.getX() + playheadNorm * waveBounds.getWidth());
      const int firstX = androidLastFixedPlayheadX >= 0
                             ? juce::jmin(androidLastFixedPlayheadX, currentX)
                             : currentX;
      const int lastX = androidLastFixedPlayheadX >= 0
                            ? juce::jmax(androidLastFixedPlayheadX, currentX)
                            : currentX;
      const int stripeWidth =
          juce::jmax(11, lastX - firstX + 11);
      // A fixed viewport only needs the playhead stripe. If follow moved the
      // viewport above, setVisibleWindow already requested a full repaint.
      repaint(firstX - 5, juce::roundToInt(waveBounds.getY()), stripeWidth,
              juce::roundToInt(waveBounds.getHeight()));
      androidLastFixedPlayheadX = currentX;
    } else {
      androidLastFixedPlayheadX = -1;
      repaint();
    }
#else
    repaint();
#endif
  }
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

  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0 || seekBounds.getWidth() <= 0.0f)
    return;

  const float clampedX =
      juce::jlimit(seekBounds.getX(), seekBounds.getRight(), x);
  const float ratio = (clampedX - seekBounds.getX()) / seekBounds.getWidth();
  const auto window = getVisibleWindow();
  const double rawTarget =
      songLength * juce::jlimit(0.0f, 1.0f, window.first + (ratio * window.second));
  const auto [playableStartSec, playableEndSec] = getPlayableRangeSeconds();
  const double target =
      juce::jlimit(playableStartSec, playableEndSec, rawTarget);

  if (isEditingArrangement() && timelineViewMode == TimelineViewMode::GuideMap) {
    sectionInsertionAnchorSec = juce::jlimit(
        playableStartSec, playableEndSec, snapSeconds(target));
    sectionInsertionAnchorValid = true;
    resized();
    repaint();
  }
  onSeekTo(target);
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

  const bool changed =
      !nearlyEqual(start, viewportStart, kFollowViewportPositionEpsilon) ||
      !nearlyEqual(span, viewportSpan, kFollowViewportPositionEpsilon);
  const bool spanChanged =
      !nearlyEqual(span, viewportSpan, kFollowViewportPositionEpsilon);
  viewportStart = start;
  viewportSpan = span;
  if (spanChanged)
    invalidateWaveformRenderCaches();
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

WaveformBar::PlaybackVisualState WaveformBar::getPlaybackVisualState() const {
  PlaybackVisualState state;
  const bool isShowingLoadingState =
      songLoadingActive || songLoadingHideAfterMs > 0.0;
  if (isShowingLoadingState) {
    state.progressRatio =
        static_cast<float>(juce::jlimit(0.0, 1.0, songLoadingProgress01));
  } else if (positionProvider != nullptr && lengthProvider != nullptr) {
    const double transportLength = juce::jmax(0.0, lengthProvider());
    if (transportLength > 0.0) {
      const double songPosition = juce::jmax(0.0, positionProvider());
      state.progressRatio = static_cast<float>(
          juce::jlimit(0.0, 1.0, songPosition / transportLength));
      state.transportProgressRatio = state.progressRatio;
    }
  }

#if JUCE_ANDROID
  if (!isShowingLoadingState && androidVisualProgressValid &&
      playingProvider != nullptr && playingProvider()) {
    state.progressRatio = androidVisualProgressRatio;
    state.transportProgressRatio = androidVisualProgressRatio;
  }
#endif

  if (followPlaybackEnabled && !isEditingArrangement() &&
      playingProvider != nullptr && playingProvider()) {
    float pendingRatio = 0.0f;
    if (getPendingJumpProgressRatio(pendingRatio)) {
      state.progressRatio = pendingRatio;
      state.showPendingTravelSeek = !isShowingLoadingState &&
                                    positionProvider != nullptr &&
                                    lengthProvider != nullptr;
    }
  }

  return state;
}

void WaveformBar::invalidateWaveformRenderCaches() {
  followWaveformBaseCache = {};
  followWaveformHighlightMaskCache = {};
  followWaveformCacheWidth = 0;
  followWaveformCacheHeight = 0;
  followWaveformCacheStart = 0.0f;
  followWaveformCacheSpan = 0.0f;
  followWaveformCacheScale = 0.0f;
  miniMapWaveformCache = {};
  miniMapWaveformCacheWidth = 0;
  miniMapWaveformCacheHeight = 0;
}

bool WaveformBar::getPendingJumpProgressRatio(float &ratioOut) const {
  if (pendingArrangementPartProvider == nullptr || lengthProvider == nullptr)
    return false;

  const int pendingPartIndex = pendingArrangementPartProvider();
  if (!juce::isPositiveAndBelow(pendingPartIndex,
                                static_cast<int>(arrangement.parts.size())))
    return false;

  const double songLength = juce::jmax(0.0, lengthProvider());
  if (songLength <= 0.0)
    return false;

  const double targetSec = juce::jlimit(
      0.0, songLength,
      arrangement.parts[static_cast<size_t>(pendingPartIndex)].startSec);
  ratioOut = static_cast<float>(juce::jlimit(0.0, 1.0, targetSec / songLength));
  return true;
}

double WaveformBar::getSongLengthSeconds() const {
  double length = lengthProvider ? juce::jmax(0.0, lengthProvider()) : 0.0;
  if (customArrangement) {
    double sourceLength = sourceLengthProvider
                              ? juce::jmax(0.0, sourceLengthProvider())
                              : 0.0;
    if (sourceLength <= 0.0)
      sourceLength = length;

    double insertedLength = 0.0;
    for (const auto &part : arrangement.parts) {
      if (std::isfinite(part.endSec))
        length = juce::jmax(length, part.endSec);
      if (part.sourcePartId.isNotEmpty() && part.endSec > part.startSec)
        insertedLength += part.endSec - part.startSec;
    }
    if (sourceLength > 0.0 && insertedLength > 0.0)
      length = juce::jmax(length, sourceLength + insertedLength);
  }
  return length;
}

bool WaveformBar::getPlaybackEndGuideSeconds(double &endSecondsOut) const {
  if (lengthProvider == nullptr || songWindowEndProvider == nullptr)
    return false;

  const double timelineLength = juce::jmax(0.0, lengthProvider());
  if (timelineLength <= 0.0)
    return false;

  const double end = juce::jlimit(0.0, timelineLength, songWindowEndProvider());
  if (customArrangement) {
    const double sourceLength = sourceLengthProvider
                                    ? juce::jmax(0.0, sourceLengthProvider())
                                    : timelineLength;
    const bool customized = songEndCustomizedProvider != nullptr
                                ? songEndCustomizedProvider()
                                : (sourceLength > 0.0 &&
                                   end < sourceLength - 0.0001);
    if (!customized)
      return false;
  } else if (end >= timelineLength - 0.0001) {
    return false;
  }

  endSecondsOut = end;
  return true;
}

bool WaveformBar::getPlaybackStartGuideSeconds(double &startSecondsOut) const {
  if (lengthProvider == nullptr || songWindowStartProvider == nullptr)
    return false;

  const double timelineLength = juce::jmax(0.0, lengthProvider());
  if (timelineLength <= 0.0)
    return false;

  const double end = songWindowEndProvider != nullptr
                         ? juce::jlimit(0.0, timelineLength,
                                        songWindowEndProvider())
                         : timelineLength;
  const double start = juce::jlimit(0.0, end, songWindowStartProvider());
  if (start <= 0.0001)
    return false;

  startSecondsOut = start;
  return true;
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
  const double currentLength = getSongLengthSeconds();
  double arrangementLength = currentLength;
  for (const auto &part : arrangement.parts) {
    if (std::isfinite(part.endSec))
      arrangementLength = juce::jmax(arrangementLength, part.endSec);
  }
  const double sourceLength = getWaveformSourceLengthSeconds();
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

    part.startSec = juce::jlimit(0.0, arrangementLength, part.startSec);
    part.endSec = juce::jlimit(0.0, arrangementLength, part.endSec);
    if (part.endSec < part.startSec)
      std::swap(part.endSec, part.startSec);

    if ((part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec =
          juce::jmin(arrangementLength, part.startSec + kMinPartDurationSec);
      if ((part.endSec - part.startSec) < kMinPartDurationSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }

    if (snapToGrid) {
      part.startSec = snapSeconds(part.startSec);
      part.endSec = snapSeconds(part.endSec);
    }

    part.startSec = juce::jlimit(0.0, arrangementLength, part.startSec);
    part.endSec = juce::jlimit(0.0, arrangementLength, part.endSec);
    if ((part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec =
          juce::jmin(arrangementLength, part.startSec + kMinPartDurationSec);
      if ((part.endSec - part.startSec) < kMinPartDurationSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }

    if (sourceLength > 0.0) {
      bool hasSourceRange = true;
      if (part.sourcePartId.isEmpty()) {
        part.sourceStartSec = juce::jlimit(0.0, sourceLength, part.startSec);
        part.sourceEndSec = juce::jlimit(0.0, sourceLength, part.endSec);
      } else if (part.sourceEndSec <= part.sourceStartSec) {
        part.sourceStartSec = 0.0;
        part.sourceEndSec = 0.0;
        hasSourceRange = false;
      } else {
        part.sourceStartSec =
            juce::jlimit(0.0, sourceLength, part.sourceStartSec);
        part.sourceEndSec = juce::jlimit(0.0, sourceLength, part.sourceEndSec);
      }
      if (hasSourceRange && part.sourceEndSec < part.sourceStartSec)
        std::swap(part.sourceStartSec, part.sourceEndSec);
      if (hasSourceRange &&
          (part.sourceEndSec - part.sourceStartSec) < kMinPartDurationSec) {
        part.sourceEndSec =
            juce::jmin(sourceLength, part.sourceStartSec + kMinPartDurationSec);
        if ((part.sourceEndSec - part.sourceStartSec) < kMinPartDurationSec)
          part.sourceStartSec =
              juce::jmax(0.0, part.sourceEndSec - kMinPartDurationSec);
      }
    }
  }

  if (sourceLength > 0.0) {
    for (auto &part : arrangement.parts) {
      if (part.sourcePartId.isEmpty())
        continue;
      if (part.sourceEndSec > part.sourceStartSec)
        continue;
      const int sourceIndex = findPartIndexById(part.sourcePartId);
      if (!juce::isPositiveAndBelow(sourceIndex,
                                    static_cast<int>(arrangement.parts.size())))
        continue;
      const auto &source = arrangement.parts[static_cast<size_t>(sourceIndex)];
      if (source.sourcePartId.isNotEmpty() &&
          source.sourceEndSec > source.sourceStartSec) {
        part.sourceStartSec = juce::jlimit(0.0, sourceLength, source.sourceStartSec);
        part.sourceEndSec = juce::jlimit(0.0, sourceLength, source.sourceEndSec);
      } else {
        part.sourceStartSec = juce::jlimit(0.0, sourceLength, source.startSec);
        part.sourceEndSec = juce::jlimit(0.0, sourceLength, source.endSec);
      }
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

WaveformBar::ArrangementHistoryEntry
WaveformBar::makeArrangementHistoryEntry() const {
  ArrangementHistoryEntry entry;
  entry.arrangement = arrangement;
  entry.customArrangement = customArrangement;
  entry.selectedPartIndex = selectedPartIndex;
  entry.selectedPartIds = selectedPartIds;
  return entry;
}

void WaveformBar::pushArrangementUndoSnapshot() {
  pushArrangementUndoSnapshot(makeArrangementHistoryEntry());
}

void WaveformBar::pushArrangementUndoSnapshot(
    const ArrangementHistoryEntry &entry) {
  constexpr size_t kMaxArrangementHistoryEntries = 64;
  if (arrangementUndoStack.size() >= kMaxArrangementHistoryEntries)
    arrangementUndoStack.erase(arrangementUndoStack.begin());
  arrangementUndoStack.push_back(entry);
  arrangementRedoStack.clear();
}

void WaveformBar::restoreArrangementHistoryEntry(
    const ArrangementHistoryEntry &entry) {
  arrangement = entry.arrangement;
  customArrangement = entry.customArrangement;
  selectedPartIndex = entry.selectedPartIndex;
  selectedPartIds = entry.selectedPartIds;
  normalizeArrangement(true, false);
  pruneSelectedPartIds();
  if (!juce::isPositiveAndBelow(selectedPartIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    selectedPartIndex =
        selectedPartIds.empty() ? -1 : findPartIndexById(selectedPartIds.back());
  }
  notifyArrangementChanged(customArrangement);
  repaint();
}

bool WaveformBar::undoArrangementEdit() {
  if (arrangementUndoStack.empty())
    return false;

  arrangementRedoStack.push_back(makeArrangementHistoryEntry());
  const auto entry = arrangementUndoStack.back();
  arrangementUndoStack.pop_back();
  restoreArrangementHistoryEntry(entry);
  return true;
}

bool WaveformBar::redoArrangementEdit() {
  if (arrangementRedoStack.empty())
    return false;

  arrangementUndoStack.push_back(makeArrangementHistoryEntry());
  const auto entry = arrangementRedoStack.back();
  arrangementRedoStack.pop_back();
  restoreArrangementHistoryEntry(entry);
  return true;
}

juce::String WaveformBar::resolveRealSourcePartId(
    const SongArrangementPartSnapshot &part) const {
  juce::String sourceId =
      part.sourcePartId.isEmpty() ? part.id : part.sourcePartId;
  const int maxDepth = static_cast<int>(arrangement.parts.size());
  for (int depth = 0; depth < maxDepth && sourceId.isNotEmpty(); ++depth) {
    const int sourceIndex = findPartIndexById(sourceId);
    if (!juce::isPositiveAndBelow(sourceIndex,
                                  static_cast<int>(arrangement.parts.size())))
      break;

    const auto &source = arrangement.parts[static_cast<size_t>(sourceIndex)];
    if (source.sourcePartId.isEmpty())
      break;
    sourceId = source.sourcePartId;
  }
  return sourceId;
}

bool WaveformBar::copySelectedArrangementParts() {
  if (timelineViewMode == TimelineViewMode::TempoMap)
    return false;

  pruneSelectedPartIds();
  std::vector<int> selectedIndices;
  for (const auto &id : selectedPartIds) {
    const int idx = findPartIndexById(id);
    if (juce::isPositiveAndBelow(idx,
                                 static_cast<int>(arrangement.parts.size())))
      selectedIndices.push_back(idx);
  }
  if (selectedIndices.empty() && juce::isPositiveAndBelow(
                                     selectedPartIndex,
                                     static_cast<int>(arrangement.parts.size()))) {
    selectedIndices.push_back(selectedPartIndex);
  }
  if (selectedIndices.empty())
    return false;

  std::sort(selectedIndices.begin(), selectedIndices.end(),
            [this](const int a, const int b) {
              return arrangement.parts[static_cast<size_t>(a)].startSec <
                     arrangement.parts[static_cast<size_t>(b)].startSec;
            });
  selectedIndices.erase(std::unique(selectedIndices.begin(), selectedIndices.end()),
                        selectedIndices.end());

  const double sourceLength = getWaveformSourceLengthSeconds();
  if (sourceLength <= 0.0)
    return false;

  const double baseStart =
      arrangement.parts[static_cast<size_t>(selectedIndices.front())].startSec;
  std::vector<CopiedArrangementPart> nextClipboard;
  nextClipboard.reserve(selectedIndices.size());

  for (const int idx : selectedIndices) {
    const auto &part = arrangement.parts[static_cast<size_t>(idx)];
    const auto sourceId = resolveRealSourcePartId(part);
    if (sourceId.isEmpty())
      continue;

    const auto [sourceStartSec, sourceEndSec] =
        arrangement_source_mapping::resolveCopySourceRange(
            arrangement.parts, part, sourceLength);
    if ((sourceEndSec - sourceStartSec) < kMinPartDurationSec)
      continue;

    CopiedArrangementPart copied;
    copied.part = part;
    copied.sourcePartId = sourceId;
    copied.sourceStartSec = sourceStartSec;
    copied.sourceEndSec = sourceEndSec;
    copied.relativeStartSec = juce::jmax(0.0, part.startSec - baseStart);
    nextClipboard.push_back(std::move(copied));
  }

  if (nextClipboard.empty())
    return false;

  arrangementClipboard = std::move(nextClipboard);
  return true;
}

bool WaveformBar::pasteCopiedArrangementParts() {
  if (timelineViewMode == TimelineViewMode::TempoMap)
    return false;
  if (arrangementClipboard.empty())
    return false;

  double insertAtSec = 0.0;
  if (juce::isPositiveAndBelow(selectedPartIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    insertAtSec = arrangement.parts[static_cast<size_t>(selectedPartIndex)].endSec;
  } else if (!arrangement.parts.empty()) {
    insertAtSec = arrangement.parts.back().endSec;
  }

  double blockDuration = 0.0;
  for (const auto &copied : arrangementClipboard) {
    const double clipDuration = copied.sourceEndSec - copied.sourceStartSec;
    if (clipDuration < kMinPartDurationSec)
      continue;
    blockDuration =
        juce::jmax(blockDuration, copied.relativeStartSec + clipDuration);
  }
  if (blockDuration < kMinPartDurationSec)
    return false;

  pushArrangementUndoSnapshot();

  for (auto &part : arrangement.parts) {
    if (part.startSec >= insertAtSec - 0.0001) {
      part.startSec += blockDuration;
      part.endSec += blockDuration;
    }
  }

  selectedPartIds.clear();
  int lastInsertedIndex = -1;
  for (const auto &copied : arrangementClipboard) {
    const double clipDuration = copied.sourceEndSec - copied.sourceStartSec;
    if (clipDuration < kMinPartDurationSec)
      continue;

    SongArrangementPartSnapshot part = copied.part;
    if (copied.sourcePartId.isNotEmpty()) {
      const int sourceIndex = findPartIndexById(copied.sourcePartId);
      if (juce::isPositiveAndBelow(
              sourceIndex, static_cast<int>(arrangement.parts.size()))) {
        const auto &sourcePart =
            arrangement.parts[static_cast<size_t>(sourceIndex)];
        part.name = sourcePart.name;
        part.color = sourcePart.color;
      }
    }
    part.id = juce::Uuid().toString();
    part.sourcePartId = copied.sourcePartId;
    part.sourceStartSec = copied.sourceStartSec;
    part.sourceEndSec = copied.sourceEndSec;
    part.startSec = insertAtSec + copied.relativeStartSec;
    part.endSec = part.startSec + clipDuration;
    if (part.color.isEmpty())
      part.color = fallbackArrangementPartColorForName(part.name);

    selectedPartIds.push_back(part.id);
    arrangement.parts.push_back(std::move(part));
    lastInsertedIndex = static_cast<int>(arrangement.parts.size()) - 1;
  }

  if (lastInsertedIndex < 0)
    return false;

  customArrangement = true;
  normalizeArrangement(true, false);
  selectedPartIndex =
      selectedPartIds.empty() ? -1 : findPartIndexById(selectedPartIds.back());
  notifyArrangementChanged(true);
  repaint();
  return true;
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

int WaveformBar::getExtraHeightForHostLayout() const {
  if (!isEditingArrangement())
    return 0;
  return static_cast<int>(std::ceil(kEditToolbarHeight + kEditToolbarBottomGap));
}

juce::Rectangle<float>
WaveformBar::getEditToolbarBounds(const juce::Rectangle<float> &bounds) const {
  const auto area = bounds.reduced(8.0f, 6.0f)
                        .withTrimmedBottom(kBottomSafetyInset);
  const float height = juce::jmin(kEditToolbarHeight, area.getHeight());
  return {area.getX(), area.getY(), area.getWidth(), height};
}

juce::Rectangle<float>
WaveformBar::getMainWaveBounds(const juce::Rectangle<float> &bounds) const {
  auto area = minimalPhoneMode
                  ? bounds
                  : bounds.reduced(8.0f, 6.0f)
                        .withTrimmedBottom(kBottomSafetyInset);
  if (isEditingArrangement())
    area = area.withTrimmedTop(kEditToolbarHeight + kEditToolbarBottomGap);
  // Phones use the full waveform height; the minimap/navigation strip is
  // deliberately omitted to preserve room for the mixer.
  if (!minimalPhoneMode)
    area = area.withTrimmedBottom(kMiniMapHeight + kMiniMapGap);
  return area;
}

juce::Rectangle<float>
WaveformBar::getSeekRulerBounds(
    const juce::Rectangle<float> &mainWaveBounds) const {
  if (minimalPhoneMode)
    return {};

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
  if (minimalPhoneMode)
    return {};

  auto area = bounds.reduced(8.0f, 6.0f);
  area = area.withTrimmedBottom(kBottomSafetyInset);
  if (isEditingArrangement())
    area = area.withTrimmedTop(kEditToolbarHeight + kEditToolbarBottomGap);
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
  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0 || windowBounds.getWidth() <= 0.0f)
    return 0.0;

  const float clampedX =
      juce::jlimit(windowBounds.getX(), windowBounds.getRight(), x);
  const float ratio = (clampedX - windowBounds.getX()) / windowBounds.getWidth();
  const auto window = getVisibleWindow();
  const float sourceRatio =
      juce::jlimit(0.0f, 1.0f, window.first + (ratio * window.second));
  return static_cast<double>(sourceRatio) * songLength;
}

float WaveformBar::sourceSecondsToWindowX(
    const juce::Rectangle<float> &windowBounds, const double sourceSeconds) const {
  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0 || windowBounds.getWidth() <= 0.0f)
    return windowBounds.getX();

  const auto window = getVisibleWindow();
  const float normalizedSource =
      static_cast<float>(juce::jlimit(0.0, 1.0, sourceSeconds / songLength));
  const float normalizedWindow =
      (normalizedSource - window.first) / juce::jmax(0.0001f, window.second);
  return windowBounds.getX() +
         (juce::jlimit(0.0f, 1.0f, normalizedWindow) * windowBounds.getWidth());
}

double WaveformBar::getWaveformSourceLengthSeconds() const {
  if (sourceLengthProvider != nullptr) {
    const double sourceLength = juce::jmax(0.0, sourceLengthProvider());
    if (sourceLength > 0.0)
      return sourceLength;
  }

  if (!customArrangement && lengthProvider != nullptr) {
    const double sourceLength = juce::jmax(0.0, lengthProvider());
    if (sourceLength > 0.0)
      return sourceLength;
  }

  double sourceLength = 0.0;
  for (const auto &part : arrangement.parts) {
    if (part.sourcePartId.isNotEmpty() &&
        part.sourceEndSec > part.sourceStartSec)
      sourceLength = juce::jmax(sourceLength, part.sourceEndSec);
    else if (part.sourcePartId.isEmpty())
      sourceLength = juce::jmax(sourceLength, part.endSec);
  }

  if (sourceLength <= 0.0)
    sourceLength = getSongLengthSeconds();
  return sourceLength;
}

std::pair<double, double> WaveformBar::getPartSourceRange(
    const SongArrangementPartSnapshot &part, const double sourceLength) const {
  if (part.sourcePartId.isEmpty()) {
    return {juce::jlimit(0.0, sourceLength, part.startSec),
            juce::jlimit(0.0, sourceLength, part.endSec)};
  }

  if (part.sourceEndSec > part.sourceStartSec) {
    return {juce::jlimit(0.0, sourceLength, part.sourceStartSec),
            juce::jlimit(0.0, sourceLength, part.sourceEndSec)};
  }

  const int sourceIndex = findPartIndexById(part.sourcePartId);
  if (juce::isPositiveAndBelow(sourceIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    const auto &source = arrangement.parts[static_cast<size_t>(sourceIndex)];
    if (source.sourcePartId.isNotEmpty() &&
        source.sourceEndSec > source.sourceStartSec) {
      return {juce::jlimit(0.0, sourceLength, source.sourceStartSec),
              juce::jlimit(0.0, sourceLength, source.sourceEndSec)};
    }
    return {juce::jlimit(0.0, sourceLength, source.startSec),
            juce::jlimit(0.0, sourceLength, source.endSec)};
  }

  return {juce::jlimit(0.0, sourceLength, part.startSec),
          juce::jlimit(0.0, sourceLength, part.endSec)};
}

std::pair<double, double> WaveformBar::getPartOriginalSourceRange(
    const SongArrangementPartSnapshot &part, const double sourceLength) const {
  return arrangement_source_mapping::sourceRangeForOriginalPart(
      arrangement.parts, part, sourceLength);
}

double WaveformBar::getPartSourceDurationSeconds(
    const SongArrangementPartSnapshot &part) const {
  if (part.sourcePartId.isEmpty())
    return juce::jmax(0.0, part.endSec - part.startSec);

  const double sourceLength = getWaveformSourceLengthSeconds();
  if (sourceLength <= 0.0)
    return 0.0;

  const auto [sourceStart, sourceEnd] =
      getPartOriginalSourceRange(part, sourceLength);
  return juce::jmax(0.0, sourceEnd - sourceStart);
}

void WaveformBar::clampDuplicatedPartToSourceDuration(
    SongArrangementPartSnapshot &part, const bool anchorEnd,
    const SongArrangementSnapshot &sourceTimeline) {
  if (part.sourcePartId.isEmpty())
    return;

  const double sourceLength = getWaveformSourceLengthSeconds();
  if (sourceLength <= 0.0)
    return;

  auto [currentSourceStart, currentSourceEnd] =
      getPartSourceRange(part, sourceLength);
  if (currentSourceEnd < currentSourceStart)
    std::swap(currentSourceStart, currentSourceEnd);

  // The preview or range dialog may already have changed the insertion's
  // duration. Resolve source limits against the coherent pre-edit timeline.
  auto [originalSourceStart, originalSourceEnd] =
      arrangement_source_mapping::sourceRangeForOriginalPart(
          sourceTimeline.parts, part, sourceLength);
  if (originalSourceEnd < originalSourceStart)
    std::swap(originalSourceStart, originalSourceEnd);

  const double originalDuration = originalSourceEnd - originalSourceStart;
  if (originalDuration < kMinPartDurationSec)
    return;

  const double sourceAnchor =
      anchorEnd
          ? juce::jlimit(originalSourceStart, originalSourceEnd,
                         currentSourceEnd)
          : juce::jlimit(originalSourceStart, originalSourceEnd,
                         currentSourceStart);
  const double maxDuration =
      anchorEnd ? sourceAnchor - originalSourceStart
                : originalSourceEnd - sourceAnchor;
  if (maxDuration < kMinPartDurationSec)
    return;

  double duration =
      juce::jlimit(kMinPartDurationSec, maxDuration, part.endSec - part.startSec);
  if (anchorEnd) {
    part.startSec = part.endSec - duration;
    part.sourceStartSec = sourceAnchor - duration;
    part.sourceEndSec = sourceAnchor;
  } else {
    part.endSec = part.startSec + duration;
    part.sourceStartSec = sourceAnchor;
    part.sourceEndSec = sourceAnchor + duration;
  }

  part.sourceStartSec = juce::jlimit(0.0, sourceLength, part.sourceStartSec);
  part.sourceEndSec = juce::jlimit(0.0, sourceLength, part.sourceEndSec);
}

void WaveformBar::rippleCloseGapAfterDuplicatedPart(
    const double gapStartSec, const double gapEndSec,
    const juce::String &excludedPartId) {
  if (!std::isfinite(gapStartSec) || !std::isfinite(gapEndSec))
    return;

  const double gap = gapEndSec - gapStartSec;
  if (gap <= 0.0001)
    return;

  for (auto &part : arrangement.parts) {
    if (excludedPartId.isNotEmpty() && part.id == excludedPartId)
      continue;
    if (part.startSec + 0.0001 < gapEndSec)
      continue;

    part.startSec = juce::jmax(0.0, part.startSec - gap);
    part.endSec = juce::jmax(part.startSec + kMinPartDurationSec,
                             part.endSec - gap);
  }
}

bool WaveformBar::mapArrangementSecondsToSourceSeconds(
    const double arrangementSeconds, const double sourceLength,
    double &sourceSecondsOut) const {
  if (sourceLength <= 0.0)
    return false;

  struct Segment {
    double start = 0.0;
    double end = 0.0;
    double sourceStart = 0.0;
    double sourceEnd = 0.0;
  };

  std::vector<Segment> segments;
  segments.reserve(arrangement.parts.size());
  for (const auto &part : arrangement.parts) {
    if (part.sourcePartId.isEmpty())
      continue;
    if (part.endSec <= part.startSec)
      continue;
    const auto [sourceStart, sourceEnd] = getPartSourceRange(part, sourceLength);
    if (sourceEnd <= sourceStart)
      continue;
    segments.push_back({part.startSec, part.endSec, sourceStart, sourceEnd});
  }

  if (segments.empty()) {
    sourceSecondsOut = juce::jlimit(0.0, sourceLength, arrangementSeconds);
    return true;
  }

  std::stable_sort(segments.begin(), segments.end(),
                   [](const Segment &a, const Segment &b) {
                     if (nearlyEqual(a.start, b.start))
                       return a.end < b.end;
                     return a.start < b.start;
                   });

  double insertedOffset = 0.0;
  for (const auto &segment : segments) {
    if (arrangementSeconds < segment.start) {
      sourceSecondsOut =
          juce::jlimit(0.0, sourceLength, arrangementSeconds - insertedOffset);
      return true;
    }

    if (arrangementSeconds < segment.end) {
      const double duration = juce::jmax(0.0001, segment.end - segment.start);
      const double sourceDuration =
          juce::jmax(0.0001, segment.sourceEnd - segment.sourceStart);
      const double ratio = juce::jlimit(
          0.0, 1.0, (arrangementSeconds - segment.start) / duration);
      sourceSecondsOut =
          juce::jlimit(0.0, sourceLength,
                       segment.sourceStart + (ratio * sourceDuration));
      return true;
    }

    insertedOffset += segment.end - segment.start;
  }

  sourceSecondsOut =
      juce::jlimit(0.0, sourceLength, arrangementSeconds - insertedOffset);
  return true;
}

WaveformBar::PartHitResult
WaveformBar::hitTestArrangementPart(const juce::Point<float> pos) const {
  for (int i = static_cast<int>(partOverlayBounds.size()) - 1; i >= 0; --i) {
    const auto &bounds = partOverlayBounds[static_cast<size_t>(i)];
    if (!bounds.body.contains(pos) && !bounds.editToolbar.contains(pos))
      continue;

    PartHitResult hit;
    hit.partIndex = bounds.partIndex;
    hit.deleteButton = bounds.showsDeleteButton && bounds.deleteButton.contains(pos);
    if (hit.deleteButton)
      return hit;
    hit.muteButton = bounds.showsMuteButton && bounds.muteButton.contains(pos);
    if (hit.muteButton)
      return hit;
    hit.skipButton = bounds.showsSkipButton && bounds.skipButton.contains(pos);
    if (hit.skipButton)
      return hit;
    hit.duplicateButton = bounds.showsDuplicateButton && bounds.duplicateButton.contains(pos);
    if (hit.duplicateButton)
      return hit;
    hit.addButton = bounds.showsAddButton && bounds.addButton.contains(pos);
    if (hit.addButton)
      return hit;
    if (!bounds.body.contains(pos))
      return {};
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

  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return;

  const auto window = getVisibleWindow();
  const int activeLoopPartIndex =
      activeArrangementLoopPartProvider ? activeArrangementLoopPartProvider() : -1;
  const int pendingJumpPartIndex =
      pendingArrangementPartProvider ? pendingArrangementPartProvider() : -1;
  bool playbackFocusStillActive = false;
  for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
    const auto &part = arrangement.parts[static_cast<size_t>(i)];
    const float startRatio =
        static_cast<float>(juce::jlimit(0.0, 1.0, part.startSec / songLength));
    const float endRatio =
        static_cast<float>(juce::jlimit(0.0, 1.0, part.endSec / songLength));

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

    const float handleWidth = juce::jmin(kPartHandleHitWidth, body.getWidth() * 0.4f);
    PartOverlayBounds overlay;
    overlay.partIndex = i;
    overlay.body = body;
    overlay.leftHandle =
        juce::Rectangle<float>(body.getX(), body.getY(), handleWidth, body.getHeight());
    overlay.rightHandle = juce::Rectangle<float>(body.getRight() - handleWidth,
                                                 body.getY(), handleWidth,
                                                 body.getHeight());
    constexpr float deleteSize = 13.0f;
    overlay.showsDeleteButton =
        isEditingArrangement() && body.getWidth() >= (deleteSize + 16.0f) &&
        body.getHeight() >= (deleteSize + 8.0f);
    if (overlay.showsDeleteButton) {
      overlay.deleteButton = juce::Rectangle<float>(
          body.getRight() - deleteSize - 3.0f, body.getY() + 3.0f, deleteSize,
          deleteSize);
    } else {
      overlay.deleteButton = {};
    }

    overlay.showsMuteButton = false;
    overlay.showsSkipButton = false;
    overlay.showsLoopButton = false;
    overlay.showsJumpButton = false;
    overlay.showsDuplicateButton = false;

    constexpr float actionHitSize = 28.0f;
    constexpr float actionIconSize = 18.0f;
    constexpr float actionGap = 8.0f;
    const bool editingNow = isEditingArrangement();
    if (!editingNow) {
      const auto actionBounds = minimalPhoneMode
          ? body.getIntersection(
                phoneSafeAreaInsets.subtractedFrom(getLocalBounds()).toFloat())
          : body;
      constexpr int actionCount = 3;
      const float horizontalW =
          (actionHitSize * static_cast<float>(actionCount)) +
          (actionGap * static_cast<float>(actionCount - 1));
      const float verticalH =
          (actionHitSize * static_cast<float>(actionCount)) +
          (actionGap * static_cast<float>(actionCount - 1));
      const bool horizontal =
          actionBounds.getWidth() >= (horizontalW + 8.0f) &&
          actionBounds.getHeight() >= (actionHitSize + 8.0f);
      const bool vertical =
          !horizontal && actionBounds.getWidth() >= (actionHitSize + 8.0f) &&
          actionBounds.getHeight() >= (verticalH + 8.0f);
      if (!part.skipPlayback && (horizontal || vertical)) {
        overlay.showsMuteButton = true;
        overlay.showsLoopButton = true;
        overlay.showsJumpButton = true;
        if (horizontal) {
          const float y = actionBounds.getCentreY() - (actionHitSize * 0.5f);
          float x = actionBounds.getCentreX() - (horizontalW * 0.5f);
          overlay.muteButton = {x, y, actionHitSize, actionHitSize};
          x += actionHitSize + actionGap;
          overlay.loopButton = {x, y, actionHitSize, actionHitSize};
          x += actionHitSize + actionGap;
          overlay.jumpButton = {x, y, actionHitSize, actionHitSize};
        } else {
          const float x = actionBounds.getCentreX() - (actionHitSize * 0.5f);
          float y = actionBounds.getCentreY() - (verticalH * 0.5f);
          overlay.muteButton = {x, y, actionHitSize, actionHitSize};
          y += actionHitSize + actionGap;
          overlay.loopButton = {x, y, actionHitSize, actionHitSize};
          y += actionHitSize + actionGap;
          overlay.jumpButton = {x, y, actionHitSize, actionHitSize};
        }

        std::array<juce::Rectangle<float>, 3> normalButtons = {
            overlay.muteButton, overlay.loopButton, overlay.jumpButton};
        std::array<juce::Rectangle<float>, 3> targetButtons = normalButtons;
        PlaybackControlFocus activeFocus = PlaybackControlFocus::None;
        if (i == activeLoopPartIndex)
          activeFocus = PlaybackControlFocus::Loop;
        else if (i == pendingJumpPartIndex)
          activeFocus = PlaybackControlFocus::Jump;
        else if (part.muteInstruments)
          activeFocus = PlaybackControlFocus::Mute;

        if (activeFocus != PlaybackControlFocus::None) {
          if (i == playbackControlFocusPartIndex &&
              activeFocus == playbackControlFocus)
            playbackFocusStillActive = true;

          const int focusIndex =
              activeFocus == PlaybackControlFocus::Mute ? 0
              : activeFocus == PlaybackControlFocus::Loop ? 1
                                                          : 2;
          const std::array<int, 3> order =
              focusIndex == 0 ? std::array<int, 3>{1, 0, 2}
              : focusIndex == 1 ? std::array<int, 3>{0, 1, 2}
                                : std::array<int, 3>{0, 2, 1};
          for (int slot = 0; slot < 3; ++slot) {
            targetButtons[static_cast<size_t>(order[static_cast<size_t>(slot)])] =
                normalButtons[static_cast<size_t>(slot)];
          }

          double progress = 1.0;
          if (i == playbackControlFocusPartIndex &&
              activeFocus == playbackControlFocus) {
            const double elapsedMs =
                juce::Time::getMillisecondCounterHiRes() -
                playbackControlFocusChangedAtMs;
            progress = juce::jlimit(0.0, 1.0, elapsedMs / 220.0);
            progress = 1.0 - std::pow(1.0 - progress, 3.0);
          }

          const auto interpolateRect =
              [progress](const juce::Rectangle<float> &from,
                         const juce::Rectangle<float> &to) {
                return juce::Rectangle<float>(
                    static_cast<float>(from.getX() +
                                       ((to.getX() - from.getX()) * progress)),
                    static_cast<float>(from.getY() +
                                       ((to.getY() - from.getY()) * progress)),
                    from.getWidth(), from.getHeight());
              };
          overlay.muteButton = interpolateRect(normalButtons[0], targetButtons[0]);
          overlay.loopButton = interpolateRect(normalButtons[1], targetButtons[1]);
          overlay.jumpButton = interpolateRect(normalButtons[2], targetButtons[2]);
        }

        overlay.muteIcon =
            overlay.muteButton.withSizeKeepingCentre(14.0f, 14.0f);
        overlay.loopIcon =
            overlay.loopButton.withSizeKeepingCentre(actionIconSize, actionIconSize);
        overlay.jumpIcon =
            overlay.jumpButton.withSizeKeepingCentre(actionIconSize, actionIconSize);
      }
    } else {
      constexpr float editButtonSize = 24.0f;
      constexpr float editButtonGap = 4.0f;
      constexpr float editToolbarPadX = 5.0f;
      constexpr float editToolbarPadY = 4.0f;
      constexpr int editActionCount = 4;
      const float toolbarW =
          (editToolbarPadX * 2.0f) +
          (editButtonSize * static_cast<float>(editActionCount)) +
          (editButtonGap * static_cast<float>(editActionCount - 1));
      const float toolbarH = editButtonSize + (editToolbarPadY * 2.0f);
      if (body.getWidth() >= 10.0f && body.getHeight() >= (toolbarH + 30.0f) &&
          lane.getWidth() >= (toolbarW + 4.0f)) {
        float toolbarX = body.getRight() - toolbarW - 4.0f;
        if (body.getWidth() < toolbarW + 8.0f)
          toolbarX = body.getCentreX() - (toolbarW * 0.5f);
        toolbarX = juce::jlimit(lane.getX() + 2.0f,
                                lane.getRight() - toolbarW - 2.0f,
                                toolbarX);
        const float toolbarY = juce::jlimit(
            body.getY() + 22.0f, body.getBottom() - toolbarH - 4.0f,
            body.getY() + 24.0f);
        overlay.editToolbar =
            juce::Rectangle<float>(toolbarX, toolbarY, toolbarW, toolbarH);
        auto buttonArea = overlay.editToolbar.reduced(editToolbarPadX,
                                                       editToolbarPadY);
        overlay.muteButton = buttonArea.removeFromLeft(editButtonSize);
        buttonArea.removeFromLeft(editButtonGap);
        overlay.skipButton = buttonArea.removeFromLeft(editButtonSize);
        buttonArea.removeFromLeft(editButtonGap);
        overlay.duplicateButton = buttonArea.removeFromLeft(editButtonSize);
        buttonArea.removeFromLeft(editButtonGap);
        overlay.addButton = buttonArea.removeFromLeft(editButtonSize);
        overlay.muteIcon =
            overlay.muteButton.withSizeKeepingCentre(14.0f, 14.0f);
        overlay.skipIcon =
            overlay.skipButton.withSizeKeepingCentre(14.0f, 14.0f);
        overlay.duplicateIcon =
            overlay.duplicateButton.withSizeKeepingCentre(14.0f, 14.0f);
        overlay.addIcon =
            overlay.addButton.withSizeKeepingCentre(15.0f, 15.0f);
        overlay.showsMuteButton = true;
        overlay.showsSkipButton = true;
        overlay.showsDuplicateButton = true;
        overlay.showsAddButton = true;
      }
    }

    if (!overlay.showsMuteButton)
      overlay.muteButton = {};
    if (!overlay.showsLoopButton) {
      overlay.loopButton = {};
      overlay.loopIcon = {};
    }
    if (!overlay.showsJumpButton) {
      overlay.jumpButton = {};
      overlay.jumpIcon = {};
    }
    if (!overlay.showsDuplicateButton) {
      overlay.duplicateButton = {};
      overlay.duplicateIcon = {};
    }
    if (!overlay.showsSkipButton) {
      overlay.skipButton = {};
      overlay.skipIcon = {};
    }
    if (!overlay.showsAddButton) {
      overlay.addButton = {};
      overlay.addIcon = {};
    }
    if (!overlay.showsMuteButton && !editingNow) {
      overlay.muteIcon = {};
    }
    if (!overlay.showsMuteButton || !editingNow) {
      overlay.editToolbar = editingNow ? overlay.editToolbar : juce::Rectangle<float>{};
    }

    partOverlayBounds.push_back(overlay);
  }

  if (!playbackFocusStillActive) {
    playbackControlFocusPartIndex = -1;
    playbackControlFocus = PlaybackControlFocus::None;
    playbackControlFocusChangedAtMs = 0.0;
  }
}

void WaveformBar::drawWaveform(juce::Graphics &g,
                               const juce::Rectangle<float> &bounds,
                               const float windowStart, const float windowSpan,
                               const bool showGrid,
                               const bool showPlayhead) const {
  if (showPlayhead && !showGrid &&
      drawCachedFollowWaveform(g, bounds, windowStart, windowSpan)) {
    return;
  }

  drawWaveformDirect(g, bounds, windowStart, windowSpan, showGrid,
                     showPlayhead);
}

void WaveformBar::drawWaveformDirect(juce::Graphics &g,
                                     const juce::Rectangle<float> &bounds,
                                     const float windowStart,
                                     const float windowSpan,
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
  if (!minimalPhoneMode) {
    g.setColour(waveformBg);
    g.fillRoundedRectangle(bounds, 4.0f);
  }
  const double songLength = getSongLengthSeconds();

  if (showGrid) {
    if (songBpm > 0.0 && songLength > 0.0 && songBeatsPerBar > 0) {
      const double barDurationSec = getBarDurationSeconds();
      const double beatDurationSec = 60.0 / songBpm;
      const double divisionSec = getSnapStepSeconds();
      if (barDurationSec > 0.0 && beatDurationSec > 0.0 && divisionSec > 0.0) {
        const double visibleStartSec = songLength * windowStart;
        const double visibleEndSec = songLength * (windowStart + windowSpan);
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
  const bool hasCustomArrangement =
      customArrangement && !arrangement.parts.empty();
  const double sourceLength = getWaveformSourceLengthSeconds();

  const auto playbackVisual = getPlaybackVisualState();

  const float playheadRatio = juce::jlimit(
      0.0f, 1.0f,
      (playbackVisual.progressRatio - windowStart) /
          juce::jmax(0.0001f, windowSpan));
  const float playheadX = snapToPixelCenter(
      bounds.getX() + (playheadRatio * bounds.getWidth()));

  float playableStartRatio = 0.0f;
  float playableEndRatio = 1.0f;
  bool showPlayableWindow = false;
  if (lengthProvider && songWindowStartProvider &&
      songWindowEndProvider) {
    const double sourceLengthForWindow = sourceLengthProvider
                                            ? juce::jmax(0.0, sourceLengthProvider())
                                            : juce::jmax(0.0, lengthProvider());
    const double transportLength =
        hasCustomArrangement ? songLength : sourceLengthForWindow;
    if (transportLength > 0.0) {
      double startSec = songWindowStartProvider();
      double endSec = songWindowEndProvider();
      endSec = juce::jlimit(0.0, transportLength, endSec);
      startSec = juce::jlimit(0.0, endSec, startSec);
      playableStartRatio =
          static_cast<float>(juce::jlimit(0.0, 1.0, startSec / transportLength));
      playableEndRatio =
          static_cast<float>(juce::jlimit(0.0, 1.0, endSec / transportLength));
      if (hasCustomArrangement) {
        double startGuideSec = 0.0;
        showPlayableWindow = getPlaybackEndGuideSeconds(endSec) ||
                             getPlaybackStartGuideSeconds(startGuideSec);
      } else {
        showPlayableWindow =
            playableStartRatio > 0.0005f || playableEndRatio < 0.9995f;
      }
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
#if JUCE_ANDROID
  // Match cache transforms: a pixel is a cell, not an inclusive endpoint.
  // Together with the globally aligned cache origin this keeps peaks at the
  // same song coordinates when the scrolling bitmap is rebuilt.
  const double samplingWidth = juce::jmax(1.0f, bounds.getWidth());
#else
  const double samplingWidth = juce::jmax(1, widthPixels - 1);
#endif
  const double sourceSecondsPerColumn =
      songLength * static_cast<double>(windowSpan) *
      (static_cast<double>(step) / samplingWidth);
  for (int x = 0; x < widthPixels; x += step) {
    const double t = static_cast<double>(x) / samplingWidth;
    const double arrangementSec = songLength * (windowStart + (t * windowSpan));
    double sourceSec = arrangementSec;
    if (hasCustomArrangement &&
        !mapArrangementSecondsToSourceSeconds(arrangementSec, sourceLength,
                                              sourceSec))
      continue;
    if (sourceLength <= 0.0 || sourceSec < 0.0 || sourceSec > sourceLength)
      continue;

    const float amp = sampleWaveformAmplitude(
        sourceSec, sourceLength, sourceSecondsPerColumn);
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

  if (!minimalPhoneMode) {
    g.setColour(waveformBg.brighter(0.16f).withAlpha(0.9f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
  }

  if (showPlayhead) {
    drawPlayheadNeedle(g, bounds, playheadX, false, false, 1.0f);

    if (playbackVisual.showPendingTravelSeek) {
      const float pendingTravelNorm = juce::jlimit(
          0.0f, 1.0f, (playbackVisual.transportProgressRatio - windowStart) /
                           juce::jmax(0.0001f, windowSpan));
      const float pendingTravelX = snapToPixelCenter(
          bounds.getX() + (pendingTravelNorm * bounds.getWidth()));
      g.setColour(juce::Colours::white.withAlpha(0.62f));
      g.drawLine(pendingTravelX, bounds.getY(), pendingTravelX,
                 bounds.getBottom(), 1.0f);
    }
  }
}

void WaveformBar::drawWaveformHighlightMask(
    juce::Graphics &g, const juce::Rectangle<float> &bounds,
    const float windowStart, const float windowSpan) const {
  if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f ||
      waveformBins.size() < 2)
    return;

  const int widthPixels = juce::jmax(2, static_cast<int>(bounds.getWidth()));
  const float midY = bounds.getCentreY();
  const float ampScale = bounds.getHeight() * 0.495f;
  const bool hasCustomArrangement =
      customArrangement && !arrangement.parts.empty();
  const double songLength = getSongLengthSeconds();
  const double sourceLength = getWaveformSourceLengthSeconds();
  if (songLength <= 0.0 || sourceLength <= 0.0)
    return;

  g.setColour(juce::Colours::white);
  constexpr int step = 2;
  const double sourceSecondsPerColumn =
      songLength * static_cast<double>(windowSpan) *
      (static_cast<double>(step) /
       static_cast<double>(juce::jmax(1, widthPixels - 1)));
  for (int x = 0; x < widthPixels; x += step) {
    const float t = static_cast<float>(x) /
                    static_cast<float>(juce::jmax(1, widthPixels - 1));
    const double arrangementSec = songLength * (windowStart + (t * windowSpan));
    double sourceSec = arrangementSec;
    if (hasCustomArrangement &&
        !mapArrangementSecondsToSourceSeconds(arrangementSec, sourceLength,
                                              sourceSec))
      continue;
    if (sourceSec < 0.0 || sourceSec > sourceLength)
      continue;

    const float amp = sampleWaveformAmplitude(
        sourceSec, sourceLength, sourceSecondsPerColumn);
    const float lineHalf = juce::jmax(1.2f, amp * ampScale);
    const float xPos = bounds.getX() + static_cast<float>(x);
    g.drawLine(xPos, midY - lineHalf, xPos, midY + lineHalf, 1.0f);
  }
}

float WaveformBar::sampleWaveformAmplitude(
    const double sourceSec, const double sourceLength,
    const double sourceSecondsPerColumn) const {
  const int bins = static_cast<int>(waveformBins.size());
  if (bins < 2 || sourceLength <= 0.0)
    return 0.0f;

  const double sourceRatio =
      juce::jlimit(0.0, 1.0, sourceSec / sourceLength);
  const double index = sourceRatio * static_cast<double>(bins - 1);
  const double binsPerColumn =
      std::abs(sourceSecondsPerColumn) / sourceLength *
      static_cast<double>(bins - 1);

  if (binsPerColumn <= 1.0) {
    const int i0 = juce::jlimit(0, bins - 1, static_cast<int>(index));
    const int i1 = juce::jmin(i0 + 1, bins - 1);
    const float fraction = static_cast<float>(index - static_cast<double>(i0));
    return juce::jmap(fraction, waveformBins[static_cast<size_t>(i0)],
                      waveformBins[static_cast<size_t>(i1)]);
  }

  const double halfRange = binsPerColumn * 0.5;
  const int first = juce::jlimit(
      0, bins - 1, static_cast<int>(std::floor(index - halfRange)));
  const int last = juce::jlimit(
      first, bins - 1, static_cast<int>(std::ceil(index + halfRange)));
  float peak = 0.0f;
  for (int i = first; i <= last; ++i)
    peak = juce::jmax(peak, waveformBins[static_cast<size_t>(i)]);
  return peak;
}

bool WaveformBar::drawCachedFollowWaveform(
    juce::Graphics &g, const juce::Rectangle<float> &bounds,
    const float windowStart, const float windowSpan) const {
#if JUCE_ANDROID
  const bool mayUsePlaybackCache = playingProvider != nullptr && playingProvider();
#else
  const bool mayUsePlaybackCache = isFollowTrackingActive();
#endif
  if (!mayUsePlaybackCache || waveformBins.size() < 2 ||
      bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f ||
      windowSpan <= 0.0f)
    return false;

  // A restricted playback window needs its darkened region composited after
  // the waveform. This also applies to a manual end trim in an arrangement.
  double playbackEndGuideSeconds = 0.0;
  if (getPlaybackEndGuideSeconds(playbackEndGuideSeconds))
    return false;
  double playbackStartGuideSeconds = 0.0;
  if (getPlaybackStartGuideSeconds(playbackStartGuideSeconds))
    return false;
  if (!customArrangement && lengthProvider != nullptr &&
      songWindowStartProvider != nullptr && songWindowEndProvider != nullptr) {
    const double length = juce::jmax(0.0, lengthProvider());
    if (length > 0.0) {
      const double start = juce::jlimit(0.0, length, songWindowStartProvider());
      const double end = juce::jlimit(start, length, songWindowEndProvider());
      if (start > 0.0001 || end < length - 0.0001)
        return false;
    }
  }

#if JUCE_ANDROID
  const auto cacheWindow = playworship::ui::makeWaveformCacheWindow(
      windowStart, windowSpan, bounds.getWidth());
  const float cacheSpan = static_cast<float>(cacheWindow.span);
  const float cacheStart = static_cast<float>(cacheWindow.start);
#else
  const float cacheSpan = juce::jlimit(
      windowSpan, 1.0f, windowSpan * kFollowWaveformCacheOverscan);
  const float cacheStart = juce::jlimit(
      0.0f, 1.0f - cacheSpan,
      windowStart - ((cacheSpan - windowSpan) * 0.5f));
#endif
#if JUCE_ANDROID
  // Android's JUCE peer transforms this bitmap on the CPU before upload. Keep
  // it at logical resolution; the display compositor performs final scaling.
  constexpr float cacheScale = 1.0f;
#else
  const float cacheScale = juce::jlimit(
      1.0f, 4.0f, juce::Component::getApproximateScaleFactorForComponent(this));
#endif
#if JUCE_ANDROID
  const int cacheWidth = cacheWindow.width;
#else
  const int cacheWidth = static_cast<int>(
      std::ceil(bounds.getWidth() * (cacheSpan / windowSpan) * cacheScale));
#endif
  const int cacheHeight =
      static_cast<int>(std::ceil(bounds.getHeight() * cacheScale));
  if (cacheWidth <= 2 || cacheHeight <= 2)
    return false;

  const float cachedEnd = followWaveformCacheStart + followWaveformCacheSpan;
  const float windowEnd = windowStart + windowSpan;
  const bool cacheContainsWindow =
      followWaveformBaseCache.isValid() &&
      windowStart >= (followWaveformCacheStart -
                      kFollowViewportPositionEpsilon) &&
      windowEnd <= (cachedEnd + kFollowViewportPositionEpsilon);
  const bool cacheNeedsRebuild =
      !followWaveformBaseCache.isValid() ||
#if !JUCE_ANDROID
      !followWaveformHighlightMaskCache.isValid() ||
#endif
      followWaveformCacheWidth != cacheWidth ||
      followWaveformCacheHeight != cacheHeight ||
      !nearlyEqual(followWaveformCacheSpan, cacheSpan,
                   kFollowViewportPositionEpsilon) ||
      !nearlyEqual(followWaveformCacheScale, cacheScale,
                   kFollowViewportPositionEpsilon) ||
      !cacheContainsWindow;
  if (cacheNeedsRebuild) {
    followWaveformBaseCache =
        juce::Image(juce::Image::ARGB, cacheWidth, cacheHeight, true);
#if !JUCE_ANDROID
    followWaveformHighlightMaskCache =
        juce::Image(juce::Image::SingleChannel, cacheWidth, cacheHeight, true);
#endif

    const auto cacheBounds = juce::Rectangle<float>(
        0.0f, 0.0f, static_cast<float>(cacheWidth) / cacheScale,
        static_cast<float>(cacheHeight) / cacheScale);
    juce::Graphics cacheGraphics(followWaveformBaseCache);
    cacheGraphics.addTransform(juce::AffineTransform::scale(cacheScale));
    drawWaveformDirect(cacheGraphics, cacheBounds, cacheStart, cacheSpan, false,
                       false);

#if !JUCE_ANDROID
    juce::Graphics highlightMaskGraphics(followWaveformHighlightMaskCache);
    highlightMaskGraphics.addTransform(
        juce::AffineTransform::scale(cacheScale));
    drawWaveformHighlightMask(highlightMaskGraphics, cacheBounds, cacheStart,
                              cacheSpan);
#endif

    followWaveformCacheWidth = cacheWidth;
    followWaveformCacheHeight = cacheHeight;
    followWaveformCacheStart = cacheStart;
    followWaveformCacheSpan = cacheSpan;
    followWaveformCacheScale = cacheScale;
  }

  const float sourceX =
      ((windowStart - followWaveformCacheStart) / followWaveformCacheSpan) *
      static_cast<float>(cacheWidth);
  const float sourceWidth =
      (windowSpan / followWaveformCacheSpan) * static_cast<float>(cacheWidth);
  if (sourceWidth <= 0.0f)
    return false;

  const float scaleX = bounds.getWidth() / sourceWidth;
  const float scaleY = bounds.getHeight() /
                       static_cast<float>(followWaveformCacheHeight);
  const auto sourceToBounds = juce::AffineTransform(
      scaleX, 0.0f, bounds.getX() - (sourceX * scaleX), 0.0f, scaleY,
      bounds.getY());
  const auto clipBounds = bounds.toNearestInt();
  g.saveState();
  g.reduceClipRegion(clipBounds);
  g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
  g.drawImageTransformed(followWaveformBaseCache, sourceToBounds);
  g.restoreState();

  const auto playbackVisual = getPlaybackVisualState();
  const float playheadRatio = juce::jlimit(
      0.0f, 1.0f,
      (playbackVisual.progressRatio - windowStart) /
          juce::jmax(0.0001f, windowSpan));
  const float playheadX = snapToPixelCenter(
      bounds.getX() + (playheadRatio * bounds.getWidth()));

  const int playedRight = static_cast<int>(std::floor(playheadX));
  if (playedRight > clipBounds.getX()) {
#if !JUCE_ANDROID
    g.saveState();
    g.reduceClipRegion(clipBounds.withRight(
        juce::jmin(clipBounds.getRight(), playedRight)));
    // Base waveform alpha is 0.70. This alpha raises only the waveform mask
    // to the existing played-state brightness (~0.92) without tinting the bg.
    g.setColour(juce::Colours::white.withAlpha(0.74f));
    g.drawImageTransformed(followWaveformHighlightMaskCache, sourceToBounds,
                           true);
    g.restoreState();
#endif
  }

  const auto waveformBg = juce::Colour::fromRGB(0x1E, 0x1E, 0x1E);
  g.setColour(waveformBg.brighter(0.16f).withAlpha(0.9f));
  g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
  drawPlayheadNeedle(g, bounds, playheadX, false, false, 1.0f);

  if (playbackVisual.showPendingTravelSeek) {
    const float pendingTravelNorm = juce::jlimit(
        0.0f, 1.0f,
        (playbackVisual.transportProgressRatio - windowStart) /
            juce::jmax(0.0001f, windowSpan));
    const float pendingTravelX = snapToPixelCenter(
        bounds.getX() + (pendingTravelNorm * bounds.getWidth()));
    g.setColour(juce::Colours::white.withAlpha(0.62f));
    g.drawLine(pendingTravelX, bounds.getY(), pendingTravelX,
               bounds.getBottom(), 1.0f);
  }

  return true;
}

void WaveformBar::drawPlaybackWindowGuides(
    juce::Graphics &g, const juce::Rectangle<float> &bounds,
    const float windowStart, const float windowSpan) const {
  double endSeconds = 0.0;
  double startSeconds = 0.0;
  const bool hasEndGuide = getPlaybackEndGuideSeconds(endSeconds);
  const bool hasStartGuide = getPlaybackStartGuideSeconds(startSeconds);
  if ((!hasEndGuide && !hasStartGuide) || bounds.getWidth() <= 2.0f ||
      bounds.getHeight() <= 2.0f)
    return;

  const double timelineLength = getSongLengthSeconds();
  if (timelineLength <= 0.0)
    return;

  const float dashPattern[] = {5.0f, 4.0f};
  const auto drawGuide = [&](const double seconds,
                             const juce::Colour colour) {
    const float ratio = static_cast<float>(juce::jlimit(
        0.0, 1.0, seconds / timelineLength));
    const float norm = juce::jlimit(
        0.0f, 1.0f,
        (ratio - windowStart) / juce::jmax(0.0001f, windowSpan));
    const float x = snapToPixelCenter(
        bounds.getX() + (norm * bounds.getWidth()));
    g.setColour(juce::Colours::black.withAlpha(0.78f));
    g.drawDashedLine({x - 1.0f, bounds.getY(), x - 1.0f, bounds.getBottom()},
                     dashPattern, 2, 3.0f);
    g.setColour(colour.withAlpha(0.96f));
    g.drawDashedLine({x, bounds.getY(), x, bounds.getBottom()}, dashPattern, 2,
                     1.5f);
  };

  if (hasStartGuide)
    drawGuide(startSeconds, juce::Colours::deepskyblue);
  if (hasEndGuide)
    drawGuide(endSeconds, UITheme::accent.brighter(0.20f));
}

void WaveformBar::drawCachedMiniMapWaveform(
    juce::Graphics &g, const juce::Rectangle<float> &bounds) const {
  const int cacheWidth = static_cast<int>(std::ceil(bounds.getWidth()));
  const int cacheHeight = static_cast<int>(std::ceil(bounds.getHeight()));
  if (cacheWidth <= 2 || cacheHeight <= 2)
    return;

  const bool cacheNeedsRebuild = !miniMapWaveformCache.isValid() ||
                                 miniMapWaveformCacheWidth != cacheWidth ||
                                 miniMapWaveformCacheHeight != cacheHeight;
  if (cacheNeedsRebuild) {
    miniMapWaveformCache =
        juce::Image(juce::Image::ARGB, cacheWidth, cacheHeight, true);
    juce::Graphics cacheGraphics(miniMapWaveformCache);
    drawWaveformDirect(
        cacheGraphics,
        {0.0f, 0.0f, static_cast<float>(cacheWidth),
         static_cast<float>(cacheHeight)},
        0.0f, 1.0f, false, false);
    miniMapWaveformCacheWidth = cacheWidth;
    miniMapWaveformCacheHeight = cacheHeight;
  }

  g.drawImageAt(miniMapWaveformCache, static_cast<int>(std::round(bounds.getX())),
                static_cast<int>(std::round(bounds.getY())));
}

void WaveformBar::drawWaveformSegment(juce::Graphics &g,
                                      const juce::Rectangle<float> &bounds,
                                      const double sourceStartSec,
                                      const double sourceEndSec) const {
  if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f)
    return;
  if (waveformBins.size() < 2)
    return;

  const double sourceLength = getWaveformSourceLengthSeconds();
  if (sourceLength <= 0.0 || sourceEndSec <= sourceStartSec)
    return;

  const float midY = bounds.getCentreY();
  const float ampScale = bounds.getHeight() * 0.495f;
  const int widthPixels = juce::jmax(2, static_cast<int>(bounds.getWidth()));

  g.setColour(juce::Colours::white.withAlpha(0.16f));
  const float centerDashes[] = {2.0f, 4.0f};
  g.drawDashedLine({bounds.getX(), midY, bounds.getRight(), midY},
                   centerDashes, 2, 1.0f);

  constexpr int step = 2;
  const double sourceDuration = sourceEndSec - sourceStartSec;
  const double sourceSecondsPerColumn =
      sourceDuration *
      (static_cast<double>(step) /
       static_cast<double>(juce::jmax(1, widthPixels - 1)));
  for (int x = 0; x < widthPixels; x += step) {
    const float t = static_cast<float>(x) /
                    static_cast<float>(juce::jmax(1, widthPixels - 1));
    const double sourceSec = sourceStartSec + (static_cast<double>(t) * sourceDuration);
    const float amp = sampleWaveformAmplitude(
        sourceSec, sourceLength, sourceSecondsPerColumn);
    const float lineHalf = juce::jmax(1.2f, amp * ampScale);
    const float xPos = bounds.getX() + static_cast<float>(x);

    g.setColour(juce::Colours::white.withAlpha(0.72f));
    g.drawLine(xPos, midY - lineHalf, xPos, midY + lineHalf, 1.0f);
  }
}

void WaveformBar::drawSeekRuler(juce::Graphics &g,
                                const juce::Rectangle<float> &bounds,
                                const float windowStart,
                                const float windowSpan) const {
  if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f)
    return;

  const double songLength = getSongLengthSeconds();
  if (songLength > 0.0 && windowSpan > 0.0f) {
    const juce::Range<double> visible{
        songLength * windowStart, songLength * (windowStart + windowSpan)};
    drawMusicalTimelineRuler(g, bounds, visible, songLength,
                             getPracticeTimelineMarks(visible, bounds.getWidth()));
  }

  if (positionProvider && lengthProvider) {
    const double length = juce::jmax(0.0, lengthProvider());
    if (length > 0.0) {
      double position = juce::jmax(0.0, positionProvider());
      double pendingTravelPosition = position;
      bool showPendingTravelSeek = false;
      if (followPlaybackEnabled && !isEditingArrangement() &&
          playingProvider != nullptr && playingProvider()) {
        float pendingRatio = 0.0f;
        if (getPendingJumpProgressRatio(pendingRatio)) {
          position = static_cast<double>(pendingRatio) * length;
          showPendingTravelSeek = true;
        }
      }
      const float progress =
          static_cast<float>(juce::jlimit(0.0, 1.0, position / length));
      const float playheadNorm =
          juce::jlimit(0.0f, 1.0f, (progress - windowStart) /
                                         juce::jmax(0.0001f, windowSpan));
      const float x = snapToPixelCenter(
          bounds.getX() + playheadNorm * bounds.getWidth());
      drawPlayheadNeedle(g, bounds.reduced(0.0f, 0.4f), x, false, false,
                         0.96f);

      if (showPendingTravelSeek) {
        const float pendingTravelRatio = static_cast<float>(
            juce::jlimit(0.0, 1.0, pendingTravelPosition / length));
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

void WaveformBar::drawSectionInsertionAnchor(
    juce::Graphics &g, const juce::Rectangle<float> &waveBounds,
    const juce::Rectangle<float> &rulerBounds, const float windowStart,
    const float windowSpan) const {
  if (!sectionInsertionAnchorValid || !isEditingArrangement() ||
      timelineViewMode != TimelineViewMode::GuideMap ||
      waveBounds.getWidth() <= 2.0f)
    return;

  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return;

  const float anchorRatio = static_cast<float>(
      juce::jlimit(0.0, 1.0, sectionInsertionAnchorSec / songLength));
  const float visibleEnd = windowStart + windowSpan;
  if (anchorRatio < windowStart || anchorRatio > visibleEnd)
    return;

  const float localRatio =
      (anchorRatio - windowStart) / juce::jmax(0.0001f, windowSpan);
  const float x = snapToPixelCenter(
      waveBounds.getX() + (localRatio * waveBounds.getWidth()));
  const float top = rulerBounds.getHeight() > 2.0f ? rulerBounds.getY()
                                                   : waveBounds.getY();
  const float dashLengths[] = {4.0f, 3.0f};

  g.setColour(juce::Colours::black.withAlpha(0.42f));
  g.drawLine(x + 1.0f, top, x + 1.0f, waveBounds.getBottom(), 2.4f);
  g.setColour(UITheme::accentSoft.withAlpha(0.92f));
  g.drawDashedLine(juce::Line<float>(x, top, x, waveBounds.getBottom()),
                   dashLengths, 2, 1.35f);
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
    const float baseAlpha = editing ? (selected ? 0.28f : 0.18f) : 0.16f;
    auto fill = sectionColour.withAlpha(baseAlpha);

    // Clip to section body so we only affect the waveform inside it.
    g.saveState();
    g.reduceClipRegion(overlay.body.toNearestInt());

    // Subtle section colour tint overlay (matches the original behaviour so
    // duplicated sections look identical except for the dashed border + link).
    g.setColour(fill);
    g.fillRoundedRectangle(overlay.body, 2.5f);

    if (part.muteInstruments && !part.skipPlayback) {
      g.setColour(juce::Colours::black.withAlpha(editing ? 0.24f : 0.30f));
      g.fillRoundedRectangle(overlay.body, 2.5f);
    }

    if (part.skipPlayback) {
      g.setColour(sectionColour.withAlpha(editing ? 0.18f : 0.14f));
      g.fillRoundedRectangle(overlay.body, 2.5f);
      g.setColour(juce::Colours::black.withAlpha(editing ? 0.24f : 0.18f));
      const float spacing = 9.0f;
      const float startX = overlay.body.getX() - overlay.body.getHeight();
      const float endX = overlay.body.getRight() + overlay.body.getHeight();
      for (float x = startX; x < endX; x += spacing) {
        g.drawLine(x, overlay.body.getBottom(), x + overlay.body.getHeight(),
                   overlay.body.getY(), 1.0f);
      }
    }

    g.restoreState();

    // Visual indicator for duplicated (reference) sections: dashed right/top/bottom edges.
    if (part.sourcePartId.isNotEmpty()) {
      g.setColour(sectionColour.withAlpha(0.55f));
      const float dashPattern[] = {3.0f, 3.0f};
      // Right edge
      g.drawDashedLine(juce::Line<float>(overlay.body.getRight(), overlay.body.getY() + 3.0f,
                                         overlay.body.getRight(), overlay.body.getBottom() - 3.0f),
                      dashPattern, 2, 1.5f);
      // Top edge
      g.drawDashedLine(juce::Line<float>(overlay.body.getX() + 3.0f, overlay.body.getY(),
                                         overlay.body.getRight() - 3.0f, overlay.body.getY()),
                      dashPattern, 2, 1.5f);
      // Bottom edge
      g.drawDashedLine(juce::Line<float>(overlay.body.getX() + 3.0f, overlay.body.getBottom(),
                                         overlay.body.getRight() - 3.0f, overlay.body.getBottom()),
                      dashPattern, 2, 1.5f);
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
    g.setColour(sectionColour.withAlpha(separatorAlpha));
    g.drawLine(separatorX, lane.getY() + 2.0f, separatorX,
               lane.getBottom() - 2.0f, separatorThickness);
    auto partName = arrangementPartDisplayLabelForName(part.name);
    if (const auto utilityLabel = utilityCueDisplayLabelForName(part.name);
        utilityLabel.isNotEmpty()) {
      partName = utilityLabel;
    }

    const auto labelBounds = minimalPhoneMode
        ? overlay.body.getIntersection(
              phoneSafeAreaInsets.subtractedFrom(getLocalBounds()).toFloat())
        : overlay.body;
    auto textArea = labelBounds.toNearestInt().reduced(6, 4);
    if (labelBounds.getWidth() >= 18.0f && textArea.getHeight() >= 13) {
      const bool useAbbreviation = labelBounds.getWidth() < 54.0f;
      const auto visibleName =
          useAbbreviation ? abbreviatedArrangementLabel(partName) : partName;
      const auto labelFont = makeAppFont(useAbbreviation ? 9.2f : 10.6f,
                                         AppFontWeight::SemiBold);
      const float textWidth = labelFont.getStringWidthFloat(visibleName);
      const float maxFlagWidth = juce::jmax(16.0f, labelBounds.getWidth() - 2.0f);
      const float flagWidth = juce::jlimit(16.0f, maxFlagWidth, textWidth + 16.0f);
      auto flag = juce::Rectangle<float>(labelBounds.getX(), labelBounds.getY() + 3.0f,
                                         flagWidth, 16.0f);
      if (flag.getRight() > labelBounds.getRight() - 1.0f)
        flag.setRight(labelBounds.getRight() - 1.0f);

      const auto labelFill = sectionColour.withAlpha(highlighted ? 0.96f : 0.88f);
      g.setColour(juce::Colours::black.withAlpha(0.28f));
      g.fillRect(flag.translated(0.8f, 1.0f));
      g.setColour(labelFill);
      g.fillRect(flag);
      g.setColour(sectionColour.brighter(0.18f).withAlpha(highlighted ? 0.95f : 0.72f));
      g.drawRect(flag, highlighted ? 1.2f : 0.8f);
      g.setColour(juce::Colours::black.withAlpha(0.48f));
      g.drawText(visibleName, flag.translated(0.7f, 0.8f).toNearestInt().reduced(4, 0),
                 juce::Justification::centred, false);
      g.setColour(juce::Colours::white.withAlpha(0.96f));
      g.setFont(labelFont);
      g.drawFittedText(visibleName, flag.toNearestInt().reduced(4, 0),
                       juce::Justification::centred, 1);

    }

    const bool showsSectionActions = hoveredActionPartIndex == overlay.partIndex;
    const auto drawMuteTextButton =
        [&g](const juce::Rectangle<float> &button, const bool hovered,
             const bool active, const juce::Colour activeColour) {
          const auto bubbleColour =
              active ? activeColour.withAlpha(hovered ? 0.90f : 0.70f)
                     : juce::Colours::black.withAlpha(hovered ? 0.30f : 0.18f);
          g.setColour(juce::Colours::black.withAlpha(hovered ? 0.18f : 0.10f));
          g.fillEllipse(button.reduced(1.0f).translated(0.0f, 1.0f));
          g.setColour(bubbleColour);
          g.fillEllipse(button.reduced(hovered ? 4.0f : 5.0f));
          g.setColour(juce::Colours::white.withAlpha((hovered || active) ? 0.96f
                                                                         : 0.64f));
          g.setFont(makeAppFont(11.4f, AppFontWeight::Bold));
          g.drawText("M", button.toNearestInt(), juce::Justification::centred,
                     false);
        };
    if ((showsSectionActions || part.muteInstruments) &&
        overlay.showsMuteButton && !editing) {
      drawMuteTextButton(overlay.muteButton,
                         hoveredMutePartIndex == overlay.partIndex,
                         part.muteInstruments, UITheme::accent);
    }

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

    if (part.skipPlayback && overlay.body.getWidth() >= 28.0f &&
        overlay.body.getHeight() >= 28.0f) {
      const auto button =
          overlay.body.withSizeKeepingCentre(28.0f, 28.0f);
      const auto icon = button.withSizeKeepingCentre(17.0f, 17.0f);
      g.setColour(juce::Colours::black.withAlpha(0.30f));
      g.fillEllipse(button.reduced(1.0f).translated(0.0f, 1.0f));
      g.setColour(sectionColour.withAlpha(0.74f));
      g.fillEllipse(button.reduced(4.5f));
      drawSvgIcon(g, icon, SvgIconName::SkipForward,
                  juce::Colours::white.withAlpha(0.94f));
    }

    const bool editToolbarVisible =
        editing && hoveredActionPartIndex == overlay.partIndex &&
        !overlay.editToolbar.isEmpty();
    if (editToolbarVisible) {
      const bool toolbarFocused =
          part.muteInstruments || part.skipPlayback ||
          hoveredMutePartIndex == overlay.partIndex ||
          hoveredSkipPartIndex == overlay.partIndex ||
          hoveredDuplicatePartIndex == overlay.partIndex ||
          hoveredAddPartIndex == overlay.partIndex;
      g.setColour(juce::Colours::black.withAlpha(toolbarFocused ? 0.36f : 0.26f));
      g.fillRoundedRectangle(overlay.editToolbar.translated(0.0f, 1.2f), 8.0f);
      g.setColour(UITheme::cardBg.withAlpha(toolbarFocused ? 0.86f : 0.72f));
      g.fillRoundedRectangle(overlay.editToolbar, 8.0f);
      g.setColour(sectionColour.withAlpha(toolbarFocused ? 0.58f : 0.34f));
      g.drawRoundedRectangle(overlay.editToolbar, 8.0f, 1.0f);

      const auto drawToolbarButton =
          [&g](const juce::Rectangle<float> &button,
               const juce::Rectangle<float> &icon, const SvgIconName iconName,
               const bool hovered, const bool active,
               const juce::Colour activeColour) {
        const auto buttonFill =
            active ? activeColour.withAlpha(hovered ? 0.90f : 0.70f)
                   : juce::Colours::white.withAlpha(hovered ? 0.18f : 0.08f);
        g.setColour(buttonFill);
        g.fillRoundedRectangle(button.reduced(1.0f), 6.0f);
        drawSvgIcon(g, icon, iconName,
                    juce::Colours::white.withAlpha((active || hovered) ? 0.96f
                                                                       : 0.68f));
      };

      const bool muteHovered = hoveredMutePartIndex == overlay.partIndex;
      const auto muteFill =
          part.muteInstruments
              ? UITheme::accent.withAlpha(muteHovered ? 0.90f : 0.70f)
              : juce::Colours::white.withAlpha(muteHovered ? 0.18f : 0.08f);
      g.setColour(muteFill);
      g.fillRoundedRectangle(overlay.muteButton.reduced(1.0f), 6.0f);
      g.setColour(juce::Colours::white.withAlpha(
          (part.muteInstruments || muteHovered) ? 0.96f : 0.68f));
      g.setFont(makeAppFont(11.0f, AppFontWeight::Bold));
      g.drawText("M", overlay.muteButton.toNearestInt(),
                 juce::Justification::centred, false);
      drawToolbarButton(overlay.skipButton, overlay.skipIcon, SvgIconName::SkipForward,
                        hoveredSkipPartIndex == overlay.partIndex,
                        part.skipPlayback, sectionColour);
      drawToolbarButton(overlay.duplicateButton, overlay.duplicateIcon,
                        SvgIconName::Copy,
                        hoveredDuplicatePartIndex == overlay.partIndex,
                        false, sectionColour);
      drawToolbarButton(overlay.addButton, overlay.addIcon, SvgIconName::Plus,
                        hoveredAddPartIndex == overlay.partIndex, false,
                        sectionColour);
    }

    if (editing) {
      textArea.removeFromTop(17);
      g.setFont(juce::FontOptions(10.2f));
      g.drawFittedText(L10n::tr("Inicio ", "Start ") + formatSeconds(part.startSec),
                       textArea.removeFromTop(12), juce::Justification::topLeft, 1);
      g.drawFittedText(L10n::tr("Fin ", "End ") + formatSeconds(part.endSec),
                       textArea.removeFromTop(12), juce::Justification::topLeft, 1);
    }

    if (editing && overlay.showsDeleteButton) {
      g.setColour(juce::Colours::white.withAlpha(0.16f));
      g.fillRoundedRectangle(overlay.deleteButton, 2.0f);
      g.setColour(juce::Colours::white.withAlpha(0.92f));
      const auto db = overlay.deleteButton.reduced(3.2f, 3.2f);
      g.drawLine(db.getX(), db.getY(), db.getRight(), db.getBottom(), 1.2f);
      g.drawLine(db.getRight(), db.getY(), db.getX(), db.getBottom(), 1.2f);
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

  drawCachedMiniMapWaveform(g, miniMapBounds);

  const double songLength = getSongLengthSeconds();
  if (songLength > 0.0) {
    for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
      const auto &part = arrangement.parts[static_cast<size_t>(i)];
      const float x = miniMapBounds.getX() +
                      static_cast<float>(
                          juce::jlimit(0.0, 1.0, part.startSec / songLength)) *
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
      double pendingTravelPosition = current;
      bool showPendingTravelSeek = false;
      if (followPlaybackEnabled && !isEditingArrangement() &&
          playingProvider != nullptr && playingProvider()) {
        float pendingRatio = 0.0f;
        if (getPendingJumpProgressRatio(pendingRatio)) {
          current = static_cast<double>(pendingRatio) * length;
          showPendingTravelSeek = true;
        }
      }
      const float progress =
          static_cast<float>(juce::jlimit(0.0, 1.0, current / length));
      const float x = snapToPixelCenter(
          miniMapBounds.getX() + progress * miniMapBounds.getWidth());
      drawPlayheadNeedle(g, miniMapBounds, x, false, false, 0.95f);

      if (showPendingTravelSeek) {
        const float pendingTravelRatio = static_cast<float>(
            juce::jlimit(0.0, 1.0, pendingTravelPosition / length));
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
    btn->setTooltip(
        L10n::tr("Agregar en la marca de la regla o al final: ",
                 "Add at the ruler marker or at the end: ") +
        displayName);
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
      presentActionPopover(std::move(panel), anchor, parentComp);
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
    presentActionPopover(std::move(panel), anchor, parentComp);
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
    presentActionPopover(std::move(panel), anchor, parentComp);
  };
  addAndMakeVisible(*utilityBtn);
  utilityBtn->setVisible(isEditingArrangement());
  sectionAddButtons.push_back(std::move(utilityBtn));
}

void WaveformBar::clearSectionInsertionAnchor() {
  if (!sectionInsertionAnchorValid)
    return;

  sectionInsertionAnchorValid = false;
  sectionInsertionAnchorSec = 0.0;
  resized();
  repaint();
}

bool WaveformBar::addArrangementPartAtInsertionAnchor(
    const juce::String &name, const double playableStartSec,
    const double playableEndSec, const double preferredDurationSec,
    const bool utilityCue) {
  if (!sectionInsertionAnchorValid)
    return false;

  constexpr double kBoundaryToleranceSec = 0.0001;
  const bool usePreferredDuration =
      preferredDurationSec >= kMinPartDurationSec;
  double insertionSec = juce::jlimit(
      playableStartSec, playableEndSec, snapSeconds(sectionInsertionAnchorSec));
  const auto colourForName = [](const juce::String &partName) {
    auto colour = standardArrangementPartColorForName(partName);
    if (colour.isEmpty())
      colour = fallbackArrangementPartColorForName(partName);
    return colour;
  };

  // Keep both sides of a split valid. A point very close to an existing edge
  // is treated as that edge, which also makes ruler placement feel magnetic.
  double nearestBoundaryDistance = kMinPartDurationSec;
  for (const auto &part : arrangement.parts) {
    for (const double boundarySec : {part.startSec, part.endSec}) {
      const double distance = std::abs(boundarySec - insertionSec);
      if (distance <= nearestBoundaryDistance) {
        insertionSec = boundarySec;
        nearestBoundaryDistance = distance;
      }
    }
  }

  // If a section already begins on the marked boundary, marking that point
  // means relabelling the existing section rather than creating an overlap.
  if (!usePreferredDuration) {
    for (int i = static_cast<int>(arrangement.parts.size()) - 1; i >= 0; --i) {
      auto &part = arrangement.parts[static_cast<size_t>(i)];
      if (std::abs(part.startSec - insertionSec) > kBoundaryToleranceSec)
        continue;

      pushArrangementUndoSnapshot();
      const auto partId = part.id;
      part.name = name;
      part.color = colourForName(name);
      customArrangement = true;
      clearSectionInsertionAnchor();
      notifyArrangementChanged(true);
      const int updatedIndex = findPartIndexById(partId);
      if (juce::isPositiveAndBelow(updatedIndex,
                                   static_cast<int>(arrangement.parts.size())))
        setSingleSelectedPart(updatedIndex);
      repaint();
      return true;
    }
  }

  // A point inside a section is a new boundary. Split the range so the
  // scheduler never receives two sections covering the same time.
  for (int i = static_cast<int>(arrangement.parts.size()) - 1; i >= 0; --i) {
    const auto original = arrangement.parts[static_cast<size_t>(i)];
    if ((!usePreferredDuration &&
         insertionSec <= original.startSec + kBoundaryToleranceSec) ||
        (usePreferredDuration &&
         insertionSec < original.startSec - kBoundaryToleranceSec) ||
        insertionSec >= original.endSec - kBoundaryToleranceSec)
      continue;

    const double leftDuration = insertionSec - original.startSec;
    const double rightDuration = original.endSec - insertionSec;
    if ((!usePreferredDuration && leftDuration < kMinPartDurationSec) ||
        rightDuration < kMinPartDurationSec)
      return false;

    if (usePreferredDuration) {
      double insertedEndSec =
          juce::jmin(original.endSec, insertionSec + preferredDurationSec);
      if ((original.endSec - insertedEndSec) < kMinPartDurationSec)
        insertedEndSec = original.endSec;
      if ((insertedEndSec - insertionSec) < kMinPartDurationSec)
        return false;

      const bool keepLeft =
          leftDuration >= kMinPartDurationSec;
      const bool keepRight =
          (original.endSec - insertedEndSec) >= kMinPartDurationSec;
      const double sourceLength = getWaveformSourceLengthSeconds();
      const auto sourceRange = getPartSourceRange(original, sourceLength);
      const double sourceStartSec = sourceRange.first;
      const double sourceEndSec = sourceRange.second;
      const double originalDuration =
          juce::jmax(kMinPartDurationSec, original.endSec - original.startSec);
      const auto sourceAtArrangementTime =
          [&](const double arrangementSec) {
            const double ratio = juce::jlimit(
                0.0, 1.0,
                (arrangementSec - original.startSec) / originalDuration);
            return sourceStartSec +
                   ((sourceEndSec - sourceStartSec) * ratio);
          };
      const auto setExplicitSourceRange =
          [&](SongArrangementPartSnapshot &part) {
            if (original.sourcePartId.isEmpty())
              return;
            part.sourceStartSec = sourceAtArrangementTime(part.startSec);
            part.sourceEndSec = sourceAtArrangementTime(part.endSec);
          };

      std::vector<SongArrangementPartSnapshot> replacements;
      replacements.reserve(3);
      if (keepLeft) {
        auto leftPart = original;
        leftPart.endSec = insertionSec;
        setExplicitSourceRange(leftPart);
        replacements.push_back(std::move(leftPart));
      }

      auto insertedPart = original;
      insertedPart.id = (!keepLeft && !keepRight)
                            ? original.id
                            : juce::Uuid().toString();
      insertedPart.name = name;
      insertedPart.color = colourForName(name);
      insertedPart.startSec = insertionSec;
      insertedPart.endSec = insertedEndSec;
      insertedPart.utilityCue = utilityCue;
      setExplicitSourceRange(insertedPart);
      const auto insertedPartId = insertedPart.id;
      replacements.push_back(std::move(insertedPart));

      if (keepRight) {
        auto trailingPart = original;
        if (keepLeft)
          trailingPart.id = juce::Uuid().toString();
        trailingPart.startSec = insertedEndSec;
        setExplicitSourceRange(trailingPart);
        replacements.push_back(std::move(trailingPart));
      }

      pushArrangementUndoSnapshot();
      auto replaceAt = arrangement.parts.begin() + i;
      replaceAt = arrangement.parts.erase(replaceAt);
      arrangement.parts.insert(replaceAt, replacements.begin(),
                               replacements.end());
      customArrangement = true;
      clearSectionInsertionAnchor();
      notifyArrangementChanged(true);
      const int insertedIndex = findPartIndexById(insertedPartId);
      if (juce::isPositiveAndBelow(
              insertedIndex, static_cast<int>(arrangement.parts.size())))
        setSingleSelectedPart(insertedIndex);
      repaint();
      return true;
    }

    SongArrangementPartSnapshot rightPart = original;
    rightPart.id = juce::Uuid().toString();
    rightPart.name = name;
    rightPart.color = colourForName(name);
    rightPart.startSec = insertionSec;
    const auto rightPartId = rightPart.id;

    pushArrangementUndoSnapshot();
    auto &leftPart = arrangement.parts[static_cast<size_t>(i)];
    leftPart.endSec = insertionSec;

    if (original.sourcePartId.isNotEmpty()) {
      const double sourceLength = getWaveformSourceLengthSeconds();
      const auto [sourceStartSec, sourceEndSec] =
          getPartSourceRange(original, sourceLength);
      const double splitRatio = juce::jlimit(
          0.0, 1.0, leftDuration / juce::jmax(kMinPartDurationSec,
                                               original.endSec - original.startSec));
      const double sourceSplitSec =
          sourceStartSec + ((sourceEndSec - sourceStartSec) * splitRatio);
      leftPart.sourceStartSec = sourceStartSec;
      leftPart.sourceEndSec = sourceSplitSec;
      rightPart.sourceStartSec = sourceSplitSec;
      rightPart.sourceEndSec = sourceEndSec;
    }

    arrangement.parts.push_back(std::move(rightPart));
    customArrangement = true;
    clearSectionInsertionAnchor();
    notifyArrangementChanged(true);
    const int insertedIndex = findPartIndexById(rightPartId);
    if (juce::isPositiveAndBelow(insertedIndex,
                                 static_cast<int>(arrangement.parts.size())))
      setSingleSelectedPart(insertedIndex);
    repaint();
    return true;
  }

  // In an uncovered range, create the usual default-length section but stop
  // it at the next section boundary to avoid introducing an overlap.
  double nextBoundarySec = playableEndSec;
  for (const auto &part : arrangement.parts) {
    if (part.startSec > insertionSec + kBoundaryToleranceSec)
      nextBoundarySec = juce::jmin(nextBoundarySec, part.startSec);
  }

  double endSec = juce::jmin(
      playableEndSec, nextBoundarySec,
      insertionSec +
          (usePreferredDuration
               ? preferredDurationSec
               : juce::jmax(8.0, kMinPartDurationSec)));
  endSec = juce::jmin(nextBoundarySec, snapSeconds(endSec));
  if ((endSec - insertionSec) < kMinPartDurationSec)
    return false;

  SongArrangementPartSnapshot part;
  part.id = juce::Uuid().toString();
  part.name = name;
  part.color = colourForName(name);
  part.startSec = insertionSec;
  part.endSec = endSec;
  part.utilityCue = utilityCue;
  const auto partId = part.id;

  pushArrangementUndoSnapshot();
  arrangement.parts.push_back(std::move(part));
  customArrangement = true;
  clearSectionInsertionAnchor();
  notifyArrangementChanged(true);
  const int insertedIndex = findPartIndexById(partId);
  if (juce::isPositiveAndBelow(insertedIndex,
                               static_cast<int>(arrangement.parts.size())))
    setSingleSelectedPart(insertedIndex);
  repaint();
  return true;
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

  if (sectionInsertionAnchorValid) {
    addArrangementPartAtInsertionAnchor(nextName, playableStartSec,
                                        playableEndSec);
    return;
  }

  double startSec = getArrangementAppendStartSec(playableStartSec);

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

  pushArrangementUndoSnapshot();
  arrangement.parts.push_back(std::move(part));
  setSingleSelectedPart(static_cast<int>(arrangement.parts.size()) - 1);
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

  if (sectionInsertionAnchorValid && firstCount == lastCount) {
    addArrangementPartAtInsertionAnchor(
        juce::String(firstCount), playableStartSec, playableEndSec,
        oneCountDurationSec);
    return;
  }

  double cursorSec = getArrangementAppendStartSec(playableStartSec);

  cursorSec =
      juce::jlimit(playableStartSec, playableEndSec, snapSecondsForward(cursorSec));

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

    if (lastInsertedIndex < 0)
      pushArrangementUndoSnapshot();
    arrangement.parts.push_back(std::move(part));
    lastInsertedIndex = static_cast<int>(arrangement.parts.size()) - 1;
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

  if (sectionInsertionAnchorValid) {
    addArrangementPartAtInsertionAnchor(cueName.trim(), playableStartSec,
                                        playableEndSec, cueDurationSec, true);
    return;
  }

  double startSec = getArrangementAppendStartSec(playableStartSec);
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
  part.utilityCue = true;

  pushArrangementUndoSnapshot();
  arrangement.parts.push_back(std::move(part));
  setSingleSelectedPart(static_cast<int>(arrangement.parts.size()) - 1);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

double WaveformBar::getArrangementAppendStartSec(
    const double fallbackStartSec) const {
  double appendStartSec = fallbackStartSec;
  for (const auto &part : arrangement.parts)
    appendStartSec = juce::jmax(appendStartSec, part.endSec);
  return appendStartSec;
}

void WaveformBar::duplicateArrangementPart(const int partIndex) {
  createDuplicatedSection(partIndex);
}

void WaveformBar::createDuplicatedSection(const int sourcePartIndex) {
  if (!juce::isPositiveAndBelow(sourcePartIndex,
                                 static_cast<int>(arrangement.parts.size())))
    return;

  const auto selectedPartId =
      arrangement.parts[static_cast<size_t>(sourcePartIndex)].id;
  normalizeArrangement(true, false);

  const int normalizedSourcePartIndex = findPartIndexById(selectedPartId);
  if (!juce::isPositiveAndBelow(normalizedSourcePartIndex,
                                 static_cast<int>(arrangement.parts.size())))
    return;

  const auto sourcePart =
      arrangement.parts[static_cast<size_t>(normalizedSourcePartIndex)];
  if ((sourcePart.endSec - sourcePart.startSec) < kMinPartDurationSec)
    return;

  // Keep a stable reference to the original source even when a copy is
  // duplicated again.  The source range below still comes from the selected
  // copy, so nested copies retain an exact trimmed range.
  const juce::String realSourceId = resolveRealSourcePartId(sourcePart);
  const int realSourceIndex = findPartIndexById(realSourceId);
  const auto &labelSourcePart =
      juce::isPositiveAndBelow(realSourceIndex,
                               static_cast<int>(arrangement.parts.size()))
          ? arrangement.parts[static_cast<size_t>(realSourceIndex)]
          : sourcePart;

  const double insertAtSec = sourcePart.endSec;
  const double sourceLength = getWaveformSourceLengthSeconds();
  const auto [sourceStartSec, sourceEndSec] =
      arrangement_source_mapping::resolveCopySourceRange(
          arrangement.parts, sourcePart, sourceLength);
  const double clipDuration = sourceEndSec - sourceStartSec;
  if (clipDuration < kMinPartDurationSec)
    return;

  const double startSec = insertAtSec;
  const double endSec = startSec + clipDuration;

  SongArrangementPartSnapshot copy;
  copy.id = juce::Uuid().toString();
  copy.name = labelSourcePart.name;
  copy.color = labelSourcePart.color;
  copy.startSec = startSec;
  copy.endSec = endSec;
  copy.sourcePartId = realSourceId;
  copy.sourceStartSec = sourceStartSec;
  copy.sourceEndSec = sourceEndSec;
  copy.muteInstruments = sourcePart.muteInstruments;
  copy.skipPlayback = sourcePart.skipPlayback;
  copy.utilityCue = sourcePart.utilityCue;
  const auto copyId = copy.id;

  pushArrangementUndoSnapshot();
  for (auto &part : arrangement.parts) {
    if (part.startSec >= insertAtSec - 0.0001) {
      part.startSec += clipDuration;
      part.endSec += clipDuration;
    }
  }
  arrangement.parts.push_back(std::move(copy));
  customArrangement = true;
  normalizeArrangement(true, false);
  const int copyIndex = findPartIndexById(copyId);
  if (juce::isPositiveAndBelow(copyIndex,
                                static_cast<int>(arrangement.parts.size())))
    setSingleSelectedPart(copyIndex);
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::toggleArrangementPartMute(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  pushArrangementUndoSnapshot();
  auto &part = arrangement.parts[static_cast<size_t>(partIndex)];
  part.muteInstruments = !part.muteInstruments;
  if (part.muteInstruments)
    part.skipPlayback = false;
  setSingleSelectedPart(partIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::toggleArrangementPartSkip(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  pushArrangementUndoSnapshot();
  auto &part = arrangement.parts[static_cast<size_t>(partIndex)];
  part.skipPlayback = !part.skipPlayback;
  if (part.skipPlayback)
    part.muteInstruments = false;
  setSingleSelectedPart(partIndex);
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
        safe->pushArrangementUndoSnapshot();
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

  const auto partId = part.id;
  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  dialog->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safe, dialog, partId](int result) {
        if (result == 1 && safe != nullptr)
          safe->applyArrangementPartRange(
              partId, dialog->getTextEditorContents("start").getDoubleValue(),
              dialog->getTextEditorContents("end").getDoubleValue());
      }),
      true);
}

void WaveformBar::applyArrangementPartRange(const juce::String &partId,
                                           double start, double end) {
  const int partIndex = findPartIndexById(partId);
  if (!juce::isPositiveAndBelow(partIndex,
                               static_cast<int>(arrangement.parts.size())))
    return;

  const double songLength = getSongLengthSeconds();
  start = snapSeconds(start);
  end = snapSeconds(end);

  const bool isInsertion =
      arrangement.parts[static_cast<size_t>(partIndex)]
          .sourcePartId.isNotEmpty();
  start = isInsertion ? juce::jmax(0.0, start)
                      : juce::jlimit(0.0, songLength, start);
  end = isInsertion ? juce::jmax(0.0, end)
                    : juce::jlimit(0.0, songLength, end);
  if (end < start)
    std::swap(start, end);

  if ((end - start) < kMinPartDurationSec) {
    end = isInsertion ? start + kMinPartDurationSec
                      : juce::jmin(songLength, start + kMinPartDurationSec);
    if ((end - start) < kMinPartDurationSec)
      start = juce::jmax(0.0, end - kMinPartDurationSec);
  }

  auto &editedPart = arrangement.parts[static_cast<size_t>(partIndex)];
  const auto beforeEdit = makeArrangementHistoryEntry();
  pushArrangementUndoSnapshot(beforeEdit);
  const double previousStart = editedPart.startSec;
  const double previousEnd = editedPart.endSec;
  const double previousDuration = previousEnd - previousStart;
  editedPart.startSec = start;
  editedPart.endSec = end;
  if (editedPart.sourcePartId.isNotEmpty()) {
    const bool sameDuration = nearlyEqual(
        editedPart.endSec - editedPart.startSec, previousDuration);
    if (!sameDuration) {
      const bool anchorEnd =
          !nearlyEqual(previousStart, editedPart.startSec) ||
          nearlyEqual(previousEnd, editedPart.endSec);
      clampDuplicatedPartToSourceDuration(
          editedPart, anchorEnd, beforeEdit.arrangement);
    }

    const bool movedBothEdges =
        !nearlyEqual(previousStart, editedPart.startSec) &&
        !nearlyEqual(previousEnd, editedPart.endSec);
    const double nextDuration = editedPart.endSec - editedPart.startSec;
    const double endDelta = editedPart.endSec - previousEnd;
    for (auto &candidate : arrangement.parts) {
      if (candidate.id == editedPart.id)
        continue;
      if (movedBothEdges) {
        // Moving the range relocates its insertion, just like dragging
        // the copy, without changing which source audio it references.
        if (candidate.startSec >= previousEnd - 0.0001) {
          candidate.startSec -= previousDuration;
          candidate.endSec -= previousDuration;
        }
        if (candidate.startSec >= editedPart.startSec - 0.0001) {
          candidate.startSec += nextDuration;
          candidate.endSec += nextDuration;
        }
      } else if (candidate.startSec >= previousEnd - 0.0001) {
        // A right-edge edit opens or closes the insertion symmetrically.
        candidate.startSec += endDelta;
        candidate.endSec += endDelta;
      }
    }
  }
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

  const auto deletedPart = arrangement.parts[static_cast<size_t>(partIndex)];
  pushArrangementUndoSnapshot();
  arrangement.parts.erase(arrangement.parts.begin() + partIndex);
  if (deletedPart.sourcePartId.isNotEmpty())
    rippleCloseGapAfterDuplicatedPart(deletedPart.startSec, deletedPart.endSec);
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

  pushArrangementUndoSnapshot();
  struct RippleGap {
    double startSec = 0.0;
    double endSec = 0.0;
  };
  std::vector<RippleGap> rippleGaps;
  for (const auto &part : arrangement.parts) {
    if (shouldDelete(part) && part.sourcePartId.isNotEmpty() &&
        part.endSec > part.startSec) {
      rippleGaps.push_back({part.startSec, part.endSec});
    }
  }
  std::sort(rippleGaps.begin(), rippleGaps.end(),
            [](const RippleGap &a, const RippleGap &b) {
              return a.startSec < b.startSec;
            });

  int fallbackIndex = selectedPartIndex;
  arrangement.parts.erase(
      std::remove_if(arrangement.parts.begin(), arrangement.parts.end(), shouldDelete),
      arrangement.parts.end());
  double appliedShift = 0.0;
  for (const auto &gap : rippleGaps) {
    const double shiftedStart = gap.startSec - appliedShift;
    const double shiftedEnd = gap.endSec - appliedShift;
    rippleCloseGapAfterDuplicatedPart(shiftedStart, shiftedEnd);
    appliedShift += gap.endSec - gap.startSec;
  }
  fallbackIndex = juce::jlimit(0, static_cast<int>(arrangement.parts.size()) - 1,
                               fallbackIndex);
  setSingleSelectedPart(fallbackIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::notifyArrangementChanged(const bool isCustom) {
  normalizeArrangement(true, false);
  invalidateWaveformRenderCaches();
  if (onArrangementChanged)
    onArrangementChanged(arrangement, isCustom);
}

void WaveformBar::resetArrangementToDefault() {
  pushArrangementUndoSnapshot();
  arrangement.parts.clear();

  customArrangement = false;
  selectedPartIndex = -1;
  selectedPartIds.clear();
  hoveredActionPartIndex = -1;
  hoveredLoopPartIndex = -1;
  hoveredJumpPartIndex = -1;
  hoveredMutePartIndex = -1;
  hoveredSkipPartIndex = -1;
  hoveredDuplicatePartIndex = -1;
  hoveredAddPartIndex = -1;
  hoveredDeletePartIndex = -1;
  setVisibleWindow(0.0f, 1.0f);

  if (onArrangementReset)
    onArrangementReset();
  notifyArrangementChanged(false);
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
