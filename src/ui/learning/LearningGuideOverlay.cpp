#include "LearningGuideOverlay.h"

#include "../../data/Localization.h"

#include <algorithm>
#include <array>
#include <limits>

namespace {
struct TextBlock {
  juce::String text;
  float fontSize = 14.0f;
  AppFontWeight weight = AppFontWeight::Regular;
  juce::Colour colour;
  int gapAfter = 8;
};

juce::TextLayout makeLayout(const TextBlock &block, int width) {
  juce::AttributedString text;
  text.setText(block.text);
  text.setFont(makeAppFont(block.fontSize, block.weight));
  text.setColour(block.colour);
  text.setLineSpacing(2.0f);
  juce::TextLayout layout;
  layout.createLayout(text, static_cast<float>(juce::jmax(1, width)));
  return layout;
}

int textHeight(const juce::String &text, int width, float size,
               AppFontWeight weight = AppFontWeight::Regular) {
  return juce::roundToInt(makeLayout({text, size, weight, UITheme::textPrimary, 0}, width)
                             .getHeight());
}

juce::Rectangle<int> clampInto(juce::Rectangle<int> rectangle,
                                juce::Rectangle<int> area) {
  rectangle.setSize(juce::jmin(rectangle.getWidth(), area.getWidth()),
                    juce::jmin(rectangle.getHeight(), area.getHeight()));
  rectangle.setPosition(
      juce::jlimit(area.getX(), area.getRight() - rectangle.getWidth(), rectangle.getX()),
      juce::jlimit(area.getY(), area.getBottom() - rectangle.getHeight(), rectangle.getY()));
  return rectangle;
}
} // namespace

class LearningGuideOverlay::ActionButton final : public juce::Button {
public:
  ActionButton(juce::String name, bool isPrimary = false)
      : juce::Button(std::move(name)), primary(isPrimary) {}

  void paintButton(juce::Graphics &g, bool over, bool down) override {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f, 4.5f);
    const auto base = primary ? UITheme::accent : UITheme::cardBgHover;
    g.setColour((down ? base.darker(0.12f) : over ? base.brighter(0.08f) : base)
                    .withMultipliedAlpha(isEnabled() ? 1.0f : 0.36f));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(UITheme::cardBorder.withAlpha(primary ? 0.18f : 0.48f));
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
    g.setColour((primary ? UITheme::onAccent : UITheme::textPrimary)
                    .withMultipliedAlpha(isEnabled() ? 1.0f : 0.45f));
    g.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(6, 0),
                     juce::Justification::centred, 1);
    if (hasKeyboardFocus(true)) {
      g.setColour(UITheme::accent);
      g.drawRoundedRectangle(bounds.reduced(2.0f), 8.0f, 1.5f);
    }
  }

private:
  bool primary;
};

class LearningGuideOverlay::TextContent final : public juce::Component {
public:
  void setBlocks(std::vector<TextBlock> newBlocks) {
    blocks = std::move(newBlocks);
    resized();
    repaint();
  }
  void resized() override {
    layouts.clear();
    offsets.clear();
    int y = 0;
    for (const auto &block : blocks) {
      offsets.push_back(y);
      layouts.push_back(makeLayout(block, getWidth()));
      y += juce::roundToInt(layouts.back().getHeight()) + block.gapAfter;
    }
    if (getHeight() != y)
      setSize(getWidth(), y);
  }
  void paint(juce::Graphics &g) override {
    for (size_t i = 0; i < layouts.size(); ++i)
      layouts[i].draw(g, {0.0f, static_cast<float>(offsets[i]),
                          static_cast<float>(getWidth()), layouts[i].getHeight()});
  }

private:
  std::vector<TextBlock> blocks;
  std::vector<juce::TextLayout> layouts;
  std::vector<int> offsets;
};

class LearningGuideOverlay::CatalogContent final : public juce::Component {
public:
  struct Row final : public juce::Button {
    Row(const playworship::learning::Lesson &course, juce::String courseStatus,
        int order, bool finished)
        : juce::Button(course.id), lesson(course), status(std::move(courseStatus)),
          number(order), completed(finished) {
      setTitle(lesson.title);
      setDescription(lesson.description + ". " + status);
    }
    int neededHeight(int width) const {
      const int textWidth = juce::jmax(40, width - 76);
      return juce::jmax(104, 32 + textHeight(lesson.title, textWidth, 16,
                                           AppFontWeight::SemiBold) +
                                  textHeight(lesson.description, textWidth, 14) +
                                  textHeight(status, textWidth, 12));
    }
    void mouseUp(const juce::MouseEvent &event) override {
      if (event.mouseWasDraggedSinceMouseDown()) {
        setState(juce::Button::buttonNormal);
        return;
      }
      juce::Button::mouseUp(event);
    }
    void paintButton(juce::Graphics &g, bool over, bool down) override {
      const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
      g.setColour(down ? UITheme::cardBgPressed
                      : over ? UITheme::cardBgHover : UITheme::panelDark);
      g.fillRoundedRectangle(bounds, 14);
      g.setColour(hasKeyboardFocus(true) ? UITheme::accent
                                         : UITheme::cardBorder.withAlpha(0.55f));
      g.drawRoundedRectangle(bounds, 14, 1);
      auto circle = juce::Rectangle<float>(14, 16, 34, 34);
      g.setColour(UITheme::accent.withAlpha(0.15f));
      g.fillEllipse(circle);
      g.setColour(UITheme::accent);
      if (completed)
        drawIconGlyph(g, circle.reduced(8), IconGlyph::Check, UITheme::accent);
      else {
        g.setFont(makeAppFont(15, AppFontWeight::SemiBold));
        g.drawText(juce::String(number), circle, juce::Justification::centred);
      }
      const int width = juce::jmax(40, getWidth() - 76);
      int y = 16;
      const std::array<TextBlock, 3> blocks{{
          {lesson.title, 16, AppFontWeight::SemiBold, UITheme::textPrimary, 5},
          {lesson.description, 14, AppFontWeight::Regular, UITheme::textSecondary, 7},
          {status, 12, AppFontWeight::Medium, UITheme::accent, 0}}};
      for (const auto &block : blocks) {
        auto layout = makeLayout(block, width);
        layout.draw(g, {60.0f, static_cast<float>(y), static_cast<float>(width),
                        layout.getHeight()});
        y += juce::roundToInt(layout.getHeight()) + block.gapAfter;
      }
    }
    playworship::learning::Lesson lesson;
    juce::String status;
    int number;
    bool completed;
  };

  void populate(const std::vector<playworship::learning::Lesson> &lessons,
                const playworship::learning::ProgressStore &progress,
                LearningGuideOverlay &owner) {
    rows.clear();
    int order = 1;
    for (const auto &course : lessons) {
      const auto saved = progress.get(course.id);
      const bool completed = progress.hasProgress(course) && saved.completed;
      juce::String status;
      if (completed)
        status = L10n::tr("Completado", "Completed");
      else if (progress.hasProgress(course))
        status = L10n::tr("Continuar · Paso ", "Continue · Step ") +
                 juce::String(progress.resumeIndex(course) + 1) +
                 L10n::tr(" de ", " of ") + juce::String(course.steps.size());
      else
        status = juce::String(course.steps.size()) + L10n::tr(" pasos", " steps");
      auto row = std::make_unique<Row>(course, status, order++, completed);
      row->onClick = [safe = juce::Component::SafePointer<LearningGuideOverlay>(&owner),
                      id = course.id] {
        if (safe != nullptr)
          safe->dispatchLesson(id);
      };
      addAndMakeVisible(*row);
      rows.push_back(std::move(row));
    }
    resized();
  }
  void resized() override {
    int y = 0;
    for (const auto &row : rows) {
      const int height = row->neededHeight(getWidth());
      row->setBounds(0, y, getWidth(), height);
      y += height + 10;
    }
    if (getHeight() != y)
      setSize(getWidth(), y);
  }

  std::vector<std::unique_ptr<Row>> rows;
};

LearningGuideOverlay::LearningGuideOverlay()
    : nextButton(std::make_unique<ActionButton>("learningNext", true)),
      closeButton(std::make_unique<ActionButton>("learningPause")),
      textContent(std::make_unique<TextContent>()),
      catalogContent(std::make_unique<CatalogContent>()) {
  setOpaque(false);
  setWantsKeyboardFocus(true);
  setInterceptsMouseClicks(true, true);
  catalogCloseButton.setSubtleHighlight(true);
  for (auto* button : std::initializer_list<juce::Button*>{closeButton.get(), &catalogCloseButton}) {
    button->setTitle(L10n::tr("Pausar guía", "Pause guide"));
    button->setTooltip(L10n::tr("Cierra la guía y conserva tu progreso.",
                               "Close the guide and keep your progress."));
    button->onClick = [this] { dispatch(Action::Pause); };
  }
  closeButton->setButtonText(L10n::tr("Cerrar", "Close"));
  previousButton.setExplicitFocusOrder(1);
  closeButton->setExplicitFocusOrder(2);
  nextButton->setExplicitFocusOrder(3);
  previousButton.setSubtleHighlight(true);
  previousButton.setTitle(L10n::tr("Paso anterior", "Previous step"));
  previousButton.setTooltip(L10n::tr("Volver al paso anterior", "Return to the previous step"));
  for (auto *component : std::initializer_list<juce::Component *>{
           &catalogCloseButton, closeButton.get(), &previousButton, nextButton.get(),
           &textViewport, &catalogViewport})
    addAndMakeVisible(*component);
  textViewport.setViewedComponent(textContent.get(), false);
  catalogViewport.setViewedComponent(catalogContent.get(), false);
  for (auto *viewport : {&textViewport, &catalogViewport}) {
    viewport->setScrollBarsShown(true, false);
    viewport->setScrollBarThickness(4);
    viewport->setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
    viewport->getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
                                               UITheme::textSecondary.withAlpha(0.4f));
  }
  textContent->setInterceptsMouseClicks(true, false);
  previousButton.onClick = [this] { dispatch(Action::Previous); };
  nextButton->onClick = [this] { dispatch(Action::Next); };
}

LearningGuideOverlay::~LearningGuideOverlay() {
  textViewport.setViewedComponent(nullptr, false);
  catalogViewport.setViewedComponent(nullptr, false);
}

void LearningGuideOverlay::showCatalog(
    const std::vector<playworship::learning::Lesson> &lessons,
    const playworship::learning::ProgressStore &progress) {
  ++presentationRevision;
  catalog = true;
  requestedTarget = {};
  requestedSupplementaryTarget = {};
  statusMessage = {};
  readinessMessage = {};
  stepReady = true;
  catalogTitle = L10n::tr("Aprende PlayWorship", "Learn PlayWorship");
  catalogSubtitle = L10n::tr("Elige un curso y prueba los controles paso a paso.",
                              "Choose a course and try the controls step by step.");
  setTitle(catalogTitle);
  catalogContent->populate(lessons, progress, *this);
  catalogViewport.setViewPosition(0, 0);
  setVisible(true);
  resized();
  repaint();
}

void LearningGuideOverlay::showStep(const playworship::learning::Lesson &course,
                                     int index, juce::Rectangle<int> targetBounds,
                                     bool songAvailable) {
  ++presentationRevision;
  catalog = false;
  lesson = course;
  setTitle(lesson.title);
  stepIndex = juce::jlimit(0, juce::jmax(0, static_cast<int>(lesson.steps.size()) - 1), index);
  requestedTarget = targetBounds;
  requestedSupplementaryTarget = {};
  hasSong = songAvailable;
  statusMessage = {};
  readinessMessage = {};
  stepReady = true;
  textViewport.setViewPosition(0, 0);
  refreshStepText();
  setVisible(true);
  resized();
  repaint();
}

void LearningGuideOverlay::updateTarget(juce::Rectangle<int> targetBounds,
                                        bool songAvailable) {
  if (requestedTarget == targetBounds && hasSong == songAvailable)
    return;
  requestedTarget = targetBounds;
  if (hasSong != songAvailable) {
    hasSong = songAvailable;
    refreshStepText();
  }
  resized();
  repaint();
}

void LearningGuideOverlay::setSafeAreaInsets(juce::BorderSize<int> insets) {
  if (safeAreaInsets == insets)
    return;
  safeAreaInsets = insets;
  resized();
  repaint();
}

void LearningGuideOverlay::setSupplementaryTarget(juce::Rectangle<int> bounds) {
  // Catalogs never expose application controls, including a delayed target
  // update from a course that has just closed.
  const auto requested = catalog ? juce::Rectangle<int>{} : bounds;
  if (requestedSupplementaryTarget == requested)
    return;
  requestedSupplementaryTarget = requested;
  resized();
  repaint();
}

void LearningGuideOverlay::setStatusMessage(const juce::String &message) {
  if (statusMessage == message)
    return;
  statusMessage = message;
  refreshStepText();
  resized();
  repaint();
}

void LearningGuideOverlay::setStepReadiness(bool ready, juce::String message) {
  if (stepReady == ready && readinessMessage == message)
    return;
  if (stepReady != ready)
    ++readinessRevision;
  stepReady = ready;
  readinessMessage = std::move(message);
  nextButton->setTooltip(readinessMessage);
  nextButton->setDescription(readinessMessage);
  refreshStepText();
  resized();
  repaint();
}

bool LearningGuideOverlay::isShowingCatalog() const { return catalog; }

void LearningGuideOverlay::refreshStepText() {
  std::vector<TextBlock> blocks;
  if (catalog) {
    if (statusMessage.isNotEmpty())
      blocks.push_back({statusMessage, 14, AppFontWeight::Medium, UITheme::accent, 0});
    textContent->setBlocks(std::move(blocks));
    return;
  }
  if (!lesson.steps.empty()) {
    const auto &step = lesson.steps[static_cast<size_t>(stepIndex)];
    blocks.push_back({L10n::tr("Paso ", "Step ") + juce::String(stepIndex + 1) +
                          L10n::tr(" de ", " of ") + juce::String(lesson.steps.size()) +
                          " · " + lesson.title,
                      12, AppFontWeight::Medium, UITheme::textSecondary, 6});
    blocks.push_back({step.title, 17, AppFontWeight::SemiBold, UITheme::textPrimary, 7});
    blocks.push_back({step.description, 14, AppFontWeight::Regular,
                      UITheme::textSecondary, 9});
    if (step.task.isNotEmpty())
      blocks.push_back({L10n::tr("Prueba: ", "Try: ") + step.task, 14,
                        AppFontWeight::Medium, UITheme::textPrimary, 9});
    if (step.needsSong && !hasSong)
      blocks.push_back({L10n::tr("Carga una canción para realizar este paso.",
                                 "Load a song to complete this step."),
                        13, AppFontWeight::Regular, UITheme::textSecondary, 9});
  }
  const bool taskAlreadyExplainsReadiness = !lesson.steps.empty() &&
      readinessMessage.trim() == lesson.steps[static_cast<size_t>(stepIndex)].task.trim();
  if (readinessMessage.isNotEmpty() && !taskAlreadyExplainsReadiness)
    blocks.push_back({readinessMessage, 13, AppFontWeight::Medium,
                      stepReady ? UITheme::accent : UITheme::textSecondary, 5});
  if (statusMessage.isNotEmpty())
    blocks.push_back({statusMessage, 13, AppFontWeight::Medium, UITheme::accent, 5});
  textContent->setBlocks(std::move(blocks));
}

juce::Rectangle<int> LearningGuideOverlay::safeBounds() const {
  auto bounds = safeAreaInsets.subtractedFrom(getLocalBounds())
                    .getIntersection(getLocalBounds());
  const int inset = juce::jmin(12, juce::jmin(bounds.getWidth(), bounds.getHeight()) / 12);
  return bounds.reduced(inset);
}

juce::Rectangle<int> LearningGuideOverlay::chooseStepCard() const {
  const auto safe = safeBounds();
  if (safe.isEmpty())
    return {};
  const int width = juce::jmin(360, safe.getWidth());
  const int height = juce::jmin(safe.getHeight() >= 500 ? 300 : 210, safe.getHeight());
  const auto defaultCard = juce::Rectangle<int>(safe.getCentreX() - width / 2,
                                                safe.getBottom() - height, width, height);
  // Cloud courses highlight the full library; keep its header and first rows
  // reachable, with the compact guide below them.
  if ((target.isEmpty() && supplementaryTarget.isEmpty()) ||
      (lesson.id.startsWith("teams-cloud-") && supplementaryTarget.isEmpty()))
    return defaultCard;

  constexpr int gap = 12;
  std::vector<juce::Rectangle<int>> candidates;
  const bool landscape = static_cast<float>(safe.getWidth()) > static_cast<float>(safe.getHeight()) * 1.35f;
  const auto appendAround = [&](const juce::Rectangle<int> &anchor) {
    if (anchor.isEmpty()) return;
    const auto appendSide = [&](bool left) {
      const int available = left ? anchor.getX() - gap - safe.getX()
                                 : safe.getRight() - anchor.getRight() - gap;
      if (available < 250) return;
      const int sideWidth = juce::jmin(width, available);
      const int x = left ? anchor.getX() - gap - sideWidth : anchor.getRight() + gap;
      candidates.push_back(clampInto(
          {x, anchor.getCentreY() - height / 2, sideWidth, height}, safe));
    };
    if (landscape) {
      appendSide(anchor.getCentreX() >= safe.getCentreX());
      appendSide(anchor.getCentreX() < safe.getCentreX());
    }
    const int x = juce::jlimit(safe.getX(), safe.getRight() - width,
                              anchor.getCentreX() - width / 2);
    const int minimumHeight = juce::jmin(210, safe.getHeight());
    const int belowSpace = safe.getBottom() - anchor.getBottom() - gap;
    const int aboveSpace = anchor.getY() - gap - safe.getY();
    if (belowSpace >= minimumHeight)
      candidates.emplace_back(x, anchor.getBottom() + gap, width, juce::jmin(height, belowSpace));
    if (aboveSpace >= minimumHeight) {
      const int aboveHeight = juce::jmin(height, aboveSpace);
      candidates.emplace_back(x, anchor.getY() - gap - aboveHeight, width, aboveHeight);
    }
    if (!landscape) {
      appendSide(anchor.getCentreX() >= safe.getCentreX());
      appendSide(anchor.getCentreX() < safe.getCentreX());
    }
  };
  appendAround(target);
  if (supplementaryTarget.isEmpty() && !candidates.empty())
    return candidates.front();

  // Very large targets leave no room for a separate card. Keep all actions
  // on screen and cover only the smaller of its top/bottom portions.
  const auto topCard = defaultCard.withY(safe.getY());
  const auto overlapArea = [](const juce::Rectangle<int> &card,
                              const juce::Rectangle<int> &anchor) {
    const auto overlap = card.getIntersection(anchor);
    return overlap.getWidth() * overlap.getHeight();
  };
  if (supplementaryTarget.isEmpty())
    return overlapArea(topCard, target) < overlapArea(defaultCard, target) ? topCard : defaultCard;

  appendAround(supplementaryTarget);
  candidates.push_back(defaultCard);
  candidates.push_back(topCard);
  const auto clear = std::find_if(candidates.begin(), candidates.end(), [&](const auto &card) {
    return !card.intersects(target) && !card.intersects(supplementaryTarget);
  });
  if (clear != candidates.end()) return *clear;
  // A waveform may fill almost the entire screen. Prefer keeping the small
  // supplementary transport available, then minimize coverage of the main target.
  return *std::min_element(candidates.begin(), candidates.end(), [&](const auto &a, const auto &b) {
    const int extraA = overlapArea(a, supplementaryTarget);
    const int extraB = overlapArea(b, supplementaryTarget);
    if ((extraA == 0) != (extraB == 0)) return extraA == 0;
    return overlapArea(a, target) + extraA < overlapArea(b, target) + extraB;
  });
}

void LearningGuideOverlay::resized() {
  const auto safe = safeBounds();
  target = requestedTarget.getIntersection(safeAreaInsets.subtractedFrom(getLocalBounds()))
                           .getIntersection(getLocalBounds());
  supplementaryTarget = requestedSupplementaryTarget
      .getIntersection(safeAreaInsets.subtractedFrom(getLocalBounds()))
      .getIntersection(getLocalBounds());
  const bool hasSteps = !lesson.steps.empty();
  catalogViewport.setVisible(catalog);
  textViewport.setVisible(!catalog || statusMessage.isNotEmpty());
  previousButton.setVisible(!catalog);
  nextButton->setVisible(!catalog);
  closeButton->setVisible(!catalog);
  catalogCloseButton.setVisible(catalog);
  nextButton->setButtonText(hasSteps && stepIndex + 1 == static_cast<int>(lesson.steps.size())
                               ? L10n::tr("Terminar", "Finish")
                               : L10n::tr("Continuar", "Continue"));
  nextButton->setTooltip(readinessMessage);
  nextButton->setDescription(readinessMessage);
  previousButton.setEnabled(hasSteps && stepIndex > 0);
  nextButton->setEnabled(hasSteps && stepReady);
  if (safe.isEmpty()) {
    cardBounds = {};
    return;
  }
  if (catalog) {
    const int width = juce::jmin(560, safe.getWidth());
    const int height = juce::jmin(660, safe.getHeight());
    cardBounds = {safe.getCentreX() - width / 2, safe.getCentreY() - height / 2, width, height};
    catalogCloseButton.setBounds(cardBounds.getRight() - 56, cardBounds.getY() + 10, 44, 44);
    auto body = cardBounds.reduced(14);
    body.removeFromTop(96);
    if (statusMessage.isNotEmpty()) {
      const auto height = juce::jmin(juce::jmax(36, body.getHeight() / 3),
          12 + textHeight(statusMessage, juce::jmax(40, body.getWidth() - 6), 14, AppFontWeight::Medium));
      textViewport.setBounds(body.removeFromTop(height));
      textContent->setSize(juce::jmax(1, body.getWidth() - 6), textContent->getHeight());
      body.removeFromTop(8);
    }
    catalogViewport.setBounds(body);
    catalogContent->setSize(juce::jmax(1, body.getWidth() - 6), catalogContent->getHeight());
    return;
  }
  cardBounds = chooseStepCard();
  auto content = cardBounds.reduced(12);
  const int controlHeight = juce::jmin(44, juce::jmax(0, (content.getHeight() - 12) / 3));
  content.removeFromTop(4);
  auto footer = content.removeFromBottom(controlHeight);
  previousButton.setBounds(footer.removeFromLeft(44));
  footer.removeFromLeft(8);
  const int labelWidth = juce::roundToInt(makeLayout(
      {nextButton->getButtonText(), 14.0f, AppFontWeight::Medium,
       UITheme::textPrimary, 0}, 500).getWidth());
  const int buttonWidth = juce::jmin(juce::jmax(0, (footer.getWidth() - 8) / 2),
                                   juce::jmax(88, labelWidth + 28));
  nextButton->setBounds(footer.removeFromRight(buttonWidth));
  footer.removeFromRight(8);
  closeButton->setBounds(footer.removeFromRight(juce::jmin(88, footer.getWidth())));
  content.removeFromBottom(6);
  textViewport.setBounds(content);
  textContent->setSize(juce::jmax(1, content.getWidth() - 6), textContent->getHeight());
}

void LearningGuideOverlay::paint(juce::Graphics &g) {
  juce::Path shade;
  shade.addRectangle(getLocalBounds().toFloat());
  if (!catalog && !target.isEmpty()) {
    shade.addRoundedRectangle(target.expanded(5).getIntersection(getLocalBounds()).toFloat(), 12);
    shade.setUsingNonZeroWinding(false);
  }
  {
    const juce::Graphics::ScopedSaveState saved(g);
    if (!catalog && !supplementaryTarget.isEmpty()) {
      // Intersect the two inverse masks rather than XOR-ing two holes: even
      // overlapping targets remain clear, without exposing their bounding union.
      juce::Path supplementaryShade;
      supplementaryShade.addRectangle(getLocalBounds().toFloat());
      supplementaryShade.addRoundedRectangle(
          supplementaryTarget.expanded(5).getIntersection(getLocalBounds()).toFloat(), 12);
      supplementaryShade.setUsingNonZeroWinding(false);
      g.reduceClipRegion(supplementaryShade);
    }
    g.setColour(juce::Colours::black.withAlpha(catalog ? 0.52f : 0.58f));
    g.fillPath(shade);
  }
  if (!catalog && !target.isEmpty()) {
    g.setColour(UITheme::accent.withAlpha(0.85f));
    g.drawRoundedRectangle(target.expanded(4).toFloat(), 11, 1.5f);
  }
  if (!catalog && !supplementaryTarget.isEmpty()) {
    g.setColour(UITheme::accent.withAlpha(0.5f));
    g.drawRoundedRectangle(supplementaryTarget.expanded(4).toFloat(), 11, 1.2f);
  }
  if (cardBounds.isEmpty())
    return;
  juce::DropShadow(juce::Colours::black.withAlpha(0.30f), 12, {0, 4})
      .drawForRectangle(g, cardBounds);
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(cardBounds.toFloat(), 18);
  g.setColour(UITheme::cardBorder.withAlpha(0.7f));
  g.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), 18, 1);
  if (catalog) {
    auto heading = cardBounds.reduced(18);
    heading.setWidth(juce::jmax(0, heading.getWidth() - 44));
    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(22, AppFontWeight::SemiBold));
    g.drawFittedText(catalogTitle,
                     heading.removeFromTop(31), juce::Justification::centredLeft, 1);
    auto subtitle = makeLayout({catalogSubtitle,
                                14, AppFontWeight::Regular, UITheme::textSecondary, 0},
                               cardBounds.getWidth() - 36);
    subtitle.draw(g, {static_cast<float>(cardBounds.getX() + 18),
                      static_cast<float>(cardBounds.getY() + 57),
                      static_cast<float>(cardBounds.getWidth() - 36), subtitle.getHeight()});
  }
}

bool LearningGuideOverlay::hitTest(int x, int y) {
  if (!catalog && (target.contains(x, y) || supplementaryTarget.contains(x, y)) &&
      !cardBounds.contains(x, y))
    return false;
  return getLocalBounds().contains(x, y);
}

void LearningGuideOverlay::dispatch(Action action) {
  if (action == Action::Next && (catalog || lesson.steps.empty() || !stepReady))
    return;
  const auto revision = presentationRevision;
  const auto readyRevision = readinessRevision;
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<LearningGuideOverlay>(this), action, revision,
       readyRevision] {
        if (safe == nullptr || safe->presentationRevision != revision)
          return;
        std::function<void()> callback;
        switch (action) {
        case Action::Next:
          if (safe->catalog || safe->lesson.steps.empty() || !safe->stepReady ||
              safe->readinessRevision != readyRevision) return;
          callback = safe->onNext;
          break;
        case Action::Previous:
          if (safe->catalog || safe->stepIndex <= 0) return;
          callback = safe->onPrevious;
          break;
        case Action::Pause: callback = safe->onPause; break;
        case Action::Restart:
          if (safe->catalog || safe->lesson.steps.empty()) return;
          callback = safe->onRestart;
          break;
        case Action::Courses: callback = safe->onShowCourses; break;
        }
        if (callback)
          callback();
      });
}

void LearningGuideOverlay::dispatchLesson(const juce::String &id) {
  const auto revision = presentationRevision;
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<LearningGuideOverlay>(this), id, revision] {
        if (safe == nullptr || !safe->catalog || safe->presentationRevision != revision)
          return;
        const auto callback = safe->onLessonSelected;
        if (callback)
          callback(id);
      });
}

bool LearningGuideOverlay::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) {
    dispatch(Action::Pause);
    return true;
  }
  if (!catalog && key == juce::KeyPress::rightKey) {
    dispatch(Action::Next);
    return true;
  }
  if (!catalog && key == juce::KeyPress::leftKey) {
    dispatch(Action::Previous);
    return true;
  }
  return false;
}
