#include "SongListPanel.h"

#include "SongImportWizardComponent.h"
#include "../components/AppModal.h"
#include "../../PluginEditor.h"
#include "../../data/AppSettings.h"
#include "../../data/Localization.h"
#include "../../data/SessionAuthLogger.h"
#include "../../data/SongProjectCreation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#if JUCE_ANDROID
#include <jni.h>
#include <juce_core/native/juce_JNIHelpers_android.h>
#endif

class SongWizardOverlay final : public juce::Component {
public:
  SongWizardOverlay(std::unique_ptr<SongImportWizardComponent> content,
                    std::function<void()> dismissCallback)
      : panel(std::move(content)), onDismiss(std::move(dismissCallback)) {
    jassert(panel != nullptr);
    panel->setOnDismissRequested(onDismiss);
    addAndMakeVisible(*panel);
    setInterceptsMouseClicks(true, true);
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colours::black.withAlpha(0.44f));
  }

  void resized() override {
    auto area = getLocalBounds();
    if (isPhoneSizedTouchLayout(*this)) {
      // The wizard owns the full phone surface so its background reaches the
      // physical edges. It applies safe-area insets only to interactive
      // content, keeping the notch and home-indicator regions free.
      panel->setBounds(area);
      return;
    }

    const int margin = useTouchOptimisedControls() ? 14 : 28;
    area.reduce(margin, margin);
    const int maxWidth = useTouchOptimisedControls() ? 920 : 860;
    const int maxHeight = useTouchOptimisedControls() ? 700 : 640;
    auto panelBounds = juce::Rectangle<int>(juce::jmin(maxWidth, area.getWidth()),
                                             juce::jmin(maxHeight, area.getHeight()));
    panelBounds.setCentre(area.getCentre());
    panel->setBounds(panelBounds);
  }

  void parentSizeChanged() override {
    if (auto *parent = getParentComponent())
      setBounds(parent->getLocalBounds());
  }

  void mouseUp(const juce::MouseEvent &event) override {
    if (!panel->isSubmissionInProgress() &&
        !panel->getBounds().contains(event.getPosition()) && onDismiss)
      onDismiss();
  }

private:
  std::unique_ptr<SongImportWizardComponent> panel;
  std::function<void()> onDismiss;
};

namespace {
class LegacyProjectMigrationTask final
    : public juce::ThreadWithProgressWindow {
public:
  LegacyProjectMigrationTask(const juce::String &songIdIn,
                             const juce::File &destinationIn,
                             juce::Component *centreAround)
      : ThreadWithProgressWindow(
            L10n::tr("Convirtiendo a proyecto", "Converting to project"),
            true, false, 10000, {}, centreAround),
        songId(songIdIn), destination(destinationIn) {}

  void run() override {
    success = ProjectStore::convertLegacySongToProject(
        songId, destination, projectSong, sourceSetlistName, &error,
        [this](const double progress, const juce::String &phase) {
          setProgress(juce::jlimit(0.0, 1.0, progress));
          setStatusMessage(phase);
        });
  }

  bool success = false;
  juce::String error;
  juce::String sourceSetlistName;
  ImportedSongData projectSong;

private:
  juce::String songId;
  juce::File destination;
};

juce::String makeAutoPlayNextMarkKey(const juce::String &setlistId,
                                     const juce::String &songId) {
  if (setlistId.isEmpty() || songId.isEmpty())
    return {};
  return setlistId + "|" + songId;
}

juce::Colour colourfulAccentFromStableId(const juce::String &id) {
  if (id.isEmpty())
    return UITheme::accent;
  const auto hash = std::hash<std::string>{}(id.toStdString());
  const float hue = static_cast<float>(hash % 360) / 360.0f;
  const float saturation = 0.78f + (static_cast<float>((hash >> 9) % 16) / 100.0f);
  const float brightness = 0.88f + (static_cast<float>((hash >> 15) % 10) / 100.0f);
  return juce::Colour::fromHSV(
      juce::jlimit(0.0f, 1.0f, hue),
      juce::jlimit(0.0f, 1.0f, saturation),
      juce::jlimit(0.0f, 1.0f, brightness), 1.0f);
}

juce::Colour songAccentFromId(const juce::String &songId) {
  return colourfulAccentFromStableId(songId);
}

float measureTextWidth(const juce::Font &font, const juce::String &text) {
  juce::GlyphArrangement glyphs;
  glyphs.addLineOfText(font, text, 0.0f, 0.0f);
  return glyphs.getBoundingBox(0, -1, true).getWidth();
}

juce::String toCapitalCase(const juce::String &text) {
  const auto normalized = text.trim().toLowerCase();
  if (normalized.isEmpty())
    return {};

  juce::String result;
  result.preallocateBytes(normalized.getNumBytesAsUTF8());

  bool capitalizeNext = true;
  for (int i = 0; i < normalized.length(); ++i) {
    const auto ch = normalized[i];
    if (juce::CharacterFunctions::isLetter(ch)) {
      result << juce::String::charToString(
          capitalizeNext ? juce::CharacterFunctions::toUpperCase(ch) : ch);
      capitalizeNext = false;
      continue;
    }

    result << juce::String::charToString(ch);
    capitalizeNext = !juce::CharacterFunctions::isLetterOrDigit(ch);
  }

  return result;
}

juce::String toSentenceCase(const juce::String &text) {
  auto normalized = text.trim().toLowerCase();
  if (normalized.isEmpty())
    return {};

  for (int i = 0; i < normalized.length(); ++i) {
    const auto ch = normalized[i];
    if (!juce::CharacterFunctions::isLetter(ch))
      continue;

    normalized = normalized.replaceSection(
        i, 1,
        juce::String::charToString(juce::CharacterFunctions::toUpperCase(ch)));
    break;
  }

  return normalized;
}

juce::File importedProjectsDirectory() {
  auto dir = SongLibraryStore::getRootDirectory()
                 .getChildFile("imports")
                 .getChildFile("projects");
  dir.createDirectory();
  return dir;
}

juce::File makeImportedProjectTarget(const juce::URL &url) {
  auto fileName = juce::File::createLegalFileName(url.getFileName().trim());
  if (fileName.isEmpty())
    fileName = "imported-project.pwproj";
  if (!fileName.endsWithIgnoreCase(".pwproj"))
    fileName += ".pwproj";

  const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
  return importedProjectsDirectory().getChildFile(
      juce::File::createLegalFileName(stamp + "_" + fileName));
}

juce::File importProjectUrlToSandbox(const juce::URL &url,
                                     juce::String &error) {
  error.clear();
  const auto target = makeImportedProjectTarget(url);
#if JUCE_ANDROID
  auto *env = juce::getEnv();
  const auto helper = env->FindClass("com/rmsl/juce/ProjectImport");
  const auto copy = helper != nullptr
                        ? env->GetStaticMethodID(
                              helper, "copyToFile",
                              "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z")
                        : nullptr;
  if (copy == nullptr) {
    env->ExceptionClear();
    error = L10n::tr("No se pudo preparar la importación del proyecto.",
                     "Couldn't prepare the project import.");
    return {};
  }

  const auto source = env->NewStringUTF(url.toString(true).toRawUTF8());
  const auto destination = env->NewStringUTF(target.getFullPathName().toRawUTF8());
  const auto copied = env->CallStaticBooleanMethod(
      helper, copy, juce::getAppContext().get(), source, destination) != 0;
  env->DeleteLocalRef(source);
  env->DeleteLocalRef(destination);
  env->DeleteLocalRef(helper);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    error = L10n::tr("No se pudo acceder al archivo seleccionado desde Archivos.",
                     "Couldn't access the selected file from Files.");
    return {};
  }
  if (!copied) {
    error = L10n::tr("No se pudo copiar el proyecto al almacenamiento local.",
                     "Couldn't copy the project to local storage.");
    return {};
  }
#else
  auto input = url.createInputStream(
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withConnectionTimeoutMs(60000));
  if (input == nullptr) {
    error = L10n::tr(
        "No se pudo acceder al archivo seleccionado desde Archivos.",
        "Couldn't access the selected file from Files.");
    return {};
  }

  auto output = target.createOutputStream();
  if (output == nullptr) {
    error = L10n::tr("No se pudo preparar la importación del proyecto.",
                     "Couldn't prepare the project import.");
    return {};
  }

  constexpr int64_t bufferSize = 256 * 1024;
  juce::HeapBlock<char> buffer(static_cast<size_t>(bufferSize));
  while (!input->isExhausted()) {
    const auto bytesRead = input->read(buffer.getData(),
                                       static_cast<int>(bufferSize));
    if (bytesRead <= 0)
      break;
    if (!output->write(buffer.getData(), static_cast<size_t>(bytesRead))) {
      error = L10n::tr("No se pudo copiar el proyecto al almacenamiento local.",
                       "Couldn't copy the project to local storage.");
      return {};
    }
  }

  output->flush();
#endif
  if (!target.existsAsFile() || target.getSize() <= 0) {
    error = L10n::tr("La copia local del proyecto quedó incompleta.",
                     "The local project copy is incomplete.");
    return {};
  }

  return target;
}
} // namespace

SongListPanel::SwipeHintHandle::SwipeHintHandle(
    const Direction directionToUse)
    : direction(directionToUse) {
  setRepaintsOnMouseActivity(true);
  setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void SongListPanel::SwipeHintHandle::setHintVisible(const bool shouldShowHint) {
  if (hintVisible == shouldShowHint)
    return;
  hintVisible = shouldShowHint;
  repaint();
}

void SongListPanel::SwipeHintHandle::setOnGestureBegin(
    std::function<void(const juce::MouseEvent &)> cb) {
  onGestureBegin = std::move(cb);
}

void SongListPanel::SwipeHintHandle::setOnGestureDrag(
    std::function<void(const juce::MouseEvent &)> cb) {
  onGestureDrag = std::move(cb);
}

void SongListPanel::SwipeHintHandle::setOnGestureEnd(
    std::function<void(const juce::MouseEvent &)> cb) {
  onGestureEnd = std::move(cb);
}

void SongListPanel::SwipeHintHandle::paint(juce::Graphics &g) {
  if (!hintVisible && !isMouseOverOrDragging())
    return;

  const float alpha = isMouseOverOrDragging() ? 0.80f : 0.48f;
  auto area = getLocalBounds().toFloat().reduced(1.5f);
  g.setColour(UITheme::cardBgHover.withAlpha(alpha * 0.18f));
  g.fillRoundedRectangle(area, 6.0f);
  g.setColour(UITheme::cardBorder.withAlpha(alpha * 0.34f));
  g.drawRoundedRectangle(area, 6.0f, 1.0f);

  const auto glyph = direction == Direction::Right ? IconGlyph::Next
                                                   : IconGlyph::SeekBack;
  drawIconGlyph(g, area.reduced(4.0f, 14.0f),
                glyph, UITheme::textSecondary.withAlpha(alpha), 1.45f);
}

void SongListPanel::SwipeHintHandle::mouseDown(const juce::MouseEvent &event) {
  if (onGestureBegin)
    onGestureBegin(event.getEventRelativeTo(getParentComponent()));
}

void SongListPanel::SwipeHintHandle::mouseDrag(const juce::MouseEvent &event) {
  if (onGestureDrag)
    onGestureDrag(event.getEventRelativeTo(getParentComponent()));
}

void SongListPanel::SwipeHintHandle::mouseUp(const juce::MouseEvent &event) {
  if (onGestureEnd)
    onGestureEnd(event.getEventRelativeTo(getParentComponent()));
}

class SongListPanel::PageContainer final : public juce::Component,
                                           private juce::Timer {
public:
  PageContainer() {
    for (auto &page : pages)
      page = std::make_unique<juce::Component>();
    for (int index = 0; index < static_cast<int>(pages.size()); ++index) {
      auto &page = *pages[static_cast<size_t>(index)];
      if (index == 0)
        addAndMakeVisible(page);
      else
        addChildComponent(page);
    }
  }

  juce::Component &getPage(const int index) {
    jassert(juce::isPositiveAndBelow(index, static_cast<int>(pages.size())));
    return *pages[static_cast<size_t>(index)];
  }

  void showPage(const int targetIndex, const bool animate,
                std::function<void()> onFinished = {}) {
    if (!juce::isPositiveAndBelow(targetIndex, static_cast<int>(pages.size())) ||
        targetIndex == currentIndex)
      return;

    // Setlists <-> Songs <-> Library <-> Create song is a linear flow.
    if (std::abs(targetIndex - currentIndex) != 1)
      return;

    finishTransition();
    const auto bounds = getLocalBounds();
    auto *incoming = pages[static_cast<size_t>(targetIndex)].get();
    auto *outgoing = pages[static_cast<size_t>(currentIndex)].get();
    if (!animate || bounds.isEmpty()) {
      outgoing->setVisible(false);
      incoming->setBounds(bounds);
      incoming->setVisible(true);
      currentIndex = targetIndex;
      if (onFinished)
        onFinished();
      return;
    }

    juce::Desktop::getInstance().getAnimator().cancelAllAnimations(false);
    const int offset = targetIndex > currentIndex ? bounds.getWidth()
                                                   : -bounds.getWidth();
    incoming->setBounds(bounds.translated(offset, 0));
    incoming->setVisible(true);
    outgoing->setVisible(true);

    auto &animator = juce::Desktop::getInstance().getAnimator();
    animator.animateComponent(outgoing, bounds.translated(-offset, 0), 1.0f,
                              transitionDurationMs, false, 0.0, 0.0);
    animator.animateComponent(incoming, bounds, 1.0f, transitionDurationMs,
                              false, 0.0, 0.0);
    currentIndex = targetIndex;
    onTransitionFinished = std::move(onFinished);
    startTimer(transitionDurationMs + 24);
  }

  void resized() override {
    finishTransition();
    const auto bounds = getLocalBounds();
    for (int index = 0; index < static_cast<int>(pages.size()); ++index) {
      auto *page = pages[static_cast<size_t>(index)].get();
      if (index == currentIndex)
        page->setBounds(bounds);
      else if (!page->isVisible())
        page->setBounds(bounds.translated(bounds.getWidth(), 0));
    }
  }

private:
  void finishTransition() {
    stopTimer();
    for (int index = 0; index < static_cast<int>(pages.size()); ++index) {
      auto *page = pages[static_cast<size_t>(index)].get();
      juce::Desktop::getInstance().getAnimator().cancelAnimation(page, false);
      page->setVisible(index == currentIndex);
    }
    pages[static_cast<size_t>(currentIndex)]->setBounds(getLocalBounds());
    if (onTransitionFinished) {
      auto completed = std::move(onTransitionFinished);
      onTransitionFinished = {};
      completed();
    }
  }

  void timerCallback() override { finishTransition(); }

  static constexpr int transitionDurationMs = 300;
  std::array<std::unique_ptr<juce::Component>, 4> pages;
  int currentIndex = 0;
  std::function<void()> onTransitionFinished;
};

class SongListPanel::SetlistsContent final : public juce::Component {
public:
  struct Row {
    juce::String id;
    juce::String name;
    int songCount = 0;
    double totalDurationSeconds = 0.0;
  };

  void setRows(std::vector<Row> nextRows, const juce::String &activeId) {
    rows = std::move(nextRows);
    selectedSetlistId = activeId;
    repaint();
  }

  int getPreferredHeight() const {
    if (rows.empty())
      return 120;
    constexpr int rowHeight = 64;
    return static_cast<int>(rows.size()) * rowHeight;
  }

  std::function<void(const juce::String &)> onSetlistChosen;
  std::function<void(const juce::String &)> onSetlistRenameRequested;
  std::function<void(const juce::String &)> onSetlistDeleteRequested;
  std::function<void(const juce::String &, juce::Rectangle<int>)>
      onSetlistOptionsRequested;

  void paint(juce::Graphics &g) override {
    if (rows.empty()) {
      const auto bounds = juce::Rectangle<int>(12, 12, getWidth() - 24, 96);
      const auto iconBounds =
          juce::Rectangle<float>(static_cast<float>(bounds.getCentreX() - 13),
                                 static_cast<float>(bounds.getCentreY() - 30),
                                 26.0f, 26.0f);
      drawSvgIcon(g, iconBounds, SvgIconName::Music,
                  UITheme::textSecondary.withAlpha(0.58f));
      g.setColour(UITheme::textSecondary.withAlpha(0.9f));
      g.setFont(makeAppFont(12.5f, AppFontWeight::Medium));
      g.drawFittedText(L10n::tr("Aun no hay setlists.", "No setlists yet."),
                       bounds.withTrimmedTop(22),
                       juce::Justification::centred, 2);
    }

    constexpr int rowHeight = 64;
    constexpr int rowGap = 0;
    constexpr int rowStart = 0;
    constexpr int rowSideInset = 0;

    for (size_t i = 0; i < rows.size(); ++i) {
      auto row = juce::Rectangle<int>(rowSideInset,
          rowStart + static_cast<int>(i) * (rowHeight + rowGap),
          getWidth() - (rowSideInset * 2), rowHeight);
      const bool revealed = static_cast<int>(i) == revealedRow;
      if (revealed && swipeOffset > 0.0f) {
        const auto renameBounds = renameActionBounds(row);
        const auto deleteBounds = deleteActionBounds(row);
        g.setColour(UITheme::cardBgHover);
        g.fillRect(renameBounds);
        g.setColour(UITheme::accent.withAlpha(0.20f));
        g.fillRect(deleteBounds);
        drawIconGlyph(
            g, renameBounds.toFloat().withSizeKeepingCentre(18.0f, 18.0f)
                   .translated(0.0f, -8.0f),
            IconGlyph::Pencil, UITheme::textPrimary, 1.5f);
        drawSvgIcon(
            g, deleteBounds.toFloat().withSizeKeepingCentre(18.0f, 18.0f)
                   .translated(0.0f, -8.0f),
            SvgIconName::Trash, UITheme::accent);
        g.setFont(makeAppFont(9.5f, AppFontWeight::Medium));
        g.setColour(UITheme::textPrimary);
        g.drawFittedText(L10n::tr("Renombrar", "Rename"),
                         renameBounds.withTrimmedTop(36),
                         juce::Justification::centred, 1);
        g.setColour(UITheme::accent);
        g.drawFittedText(L10n::tr("Eliminar", "Delete"),
                         deleteBounds.withTrimmedTop(36),
                         juce::Justification::centred, 1);
      }
      if (revealed)
        row.translate(-juce::roundToInt(swipeOffset), 0);
      const bool active = rows[i].id == selectedSetlistId;
      if (active) {
        g.setColour(AppColours::accentSuccess);
        g.fillRect(row.removeFromLeft(4));
      }
      auto c = row.reduced(16, 10);
      const auto avatar = c.removeFromLeft(44);
      g.setColour(juce::Colour(0xff3A3D45));
      g.fillRoundedRectangle(avatar.toFloat(), 8.0f);
      drawSvgIcon(g, avatar.toFloat().reduced(12.0f), SvgIconName::Music,
                  AppColours::textSecondary);
      c.removeFromLeft(12);
      auto optionsRect = c.removeFromRight(40);
      c.removeFromRight(6);

      constexpr int titleHeight = 17;
      constexpr int artistHeight = 13;
      constexpr int lineGap = 3;
      const int totalTextHeight = titleHeight + artistHeight + lineGap;
      const int textStartY =
          c.getY() + juce::jmax(0, (c.getHeight() - totalTextHeight) / 2);

      auto titleRect =
          juce::Rectangle<int>(c.getX(), textStartY, c.getWidth(), titleHeight);
      auto artistRect = juce::Rectangle<int>(c.getX(), titleRect.getBottom() + lineGap,
                                             c.getWidth(), artistHeight);
      g.setColour(AppColours::textPrimary);
      g.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
      g.drawFittedText(rows[i].name, titleRect, juce::Justification::centredLeft, 1);

      g.setColour(AppColours::textSubtitle);
      g.setFont(makeAppFont(10.5f, AppFontWeight::Regular));
      g.drawFittedText(
          juce::String(rows[i].songCount) + " " +
              (rows[i].songCount == 1 ? L10n::tr(u8"canción", "song")
                                      : L10n::tr("canciones", "songs")) +
              " · " +
              formatDurationHMS(rows[i].totalDurationSeconds), artistRect,
          juce::Justification::centredLeft, 1);

      drawOptionsButton(g, optionsRect.withSizeKeepingCentre(20, 20));
      if (i + 1 < rows.size()) {
        g.setColour(AppColours::separator);
        g.drawLine(static_cast<float>(avatar.getRight() + 12),
                   static_cast<float>(row.getBottom() - 1),
                   static_cast<float>(row.getRight()),
                   static_cast<float>(row.getBottom() - 1), 1.0f);
      }
    }

  }

  void mouseDown(const juce::MouseEvent &event) override {
    swipeCandidateRow = rowIndexAtY(juce::roundToInt(event.position.y));
    swipeStartPosition = event.position;
    swipeStartOffset = swipeCandidateRow == revealedRow ? swipeOffset : 0.0f;
    swipeDragging = false;
  }

  void mouseDrag(const juce::MouseEvent &event) override {
    if (!juce::isPositiveAndBelow(swipeCandidateRow,
                                  static_cast<int>(rows.size())))
      return;
    const auto delta = event.position - swipeStartPosition;
    if (!swipeDragging) {
      if (std::abs(delta.x) < 7.0f || std::abs(delta.x) <= std::abs(delta.y))
        return;
      swipeDragging = true;
      revealedRow = swipeCandidateRow;
    }
    swipeOffset = juce::jlimit(0.0f, static_cast<float>(kActionWidth),
                               swipeStartOffset - delta.x);
    repaint();
  }

  void mouseUp(const juce::MouseEvent &event) override {
    if (event.mods.isPopupMenu())
      return;

    const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
    if (!juce::isPositiveAndBelow(idx, static_cast<int>(rows.size())))
      return;
    const auto rowBounds = rowBoundsAt(idx);
    if (swipeDragging) {
      swipeOffset = swipeOffset >= static_cast<float>(kActionWidth) * 0.42f
                        ? static_cast<float>(kActionWidth)
                        : 0.0f;
      if (swipeOffset <= 0.0f)
        revealedRow = -1;
      swipeDragging = false;
      swipeCandidateRow = -1;
      repaint();
      return;
    }

    if (idx == revealedRow && swipeOffset > 0.0f) {
      const auto pos = event.position.toInt();
      const auto &id = rows[static_cast<size_t>(idx)].id;
      if (renameActionBounds(rowBounds).contains(pos)) {
        if (onSetlistRenameRequested)
          onSetlistRenameRequested(id);
        swipeOffset = 0.0f;
        revealedRow = -1;
        repaint();
        return;
      }
      if (deleteActionBounds(rowBounds).contains(pos)) {
        if (onSetlistDeleteRequested)
          onSetlistDeleteRequested(id);
        swipeOffset = 0.0f;
        revealedRow = -1;
        repaint();
        return;
      }
      swipeOffset = 0.0f;
      revealedRow = -1;
      repaint();
      return;
    }

    const auto optionsBounds = optionsButtonBoundsForRow(rowBounds);
    if (optionsBounds.contains(event.position.toInt())) {
      if (onSetlistOptionsRequested) {
        onSetlistOptionsRequested(
            rows[static_cast<size_t>(idx)].id,
            getLocalArea(this, optionsBounds.expanded(2)));
      }
      return;
    }

    if (onSetlistChosen)
      onSetlistChosen(rows[static_cast<size_t>(idx)].id);
  }

  void mouseDoubleClick(const juce::MouseEvent &event) override {
    juce::ignoreUnused(event);
  }

private:
  static constexpr int kActionWidth = 112;

  static juce::Rectangle<int>
  renameActionBounds(const juce::Rectangle<int> &rowBounds) {
    return rowBounds.withLeft(rowBounds.getRight() - kActionWidth)
        .removeFromLeft(kActionWidth / 2);
  }

  static juce::Rectangle<int>
  deleteActionBounds(const juce::Rectangle<int> &rowBounds) {
    return rowBounds.withLeft(rowBounds.getRight() - (kActionWidth / 2));
  }

  juce::Rectangle<int> rowBoundsAt(const int rowIndex) const {
    constexpr int rowHeight = 64;
    return juce::Rectangle<int>(0, rowIndex * rowHeight, getWidth(), rowHeight);
  }

  juce::Rectangle<int>
  optionsButtonBoundsForRow(const juce::Rectangle<int> &rowBounds) const {
    auto c = rowBounds.reduced(16, 8);
    c.removeFromLeft(4);
    c.removeFromLeft(12);
    auto optionsRect = c.removeFromRight(40);
    return optionsRect;
  }

  static void drawOptionsButton(juce::Graphics &g, juce::Rectangle<int> bounds) {
    auto b = bounds.toFloat();
    drawIconGlyph(g, b, IconGlyph::More, UITheme::textSecondary, 1.4f);
  }

  static juce::String formatDurationHMS(const double totalSeconds) {
    const int seconds =
        juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
    const int hh = seconds / 3600;
    const int mm = (seconds % 3600) / 60;
    const int ss = seconds % 60;
    return hh > 0 ? juce::String::formatted("%02d:%02d:%02d", hh, mm, ss)
                  : juce::String::formatted("%02d:%02d", mm, ss);
  }

  int rowIndexAtY(const int y) const {
    if (y < 0)
      return -1;
    constexpr int rowHeight = 64;
    const int index = y / rowHeight;
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
      return -1;
    return index;
  }

  std::vector<Row> rows;
  juce::String selectedSetlistId;
  int revealedRow = -1;
  int swipeCandidateRow = -1;
  float swipeOffset = 0.0f;
  float swipeStartOffset = 0.0f;
  juce::Point<float> swipeStartPosition;
  bool swipeDragging = false;
};


SongListPanel::SongListPanel() {
  selectedSongId = AppSettings::getLastSelectedSongId();
  pageContainer = std::make_unique<PageContainer>();
  addAndMakeVisible(*pageContainer);
  auto &setlistsPage = pageContainer->getPage(0);
  auto &songsPage = pageContainer->getPage(1);
  auto &libraryPage = pageContainer->getPage(2);

  titleLabel.setJustificationType(juce::Justification::centredLeft);
  titleLabel.setFont(makeAppFont(17.0f, AppFontWeight::SemiBold));
  titleLabel.setColour(juce::Label::textColourId,
                       AppColours::textPrimary.withAlpha(0.96f));
  songsPage.addAndMakeVisible(titleLabel);

  songsSubtitleLabel.setJustificationType(juce::Justification::centredLeft);
  songsSubtitleLabel.setFont(makeAppFont(11.2f, AppFontWeight::Regular));
  songsSubtitleLabel.setColour(
      juce::Label::textColourId, UITheme::textSecondary.withAlpha(0.88f));
  songsSubtitleLabel.setVisible(false);
  songsPage.addAndMakeVisible(songsSubtitleLabel);

  setlistsHeaderLabel.setJustificationType(juce::Justification::centredLeft);
  setlistsHeaderLabel.setFont(makeAppFont(19.0f, AppFontWeight::Bold));
  setlistsHeaderLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  setlistsHeaderLabel.setText(L10n::tr("Repertorio", "Repertoire"),
                              juce::dontSendNotification);
  setlistsPage.addAndMakeVisible(setlistsHeaderLabel);

  setlistsSubtitleLabel.setJustificationType(juce::Justification::centredLeft);
  setlistsSubtitleLabel.setFont(makeAppFont(12.4f, AppFontWeight::Medium));
  setlistsSubtitleLabel.setColour(juce::Label::textColourId,
                                  UITheme::textSecondary.withAlpha(0.95f));
  setlistsSubtitleLabel.setText(L10n::tr("Mis setlists", "My setlists"),
                                juce::dontSendNotification);
  setlistsPage.addAndMakeVisible(setlistsSubtitleLabel);

  backButton.setSvgLabel(SvgIconName::ArrowLeft, {});
  backButton.onClick = [this] { showSetlistsView(); };
  songsPage.addAndMakeVisible(backButton);

  headerMenuButton.setTooltip(
      L10n::tr("Abrir biblioteca", "Open library"));
  headerMenuButton.setSvgLabel(SvgIconName::Music, {});
  headerMenuButton.onClick = [this] { openLibraryBrowser(); };
  songsPage.addAndMakeVisible(headerMenuButton);

  addSongsButton.setTooltip(
      L10n::tr("Agregar canciones", "Add songs"));
  addSongsButton.setAccent(true);
  addSongsButton.setSvgLabel(SvgIconName::Plus, {});
  addSongsButton.onClick = [this] { openAddFromLibraryMenu(); };
  songsPage.addAndMakeVisible(addSongsButton);

  createSetlistButton.setTooltip(
      L10n::tr("Crear setlist", "Create setlist"));
  createSetlistButton.setAccent(true);
  createSetlistButton.setSvgLabel(SvgIconName::Plus, {});
  createSetlistButton.onClick = [this] { createSetlist(); };
  setlistsPage.addAndMakeVisible(createSetlistButton);

  auto configureCloseButton = [this](HeaderIconButton &button,
                                     juce::Component &page) {
    button.setTooltip(L10n::tr("Cerrar", "Close"));
    button.setSubtleHighlight(true);
    button.onClick = [this] {
      if (onOverlayCloseRequested)
        onOverlayCloseRequested();
    };
    page.addAndMakeVisible(button);
    button.setVisible(false);
  };
  configureCloseButton(closeSetlistsButton, setlistsPage);
  configureCloseButton(closeSongsButton, songsPage);
  configureCloseButton(closeLibraryButton, libraryPage);

  setlistSearchField.setPlaceholderText(
      L10n::tr("Buscar setlist...", "Search setlist..."));
  setlistSearchField.setIconGlyph(IconGlyph::Search);
  setlistSearchField.setOnTextChange([this](const juce::String &text) {
    setlistSearchQuery = text.trim().toLowerCase();
    refreshFromStorage();
  });
  setlistsPage.addAndMakeVisible(setlistSearchField);

  songsHeaderPanel.setOnAddSongsClicked([this] { openAddFromLibraryMenu(); });
  songsHeaderPanel.setOnPadsClicked([this] {
    if (onPadsRequested)
      onPadsRequested();
  });
  songsHeaderPanel.setOnSongsClicked([this] { listViewport.setViewPosition(0, 0); });
  songsHeaderPanel.setLibraryActionVisible(false);
  songsPage.addAndMakeVisible(songsHeaderPanel);

  listViewport.setViewedComponent(&listContent, false);
  listViewport.setScrollBarsShown(true, false, false, false);
  listViewport.setScrollBarThickness(1);
  listViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  songsPage.addAndMakeVisible(listViewport);

  libraryCatalogPanel.setOnSongLoadRequested(
      [this](const ImportedSongData &song, int songIndex) {
        juce::ignoreUnused(songIndex);
        const auto requestedSong = song;
        if (!activateVisibleSetlistForSong(requestedSong)) return;
        selectedSongId = requestedSong.id;
        if (!remoteLibrary) AppSettings::setLastSelectedSongId(selectedSongId);
        if (onSongActivated != nullptr) {
          const int indexForSetlist = getSongIndexById(requestedSong.id);
          onSongActivated(requestedSong, indexForSetlist >= 0 ? indexForSetlist : -1);
        }
      });
  libraryCatalogPanel.setOnAddSongsToSetlist([this](const juce::StringArray &songIds) {
    const auto targetSetlistId = libraryTargetSetlistId.isNotEmpty()
                                     ? libraryTargetSetlistId
                                     : activeSetlistId;
    const bool returnToSetlistSongs = libraryTargetSetlistId.isNotEmpty();
    if (targetSetlistId.isEmpty() || songIds.isEmpty())
      return;
    for (const auto &songId : songIds) {
      if (songId.isEmpty())
        continue;
      if (remoteLibrary) { if (onRemoteAction) onRemoteAction("setlist.add", songId, targetSetlistId); }
      else SongLibraryStore::addSongToSetlist(targetSetlistId, songId);
    }

    refreshFromStorage();
    if (returnToSetlistSongs) {
      libraryTargetSetlistId.clear();
      libraryCatalogPanel.setTargetSetlistId({});
      showSongsSubview(true);
    }
  });
  libraryCatalogPanel.setOnCreateSongRequested(
      [this] { openSongImportWizard(true); });
  libraryCatalogPanel.setOnOpenProjectRequested(
      [this] { openProjectFileChooser(); });
  libraryCatalogPanel.setOnSongEditRequested(
      [this](const juce::String &songId) { openSongEditWizard(songId); });
  libraryCatalogPanel.setOnSongConvertRequested(
      [this](const juce::String &songId) {
        beginLegacySongConversion(songId);
      });
  libraryCatalogPanel.setOnProjectUploadRequested(
      [this](const juce::String &songId) {
        const auto song = std::find_if(
            librarySongs.begin(), librarySongs.end(),
            [&songId](const ImportedSongData &candidate) {
              return candidate.id == songId;
            });
        if (song != librarySongs.end() && !remoteLibrary && onProjectUploadRequested != nullptr)
          onProjectUploadRequested(*song);
      });
  libraryCatalogPanel.setOnCloudDownloadRequested(
      [this](const juce::String &songId, const juce::String &targetSetlistId) {
        if (!remoteLibrary && !remoteLibrary && onCloudDownloadRequested != nullptr)
          onCloudDownloadRequested(songId, targetSetlistId);
      });
  libraryCatalogPanel.setOnCloudSearchRequested(
      [this](const juce::String &query) {
        if (!remoteLibrary && !remoteLibrary && onCloudSearchRequested != nullptr)
          onCloudSearchRequested(query);
      });
  libraryCatalogPanel.setOnCloudDeleteRequested(
      [this](const juce::String &songId, const juce::String &title) {
        if (!remoteLibrary && !remoteLibrary && onCloudDeleteRequested != nullptr)
          onCloudDeleteRequested(songId, title);
      });
  libraryCatalogPanel.setOnCloudSettingsSyncRequested(
      [this](const juce::String &songId) {
        const auto song = std::find_if(
            librarySongs.begin(), librarySongs.end(),
            [&songId](const ImportedSongData &candidate) {
              return candidate.id == songId;
            });
        if (song != librarySongs.end() && !remoteLibrary && onCloudSettingsSyncRequested != nullptr)
          onCloudSettingsSyncRequested(*song);
      });
  libraryCatalogPanel.setOnCloudSettingsVersionsRequested(
      [this](const juce::String &songId, juce::Rectangle<int> screenAnchor) {
        const auto song = std::find_if(
            librarySongs.begin(), librarySongs.end(),
            [&songId](const ImportedSongData &candidate) {
              return candidate.id == songId;
            });
        if (song != librarySongs.end() && !remoteLibrary && onCloudSettingsVersionsRequested != nullptr)
          onCloudSettingsVersionsRequested(*song, screenAnchor);
      });
  libraryCatalogPanel.setCloudSettingsPendingProvider(
      [this](const juce::String &songId) {
        const auto song = std::find_if(
            librarySongs.begin(), librarySongs.end(),
            [&songId](const ImportedSongData &candidate) {
              return candidate.id == songId;
            });
        return song != librarySongs.end() &&
               cloudSettingsPendingProvider != nullptr &&
               cloudSettingsPendingProvider(*song);
      });
  libraryCatalogPanel.setOnSongDeleteRequested([this](const juce::String &songId) {
    if (remoteLibrary) return;
    const auto trimmedSongId = songId.trim();
    if (trimmedSongId.isEmpty())
      return;
    juce::String songTitle;
    const auto it =
        std::find_if(librarySongs.begin(), librarySongs.end(),
                     [&trimmedSongId](const ImportedSongData &song) {
                       return song.id == trimmedSongId;
                     });
    if (it != librarySongs.end())
      songTitle = it->title.trim();
    if (songTitle.isEmpty())
      songTitle = L10n::tr(u8"(Sin título)", "(Untitled)");

    const auto confirmText = L10n::tr(
        juce::String("Eliminar \"") + songTitle +
            "\" de la biblioteca? Esta accion no se puede deshacer.",
        juce::String("Delete \"") + songTitle +
            "\" from the library? This action can't be undone.");

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle(L10n::tr(u8"Eliminar canción", "Delete song"))
            .withMessage(confirmText)
            .withButton(L10n::tr("Eliminar", "Delete"))
            .withButton(L10n::tr("Cancelar", "Cancel")),
        [this, trimmedSongId](const int result) {
          if (result != 1)
            return;
          SongLibraryStore::deleteSong(trimmedSongId);
          selectedSongId = {};
          refreshFromStorage();
        });
  });
  libraryCatalogPanel.setOnBackRequested([this] {
    libraryTargetSetlistId.clear();
    libraryCatalogPanel.setTargetSetlistId({});
    showSongsSubview(true);
  });
  libraryPage.addAndMakeVisible(libraryCatalogPanel);
  setlistsContent = std::make_unique<SetlistsContent>();
  setlistsContent->onSetlistChosen = [this](const juce::String &setlistId) {
    showSongsView(setlistId);
  };
  setlistsContent->onSetlistRenameRequested =
      [this](const juce::String &setlistId) { renameSetlistById(setlistId); };
  setlistsContent->onSetlistDeleteRequested =
      [this](const juce::String &setlistId) { deleteSetlistById(setlistId); };
  setlistsContent->onSetlistOptionsRequested =
      [this](const juce::String &setlistId, juce::Rectangle<int> targetArea) {
        openSetlistOptionsMenu(setlistId, targetArea);
      };
  setlistsViewport.setViewedComponent(setlistsContent.get(), false);
  setlistsViewport.setScrollBarsShown(true, false, false, false);
  setlistsViewport.setScrollBarThickness(1);
  setlistsPage.addAndMakeVisible(setlistsViewport);

  listContent.setOnSongActivated([this](int songIndex) {
    activateSongAtIndex(songIndex);
  });
  listContent.setOnSongsReordered([this](int fromIndex, int toIndex) {
    if (activeSetlistId.isEmpty())
      return;
    if (fromIndex == toIndex)
      return;
    if (!juce::isPositiveAndBelow(fromIndex,
                                  static_cast<int>(activeSongIndices.size())) ||
        !juce::isPositiveAndBelow(toIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    if (remoteLibrary) { if (onRemoteAction) onRemoteAction("setlist.move", juce::var(juce::Array<juce::var>{fromIndex,toIndex}), activeSetlistId); return; }
    if (SongLibraryStore::moveSongInSetlist(activeSetlistId, fromIndex,
                                            toIndex)) {
      refreshFromStorage();
    }
  });
  listContent.setOnDragStateChanged([this](const bool dragging) {
    listViewport.setScrollOnDragMode(
        dragging ? juce::Viewport::ScrollOnDragMode::never
                 : juce::Viewport::ScrollOnDragMode::all);
  });
  listContent.setOnAutoPlayNextToggled([this](int songIndex) {
    if (!juce::isPositiveAndBelow(songIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    const int libraryIndex = activeSongIndices[static_cast<size_t>(songIndex)];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      return;
    const auto songId = librarySongs[static_cast<size_t>(libraryIndex)].id;
    const auto key = makeAutoPlayNextMarkKey(activeSetlistId, songId);
    if (key.isEmpty())
      return;
    const bool shouldEnable = !autoPlayNextMarks.contains(key);
    if (remoteLibrary) {
      auto *v = new juce::DynamicObject(); v->setProperty("songId", songId); v->setProperty("enabled", shouldEnable);
      if (onRemoteAction) onRemoteAction("setlist.autoplay", juce::var(v), activeSetlistId); return;
    }
    if (!SongLibraryStore::setAutoPlayNextForSetlist(
            activeSetlistId, songId, shouldEnable))
      return;
    refreshFromStorage();
  });
  listContent.setOnSongRemoveRequested([this](int songIndex) {
    if (activeSetlistId.isEmpty())
      return;
    if (!juce::isPositiveAndBelow(songIndex,
                                  static_cast<int>(activeSongIndices.size())))
      return;
    const int libraryIndex = activeSongIndices[static_cast<size_t>(songIndex)];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      return;
    const auto songId = librarySongs[static_cast<size_t>(libraryIndex)].id;
    if (songId.isEmpty())
      return;
    if (remoteLibrary) { if (onRemoteAction) onRemoteAction("setlist.remove", songId, activeSetlistId); return; }
    SongLibraryStore::removeSongFromSetlist(activeSetlistId, songId);
    autoPlayNextMarks.removeString(
        makeAutoPlayNextMarkKey(activeSetlistId, songId));
    refreshFromStorage();
  });
  refreshThemeColours();
  refreshFromStorage();
  showPage(viewMode == ViewMode::Setlists ? 0 : 1, false);
}

SongListPanel::~SongListPanel() = default;

void SongListPanel::setMinimalPhoneMode(const bool enabled) {
  if (minimalPhoneMode == enabled)
    return;
  minimalPhoneMode = enabled;
  songsHeaderPanel.setVisible(!minimalPhoneMode);
  closeSetlistsButton.setVisible(minimalPhoneMode);
  closeSongsButton.setVisible(minimalPhoneMode);
  closeLibraryButton.setVisible(minimalPhoneMode);
  songsSubtitleLabel.setVisible(minimalPhoneMode);

  // iOS navigation bars and Material top app bars both use flat icon actions.
  // Preserve each platform's touch target while removing desktop chrome.
  for (auto *button : std::array<HeaderIconButton *, 7>{
           &backButton, &headerMenuButton, &addSongsButton,
           &createSetlistButton, &closeSetlistsButton, &closeSongsButton,
           &closeLibraryButton})
    button->setBare(minimalPhoneMode);
  libraryCatalogPanel.setMinimalPhoneMode(minimalPhoneMode);

  titleLabel.setFont(makeAppFont(minimalPhoneMode ? 18.0f : 17.0f,
                                 AppFontWeight::SemiBold));
  setlistsHeaderLabel.setFont(
      makeAppFont(minimalPhoneMode ? 18.0f : 19.0f,
                  minimalPhoneMode ? AppFontWeight::SemiBold
                                   : AppFontWeight::Bold));
  setlistsSubtitleLabel.setFont(
      makeAppFont(minimalPhoneMode ? 11.2f : 12.4f,
                  minimalPhoneMode ? AppFontWeight::Regular
                                   : AppFontWeight::Medium));
  resized();
}

void SongListPanel::setPhoneSafeAreaInsets(
    const juce::BorderSize<int> insets) {
  if (phoneSafeAreaInsets == insets)
    return;
  phoneSafeAreaInsets = insets;
  if (minimalPhoneMode)
    resized();
}

void SongListPanel::setOnOverlayCloseRequested(std::function<void()> cb) {
  onOverlayCloseRequested = std::move(cb);
}

void SongListPanel::setOnSongActivated(
    std::function<void(const ImportedSongData &, int)> cb) {
  onSongActivated = std::move(cb);
}

void SongListPanel::setOnSongEditStarted(
    std::function<void(const juce::String &)> cb) {
  onSongEditStarted = std::move(cb);
}

void SongListPanel::setOnSongEdited(
    std::function<void(const ImportedSongData &, int)> cb) {
  onSongEdited = std::move(cb);
}

void SongListPanel::setOnProjectUploadRequested(
    std::function<void(const ImportedSongData &)> cb) {
  onProjectUploadRequested = std::move(cb);
}

void SongListPanel::setOnCloudDownloadRequested(
    std::function<void(const juce::String &, const juce::String &)> cb) {
  onCloudDownloadRequested = std::move(cb);
}

void SongListPanel::setOnCloudSearchRequested(
    std::function<void(const juce::String &)> cb) {
  onCloudSearchRequested = std::move(cb);
}

void SongListPanel::setOnCloudDeleteRequested(
    std::function<void(const juce::String &, const juce::String &)> cb) {
  onCloudDeleteRequested = std::move(cb);
}

void SongListPanel::setOnCloudSettingsSyncRequested(
    std::function<void(const ImportedSongData &)> cb) {
  onCloudSettingsSyncRequested = std::move(cb);
}

void SongListPanel::setOnCloudSettingsVersionsRequested(
    std::function<void(const ImportedSongData &, juce::Rectangle<int>)> cb) {
  onCloudSettingsVersionsRequested = std::move(cb);
}

void SongListPanel::setCloudSettingsPendingProvider(
    std::function<bool(const ImportedSongData &)> provider) {
  cloudSettingsPendingProvider = std::move(provider);
}

void SongListPanel::setCloudUploadProgress(const juce::String &songId,
                                           const double fraction,
                                           const juce::String &phase) {
  if (songId.isEmpty())
    return;
  cloudUploadIndicators[songId.toStdString()] = {
      juce::jlimit(0.0, 1.0, fraction), phase};
  listContent.setCloudUploadProgress(songId, fraction, phase);
  libraryCatalogPanel.setCloudUploadProgress(songId, fraction, phase);
}

void SongListPanel::clearCloudUploadProgress(const juce::String &songId) {
  cloudUploadIndicators.erase(songId.toStdString());
  listContent.setCloudUploadProgress(songId, -1.0, {});
  libraryCatalogPanel.clearCloudUploadProgress(songId);
}

void SongListPanel::setCloudSongs(std::vector<CloudSongSummary> songs) {
  libraryCatalogPanel.setCloudSongs(std::move(songs));
}

void SongListPanel::setCloudSearchLoading(const bool isLoading) {
  libraryCatalogPanel.setCloudSearchLoading(isLoading);
}

void SongListPanel::setOnSetlistNameChanged(
    std::function<void(const juce::String &)> cb) {
  onSetlistNameChanged = std::move(cb);
}

int SongListPanel::getSongCount() const {
  return static_cast<int>(activeSongIndices.size());
}

const ImportedSongData *SongListPanel::getSongPtr(int index) const {
  if (!juce::isPositiveAndBelow(index,
                                static_cast<int>(activeSongIndices.size())))
    return nullptr;
  const int libraryIndex = activeSongIndices[static_cast<size_t>(index)];
  if (!juce::isPositiveAndBelow(libraryIndex,
                                static_cast<int>(librarySongs.size())))
    return nullptr;
  return &librarySongs[static_cast<size_t>(libraryIndex)];
}

int SongListPanel::getSongIndexById(const juce::String &songId) const {
  if (songId.isEmpty())
    return -1;

  for (size_t i = 0; i < activeSongIndices.size(); ++i) {
    const int libraryIndex = activeSongIndices[i];
    if (!juce::isPositiveAndBelow(libraryIndex,
                                  static_cast<int>(librarySongs.size())))
      continue;
    if (librarySongs[static_cast<size_t>(libraryIndex)].id == songId)
      return static_cast<int>(i);
  }

  return -1;
}

void SongListPanel::setSelectedSongIndex(const int songIndex) {
  if (!juce::isPositiveAndBelow(songIndex,
                                static_cast<int>(activeSongIndices.size())))
    return;

  const int libraryIndex = activeSongIndices[static_cast<size_t>(songIndex)];
  if (!juce::isPositiveAndBelow(libraryIndex,
                                static_cast<int>(librarySongs.size())))
    return;

  selectedSongId = librarySongs[static_cast<size_t>(libraryIndex)].id;
  lastActivatedIndex = songIndex;
  if (!remoteLibrary) AppSettings::setLastSelectedSongId(selectedSongId);
  listContent.setSelectedSongId(selectedSongId);
}

bool SongListPanel::isAutoPlayNextEnabledForSong(
    const juce::String &songId) const {
  const auto key = makeAutoPlayNextMarkKey(activeSetlistId, songId);
  return key.isNotEmpty() && autoPlayNextMarks.contains(key);
}

int SongListPanel::resolveAutoPlayNextIndex(
    const juce::String &currentSongId) const {
  if (currentSongId.isEmpty() || activeSetlistId.isEmpty() || activeSongIndices.empty())
    return -1;

  const int currentIndex = getSongIndexById(currentSongId);
  if (currentIndex < 0)
    return -1;
  if (!isAutoPlayNextEnabledForSong(currentSongId))
    return -1;

  const int immediateNext = currentIndex + 1;
  return juce::isPositiveAndBelow(
             immediateNext, static_cast<int>(activeSongIndices.size()))
             ? immediateNext
             : -1;
}

std::optional<ImportedSongData> SongListPanel::getPlaybackAdjacentSong(
    const juce::String &currentSongId, int direction, bool wrap) const {
  if (currentSongId.isEmpty() || direction == 0) return std::nullopt;
  const auto setlist = std::find_if(playbackLibrary.setlists.begin(), playbackLibrary.setlists.end(),
      [this](const SetlistData &candidate) { return candidate.id == playbackLibrary.activeSetlistId; });
  if (setlist == playbackLibrary.setlists.end() || setlist->items.empty()) return std::nullopt;
  std::vector<const ImportedSongData *> playable;
  for (const auto &item : setlist->items) {
    const auto song = std::find_if(playbackLibrary.songs.begin(), playbackLibrary.songs.end(),
        [&item](const ImportedSongData &candidate) { return candidate.id == item.songId; });
    if (song != playbackLibrary.songs.end()) playable.push_back(&*song);
  }
  const auto current = std::find_if(playable.begin(), playable.end(),
      [&currentSongId](const ImportedSongData *candidate) { return candidate->id == currentSongId; });
  if (current == playable.end()) return std::nullopt;
  int target = static_cast<int>(std::distance(playable.begin(), current)) + (direction < 0 ? -1 : 1);
  const auto count = static_cast<int>(playable.size());
  if (wrap) target = (target + count) % count;
  if (!juce::isPositiveAndBelow(target, count) || playable[static_cast<size_t>(target)]->id == currentSongId)
    return std::nullopt;
  return *playable[static_cast<size_t>(target)];
}

std::optional<ImportedSongData> SongListPanel::getAutoPlayNextSong(
    const juce::String &currentSongId) const {
  const auto setlist = std::find_if(playbackLibrary.setlists.begin(), playbackLibrary.setlists.end(),
      [this](const SetlistData &candidate) { return candidate.id == playbackLibrary.activeSetlistId; });
  if (setlist == playbackLibrary.setlists.end()) return std::nullopt;
  const auto current = std::find_if(setlist->items.begin(), setlist->items.end(),
      [&currentSongId](const SetlistItem &candidate) { return candidate.songId == currentSongId; });
  if (current == setlist->items.end() || !current->transitionToNext.autoPlay) return std::nullopt;
  return getPlaybackAdjacentSong(currentSongId, 1, false);
}

void SongListPanel::setNowPlayingSong(const juce::String &songId,
                                      bool isPlaying) {
  if (nowPlayingSongId == songId && isPlaybackActive == isPlaying)
    return;
  nowPlayingSongId = songId;
  isPlaybackActive = isPlaying;
  listContent.setNowPlayingSong(songId, isPlaying);
  libraryCatalogPanel.setNowPlayingSong(songId, isPlaying);
}

int SongListPanel::getLastActivatedIndex() const { return lastActivatedIndex; }

juce::String SongListPanel::getActiveSetlistName() const {
  return activeSetlistName;
}

void SongListPanel::refreshFromStorageView() { refreshFromStorage(); }

void SongListPanel::openLocalLibrary() {
  refreshFromStorage();
  openLibraryBrowser();
}

void SongListPanel::activateSongAtIndex(const int songIndex) {
  const auto *song = getSongPtr(songIndex);
  if (song == nullptr)
    return;
  const auto requestedSong = *song;
  if (!activateVisibleSetlistForSong(requestedSong)) return;
  const int resolvedIndex = getSongIndexById(requestedSong.id);
  setSelectedSongIndex(resolvedIndex);
  if (onSongActivated)
    onSongActivated(requestedSong, resolvedIndex);
}

bool SongListPanel::activateVisibleSetlistForSong(const ImportedSongData &song) {
  if (remoteLibrary || activeSetlistId.isEmpty() || getSongIndexById(song.id) < 0 ||
      activeSetlistId == playbackLibrary.activeSetlistId) return true;
  // An explicit row activation commits the visible list before the loader
  // reads its mix, pitch, tempo and transport queue.
  const auto requestedSetlistId = activeSetlistId;
  if (!SongLibraryStore::setActiveSetlistId(requestedSetlistId)) return false;
  refreshFromStorage();
  return true;
}

void SongListPanel::showActiveSetlist() {
  // Opening the drawer preserves the active transport setlist.
  viewMode = ViewMode::Songs;
  songsSubview = SongsSubview::SetlistSongs;
  initialViewApplied = true;
  refreshFromStorage();
  showPage(1, true);
}

void SongListPanel::showSongPicker(juce::Component &anchor,
                                  std::function<void()> onAdd) {
  class SongPicker final : public juce::Component,
                           private juce::ListBoxModel {
  public:
    struct Entry {
      juce::String id, title, artist, artworkPath;
      bool current = false;
    };

    SongPicker(std::vector<Entry> items,
               std::function<void(juce::String)> callback,
               std::function<void()> addCallback)
        : entries(std::move(items)), onSelect(std::move(callback)) {
      setWantsKeyboardFocus(true);
      addButton.setTooltip(L10n::tr("Biblioteca y setlists", "Library and setlists"));
      addButton.setAccent(true);
      addButton.onClick = [addCallback] {
        dismissActiveActionPopover();
        juce::MessageManager::callAsync(addCallback);
      };
      addAndMakeVisible(addButton);
      list.setModel(this);
      list.setRowHeight(48);
      list.setColour(juce::ListBox::backgroundColourId,
                     juce::Colours::transparentBlack);
      list.setOutlineThickness(0);
      list.getViewport()->setScrollBarsShown(true, false);
      list.getViewport()->setScrollBarThickness(2);
      list.getViewport()->setScrollOnDragMode(
          juce::Viewport::ScrollOnDragMode::all);
      addAndMakeVisible(list);
      setSize(280, 44 + (entries.empty() ? 64 :
                        juce::jmin(5, static_cast<int>(entries.size())) * 48));
      for (int i = 0; i < getNumRows(); ++i)
        if (entries[static_cast<size_t>(i)].current) {
          list.selectRow(i);
          break;
        }
    }

    void paint(juce::Graphics &g) override {
      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(13.0f, AppFontWeight::SemiBold));
      g.drawText(L10n::tr("Canciones del setlist", "Setlist songs"),
                 getLocalBounds().removeFromTop(44).withTrimmedRight(44).reduced(10, 0),
                 juce::Justification::centredLeft);
      if (entries.empty()) {
        g.setColour(UITheme::textSecondary);
        g.setFont(makeAppFont(13.0f));
        g.drawFittedText(L10n::tr("No hay canciones en el setlist activo.",
                                 "There are no songs in the active setlist."),
                         getLocalBounds().withTrimmedTop(44).reduced(12),
                         juce::Justification::centred, 2);
      }
    }

    void resized() override {
      addButton.setBounds(getWidth() - 44, 0, 44, 44);
      list.setBounds(getLocalBounds().withTrimmedTop(44));
      list.setVisible(!entries.empty());
    }

  private:
    int getNumRows() override { return static_cast<int>(entries.size()); }

    void paintListBoxItem(int index, juce::Graphics &g, int width,
                          int height, bool selected) override {
      if (!juce::isPositiveAndBelow(index, getNumRows()))
        return;
      const auto &entry = entries[static_cast<size_t>(index)];
      auto row = juce::Rectangle<int>(width, height).reduced(4, 2);
      g.setColour(entry.current || selected ? UITheme::accent.withAlpha(0.16f)
                                            : UITheme::cardBg);
      g.fillRoundedRectangle(row.toFloat(), 8.0f);
      auto text = row.reduced(8, 5);
      g.setColour(UITheme::textSecondary);
      g.setFont(makeAppFont(12.0f));
      g.drawText(juce::String(index + 1), text.removeFromLeft(20),
                 juce::Justification::centredLeft);

      const auto artworkBounds = text.removeFromLeft(32)
                                     .withSizeKeepingCentre(32, 32).toFloat();
      text.removeFromLeft(8);
      const auto artworkKey = entry.artworkPath.toStdString();
      auto cached = artworkCache.find(artworkKey);
      if (cached == artworkCache.end()) {
        juce::Image artwork;
        if (juce::File::isAbsolutePath(entry.artworkPath)) {
          const juce::File file(entry.artworkPath);
          if (file.existsAsFile())
            artwork = juce::ImageFileFormat::loadFrom(file);
        }
        cached = artworkCache.emplace(artworkKey, std::move(artwork)).first;
      }
      {
        juce::Graphics::ScopedSaveState saved(g);
        juce::Path clip;
        clip.addRoundedRectangle(artworkBounds, 4.0f);
        g.reduceClipRegion(clip);
        if (cached->second.isValid()) {
          g.drawImage(cached->second, artworkBounds,
                       juce::RectanglePlacement::centred |
                           juce::RectanglePlacement::fillDestination);
        } else {
          g.setColour(UITheme::cardBgHover);
          g.fillRect(artworkBounds);
          drawIconGlyph(g, artworkBounds.reduced(7.0f), IconGlyph::MusicNote,
                         UITheme::textSecondary);
        }
      }
      if (entry.current)
        drawIconGlyph(g, text.removeFromRight(18).withSizeKeepingCentre(14, 14).toFloat(),
                       IconGlyph::Check, UITheme::accent);
      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(13.0f, AppFontWeight::SemiBold));
      g.drawFittedText(entry.title, text.removeFromTop(19),
                       juce::Justification::centredLeft, 1);
      g.setColour(UITheme::textSecondary);
      g.setFont(makeAppFont(10.5f));
      g.drawFittedText(entry.artist, text, juce::Justification::centredLeft, 1);
    }

    void listBoxItemClicked(int index, const juce::MouseEvent &) override {
      choose(index);
    }
    void returnKeyPressed(int index) override { choose(index); }

    void choose(int index) {
      if (selectionStarted || !juce::isPositiveAndBelow(index, getNumRows()))
        return;
      selectionStarted = true;
      const auto id = entries[static_cast<size_t>(index)].id;
      auto callback = onSelect;
      dismissActiveActionPopover();
      // Finish the row event and dismiss the callout before loading a project.
      juce::MessageManager::callAsync([callback, id] { callback(id); });
    }

    std::vector<Entry> entries;
    std::unordered_map<std::string, juce::Image> artworkCache;
    std::function<void(juce::String)> onSelect;
    juce::ListBox list;
    HeaderIconButton addButton{"addToSetlist", IconGlyph::Add};
    bool selectionStarted = false;
  };

  std::vector<SongPicker::Entry> entries;
  for (int i = 0; i < getSongCount(); ++i)
    if (const auto *song = getSongPtr(i))
      entries.push_back({song->id,
                         song->title.isNotEmpty() ? song->title
                             : L10n::tr("Canción sin título", "Untitled song"),
                         song->artist, song->artworkPath.trim(),
                         song->id == selectedSongId});

  const auto setlistId = activeSetlistId;
  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  auto picker = std::make_unique<SongPicker>(
      std::move(entries), [safeThis, setlistId](const juce::String &id) {
        if (safeThis != nullptr && safeThis->activeSetlistId == setlistId)
          safeThis->activateSongAtIndex(safeThis->getSongIndexById(id));
      }, std::move(onAdd));
  auto *parent = anchor.findParentComponentOfClass<juce::AudioProcessorEditor>();
  if (parent == nullptr)
    return;
  ActionPopoverOptions options;
  options.compactMaximumWidth = 280;
  presentActionPopover(std::move(picker),
                        parent->getLocalArea(&anchor, anchor.getLocalBounds()),
                        parent, options);
}

void SongListPanel::openSongImportWizardFromExternal() {
  openSongImportWizard();
}

void SongListPanel::refreshLanguageTexts() {
  setlistsHeaderLabel.setText(L10n::tr("Repertorio", "Repertoire"),
                              juce::dontSendNotification);
  setlistsSubtitleLabel.setText(L10n::tr("Mis setlists", "My setlists"),
                                juce::dontSendNotification);
  createSetlistButton.setTooltip(
      L10n::tr("Crear setlist", "Create setlist"));
  createSetlistButton.setSvgLabel(SvgIconName::Plus, {});
  setlistSearchField.setPlaceholderText(
      L10n::tr("Buscar setlist...", "Search setlist..."));
  headerMenuButton.setTooltip(
      L10n::tr("Abrir biblioteca", "Open library"));
  headerMenuButton.setSvgLabel(SvgIconName::Music, {});
  addSongsButton.setTooltip(
      L10n::tr("Agregar canciones", "Add songs"));
  addSongsButton.setSvgLabel(SvgIconName::Plus, {});
  songsHeaderPanel.refreshLanguageTexts();
  libraryCatalogPanel.refreshLanguageTexts();
  refreshFromStorage();
}

void SongListPanel::refreshThemeColours() {
  titleLabel.setColour(juce::Label::textColourId,
                       AppColours::textPrimary.withAlpha(0.96f));
  songsSubtitleLabel.setColour(
      juce::Label::textColourId, UITheme::textSecondary.withAlpha(0.88f));
  setlistsHeaderLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  setlistsSubtitleLabel.setColour(juce::Label::textColourId,
                                  UITheme::textSecondary.withAlpha(0.95f));

  setlistSearchField.refreshThemeColours();
  libraryCatalogPanel.refreshThemeColours();
  repaint();
}

void SongListPanel::paint(juce::Graphics &g) {
  // Edge-to-edge fill, matching SongImportWizardComponent::paint: the panel
  // becomes its own "page" of a vertical app. The rounded-card treatment used
  // to live here on every platform, but on phone it made the drawer look like
  // a floating surface rather than a screen-filling overlay. In non-phone
  // modes we keep the rounded card via drawStudioBackground + the legacy
  // border so desktop/tablet still have a discrete card around the panel.
  const auto bounds = getLocalBounds().toFloat();
  if (minimalPhoneMode) {
    drawStudioBackground(g, bounds);
  } else {
    g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.38f));
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.86f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);
  }

  if (viewMode == ViewMode::Songs)
    return;

  const auto setlistsLayout = computeSetlistsLayout();
  const auto searchRow = setlistsLayout.searchRow;
  g.setColour(UITheme::cardBorder.withAlpha(0.20f));
  g.drawLine(static_cast<float>(searchRow.getX()),
             static_cast<float>(searchRow.getBottom() + 8),
             static_cast<float>(searchRow.getRight()),
             static_cast<float>(searchRow.getBottom() + 8), 1.0f);
}

void SongListPanel::resized() {
  pageContainer->setBounds(getLocalBounds());
  if (libraryCreationWizard != nullptr)
    libraryCreationWizard->setBounds(pageContainer->getPage(3).getLocalBounds());

  auto songsLayout = computeSongsLayout();
  applySongsLayout(songsLayout);
  const auto setlistsLayout = computeSetlistsLayout();
  applySetlistsLayout(setlistsLayout);
  auto libraryBounds =
      getPageContentBounds(minimalPhoneMode ? mobileTopBarHorizontalMargin()
                                            : 16);
  const int mobileActionSize = mobileTopBarActionSize();
  libraryCatalogPanel.setHeaderTrailingInset(
      minimalPhoneMode ? mobileActionSize + mobileTopBarActionGap() : 0);
  libraryCatalogPanel.setBounds(libraryBounds);
  closeLibraryButton.setBounds(
      minimalPhoneMode
          ? juce::Rectangle<int>(
                libraryBounds.getRight() - mobileActionSize,
                libraryBounds.getY() +
                    (mobileTopBarHeight() - mobileActionSize) / 2,
                mobileActionSize, mobileActionSize)
          : juce::Rectangle<int>());
  closeLibraryButton.toFront(false);

  listContent.setSize(juce::jmax(1, listViewport.getWidth()),
                      juce::jmax(listViewport.getHeight(),
                                 listContent.getPreferredHeight()));
  if (setlistsContent != nullptr) {
    setlistsContent->setSize(juce::jmax(1, setlistsViewport.getWidth()),
                             juce::jmax(setlistsViewport.getHeight(),
                                        setlistsContent->getPreferredHeight()));
  }
}

juce::Rectangle<int> SongListPanel::getPageContentBounds(
    const int margin) const {
  auto bounds = getLocalBounds();
  if (minimalPhoneMode)
    bounds = phoneSafeAreaInsets.subtractedFrom(bounds);
  return bounds.reduced(margin);
}

SongListPanel::SongsLayout SongListPanel::computeSongsLayout() const {
  SongsLayout layout;
  auto area = getPageContentBounds(
      minimalPhoneMode ? mobileTopBarHorizontalMargin() : 16);
  layout.workspace = area;

  int headerRowHeight = minimalPhoneMode ? mobileTopBarHeight() : 56;
  int headerPanelHeight = minimalPhoneMode ? 0 : (area.getHeight() < 560 ? 44 : 50);
  int gapAfterHeader = minimalPhoneMode ? 4 : (area.getHeight() < 560 ? 6 : 8);
  int gapAfterPanel = minimalPhoneMode ? 0 : (area.getHeight() < 560 ? 8 : 12);
  constexpr int minListHeight = 140;

  int availableListHeight = area.getHeight() - headerRowHeight -
                            headerPanelHeight - gapAfterHeader - gapAfterPanel;
  if (availableListHeight < minListHeight) {
    int deficit = minListHeight - availableListHeight;
    const int reducePanel = juce::jmin(deficit, juce::jmax(0, headerPanelHeight - 42));
    headerPanelHeight -= reducePanel;
    deficit -= reducePanel;
    const int reduceHeader = juce::jmin(deficit, juce::jmax(0, headerRowHeight - 52));
    headerRowHeight -= reduceHeader;
    deficit -= reduceHeader;
    const int reduceGap2 = juce::jmin(deficit, juce::jmax(0, gapAfterPanel - 4));
    gapAfterPanel -= reduceGap2;
    deficit -= reduceGap2;
    const int reduceGap1 = juce::jmin(deficit, juce::jmax(0, gapAfterHeader - 4));
    gapAfterHeader -= reduceGap1;
  }

  layout.headerRow = area.removeFromTop(headerRowHeight);
  area.removeFromTop(gapAfterHeader);
  layout.headerPanel = area.removeFromTop(headerPanelHeight);
  area.removeFromTop(gapAfterPanel);
  layout.listArea = area;

  auto headerGrid = layout.headerRow;
  const int actionSize = minimalPhoneMode ? mobileTopBarActionSize() : 40;
  const auto leftColumn = headerGrid.removeFromLeft(actionSize);
  const int libraryButtonWidth = actionSize;
  const int addButtonWidth = actionSize;
  const int actionGap = minimalPhoneMode ? mobileTopBarActionGap() : 12;
  if (minimalPhoneMode) {
    layout.close = headerGrid.removeFromRight(actionSize)
                       .withSizeKeepingCentre(actionSize, actionSize);
    headerGrid.removeFromRight(mobileTopBarActionGap());
  }
  const auto rightColumn =
      headerGrid.removeFromRight(libraryButtonWidth + actionGap + addButtonWidth);
  layout.back = leftColumn.withSizeKeepingCentre(actionSize, actionSize);
  auto actionRow = rightColumn;
  layout.menu = actionRow.removeFromLeft(libraryButtonWidth)
                    .withSizeKeepingCentre(libraryButtonWidth,
                                           libraryButtonWidth);
  actionRow.removeFromLeft(actionGap);
  layout.add = actionRow.withSizeKeepingCentre(addButtonWidth, addButtonWidth);
  layout.edit = {};
  if (minimalPhoneMode) {
    auto titleArea = headerGrid.reduced(8, 5);
    layout.title = titleArea.removeFromTop(25);
    layout.subtitle = titleArea.removeFromTop(17);
  } else {
    layout.title = headerGrid.withSizeKeepingCentre(headerGrid.getWidth(), 40);
  }
  return layout;
}

SongListPanel::SetlistsLayout SongListPanel::computeSetlistsLayout() const {
  SetlistsLayout layout;
  auto area = getPageContentBounds(
      minimalPhoneMode ? mobileTopBarHorizontalMargin() : 16);

  int headerHeight =
      minimalPhoneMode ? mobileTopBarHeight()
                       : (area.getHeight() < 560 ? 48 : 54);
  int searchHeight = minimalPhoneMode ? 44 : (area.getHeight() < 560 ? 34 : 36);
  int gap1 = area.getHeight() < 560 ? 8 : 10;
  int gap3 = area.getHeight() < 560 ? 8 : 10;
  constexpr int minViewportHeight = 140;

  int availableViewportHeight =
      area.getHeight() - headerHeight - searchHeight - gap1 - gap3;
  if (availableViewportHeight < minViewportHeight) {
    int deficit = minViewportHeight - availableViewportHeight;
    const int reduceHeader = juce::jmin(deficit, juce::jmax(0, headerHeight - 48));
    headerHeight -= reduceHeader;
    deficit -= reduceHeader;
    const int reduceSearch = juce::jmin(deficit, juce::jmax(0, searchHeight - 34));
    searchHeight -= reduceSearch;
    deficit -= reduceSearch;
    const int reduceGap3 = juce::jmin(deficit, juce::jmax(0, gap3 - 6));
    gap3 -= reduceGap3;
    deficit -= reduceGap3;
    const int reduceGap1 = juce::jmin(deficit, juce::jmax(0, gap1 - 6));
    gap1 -= reduceGap1;
  }

  auto headerRow = area.removeFromTop(headerHeight);
  const int createButtonSize =
      minimalPhoneMode ? mobileTopBarActionSize() : 40;
  if (minimalPhoneMode) {
    layout.close = headerRow.removeFromRight(createButtonSize)
                       .withSizeKeepingCentre(createButtonSize,
                                              createButtonSize);
    headerRow.removeFromRight(mobileTopBarActionGap());
  }
  const auto headerAction = headerRow.removeFromRight(createButtonSize);
  layout.createRow = headerAction.withSizeKeepingCentre(createButtonSize, createButtonSize);
  area.removeFromTop(gap1);
  layout.searchRow = area.removeFromTop(searchHeight);
  area.removeFromTop(gap3);

  auto headerCenterColumn = headerRow;

  if (minimalPhoneMode) {
    auto titleArea = headerCenterColumn.reduced(8, 5);
    layout.headerTitle = titleArea.removeFromTop(25);
    layout.headerSubtitle = titleArea.removeFromTop(17);
  } else {
    constexpr int titleHeight = 22;
    constexpr int subtitleHeight = 15;
    constexpr int titleSubtitleGap = 2;
    constexpr int totalTextHeight =
        titleHeight + subtitleHeight + titleSubtitleGap;
    const int textY = headerCenterColumn.getY() +
                      juce::jmax(0, (headerCenterColumn.getHeight() -
                                    totalTextHeight) /
                                       2);
    layout.headerTitle = juce::Rectangle<int>(
        headerCenterColumn.getX(), textY, headerCenterColumn.getWidth(),
        titleHeight);
    layout.headerSubtitle = juce::Rectangle<int>(
        headerCenterColumn.getX(), textY + titleHeight + titleSubtitleGap,
        headerCenterColumn.getWidth(), subtitleHeight);
  }
  layout.viewport = area;
  return layout;
}

void SongListPanel::applySongsLayout(const SongsLayout &layout) {
  updateSongsSubviewLayout(layout);
  backButton.setBounds(layout.back);
  titleLabel.setBounds(layout.title);
  songsSubtitleLabel.setBounds(layout.subtitle);
  headerMenuButton.setBounds(layout.menu);
  addSongsButton.setBounds(layout.add);
  closeSongsButton.setBounds(layout.close);
  closeSongsButton.toFront(false);
  if (titleLabel.getText().isEmpty())
    titleLabel.setText(toCapitalCase(activeSetlistName),
                       juce::dontSendNotification);
  titleLabel.toFront(false);

  songsHeaderPanel.setVisible(!minimalPhoneMode);
  juce::FlexBox contentFlex;
  contentFlex.flexDirection = juce::FlexBox::Direction::column;
  if (!minimalPhoneMode)
    contentFlex.items.add(
        juce::FlexItem(songsHeaderPanel).withHeight(layout.headerPanel.getHeight()));
  contentFlex.items.add(juce::FlexItem(listViewport).withFlex(1.0f)
                           .withMargin({static_cast<float>(
                                            layout.listArea.getY() -
                                            layout.headerPanel.getBottom()),
                                        0, 0, 0}));
  contentFlex.performLayout(
      juce::Rectangle<float>(static_cast<float>(layout.workspace.getX()),
                             static_cast<float>(layout.headerPanel.getY()),
                             static_cast<float>(layout.workspace.getWidth()),
                             static_cast<float>(layout.workspace.getBottom() -
                                                layout.headerPanel.getY())));
}

void SongListPanel::updateSongsSubviewLayout(const SongsLayout &layout) {
  cachedSongsLayout = layout;
}

void SongListPanel::applySongsSubviewOffset(float offsetPx) {
  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  songsSubviewOffsetPx =
      juce::jlimit(-static_cast<float>(workspaceWidth), 0.0f, offsetPx);
  const int shiftX = juce::roundToInt(songsSubviewOffsetPx);

  backButton.setBounds(cachedSongsLayout.back.translated(shiftX, 0));
  headerMenuButton.setBounds(cachedSongsLayout.menu.translated(shiftX, 0));
  addSongsButton.setBounds(cachedSongsLayout.add.translated(shiftX, 0));
  titleLabel.setBounds(cachedSongsLayout.title.translated(shiftX, 0));
  songsSubtitleLabel.setBounds(
      cachedSongsLayout.subtitle.translated(shiftX, 0));
  songsHeaderPanel.setBounds(cachedSongsLayout.headerPanel.translated(shiftX, 0));
  listViewport.setBounds(cachedSongsLayout.listArea.translated(shiftX, 0));

  libraryCatalogPanel.setBounds(
      cachedSongsLayout.workspace.translated(workspaceWidth + shiftX, 0));
}

void SongListPanel::animateSongsSubviewToCurrent() {
  if (viewMode != ViewMode::Songs || cachedSongsLayout.workspace.isEmpty())
    return;

  auto &animator = juce::Desktop::getInstance().getAnimator();
  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  const float targetOffset =
      songsSubview == SongsSubview::Library ? -static_cast<float>(workspaceWidth)
                                            : 0.0f;
  if (std::abs(songsSubviewOffsetPx - targetOffset) < 0.6f) {
    applySongsSubviewOffset(targetOffset);
    applyModeVisibility();
    repaint();
    return;
  }

  auto animate = [&animator, this](juce::Component &component,
                                   juce::Rectangle<int> target) {
    component.setVisible(true);
    animator.animateComponent(&component, target, 1.0f, songsSwipeDurationMs,
                              false, 1.0, 1.0);
  };

  const int shiftX = juce::roundToInt(targetOffset);
  animate(backButton, cachedSongsLayout.back.translated(shiftX, 0));
  animate(headerMenuButton, cachedSongsLayout.menu.translated(shiftX, 0));
  animate(addSongsButton, cachedSongsLayout.add.translated(shiftX, 0));
  animate(titleLabel, cachedSongsLayout.title.translated(shiftX, 0));
  animate(songsHeaderPanel, cachedSongsLayout.headerPanel.translated(shiftX, 0));
  animate(listViewport, cachedSongsLayout.listArea.translated(shiftX, 0));
  animate(libraryCatalogPanel, cachedSongsLayout.workspace.translated(
                                  workspaceWidth + shiftX, 0));

  songsSubviewOffsetPx = targetOffset;
  songsSubviewTransitionInFlight = true;
  transitionInFlight = true;
  startTimer(songsSwipeDurationMs + 24);
  applyModeVisibility();
  repaint();
}

void SongListPanel::finishSongsSubviewAnimation() {
  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  const float targetOffset =
      songsSubview == SongsSubview::Library ? -static_cast<float>(workspaceWidth)
                                            : 0.0f;
  applySongsSubviewOffset(targetOffset);
}

void SongListPanel::beginSongsSwipeGesture(const juce::MouseEvent &event,
                                           const bool fromSongsToLibrary) {
  if (viewMode != ViewMode::Songs || cachedSongsLayout.workspace.isEmpty())
    return;
  if (fromSongsToLibrary && songsSubview != SongsSubview::SetlistSongs)
    return;
  if (!fromSongsToLibrary && songsSubview != SongsSubview::Library)
    return;

  juce::Desktop::getInstance().getAnimator().cancelAllAnimations(true);
  songsSwipeGestureActive = true;
  songsSubviewTransitionInFlight = false;
  transitionInFlight = false;
  stopTimer();
  songsSwipeStartOffsetPx = songsSubviewOffsetPx;
  songsSwipeStartPosition = event.position;
  applyModeVisibility();
}

void SongListPanel::updateSongsSwipeGesture(const juce::MouseEvent &event) {
  if (!songsSwipeGestureActive || viewMode != ViewMode::Songs)
    return;

  const float deltaX = event.position.x - songsSwipeStartPosition.x;
  applySongsSubviewOffset(songsSwipeStartOffsetPx + deltaX);
  repaint();
}

void SongListPanel::endSongsSwipeGesture(const juce::MouseEvent &event) {
  if (!songsSwipeGestureActive || viewMode != ViewMode::Songs)
    return;

  const int workspaceWidth = juce::jmax(1, cachedSongsLayout.workspace.getWidth());
  const float progress =
      juce::jlimit(0.0f, 1.0f, -songsSubviewOffsetPx / static_cast<float>(workspaceWidth));
  const float threshold = swipeThresholdRatio();

  const bool movedLeft = (event.position.x - songsSwipeStartPosition.x) < 0.0f;
  if (songsSubview == SongsSubview::SetlistSongs) {
    songsSubview = (progress >= threshold || (progress > 0.12f && movedLeft))
                       ? SongsSubview::Library
                       : SongsSubview::SetlistSongs;
  } else {
    songsSubview = (progress <= (1.0f - threshold) ||
                    (progress < 0.88f && !movedLeft))
                       ? SongsSubview::SetlistSongs
                       : SongsSubview::Library;
  }

  songsSwipeGestureActive = false;
  animateSongsSubviewToCurrent();
}

float SongListPanel::swipeThresholdRatio() { return 0.25f; }

void SongListPanel::applySetlistsLayout(const SetlistsLayout &layout) {
  setlistsHeaderLabel.setBounds(layout.headerTitle);
  setlistsSubtitleLabel.setBounds(layout.headerSubtitle);
  createSetlistButton.setBounds(layout.createRow);
  closeSetlistsButton.setBounds(layout.close);
  closeSetlistsButton.toFront(false);

  juce::FlexBox contentFlex;
  contentFlex.flexDirection = juce::FlexBox::Direction::column;
  contentFlex.items.add(
      juce::FlexItem(setlistSearchField).withHeight(layout.searchRow.getHeight()));
  contentFlex.items.add(juce::FlexItem(setlistsViewport).withFlex(1.0f)
                           .withMargin({10, 0, 0, 0}));
  contentFlex.performLayout(
      juce::Rectangle<float>(static_cast<float>(layout.searchRow.getX()),
                             static_cast<float>(layout.searchRow.getY()),
                             static_cast<float>(layout.searchRow.getWidth()),
                             static_cast<float>(layout.viewport.getBottom() -
                                                layout.searchRow.getY())));
}

void SongListPanel::openHeaderMenu() {
  juce::PopupMenu menu;
  const bool canSaveProject = saveProjectAvailableProvider != nullptr &&
                              saveProjectAvailableProvider();
  const bool projectPending = canSaveProject &&
                              saveProjectPendingProvider != nullptr &&
                              saveProjectPendingProvider();
  menu.addItem(projectPending
                   ? L10n::tr("Guardar proyecto (cambios pendientes)",
                              "Save project (pending changes)")
                   : L10n::tr("Guardar proyecto", "Save project"),
               canSaveProject, false, [this] {
                 if (onSaveProjectRequested)
                   onSaveProjectRequested();
               });
  menu.addSeparator();
  if (viewMode == ViewMode::Setlists) {
    menu.addItem(L10n::tr("Crear setlist", "Create setlist"),
                 [this] { createSetlist(); });
    menu.addSeparator();
    menu.addItem(L10n::tr(u8"Crear canción", "Create song"),
                 [this] { openSongImportWizard(); });
    menu.addItem(L10n::tr("Abrir proyecto...", "Open project..."),
                 [this] { openProjectFileChooser(); });
    menu.addItem(
        L10n::tr("Limpiar busqueda", "Clear search"),
        setlistSearchQuery.isNotEmpty(), false,
        [this] {
          setlistSearchField.clear();
          setlistSearchQuery.clear();
          refreshFromStorage();
        });
  } else {
    menu.addItem(L10n::tr("Biblioteca", "Library"),
                 [this] { openLibraryBrowser(); });
    menu.addSeparator();
    menu.addItem(L10n::tr("Setlists...", "Setlists..."),
                 [this] { showSetlistsView(); });
    menu.addSeparator();
    menu.addItem(L10n::tr("Eliminar setlist actual", "Delete current setlist"),
                 [this] { deleteActiveSetlist(); });
  }
  menu.showMenuAsync(makeTouchPopupMenuOptions(&headerMenuButton));
}

void SongListPanel::setOnSaveProjectRequested(std::function<void()> cb) {
  onSaveProjectRequested = std::move(cb);
}

void SongListPanel::setOnPadsRequested(std::function<void()> cb) {
  onPadsRequested = std::move(cb);
}

void SongListPanel::setSaveProjectStateProvider(
    std::function<bool()> availableProvider,
    std::function<bool()> pendingProvider) {
  saveProjectAvailableProvider = std::move(availableProvider);
  saveProjectPendingProvider = std::move(pendingProvider);
}

void SongListPanel::openLibraryBrowser() {
  libraryTargetSetlistId.clear();
  libraryCatalogPanel.setTargetSetlistId({});
  libraryCatalogPanel.setHeaderMode(LibraryCatalogPanel::HeaderMode::Library);
  showLibraryView(true);
}

void SongListPanel::openAddFromLibraryMenu() {
  libraryTargetSetlistId = activeSetlistId;
  libraryCatalogPanel.setTargetSetlistId(libraryTargetSetlistId);
  libraryCatalogPanel.setHeaderMode(
      LibraryCatalogPanel::HeaderMode::SetlistTarget);
  showLibraryView(true);
}

void SongListPanel::openSetlistOptionsMenu(const juce::String &setlistId,
                                           juce::Rectangle<int> targetArea) {
  if (setlistId.isEmpty())
    return;

  juce::PopupMenu menu;
  menu.addItem(L10n::tr("Abrir setlist", "Open setlist"),
               [this, setlistId] { showSongsView(setlistId); });
  menu.addItem(L10n::tr("Renombrar", "Rename"),
               [this, setlistId] { renameSetlistById(setlistId); });
  menu.addSeparator();
  menu.addItem(L10n::tr("Eliminar", "Delete"),
               [this, setlistId] { deleteSetlistById(setlistId); });
  if (auto *viewed = setlistsViewport.getViewedComponent()) {
    if (targetArea.isEmpty())
      targetArea = viewed->getLocalBounds().withSizeKeepingCentre(10, 10);
    const auto screenArea = viewed->localAreaToGlobal(targetArea);
    menu.showMenuAsync(
        makeTouchPopupMenuOptions(nullptr)
            .withTargetScreenArea(screenArea)
            .withParentComponent(this));
    return;
  }
  menu.showMenuAsync(makeTouchPopupMenuOptions(this));
}

void SongListPanel::createSetlist() {
  promptTextAsync(
      L10n::tr("Crear Setlist", "Create setlist"),
      L10n::tr("Nombre del setlist:", "Setlist name:"), "",
      [this](juce::String name) {
        name = name.trim();
        if (name.isEmpty())
          return;
        if (remoteLibrary) { if (onRemoteAction) onRemoteAction("setlist.create", name, {}); return; }
        const auto newId = SongLibraryStore::createSetlist(name);
        if (newId.isNotEmpty())
          SongLibraryStore::setActiveSetlistId(newId);
        refreshFromStorage();
      });
}

void SongListPanel::renameSetlistById(const juce::String &setlistId) {
  if (setlistId.isEmpty())
    return;
  auto library = (remoteLibrary ? *remoteLibrary : SongLibraryStore::loadLibrarySnapshot());
  auto it = std::find_if(library.setlists.begin(), library.setlists.end(),
                         [&setlistId](const SetlistData &s) {
                           return s.id == setlistId;
                         });
  if (it == library.setlists.end())
    return;

  promptTextAsync(
      L10n::tr("Renombrar Setlist", "Rename setlist"),
      L10n::tr("Nombre del setlist:", "Setlist name:"), it->name,
      [this, setlistId](juce::String name) {
        name = name.trim();
        if (name.isEmpty())
          return;
        if (remoteLibrary) { if (onRemoteAction) onRemoteAction("setlist.rename", name, setlistId); return; }
        SongLibraryStore::renameSetlist(setlistId, name);
        refreshFromStorage();
      });
}

void SongListPanel::deleteSetlistById(const juce::String &setlistId) {
  if (setlistId.isEmpty())
    return;

  const auto library = (remoteLibrary ? *remoteLibrary : SongLibraryStore::loadLibrarySnapshot());
  const auto safe = juce::Component::SafePointer<SongListPanel>(this);
  if (library.setlists.size() <= 1) {
    appModal = std::make_unique<AppModal>(
        *this, AppModal::Kind::Information,
        L10n::tr("No se puede eliminar", "Can't delete"),
        L10n::tr("Se requiere al menos un setlist.",
                 "At least one setlist is required."),
        L10n::tr("Entendido", "OK"), juce::String(), nullptr);
    appModal->onClosed = [safe] { if (safe != nullptr) safe->appModal.reset(); };
    appModal->show();
    return;
  }

  appModal = std::make_unique<AppModal>(
      *this, AppModal::Kind::Destructive,
      L10n::tr("Eliminar setlist", "Delete setlist"),
      L10n::tr(u8"Eliminar este setlist? Esto no quitará canciones de la biblioteca.",
               "Delete this setlist? This won't remove songs from the library."),
      L10n::tr("Eliminar", "Delete"), L10n::tr("Cancelar", "Cancel"),
      [safe, setlistId](bool accepted, const juce::String &) {
        if (!accepted || safe == nullptr)
          return;
        if (safe->remoteLibrary) { if (safe->onRemoteAction) safe->onRemoteAction("setlist.delete", {}, setlistId); return; }
        SongLibraryStore::deleteSetlist(setlistId);
        safe->refreshFromStorage();
      });
  appModal->onClosed = [safe] { if (safe != nullptr) safe->appModal.reset(); };
  appModal->show();
}

void SongListPanel::promptTextAsync(const juce::String &title,
                                    const juce::String &message,
                                    const juce::String &initial,
                                    std::function<void(juce::String)> onComplete) {
  const auto safe = juce::Component::SafePointer<SongListPanel>(this);
  appModal = std::make_unique<AppModal>(
      *this, AppModal::Kind::Input, title,
      L10n::tr("Escribe un nombre para identificar este setlist.",
               "Enter a name to identify this setlist."),
      initial.isEmpty() ? L10n::tr("Crear", "Create")
                        : L10n::tr("Guardar", "Save"),
      L10n::tr("Cancelar", "Cancel"),
      [safe, callback = std::move(onComplete)](bool accepted,
                                                const juce::String &value) {
        if (accepted && safe != nullptr && callback) callback(value);
      }, message, initial);
  appModal->onClosed = [safe] { if (safe != nullptr) safe->appModal.reset(); };
  appModal->show();
}

void SongListPanel::renameActiveSetlist() {
  renameSetlistById(activeSetlistId);
}

void SongListPanel::deleteActiveSetlist() {
  deleteSetlistById(activeSetlistId);
}

juce::String SongListPanel::formatDurationHMS(const double totalSeconds) {
  const int seconds =
      juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
  const int hh = seconds / 3600;
  const int mm = (seconds % 3600) / 60;
  const int ss = seconds % 60;
  return hh > 0 ? juce::String::formatted("%02d:%02d:%02d", hh, mm, ss)
                : juce::String::formatted("%02d:%02d", mm, ss);
}

void SongListPanel::refreshSongsHeaderSummary() {
  double total = 0.0;
  int bpmCount = 0;
  int bpmAccum = 0;
  for (const int idx : activeSongIndices) {
    if (!juce::isPositiveAndBelow(idx, static_cast<int>(librarySongs.size())))
      continue;
    const auto &song = librarySongs[static_cast<size_t>(idx)];
    total += estimateSongLengthSeconds(song);
    if (song.bpm > 0) {
      bpmAccum += song.bpm;
      ++bpmCount;
    }
  }

  SetlistSongsHeaderPanel::Summary summary;
  summary.duration = formatDurationHMS(total);
  summary.songsCount = static_cast<int>(activeSongIndices.size());
  summary.averageBpm =
      bpmCount > 0 ? juce::roundToInt(static_cast<double>(bpmAccum) /
                                      static_cast<double>(bpmCount))
                   : 0;
  songsHeaderPanel.setSummary(summary);
  songsSubtitleLabel.setText(
      juce::String(summary.songsCount) + " " +
          (summary.songsCount == 1 ? L10n::tr(u8"canción", "song")
                                   : L10n::tr("canciones", "songs")) +
          u8"  •  " + summary.duration,
      juce::dontSendNotification);
}

void SongListPanel::ListContent::setSongs(std::vector<SongRow> rows) {
  const bool wasDragging = isDragging;
  songs = std::move(rows);
  artworkCache.clear();
  dragCandidate = false;
  isDragging = false;
  dragSourceIndex = -1;
  dragInsertionIndex = -1;
  pendingActionsMenuRow = -1;
  hoveredRow = -1;
  revealedActionsRow = -1;
  swipeCandidateRow = -1;
  swipeOffset = 0.0f;
  swipeDragging = false;
  if (wasDragging && onDragStateChanged)
    onDragStateChanged(false);
  repaint();
}

void SongListPanel::ListContent::setNowPlayingSong(const juce::String &songId,
                                                   bool isPlaying) {
  bool changed = false;
  for (auto &row : songs) {
    const bool next = isPlaying && row.songId == songId;
    if (row.isNowPlaying == next)
      continue;
    row.isNowPlaying = next;
    changed = true;
  }
  if (changed)
    repaint();
}

void SongListPanel::ListContent::setSelectedSongId(const juce::String &songId) {
  bool changed = false;
  for (auto &row : songs) {
    const bool next = songId.isNotEmpty() && row.songId == songId;
    if (row.isSelected == next)
      continue;
    row.isSelected = next;
    changed = true;
  }
  if (changed)
    repaint();
}

void SongListPanel::ListContent::setCloudUploadProgress(
    const juce::String &songId, const double fraction,
    const juce::String &phase) {
  for (int index = 0; index < static_cast<int>(songs.size()); ++index) {
    auto &row = songs[static_cast<size_t>(index)];
    if (row.songId != songId)
      continue;
    row.cloudUploadFraction = fraction < 0.0 ? -1.0 : juce::jlimit(0.0, 1.0, fraction);
    row.cloudUploadPhase = phase;
    repaint(juce::Rectangle<int>(rowSideInset, rowTopAtIndex(index),
                                 getWidth() - (rowSideInset * 2), rowHeight));
    break;
  }
}

void SongListPanel::ListContent::setOnSongActivated(
    std::function<void(int)> cb) {
  onSongActivated = std::move(cb);
}

void SongListPanel::ListContent::setOnSongsReordered(
    std::function<void(int, int)> cb) {
  onSongsReordered = std::move(cb);
}

void SongListPanel::ListContent::setOnDragStateChanged(
    std::function<void(bool)> cb) {
  onDragStateChanged = std::move(cb);
}

void SongListPanel::ListContent::setOnAutoPlayNextToggled(
    std::function<void(int)> cb) {
  onAutoPlayNextToggled = std::move(cb);
}

void SongListPanel::ListContent::setOnSongRemoveRequested(
    std::function<void(int)> cb) {
  onSongRemoveRequested = std::move(cb);
}

void SongListPanel::ListContent::setOnAddSongsRequested(
    std::function<void()> cb) {
  onAddSongsRequested = std::move(cb);
}

void SongListPanel::ListContent::setOnOptimizeRequested(
    std::function<void(int)> cb) {
  onOptimizeRequested = std::move(cb);
}

int SongListPanel::ListContent::getPreferredHeight() const {
  const int songCount = static_cast<int>(songs.size());
  const int rowsHeight = rowStart + songCount * (rowHeight + rowGap);
  return juce::jmax(88, rowsHeight);
}

void SongListPanel::ListContent::paint(juce::Graphics &g) {
  const bool hasActiveDrag =
      isDragging && juce::isPositiveAndBelow(dragSourceIndex,
                                             static_cast<int>(songs.size()));

  auto paintSongRow = [this, &g](const SongRow &rowData,
                                 juce::Rectangle<int> row, const int rowIndex,
                                 juce::Colour fill, float fillAlpha,
                                 float borderAlpha) {
    auto formatDurationMMSS = [](double totalSeconds) {
      const int seconds =
          juce::jmax(0, juce::roundToInt(std::floor(totalSeconds + 0.5)));
      const int mm = seconds / 60;
      const int ss = seconds % 60;
      return juce::String::formatted("%02d:%02d", mm, ss);
    };

    const auto playingStateColour = UITheme::accent;
    const auto nextStateColour = juce::Colour::fromRGB(250, 204, 21);
    const bool showPlayingState = rowData.isNowPlaying;
    const bool showNextState = rowData.autoPlayNext && !showPlayingState;
    const bool showSelectedState =
        rowData.isSelected && !showPlayingState && !showNextState;

    auto rowFill = fill;
    if (rowFill == UITheme::cardBgHover)
      rowFill = UITheme::cardBg;
    if (rowIndex == hoveredRow)
      rowFill = rowFill.interpolatedWith(UITheme::cardBgHover, 0.28f);

    juce::Path rowPath;
    rowPath.addRectangle(row.toFloat());
    if (rowIndex == hoveredRow || showSelectedState || showPlayingState) {
      g.setColour(rowFill.withAlpha(fillAlpha));
      g.fillRect(row);
    }

    const auto stateBarColour =
        showPlayingState
            ? playingStateColour.withAlpha(0.96f)
            : showNextState
                  ? nextStateColour.withAlpha(0.95f)
                  : rowData.accentColour.withAlpha(showSelectedState ? 0.86f
                                                                     : 0.66f);
    const auto stateBar = juce::Rectangle<float>(
        static_cast<float>(row.getX()), static_cast<float>(row.getY()), 4.0f,
        static_cast<float>(row.getHeight()));
    g.saveState();
    g.reduceClipRegion(rowPath);
    g.setColour(stateBarColour);
    g.fillRect(stateBar);
    g.restoreState();

    const auto borderColour = showPlayingState
                                  ? playingStateColour.withAlpha(
                                        juce::jlimit(0.0f, 1.0f,
                                                     borderAlpha + 0.25f))
                                  : showSelectedState
                                              ? UITheme::accent.withAlpha(
                                                    juce::jlimit(0.0f, 1.0f,
                                                                 borderAlpha +
                                                                     0.18f))
                                              : UITheme::cardBorder.withAlpha(
                                                    borderAlpha * 0.42f);
    juce::ignoreUnused(borderColour, borderAlpha);

    auto c = row.reduced(14, 7);
    c.removeFromLeft(6);
    auto optionsHitArea = c.removeFromRight(34);
    c.removeFromRight(2);

    constexpr int artworkSize = 42;
    auto artworkArea = juce::Rectangle<int>(
        c.getX(),
        c.getY() + juce::jmax(0, (c.getHeight() - artworkSize) / 2), artworkSize,
        artworkSize);

    juce::Image artworkImage;
    const auto artworkKey = rowData.artworkPath.trim().toStdString();
    if (!artworkKey.empty()) {
      auto it = artworkCache.find(artworkKey);
      if (it == artworkCache.end()) {
        const juce::File artworkFile(rowData.artworkPath.trim());
        juce::Image loaded;
        if (artworkFile.existsAsFile())
          loaded = juce::ImageFileFormat::loadFrom(artworkFile);
        it = artworkCache.emplace(artworkKey, std::move(loaded)).first;
      }
      artworkImage = it->second;
    }

    g.setColour(UITheme::cardBgHover.withAlpha(0.9f * fillAlpha));
    g.fillRoundedRectangle(artworkArea.toFloat(), 7.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.72f * fillAlpha));
    g.drawRoundedRectangle(artworkArea.toFloat().reduced(0.5f), 7.0f, 1.0f);

    if (artworkImage.isValid()) {
      g.drawImageWithin(artworkImage, artworkArea.getX(), artworkArea.getY(),
                        artworkArea.getWidth(), artworkArea.getHeight(),
                        juce::RectanglePlacement::centred |
                            juce::RectanglePlacement::onlyReduceInSize);
    } else {
      juce::String placeholder = rowData.title.trim();
      placeholder = placeholder.isNotEmpty()
                        ? placeholder.substring(0, 1).toUpperCase()
                        : "?";
      g.setColour(UITheme::textSecondary.withAlpha(0.88f * fillAlpha));
      g.setFont(makeAppFont(15.0f, AppFontWeight::SemiBold));
      g.drawFittedText(placeholder, artworkArea, juce::Justification::centred, 1);
    }

    c.removeFromLeft(artworkSize + 11);

    if (showPlayingState) {
      const auto stateArea =
          juce::Rectangle<int>(c.getX(), c.getY(), 16, c.getHeight());
      drawIconGlyph(g, stateArea.toFloat().withSizeKeepingCentre(14.0f, 14.0f),
                    IconGlyph::Play, playingStateColour.withAlpha(0.95f), 1.35f);
      c.removeFromLeft(20);
    } else if (showNextState) {
      const auto stateArea =
          juce::Rectangle<int>(c.getX(), c.getY(), 16, c.getHeight());
      drawIconGlyph(g, stateArea.toFloat().withSizeKeepingCentre(14.0f, 14.0f),
                    IconGlyph::Next, nextStateColour.withAlpha(0.95f), 1.30f);
      c.removeFromLeft(20);
    }
    constexpr int titleHeight = 17;
    constexpr int artistHeight = 13;
    constexpr int metaHeight = 12;
    constexpr int lineGap = 2;
    const int totalTextHeight =
        titleHeight + artistHeight + metaHeight + (lineGap * 2);
    const int textStartY =
        c.getY() + juce::jmax(0, (c.getHeight() - totalTextHeight) / 2);

    auto titleRect =
        juce::Rectangle<int>(c.getX(), textStartY, c.getWidth(), titleHeight);
    auto artistRect = juce::Rectangle<int>(c.getX(), titleRect.getBottom() + lineGap,
                                           c.getWidth(), artistHeight);
    auto metaRect = juce::Rectangle<int>(c.getX(), artistRect.getBottom() + lineGap,
                                         c.getWidth(), metaHeight);

    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(15.0f, AppFontWeight::SemiBold));
    g.drawFittedText(rowData.title, titleRect, juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textSecondary.withAlpha(0.95f));
    g.setFont(makeAppFont(10.8f, AppFontWeight::Medium));
    g.drawFittedText(
        rowData.artist.isNotEmpty()
            ? rowData.artist
            : L10n::tr("Sin artista", "Unknown artist").toUpperCase(),
        artistRect, juce::Justification::centredLeft, 1);
    g.setColour(UITheme::textSecondary.withAlpha(0.76f));
    g.setFont(makeAppFont(10.8f, AppFontWeight::Regular));
    const auto detail = (rowData.key.isNotEmpty() ? rowData.key : "-") + u8" • " +
                        (rowData.bpm > 0 ? juce::String(rowData.bpm) : "--") +
                        " BPM" + u8" • " + juce::String(rowData.stemCount) + " stems" +
                        u8" • " + formatDurationMMSS(rowData.durationSeconds);
    g.drawFittedText(detail, metaRect, juce::Justification::centredLeft, 1);

    const bool uploadInProgress = rowData.cloudUploadFraction >= 0.0;
    const auto progressBounds = juce::Rectangle<float>(
        static_cast<float>(artworkArea.getRight() + 11),
        static_cast<float>(row.getBottom() - 4),
        static_cast<float>(juce::jmax(0, row.getRight() - artworkArea.getRight() - 22)),
        3.0f);
    if (uploadInProgress && progressBounds.getWidth() > 0.0f) {
      g.setColour(UITheme::cardBorder.withAlpha(0.75f));
      g.fillRoundedRectangle(progressBounds, 1.5f);
      const auto completedWidth = static_cast<float>(
          progressBounds.getWidth() * rowData.cloudUploadFraction);
      if (completedWidth > 0.0f) {
        g.setColour(UITheme::accent.withAlpha(0.98f));
        g.fillRoundedRectangle(progressBounds.withWidth(completedWidth), 1.5f);
      }
    }

    const auto menuAlpha = rowIndex == hoveredRow ? 0.88f : 0.64f;
    drawIconGlyph(g, optionsHitArea.toFloat().withSizeKeepingCentre(18.0f, 18.0f),
                  IconGlyph::More, UITheme::textSecondary.withAlpha(menuAlpha), 1.35f);
    if (!uploadInProgress && rowIndex + 1 < static_cast<int>(songs.size())) {
      g.setColour(AppColours::separator);
      g.drawLine(static_cast<float>(artworkArea.getRight() + 11),
                 static_cast<float>(row.getBottom() - 1),
                 static_cast<float>(row.getRight()),
                 static_cast<float>(row.getBottom() - 1), 1.0f);
    }
  };

  for (size_t i = 0; i < songs.size(); ++i) {
    auto row = juce::Rectangle<int>(
        rowSideInset, rowStart + static_cast<int>(i) * (rowHeight + rowGap),
        getWidth() - (rowSideInset * 2), rowHeight);
    const bool actionsRevealed = static_cast<int>(i) == revealedActionsRow &&
                                 swipeOffset > 0.0f;
    if (actionsRevealed) {
      const int actionWidth = 72;
      auto tray = row.removeFromRight(actionWidth * 2);
      auto nextBounds = tray.removeFromLeft(actionWidth);
      g.setColour(juce::Colour::fromRGB(55, 65, 81));
      g.fillRect(nextBounds);
      drawSvgIcon(g, nextBounds.toFloat().withSizeKeepingCentre(17.0f, 17.0f)
                         .translated(0.0f, -8.0f),
                  SvgIconName::SkipForward, UITheme::textPrimary);
      g.setColour(juce::Colour::fromRGB(153, 27, 27));
      g.fillRect(tray);
      drawSvgIcon(g, tray.toFloat().withSizeKeepingCentre(17.0f, 17.0f)
                         .translated(0.0f, -8.0f),
                  SvgIconName::Trash, UITheme::textPrimary);
      g.setFont(makeAppFont(9.3f, AppFontWeight::Medium));
      g.setColour(UITheme::textPrimary.withAlpha(0.92f));
      g.drawFittedText(L10n::tr("Siguiente", "Next"),
                       nextBounds.withTrimmedTop(36),
                       juce::Justification::centred, 1);
      g.drawFittedText(L10n::tr("Quitar", "Remove"),
                       tray.withTrimmedTop(36),
                       juce::Justification::centred, 1);
      row = juce::Rectangle<int>(rowSideInset, rowTopAtIndex(static_cast<int>(i)),
                                 getWidth() - (rowSideInset * 2), rowHeight)
                .translated(-juce::roundToInt(swipeOffset), 0);
    }
    if (hasActiveDrag && static_cast<int>(i) == dragSourceIndex) {
      paintSongRow(songs[i], row, static_cast<int>(i), UITheme::cardBgHover, 0.24f,
                   0.35f);
      continue;
    }
    paintSongRow(songs[i], row, static_cast<int>(i), UITheme::cardBgHover, 1.0f,
                 0.58f);
  }

  if (songs.empty()) {
    const auto emptyBounds = getLocalBounds().reduced(10);
    const auto iconBounds = juce::Rectangle<float>(
        static_cast<float>(emptyBounds.getCentreX() - 12),
        static_cast<float>(emptyBounds.getCentreY() - 29), 24.0f, 24.0f);
    drawSvgIcon(g, iconBounds, SvgIconName::Music,
                UITheme::textSecondary.withAlpha(0.56f));
    g.setColour(UITheme::textSecondary.withAlpha(0.88f));
    g.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
    g.drawFittedText(L10n::tr(u8"Aún no hay canciones en este setlist.",
                              "No songs in this setlist yet."),
                     emptyBounds.withTrimmedTop(22),
                     juce::Justification::centred, 2);
  }

  if (hasActiveDrag) {
    const int dragTop = juce::roundToInt(dragMouseY - dragRowOffsetY);
    const int clampedTop =
        juce::jlimit(rowStart,
                     juce::jmax(rowStart, getHeight() - rowHeight - rowStart),
                     dragTop);
    const auto dragRow =
        juce::Rectangle<int>(rowSideInset, clampedTop,
                             getWidth() - (rowSideInset * 2), rowHeight);
    paintSongRow(songs[static_cast<size_t>(dragSourceIndex)], dragRow,
                 dragSourceIndex, UITheme::cardBgPressed, 0.95f, 0.86f);

    const int lineY = insertionLineY(dragInsertionIndex);
    const auto lineBounds =
        juce::Rectangle<float>(static_cast<float>(rowSideInset),
                               static_cast<float>(lineY - 1),
                               static_cast<float>(getWidth() - (rowSideInset * 2)),
                               2.0f);
    g.setColour(UITheme::accent.withAlpha(0.95f));
    g.fillRoundedRectangle(lineBounds, 1.0f);
  }
}

void SongListPanel::ListContent::mouseDown(const juce::MouseEvent &event) {
  dragCandidate = false;
  isDragging = false;
  dragSourceIndex = -1;
  dragInsertionIndex = -1;
  pendingActionsMenuRow = -1;
  dragRowOffsetY = 0.0f;
  dragMouseY = event.position.y;
  swipeDragging = false;
  gestureDirectionDecided = false;
  swipeCandidateRow = -1;

  const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
  if (!juce::isPositiveAndBelow(idx, static_cast<int>(songs.size())))
    return;

  if (revealedActionsRow >= 0 && idx != revealedActionsRow) {
    revealedActionsRow = -1;
    swipeOffset = 0.0f;
    repaint();
  }

  auto row =
      juce::Rectangle<int>(rowSideInset, rowTopAtIndex(idx),
                           getWidth() - (rowSideInset * 2), rowHeight);
  if (optionsButtonBoundsForRow(row).contains(event.position.toInt())) {
    pendingActionsMenuRow = idx;
    return;
  }

  dragCandidate = true;
  swipeCandidateRow = idx;
  swipeStartOffset = idx == revealedActionsRow ? swipeOffset : 0.0f;
  dragSourceIndex = idx;
  dragInsertionIndex = idx;
  mouseDownPosition = event.position;
  dragRowOffsetY = event.position.y - static_cast<float>(rowTopAtIndex(idx));
}

void SongListPanel::ListContent::mouseDrag(const juce::MouseEvent &event) {
  if (pendingActionsMenuRow >= 0)
    return;

  if (!dragCandidate || !juce::isPositiveAndBelow(dragSourceIndex,
                                                  static_cast<int>(songs.size())))
    return;

  const auto delta = event.position - mouseDownPosition;
  if (!gestureDirectionDecided && delta.getDistanceFromOrigin() >= 7.0f) {
    gestureDirectionDecided = true;
    swipeDragging = std::abs(delta.x) > std::abs(delta.y);
  }
  if (swipeDragging) {
    revealedActionsRow = swipeCandidateRow;
    swipeOffset = juce::jlimit(0.0f, 144.0f, swipeStartOffset - delta.x);
    repaint();
    return;
  }
  if (!gestureDirectionDecided)
    return;

  if (!isDragging) {
    const bool isTouchInput = event.source.isTouch();
    const float minDragDistancePx = isTouchInput ? 9.0f : 4.0f;
    const float dragDistance =
        event.position.getDistanceFrom(mouseDownPosition);
    const bool enoughDistance = dragDistance >= minDragDistancePx;

    // Keep touch drag responsive while still filtering tiny scroll jitter.
    if (!enoughDistance)
      return;
  }

  const bool wasDragging = isDragging;
  isDragging = true;
  if (!wasDragging && onDragStateChanged)
    onDragStateChanged(true);
  dragMouseY = event.position.y;
  dragInsertionIndex = insertionIndexAtY(juce::roundToInt(event.position.y));
  repaint();
}

void SongListPanel::ListContent::mouseUp(const juce::MouseEvent &event) {
  if (event.mods.isPopupMenu())
    return;

  if (swipeDragging) {
    swipeOffset = swipeOffset >= 54.0f ? 144.0f : 0.0f;
    if (swipeOffset == 0.0f)
      revealedActionsRow = -1;
    swipeDragging = false;
    dragCandidate = false;
    repaint();
    return;
  }

  if (revealedActionsRow >= 0 && swipeOffset > 0.0f) {
    auto row = juce::Rectangle<int>(rowSideInset,
        rowTopAtIndex(revealedActionsRow), getWidth() - rowSideInset * 2, rowHeight);
    const auto tray = row.removeFromRight(144);
    if (tray.contains(event.position.toInt())) {
      const auto songIndex = songs[static_cast<size_t>(revealedActionsRow)].songIndex;
      if (event.position.x < static_cast<float>(tray.getX() + 72)) {
        if (revealedActionsRow + 1 < static_cast<int>(songs.size()) && onAutoPlayNextToggled)
          onAutoPlayNextToggled(songIndex);
      } else if (onSongRemoveRequested) {
        onSongRemoveRequested(songIndex);
      }
      revealedActionsRow = -1;
      swipeOffset = 0.0f;
      repaint();
      return;
    }
  }

  if (pendingActionsMenuRow >= 0) {
    const int row = pendingActionsMenuRow;
    pendingActionsMenuRow = -1;
    if (juce::isPositiveAndBelow(row, static_cast<int>(songs.size()))) {
      const auto rowBounds = juce::Rectangle<int>(
          rowSideInset, rowTopAtIndex(row), getWidth() - (rowSideInset * 2),
          rowHeight);
      if (optionsButtonBoundsForRow(rowBounds).contains(event.position.toInt()))
        showActionsMenuForRow(row);
    }
    return;
  }

  if (!dragCandidate) {
    return;
  }

  if (isDragging && juce::isPositiveAndBelow(dragSourceIndex,
                                             static_cast<int>(songs.size()))) {
    int insertion = dragInsertionIndex;
    if (!juce::isPositiveAndBelow(insertion, static_cast<int>(songs.size()) + 1))
      insertion = insertionIndexAtY(juce::roundToInt(dragMouseY));

    int toIndex = insertion;
    if (insertion > dragSourceIndex)
      --toIndex;
    toIndex = juce::jlimit(0, static_cast<int>(songs.size()) - 1, toIndex);

    if (onSongsReordered && dragSourceIndex != toIndex)
      onSongsReordered(dragSourceIndex, toIndex);
  } else if (juce::isPositiveAndBelow(dragSourceIndex,
                                      static_cast<int>(songs.size()))) {
    const int releaseRow = rowIndexAtY(juce::roundToInt(event.position.y));
    if (releaseRow == dragSourceIndex) {
      for (int i = 0; i < static_cast<int>(songs.size()); ++i)
        songs[static_cast<size_t>(i)].isSelected = (i == dragSourceIndex);
      if (onSongActivated)
        onSongActivated(songs[static_cast<size_t>(dragSourceIndex)].songIndex);
    }
  }

  dragCandidate = false;
  isDragging = false;
  dragSourceIndex = -1;
  dragInsertionIndex = -1;
  dragRowOffsetY = 0.0f;
  dragMouseY = 0.0f;
  if (onDragStateChanged)
    onDragStateChanged(false);
  repaint();
}

void SongListPanel::ListContent::mouseDoubleClick(
    const juce::MouseEvent &event) {
  juce::ignoreUnused(event);
}

void SongListPanel::ListContent::mouseMove(const juce::MouseEvent &event) {
  const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
  if (hoveredRow == idx)
    return;
  const int previousHovered = hoveredRow;
  hoveredRow = idx;
  if (juce::isPositiveAndBelow(previousHovered, static_cast<int>(songs.size())))
    repaint(juce::Rectangle<int>(rowSideInset, rowTopAtIndex(previousHovered),
                                 getWidth() - (rowSideInset * 2), rowHeight)
                .expanded(3));
  if (juce::isPositiveAndBelow(hoveredRow, static_cast<int>(songs.size())))
    repaint(juce::Rectangle<int>(rowSideInset, rowTopAtIndex(hoveredRow),
                                 getWidth() - (rowSideInset * 2), rowHeight)
                .expanded(3));
}

void SongListPanel::ListContent::mouseExit(const juce::MouseEvent &) {
  if (hoveredRow < 0)
    return;
  const int previousHovered = hoveredRow;
  hoveredRow = -1;
  if (juce::isPositiveAndBelow(previousHovered, static_cast<int>(songs.size())))
    repaint(juce::Rectangle<int>(rowSideInset, rowTopAtIndex(previousHovered),
                                 getWidth() - (rowSideInset * 2), rowHeight)
                .expanded(3));
}

int SongListPanel::ListContent::rowIndexAtY(int y) const {
  if (y < rowStart)
    return -1;

  const int relY = y - rowStart;
  const int step = rowHeight + rowGap;
  const int row = relY / step;
  if (!juce::isPositiveAndBelow(row, static_cast<int>(songs.size())))
    return -1;

  if ((relY % step) >= rowHeight)
    return -1;

  return row;
}

int SongListPanel::ListContent::insertionIndexAtY(int y) const {
  const int step = rowHeight + rowGap;
  const int count = static_cast<int>(songs.size());

  if (count <= 0)
    return 0;

  for (int i = 0; i < count; ++i) {
    const int rowCenterY = rowStart + i * step + (rowHeight / 2);
    if (y < rowCenterY)
      return i;
  }

  return count;
}

int SongListPanel::ListContent::rowTopAtIndex(int index) const {
  const int step = rowHeight + rowGap;
  return rowStart + index * step;
}

int SongListPanel::ListContent::insertionLineY(int insertionIndex) const {
  const int step = rowHeight + rowGap;
  const int count = static_cast<int>(songs.size());
  const int idx = juce::jlimit(0, count, insertionIndex);

  if (idx == 0)
    return rowStart;
  if (idx >= count)
    return rowStart + (count - 1) * step + rowHeight;
  return rowStart + idx * step - (rowGap / 2);
}

juce::Rectangle<int> SongListPanel::ListContent::optionsButtonBoundsForRow(
    const juce::Rectangle<int> &rowBounds) const {
  auto c = rowBounds.reduced(16, 8);
  auto optionsRect = c.removeFromRight(34);
  return optionsRect;
}

void SongListPanel::ListContent::showActionsMenuForRow(int rowIndex) {
  if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(songs.size())))
    return;

  class ActionChip final : public juce::Button {
  public:
    ActionChip(juce::String labelText, SvgIconName iconToUse, bool emphasized,
               bool destructive)
        : juce::Button(labelText), label(std::move(labelText)),
          icon(iconToUse), isEmphasized(emphasized), isDestructive(destructive) {}

    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override {
      auto b = getLocalBounds().toFloat();
      auto fill = UITheme::cardBgHover;
      if (isEmphasized)
        fill = fill.interpolatedWith(UITheme::accentSoft, 0.22f);
      if (isMouseOverButton)
        fill = fill.brighter(0.06f);
      if (isButtonDown)
        fill = fill.darker(0.05f);

      g.setColour(fill);
      g.fillRoundedRectangle(b, 12.0f);
      juce::ColourGradient highlight(UITheme::textPrimary.withAlpha(0.055f),
                                     b.getCentreX(), b.getY(),
                                     juce::Colours::transparentBlack,
                                     b.getCentreX(), b.getBottom(), false);
      g.setGradientFill(highlight);
      g.fillRoundedRectangle(b, 12.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.65f));
      g.drawRoundedRectangle(b, 12.0f, 1.0f);

      const auto textColour = UITheme::textPrimary;
      const auto iconBounds = b.reduced(12.0f, 0.0f).removeFromLeft(18.0f)
                                .withSizeKeepingCentre(15.0f, 15.0f);
      drawSvgIcon(g, iconBounds, icon, textColour);
      g.setColour(textColour);
      g.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
      g.drawFittedText(label, getLocalBounds().reduced(36, 0),
                       juce::Justification::centredLeft, 1);
    }

  private:
    juce::String label;
    SvgIconName icon;
    bool isEmphasized = false;
    bool isDestructive = false;
  };

  class SongActionsPopover final : public juce::Component {
  public:
    SongActionsPopover(bool autoPlayEnabled, bool hasNextSong,
                       std::function<void()> onToggleFn,
                       std::function<void()> onRemoveFn)
        : toggleChip(!hasNextSong
                         ? L10n::tr("No hay canción siguiente",
                                    "There is no next song")
                         : (autoPlayEnabled
                                ? L10n::tr(
                                      "No reproducir siguiente al terminar",
                                      "Don't play next when finished")
                                : L10n::tr("Reproducir siguiente al terminar",
                                           "Play next when finished")),
                     SvgIconName::SkipForward, autoPlayEnabled, false),
          removeChip(L10n::tr("Quitar del setlist", "Remove from setlist"),
                     SvgIconName::Trash, false, true),
          onToggle(std::move(onToggleFn)), onRemove(std::move(onRemoveFn)) {
      toggleChip.setEnabled(hasNextSong);
      toggleChip.onClick = [this] {
        if (onToggle)
          onToggle();
        closeSelf();
      };
      removeChip.onClick = [this] {
        if (onRemove)
          onRemove();
        closeSelf();
      };
      addAndMakeVisible(toggleChip);
      addAndMakeVisible(removeChip);
    }

    void paint(juce::Graphics &g) override {
      auto b = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(b, 8.0f);
      juce::ColourGradient highlight(UITheme::textPrimary.withAlpha(0.035f),
                                     b.getCentreX(), b.getY(),
                                     juce::Colours::transparentBlack,
                                     b.getCentreX(), b.getBottom(), false);
      g.setGradientFill(highlight);
      g.fillRoundedRectangle(b, 8.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.82f));
      g.drawRoundedRectangle(b, 8.0f, 1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(10);
      toggleChip.setBounds(area.removeFromTop(34));
      area.removeFromTop(8);
      removeChip.setBounds(area.removeFromTop(34));
    }

  private:
    ActionChip toggleChip;
    ActionChip removeChip;
    std::function<void()> onToggle;
    std::function<void()> onRemove;

    void closeSelf() {
      if (auto *callout = findParentComponentOfClass<juce::CallOutBox>()) {
        callout->exitModalState(0);
        callout->setVisible(false);
      }
    }
  };

  const auto songIndex = songs[static_cast<size_t>(rowIndex)].songIndex;
  const bool autoPlayEnabled = songs[static_cast<size_t>(rowIndex)].autoPlayNext;
  const auto rowBounds =
      juce::Rectangle<int>(rowSideInset, rowTopAtIndex(rowIndex),
                           getWidth() - (rowSideInset * 2), rowHeight);
  const auto optionsBounds = optionsButtonBoundsForRow(rowBounds);

  auto safeThis = juce::Component::SafePointer<ListContent>(this);
  auto panel = std::make_unique<SongActionsPopover>(
      autoPlayEnabled,
      rowIndex + 1 < static_cast<int>(songs.size()),
      [safeThis, songIndex] {
        if (safeThis == nullptr)
          return;
        if (safeThis->onAutoPlayNextToggled)
          safeThis->onAutoPlayNextToggled(songIndex);
      },
      [safeThis, songIndex] {
        if (safeThis == nullptr)
          return;
        if (safeThis->onSongRemoveRequested)
          safeThis->onSongRemoveRequested(songIndex);
      });
  panel->setSize(240, 96);

  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  juce::Component *parentComp = editor != nullptr
                                    ? static_cast<juce::Component *>(editor)
                                    : static_cast<juce::Component *>(this);
  const auto anchor = parentComp->getLocalArea(this, optionsBounds);
  presentActionPopover(std::move(panel), anchor, parentComp);
}

class SongListPanel::SetlistManagerPanel final : public juce::Component {
public:
  using SetlistLoadedCallback = std::function<void(const juce::String &)>;

  explicit SetlistManagerPanel(SetlistLoadedCallback onLoaded)
      : onSetlistLoaded(std::move(onLoaded)) {
    setSize(860, 560);

    titleLabel.setText(L10n::tr("Setlists", "Setlists"),
                       juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(18.0f));
    addAndMakeVisible(titleLabel);

    createButton.setButtonText(L10n::tr("Crear Setlist", "Create a Setlist"));
    createButton.onClick = [this] { createSetlist(); };
    addAndMakeVisible(createButton);

    searchEditor.setTextToShowWhenEmpty(
        L10n::tr("Buscar Setlists", "Search Setlists"), UITheme::textSecondary);
    searchEditor.onTextChange = [this] { refreshSetlistRows(); };
    addAndMakeVisible(searchEditor);

    setlistViewport.setViewedComponent(&setlistContent, false);
    setlistViewport.setScrollBarsShown(true, false, false, false);
    setlistViewport.setScrollBarThickness(1);
    addAndMakeVisible(setlistViewport);

    songsViewport.setViewedComponent(&songsContent, false);
    songsViewport.setScrollBarsShown(true, false, false, false);
    songsViewport.setScrollBarThickness(1);
    addAndMakeVisible(songsViewport);

    addSongButton.setButtonText(L10n::tr(u8"Agregar canción", "Add Song"));
    addSongButton.onClick = [this] { showAddSongMenu(); };
    addAndMakeVisible(addSongButton);

    loadButton.setButtonText(L10n::tr("Cargar setlist", "Load setlist"));
    loadButton.onClick = [this] { loadSelectedSetlist(); };
    addAndMakeVisible(loadButton);

    setlistContent.setOnAction(
        [this](int index, SetlistListContent::Action action) {
          handleSetlistAction(index, action);
        });

    songsContent.setOnAction(
        [this](int row, SongsListContent::Action action) {
          handleSongAction(row, action);
        });

    applyTheme();
    reloadLibrary();
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.7f));
    g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(18);
    auto header = area.removeFromTop(36);
    titleLabel.setBounds(header);

    const int createW = 160;
    auto headerRight = header.removeFromRight(createW);
    createButton.setBounds(headerRight.removeFromLeft(createW));

    area.removeFromTop(8);
    auto topRow = area.removeFromTop(30);
    searchEditor.setBounds(
        topRow.removeFromLeft(juce::jmin(320, topRow.getWidth() / 2)));

    area.removeFromTop(10);
    auto contentArea = area;
    const int leftW = juce::jlimit(
        240, 320, juce::roundToInt(static_cast<float>(contentArea.getWidth()) * 0.38f));
    auto left = contentArea.removeFromLeft(leftW);
    auto right = contentArea;

    left.removeFromTop(6);
    setlistViewport.setBounds(left);

    auto rightHeader = right.removeFromTop(32);
    addSongButton.setBounds(rightHeader.removeFromLeft(110));
    rightHeader.removeFromLeft(8);
    loadButton.setBounds(rightHeader.removeFromLeft(130));
    right.removeFromTop(6);
    songsViewport.setBounds(right);

    setlistContent.setSize(
        juce::jmax(1, setlistViewport.getWidth()),
        juce::jmax(setlistViewport.getHeight(),
                   setlistContent.getPreferredHeight()));
    songsContent.setSize(juce::jmax(1, songsViewport.getWidth()),
                         juce::jmax(songsViewport.getHeight(),
                                    songsContent.getPreferredHeight()));
  }

private:
  struct SetlistRow {
    juce::String id;
    juce::String name;
    int songCount = 0;
  };

  class SetlistListContent final : public juce::Component {
  public:
    enum class Action { Select, Rename, Delete };

    void setRows(std::vector<SetlistRow> nextRows, int selected) {
      rows = std::move(nextRows);
      selectedIndex = selected;
      repaint();
    }

    void setOnAction(std::function<void(int, Action)> cb) {
      onAction = std::move(cb);
    }

    int getPreferredHeight() const {
      if (rows.empty())
        return 88;
      constexpr int rowH = 44;
      constexpr int gap = 6;
      return 8 + static_cast<int>(rows.size()) * (rowH + gap);
    }

    void paint(juce::Graphics &g) override {
      if (rows.empty()) {
        g.setColour(UITheme::textSecondary.withAlpha(0.8f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(L10n::tr("Aun no hay setlists.", "No setlists yet."),
                         getLocalBounds().reduced(10),
                         juce::Justification::centred, 1);
        return;
      }

      constexpr int rowH = 44;
      constexpr int gap = 6;
      for (size_t i = 0; i < rows.size(); ++i) {
        auto row = juce::Rectangle<int>(
            6, 8 + static_cast<int>(i) * (rowH + gap), getWidth() - 12, rowH);
        const bool selected = static_cast<int>(i) == selectedIndex;
        g.setColour(selected ? UITheme::cardBgHover : UITheme::cardBg);
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        g.setColour(UITheme::cardBorder.withAlpha(0.6f));
        g.drawRoundedRectangle(row.toFloat(), 4.0f, 1.0f);

        auto textArea = row.reduced(10, 6);
        const int actionSize = 16;
        const int actionGap = 6;
        const int actionRightPad = 10;
        const int actionY = row.getCentreY() - (actionSize / 2);
        const int deleteX = row.getRight() - actionRightPad - actionSize;
        const int renameX = deleteX - actionGap - actionSize;
        auto renameRect =
            juce::Rectangle<int>(renameX, actionY, actionSize, actionSize);
        auto deleteRect =
            juce::Rectangle<int>(deleteX, actionY, actionSize, actionSize);
        g.setColour(UITheme::textPrimary);
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(rows[i].name, textArea.removeFromTop(16),
                         juce::Justification::centredLeft, 1);
        g.setColour(UITheme::textSecondary);
        g.setFont(juce::FontOptions(9.0f));
        g.drawFittedText(juce::String(rows[i].songCount) + " " +
                             L10n::tr("canciones", "songs"),
                         textArea,
                         juce::Justification::centredLeft, 1);

        drawActionButton(g, renameRect, IconGlyph::Pencil,
                         UITheme::textSecondary);
        drawDeleteButton(g, deleteRect, UITheme::accent);
      }
    }

    void mouseUp(const juce::MouseEvent &event) override {
      const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
      if (!juce::isPositiveAndBelow(idx, static_cast<int>(rows.size())))
        return;

      auto row = juce::Rectangle<int>(
          6, 8 + static_cast<int>(idx) * (44 + 6), getWidth() - 12, 44);
      const int actionSize = 16;
      const int actionGap = 6;
      const int actionRightPad = 10;
      const int actionY = row.getCentreY() - (actionSize / 2);
      const int deleteX = row.getRight() - actionRightPad - actionSize;
      const int renameX = deleteX - actionGap - actionSize;
      auto renameRect =
          juce::Rectangle<int>(renameX, actionY, actionSize, actionSize);
      auto deleteRect =
          juce::Rectangle<int>(deleteX, actionY, actionSize, actionSize);

      const auto pos = event.position.toInt();
      if (renameRect.contains(pos)) {
        if (onAction)
          onAction(idx, Action::Rename);
        return;
      }
      if (deleteRect.contains(pos)) {
        if (onAction)
          onAction(idx, Action::Delete);
        return;
      }
      if (onAction)
        onAction(idx, Action::Select);
    }

  private:
    int rowIndexAtY(int y) const {
      constexpr int rowStart = 8;
      constexpr int rowH = 44;
      constexpr int gap = 6;
      if (y < rowStart)
        return -1;
      const int relY = y - rowStart;
      const int step = rowH + gap;
      const int row = relY / step;
      if (!juce::isPositiveAndBelow(row, static_cast<int>(rows.size())))
        return -1;
      if ((relY % step) >= rowH)
        return -1;
      return row;
    }

    std::vector<SetlistRow> rows;
    int selectedIndex = -1;
    std::function<void(int, Action)> onAction;

    void drawActionButton(juce::Graphics &g, juce::Rectangle<int> bounds,
                          IconGlyph glyph, juce::Colour iconColour) {
      auto b = bounds.toFloat();
      g.setColour(UITheme::cardBgHover);
      g.fillRoundedRectangle(b, 3.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.6f));
      g.drawRoundedRectangle(b, 3.0f, 1.0f);
      drawIconGlyph(g, b.reduced(2.0f), glyph, iconColour, 1.4f);
    }

    void drawDeleteButton(juce::Graphics &g, juce::Rectangle<int> bounds,
                          juce::Colour iconColour) {
      auto b = bounds.toFloat();
      g.setColour(UITheme::cardBgHover);
      g.fillRoundedRectangle(b, 3.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.6f));
      g.drawRoundedRectangle(b, 3.0f, 1.0f);

      const auto pad = 5.0f;
      g.setColour(iconColour);
      g.drawLine(b.getX() + pad, b.getY() + pad, b.getRight() - pad,
                 b.getBottom() - pad, 1.6f);
      g.drawLine(b.getRight() - pad, b.getY() + pad, b.getX() + pad,
                 b.getBottom() - pad, 1.6f);
    }
  };

  class SongsListContent final : public juce::Component {
  public:
    enum class Action { Remove };

    struct SongRow {
      juce::String songId;
      juce::String title;
      juce::String meta;
    };

    void setRows(std::vector<SongRow> nextRows) {
      rows = std::move(nextRows);
      repaint();
    }

    void setOnAction(std::function<void(int, Action)> cb) {
      onAction = std::move(cb);
    }

    int getPreferredHeight() const {
      if (rows.empty())
        return 120;
      constexpr int rowH = 52;
      constexpr int gap = 6;
      return 8 + static_cast<int>(rows.size()) * (rowH + gap);
    }

    void paint(juce::Graphics &g) override {
      if (rows.empty()) {
        g.setColour(UITheme::textSecondary.withAlpha(0.8f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(L10n::tr(u8"Aún no hay canciones.", "No songs yet."),
                         getLocalBounds().reduced(10),
                         juce::Justification::centred, 1);
        return;
      }

      constexpr int rowH = 52;
      constexpr int gap = 6;
      for (size_t i = 0; i < rows.size(); ++i) {
        auto row = juce::Rectangle<int>(
            6, 8 + static_cast<int>(i) * (rowH + gap), getWidth() - 12, rowH);
        g.setColour(UITheme::cardBgHover);
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
        g.setColour(UITheme::cardBorder.withAlpha(0.6f));
        g.drawRoundedRectangle(row.toFloat(), 4.0f, 1.0f);

        auto c = row.reduced(8, 6);
        c.removeFromLeft(24);
        auto buttonArea = c.removeFromRight(26);
        auto removeRect = buttonArea.removeFromRight(22).reduced(2);
        drawActionButton(g, removeRect, "X", UITheme::accent);

        g.setColour(UITheme::textPrimary.darker(0.16f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(rows[i].title, c.removeFromTop(16),
                         juce::Justification::centredLeft, 1);
        g.setColour(UITheme::textSecondary);
        g.setFont(juce::FontOptions(9.0f));
        g.drawFittedText(rows[i].meta, c, juce::Justification::centredLeft, 1);
      }
    }

    void mouseUp(const juce::MouseEvent &event) override {
      const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
      if (!juce::isPositiveAndBelow(idx, static_cast<int>(rows.size())))
        return;

      auto row = juce::Rectangle<int>(
          6, 8 + idx * (52 + 6), getWidth() - 12, 52);
      auto c = row.reduced(8, 6);
      c.removeFromLeft(24);
      auto buttonArea = c.removeFromRight(26);
      auto removeRect = buttonArea.removeFromRight(22).reduced(2);

      const auto pos = event.position.toInt();
      if (removeRect.contains(pos)) {
        if (onAction)
          onAction(idx, Action::Remove);
        return;
      }
    }

  private:
    int rowIndexAtY(int y) const {
      constexpr int rowStart = 8;
      constexpr int rowH = 52;
      constexpr int gap = 6;
      if (y < rowStart)
        return -1;
      const int relY = y - rowStart;
      const int step = rowH + gap;
      const int row = relY / step;
      if (!juce::isPositiveAndBelow(row, static_cast<int>(rows.size())))
        return -1;
      if ((relY % step) >= rowH)
        return -1;
      return row;
    }

    void drawActionButton(juce::Graphics &g, juce::Rectangle<int> bounds,
                          const juce::String &label, juce::Colour colour) {
      g.setColour(UITheme::cardBorder.withAlpha(0.5f));
      g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);
      g.setColour(colour);
      g.setFont(juce::FontOptions(10.0f));
      g.drawFittedText(label, bounds, juce::Justification::centred, 1);
    }

    std::vector<SongRow> rows;
    std::function<void(int, Action)> onAction;
  };

  void applyTheme() {
    titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
    applyTextEditorTheme(searchEditor, UITheme::cardBgHover,
                         UITheme::cardBorder.withAlpha(0.6f));
    searchEditor.setJustification(juce::Justification::centredLeft);
    styleButton(createButton, UITheme::cardBgHover);
    styleButton(loadButton, UITheme::accent);
    styleButton(addSongButton, UITheme::cardBgHover);
  }

  void styleButton(juce::TextButton &button, juce::Colour base) {
    button.setColour(juce::TextButton::buttonColourId, base);
    button.setColour(juce::TextButton::buttonOnColourId, base.darker(0.08f));
    button.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    button.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
  }

  void reloadLibrary() {
    library = SongLibraryStore::loadLibrarySnapshot();
    if (selectedSetlistId.isEmpty() || !setlistExists(selectedSetlistId)) {
      selectedSetlistId = library.activeSetlistId;
    }
    if (selectedSetlistId.isEmpty() && !library.setlists.empty())
      selectedSetlistId = library.setlists.front().id;
    refreshSetlistRows();
    refreshSongsRows();
  }

  bool setlistExists(const juce::String &id) const {
    for (const auto &setlist : library.setlists) {
      if (setlist.id == id)
        return true;
    }
    return false;
  }

  std::vector<SetlistRow> buildSetlistRows() const {
    const auto query = searchEditor.getText().trim().toLowerCase();
    std::vector<SetlistRow> rows;
    rows.reserve(library.setlists.size());
    for (const auto &setlist : library.setlists) {
      if (query.isNotEmpty() && !setlist.name.toLowerCase().contains(query))
        continue;
      rows.push_back({setlist.id, setlist.name,
                      static_cast<int>(setlist.items.size())});
    }
    return rows;
  }

  void refreshSetlistRows() {
    auto rows = buildSetlistRows();

    int selectedIndex = -1;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].id == selectedSetlistId) {
        selectedIndex = static_cast<int>(i);
        break;
      }
    }
    setlistContent.setRows(std::move(rows), selectedIndex);
    resized();
  }

  void refreshSongsRows() {
    std::vector<SongsListContent::SongRow> rows;
    const auto *setlist = findSetlist(selectedSetlistId);
    if (setlist != nullptr) {
      rows.reserve(setlist->items.size());
      for (size_t i = 0; i < setlist->items.size(); ++i) {
        const auto &item = setlist->items[i];
        const auto *song = findSong(item.songId);
        if (song == nullptr)
          continue;
        rows.push_back(buildSongRow(*song, static_cast<int>(i)));
      }
    }
    songsContent.setRows(std::move(rows));
    resized();
  }

  SongsListContent::SongRow buildSongRow(const ImportedSongData &song,
                                         int index) const {
    juce::ignoreUnused(index);
    const auto includedStems = static_cast<int>(std::count_if(
        song.stems.begin(), song.stems.end(),
        [](const ImportedStemData &stem) { return stem.include; }));

    juce::String meta;
    if (song.artist.isNotEmpty()) {
      meta << song.artist << u8" • ";
    }
    if (song.key.isNotEmpty()) {
      meta << song.key << u8" • ";
    }
    if (song.bpm > 0) {
      meta << juce::String(song.bpm) << " BPM" << u8" • ";
    }
    meta << juce::String(includedStems) << " stems";

    SongsListContent::SongRow row;
    row.songId = song.id;
    row.title = song.title.isNotEmpty()
                    ? song.title
                    : L10n::tr(u8"Canción sin título", "Untitled Song");
    row.meta = meta;
    return row;
  }

  const SetlistData *findSetlist(const juce::String &id) const {
    for (const auto &setlist : library.setlists) {
      if (setlist.id == id)
        return &setlist;
    }
    return nullptr;
  }

  const ImportedSongData *findSong(const juce::String &id) const {
    for (const auto &song : library.songs) {
      if (song.id == id)
        return &song;
    }
    return nullptr;
  }

  void selectSetlistByIndex(int index) {
    auto rows = buildSetlistRows();
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
      return;
    selectedSetlistId = rows[static_cast<size_t>(index)].id;
    refreshSetlistRows();
    refreshSongsRows();
  }

  void handleSetlistAction(int index, SetlistListContent::Action action) {
    auto rows = buildSetlistRows();
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows.size())))
      return;
    const auto id = rows[static_cast<size_t>(index)].id;
    switch (action) {
    case SetlistListContent::Action::Select:
      selectedSetlistId = id;
      refreshSetlistRows();
      refreshSongsRows();
      break;
    case SetlistListContent::Action::Rename:
      renameSetlist(id);
      break;
    case SetlistListContent::Action::Delete:
      deleteSetlist(id);
      break;
    }
  }

  void handleSongAction(int row, SongsListContent::Action action) {
    const auto *setlist = findSetlist(selectedSetlistId);
    if (setlist == nullptr)
      return;
    if (!juce::isPositiveAndBelow(row,
                                  static_cast<int>(setlist->items.size())))
      return;
    const auto &item = setlist->items[static_cast<size_t>(row)];
    switch (action) {
    case SongsListContent::Action::Remove:
      SongLibraryStore::removeSongFromSetlist(selectedSetlistId, item.songId);
      dirty = true;
      break;
    }
    reloadLibrary();
  }

  void showAddSongMenu() {
    const auto *setlist = findSetlist(selectedSetlistId);
    if (setlist == nullptr)
      return;
    juce::PopupMenu menu;
    for (const auto &song : library.songs) {
      const bool exists =
          std::any_of(setlist->items.begin(), setlist->items.end(),
                      [&song](const SetlistItem &item) {
                        return item.songId == song.id;
                      });
      if (exists)
        continue;
      menu.addItem(song.title.isNotEmpty()
                       ? song.title
                       : L10n::tr(u8"Canción sin título", "Untitled Song"),
                   [this, id = song.id] {
                     SongLibraryStore::addSongToSetlist(selectedSetlistId, id);
                     dirty = true;
                     reloadLibrary();
                   });
    }
    if (menu.getNumItems() == 0) {
      menu.addItem(L10n::tr("No hay canciones disponibles",
                            "No songs available"),
                   false, false, nullptr);
    }
    menu.showMenuAsync(makeTouchPopupMenuOptions(&addSongButton));
  }

  void createSetlist() {
    promptTextAsync(
        L10n::tr("Crear Setlist", "Create Setlist"),
        L10n::tr("Nombre del setlist:", "Setlist name:"), "",
        [this](juce::String name) {
          name = name.trim();
          if (name.isEmpty())
            return;
          const auto id = SongLibraryStore::createSetlist(name);
          if (id.isNotEmpty())
            selectedSetlistId = id;
          dirty = true;
          reloadLibrary();
        });
  }

  void renameSetlist() { renameSetlist(selectedSetlistId); }

  void renameSetlist(const juce::String &id) {
    if (id.isEmpty())
      return;
    const auto *setlist = findSetlist(id);
    if (setlist == nullptr)
      return;
    promptTextAsync(
        L10n::tr("Renombrar Setlist", "Rename Setlist"),
        L10n::tr("Nombre del setlist:", "Setlist name:"), setlist->name,
        [this, id](juce::String name) {
          name = name.trim();
          if (name.isEmpty())
            return;
          SongLibraryStore::renameSetlist(id, name);
          dirty = true;
          reloadLibrary();
        });
  }

  void deleteSetlist() { deleteSetlist(selectedSetlistId); }

  void deleteSetlist(const juce::String &id) {
    if (id.isEmpty())
      return;
    const auto safe = juce::Component::SafePointer<SetlistManagerPanel>(this);
    if (library.setlists.size() <= 1) {
      appModal = std::make_unique<AppModal>(
          *this, AppModal::Kind::Information,
          L10n::tr("No se puede eliminar", "Can't delete"),
          L10n::tr("Se requiere al menos un setlist.",
                   "At least one setlist is required."),
          L10n::tr("Entendido", "OK"), juce::String(), nullptr);
      appModal->onClosed = [safe] { if (safe != nullptr) safe->appModal.reset(); };
      appModal->show();
      return;
    }
    appModal = std::make_unique<AppModal>(
        *this, AppModal::Kind::Destructive,
        L10n::tr("Eliminar setlist", "Delete setlist"),
        L10n::tr(u8"Eliminar este setlist? Esto no quitará canciones de la biblioteca.",
                 "Delete this setlist? This won't remove songs from the library."),
        L10n::tr("Eliminar", "Delete"), L10n::tr("Cancelar", "Cancel"),
        [safe, id](bool accepted, const juce::String &) {
          if (!accepted || safe == nullptr)
            return;
          SongLibraryStore::deleteSetlist(id);
          safe->selectedSetlistId = {};
          safe->dirty = true;
          safe->reloadLibrary();
        });
    appModal->onClosed = [safe] { if (safe != nullptr) safe->appModal.reset(); };
    appModal->show();
  }

  void loadSelectedSetlist() {
    if (selectedSetlistId.isEmpty())
      return;
    if (!SongLibraryStore::setActiveSetlistId(selectedSetlistId))
      return;
    if (onSetlistLoaded)
      onSetlistLoaded(selectedSetlistId);
    dirty = false;
    closeSelf();
  }

  void promptTextAsync(const juce::String &title, const juce::String &message,
                       const juce::String &initial,
                       std::function<void(juce::String)> onComplete) {
    const auto safe = juce::Component::SafePointer<SetlistManagerPanel>(this);
    appModal = std::make_unique<AppModal>(
        *this, AppModal::Kind::Input, title,
        L10n::tr("Escribe un nombre para identificar este setlist.",
                 "Enter a name to identify this setlist."),
        initial.isEmpty() ? L10n::tr("Crear", "Create")
                          : L10n::tr("Guardar", "Save"),
        L10n::tr("Cancelar", "Cancel"),
        [safe, callback = std::move(onComplete)](bool accepted,
                                                  const juce::String &value) {
          if (accepted && safe != nullptr && callback) callback(value);
        }, message, initial);
    appModal->onClosed = [safe] { if (safe != nullptr) safe->appModal.reset(); };
    appModal->show();
  }

  void closeSelf() {
    if (dirty && onSetlistLoaded) {
      const auto active =
          SongLibraryStore::loadLibrarySnapshot().activeSetlistId;
      onSetlistLoaded(active);
    }
    if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
      dw->exitModalState(0);
      dw->setVisible(false);
    }
  }

  juce::Label titleLabel;
  std::unique_ptr<AppModal> appModal;
  juce::TextButton createButton;
  juce::TextEditor searchEditor;
  juce::TextButton addSongButton;
  juce::TextButton loadButton;

  juce::Viewport setlistViewport;
  SetlistListContent setlistContent;
  juce::Viewport songsViewport;
  SongsListContent songsContent;

  LibraryState library;
  juce::String selectedSetlistId;
  SetlistLoadedCallback onSetlistLoaded;
  bool dirty = false;
};

void SongListPanel::showSongImportOverlay(
    std::unique_ptr<SongImportWizardComponent> panel) {
  if (panel == nullptr)
    return;

  auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
  auto *host = editor != nullptr ? static_cast<juce::Component *>(editor)
                                 : static_cast<juce::Component *>(this);
  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  const auto dismiss = [safeThis] {
    juce::MessageManager::callAsync([safeThis] {
      if (safeThis != nullptr)
        safeThis->songImportOverlay.reset();
    });
  };

  songImportOverlay.reset();
  songImportOverlay = std::make_unique<SongWizardOverlay>(std::move(panel), dismiss);
  host->addAndMakeVisible(*songImportOverlay);
  songImportOverlay->setBounds(host->getLocalBounds());
  songImportOverlay->toFront(false);
}

void SongListPanel::showLibraryCreationPage(
    std::unique_ptr<SongImportWizardComponent> panel) {
  if (panel == nullptr || libraryCreationWizard != nullptr)
    return;
  panel->setEmbeddedLibraryPage(true);
  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  panel->setOnDismissRequested([safeThis] {
    // Defer destruction until the wizard's button/completion callback returns.
    juce::MessageManager::callAsync([safeThis] {
      if (safeThis == nullptr)
        return;
      safeThis->pageContainer->showPage(2, true, [safeThis] {
        if (safeThis != nullptr)
          safeThis->libraryCreationWizard.reset();
      });
    });
  });
  libraryCreationWizard = std::move(panel);
  auto &creationPage = pageContainer->getPage(3);
  creationPage.addAndMakeVisible(*libraryCreationWizard);
  libraryCreationWizard->setBounds(creationPage.getLocalBounds());
  showPage(3, true);
}

void SongListPanel::openProjectFileChooser() {
  if (remoteLibrary) return;
  // Some Windows native file dialogs hide every entry when filtering by an
  // application-specific extension that is not yet registered on the system.
  // Keep all files visible there, then enforce .pwproj in openProjectFile().
#if JUCE_WINDOWS
  constexpr auto projectChooserPattern = "*.pwproj;*.PWPROJ;*.*";
#else
  constexpr auto projectChooserPattern = "*.pwproj";
#endif
  projectFileChooser = std::make_unique<juce::FileChooser>(
      L10n::tr("Abrir proyecto", "Open project"), juce::File{},
      projectChooserPattern);
  projectFileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis = juce::Component::SafePointer<SongListPanel>(this)](
                                      const juce::FileChooser &chooser) {
    if (safeThis != nullptr)
      safeThis->openProjectFileFromUrl(chooser.getURLResult());
  });
}

void SongListPanel::openProjectFileFromUrl(const juce::URL &url) {
  if (url.isEmpty())
    return;

  const auto file = url.getLocalFile();
  SessionAuthLogger::write("[ProjectOpenUI] picker url=" + url.toString(false) +
                           " localPath=" + file.getFullPathName());

#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
  juce::String importError;
  const auto importedFile = importProjectUrlToSandbox(url, importError);
  if (importedFile == juce::File{}) {
    SessionAuthLogger::write("[ProjectOpenUI] import failed url=" +
                             url.toString(false) + " error=\"" +
                             importError + "\"");
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("No se pudo importar el proyecto", "Couldn't import project"),
        importError);
    return;
  }

  SessionAuthLogger::write("[ProjectOpenUI] imported url to path=" +
                           importedFile.getFullPathName() + " size=" +
                           juce::String(importedFile.getSize()));
  openProjectFile(importedFile);
#else
  openProjectFile(file);
#endif
}

void SongListPanel::openProjectFile(const juce::File &file) {
  if (remoteLibrary) return;
  SessionAuthLogger::write("[ProjectOpenUI] requested path=" +
                           file.getFullPathName() + " exists=" +
                           juce::String(file.existsAsFile() ? 1 : 0) +
                           " size=" + juce::String(file.getSize()));
  if (!file.existsAsFile()) {
    SessionAuthLogger::write("[ProjectOpenUI] file missing path=" +
                             file.getFullPathName());
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Proyecto no encontrado", "Project not found"),
        L10n::tr("El archivo seleccionado ya no existe o no está disponible.",
                 "The selected file no longer exists or is unavailable."));
    return;
  }
  if (!file.hasFileExtension(ProjectStore::kFileExtension)) {
    SessionAuthLogger::write("[ProjectOpenUI] invalid extension path=" +
                             file.getFullPathName());
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Archivo no compatible", "Unsupported file"),
        L10n::tr("Selecciona un proyecto con extensión .pwproj.",
                 "Select a project with the .pwproj extension."));
    return;
  }
  SessionAuthLogger::write("[ProjectOpenUI] opening path=" +
                           file.getFullPathName());
  ProjectDocument document;
  juce::String error;
  if (!ProjectStore::open(file, document, &error) ||
      !SongLibraryStore::registerProjectSong(document.song)) {
    SessionAuthLogger::write("[ProjectOpenUI] failed path=" +
                             file.getFullPathName() + " error=\"" + error +
                             "\" songId=" + document.song.id);
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
        L10n::tr("No se pudo abrir el proyecto", "Couldn't open project"), error);
    return;
  }
  SessionAuthLogger::write("[ProjectOpenUI] success path=" +
                           file.getFullPathName() + " songId=" +
                           document.song.id + " stems=" +
                           juce::String(static_cast<int>(document.song.stems.size())));
  selectedSongId = document.song.id;
  if (!remoteLibrary) AppSettings::setLastSelectedSongId(selectedSongId);
  if (libraryTargetSetlistId.isNotEmpty())
    SongLibraryStore::addSongToSetlist(libraryTargetSetlistId, selectedSongId);
  refreshFromStorage();
  if (onSongActivated != nullptr) {
    const int index = getSongIndexById(selectedSongId);
    onSongActivated(document.song, index);
  }
}

void SongListPanel::beginLegacySongConversion(const juce::String &songId) {
  if (remoteLibrary) return;
  const auto trimmedSongId = songId.trim();
  const auto songIt = std::find_if(
      librarySongs.begin(), librarySongs.end(),
      [&trimmedSongId](const ImportedSongData &candidate) {
        return candidate.id == trimmedSongId;
      });
  if (songIt == librarySongs.end() || isProjectBackedSong(*songIt))
    return;

  auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
  if (editor != nullptr && editor->isSongCurrentlyLoaded(trimmedSongId)) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Canción actualmente cargada", "Song currently loaded"),
        L10n::tr(
            "Carga otra canción antes de convertir esta. Así PlayWorship puede cerrar sus archivos legacy y eliminarlos con seguridad.",
            "Load another song before converting this one. This lets PlayWorship close and safely remove its legacy files."));
    return;
  }
  if (editor != nullptr)
    editor->flushActiveSongPersistenceNow();

  const auto title = juce::File::createLegalFileName(songIt->title.trim());
  const auto artist = juce::File::createLegalFileName(songIt->artist.trim());
  const auto baseName = artist.isNotEmpty() && title.isNotEmpty()
                            ? artist + " - " + title
                            : (title.isNotEmpty() ? title : "Canción");

  juce::AlertWindow choice(
      L10n::tr("Convertir a proyecto", "Convert to project"),
      L10n::tr(
          "Se creará y validará el proyecto antes de eliminar la estructura legacy. ¿Dónde quieres guardarlo?",
          "The project will be created and validated before the legacy structure is removed. Where do you want to save it?"),
      juce::MessageBoxIconType::QuestionIcon);
  choice.addButton(L10n::tr("Biblioteca local", "Local library"), 1);
  choice.addButton(L10n::tr("Elegir ubicación...", "Choose location..."), 2);
  choice.addButton(L10n::tr("Cancelar", "Cancel"), 0);
  const int choiceId =
#if JUCE_ANDROID
      1;
#else
      choice.runModalLoop();
#endif
  if (choiceId == 0)
    return;

  if (choiceId == 1) {
    auto localDirectory =
        SongLibraryStore::getRootDirectory().getChildFile("projects");
    localDirectory.createDirectory();
    const auto destination = localDirectory.getNonexistentChildFile(
        baseName, ProjectStore::kFileExtension, false);
    convertLegacySongAtDestination(trimmedSongId, destination);
    return;
  }

  projectConversionFileChooser = std::make_unique<juce::FileChooser>(
      L10n::tr("Guardar proyecto convertido", "Save converted project"),
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
          .getChildFile(baseName + ProjectStore::kFileExtension),
      "*.pwproj");
  projectConversionFileChooser->launchAsync(
      juce::FileBrowserComponent::saveMode |
          juce::FileBrowserComponent::canSelectFiles |
          juce::FileBrowserComponent::warnAboutOverwriting,
      [safeThis = juce::Component::SafePointer<SongListPanel>(this),
       trimmedSongId](const juce::FileChooser &chooser) {
        if (safeThis == nullptr)
          return;
        auto destination = chooser.getResult();
        if (destination == juce::File{})
          return;
        if (!destination.hasFileExtension(ProjectStore::kFileExtension))
          destination =
              destination.withFileExtension(ProjectStore::kFileExtension);
        safeThis->convertLegacySongAtDestination(trimmedSongId, destination);
      });
}

void SongListPanel::convertLegacySongAtDestination(
    const juce::String &songId, const juce::File &destination) {
  auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
  auto libraryDestination = destination;
#if MTCONTROLS_MAC_APP_STORE
  // Keep the registered project inside the container. A Save panel grants
  // this process access to an exported copy, not persistent access next run.
  if (!destination.isAChildOf(SongLibraryStore::getRootDirectory())) {
    auto projects = SongLibraryStore::getRootDirectory().getChildFile("projects");
    projects.createDirectory();
    libraryDestination = projects.getNonexistentChildFile(
        destination.getFileNameWithoutExtension(), ProjectStore::kFileExtension, false);
  }
#endif
  LegacyProjectMigrationTask task(songId, libraryDestination,
                                  editor != nullptr
                                      ? static_cast<juce::Component *>(editor)
                                      : static_cast<juce::Component *>(this));
  const bool threadCompleted =
#if JUCE_ANDROID
      (task.startThread(), task.waitForThreadToExit(-1), true);
#else
      task.runThread();
#endif
  if (!threadCompleted || !task.success) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("No se pudo convertir la canción",
                 "Couldn't convert song"),
        task.error.isNotEmpty()
            ? task.error
            : L10n::tr("La conversión no pudo completarse.",
                       "Conversion couldn't be completed."));
    return;
  }

#if MTCONTROLS_MAC_APP_STORE
  if (libraryDestination != destination && !libraryDestination.copyFileTo(destination)) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("No se pudo exportar la copia", "Couldn't export the copy"),
        L10n::tr("El proyecto convertido se conserva en Biblioteca. Intenta exportarlo nuevamente.",
                 "The converted project is kept in Library. Try exporting it again."));
    refreshFromStorage();
    return;
  }
#endif

  selectedSongId = songId;
  if (!remoteLibrary) AppSettings::setLastSelectedSongId(selectedSongId);
  refreshFromStorage();
  const auto sourceLabel = task.sourceSetlistName.isNotEmpty()
                               ? task.sourceSetlistName
                               : L10n::tr("Valores predeterminados",
                                          "Default settings");
  juce::NativeMessageBox::showMessageBoxAsync(
      juce::MessageBoxIconType::InfoIcon,
      L10n::tr("Conversión completada", "Conversion completed"),
      L10n::tr("La canción ahora vive en el proyecto:\n",
               "The song now lives in the project:\n") +
          libraryDestination.getFullPathName() + "\n\n" +
          L10n::tr("Configuración tomada de: ", "Settings taken from: ") +
          sourceLabel);
}

void SongListPanel::openSongImportWizard(const bool withinLibrary) {
  if (remoteLibrary) return;
  if (libraryCreationWizard != nullptr)
    return;
  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  auto panel = std::make_unique<SongImportWizardComponent>(
      [safeThis](ImportedSongData song, juce::String &errorMessage,
                 const SongImportProgressFn &progress) {
        if (!SongProjectCreation::save(std::move(song), errorMessage, progress))
          return false;
        juce::MessageManager::callAsync([safeThis] {
          if (safeThis != nullptr)
            safeThis->refreshFromStorage();
        });
        return true;
      },
      SongImportWizardComponent::Mode::Import,
      ImportedSongData{}, SongProjectCreation::resolveDestination);

  if (withinLibrary)
    showLibraryCreationPage(std::move(panel));
  else
    showSongImportOverlay(std::move(panel));
}

void SongListPanel::openSongEditWizard(const juce::String &songId) {
  if (remoteLibrary) return;
  const auto trimmedSongId = songId.trim();
  if (trimmedSongId.isEmpty())
    return;

  ImportedSongData baseSong;
  bool found = false;
  for (const auto &song : librarySongs) {
    if (song.id == trimmedSongId) {
      baseSong = song;
      found = true;
      break;
    }
  }
  if (!found) {
    const auto snapshotSongs = SongLibraryStore::loadSongs();
    for (const auto &song : snapshotSongs) {
      if (song.id == trimmedSongId) {
        baseSong = song;
        found = true;
        break;
      }
    }
  }
  if (!found)
    return;

  if (onSongEditStarted)
    onSongEditStarted(trimmedSongId);

  auto safeThis = juce::Component::SafePointer<SongListPanel>(this);
  auto refreshEditedSong = [safeThis, editedSongId = trimmedSongId] {
    if (safeThis == nullptr)
      return;
    safeThis->refreshFromStorage();
    const auto allSongs = SongLibraryStore::loadSongs();
    const auto it = std::find_if(
        allSongs.begin(), allSongs.end(),
        [&editedSongId](const ImportedSongData &candidate) {
          return candidate.id == editedSongId;
        });
    if (it == allSongs.end())
      return;
    const int indexForSetlist = safeThis->getSongIndexById(editedSongId);
    if (safeThis->onSongEdited != nullptr) {
      // The editor knows which song is actually loaded. Selection can differ
      // from the playing song while editing a library item.
      safeThis->onSongEdited(*it, indexForSetlist >= 0 ? indexForSetlist : -1);
    } else if ((editedSongId == safeThis->selectedSongId ||
                editedSongId == safeThis->nowPlayingSongId) &&
               safeThis->onSongActivated != nullptr) {
      safeThis->onSongActivated(*it, indexForSetlist >= 0 ? indexForSetlist : -1);
    }
  };

  auto panel = std::make_unique<SongImportWizardComponent>(
      [editedSongId = trimmedSongId, refreshEditedSong](ImportedSongData song,
                                               juce::String &errorMessage,
                                               const SongImportProgressFn &progress) {
        song.id = editedSongId;
        juce::String updateError;
        const bool success =
            SongLibraryStore::updateSong(std::move(song), &updateError, progress);
        if (!success) {
          errorMessage = updateError.isNotEmpty()
                             ? updateError
                             : L10n::tr(u8"No se pudo actualizar la canción.",
                                        "Couldn't update the song.");
          return false;
        }

        juce::MessageManager::callAsync(refreshEditedSong);
        return true;
      },
      SongImportWizardComponent::Mode::Edit,
      baseSong,
      std::function<bool(const ImportedSongData &, juce::File &, juce::String &)>{},
      [editedSongId = trimmedSongId, refreshEditedSong](ImportedSongData song,
                                               juce::String &errorMessage) {
        song.id = editedSongId;
        juce::String updateError;
        const bool success =
            SongLibraryStore::updateSongMetadata(std::move(song), &updateError);
        if (!success) {
          errorMessage = updateError.isNotEmpty()
                             ? updateError
                             : L10n::tr(u8"No se pudo actualizar la canción.",
                                        "Couldn't update the song.");
          return false;
        }

        refreshEditedSong();
        return true;
      });
  panel->refreshThemeColours();
  showSongImportOverlay(std::move(panel));
}

void SongListPanel::openSetlistManager() {
  showSetlistsView();
}

void SongListPanel::showSetlistsView() {
  viewMode = ViewMode::Setlists;
  songsSubview = SongsSubview::SetlistSongs;
  initialViewApplied = true;
  refreshFromStorage();
  showPage(0, true);
}

void SongListPanel::showSongsView(const juce::String &setlistId) {
  if (setlistId.isNotEmpty()) {
    if (remoteLibrary) { if (onRemoteAction) onRemoteAction("setlist.select", {}, setlistId); }
    else SongLibraryStore::setActiveSetlistId(setlistId);
  }
  viewMode = ViewMode::Songs;
  songsSubview = SongsSubview::SetlistSongs;
  initialViewApplied = true;
  refreshFromStorage();
  showPage(1, true);
}

void SongListPanel::showLibraryView(const bool animate) {
  if (viewMode != ViewMode::Songs)
    return;
  songsSubview = SongsSubview::Library;
  showPage(2, animate);
}

void SongListPanel::showSongsSubview(const bool animate) {
  if (viewMode != ViewMode::Songs)
    return;
  songsSubview = SongsSubview::SetlistSongs;
  showPage(1, animate);
}

void SongListPanel::showPage(const int index, const bool animate) {
  pageContainer->showPage(index, animate);
}

void SongListPanel::applyInitialViewFromStorage() {
  if (initialViewApplied && !(viewMode == ViewMode::Setlists && activeSetlistId.isEmpty()))
    return;
  if (activeSetlistId.isNotEmpty()) {
    viewMode = ViewMode::Songs;
    songsSubview = SongsSubview::SetlistSongs;
  } else {
    viewMode = ViewMode::Setlists;
    songsSubview = SongsSubview::SetlistSongs;
  }
  initialViewApplied = true;
}

void SongListPanel::applyModeVisibility() {
  const bool hasActiveSetlist = activeSetlistId.isNotEmpty();
  titleLabel.setVisible(true);
  songsSubtitleLabel.setVisible(minimalPhoneMode);
  backButton.setVisible(true);
  headerMenuButton.setVisible(true);
  addSongsButton.setVisible(true);
  songsHeaderPanel.setVisible(!minimalPhoneMode);
  listViewport.setVisible(true);
  libraryCatalogPanel.setVisible(true);
  setlistsHeaderLabel.setVisible(true);
  setlistsSubtitleLabel.setVisible(true);
  createSetlistButton.setVisible(true);
  setlistSearchField.setVisible(true);
  setlistsViewport.setVisible(true);

  editSetlistButton.setVisible(false);
  headerMenuButton.setEnabled(hasActiveSetlist);
  addSongsButton.setEnabled(hasActiveSetlist);
  songsHeaderPanel.setActionsEnabled(hasActiveSetlist);
  createSetlistButton.setEnabled(true);
  setlistSearchField.setEnabled(true);
  backButton.setEnabled(true);

  for (juce::Component *c : std::array<juce::Component *, 13>{
           &titleLabel,          &songsSubtitleLabel,   &backButton,
           &headerMenuButton,    &addSongsButton,       &songsHeaderPanel,
           &listViewport,        &libraryCatalogPanel,  &setlistsHeaderLabel,
           &setlistsSubtitleLabel, &createSetlistButton, &setlistSearchField,
           &setlistsViewport}) {
    c->setAlpha(1.0f);
  }
}

void SongListPanel::animateTransitionIfNeeded(const ViewMode previousMode) {
  if (previousMode == viewMode)
    return;
  if (getWidth() <= 0 || getHeight() <= 0)
    return;

  auto &animator = juce::Desktop::getInstance().getAnimator();
  animator.cancelAllAnimations(true);
  stopTimer();

  applySongsLayout(computeSongsLayout());
  applySetlistsLayout(computeSetlistsLayout());

  std::array<juce::Component *, 8> songsGroup = {
      &titleLabel,         &songsSubtitleLabel, &backButton,
      &headerMenuButton,   &addSongsButton,     &songsHeaderPanel,
      &listViewport,       &libraryCatalogPanel};
  std::array<juce::Component *, 5> setlistsGroup = {
      &setlistsHeaderLabel, &setlistsSubtitleLabel, &createSetlistButton,
      &setlistSearchField, &setlistsViewport};

  const int slideDistance = juce::jmax(60, getWidth() - 20);
  const bool toSongs = viewMode == ViewMode::Songs;

  for (auto *c : songsGroup)
    c->setVisible(true);
  for (auto *c : setlistsGroup)
    c->setVisible(true);

  if (toSongs) {
    for (auto *c : songsGroup) {
      const auto target = c->getBounds();
      c->setBounds(target.translated(slideDistance, 0));
      c->setAlpha(0.0f);
      animator.animateComponent(c, target, 1.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
    for (auto *c : setlistsGroup) {
      const auto target = c->getBounds().translated(-slideDistance, 0);
      animator.animateComponent(c, target, 0.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
  } else {
    for (auto *c : setlistsGroup) {
      const auto target = c->getBounds();
      c->setBounds(target.translated(-slideDistance, 0));
      c->setAlpha(0.0f);
      animator.animateComponent(c, target, 1.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
    for (auto *c : songsGroup) {
      const auto target = c->getBounds().translated(slideDistance, 0);
      animator.animateComponent(c, target, 0.0f, transitionDurationMs, false,
                                1.0, 1.0);
    }
  }

  transitionInFlight = true;
  startTimer(transitionDurationMs + 24);
}

void SongListPanel::timerCallback() {
  stopTimer();
  if (!transitionInFlight)
    return;
  transitionInFlight = false;
  songsSubviewTransitionInFlight = false;
  juce::Desktop::getInstance().getAnimator().cancelAllAnimations(true);
  finishSongsSubviewAnimation();
  applyModeVisibility();
  resized();
  repaint();
}

void SongListPanel::refreshFromStorage() {
  auto library = (remoteLibrary ? *remoteLibrary : SongLibraryStore::loadLibrarySnapshot());
  // Read the active queue without activating or constructing any setlist.
  playbackLibrary = library;
  librarySongs = library.songs;
  activeSetlistId = library.activeSetlistId;
  activeSetlistName = L10n::tr("Setlist", "Setlist");
  activeSongIndices.clear();

  const SetlistData *activeSetlist = nullptr;
  for (const auto &setlist : library.setlists) {
    if (setlist.id == activeSetlistId) {
      activeSetlist = &setlist;
      break;
    }
  }
  if (activeSetlist == nullptr && !library.setlists.empty()) {
    activeSetlist = &library.setlists.front();
    activeSetlistId = activeSetlist->id;
  }
  if (activeSetlist != nullptr)
    activeSetlistName = activeSetlist->name;
  autoPlayNextMarks.clear();
  if (activeSetlist != nullptr) {
    for (const auto &item : activeSetlist->items) {
      if (item.transitionToNext.autoPlay)
        autoPlayNextMarks.addIfNotAlreadyThere(
            makeAutoPlayNextMarkKey(activeSetlistId, item.songId));
    }
  }
  applyInitialViewFromStorage();
  if (setlistsContent != nullptr) {
    std::vector<SetlistsContent::Row> setlistRows;
    setlistRows.reserve(library.setlists.size());
    const auto query = setlistSearchQuery.trim().toLowerCase();
    for (const auto &setlist : library.setlists) {
      if (query.isNotEmpty() &&
          !setlist.name.toLowerCase().contains(query))
        continue;

      double totalSeconds = 0.0;
      for (const auto &item : setlist.items) {
        const auto it =
            std::find_if(librarySongs.begin(), librarySongs.end(),
                         [&item](const ImportedSongData &song) {
                           return song.id == item.songId;
                         });
        if (it == librarySongs.end())
          continue;
        totalSeconds += estimateSongLengthSeconds(*it);
      }

      setlistRows.push_back({setlist.id, toCapitalCase(setlist.name),
                             static_cast<int>(setlist.items.size()),
                             totalSeconds});
    }
    setlistsContent->setRows(std::move(setlistRows), activeSetlistId);
  }

  std::vector<SongRow> rows;
  if (activeSetlist != nullptr)
    rows.reserve(activeSetlist->items.size());

  int order = 1;
  if (activeSetlist != nullptr) {
    for (size_t i = 0; i < activeSetlist->items.size(); ++i) {
      const auto &item = activeSetlist->items[i];
      const auto it =
          std::find_if(librarySongs.begin(), librarySongs.end(),
                       [&item](const ImportedSongData &song) {
                         return song.id == item.songId;
                       });
      if (it == librarySongs.end())
        continue;
      const auto &song = *it;
      const int libraryIndex =
          static_cast<int>(std::distance(librarySongs.begin(), it));
      activeSongIndices.push_back(libraryIndex);

      const auto includedStems = static_cast<int>(std::count_if(
          song.stems.begin(), song.stems.end(),
          [](const ImportedStemData &stem) { return stem.include; }));

      const int rowIndex = static_cast<int>(rows.size());
      const auto autoPlayKey = makeAutoPlayNextMarkKey(activeSetlistId, song.id);
      SongRow row{order++,
                  toSentenceCase(song.title.isNotEmpty()
                                     ? song.title
                                     : L10n::tr(u8"Canción sin título",
                                                "Untitled Song")),
                  song.artist.trim().toUpperCase(),
                  song.artworkPath.trim(),
                  song.key,
                  song.bpm,
                  includedStems,
                  song.id,
                  autoPlayNextMarks.contains(autoPlayKey),
                  isPlaybackActive && song.id == nowPlayingSongId,
                  song.id == selectedSongId,
                  rowIndex,
                  estimateSongLengthSeconds(song),
                  songAccentFromId(song.id),
                  -1.0,
                  {}};
      if (const auto indicator = cloudUploadIndicators.find(song.id.toStdString());
          indicator != cloudUploadIndicators.end()) {
        row.cloudUploadFraction = indicator->second.fraction;
        row.cloudUploadPhase = indicator->second.phase;
      }
      rows.push_back(std::move(row));
    }
  }

  if (!juce::isPositiveAndBelow(lastActivatedIndex,
                                static_cast<int>(activeSongIndices.size())))
    lastActivatedIndex = activeSongIndices.empty() ? -1 : 0;

  if (selectedSongId.isEmpty() &&
      juce::isPositiveAndBelow(lastActivatedIndex,
                               static_cast<int>(activeSongIndices.size()))) {
    const int libIndex = activeSongIndices[static_cast<size_t>(lastActivatedIndex)];
    if (juce::isPositiveAndBelow(libIndex, static_cast<int>(librarySongs.size())))
      selectedSongId = librarySongs[static_cast<size_t>(libIndex)].id;
  }

  listContent.setSongs(std::move(rows));
  listContent.setSelectedSongId(selectedSongId);
  libraryCatalogPanel.setRemoteLibrary(remoteLibrary);
  libraryCatalogPanel.setActiveSetlistId(activeSetlistId);
  libraryCatalogPanel.setNowPlayingSong(nowPlayingSongId, isPlaybackActive);
  libraryCatalogPanel.refreshFromStorage();
  refreshSongsHeaderSummary();
  titleLabel.setText(toCapitalCase(activeSetlistName),
                     juce::dontSendNotification);
  if (!transitionInFlight)
    applyModeVisibility();
  if (!transitionInFlight)
    resized();
  if (onSetlistNameChanged)
    onSetlistNameChanged(activeSetlistName);
}

void SongListPanel::setRemoteLibrary(std::shared_ptr<const LibraryState> library) {
  remoteLibrary = std::move(library);
  libraryCatalogPanel.onRemoteAction = onRemoteAction;
  refreshFromStorage();
}

void SongListPanel::openCloudLibrary() {
  openLocalLibrary();
  libraryCatalogPanel.openCloud();
}
