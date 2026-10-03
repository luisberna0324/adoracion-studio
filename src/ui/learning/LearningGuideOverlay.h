#pragma once

#include "LearningGuideModel.h"
#include "../UiCommon.h"

#include <memory>

/** Interactive help layered over the existing controls, with no audio actions. */
class LearningGuideOverlay final : public juce::Component {
public:
  LearningGuideOverlay();
  ~LearningGuideOverlay() override;

  std::function<void(juce::String)> onLessonSelected;
  std::function<void()> onNext, onPrevious, onPause, onRestart, onShowCourses;

  void showCatalog(const std::vector<playworship::learning::Lesson> &,
                   const playworship::learning::ProgressStore &);
  void showStep(const playworship::learning::Lesson &, int index,
                juce::Rectangle<int> targetBounds, bool songAvailable);
  void updateTarget(juce::Rectangle<int>, bool songAvailable);
  void setSafeAreaInsets(juce::BorderSize<int>);
  void setStatusMessage(const juce::String &);
  bool isShowingCatalog() const;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool hitTest(int x, int y) override;
  bool keyPressed(const juce::KeyPress &) override;

private:
  class ActionButton;
  class TextContent;
  class CatalogContent;
  enum class Action { Next, Previous, Pause, Restart, Courses };
  void dispatch(Action);
  void dispatchLesson(const juce::String &);
  void refreshStepText();
  juce::Rectangle<int> safeBounds() const;
  juce::Rectangle<int> chooseStepCard() const;

  HeaderIconButton closeButton{"learningPause", IconGlyph::Close};
  std::unique_ptr<ActionButton> previousButton, nextButton, coursesButton,
      restartButton;
  juce::Viewport textViewport, catalogViewport;
  std::unique_ptr<TextContent> textContent;
  std::unique_ptr<CatalogContent> catalogContent;
  playworship::learning::Lesson lesson;
  int stepIndex = 0;
  bool catalog = true, hasSong = false;
  juce::String statusMessage, catalogTitle, catalogSubtitle;
  juce::BorderSize<int> safeAreaInsets;
  juce::Rectangle<int> requestedTarget, target, cardBounds;
  juce::uint64 presentationRevision = 0;

  friend struct LearningOverlayTestAccess;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LearningGuideOverlay)
};
