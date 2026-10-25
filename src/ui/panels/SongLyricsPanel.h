#pragma once

#include "../../data/SongLyrics.h"
#include "../UiCommon.h"
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <optional>

// Edits a private draft. Only Listo applies it; closing never writes a song.
// The caller owns the panel and defers destruction from either callback.
class SongLyricsPanel final : public juce::Component, private juce::Timer {
public:
  using Apply = std::function<void(std::optional<SongLyricsDocument>)>;
  SongLyricsPanel(juce::String songTitle,
                  std::optional<SongLyricsDocument> document, bool readOnly,
                  Apply onApply, std::function<void()> onClose);
  ~SongLyricsPanel() override;
  const SongLyricsDocument &getDocument() const { return draft; }
  void paint(juce::Graphics &) override;
  void resized() override;
  void parentSizeChanged() override;
  bool keyPressed(const juce::KeyPress &) override;

private:
  friend struct SongLyricsPanelTestAccess;
  class Preview;
  void timerCallback() override;
  void selectBlock(int index);
  bool syncActiveBlock();
  void updateBlockChoices();
  void refreshPreview();
  void addBlock();
  void removeBlock();
  void selectPosition(int block, int line, int position, const juce::String &chordId);
  void applyChord();
  void moveChord(int delta);
  void deleteChord();
  SongLyricsChord *selectedChord();
  void updateChordActions();
  void finish(bool apply);

  SongLyricsDocument draft;
  bool readOnly = false, updating = false, previewMode = false, finished = false;
  bool activeBlockLoaded = false;
  int activeBlock = 0, selectedLine = -1, selectedPosition = 0;
  juce::String selectedChordId;
  Apply onApply;
  std::function<void()> onClose;
  juce::Label heading, songLabel, hint;
  AdaptiveComboBox blockBox;
  juce::TextEditor blockName, bodyEditor, chordEditor;
  juce::TextButton addBlockButton, removeBlockButton, doneButton;
  juce::TextButton editButton, previewButton, chordApplyButton;
  juce::TextButton chordLeftButton, chordRightButton, chordDeleteButton;
  HeaderIconButton closeButton{"songLyricsClose", IconGlyph::Close};
  juce::Viewport previewViewport;
  std::unique_ptr<Preview> preview;
  juce::Rectangle<int> cardBounds;
  int lastKeyboardInset = -1;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongLyricsPanel)
};
