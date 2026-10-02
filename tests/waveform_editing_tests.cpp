#include "ui/panels/WaveformBar.h"
#include "PluginProcessor.h"
#include "data/ApplicationDataPaths.h"
#include "data/AssetBootstrapManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void expect(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void expectNear(const double actual, const double expected,
                const std::string &message) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001)
    throw std::runtime_error(message + " (actual " + std::to_string(actual) +
                             ", expected " + std::to_string(expected) + ")");
}

juce::MouseEvent pointerEvent(WaveformBar &waveform,
                              const juce::Point<float> position,
                              const juce::Point<float> start,
                              const bool dragged = false) {
  const juce::Time now(1000000);
  // Alt selects the existing free-drag mode so exact source-range assertions
  // do not also depend on the current song's musical snap setting.
  return {juce::Desktop::getInstance().getMainMouseSource(), position,
          juce::ModifierKeys::leftButtonModifier |
              juce::ModifierKeys::altModifier,
          1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &waveform, &waveform,
          now, start, now, 1, dragged};
}

SongArrangementPartSnapshot original(const char *id, const double start,
                                     const double end) {
  SongArrangementPartSnapshot part;
  part.id = id;
  part.name = id;
  part.startSec = start;
  part.endSec = end;
  return part;
}
} // namespace

// Access hit geometry and published presentation state. Copy, movement,
// resizing, selection and history all use production input handlers.
struct WaveformEditingTestAccess {
  static const SongArrangementSnapshot &snapshot(const WaveformBar &waveform) {
    return waveform.arrangement;
  }

  static juce::String selectedId(const WaveformBar &waveform) {
    expect(juce::isPositiveAndBelow(waveform.selectedPartIndex,
                                   static_cast<int>(waveform.arrangement.parts.size())),
           "an arrangement part is selected");
    return waveform.arrangement.parts[static_cast<size_t>(waveform.selectedPartIndex)].id;
  }

  static juce::Rectangle<float> waveBody(const WaveformBar &waveform) {
    return waveform.getMainWaveBodyBounds(
        waveform.getMainWaveBounds(waveform.getLocalBounds().toFloat()));
  }

  static juce::Rectangle<float> ruler(const WaveformBar &waveform) {
    return waveform.getSeekRulerBounds(
        waveform.getMainWaveBounds(waveform.getLocalBounds().toFloat()));
  }

  static void expectInsertionPointerStarted(const WaveformBar &waveform) {
    expect(waveform.dragMode == WaveformBar::DragMode::seek,
           "dragging the ruler uses the real insertion-pointer seek handler");
    expect(waveform.sectionInsertionAnchorValid,
           "the ruler gesture establishes a section insertion anchor");
  }

  static void publishArrangement(WaveformBar &waveform,
                                 const SongArrangementSnapshot &snapshot) {
    // Publish a partial annotation snapshot through the same callback used
    // after adding a section, without displaying the section picker in CI.
    waveform.setArrangement(snapshot, true);
    waveform.notifyArrangementChanged(true);
  }

  static juce::Point<float> pointAt(const WaveformBar &waveform,
                                   const double seconds,
                                   const float y) {
    return {waveform.sourceSecondsToWindowX(waveBody(waveform), seconds), y};
  }

  static WaveformBar::PartOverlayBounds overlay(WaveformBar &waveform,
                                                const juce::String &id) {
    waveform.rebuildPartOverlayBounds(waveBody(waveform));
    const int index = waveform.findPartIndexById(id);
    for (const auto &bounds : waveform.partOverlayBounds)
      if (bounds.partIndex == index)
        return bounds;
    throw std::runtime_error("missing arrangement hit bounds: " + id.toStdString());
  }

  static juce::Point<float> bodyPoint(WaveformBar &waveform,
                                     const juce::String &id) {
    const auto bounds = overlay(waveform, id).body;
    // The edit toolbar is near the top; the lower body starts a part move.
    return {bounds.getCentreX(), bounds.getBottom() - 20.0f};
  }

  static juce::Point<float> rightHandle(WaveformBar &waveform,
                                       const juce::String &id) {
    const auto bounds = overlay(waveform, id);
    return {bounds.rightHandle.getCentreX(), bounds.body.getBottom() - 20.0f};
  }

  static void expectMoveStarted(const WaveformBar &waveform) {
    expect(waveform.dragMode == WaveformBar::DragMode::movePart,
           "mouse-down in the section body starts the real move handler");
  }

  static void expectResizeStarted(const WaveformBar &waveform) {
    expect(waveform.dragMode == WaveformBar::DragMode::resizePartRight,
           "mouse-down on the right handle starts the real resize handler");
  }

  static double mappedSource(const WaveformBar &waveform, const double seconds) {
    double source = -1.0;
    expect(waveform.mapArrangementSecondsToSourceSeconds(seconds, 30.0, source),
           "arrangement position maps to source audio");
    return source;
  }

  static void saveRange(WaveformBar &waveform, const juce::String &id,
                        const double start, const double end) {
    const int index = waveform.findPartIndexById(id);
    expect(index >= 0, "the range editor target exists");
    waveform.snapModeBox.setSelectedId(
        static_cast<int>(WaveformBar::SnapMode::Free), juce::sendNotificationSync);
    // Exercise the same save operation as the dialog without creating a native
    // window: console CI may have no display for AlertWindow to attach to.
    waveform.applyArrangementPartRange(id, start, end);
  }
};

namespace {
struct Fixture {
  static constexpr double sourceLength = 30.0;
  double timelineLength = sourceLength;
  int commits = 0;
  SongArrangementSnapshot published;
  WaveformBar waveform;

  explicit Fixture(const bool utility = false) {
    waveform.setTransportProviders([] { return 0.0; },
                                   [this] { return timelineLength; },
                                   [] { return false; });
    waveform.setSongSourceLengthProvider([] { return sourceLength; });
    waveform.setSongRhythm(120.0, "4/4");
    waveform.setWaveformBins(std::vector<float>(512, 0.5f));
    SongArrangementSnapshot initial;
    initial.parts = {original("Intro", 0.0, 10.0),
                     original("A", 10.0, 20.0),
                     original("B", 20.0, 30.0)};
    initial.parts[1].utilityCue = utility;
    waveform.setArrangement(initial, false);
    waveform.setSize(1200, 500);
    waveform.setOnArrangementChanged(
        [this](const SongArrangementSnapshot &snapshot, const bool) {
          published = snapshot;
          ++commits;
          double inserted = 0.0;
          timelineLength = sourceLength;
          for (const auto &part : snapshot.parts) {
            timelineLength = std::max(timelineLength, part.endSec);
            if (part.sourcePartId.isNotEmpty())
              inserted += part.endSec - part.startSec;
          }
          timelineLength = std::max(timelineLength, sourceLength + inserted);
        });
    waveform.triggerEditToggleFromMidi();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  }

  SongArrangementPartSnapshot part(const juce::String &id) const {
    for (const auto &candidate : WaveformEditingTestAccess::snapshot(waveform).parts)
      if (candidate.id == id)
        return candidate;
    throw std::runtime_error("missing section: " + id.toStdString());
  }

  void click(const juce::Point<float> point) {
    waveform.mouseDown(pointerEvent(waveform, point, point));
    waveform.mouseUp(pointerEvent(waveform, point, point));
  }

  juce::String duplicate(const juce::String &id) {
    const auto bounds = WaveformEditingTestAccess::overlay(waveform, id);
    expect(bounds.showsDuplicateButton, "Copy is available in the section toolbar");
    const auto oldCount = WaveformEditingTestAccess::snapshot(waveform).parts.size();
    click(bounds.duplicateButton.getCentre());
    expect(WaveformEditingTestAccess::snapshot(waveform).parts.size() == oldCount + 1,
           "the Copy button adds one section");
    const auto copyId = WaveformEditingTestAccess::selectedId(waveform);
    expect(copyId != id, "the new copy becomes selected");
    return copyId;
  }

  bool command(const char key) {
    return waveform.keyPressed(
        juce::KeyPress(key, juce::ModifierKeys::commandModifier, 0));
  }

  void select(const juce::String &id) {
    click(WaveformEditingTestAccess::bodyPoint(waveform, id));
    expect(WaveformEditingTestAccess::selectedId(waveform) == id,
           "click selects the requested section");
  }

  juce::String pasteCopyOf(const juce::String &id) {
    select(id);
    expect(command('C'), "copy shortcut is handled");
    expect(command('V'), "paste shortcut is handled");
    return WaveformEditingTestAccess::selectedId(waveform);
  }

  void move(const juce::String &id, const double targetStart) {
    const auto section = part(id);
    const auto start = WaveformEditingTestAccess::bodyPoint(waveform, id);
    const auto body = WaveformEditingTestAccess::waveBody(waveform);
    const auto delta = (targetStart - section.startSec) / timelineLength * body.getWidth();
    const auto end = start.translated(static_cast<float>(delta), 0.0f);
    waveform.mouseDown(pointerEvent(waveform, start, start));
    WaveformEditingTestAccess::expectMoveStarted(waveform);
    waveform.mouseDrag(pointerEvent(waveform, end, start, true));
    waveform.mouseUp(pointerEvent(waveform, end, start, true));
  }

  void resizeRight(const juce::String &id, const double targetEnd) {
    const auto section = part(id);
    const auto start = WaveformEditingTestAccess::rightHandle(waveform, id);
    const auto body = WaveformEditingTestAccess::waveBody(waveform);
    const auto delta = (targetEnd - section.endSec) / timelineLength * body.getWidth();
    const auto end = start.translated(static_cast<float>(delta), 0.0f);
    waveform.mouseDown(pointerEvent(waveform, start, start));
    WaveformEditingTestAccess::expectResizeStarted(waveform);
    waveform.mouseDrag(pointerEvent(waveform, end, start, true));
    waveform.mouseUp(pointerEvent(waveform, end, start, true));
  }

  void editRange(const juce::String &id, const double start, const double end) {
    WaveformEditingTestAccess::saveRange(waveform, id, start, end);
  }

  void expectPosition(const juce::String &id, const double start,
                      const double end) const {
    const auto section = part(id);
    expectNear(section.startSec, start, id.toStdString() + " arrangement start");
    expectNear(section.endSec, end, id.toStdString() + " arrangement end");
  }

  void expectSource(const juce::String &id, const double start,
                    const double end) const {
    const auto section = part(id);
    expect(section.sourcePartId == "A", "copy retains the original source ID");
    expectNear(section.sourceStartSec, start, "copy source start remains anchored");
    expectNear(section.sourceEndSec, end, "copy source end remains anchored");
    expectNear(WaveformEditingTestAccess::mappedSource(
                   waveform, (section.startSec + section.endSec) * 0.5),
               (start + end) * 0.5, "waveform samples the expected original audio");
  }
};

void writeSyntheticWav(const juce::File &source, const int frameCount) {
  auto stream = std::unique_ptr<juce::OutputStream>(source.createOutputStream());
  juce::WavAudioFormat wav;
  auto writer = wav.createWriterFor(
      stream, juce::AudioFormatWriterOptions{}
                  .withSampleRate(44100.0)
                  .withNumChannels(1)
                  .withBitsPerSample(16));
  expect(writer != nullptr, "the isolated synthetic WAV writer opens");
  juce::AudioBuffer<float> samples(1, 4096);
  samples.clear();
  for (int frame = 0; frame < samples.getNumSamples(); ++frame)
    samples.setSample(0, frame, 0.1f);
  int remaining = frameCount;
  while (remaining > 0) {
    const int frames = std::min(remaining, samples.getNumSamples());
    expect(writer->writeFromAudioSampleBuffer(samples, 0, frames),
           "the isolated source audio is written");
    remaining -= frames;
  }
}

juce::File writeProcessorSongSource() {
  const auto root = ApplicationDataPaths::isolatedTestRoot();
  // Processor startup preloads pads asynchronously. Satisfy its local asset
  // check before constructing it, so this transport test cannot download a
  // pack or depend on network access.
  const auto pads = AssetBootstrapManager::getPadsPackDirectory();
  expect(pads.isAChildOf(root), "processor test pad assets are isolated");
  const auto firstPad = pads.getChildFile("fixture-pad-0.wav");
  if (!firstPad.existsAsFile()) {
    writeSyntheticWav(firstPad, 2205);
    for (int index = 1; index < 20; ++index)
      expect(firstPad.copyFileTo(pads.getChildFile(
                 "fixture-pad-" + juce::String(index) + ".wav")),
             "the temporary local pads pack is pre-seeded");
  }
  const auto source = root.getChildFile(
      "waveform-transport-" + juce::Uuid().toString() + ".wav");
  writeSyntheticWav(source, static_cast<int>(Fixture::sourceLength * 44100.0));
  return source;
}

// Use the public processor loading API so duration comes from decoded audio,
// rather than a mock that could reproduce the same incorrect end calculation.
// The CTest wrapper and isolated storage translation units keep constructor
// migrations, background decode cache and app settings in the temporary root.
struct ProcessorFixture {
  juce::File source = writeProcessorSongSource();
  MTControlsAudioProcessor processor;
  Fixture edit;

  ProcessorFixture() {
    juce::StringArray paths;
    paths.add(source.getFullPathName());
    expect(processor.loadSongStems(paths, juce::Uuid().toString(),
                                   "Waveform duration regression", "C", 120.0),
           "the real processor loads the isolated source song");
    expectNear(processor.getLoadedSongLengthSeconds(), Fixture::sourceLength,
               "decoded source duration is 30 seconds");
    processor.setSongArrangement(
        WaveformEditingTestAccess::snapshot(edit.waveform), false);
    processor.setArrangementEditingActive(true);
    edit.waveform.setTransportProviders(
        [this] { return processor.getSongCurrentPositionSeconds(); },
        [this] { return processor.getSongLengthSeconds(); },
        [this] { return processor.isSongPlaying(); });
    edit.waveform.setSongSourceLengthProvider(
        [this] { return processor.getLoadedSongLengthSeconds(); });
    edit.waveform.setSongEndCustomizedProvider(
        [this] { return processor.isSongEndCustomized(); });
    edit.waveform.setSongWindowProviders(
        [this] { return processor.getSongStartSeconds(); },
        [this] { return processor.getSongEndSeconds(); });
    edit.waveform.setSeekCallback(
        [this](const double seconds) { processor.seekSongTo(seconds); });
    edit.waveform.setOnArrangementChanged(
        [this](const SongArrangementSnapshot &snapshot, const bool custom) {
          edit.published = snapshot;
          ++edit.commits;
          processor.setSongArrangement(snapshot, custom);
          edit.timelineLength = processor.getSongLengthSeconds();
        });
  }

  void publishPartialAnnotation() {
    SongArrangementSnapshot partial;
    partial.parts = {original("A", 5.0, 10.0)};
    WaveformEditingTestAccess::publishArrangement(edit.waveform, partial);
  }

  void dragInsertionPointer(const double startSeconds,
                            const double endSeconds) {
    const auto ruler = WaveformEditingTestAccess::ruler(edit.waveform);
    expect(ruler.getWidth() > 2.0f && ruler.getHeight() > 2.0f,
           "the edit ruler is available for insertion-pointer input");
    const auto start = WaveformEditingTestAccess::pointAt(
        edit.waveform, startSeconds, ruler.getCentreY());
    const auto end = WaveformEditingTestAccess::pointAt(
        edit.waveform, endSeconds, ruler.getCentreY());
    const int commitsBefore = edit.commits;
    edit.waveform.mouseDown(pointerEvent(edit.waveform, start, start));
    WaveformEditingTestAccess::expectInsertionPointerStarted(edit.waveform);
    edit.waveform.mouseDrag(pointerEvent(edit.waveform, end, start, true));
    edit.waveform.mouseUp(pointerEvent(edit.waveform, end, start, true));
    expect(edit.commits == commitsBefore,
           "moving the insertion pointer does not publish an arrangement edit");
  }
};

void testPartialAnnotationsPreserveRestoredFullSongEnd() {
  ProcessorFixture f;
  // Restoring a saved window calls this even if its end matches full audio.
  f.processor.setSongEndSeconds(30.0);
  expect(f.processor.isSongEndCustomized(), "the restored Fin flag is explicit");
  f.dragInsertionPointer(2.0, 8.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "moving the insertion pointer preserves Fin before adding a section");
  f.publishPartialAnnotation();
  expectNear(f.processor.getSongLengthSeconds(), 30.0,
             "a partial annotation does not shorten decoded song duration");
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "a first partial section must not replace restored full-song Fin");
  f.dragInsertionPointer(17.0, 24.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "moving the insertion pointer after adding a section preserves Fin");
  expectNear(f.processor.getSongCurrentPositionSeconds(), 24.0,
             "the insertion pointer can seek beyond the last annotation");
}

void testPartialAnnotationsPreserveDefaultFullSongEnd() {
  ProcessorFixture f;
  expect(!f.processor.isSongEndCustomized(), "a fresh song has default Fin");
  f.publishPartialAnnotation();
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "partial annotations preserve the default full-song Fin");
  f.dragInsertionPointer(18.0, 26.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "default Fin stays at the source end after ruler seeking");
  expectNear(f.processor.getSongCurrentPositionSeconds(), 26.0,
             "a default song can seek past the final annotation");
}

void testPartialAnnotationsPreserveExplicitWindowTrim() {
  ProcessorFixture f;
  f.processor.setSongEndSeconds(20.0);
  f.publishPartialAnnotation();
  expectNear(f.processor.getSongEndSeconds(), 20.0,
             "publishing partial annotations preserves an explicit earlier Fin");
  f.dragInsertionPointer(12.0, 25.0);
  expectNear(f.processor.getSongEndSeconds(), 20.0,
             "ruler seeking preserves the explicit window trim");
  expectNear(f.processor.getSongCurrentPositionSeconds(), 20.0,
             "the explicit trim still limits the insertion pointer and seek");
}

void testMovingAndTrimmingAnnotationPreservesSongEnd() {
  ProcessorFixture f;
  f.processor.setSongEndSeconds(30.0);
  f.publishPartialAnnotation();
  f.edit.move("A", 1.0);
  f.edit.expectPosition("A", 1.0, 6.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "moving an annotation earlier never moves Fin to its right edge");
  f.edit.resizeRight("A", 4.0);
  f.edit.expectPosition("A", 1.0, 4.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "trimming an annotation never trims full-song Fin");
  expect(f.edit.command('Z'), "annotation resize can be undone");
  f.edit.expectPosition("A", 1.0, 6.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "annotation undo also preserves full-song Fin");
}

void testCopiedInsertionExtendsAndRestoresFullSongEnd() {
  ProcessorFixture f;
  f.processor.setSongEndSeconds(30.0);
  const auto copy = f.edit.duplicate("A");
  expectNear(f.processor.getSongLengthSeconds(), 40.0,
             "a copied ten-second insertion extends the real arrange duration");
  expectNear(f.processor.getSongEndSeconds(), 40.0,
             "a restored full Fin follows the copied insertion extension");
  f.edit.move(copy, 0.0);
  expectNear(f.processor.getSongEndSeconds(), 40.0,
             "relocating an inserted copy keeps the extended Fin");
  f.edit.select(copy);
  expect(f.edit.waveform.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),
         "Delete removes the selected insertion through the real editor handler");
  expectNear(f.processor.getSongLengthSeconds(), 30.0,
             "removing the copied insertion restores decoded source duration");
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "removing the insertion returns following Fin to full source end");
}

void testMoveLeftKeepsSourceAndHistory() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.expectPosition(copy, 20.0, 30.0);
  f.expectSource(copy, 10.0, 20.0);
  const auto before = f.commits;
  f.move(copy, 0.0);
  expect(f.commits == before + 1, "a completed move publishes exactly once");
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
  f.expectPosition("B", 30.0, 40.0);
  expectNear(f.timelineLength, 40.0, "moving a copy does not change song duration");

  expect(f.command('Z'), "move undo is available");
  f.expectPosition(copy, 20.0, 30.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 10.0, 20.0);
  expect(f.command('Y'), "move redo is available");
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
}

void testReturningDragDoesNotAccumulateRipple() {
  Fixture f;
  const auto copy = f.duplicate("A");
  const auto start = WaveformEditingTestAccess::bodyPoint(f.waveform, copy);
  const auto left = WaveformEditingTestAccess::pointAt(f.waveform, 5.0, start.y);
  const auto middle = WaveformEditingTestAccess::pointAt(f.waveform, 15.0, start.y);
  const int before = f.commits;
  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  WaveformEditingTestAccess::expectMoveStarted(f.waveform);
  for (const auto point : {left, middle, left, start}) {
    f.waveform.mouseDrag(pointerEvent(f.waveform, point, start, true));
    f.expectSource(copy, 10.0, 20.0);
    const auto section = f.part(copy);
    expectNear(section.endSec - section.startSec, 10.0,
               "every drag preview preserves the full copy duration");
    expect(f.commits == before, "drag previews do not publish intermediate arrangements");
  }
  f.waveform.mouseUp(pointerEvent(f.waveform, start, start, true));
  f.expectPosition(copy, 20.0, 30.0);
  f.expectPosition("Intro", 0.0, 10.0);
  f.expectPosition("A", 10.0, 20.0);
  f.expectPosition("B", 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
}

void testCopyAgainAfterMoving() {
  for (const bool useClipboard : {false, true}) {
    Fixture f;
    const auto copy = f.duplicate("A");
    f.move(copy, 0.0);
    const auto nested = useClipboard ? f.pasteCopyOf(copy) : f.duplicate(copy);
    f.expectPosition(nested, 10.0, 20.0);
    f.expectSource(nested, 10.0, 20.0);
    f.expectSource(copy, 10.0, 20.0);
    f.expectPosition("A", 30.0, 40.0);
    expectNear(f.timelineLength, 50.0, "copying after relocation inserts one duration");
  }
}

void testTrimAndExpandRestoresGap() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.move(copy, 0.0);
  f.resizeRight(copy, 5.0);
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("Intro", 5.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  f.expectPosition("B", 25.0, 35.0);
  expectNear(f.timelineLength, 35.0, "trimming closes the removed insertion gap");
  f.resizeRight(copy, 10.0);
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
  f.expectPosition("B", 30.0, 40.0);
  expectNear(f.timelineLength, 40.0, "expanding reopens the insertion gap without overlap");
  expect(f.command('Z'), "expansion undo is available");
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  expect(f.command('Y'), "expansion redo is available");
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
}

void testCopyTrimmedSectionAndMove() {
  for (const bool useClipboard : {false, true}) {
    Fixture f;
    const auto copy = f.duplicate("A");
    f.resizeRight(copy, 25.0);
    f.expectSource(copy, 10.0, 15.0);
    f.expectPosition("B", 25.0, 35.0);
    f.move(copy, 0.0);
    f.expectPosition(copy, 0.0, 5.0);
    f.expectSource(copy, 10.0, 15.0);
    const auto nested = useClipboard ? f.pasteCopyOf(copy) : f.duplicate(copy);
    f.expectPosition(nested, 5.0, 10.0);
    f.expectSource(nested, 10.0, 15.0);
    f.expectPosition("A", 20.0, 30.0);
  }
}

void testTailCopyCanRegainItsFullLength() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.move(copy, 30.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  f.resizeRight(copy, 35.0);
  f.expectPosition(copy, 30.0, 35.0);
  f.expectSource(copy, 10.0, 15.0);
  expectNear(f.timelineLength, 35.0, "trimming the tail shortens arrange length");
  f.resizeRight(copy, 40.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  expectNear(f.timelineLength, 40.0,
             "a tail copy expands beyond the shortened arrange length");
}

void testUtilityCopyRetainsGuideTimingIdentity() {
  for (const bool useClipboard : {false, true}) {
    Fixture f(true);
    const auto copy = useClipboard ? f.pasteCopyOf("A") : f.duplicate("A");
    expect(f.part(copy).utilityCue,
           "Copy and keyboard paste preserve utility guide announcements at entry");
  }
}

void testRangeDialogPreservesSourceAndRipple() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.move(copy, 0.0);
  const int before = f.commits;
  f.editRange(copy, 0.0, 5.0);
  expect(f.commits == before + 1, "saving a range publishes exactly one change");
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("Intro", 5.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  f.expectPosition("B", 25.0, 35.0);
  f.editRange(copy, 0.0, 10.0);
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
  f.expectPosition("B", 30.0, 40.0);
  expect(f.command('Z'), "range dialog edit can be undone");
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  expect(f.command('Y'), "range dialog edit can be redone");
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);

  f.editRange(copy, 30.0, 40.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 0.0, 10.0);
  f.expectPosition("A", 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  expectNear(f.timelineLength, 40.0,
             "moving both range endpoints preserves insertion duration");
  f.editRange(copy, 30.0, 35.0);
  f.expectPosition(copy, 30.0, 35.0);
  f.expectSource(copy, 10.0, 15.0);
  expectNear(f.timelineLength, 35.0, "range edit trims the copied tail");
  f.editRange(copy, 30.0, 40.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  expectNear(f.timelineLength, 40.0,
             "range edit can re-extend the copied tail beyond the old end");
}
} // namespace

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  int failures = 0;
  const auto run = [&failures](const char *name, auto test) {
    try {
      test();
      std::cout << "PASS: " << name << '\n';
    } catch (const std::exception &error) {
      ++failures;
      std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
    }
  };
  run("move left preserves source and undo/redo", testMoveLeftKeepsSourceAndHistory);
  run("returning drag preserves duration and ripple", testReturningDragDoesNotAccumulateRipple);
  run("copy again after moving", testCopyAgainAfterMoving);
  run("trim and expand reopen insertion gap", testTrimAndExpandRestoresGap);
  run("copy trimmed section and move", testCopyTrimmedSectionAndMove);
  run("tail copy regains full length", testTailCopyCanRegainItsFullLength);
  run("utility copy preserves guide timing", testUtilityCopyRetainsGuideTimingIdentity);
  run("range dialog preserves source and ripple", testRangeDialogPreservesSourceAndRipple);
  run("partial annotations preserve restored full Fin",
      testPartialAnnotationsPreserveRestoredFullSongEnd);
  run("partial annotations preserve default full Fin",
      testPartialAnnotationsPreserveDefaultFullSongEnd);
  run("partial annotations preserve explicit trimmed Fin",
      testPartialAnnotationsPreserveExplicitWindowTrim);
  run("moving and trimming annotations preserve Fin",
      testMovingAndTrimmingAnnotationPreservesSongEnd);
  run("copied insertion extends and restores following Fin",
      testCopiedInsertionExtendsAndRestoresFullSongEnd);
  return failures == 0 ? 0 : 1;
}
